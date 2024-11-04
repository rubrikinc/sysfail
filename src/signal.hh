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

#ifndef _SIGNAL_HH
#define _SIGNAL_HH

#include <signal.h>

namespace sysfail {
    using signal_t = int;
    using sigaction_t = void (*) (int, siginfo_t *, void *);

    void enable_handler(signal_t signal, sigaction_t hdlr);

    void _send_signal(
        pid_t tid,
        int sig,
        void* t,
        std::function<void(void*)> on_esrch);

    template <typename T> void send_signal(
        pid_t tid,
        int sig,
        T* t,
        std::function<void(T*)> on_esrch = [](T*) { /* No such process */ }
    ) {
        _send_signal(tid, sig, reinterpret_cast<void*>(t), [&](void* t) {
            on_esrch(reinterpret_cast<T*>(t));
        });
    }
}

#endif