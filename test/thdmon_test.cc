#include <gtest/gtest.h>
#include <expected>
#include <chrono>

#include <oneapi/tbb/concurrent_vector.h>

#include "thdmon.hh"

using namespace testing;
using namespace std::chrono_literals;

namespace sysfail {
    struct TMonEvt {
        pid_t tid;
        DiscThdSt state;
    };
    struct TestExp {
        pid_t tid;
        std::string id;
    };
    enum class TestEvt {
        Start,
        T1Started,
        TxRunning,
        T1Remains,
        End
    };

    void with_delay(std::function<void()> f) {
        std::this_thread::sleep_for(50ms);
        f();
    }

    using Evt = std::variant<TMonEvt, TestEvt, TestExp>;
    using Matcher = std::function<bool(const Evt&)>;

    template <typename T> struct M {
        std::vector<std::function<bool(const T&)>> ms;

        M(std::vector<std::function<bool(const T&)>>&& _ms) : ms(std::move(_ms)) {}

        bool operator()(const Evt& e) {
            return std::holds_alternative<T>(e) &&
                std::all_of(
                    ms.begin(),
                    ms.end(),
                    [&](auto m) { return m(std::get<T>(e)); });
        }
    };

    struct Evts {
        std::vector<Evt> es;

        Evts(oneapi::tbb::concurrent_vector<Evt>&& _es) {
            es = std::vector<Evt>(_es.begin(), _es.end());
            _es.clear();
        }

        template <typename E, typename B> int before(M<E> expected, M<B> ref) {
            int count = 0;
            auto ref_pt = std::find_if(es.begin(), es.end(), ref);
            for (auto it = es.begin(); it != ref_pt; it++) {
                if (expected(*it)) count++;
            }
            return count;
        }

        template <typename E, typename A> int after(M<E> expected, M<A> ref) {
            int count = 0;
            auto ref_pt = std::find_if(es.begin(), es.end(), ref);
            for (auto it = ref_pt; it != es.end(); it++) {
                if (expected(*it)) count++;
            }
            return count;
        }

        template <typename E, typename B, typename A> int between(
            M<E> expected,
            M<B> before,
            M<A> after
        ) {
            int count = 0;
            auto ref_b = std::find_if(es.begin(), es.end(), before);
            auto ref_a = std::find_if(es.begin(), es.end(), after);
            for (auto it = ref_b; it != ref_a; it++) {
                if (expected(*it)) count++;
            }
            return count;
        }

        template <typename E> int count(M<E> expected) {
            return std::count_if(es.begin(), es.end(), expected);
        }
    };

    using v = std::vector<int>;

    using P = thread_discovery::ProcPoll;

