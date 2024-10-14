#ifndef _THDMON_HH
#define _THDMON_HH

#include <functional>
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
        const pid_t pid;
        const std::chrono::microseconds poll_itvl;
        std::thread poller_thd;
        std::binary_semaphore poll_initialized{0};
        struct {
            std::mutex stop_mtx;
            std::condition_variable stop_cv;
            bool stop = false;
        } stop_ctrl;
        void process(ThdEvtHdlr handler);
    public:
        ThdMon(
            pid_t pid,
            const thread_discovery::ProcPoll& config,
            ThdEvtHdlr handler);
        ~ThdMon();
    };
}

#endif