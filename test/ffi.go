package main

/*
#cgo CFLAGS: -I../include
#cgo LDFLAGS: -lsysfail

#include <stdlib.h>
#include <sysfail.h>

void set_poll_itvl(sysfail_thread_discovery_t* disc, uint32_t value) {
    disc->poll_itvl_usec = value;
}

void stop_sysfail(sysfail_session_t* session) {
	if (session) session->stop(session);
}

void set_error_weights(sysfail_outcome_t* outcome, sysfail_error_wt_t* weights) {
	outcome->error_wts = weights;
}
*/
import "C"
import (
	"fmt"
	"os"
	"strconv"
	"syscall"
	"unsafe"
)

func p(probability float64) C.sysfail_probability_t {
	return C.sysfail_probability_t{p: C.double(probability), after_bias: 0}
}

func mk_sysfail_session(
	syscall int,
	errno syscall.Errno,
) *C.sysfail_session_t {
	if os.Getenv("NO_SYSFAIL") == "y" {
		return nil
	}

	sz := (C.sizeof_sysfail_syscall_outcome_t)
	outcome := (*C.sysfail_syscall_outcome_t)(C.malloc(C.ulong(sz)))
	defer C.free(unsafe.Pointer(outcome))

	outcome.outcome.fail = p(1.0)
	outcome.outcome.delay = p(0.0)
	outcome.outcome.max_delay_usec = 0
	outcome.outcome.ctx = nil
	outcome.outcome.eligible = nil
	outcome.outcome.num_errors = 1

	errorWt := (*C.sysfail_error_wt_t)(C.malloc(C.ulong(C.sizeof_sysfail_error_wt_t)))
	errorWt.nerror = C.int(errno)
	errorWt.weight = 1.0

	C.set_error_weights(&outcome.outcome, errorWt)

	/* fmt.Printf("Fail probability: %f\n", outcome.fail.p)
	fmt.Printf("Number of errors: %d\n", outcome.num_errors)
	fmt.Printf("Error: errno=%d, weight=%f\n", errorWt.nerror, errorWt.weight)
	*/
	outcome.next = nil
	outcome.syscall = C.int(syscall)

	plan := &C.sysfail_plan_t{
		strategy:         C.sysfail_tdisk_poll,
		ctx:              nil,
		selector:         nil,
		syscall_outcomes: outcome,
	}

	C.set_poll_itvl(&plan.config, 1000)

	return (*C.sysfail_session_t)(C.sysfail_start(plan))
}

func enable_sysfail_and_exit(status uintptr) syscall.Errno {
	errno := syscall.ESRCH
	errnoStrOverride := os.Getenv("EXIT_ERRNO")
	if errnoStrOverride != "" {
		errnoOverride, err := strconv.Atoi(errnoStrOverride)
		if err == nil {
			errno = syscall.Errno(errnoOverride)
		}
	}

	sysfail := mk_sysfail_session(syscall.SYS_EXIT_GROUP, errno)
	defer func() {
		if sysfail != nil {
			C.stop_sysfail(sysfail)
		}
	}()

	_, _, errno = syscall.Syscall(syscall.SYS_EXIT_GROUP, status, 0, 0)
	return errno
}

func main() {
	// *Test-1*
	// The process would fail (status 123) if sysfail does not block exit_group
	errno := enable_sysfail_and_exit(123)
	// Run the following commmands to observe Test-1's negative case manually:
	//   $ cd <build dir>
	//   $ env LD_LIBRARY_PATH=src NO_SYSFAIL=y ./test/go_ffi
	//   $ echo "Exit status: $?" # Expect 123

	// *Test-2*
	// If sysfail does not return correct exit-code, now exit_group isn't
	// blocked, the process would fail with status 100
	if errno != syscall.ESRCH {
		// If an unexpected error is return, this should fail the test
		fmt.Fprintf(os.Stderr, "Expected ESRCH, got %v\n", errno)
		os.Exit(100)
	}
	// Run the following commmands to observe Test-2's negative case manually:
	//   $ cd <build dir>
	//   $ env LD_LIBRARY_PATH=src EXIT_ERRNO=1 ./test/go_ffi
	//   $ echo "Exit status: $?" # Expect 100

	fmt.Println("Test passed!")
}