	TEST(ThdMon, ReportsThreadActivity) {
        oneapi::tbb::concurrent_vector<Evt> live_evts;

        std::binary_semaphore t0_sem(0);
        std::atomic<pid_t> t0_tid = 0, t1_tid = 0;
        std::thread t0([&]() {
            with_delay([&]() {
                live_evts.push_back(TestExp{gettid(), "t0"});
                t0_tid = gettid();
                t0_sem.acquire(); // TODO: try commenting this line
            });
        });

        {
            ThdMon tmon(P{10ms}, [&](pid_t tid, DiscThdSt state) {
                live_evts.push_back(TMonEvt{tid, state});
            });

            with_delay([&live_evts]() { live_evts.push_back(TestEvt::Start); });

            std::binary_semaphore t1_sem(0), t1_running(1);

            std::thread t1([&]() {
                with_delay([&]() {
                    t1_running.release();
                    live_evts.push_back(TestExp{gettid(), "t1"});
                    t1_tid = gettid();
                    t1_sem.acquire(); // TODO: try commenting this line
                });
            });

            t1_running.acquire();
            with_delay([&live_evts]() {
                live_evts.push_back(TestEvt::T1Started);
            });

            std::vector<std::thread> thds;

            for (int i = 0; i < 5; i++) {
                thds.emplace_back([&]() {
                    with_delay([&live_evts]() {
                        live_evts.push_back(TestExp{gettid(), "tX"});
                    });
                });
            }
            with_delay([&live_evts]() {
                live_evts.push_back(TestEvt::TxRunning);
            });

            for (auto& t : thds) t.join();

            with_delay([&live_evts]() {
                live_evts.push_back(TestEvt::T1Remains);
            });

            t1_sem.release();
            t1.join();

            with_delay([&live_evts]() { live_evts.push_back(TestEvt::End); });
        }

        t0_sem.release();
        t0.join();

        Evts evts(std::move(live_evts));

        EXPECT_EQ(
            (v{
                evts.before(
                    M<TMonEvt>{{
                        [](auto& e) { return e.state == DiscThdSt::Existing; },
                        [&](auto& e) { return e.tid == t0_tid || e.tid == gettid(); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::Start; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [&](auto& e) { return e.tid == t0_tid || e.tid == gettid(); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::Start; }}})}),
            (v{2, 2})) << "pre-existing threads were not identified";

        EXPECT_EQ(
            (v{
                evts.before(
                    M<TMonEvt>{{[&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::Start; }}}),
                evts.before(
                    M<TMonEvt>{{[&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestExp>{{[&](auto& e) { return e.tid == t1_tid; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [](auto& e) { return e.state == DiscThdSt::Spawned; },
                        [&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [](auto& e) { return e.state == DiscThdSt::Terminated; },
                        [&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Remains; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [](auto& e) { return e.state == DiscThdSt::Terminated; },
                        [&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::End; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [&](auto& e) { return e.tid == t1_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::End; }}})}),
            (v{0, 1, 1, 0, 1, 2})) << "Spwan / reap of t1 was not reported";

        pid_t mon_tid = 0;

        EXPECT_EQ(
            evts.before(
                    M<TMonEvt>{{[&](auto& e) {
                        auto self = e.state == DiscThdSt::Self;
                        if (self) mon_tid = e.tid;
                        return self;
                    }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::Start; }}}),
            1) << "Monitor thread was not identified";

        EXPECT_NE(mon_tid, 0);

        EXPECT_EQ(
            (v{
                evts.before(
                    M<TMonEvt>{{[&](auto& e) { return e.tid == mon_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::Start; }}}),
                evts.before(
                    M<TMonEvt>{{
                        [&](auto& e) { return e.tid == mon_tid; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::End; }}}),
                evts.before(
                    M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Self; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::End; }}})}),
            (v{1, 1, 1})) << "Monitor thread was identified too many times";

        std::set<pid_t> non_tx{gettid(), t0_tid, t1_tid};
        std::set<pid_t> tx;
        EXPECT_EQ(
            evts.between(
                M<TMonEvt>{{[&](auto& e) {
                    auto is_tx = ! non_tx.contains(e.tid);
                    if (is_tx) tx.insert(e.tid);
                    return is_tx;
                }}},
                M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}},
                M<TestEvt>{{[](auto& e) { return e == TestEvt::TxRunning; }}}),
            5) << "Transitive threads were not identified";

        EXPECT_EQ(tx.size(), 5);

        EXPECT_EQ(
            (v{
                evts.before(
                    M<TMonEvt>{{[&](auto& e) { return tx.contains(e.tid); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}}),
                evts.after(
                    M<TMonEvt>{{[&](auto& e) { return tx.contains(e.tid); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Remains; }}}),
                evts.between(
                    M<TestExp>{{[&](auto& e) {
                        return e.id == "tX" && tx.contains(e.tid);
                    }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Remains; }}}),
                evts.between(
                    M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Spawned; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::TxRunning; }}}),
                evts.between(
                    M<TMonEvt>{{[&](auto& e) { return tx.contains(e.tid); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Started; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::TxRunning; }}}),
                evts.between(
                    M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Terminated; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::TxRunning; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Remains; }}}),
                evts.between(
                    M<TMonEvt>{{[&](auto& e) { return tx.contains(e.tid); }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::TxRunning; }}},
                    M<TestEvt>{{[](auto& e) { return e == TestEvt::T1Remains; }}})}),
            (v{0, 0, 5, 5, 5, 5, 5})) << "Short-lived threads were identified too many times";

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Spawned; }}}),
            6);

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Terminated; }}}),
            6);

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Existing; }}}),
            2);
	}

    TEST(ThdMon, DoesNotReportSubprocesses) {
        oneapi::tbb::concurrent_vector<Evt> live_evts;

        pid_t child_pid = 0;
        {
            ThdMon tmon(P{10ms}, [&](pid_t tid, DiscThdSt state) {
                live_evts.push_back(TMonEvt{tid, state});
            });

            child_pid = fork();
            std::this_thread::sleep_for(50ms);
            if (child_pid == 0) {
                exit(0);
            }
        }

        Evts evts(std::move(live_evts));

        EXPECT_NE(child_pid, 0);

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Self; }}}),
            1);

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.state == DiscThdSt::Existing; }}}),
            1);

        EXPECT_EQ(
            evts.count(M<TMonEvt>{{[&](auto& e) { return e.tid == child_pid; }}}),
            0);
    }

    TEST(ThdMon, StopsReasonablyQuicklyDespiteHighPollInterval) {
        oneapi::tbb::concurrent_vector<Evt> live_evts;

        auto start_tm = std::chrono::system_clock::now();
        {
            ThdMon tmon(P{30min}, [&](pid_t tid, DiscThdSt state) {
                live_evts.push_back(TMonEvt{tid, state});
            });
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_LT((std::chrono::system_clock::now() - start_tm), 20ms);
    }
}