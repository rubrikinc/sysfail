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