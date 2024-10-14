#include <iostream>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <chrono>
#include <thread>

#include "thdmon.hh"

namespace fs = std::filesystem;

namespace {
    static fs::path tasks_dir_for(pid_t pid) {
        return fs::path("/proc") / std::to_string(pid) / "task";
    }
}

sysfail::ThdMon::ThdMon(
    pid_t pid,
    const thread_discovery::ProcPoll& config,
    ThdEvtHdlr handler
) : pid(pid), poll_itvl(config.itvl) {
    // Found the hard way that inotify does not work for /proc
    // so for now we poll!
    // TODO: replace this with netlink cn_proc based monitoring

    tasks_dir_for(pid);
    if (! fs::exists(tasks_dir_for(pid))) {
        throw std::runtime_error("No such process" + std::to_string(pid));
    }

    poller_thd = std::thread(&ThdMon::process, this, handler);
    poll_initialized.acquire();
}

sysfail::ThdMon::~ThdMon() {
    {
        std::lock_guard<std::mutex> l(stop_ctrl.stop_mtx);
        stop_ctrl.stop = true;
        stop_ctrl.stop_cv.notify_all();
    }
    poller_thd.join();
}

void sysfail::ThdMon::process(ThdEvtHdlr handler) {
    using namespace std::chrono_literals;

    fs::path task_dir = tasks_dir_for(pid);

    using gen_t = uint32_t;
    const gen_t gen_start = 0;
    std::unordered_map<pid_t, gen_t> known_thds;

    pid_t self = gettid();
    known_thds.insert({self, gen_start});
    handler(self, DiscThdSt::Self);
    bool run = true;
    std::unique_lock<std::mutex> l(stop_ctrl.stop_mtx);
    for (gen_t g = gen_start; run; g++) {
        for (const auto& entry : fs::directory_iterator(task_dir)) {
            auto name = entry.path().filename().string();
            pid_t tid = std::stoi(name);
            auto it = known_thds.find(tid);
            if (it == known_thds.end()) {
                handler(
                    tid,
                    g == gen_start ? DiscThdSt::Existing : DiscThdSt::Spawned);
                known_thds.insert({tid, g});
            } else {
                it->second = g;
            }
        }
        std::vector<pid_t> to_remove;
        for (auto [tid, gen] : known_thds) {
            if (gen < g) {
                to_remove.push_back(tid);
            }
        }
        for (auto tid : to_remove) {
            handler(tid, DiscThdSt::Terminated);
            known_thds.erase(tid);
        }
        if (g == gen_start) {
            poll_initialized.release();
        }
        stop_ctrl.stop_cv.wait_for(
            l,
            poll_itvl,
            [&](){ return stop_ctrl.stop; });
        run = ! stop_ctrl.stop;
    }
}