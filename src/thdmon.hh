#ifndef _THDMON_HH
#define _THDMON_HH

#include <functional>
#include <filesystem>
#include <thread>
#include <condition_variable>
#include <semaphore>

#include "signal.hh"
#include "sysfail.hh"

namespace sysfail {
    enum class DiscThdSt { // discovered thread state
        Self,
        Existing,
        Spawned,
        Terminated
    };

    using ThdEvtHdlr = std::function<void(pid_t, DiscThdSt)>;

    class ThdMon {
        const ThdEvtHdlr handler;
        std::chrono::microseconds poll_itvl;
        std::thread poller_thd;
        std::binary_semaphore poll_initialized{0};

        struct {
            std::mutex stop_mtx;
            std::condition_variable stop_cv;
            bool stop = false;
        } stop_ctrl;

        using gen_t = uint32_t;
        gen_t gen = 0;
        std::unordered_map<pid_t, gen_t> known_thds;

        void process();
        void scan_tasks();
    public:
        ThdMon(
            const thread_discovery::Strategy& config,
            ThdEvtHdlr handler);
        ~ThdMon();
    };
}

#endif