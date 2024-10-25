#include <gtest/gtest.h>
#include <sysfail.hh>
#include <expected>
#include <chrono>
#include <random>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <regex>
#include <barrier>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_set>
#include <oneapi/tbb/concurrent_vector.h>

#include "cisq.hh"
#include "log.hh" // TODO: delete me!

// Do not include any production headers  other than `sysfail.h` here.
// This is a dedicated cwrapper test-suite (purely meant for testing the C-ABI).

extern "C" {
    #include "sysfail.h"
}

using namespace testing;
using namespace std::chrono_literals;
using namespace std::placeholders;
using namespace Cisq;

namespace cwrapper {

    sysfail_syscall_outcome_t* mk_outcome(
        int syscall,
        sysfail_probability_t fail,
        sysfail_probability_t delay,
        uint32_t max_delay_usec,
        void* ctx,
        sysfail_invocation_predicate_t eligible,
        std::vector<sysfail_error_wt_t> error_wts,
        sysfail_syscall_outcome_t* next = nullptr
    ) {
        size_t outcome_size = sizeof(
            sysfail_syscall_outcome_t) +
            error_wts.size() * sizeof(sysfail_error_wt_t);
        auto buffer = new uint8_t[outcome_size];
        sysfail_syscall_outcome_t* so =
            reinterpret_cast<sysfail_syscall_outcome_t*>(buffer);
        *so = {
            .next = next,
            .syscall = syscall,
            .outcome = {
                .fail = fail,
                .delay = delay,
                .max_delay_usec = max_delay_usec,
                .ctx = ctx,
                .eligible = eligible,
                .num_errors = static_cast<uint32_t>(error_wts.size())
            }
        };
        std::copy(error_wts.begin(), error_wts.end(), so->outcome.error_wts);
        return so;
    }

    using plan_cleanup_t = void(*)(sysfail_plan_t*);

    std::unique_ptr<sysfail_plan_t, plan_cleanup_t> mk_plan(
        sysfail_syscall_outcome_t* outcomes,
        sysfail_thread_discovery_strategy_t tdisc_strategy,
        sysfail_thread_discovery_t tdisc_config,
        void* ctx,
        sysfail_thread_predicate_t selector
    ) {
        std::unique_ptr<sysfail_plan_t, plan_cleanup_t> plan(
            new sysfail_plan_t,
            [](sysfail_plan_t* p) {
                if (p->syscall_outcomes) {
                    auto* next = p->syscall_outcomes;
                    while (next) {
                        auto* curr = next;
                        next = curr->next;
                        delete[] reinterpret_cast<uint8_t*>(curr);
                    }
                }
                delete p;
            });
        plan->strategy = tdisc_strategy;
        if (plan->strategy == sysfail_tdisk_poll) {
            plan->config.poll_itvl_usec = tdisc_config.poll_itvl_usec;
        }
        plan->syscall_outcomes = outcomes;
        plan->ctx = ctx;
        plan->selector = selector;

        return plan;
    }

    // Check
    // 1. new thred auto discovered
    // 2. write failures
    // 2.1. failure-type ratio
    // 2.2. before-after ratio (what read saw)
    // 3. read failures
    // 3.1. failure-type ratio
    // 4. thread-selection, other thread should see no failures
    // 5. call-selection, no failures on the wrong pipe

    bool accept_fd(const std::unordered_set<int>& fds, int fd) {
        return fds.find(fd) != fds.end();
    }

    #define EXPECT_IN_RANGE(VAL, MIN, MAX) \
        EXPECT_GE((VAL), (MIN));           \
        EXPECT_LE((VAL), (MAX))

    using Errno = int;
    using Count = int;

    using ErrorDistribution = std::unordered_map<Errno, Count>;

     std::string to_string(const ErrorDistribution& d) {
        std::stringstream ss;
        for (const auto& [err, count] : d) {
            ss << std::strerror(err) << "(" << err << ")" << ": " << count << ", ";
        }
        return ss.str();
     }

