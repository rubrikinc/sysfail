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

#ifndef _LIB_H
#define _LIB_H

#include <iostream>
#include <sys/prctl.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <random>
#include <thread>
#include <linux/unistd.h>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>

#include "sysfail.hh"
#include "map.hh"
#include "syscall.hh"
#include "log.hh"
#include "thdmon.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

namespace sysfail {
    void continue_syscall(ucontext_t *ctx);

    static void handle_sigsys(int sig, siginfo_t *info, void *ucontext);
    static void reenable_sysfail(int sig, siginfo_t *info, void *ucontext);
    static void enable_sysfail(int sig, siginfo_t *info, void *ucontext);
    static void disable_sysfail(int sig, siginfo_t *info, void *ucontext);

    struct ActiveOutcome {
        Probability fail;
        Probability delay;
        std::chrono::microseconds max_delay;
        std::map<double, Errno> error_by_cumulative_p;
        InvocationPredicate eligibility_check;

        ActiveOutcome(const Outcome& _o);

        bool eligible(const greg_t* regs) const;
    };

    struct ActivePlan {
        const Plan p;
        std::unordered_map<Syscall, const ActiveOutcome> outcomes;

        ActivePlan(const Plan& _plan);
    };

    struct ThdState {
        char on;
        std::binary_semaphore sig_coord; // for signal handler coordination

        ThdState() :
            on(SYSCALL_DISPATCH_FILTER_ALLOW),
            sig_coord(1) {}
    };

    using ThdSt = oneapi::tbb::concurrent_hash_map<pid_t, ThdState>;

    const int SIG_ENABLE = SIGRTMIN + 4;
    const int SIG_DISABLE = SIGRTMIN + 5;
    const int SIG_REARM = SIGRTMIN + 6;

    struct ActiveSession {
        ActivePlan plan;
        AddrRange self_text;
        std::random_device rd;
        ThdSt thd_st;
        std::unique_ptr<ThdMon> tmon;

        ActiveSession(const Plan& _plan, AddrRange&& _self_addr);

        // Some procedures (sig-handlers etc) require the global-session to be
        // defined, so first define the global session and then initialize it.
        void initialize();

        pid_t disarm();

        void rearm();

        // These routines should never be used directly to add or remove
        // threads being sys-failed. Use Session::add() and Session::remove().
        // Using this directly would break Session teardown.
        void thd_enable();

        void thd_disable();

        void thd_enable(pid_t tid);

        void thd_disable(pid_t tid);

        void fail_maybe(ucontext_t *ctx);

        void thd_track(pid_t tid, DiscThdSt state);

        void discover_threads();
    };

}

#endif