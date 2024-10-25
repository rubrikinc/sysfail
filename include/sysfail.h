#ifndef _SYSFAIL_H
#define _SYSFAIL_H

#include <sys/types.h>
#include <stdint.h>
#include <signal.h>

typedef pid_t sysfail_tid_t;

struct {
    double p;
    double after_bias;
} typedef sysfail_probability_t;

struct {
    int nerror; // errno
    double weight;
} typedef sysfail_error_wt_t;

typedef void sysfail_userdata_t;

typedef bool(*sysfail_invocation_predicate_t)(sysfail_userdata_t*, const greg_t*);

struct {
    sysfail_probability_t fail;
    sysfail_probability_t delay;
    uint32_t max_delay_usec;

    sysfail_userdata_t* ctx;
    sysfail_invocation_predicate_t eligible;

    uint32_t num_errors;
    sysfail_error_wt_t error_wts[];
} typedef sysfail_outcome_t;

typedef bool(*sysfail_thread_predicate_t)(sysfail_userdata_t*, sysfail_tid_t);

enum {
    sysfail_tdisc_none  = 0,
    sysfail_tdisk_poll  = 1,
} typedef sysfail_thread_discovery_strategy_t;

union {
    uint32_t poll_itvl_usec;
} typedef sysfail_thread_discovery_t;

typedef struct sysfail_syscall_outcome_s sysfail_syscall_outcome_t;

struct sysfail_syscall_outcome_s {
    sysfail_syscall_outcome_t* next;
    int syscall;
    sysfail_outcome_t outcome;
};

struct {
    sysfail_thread_discovery_strategy_t strategy;
    sysfail_thread_discovery_t config;

    sysfail_userdata_t *ctx;
    sysfail_thread_predicate_t selector;

    sysfail_syscall_outcome_t* syscall_outcomes;
} typedef sysfail_plan_t;

typedef struct sysfail_session_s sysfail_session_t;

struct sysfail_session_s {
    void* data;
    void (*stop)(sysfail_session_t*);
    void (*add_this_thread)(sysfail_session_t*);
    void (*remove_this_thread)(sysfail_session_t*);
    void (*add_thread)(sysfail_session_t*, sysfail_tid_t);
    void (*remove_thread)(sysfail_session_t*, sysfail_tid_t);
    void (*discover_threads)(sysfail_session_t*);
};

sysfail_session_t* sysfail_start(const sysfail_plan_t*);

#endif