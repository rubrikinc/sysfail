#include <iostream>
#include <cstring>
#include "signal.hh"

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

