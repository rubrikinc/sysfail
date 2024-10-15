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
#include <thread>
#include <cmath>
#include <filesystem>
#include <oneapi/tbb/concurrent_vector.h>

#include "cisq.hh"
#include "helpers.hh"

using namespace testing;
using namespace std::chrono_literals;

namespace sysfail {
    TEST(Lib_ThdMon, FailureInjectsAllEligibleThreads) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        auto poll_dur = 5ms;

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [](pid_t tid) { return tid % 2 == 0; },
            thread_discovery::ProcPoll(poll_dur));

        {
            std::unique_ptr<Session> s(new Session(p));

            const int thd_count = 40;

            std::barrier b(thd_count + 1);
            std::vector<std::thread> thds;
            oneapi::tbb::concurrent_vector<pid_t> tids;

            // Threads themselves adds <tid> % 3 = 0
            // Test body adds <tid> % 3 = 1
            // Poller adds <tid> % 3 = 2
            //
            // ThdMon does not expose a way to stop failure-injecting a thread
            // until it dies, so test here removes <tid> % 3 < 2 and thread
            // itself removes <tid> % 3 = 2.
            for (int i = 0; i < thd_count; i++) {
                thds.emplace_back([&]() {
                    auto tid = gettid();
                    tids.push_back(tid);

                    b.arrive_and_wait(); // 1
                    if (tid % 3 == 0) {
                        s->add();
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
                    if (tid % 3 == 2) {
                        s->remove();
                    }

                    b.arrive_and_wait(); // 4

                    ret = f.read();
                    EXPECT_TRUE(ret.has_value());
                    EXPECT_EQ(ret.value(), "foo");
                });
            }

            b.arrive_and_wait(); // 1
            for (auto tid : tids) {
                if (tid % 3 == 1) {
                    s->add(tid);
                }
            }
            std::this_thread::sleep_for(poll_dur * 2);
            b.arrive_and_wait(); // 2
            b.arrive_and_wait(); // 3
            for (auto tid : tids) {
                if (tid % 3 < 2) {
                    s->remove(tid);
                }
            }
            b.arrive_and_wait(); // 4

            for (auto& t : thds) t.join();
        };
    }

    TEST(Lib_ThdMon, DiscoversAndFailureInjectsAllThreads) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        auto poll_dur = 1ms;

        std::random_device rd;
        thread_local std::mt19937 rnd_eng(rd());

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [test_thd](pid_t tid) { return tid % 2 == 0 && tid != test_thd; },
            thread_discovery::ProcPoll(poll_dur));

        int thd_count = 40;
        std::uniform_int_distribution<int> delay_ms(0, 10);
        auto session_starter = ceil(
            std::normal_distribution<double>{
                static_cast<double>(thd_count / 2), // mean
                static_cast<double>(thd_count / 10) // sd
             }(rnd_eng));

        std::barrier b(thd_count + 1);

        struct {
            std::mutex m;
            std::condition_variable cv;
            std::unique_ptr<Session> inst;
        } session;

        std::vector<std::thread> thds;

        for (int i = 0; i < thd_count; i++) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(delay_ms(rnd_eng)));

            thds.emplace_back([&]() {
                if (i == session_starter) {
                    std::unique_lock<std::mutex> l(session.m);
                    session.inst = std::make_unique<Session>(p);
                    session.cv.notify_all();
                } else {
                    std::unique_lock<std::mutex> l(session.m);
                    if (session.inst == nullptr) {
                        session.cv.wait(
                            l,
                            [&]() { return session.inst != nullptr; });
                    }
                }

                // wait long enough for poller to discover the thread
                std::this_thread::sleep_for(5ms);

                auto tid = gettid();

                auto ret = f.read();
                if (tid % 2 == 0) {
                    EXPECT_FALSE(ret.has_value());
                } else {
                    EXPECT_TRUE(ret.has_value());
                    EXPECT_EQ(ret.value(), "foo");
                }

                b.arrive_and_wait(); // 1
                b.arrive_and_wait(); // 2

                ret = f.read();
                EXPECT_TRUE(ret.has_value());
                EXPECT_EQ(ret.value(), "foo");
            });
        }

        b.arrive_and_wait(); // 1
        session.inst.reset();
        b.arrive_and_wait(); // 2

        for (auto& t : thds) t.join();
    }

    TEST(Lib_ThdMon, DoesNotRunAnyThreadsInNoThreadConfig) {
        TmpFile f;
        f.write("foo");

        auto test_thd = gettid();

        auto poll_dur = 1ms;

        std::random_device rd;
        thread_local std::mt19937 rnd_eng(rd());

        sysfail::Plan p(
            { {SYS_read, {1.0, 0, 0us, {{EIO, 1}}}} },
            [test_thd](pid_t tid) { return tid % 2 == 0 && tid != test_thd; },
            thread_discovery::None{});


        std::vector<std::thread> thds;

        auto pid = getpid();
        auto my_thread_count = [pid]() -> int {
            namespace fs = std::filesystem;
            int count = 0;
            for (const auto& entry : fs::directory_iterator(tasks_dir)) {
                count++;
            }
            return count;
        };

        {
            Session s(p);

            EXPECT_EQ(my_thread_count(), 1);

            std::this_thread::sleep_for(5ms);

            EXPECT_EQ(my_thread_count(), 1);
        }

        for (auto& t : thds) t.join();
    }
}