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
#include "lib.hh"
#include "sysfail.h"
#include "map.hh"
#include "syscall.hh"
#include "log.hh"

extern "C" {
    extern void sysfail_restore(greg_t*);
}

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
) : fail_p(_o.fail_probability),
    delay_p(_o.delay_probability),
    max_delay(_o.max_delay) {
    double cumulative = 0;
    for (const auto& [err_no, weight] : _o.error_weights) {
        cumulative += weight;
        error_by_cumulative_p[cumulative] = err_no;
    }
}

sysfail::ActivePlan::ActivePlan(const Plan& _plan) : selector(_plan.selector) {
    for (const auto& [call, o] : _plan.outcomes) {
        outcomes.insert({call, o});
    }
}

void sysfail::enable_handler(signal_t signal, sigaction_t hdlr) {
    struct sigaction action;
    sigset_t mask;

    memset(&action, 0, sizeof(action));
    sigemptyset(&mask);

    action.sa_sigaction = hdlr;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    action.sa_mask = mask;

    if (sigaction(signal, &action, nullptr) != 0) {
        std::cerr
            << "Failed to set new sigaction: "
            << std::strerror(errno) << '\n';
        throw std::runtime_error("Failed to set new sigaction");
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

    thd_enable(); // TODO: enable for all threads
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
    sysfail::ThdSt::accessor& a
) {
    auto tid = gettid();
    auto ret = prctl(
        PR_SET_SYSCALL_USER_DISPATCH,
        PR_SYS_DISPATCH_ON,
        self_text.start,
        self_text.length,
        &a->second.on);
    if (ret == -1) {
        auto errStr = std::string(std::strerror(errno));
        std::cerr
            << "Failed to enable sysfail for " << tid << "\n";
        throw std::runtime_error("Failed to enable sysfail: " + errStr);
    }

    a->second.on = SYSCALL_DISPATCH_FILTER_BLOCK;
}

static void disable(
    const sysfail::AddrRange& self_text,
    sysfail::ThdSt::accessor& a
) {
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
    // caller must erase the thd-state
}

static void send_signal(pid_t tid, int sig, std::binary_semaphore* sem) {
    // TODO handle group-leader change (retry EINVAL)
    auto ret = sysfail::syscall(getpid(), tid, sig, 0, 0, 0, SYS_tgkill);
    if (ret < 0) {
        if (ret == -ESRCH) {
            // the thread died without telling us it was dying but we don't want
            // to deadlock so we release
            if (sem) sem->release();
            return;
        }
    }

    assert(ret == 0);
}

void sysfail::ActiveSession::thd_enable(pid_t tid) {
    // TODO: respect pid-filter
    ThdSt::accessor a;
    assert(thd_st.insert(a, tid));

    auto& sem = a->second.sig_coord;
    sem.acquire();
    a.release();

    send_signal(tid, SIG_ENABLE, &sem);

    sem.acquire();
    sem.release(); // leave sem in a re-usable state
}

void sysfail::ActiveSession::thd_disable(pid_t tid) {
    sysfail::ThdSt::accessor a;
    if (! thd_st.find(a, tid)) {
        std::cerr << "No thread state for " << tid << "\n";
        return;
    }

    auto& sem = a->second.sig_coord;
    sem.acquire();
    a.release();

    send_signal(tid, SIG_DISABLE, &sem);

    sem.acquire();
    sem.release(); // leave sem in a re-usable state
    assert(thd_st.find(a, tid));
    thd_st.erase(a);
}

void sysfail::ActiveSession::thd_enable() {
    auto tid = gettid();
    if (!plan.selector(tid)) {
        // std::cerr << "Not enabling sysfail for " << pid << "\n";
        return;
    }

    ThdSt::accessor a;
    if (thd_st.insert(a, tid)) {
        a->second.on = SYSCALL_DISPATCH_FILTER_ALLOW;
        enable(self_text, a);
    }
}

void sysfail::ActiveSession::thd_disable() {
    auto tid = gettid();
    ThdSt::accessor a;
    if (! thd_st.find(a, tid)) return;

    a->second.on = SYSCALL_DISPATCH_FILTER_ALLOW;
    disable(self_text, a);
    thd_st.erase(a);
}

void sysfail::ActiveSession::fail_maybe(ucontext_t *ctx) {
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
            auto err_p = p_dist(rnd_eng);
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

namespace {
    std::shared_ptr<sysfail::ActiveSession> session = nullptr;
}

static void sysfail::enable_sysfail(int sig, siginfo_t *info, void *ucontext) {
    {
        auto s = session;
        if (!s) {
            std::cerr << "Can't enable sysfail, no active session\n";
            return;
        }

        // Expect thread-state is initialized
        auto tid = gettid();
        sysfail::ThdSt::accessor a;

        if (!s->thd_st.find(a, tid)) {
            std::cerr << "Can't enable sysfail, no thread state\n";
            return;
        }

        enable(s->self_text, a);
        a->second.sig_coord.release();
    }

    ucontext_t *ctx = (ucontext_t *)ucontext;
    sysfail_restore(ctx->uc_mcontext.gregs);
}

static void sysfail::disable_sysfail(int sig, siginfo_t *info, void *ucontext) {
    auto s = session;
    if (!s) {
        std::cerr << "Can't disable sysfail, no active session\n";
        return;
    }

    auto tid = gettid();
    sysfail::ThdSt::accessor a;
    if (! s->thd_st.find(a, tid)) {
        std::cerr << "Can't disable sysfail, no thread state for "
                  << tid << "\n";
        return;
    }

    disable(s->self_text, a);
    a->second.sig_coord.release();
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
                    send_signal(tid, SIG_REARM, nullptr);
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

    session = std::make_shared<ActiveSession>(_plan, m->self_text());
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

extern "C" {
    sysfail_session_t* start(const sysfail_plan_t *plan) {
        auto session = new sysfail::Session({});
        auto stop = [](void* data) {
                delete static_cast<sysfail::Session*>(data);
            };
        return new sysfail_session_t{session, stop};
    }
}
