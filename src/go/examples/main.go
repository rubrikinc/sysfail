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

package main

import (
	"fmt"
	"os"
	"strconv"
	"syscall"

	"github.com/rubrikinc/sysfail"
)

func mkSysfailSession(
	syscallNum int,
	errno syscall.Errno,
) *sysfail.Session {
	if os.Getenv("NO_SYSFAIL") == "y" {
		return nil
	}

	outcome := sysfail.Outcome{
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
			Type:     sysfail.AlwaysEligible,
		},
		NumErrors: 1,
		ErrorWeights: []sysfail.ErrorWeight{
			{
				Nerror: int(errno),
				Weight: 1.0,
			},
		},
	}

	syscallOutcome := &sysfail.SyscallOutcome{
		Syscall: syscallNum,
		Outcome: outcome,
	}

	outcomeList := []*sysfail.SyscallOutcome{syscallOutcome}
	plan, err := sysfail.NewPlan(
		sysfail.ThreadDiscoveryPoll,
		1000,
		outcomeList,
		nil,
		nil,
	)
	if err != nil {
		return nil
	}
	session, err := sysfail.StartSession(plan)
	if err != nil {
		return nil
	} else {
		return session
	}
}

func enableSysfailAndExit(status uintptr) syscall.Errno {
	errno := syscall.ESRCH
	errnoStrOverride := os.Getenv("EXIT_ERRNO")
	if errnoStrOverride != "" {
		errnoOverride, err := strconv.Atoi(errnoStrOverride)
		if err == nil {
			errno = syscall.Errno(errnoOverride)
		}
	}

	sysfailSession := mkSysfailSession(syscall.SYS_EXIT_GROUP, errno)
	defer func() {
		if sysfailSession != nil {
			sysfailSession.Stop()
		}
	}()

	_, _, errno = syscall.Syscall(syscall.SYS_EXIT_GROUP, status, 0, 0)
	return errno
}

func main() {
	// Test-1: The process would fail (status 123) if sysfail does not block exit_group
	errno := enableSysfailAndExit(123)
	// Run the following commands to observe Test-1's negative case manually:
	//   $ cd <build dir>
	//   $ env LD_LIBRARY_PATH=src NO_SYSFAIL=y ./test/go_ffi
	//   $ echo "Exit status: $?" # Expect 123

	// Test-2: If sysfail does not return correct exit-code, now exit_group isn't
	// blocked, the process would fail with status 100
	if errno != syscall.ESRCH {
		// If an unexpected error is returned, this should fail the test
		fmt.Fprintf(os.Stderr, "Expected ESRCH, got %v\n", errno)
		os.Exit(100)
	}
	// Run the following commands to observe Test-2's negative case manually:
	//   $ cd <build dir>
	//   $ env LD_LIBRARY_PATH=src EXIT_ERRNO=1 ./test/go_ffi
	//   $ echo "Exit status: $?" # Expect 100

	fmt.Println("Test passed!")
}
