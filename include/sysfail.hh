#ifndef _SYSFAIL_HH
#define _SYSFAIL_HH

#include <chrono>
#include <memory>
#include <map>
#include <functional>
#include <sys/syscall.h>

namespace sysfail {
    using Syscall = int;

    const int OneMillion = 1000000;

    struct Outcome {
        double fail_probability;
        double delay_probability;
        std::chrono::microseconds max_delay;
    };
    struct Plan {
        const std::unordered_map<Syscall, const Outcome> outcomes;
        const std::function<bool(pid_t)> selector;
        Plan(
            const std::unordered_map<Syscall, const Outcome>& _outcomes,
            const std::function<bool(pid_t)>& _selector
        ) : outcomes(_outcomes), selector(_selector) {}
        Plan(Plan&& _plan): outcomes(_plan.outcomes), selector(_plan.selector) {}
        Plan() : outcomes({}), selector([](pid_t) { return false; }) {}
    };
    class Session {
    public:
        explicit Session(const Plan& _plan);
        bool stop();
    };
}

#endif