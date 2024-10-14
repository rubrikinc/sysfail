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
#include <oneapi/tbb/concurrent_vector.h>

#include "cisq.hh"

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
}