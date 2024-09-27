#include <iostream>
#include <sys/prctl.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <random>
#include <thread>

#include "sysfail.hh"
#include "sysfail.h"
#include "map.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

namespace sysfail {
    struct ActiveOutcome {
        double fail_p;
        double delay_p;
        std::chrono::microseconds max_delay;
        std::map<double, Errno> error_by_cumulative_p;

        ActiveOutcome(
            const Outcome& _o
        ) : fail_p(_o.fail_probability),
            delay_p(_o.delay_probability),
            max_delay(_o.max_delay) {
            double cumulative = 0;
            for (const auto& [err_no, weight] : _o.error_weights) {
                cumulative += weight;
                error_by_cumulative_p[cumulative] = err_no;
            }
        }
    };

    struct ActivePlan {
        std::unordered_map<Syscall, const ActiveOutcome> outcomes;
        const std::function<bool(pid_t)> selector;

        ActivePlan(
            const Plan& _plan
        ) : selector(_plan.selector) {
            for (const auto& [call, o] : _plan.outcomes) {
                outcomes.insert({call, o});
            }
        }
    };

    struct ActiveSession {
        ActivePlan plan;
        const std::vector<AddrRange> pass_thru;
        char on;
    };

    std::shared_ptr<ActiveSession> session = nullptr;

    std::random_device rd;

    static void pass_thru(ucontext_t *ctx) {
        long rax = ctx->uc_mcontext.gregs[REG_RAX];
        long rdi = ctx->uc_mcontext.gregs[REG_RDI];
        long rsi = ctx->uc_mcontext.gregs[REG_RSI];
        long rdx = ctx->uc_mcontext.gregs[REG_RDX];
        long rcx;
        long r11;

        register long r10 asm("r10") = ctx->uc_mcontext.gregs[REG_R10];
        register long r8 asm("r8") = ctx->uc_mcontext.gregs[REG_R8];
        register long r9 asm("r9") = ctx->uc_mcontext.gregs[REG_R9];


        asm volatile("movq %%rcx, %0" : "=r"(rcx));
        asm volatile("movq %%r11, %0" : "=r"(r11));

        asm volatile(
            "syscall \n"
            : "+a"(rax),
              "+D"(rdi),
              "+S"(rsi),
              "+d"(rdx),
              "+r"(r10),
              "+r"(r8),
              "+r"(r9)
            :
            : "rcx", "r11", "memory"
        );

        asm volatile("movq %0, %%rcx" :: "r"(rcx));
        asm volatile("movq %0, %%r11" :: "r"(r11));

        ctx->uc_mcontext.gregs[REG_RAX] = rax;
        ctx->uc_mcontext.gregs[REG_RDI] = rdi;
        ctx->uc_mcontext.gregs[REG_RSI] = rsi;
        ctx->uc_mcontext.gregs[REG_RDX] = rdx;
        ctx->uc_mcontext.gregs[REG_R10] = r10;
        ctx->uc_mcontext.gregs[REG_R8] = r8;
        ctx->uc_mcontext.gregs[REG_R9] = r9;

        // current_session->on = SYSCALL_DISPATCH_FILTER_BLOCK;
    }

    void fail_maybe(
        const ActivePlan& plan,
        Syscall call,
        ucontext_t *ctx
    ) {
        auto o = plan.outcomes.find(call);
        if (o == plan.outcomes.end()) {
            pass_thru(ctx);
            return;
        }

        thread_local std::mt19937 rnd_eng(rd());

        std::uniform_real_distribution<double> p_dist(0, 1);
        if (o->second.delay_p > 0) {
            if (p_dist(rnd_eng) < o->second.delay_p) {
                std::uniform_int_distribution<int> delay_dist(0, o->second.max_delay.count());
                std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(rnd_eng)));
            }
        }
        if (o->second.fail_p > 0) {
            if (p_dist(rnd_eng) < o->second.fail_p) {
                auto err_p =p_dist(rnd_eng);
                auto e = o->second.error_by_cumulative_p.lower_bound(err_p);
                if (e != o->second.error_by_cumulative_p.end()) {
                    // kernel returns negative 0 - 4096 error codes in %rax
                    ctx->uc_mcontext.gregs[REG_RAX] = -e->second;
                    return;
                }
            }
        }

        pass_thru(ctx);
    }

    static void handle_sigsys(int sig, siginfo_t *info, void *ucontext) {
        ucontext_t *ctx = (ucontext_t *)ucontext;
        auto s = session;

        if (s) {
            fail_maybe(s->plan, ctx->uc_mcontext.gregs[REG_RAX], ctx);
        } else {
            pass_thru(ctx);
        }

        sysfail_restore(ctx->uc_mcontext.gregs);
    }

    void enable_maybe(
        const Plan& plan,
        pid_t pid,
        const std::vector<sysfail::AddrRange>& pass_thru
    ) {
        if (!plan.selector(pid)) {
            // std::cerr << "Not enabling sysfail for " << pid << "\n";
            return;
        }

        for (const auto& r : pass_thru) {
            auto ret = prctl(
                PR_SET_SYSCALL_USER_DISPATCH,
                PR_SYS_DISPATCH_ON,
                r.start,
                r.length,
                &session->on);
            if (ret == -1) {
                auto errStr = std::string(std::strerror(errno));
                std::cerr
                    << "Failed to enable sysfail for " << r.path
                    << ", err: " << errStr << "\n";
                ;
                throw std::runtime_error("Failed to enable sysfail: " + errStr);
            }
        }


        session->on = SYSCALL_DISPATCH_FILTER_BLOCK;
    }
}

sysfail::Session::Session(const Plan& _plan) {
    auto m = get_mmap(getpid());
    assert(m.has_value());

    auto mappings = m->bypass_mappings();

    session = std::make_shared<ActiveSession>(
        _plan,
        mappings,
        SYSCALL_DISPATCH_FILTER_ALLOW);

    struct sigaction action;
    sigset_t mask;

    memset(&action, 0, sizeof(action));
    sigemptyset(&mask);

    action.sa_sigaction = handle_sigsys;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    action.sa_mask = mask;

    if (sigaction(SIGSYS, &action, NULL) != 0) {
        std::cerr
            << "Failed to set new sigaction: "
            << std::strerror(errno) << '\n';
        throw std::runtime_error("Failed to set new sigaction");
    }

    enable_maybe(_plan, gettid(), mappings);
}

bool sysfail::Session::stop() {
    session->on = SYSCALL_DISPATCH_FILTER_ALLOW;
    return true;
}

extern "C" {
    sysfail_session_t* start(const sysfail_plan_t *plan) {
        auto session = new sysfail::Session({});
        auto stop = [](void* data) {
                return static_cast<sysfail::Session*>(data)->stop();
            };
        return new sysfail_session_t{session, stop};
    }
}
