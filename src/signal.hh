#ifndef _SIGNAL_HH
#define _SIGNAL_HH

#include <signal.h>

namespace sysfail {
    using signal_t = int;
    using sigaction_t = void (*) (int, siginfo_t *, void *);

    void enable_handler(signal_t signal, sigaction_t hdlr);
}

#endif