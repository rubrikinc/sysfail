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

#include <gtest/gtest.h>
#include <sysfail.hh>
#include <expected>
#include <chrono>
#include <random>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <barrier>
#include <oneapi/tbb/concurrent_vector.h>

#include "cisq.hh"
#include "log.hh"
#include "signal.hh"
#include "session.hh"

using namespace testing;
using namespace std::chrono_literals;

namespace sysfail {
    using namespace Cisq;

    TEST(Session, LoadSessionWithoutFailureInjection) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Session s({});
        auto success = 0;
        for (int i = 0; i < 10; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
                EXPECT_EQ(r.value(), "foo bar baz quux");
            }
        }
        EXPECT_EQ(success, 10);
    }

    TEST(Session, LoadSessionWithSysReadBlocked) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1.0}}}} },
            [](pid_t pid) { return true; },
            thread_discovery::None{});

        sysfail::Session s(p);
        auto success = 0;
        for (int i = 0; i < 10; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
                EXPECT_EQ(r.value(), "foo bar baz quux");
            }
        }
        EXPECT_EQ(success, 0);
    }

    TEST(Session, SysOpenAndReadFailureInjection) {
        TmpFile tFile;
        tFile.write("foo bar baz quux");

        sysfail::Plan p(
            { {SYS_read, {0.33, 0, 0us, {{EIO, 1.0}}}},
              {SYS_openat, {0.25, 0, 0us, {{EINVAL, 1.0}}}}},
            [](pid_t pid) { return true; },
            thread_discovery::None{});

        {
            sysfail::Session s(p);
            auto success = 0;
            for (int i = 0; i < 1000; i++) {
                auto r = tFile.read();
                if (r.has_value()) {
                    success++;
                }
            }
            // _OPEN_AND_READ_ERR_
            // 50 +- 10% (margin of error) around mean 50% expected success rate
            // Read happens after open
            // P(open succeeds) = (1 - 0.25) = 0.75
            // P(read success | open success) = 0.67
            // P(read success) = P(read success | open success) * P (open success) =
            //     0.67 * 0.75 = 0.50
            EXPECT_GT(success, 400);
            EXPECT_LT(success, 600);
        }

        auto success = 0;
        for (int i = 0; i < 100; i++) {
            auto r = tFile.read();
            if (r.has_value()) {
                success++;
            }
        }
        EXPECT_EQ(success, 100);
    }

    TEST(Session, SysSlowReadFastWrite) {
        TmpFile tFile;

        sysfail::Plan p(
            { {SYS_read, {0, 0.5, 10ms, {}}},
              {SYS_write, {0, 0, 0ms, {}}} },
            [](pid_t pid) { return true; },
            thread_discovery::None{});

        {
            sysfail::Session s(p);
            auto read_tm = 0ns, write_tm = 0ns;
            for (int i = 0; i < 100; i++) {
                auto str = "foo bar " + i;
                auto write_start = std::chrono::system_clock::now();
                tFile.write(str);
                write_tm += std::chrono::system_clock::now() - write_start;
                auto read_start = std::chrono::system_clock::now();
                auto r = tFile.read();
                read_tm += std::chrono::system_clock::now() - read_start;
                EXPECT_EQ(r.value(), str);
            }
            EXPECT_GT(static_cast<double>(read_tm.count())/write_tm.count(), 2);
        }

        auto read_tm = 0ns, write_tm = 0ns;
        for (int i = 0; i < 100; i++) {
            auto str = "baz quux " + i;
            auto write_start = std::chrono::system_clock::now();
            tFile.write(str);
            write_tm += std::chrono::system_clock::now() - write_start;
            auto read_start = std::chrono::system_clock::now();
            auto r = tFile.read();
            read_tm += std::chrono::system_clock::now() - read_start;
            EXPECT_EQ(r.value(), str);
        }

        EXPECT_LT(static_cast<double>(read_tm.count())/write_tm.count(), 1);
    }

    TEST(Session, SeveralThreadsTest) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { {SYS_read, {0.33, 0, 0us, {{EIO, 1.0}}}},
              {SYS_openat, {0.25, 0, 0us, {{EINVAL, 1.0}}}},
              {SYS_write, {0.8, 0, 0us, {{EINVAL, 1.0}}}}},
            [test_thd](pid_t tid) { return tid % 2 == 0 && tid != test_thd; },
            thread_discovery::None{});

        const auto thds = 10;
        const auto attempts = 1000;

        std::random_device rd;
        thread_local std::mt19937 rnd_eng(rd());

        struct Result {
            pid_t thd_id;
            int success;
            bool reader;
        };

        struct Results {
            std::vector<Result> result;
            std::mutex mtx;
        };

        Results r;
        {
            Session s(p);
            std::vector<std::thread> threads;
            std::atomic<long> w_ctr = 0;
            for (auto i = 0; i < 10; i++) {
                std::binomial_distribution<bool> rw_dist;
                auto reader = rw_dist(rnd_eng);
                auto disable_explicitly = rw_dist(rnd_eng);
                threads.push_back(std::thread([&]() {
                    s.add();
                    int success = 0;
                    for (auto a = 0; a < attempts; a++) {
                        if (reader) {
                            auto r = f.read();
                            if (r.has_value()) {
                                EXPECT_TRUE(
                                    r.value() == "foo" ||
                                    r.value().empty() ||
                                    r.value().starts_with("bar-"))
                                    << "value: " << r.value();
                                success++;
                            }
                        } else {
                            auto w = f.write(
                                    std::string("bar-") +
                                    std::to_string(w_ctr.fetch_add(1)));
                            if (w.has_value()) {
                                success++;
                            }
                        }
                    }
                    std::lock_guard<std::mutex> l(r.mtx);
                    r.result.emplace_back(
                        gettid(),
                        success,
                        reader);
                    if (disable_explicitly) {
                        s.remove();
                    }
                }));
            }
            for (auto& t : threads) {
                t.join();
            }
        }

        for (const auto& r : r.result) {
            if (r.thd_id % 2 == 0) {
                if (r.reader) {
                    // refer _OPEN_AND_READ_ERR_ above
                    EXPECT_GT(r.success, 0.4 * attempts);
                    EXPECT_LT(r.success, 0.6 * attempts);
                } else {
                    // 0.75 * 0.8 = 0.15 (similar to _OPEN_AND_READ_ERR_) +-
                    // 0.05 margin of error
                    EXPECT_GT(r.success, 0.1 * attempts);
                    EXPECT_LT(r.success, 0.2 * attempts);
                }
            } else { // we didn't inject failures for odd threads
                EXPECT_EQ(r.success, attempts);
            }
        }
    }

    void elapse(std::chrono::microseconds us) {
        struct timespec start, current;
        long elapsed_time;

        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start) == -1) {
            throw new std::runtime_error("Failed to get starting time");
        }

        do {
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &current) == -1) {
                throw new std::runtime_error("Failed to get current time");
            }

            elapsed_time = (current.tv_sec - start.tv_sec) * 1e6 + (current.tv_nsec - start.tv_nsec) / 1e3;
        } while (us.count() > elapsed_time);
    }

    TEST(Session, SysfailDisable) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1.0}}}} },
            [](pid_t tid) { return true; },
            thread_discovery::None{});

        std::binary_semaphore chk_sem(1), start_sem(1);

        sysfail::Session s(p);
        std::thread t([&]() {
            start_sem.release();
            s.add();
            auto r = f.read();
            EXPECT_FALSE(r.has_value());

            // the main thread only turned off sysfail for itself, not for
            // other threads
            chk_sem.acquire();
            r = f.read();
            EXPECT_FALSE(r.has_value());

            chk_sem.release();
        });
        // wait for failure-injection to start again after clone3
        // elapse(1ms);

        start_sem.acquire();
        auto r = f.read();
        EXPECT_FALSE(r.has_value());
        s.remove();

        chk_sem.release();
        r = f.read();
        EXPECT_TRUE(r.has_value());
        chk_sem.acquire();

        s.add();
        r = f.read();
        EXPECT_FALSE(r.has_value());
        t.join();
    }

    TEST(Session, ErrorDistribution) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { { SYS_read,
                {1.0, 0, 0us, {{EIO, 0.1}, {EINVAL, 0.3}, {EFAULT, 0.6}}}} },
            [](pid_t tid) { return true; },
            thread_discovery::None{});

        std::unordered_map<Errno, int> error_count;

        {
            Session s(p);
            for (int i = 0; i < 1000; i++) {
                auto ret = f.read();
                EXPECT_FALSE(ret.has_value());
                auto err_no = ret.error().err();
                auto e = error_count.find(err_no);
                if (e == error_count.end()) {
                    error_count.emplace(err_no, 1);
                } else {
                    e->second++;
                }
            }
        }

        EXPECT_EQ(3, error_count.size());
        auto eio_count = error_count[EIO];
        auto einval_count = error_count[EINVAL];
        auto efault_count = error_count[EFAULT];

        EXPECT_LT(eio_count, einval_count);
        EXPECT_LT(einval_count, efault_count);
        EXPECT_LT(eio_count + einval_count, efault_count);
        EXPECT_GT(2 * (eio_count + einval_count), efault_count);
        EXPECT_EQ(1000, eio_count + einval_count + efault_count);
    }

    TEST(Session, StopFailureInjectionOnOtherThreads) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [](pid_t tid) { return true; },
            thread_discovery::None{});

        {
            Session s(p);

            auto ret = f.read();
            EXPECT_FALSE(ret.has_value());

            std::barrier b(2);

            pid_t main_tid = gettid();

            std::atomic<pid_t> thd_tid = 0;

            std::thread t([&]() {
                thd_tid = gettid();

                auto ret = f.read();
                EXPECT_TRUE(ret.has_value());
                EXPECT_EQ(ret.value(), "foo");

                b.arrive_and_wait(); // 1
                b.arrive_and_wait(); // 2

                ret = f.read();
                EXPECT_FALSE(ret.has_value());

                s.remove(main_tid);
                b.arrive_and_wait(); // 3
                b.arrive_and_wait(); // 4

                ret = f.read();
                EXPECT_TRUE(ret.has_value());
                EXPECT_EQ(ret.value(), "foo");
            });

            b.arrive_and_wait(); // 1
            s.add(thd_tid);
            b.arrive_and_wait(); // 2

            b.arrive_and_wait(); // 3
            ret = f.read();
            EXPECT_TRUE(ret.has_value());
            EXPECT_EQ(ret.value(), "foo");

            s.remove(thd_tid);
            b.arrive_and_wait(); // 4

            t.join();
        };
    }

    TEST(Session, AddThreadChecksThreadIdFilter) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [](pid_t tid) { return tid % 2 == 0; },
            thread_discovery::None{});

        {
            Session s(p);

            const int thd_count = 40;

            std::barrier b(thd_count + 1);
            std::vector<std::thread> thds;
            oneapi::tbb::concurrent_vector<pid_t> tids;

            for (int i = 0; i < thd_count; i++) {
                thds.emplace_back([&]() {
                    auto tid = gettid();
                    tids.push_back(tid);

                    b.arrive_and_wait(); // 1
                    if (tid % 3 == 0) {
                        s.add(tid);
                    } else if (tid % 3 == 1) {
                        s.add();
                    }

                    b.arrive_and_wait(); // 2
                    auto ret = f.read();
                    if (tid % 2 == 0) {
                        EXPECT_FALSE(ret.has_value());
                    } else {
                        EXPECT_TRUE(ret.has_value());
                        EXPECT_EQ(ret.value(), "foo");
                    }

                    b.arrive_and_wait(); // 3
                    if (tid % 4 == 1) {
                        s.remove();
                    } else if (tid % 4 == 2) {
                        s.remove(tid);
                    }

                    b.arrive_and_wait(); // 4

                    ret = f.read();
                    EXPECT_TRUE(ret.has_value());
                    EXPECT_EQ(ret.value(), "foo");
                });
            }

            b.arrive_and_wait(); // 1
            for (auto tid : tids) {
                if (tid % 3 == 2) {
                    // threads themselves add <tid> % 3 \in {0, 1}
                    s.add(tid);
                }
            }
            b.arrive_and_wait(); // 2
            b.arrive_and_wait(); // 3
            for (auto tid : tids) {
                if (tid % 4 == 0) {
                    // threads themselves add <tid> % 4 \in {1, 2}
                    // no one removes <tid> % 4 == 3
                    s.remove(tid);
                }
            }
            b.arrive_and_wait(); // 4

            for (auto& t : thds) t.join();
        };
    }

    TEST(Session, ThreadAddRemoveIdempotence) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [](pid_t tid) { return tid % 2 == 0; },
            thread_discovery::None{});

        {
            std::random_device rd;
            thread_local std::mt19937 rnd_eng(rd());

            std::binomial_distribution<bool> redundant_op;

            Session s(p);

            const int thd_count = 40;

            std::barrier b(thd_count + 1);
            std::vector<std::thread> thds;
            oneapi::tbb::concurrent_vector<pid_t> tids;

            for (int i = 0; i < thd_count; i++) {
                auto redundant_add = redundant_op(rnd_eng);
                auto redundant_rm = redundant_op(rnd_eng);
                auto redundant_branch = redundant_op(rnd_eng);
                thds.emplace_back([=, &tids, &b, &s, &f]() {
                    auto tid = gettid();
                    tids.push_back(tid);

                    b.arrive_and_wait(); // 1
                    if (tid % 3 == 0) {
                        s.add(tid);
                    } else if (tid % 3 == 1) {
                        s.add();
                    }
                    if (redundant_add) {
                        if (redundant_branch) {
                            s.add(tid);
                        } else {
                            s.add();
                        }
                    }

                    b.arrive_and_wait(); // 2
                    auto ret = f.read();
                    if (tid % 2 == 0) {
                        EXPECT_FALSE(ret.has_value());
                    } else {
                        EXPECT_TRUE(ret.has_value());
                        EXPECT_EQ(ret.value(), "foo");
                    }

                    b.arrive_and_wait(); // 3
                    if (tid % 4 == 1) {
                        s.remove();
                    } else if (tid % 4 == 2) {
                        s.remove(tid);
                    }
                    if (redundant_rm) {
                        if (redundant_branch) {
                            s.remove(tid);
                        } else {
                            s.remove();
                        }
                    }

                    b.arrive_and_wait(); // 4

                    ret = f.read();
                    EXPECT_TRUE(ret.has_value());
                    EXPECT_EQ(ret.value(), "foo");
                });
            }

            b.arrive_and_wait(); // 1
            for (auto tid : tids) {
                if (tid % 3 == 2) {
                    // threads themselves add <tid> % 3 \in {0, 1}
                    s.add(tid);
                }
                if (redundant_op(rnd_eng)) {
                    if (redundant_op(rnd_eng)) {
                        s.add(tid);
                    } else {
                        s.add();
                    }
                }
            }
            b.arrive_and_wait(); // 2
            b.arrive_and_wait(); // 3
            for (auto tid : tids) {
                if (tid % 4 == 0) {
                    // threads themselves add <tid> % 4 \in {1, 2}
                    // no one removes <tid> % 4 == 3
                    s.remove(tid);
                }
                if (redundant_op(rnd_eng)) {
                    if (redundant_op(rnd_eng)) {
                        s.remove(tid);
                    } else {
                        s.remove();
                    }
                }
            }
            b.arrive_and_wait(); // 4

            for (auto& t : thds) t.join();
        };
    }

    TEST(Session, FailsAfterTheSyscallWhenSoConfigured) {
        TmpFile tFile;

        sysfail::Plan p(
            { {SYS_write, {{1, 1}, 0, 0ms, {{EIO, 1}}}} },
            [](pid_t tid) { return true; },
            thread_discovery::None{});

        tFile.write("foo");

        {
            // NOTE: this test does not report failure because writes fail
            // use debugger to see the failure
            auto rd_result = tFile.read();
            EXPECT_EQ(rd_result.value(), "foo");
            Session s(p);
            auto wr_result = tFile.write("bar");
            EXPECT_FALSE(wr_result.has_value());
            rd_result = tFile.read();
            EXPECT_TRUE(rd_result.has_value());
            // log(rd_result.value().c_str());  // <- this can help debug
            EXPECT_EQ(rd_result.value(), "bar");
        }
    }

    using Tm = std::chrono::time_point<std::chrono::system_clock>;

    struct DelayMesurement {
        std::chrono::microseconds rd, wr;
    };

    struct DelayEpisodes {
        DelayMesurement without, with;
    };

    using FailMsg = std::string;

    std::pair<DelayEpisodes, FailMsg> mesure_delay_effects(
        std::chrono::microseconds sleep_us,
        Probability p_delay
    ) {
        auto test_tid = gettid();

        sysfail::Plan p(
            { {SYS_write, {0, p_delay, sleep_us, {}}} },
            [=](pid_t tid) { return tid == test_tid; },
            thread_discovery::None{});

        int evt_count = 1000;

        Pipe<Tm> pipe;

        auto us = [](auto d) {
            return std::chrono::duration_cast<std::chrono::microseconds>(d);
        };

        auto now = std::chrono::system_clock::now;

        auto total_tm = [](
            const std::vector<std::chrono::microseconds>& delays
        ) {
            return std::reduce(
                delays.begin(),
                delays.end(),
                0us,
                std::plus<std::chrono::microseconds>());
        };

        auto compute_delivery_metrics = [&]() {
            std::vector<std::chrono::microseconds> write_delay;
            std::vector<std::chrono::microseconds> read_delay;

            std::thread reader([&]() {
                for (int i = 0; i < evt_count; i++) {
                    auto polled = pipe.read();
                    if (polled.has_value()) {
                        auto delay = us(now() - polled.value());
                        read_delay.push_back(delay);
                    } else {
                        std::cerr << "Failed to read from pipe" << std::endl;
                    }
                }
            });

            for (int i = 0; i < evt_count; i++) {
                auto s = now();
                pipe.write(s);
                write_delay.push_back(us(now() - s));
            }
            reader.join();

            return std::make_pair(total_tm(read_delay), total_tm(write_delay));
        };

        auto [read_tm_without_delay, write_tm_without_delay] =
            compute_delivery_metrics();
        Session s(p);
        auto [read_tm_with_delay, write_tm_with_delay] =
            compute_delivery_metrics();
        s.remove();

        std::stringstream ss;
        ss << "Read without delay: " << read_tm_without_delay << std::endl;
        ss << "Read with delay: " << read_tm_with_delay << std::endl;

        ss << "Write without delay: " << write_tm_without_delay << std::endl;
        ss << "Write with delay: " << write_tm_with_delay << std::endl;

        return {
            {
                DelayMesurement{read_tm_without_delay, write_tm_without_delay},
                DelayMesurement{read_tm_with_delay, write_tm_with_delay}
            },
            ss.str()
        };
    }

    TEST(Session, DelaysAfterTheSyscallWhenSoConfigured) {
        auto [d, fail_msg] = mesure_delay_effects(1ms, {1, 1});

        auto higher_rd_tm = std::max(d.without.rd, d.without.rd);
        auto lower_rd_tm = std::min(d.without.rd, d.with.rd);

        EXPECT_LT(higher_rd_tm / lower_rd_tm, 15) << fail_msg;
        EXPECT_GT(d.with.wr / d.without.wr, 150) << fail_msg;
    }

    TEST(Session, DelaysBeforeTheSyscallWhenSoConfigured) {
        // Because sleep before has higher number of things happening after
        // the sleep, the effect is less pronounced. This can be made more
        // pronounced by bumping up the sleep, but this is good balance between
        // reliably demonstrating sysfail injects delay and at the same time
        // not slowing the test down too much, it still finishes in <1s.
        auto [d, fail_msg] = mesure_delay_effects(2ms, {1, 0});

        auto higher_rd_tm = std::max(d.without.rd, d.without.rd);
        auto lower_rd_tm = std::min(d.without.rd, d.with.rd);

        EXPECT_GT(d.with.rd, d.without.rd) << fail_msg;
        EXPECT_GT(d.with.wr / d.without.wr, 150) << fail_msg;
    }

    template <typename T, typename E> void assertValue(
        const std::expected<T, E> &e,
        const T &v,
        const char* file,
        int line
    ) {
        ScopedTrace t(file, line, "");
        EXPECT_TRUE(e.has_value());
        if (e.has_value()) {
            EXPECT_EQ(e.value(), v);
        }
    }

    #define ASSERT_VALUE(e, v) assertValue(e, v, __FILE__, __LINE__)

    TEST(Session, DoesNotFailIneligibleSyscalls) {
        Pipe<int> p1, p2;

        auto fail_p1rd = [&p1](const greg_t* regs) -> bool {
            return p1.rd_fd == regs[REG_RDI];
        };

        auto fail_p1wr = invp::p([&p1](auto wr, auto fd, auto buff, auto sz) {
            return fd == p1.wr_fd;
        });

        {
            EXPECT_TRUE(p1.write(10).has_value());
            EXPECT_TRUE(p2.write(20).has_value());
            ASSERT_VALUE(p1.read(), 10);
            ASSERT_VALUE(p2.read(), 20);
        }

        {
            sysfail::Plan p(
                { {SYS_write, {{1, 0}, 0, 0ms, {{EIO, 1}}, fail_p1wr}} },
                [](pid_t tid) { return true; },
                thread_discovery::None{});
            Session s(p);
            EXPECT_FALSE(p1.write(30).has_value());
            EXPECT_TRUE(p2.write(40).has_value());
        }

        EXPECT_TRUE(p1.write(50).has_value());

        {
            sysfail::Plan p(
                { {SYS_read, {{1, 0}, 0, 0ms, {{EIO, 1}}, fail_p1rd}} },
                [](pid_t tid) { return true; },
                thread_discovery::None{});
            Session s(p);
            EXPECT_FALSE(p1.read().has_value());
            ASSERT_VALUE(p2.read(), 40);
        }
    }
}
