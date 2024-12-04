package sysfail

/*
#cgo CFLAGS: -I../../include
#cgo LDFLAGS: -L. -lsysfail

#include "sysfail.h"
#include <stdlib.h>

void set_poll_itvl(sysfail_thread_discovery_t* disc, uint32_t value) {
    disc->poll_itvl_usec = value;
}

void stop_sysfail(sysfail_session_t* session) {
    if (session) session->stop(session);
}

void add_this_thread(sysfail_session_t* session) {
    if (session) session->add_this_thread(session);
}

void remove_this_thread(sysfail_session_t* session) {
    if (session) session->remove_this_thread(session);
}

void add_thread(sysfail_session_t* session, sysfail_tid_t tid) {
    if (session) session->add_thread(session, tid);
}

void remove_thread(sysfail_session_t* session, sysfail_tid_t tid) {
    if (session) session->remove_thread(session, tid);
}

void discover_threads(sysfail_session_t* session) {
    if (session) session->discover_threads(session);
}

void set_error_weights(sysfail_outcome_t* outcome, sysfail_error_wt_t* weights) {
	outcome->error_wts = weights;
}

*/
import "C"
import (
	"unsafe"
)

type Probability struct {
	P         float64
	AfterBias float64
}

type ErrorWeight struct {
	Nerror int
	Weight float64
}

type Outcome struct {
	Fail         Probability
	Delay        Probability
	MaxDelayUsec uint32
	Ctx          unsafe.Pointer
	Eligible     func(ctx unsafe.Pointer, regs *C.greg_t) int
	NumErrors    uint32
	ErrorWeights []ErrorWeight
}

type ThreadDiscoveryStrategy uint32

const (
	ThreadDiscoveryNone ThreadDiscoveryStrategy = iota
	ThreadDiscoveryPoll
)

type ThreadDiscoveryConfig struct {
	PollIntervalUsec uint32
}

type SyscallOutcome struct {
	Syscall int
	Outcome Outcome
}

type Plan struct {
	Strategy        ThreadDiscoveryStrategy
	Config          ThreadDiscoveryConfig
	Ctx             unsafe.Pointer
	Selector        func(ctx unsafe.Pointer, tid C.sysfail_tid_t) int
	SyscallOutcomes []*SyscallOutcome
}

type Session struct {
	session *C.sysfail_session_t
	plan    *C.sysfail_plan_t
}

// NewPlan creates a new plan for failure injection.
func NewPlan(strategy ThreadDiscoveryStrategy, pollIntervalUsec uint32, syscallOutcomes []*SyscallOutcome) *Plan {
	return &Plan{
		Strategy: strategy,
		Config: ThreadDiscoveryConfig{
			PollIntervalUsec: pollIntervalUsec,
		},
		SyscallOutcomes: syscallOutcomes,
		Selector:        nil, // Use appropriate selector function
		Ctx:             nil, // User data
	}
}

// StartSession starts a new failure injection session.
func StartSession(plan *Plan) *Session {
	var cOutcomes *C.sysfail_syscall_outcome_t
	if len(plan.SyscallOutcomes) > 0 {
		cOutcomes = (*C.sysfail_syscall_outcome_t)(C.malloc(C.size_t(len(plan.SyscallOutcomes) * int(unsafe.Sizeof(C.sysfail_syscall_outcome_t{})))))
		var prev *C.sysfail_syscall_outcome_t = nil
		for i, outcome := range plan.SyscallOutcomes {
			curr := (*C.sysfail_syscall_outcome_t)(unsafe.Pointer(uintptr(unsafe.Pointer(cOutcomes)) + uintptr(i)*unsafe.Sizeof(*cOutcomes)))
			curr.syscall = C.int(outcome.Syscall)
			curr.outcome.fail.p = C.double(outcome.Outcome.Fail.P)
			curr.outcome.fail.after_bias = C.double(outcome.Outcome.Fail.AfterBias)
			curr.outcome.delay.p = C.double(outcome.Outcome.Delay.P)
			curr.outcome.delay.after_bias = C.double(outcome.Outcome.Delay.AfterBias)
			curr.outcome.max_delay_usec = C.uint(outcome.Outcome.MaxDelayUsec)
			curr.outcome.ctx = outcome.Outcome.Ctx
			// curr.outcome.eligible = // still need to convert the function pointer if used
			curr.outcome.num_errors = C.uint(outcome.Outcome.NumErrors)
			if outcome.Outcome.NumErrors > 0 {
				ewSize := int(unsafe.Sizeof(C.sysfail_error_wt_t{}))
				var cErrors *C.sysfail_error_wt_t
				cErrors = (*C.sysfail_error_wt_t)(C.malloc(C.size_t(outcome.Outcome.NumErrors) * C.size_t(ewSize)))
				for j, ew := range outcome.Outcome.ErrorWeights {
					ewPtr := (*C.sysfail_error_wt_t)(unsafe.Pointer(uintptr(unsafe.Pointer(cErrors)) + uintptr(j)*unsafe.Sizeof(*cErrors)))
					ewPtr.nerror = C.int(ew.Nerror)
					ewPtr.weight = C.double(ew.Weight)
				}
				C.set_error_weights(&curr.outcome, cErrors)
			}
			if prev != nil {
				prev.next = curr
			}
			prev = curr
		}
	}

	cPlan := &C.sysfail_plan_t{
		strategy:         C.sysfail_thread_discovery_strategy_t(plan.Strategy),
		ctx:              plan.Ctx,
		selector:         nil, // Use appropriate selector function
		syscall_outcomes: cOutcomes,
	}

	C.set_poll_itvl(&cPlan.config, C.uint(plan.Config.PollIntervalUsec))

	session := C.sysfail_start(cPlan)
	return &Session{session: session}
}

// Stop stops the failure injection session.
func (s *Session) Stop() {
	C.stop_sysfail(s.session)
}

// AddThisThread enables failure injection for the calling thread.
func (s *Session) AddThisThread() {
	C.add_this_thread(s.session)
}

// RemoveThisThread disables failure injection for the calling thread.
func (s *Session) RemoveThisThread() {
	C.remove_this_thread(s.session)
}

// AddThread enables failure injection for the specified thread.
func (s *Session) AddThread(tid int) {
	C.add_thread(s.session, C.sysfail_tid_t(tid))
}

// RemoveThread disables failure injection for the specified thread.
func (s *Session) RemoveThread(tid int) {
	C.remove_thread(s.session, C.sysfail_tid_t(tid))
}

// DiscoverThreads discovers threads for failure injection.
func (s *Session) DiscoverThreads() {
	C.discover_threads(s.session)
}
