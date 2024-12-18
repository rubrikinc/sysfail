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

#ifndef _SYSFAIL_H
#define _SYSFAIL_H

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>

typedef pid_t sysfail_tid_t;

/**
 * Probability values are in the range [0, 1].
 * 0 means never fail or delay, 1 means always fail or delay.
 */
struct {
    // Probability of failure / delay
    double p;

    // Bias of failure, lower values make failure more likely before syscall
    // and higher values make failure more likely after syscall.
    // This is especially useful for testing idempotency properties of the
    // application.
    double after_bias;
} typedef sysfail_probability_t;

/**
 * Error weights are used to determine the mix of errors presented when a
 * syscall is failed.
 */
struct {
    // Error number (errno) to be presented to the application.
    int nerror;
    // Relative weight, higher weight makes the error more likely. This does not
    // affect the probability of failure, only the distribution of errors.
    double weight;
} typedef sysfail_error_wt_t;

/**
 * `sysfail_userdata_t` is the user data that is passed to callbacks
 * (predicates etc). Sysfail does not try to interpret this data.
 */
typedef void sysfail_userdata_t;

/**
 * `sysfail_syscall` is the syscall number.
 */
int sysfail_syscall(const greg_t*);

/**
 * `sysfail_syscall_arg` is the argument number.
 */
greg_t sysfail_syscall_arg(const greg_t*, int);

/**
 * `sysfail_invocation_predicate_t` is the predicate that determines if the
 * syscall is eligible for failure injection.
 */
typedef int(*sysfail_invocation_predicate_t)(sysfail_userdata_t*, const greg_t*);

/**
 * `sysfail_outcome_t` is the outcome of a syscall.
 */
struct {
    // Probability and bias of failure
    sysfail_probability_t fail;
    // Probability and bias of delay
    sysfail_probability_t delay;
    // Maximum delay in microseconds
    uint32_t max_delay_usec;

    // User data
    sysfail_userdata_t* ctx;
    // Eligibility predicate
    sysfail_invocation_predicate_t eligible;

    // Number of errors to form the mix to be drwan from when the syscall fails
    uint32_t num_errors;
    // Actual error codes and their weights
    sysfail_error_wt_t *error_wts;
} typedef sysfail_outcome_t;

/**
 * `sysfail_thread_predicate_t` is the predicate that determines if the thread
 * is eligible for failure injection.
 */
typedef int(*sysfail_thread_predicate_t)(sysfail_userdata_t*, sysfail_tid_t);

/**
 * `sysfail_thread_discovery_strategy_t` is the strategy to discover threads.
 * Test can choose to have the threads automatically discovered or manually
 * add / remove threads that are failure injected.
 * Alternatively, the test can choose to poll the threads using the session API
 * to discover threads at specific points in the execution of test.
 */
enum {
    // No thread discovery strategy, add / remove / discover threads manually
    sysfail_tdisc_none  = 0,
    // Poll to discover threads at regular intervals, manual controls can also
    // be used in conjunction with automatic discovery.
    sysfail_tdisk_poll  = 1,
} typedef sysfail_thread_discovery_strategy_t;

/**
 * `sysfail_thread_discovery_t` is the configuration for thread discovery.
 */
union {
    // Polling interval in microseconds
    uint32_t poll_itvl_usec;
} typedef sysfail_thread_discovery_t;

/**
 * `sysfail_syscall_outcome_t` is the outcome of a syscall.
 */
typedef struct sysfail_syscall_outcome_s sysfail_syscall_outcome_t;
struct sysfail_syscall_outcome_s {
    // Next outcome in the chain (linked list)
    sysfail_syscall_outcome_t* next;

    // Syscall number
    int syscall;

    // Outcome of the syscall
    sysfail_outcome_t outcome;
};

/**
 * `sysfail_plan_t` is the overall plan for failure injection.
 */
struct {
    // Strategy for thread discovery
    sysfail_thread_discovery_strategy_t strategy;
    // Configuration for thread discovery
    sysfail_thread_discovery_t config;

    // User data
    sysfail_userdata_t *ctx;
    // Predicate to determine if the thread is eligible for failure injection
    sysfail_thread_predicate_t selector;

    // Outcomes for syscalls (list)
    sysfail_syscall_outcome_t* syscall_outcomes;
} typedef sysfail_plan_t;

/**
 * `sysfail_session_t` is the session for failure injection.
 */
typedef struct sysfail_session_s sysfail_session_t;
struct sysfail_session_s {
    // (Internal) Sysfail data
    void* data;

    // Stop the session and free all resources (including the session itself)
    // Caller should not use the session after stop.
    // Plan is not freed, caller must free the plan independently.
    void (*stop)(sysfail_session_t*);

    // Enable failure injection on the current (calling) thread
    void (*add_this_thread)(sysfail_session_t*);
    // Disable failure injection on the current (calling) thread
    void (*remove_this_thread)(sysfail_session_t*);

    // Enable failure injection on the thread with the given tid
    void (*add_thread)(sysfail_session_t*, sysfail_tid_t);

    // Disable failure injection on the thread with the given tid
    void (*remove_thread)(sysfail_session_t*, sysfail_tid_t);

    // Discover threads using the strategy configured in the plan
    void (*discover_threads)(sysfail_session_t*);
};

/**
 * Start failure injection in the process. Starts the thread-discovery strategy.
 * Returns the session that can be used to control failure injection.
 * Caller must free the session using the `stop` method (not directly).
 * Caller must free the plan independently.
 *
 * Depending on discovery strategy, all or just the calling thread are
 * presented to the thread selector. Any threads chosen by the selector are
 * failure injected.
 */
sysfail_session_t* sysfail_start(const sysfail_plan_t*);

#endif