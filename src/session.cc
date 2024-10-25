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
#include <functional>
#include <linux/unistd.h>

#include "sysfail.hh"
#include "session.hh"
#include "sysfail.h"
#include "map.hh"
#include "syscall.hh"
#include "log.hh"
#include "signal.hh"
#include "helpers.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

using namespace std::placeholders;
using namespace std::chrono_literals;

void sysfail::continue_syscall(ucontext_t *ctx) {
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

sysfail::ActiveOutcome::ActiveOutcome(
    const Outcome& _o
) : fail(_o.fail),
    delay(_o.delay),
    max_delay(_o.max_delay),
    eligibility_check(_o.eligible) {
    double cumulative = 0;
    for (const auto& [err_no, weight] : _o.error_weights) {
        cumulative += weight;
        error_by_cumulative_p[cumulative] = err_no;
    }
}

bool sysfail::ActiveOutcome::eligible(const greg_t* regs) const {
    if (!eligibility_check) return true;

     // user wants to filter individual syscalls
    return eligibility_check(regs);
}

sysfail::ActivePlan::ActivePlan(const Plan& p) : p(p) {
    for (const auto& [call, o] : p.outcomes) {
        outcomes.insert({call, o});
    }
}

sysfail::ActiveSession::ActiveSession(
    const Plan& _plan,
    AddrRange&& _self_addr
) : plan(_plan), self_text(_self_addr) {
    enable_handler(SIGSYS, handle_sigsys);
    enable_handler(SIG_REARM, reenable_sysfail);
    enable_handler(SIG_ENABLE, enable_sysfail);
    enable_handler(SIG_DISABLE, disable_sysfail);
}

void sysfail::ActiveSession::initialize() {
    tmon = std::make_unique<sysfail::ThdMon>(
        plan.p.thd_disc,
        std::bind(&ActiveSession::thd_track, this, _1, _2));
}

void sysfail::ActiveSession::thd_track(pid_t tid, sysfail::DiscThdSt state) {
    switch (state) {
        case DiscThdSt::Existing:
        case DiscThdSt::Spawned:
            thd_enable(tid);
            break;
        case DiscThdSt::Terminated:
            thd_disable(tid);
    }
}

pid_t sysfail::ActiveSession::disarm() {
    auto tid = gettid();
    ThdSt::accessor a;
    if (thd_st.find(a, tid)) {
        a->second.on = SYSCALL_DISPATCH_FILTER_ALLOW;
    }
    assert(! a.empty());
    return tid;
}

void sysfail::ActiveSession::rearm() {
    auto tid = gettid();
    ThdSt::accessor a;
    if (thd_st.find(a, tid)) {
        a->second.on = SYSCALL_DISPATCH_FILTER_BLOCK;
    }
}

static void enable(
    const sysfail::AddrRange& self_text,
    sysfail::ThdState* st
) {
    auto tid = gettid();
    auto ret = prctl(
        PR_SET_SYSCALL_USER_DISPATCH,
        PR_SYS_DISPATCH_ON,
        self_text.start,
        self_text.length,
        &st->on);
    if (ret == -1) {
        auto errStr = std::string(std::strerror(errno));
        std::cerr
            << "Failed to enable sysfail for " << tid << "\n";
        throw std::runtime_error("Failed to enable sysfail: " + errStr);
    }

    st->on = SYSCALL_DISPATCH_FILTER_BLOCK;
}

static void disable() {
    auto ret = prctl(
        PR_SET_SYSCALL_USER_DISPATCH,
        PR_SYS_DISPATCH_OFF,
        0,
        0,
        0);
    if (ret == -1) {
        auto errStr = std::string(std::strerror(errno));
        std::cerr << "Failed to disable sysfail, err: " << errStr << "\n";
        throw std::runtime_error("Failed to disable sysfail: " + errStr);
    }
    // caller must erase the thd-state
}

void sysfail::ActiveSession::thd_enable(pid_t tid) {
    if (! plan.p.selector(tid)) return; // TODO: log

    ThdSt::accessor a;
    if (! thd_st.insert(a, tid)) return; // idempotency check

    auto& st = a->second;
    st.sig_coord.acquire();

    send_signal<ThdState>(
        tid,
        SIG_ENABLE,
        &st,
        [](auto* st) { st->sig_coord.release(); });

    st.sig_coord.acquire();
    st.sig_coord.release(); // leave sem in a re-usable state
}

void sysfail::ActiveSession::thd_disable(pid_t tid) {
    sysfail::ThdSt::accessor a;
    if (! thd_st.find(a, tid)) return; // idempotency check

    auto& st = a->second;
    st.sig_coord.acquire();

    send_signal<ThdState>(
        tid,
        SIG_DISABLE,
        &st,
        [](auto* st) { st->sig_coord.release(); });

    st.sig_coord.acquire();
    st.sig_coord.release(); // leave sem in a re-usable state
    thd_st.erase(a);
}

void sysfail::ActiveSession::thd_enable() {
    auto tid = gettid();
    if (!plan.p.selector(tid)) {
        // std::cerr << "Not enabling sysfail for " << pid << "\n";
        return;
    }

    ThdSt::accessor a;
    if (thd_st.insert(a, tid)) {
        a->second.on = SYSCALL_DISPATCH_FILTER_ALLOW;
        enable(self_text, &a->second);
    }
}

void sysfail::ActiveSession::thd_disable() {
    auto tid = gettid();
    ThdSt::accessor a;
    if (! thd_st.find(a, tid)) return; // idempotency check

    a->second.on = SYSCALL_DISPATCH_FILTER_ALLOW;
    disable();
    thd_st.erase(a);
}

namespace {
    void sleep(std::chrono::microseconds dur) {
        // TODO: avoid libc's nanosleep, if someone failure-injects it
        // this will confuse them. Use an equivalent
        // continue-sleep-after-interrupt helper instead.
        std::this_thread::sleep_for(dur);
    }
}

void sysfail::ActiveSession::fail_maybe(ucontext_t *ctx) {
    auto regs = ctx->uc_mcontext.gregs;
    auto call = ctx->uc_mcontext.gregs[REG_RAX];

    auto o = plan.outcomes.find(call);
    if (o == plan.outcomes.end() || !o->second.eligible(regs)) {
        continue_syscall(ctx);
        return;
    }

    thread_local std::mt19937 rnd_eng(rd());

    std::uniform_real_distribution<double> p_dist(0, 1);
    auto delay_after = std::chrono::microseconds(0);
    if (o->second.delay.p > 0) {
        if (p_dist(rnd_eng) < o->second.delay.p) {
            auto after_p = p_dist(rnd_eng);
            auto bias = o->second.delay.after_bias;
            std::uniform_int_distribution<int> delay_dist(0, o->second.max_delay.count());
            auto delay = std::chrono::microseconds(delay_dist(rnd_eng));
            if (bias && after_p < bias) {
                delay_after = delay;
            } else {
                sleep(delay);
            }
        }
    }
    Errno fail_with = 0;
    if (o->second.fail.p > 0) {
        if (p_dist(rnd_eng) < o->second.fail.p) {
            auto err_p = p_dist(rnd_eng);
            auto e = o->second.error_by_cumulative_p.lower_bound(err_p);
            auto after_p = p_dist(rnd_eng);
            if (o->second.fail.after_bias && (after_p < o->second.fail.after_bias)) {
                fail_with = e->second;
            } else {
                if (e != o->second.error_by_cumulative_p.end()) {
                    // kernel returns negative 0 - 4096 error codes in %rax
                    regs[REG_RAX] = -e->second;
                    return;
                }
            }
        }
    }

    continue_syscall(ctx);

    if (delay_after.count()) {
        sleep(delay_after);
    }
    if (fail_with) {
        regs[REG_RAX] = -fail_with;
    }
}

void sysfail::ActiveSession::discover_threads() {
    if (!tmon) {
        // this can happen if discover is called after ActiveSession is
        // constructed but before it is initialized.
        throw std::runtime_error("Thread monitor not initialized");
    }
    tmon->rescan_threads();
}

namespace {
    std::shared_ptr<sysfail::ActiveSession> session = nullptr;

    struct NotifySigHdlrCompletion {
        sysfail::ThdState* st;

        NotifySigHdlrCompletion(
            siginfo_t *info
        ): st(reinterpret_cast<sysfail::ThdState*>(info->si_value.sival_ptr)) {}

        ~NotifySigHdlrCompletion() {
            st->sig_coord.release();
        }
    };
}

static void sysfail::enable_sysfail(int sig, siginfo_t *info, void *ucontext) {
    {
        NotifySigHdlrCompletion r(info); // Expect thread-state is initialized
        auto s = session;
        if (!s) {
            std::cerr << "Can't enable sysfail, no active session\n";
            return;
        }

        enable(s->self_text, r.st);
    }

    ucontext_t *ctx = (ucontext_t *)ucontext;
    sysfail_restore(ctx->uc_mcontext.gregs);
}

static void sysfail::disable_sysfail(int sig, siginfo_t *info, void *ucontext) {
    NotifySigHdlrCompletion r(info); // Expect thread-state is initialized
    auto s = session;
    if (!s) {
        std::cerr << "Can't disable sysfail, no active session\n";
        return;
    }

    disable();
}

static void sysfail::reenable_sysfail(int sig, siginfo_t *info, void *ucontext) {
    ucontext_t *ctx = (ucontext_t *)ucontext;
    {
        auto s = session;
        if (s) { s->rearm(); }
    }
    sysfail_restore(ctx->uc_mcontext.gregs);
}

static void sysfail::handle_sigsys(int sig, siginfo_t *info, void *ucontext) {
    ucontext_t *ctx = (ucontext_t *)ucontext;

    {
        auto s = session;
        auto syscall = ctx->uc_mcontext.gregs[REG_RAX];

        // log("Handling syscall: %d\n", syscall);

        // LIBC turns off all signals before thread spawn and teardown.
        // Keep sysfail disabled here because libc wants quiescent state in
        // these parts.
        if (s && syscall == SYS_rt_sigprocmask) {
            auto sigset = (sigset_t*)ctx->uc_mcontext.gregs[REG_RSI];
            auto sigsys_on = sigismember(sigset, SIGSYS);
            if (sigsys_on) {
                auto cmd = ctx->uc_mcontext.gregs[REG_RDI];
                if (cmd == SIG_BLOCK || cmd == SIG_SETMASK) {
                    auto tid = s->disarm();
                    continue_syscall(ctx);
                    send_signal<void>(tid, SIG_REARM, nullptr);
                }
            }
        } else if (syscall == SYS_rt_sigreturn) {
            // TODO handle sigreturn correctly, may be write a test for it?
        } else if (s && syscall != SYS_exit) {
            s->fail_maybe(ctx);
        } else {
            continue_syscall(ctx);
        }
    }
    sysfail_restore(ctx->uc_mcontext.gregs);
    assert(false);
}

sysfail::Session::Session(const Plan& _plan) {
    auto m = get_mmap(getpid());
    assert(m.has_value());

    auto s = std::make_shared<ActiveSession>(_plan, m->self_text());
    session = s;
    s->initialize();
}

sysfail::Session::~Session() {
    auto s = session;
    if (s) {
        std::unique_lock<std::shared_mutex> l(lck);
        std::vector<pid_t> tids;
        for(ThdSt::iterator i = s->thd_st.begin(); i != s->thd_st.end(); ++i) {
            tids.push_back(i->first);
        }
        for (auto tid : tids) {
            s->thd_disable(tid);
        }
        assert(s->thd_st.empty());
        session.reset();
    }
}

void sysfail::Session::add() {
    std::shared_lock<std::shared_mutex> l(lck);
    session->thd_enable();
}

void sysfail::Session::remove() {
    std::shared_lock<std::shared_mutex> l(lck);
    session->thd_disable();
}

void sysfail::Session::add(pid_t tid) {
    std::shared_lock<std::shared_mutex> l(lck);
    session->thd_enable(tid);
}

void sysfail::Session::remove(pid_t tid) {
    std::shared_lock<std::shared_mutex> l(lck);
    session->thd_disable(tid);
}

void sysfail::Session::discover_threads() {
    std::shared_lock<std::shared_mutex> l(lck);
    session->discover_threads();
}
