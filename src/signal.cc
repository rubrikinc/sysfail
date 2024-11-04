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

#include <iostream>
#include <cstring>
#include <functional>
#include <cassert>

#include "signal.hh"
#include "syscall.hh"

void sysfail::enable_handler(signal_t signal, sigaction_t hdlr) {
    struct sigaction action;
    sigset_t mask;

    memset(&action, 0, sizeof(action));
    sigemptyset(&mask);

    action.sa_sigaction = hdlr;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    action.sa_mask = mask;

    if (sigaction(signal, &action, nullptr) != 0) {
        auto err_str = std::strerror(errno);
        std::cerr << "Failed to set new sigaction: " << err_str << std::endl;
        throw std::runtime_error(
            "Failed to set new sigaction for signal " +
            std::to_string(signal) + " err: " + err_str);
    }
}

void sysfail::_send_signal(
    pid_t tid,
    int sig,
    void* t,
    std::function<void(void*)> on_esrch
) {
    auto pid = getpid();
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    info.si_code = SI_QUEUE;
    info.si_pid = pid;
    info.si_uid = getuid();
    info.si_value = { .sival_ptr = t };

    auto ret = sysfail::syscall(
        pid,
        tid,
        sig,
        reinterpret_cast<uint64_t>(&info),
        0,
        0,
        SYS_rt_tgsigqueueinfo);
    if (ret < 0) {
        if (ret == -ESRCH) {
            // the thread died without telling us it was dying but we don't want
            // to deadlock so we release
            if (t && on_esrch) on_esrch(t);
            return;
        }
    }
    assert(ret == 0);
}

