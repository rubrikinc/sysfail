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

#include "sysfail.hh"
#include "sysfail.h"
#include "map.hh"
#include "syscall.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

namespace sysfail {
    static void continue_syscall(ucontext_t *ctx) {
        auto rax = syscall(
            ctx->uc_mcontext.gregs[REG_RDI],
            ctx->uc_mcontext.gregs[REG_RSI],
            ctx->uc_mcontext.gregs[REG_RDX],
            ctx->uc_mcontext.gregs[REG_R10],
            ctx->uc_mcontext.gregs[REG_R8],
            ctx->uc_mcontext.gregs[REG_R9],
            ctx->uc_mcontext.gregs[REG_RAX]);

        ctx->uc_mcontext.gregs[REG_RAX] = rax;
    }

    static void handle_sigsys(int sig, siginfo_t *info, void *ucontext);

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
        AddrRange self_text;
        volatile char on;
        std::random_device rd;

        ActiveSession(
            const Plan& _plan,
            AddrRange&& _self_addr
        ) : plan(_plan), self_text(_self_addr) {
            on = SYSCALL_DISPATCH_FILTER_ALLOW;

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

            thd_enable(gettid());

            on = SYSCALL_DISPATCH_FILTER_BLOCK;
        }

        void thd_enable(pid_t tid) {
            if (!plan.selector(tid)) {
                // std::cerr << "Not enabling sysfail for " << pid << "\n";
                return;
            }

            auto ret = prctl(
                PR_SET_SYSCALL_USER_DISPATCH,
                PR_SYS_DISPATCH_ON,
                self_text.start,
                self_text.length,
                &on);
            if (ret == -1) {
                auto errStr = std::string(std::strerror(errno));
                std::cerr
                    << "Failed to enable sysfail for " << self_text.path
                    << ", err: " << errStr << "\n";
                throw std::runtime_error("Failed to enable sysfail: " + errStr);
            }
        }

        void thd_disable(pid_t tid) {
            auto ret = prctl(
                PR_SET_SYSCALL_USER_DISPATCH,
                PR_SYS_DISPATCH_OFF,
                0,
                0,
                0);
            if (ret == -1) {
                auto errStr = std::string(std::strerror(errno));
                std::cerr
                    << "Failed to disable sysfail for " << self_text.path
                    << ", err: " << errStr << "\n";
                throw std::runtime_error("Failed to disable sysfail: " + errStr);
            }
        }

        void fail_maybe(ucontext_t *ctx) {
            auto call = ctx->uc_mcontext.gregs[REG_RAX];

            auto o = plan.outcomes.find(call);
            if (o == plan.outcomes.end()) {
                continue_syscall(ctx);
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

            continue_syscall(ctx);
        }
    };

    std::shared_ptr<ActiveSession> session = nullptr;

    static void handle_sigsys(int sig, siginfo_t *info, void *ucontext) {
        ucontext_t *ctx = (ucontext_t *)ucontext;
        auto s = session;

        if (ctx->uc_mcontext.gregs[REG_RAX] == SYS_rt_sigprocmask) {
            // **LIBC SPECIFIC NOTE**
            // `libc` turns off all signals before thread teardown
            // keep SIGSYS enabled because we don't really have a great way
            // to check if we are in a teardown phase (and threads manipulating)
            // signals should not accidentally disable sysfail.
            //
            // If push comes to shove we can perhaps detect whether `libc`
            // `start_thread` (private symbol) is calling us and disable
            // sysfail, but this would be a hack.
            auto sigset = (sigset_t*)(ctx->uc_mcontext.gregs[REG_RSI]);
            sigdelset(sigset, SIGSYS);
        }
        if (s && ctx->uc_mcontext.gregs[REG_RAX] != SYS_exit) {
            s->fail_maybe(ctx);
        } else {
            continue_syscall(ctx);
        }

        sysfail_restore(ctx->uc_mcontext.gregs);
    }
}

sysfail::Session::Session(const Plan& _plan) {
    auto m = get_mmap(getpid());
    assert(m.has_value());

    session = std::make_shared<ActiveSession>(_plan, m->self_text());
}

sysfail::Session::~Session() {
    session->on = SYSCALL_DISPATCH_FILTER_ALLOW;
    session.reset();
}

void sysfail::Session::add(pid_t tid) {
    session->thd_enable(tid);
}

void sysfail::Session::remove(pid_t tid) {
    session->thd_disable(tid);
}

extern "C" {
    sysfail_session_t* start(const sysfail_plan_t *plan) {
        auto session = new sysfail::Session({});
        auto stop = [](void* data) {
                delete static_cast<sysfail::Session*>(data);
            };
        return new sysfail_session_t{session, stop};
    }
}