    struct WriteResult {
        std::unordered_set<int> successful_writes;
        ErrorDistribution errs;

        WriteResult() = default;

        WriteResult(
            WriteResult&& rval
        ) : successful_writes(std::move(rval.successful_writes)),
            errs(std::move(rval.errs)) {};

        WriteResult& operator=(WriteResult&& rval) {
            successful_writes = std::move(rval.successful_writes);
            errs = std::move(rval.errs);
            return *this;
        }
    };

    struct ReadResult {
        std::vector<int> nos;
        ErrorDistribution errs;

        ReadResult() = default;

        ReadResult(
            ReadResult&& rval
        ) : nos(std::move(rval.nos)), errs(std::move(rval.errs)) {};

        ReadResult& operator=(ReadResult&& rval) {
            nos = std::move(rval.nos);
            errs = std::move(rval.errs);
            return *this;
        }
    };

    static ReadResult read_all(
        Pipe<int>& p,
        std::atomic<bool>& w_done
    ) {
        // WARNING: DO NOT failure inject EAGAIN or EWOULDBLOCK when using this

        AsyncRead ar(p.rd_fd);
        ReadResult rr;
        int i = 0;
        while (true) {
            auto ret = p.read();
            if (ret.has_value()) {
                rr.nos.push_back(ret.value());
            } else {
                auto e = ret.error();
                if ((e.err() == EAGAIN || e.err() == EWOULDBLOCK) && w_done) {
                    break;
                } else {
                    rr.errs[ret.error().err()]++;
                }
            }
        }
        return rr;
    };

    static ReadResult read_n(Pipe<int>& p, int count) {
        ReadResult rr;
        for (int i = 0; i < count; i++) {
            auto ret = p.read();
            if (ret.has_value()) {
                rr.nos.push_back(ret.value());
            } else {
                rr.errs[ret.error().err()]++;
            }
        }
        return rr;
    };

    WriteResult write_n(Pipe<int>& p, int count, int start) {
        WriteResult wr;
        int j = 0;
        for (int i = start; i < (start + count); i++) {
            auto ret = p.write(i);
            if (ret.has_value()) {
                wr.successful_writes.insert(i);
            } else {
                wr.errs[ret.error().err()]++;
            }
        }
        return wr;
    };

    static bool fd_exists(void *ctx, const greg_t* regs) {
        auto fds = reinterpret_cast<std::unordered_set<int>*>(ctx);
        return fds->find(regs[REG_RDI]) != fds->end();
    };

