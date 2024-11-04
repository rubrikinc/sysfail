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
        ThdMon( const thread_discovery::Strategy& config, ThdEvtHdlr handler);

        ~ThdMon();

        void rescan_threads();
    };
}

#endif