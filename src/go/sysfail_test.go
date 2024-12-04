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
	"github.com/rubrikinc/sysfail"
	"io/ioutil"
	"os"
	"syscall"
	"testing"
)

func TestInjectsFailures(t *testing.T) {
	// Create a temporary file
	tmpFile, err := ioutil.TempFile("", "example123")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tmpFile.Name())
	// Inject failures
	os.Setenv("NO_SYSFAIL", "n")
	plan, err := sysfail.NewPlan(
		sysfail.ThreadDiscoveryNone,
		1000,
		[]*sysfail.SyscallOutcome{
			{
				Syscall: syscall.SYS_WRITE,
				Outcome: sysfail.Outcome{
					Fail: sysfail.Probability{
						P: 1.0,
					},
					Delay: sysfail.Probability{
						P: 0.0,
					},
					MaxDelayUsec: 0,
					Ctx:          nil,
					Eligible: sysfail.Eligibility{
						Eligible: nil,
						Type:     sysfail.EligibleFuncStdOutStdErr,
					},
					NumErrors: 1,
					ErrorWeights: []sysfail.ErrorWeight{
						{
							Nerror: int(syscall.EIO),
							Weight: 1,
						},
					},
				},
			},
		}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	session, err := sysfail.StartSession(plan)
	if err != nil {
		t.Fatal(err)
	}
	// Write to file
	failures := 0
	for i := 0; i < 100; i++ {
		_, err := tmpFile.Write([]byte("test"))
		if err != nil {
			failures++
		}
	}
	if failures != 100 {
		t.Fatal("Expected 100 failures, got:", failures)
	}
	session.Stop()
}