    TEST(CWrapper, TestInjectsFailures) {
        Pipe<int> rw_broken_pipe, r_broken_pipe, w_broken_pipe, healthy_pipe;

        std::barrier b1(3), rw(2);

        sysfail_tid_t old_failing_thd_tid;

        // Pipe has 1MiB of capacity, we write multiple times, be careful not
        // to approach 256k writes without draining the pipe
        const int ops = 1000;

        std::atomic<bool> w_done = false;

        ReadResult thd_rr;
        std::thread old_failing_thd([&]() {
                old_failing_thd_tid = syscall(SYS_gettid);
                b1.arrive_and_wait(); // 1
                rw.arrive_and_wait(); // 2
                thd_rr = read_n(healthy_pipe, ops);
                rw.arrive_and_wait(); // 3
                thd_rr = read_all(rw_broken_pipe, w_done);
                rw.arrive_and_wait(); // 4
            });

        std::thread old_non_failing_thd([&]() {
                b1.arrive_and_wait(); // 1
            });

        b1.arrive_and_wait(); // 1

        auto test_tid = gettid();

        struct {
            std::mutex m;
            std::unordered_set<sysfail_tid_t> thds;
        } failing_thds {.thds = {old_failing_thd_tid, test_tid}};

        std::unordered_set<int> broken_rd_fds = {
            rw_broken_pipe.rd_fd,
            r_broken_pipe.rd_fd};

        std::unordered_set<int> broken_wr_fds = {
            rw_broken_pipe.wr_fd,
            w_broken_pipe.wr_fd};

        auto thd_disc_poll_tm = 10ms;
        const auto thd_disc_poll_tm_us =
            std::chrono::duration_cast<std::chrono::microseconds>(thd_disc_poll_tm)
                .count();

        // TODO: randomly reverse outcomes
        auto plan = mk_plan(
            mk_outcome(
                SYS_read,
                {0.25, 0.5},
                {0, 0},
                0,
                &broken_rd_fds,
                fd_exists,
                {{EIO, 0.5}, {EACCES, 0.5}},
                mk_outcome(
                    SYS_write,
                    {0.5, 0.5},
                    {0, 0},
                    0,
                    &broken_wr_fds,
                    fd_exists,
                    {{EIO, 0.6}, {EBADFD, 0.3}, {EBADF, 0.1}})),
            sysfail_tdisk_poll,
            {.poll_itvl_usec = static_cast<uint32_t>(thd_disc_poll_tm_us)},
            &failing_thds,
            [](void* ctx, auto tid) {
                auto ft = reinterpret_cast<decltype(failing_thds)*>(ctx);
                // std::lock_guard<std::mutex> l(ft->m);
                return ft->thds.find(tid) != ft->thds.end();
            });

        std::unique_ptr<sysfail_session_t, void(*)(sysfail_session_t*)> s(
            sysfail_start(plan.get()),
            [](sysfail_session_t* s) { s->stop(s); });

        // allow sysfail to discove threads
        std::this_thread::sleep_for(thd_disc_poll_tm);

        rw.arrive_and_wait(); // 2

        auto wr = write_n(healthy_pipe, ops, 0);

        rw.arrive_and_wait(); // 3

        // does not failure-inject healthy-pipe
        EXPECT_EQ(wr.successful_writes.size(), ops);
        EXPECT_TRUE(wr.errs.empty());
        EXPECT_EQ(thd_rr.nos.size(), ops);
        EXPECT_TRUE(thd_rr.errs.empty());

        wr = write_n(rw_broken_pipe, ops, 0);
        w_done = true;

        rw.arrive_and_wait(); // 4

        auto err_count = [](const ErrorDistribution& errs) {
            int count = 0;
            for (const auto& [e, c] : errs) {
                if (e == EAGAIN || e == EWOULDBLOCK) continue;
                count += c;
            }
            return count;
        };

        // failure-injects both read and write ops on broken pipe
        EXPECT_IN_RANGE(wr.successful_writes.size(), ops * .3, ops * .7);
        EXPECT_IN_RANGE(err_count(wr.errs), ops * .3, ops * .7);
        EXPECT_EQ(wr.errs.size(), 3) << to_string(wr.errs);
        EXPECT_GT(wr.errs[EIO], wr.errs[EBADFD] + wr.errs[EBADF]);
        EXPECT_GT(wr.errs[EBADFD], wr.errs[EBADF]);

        // It should be 0.75 * 0.75, but use higher tolerances to avoid flaky
        // tests. P(read | write) = (1 - P(write fails before)) * P(read)
        //                        = 0.75 * 0.75
        // so, we use 0.8 ≈ 0.9 * 0.9
        EXPECT_IN_RANGE(thd_rr.nos.size(), wr.successful_writes.size(), ops * .8);
        EXPECT_IN_RANGE(err_count(thd_rr.errs), ops * .1, ops * .6);
        // EBUSY shows up sometimes (perhaps when pipe is full?)
        EXPECT_IN_RANGE(thd_rr.errs.size(), 2, 3) << to_string(thd_rr.errs);
        EXPECT_GT(thd_rr.errs[EIO], 0);
        EXPECT_GT(thd_rr.errs[EACCES], 0);
        EXPECT_GT(
            static_cast<double>(thd_rr.errs[EIO]) / thd_rr.errs[EACCES],
            0.6);
        EXPECT_GT(
            static_cast<double>(thd_rr.errs[EACCES]) / thd_rr.errs[EIO],
            0.6);

        old_failing_thd.join();
        old_non_failing_thd.join();
    }
}
