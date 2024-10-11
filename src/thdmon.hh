#ifndef _THDMON_HH
#define _THDMON_HH

#include <functional>
#include <thread>

#include "signal.hh"

namespace sysfail {
    enum class ThdSt {
        Self,
        Existing,
        Spawned,
        Terminated
    };

    using ThdEvtHdlr = std::function<void(pid_t, ThdSt)>;

    class ThdMon {
        const pid_t pid;
        const std::chrono::microseconds poll_itvl;
        std::thread thd;
        std::atomic<bool> run = true;
        void process(ThdEvtHdlr handler);
    public:
        ThdMon(pid_t pid, std::chrono::microseconds poll_itvl, ThdEvtHdlr handler);
        ~ThdMon();
    };
}

#endif