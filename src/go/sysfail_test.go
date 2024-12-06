/*
 * Copyright © 2024 Rubrik, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package sysfail_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/rubrikinc/sysfail"
)

func TestInjectsFailures(t *testing.T) {
	// Create a temporary file
	tmpFile, err := ioutil.TempFile("", "example123")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tmpFile.Name())
	tmpFileFd := tmpFile.Fd()
	fmt.Println("Temp file created with fd:", tmpFileFd)
	// Inject failures
	os.Setenv("NO_SYSFAIL", "n")
	plan := sysfail.NewPlan(
		sysfail.ThreadDiscoveryPoll,
		1000,
		[]*sysfail.SyscallOutcome{
			{
				Syscall: syscall.SYS_WRITE,
				Outcome: sysfail.Outcome{
					Fail: sysfail.Probability{
						P: 0.0,
					},
					Delay: sysfail.Probability{
						P: 0.0,
					},
					MaxDelayUsec: 0,
					Ctx:          nil,
					Eligible:     nil,
					NumErrors:    1,
					ErrorWeights: []sysfail.ErrorWeight{
						{
							Nerror: int(syscall.EIO),
							Weight: 1,
						},
					},
				},
			},
		}, func(ctx unsafe.Pointer, tid int) int {
			// type cast the context to an integer
			fd := *(*uintptr)(ctx)
			fmt.Printf("context: %d\n", fd)
			if fd == tmpFileFd {
				return 1
			}
			return 0
		}, unsafe.Pointer(&tmpFileFd))
	session, err := sysfail.StartSession(plan)
	if err != nil {
		t.Fatal(err)
	}
	defer session.Stop()
	fmt.Println("Injecting failures")

	// wait for the session to poll the threads.
	time.Sleep(10 * time.Millisecond)

	// Write to file
	for i := 0; i < 100; i++ {
		_, err := tmpFile.Write([]byte("test"))
		if err != nil {
			t.Logf("Write failed: %v\n", err)
		}
	}

	fmt.Println("Test complete!")
}
