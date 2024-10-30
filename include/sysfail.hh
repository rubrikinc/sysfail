#ifndef _SYSFAIL_HH
#define _SYSFAIL_HH

#include <chrono>
#include <memory>
#include <map>
#include <functional>
#include <shared_mutex>
#include <sys/syscall.h>
#include <chrono>
#include <variant>
#include <signal.h>
#include <stdexcept>

namespace sysfail {
    // Syscall number
    using Syscall = int;
    // Error number (errno)
    using Errno = int;
    // Signal number
    using Signal = int;

    /**
     * Probability of failure / delay
     */
    struct Probability {
        // [0, 1] 0 => never fail, 1 => always fail
        const double p;
         // [0, 1] 0 => before syscall, 1 => after syscall
        const double after_bias;

        Probability( // TODO: move to def file
            double p,
            double after_bias = 0
        ) : p(p), after_bias(after_bias) {
            if (p < 0 || p > 1) {
                throw std::invalid_argument("Probability must be in [0, 1]");
            }
            if (after_bias < 0 || after_bias > 1) {
                throw std::invalid_argument("Bias must be in [0, 1]");
            }
        }
    };

    // Invocation predicate filters syscall invocations at per-invocation level.
    // Call is failure-injected only if the predicate returns true.
    // This or one of the arity-specific types (below) can be used to filter
    // syscalls.
    using InvocationPredicate = const std::function<bool(const greg_t*)>;

    namespace invp {
        // Syscall argument
        using A = greg_t;
        // Predicate for syscalls that do not accept any arguments.
        // Eg. SYS_getpid
        using Zero = std::function<bool(Syscall)>;
        // Predicate for syscalls that accept one argument. Eg. SYS_exit_group
        using One = std::function<bool(Syscall, A)>;
        // Predicate for syscalls that accept two arguments. Eg. SYS_listen
        using Two = std::function<bool(Syscall, A, A)>;
        // Predicate for syscalls that accept three arguments. Eg. SYS_write
        using Three = std::function<bool(Syscall, A, A, A)>;
        // Predicate for syscalls that accept four arguments. Eg. SYS_openat2
        using Four = std::function<bool(Syscall, A, A, A, A)>;
        // Predicate for syscalls that accept five arguments. Eg. SYS_pwritev
        using Five = std::function<bool(Syscall, A, A, A, A, A)>;
        // Predicate for syscalls that accept all six arguments. Eg. SYS_mmap
        using Six = std::function<bool(Syscall, A, A, A, A, A, A)>;

        // Arity independent predicate type
        using P = std::variant<Zero, One, Two, Three, Four, Five, Six>;

        // Generates a general invocation predicate given arity-aware definition
        InvocationPredicate p(P p);
    }

    /**
     * Outcome of a syscall
     */
    struct Outcome {
        // Probability of failure
        const Probability fail;
        // Probability of delay
        const Probability delay;
        // Maximum delay in microseconds
        const std::chrono::microseconds max_delay;
        // Errors to be presented to the call=site when failure is injected
        // and relative weights. Higher weight makes the error more likely. This
        // does not affect the probability of failure, only the distribution of
        // errors when the syscall fails.
        const std::map<Errno, double> error_weights;
        // Eligibility predicate for the syscall
        InvocationPredicate eligible;
    };

    namespace thread_discovery {
        using namespace std::chrono_literals;

        // Either add / remove threads manually or use Session API to trigger a
        // single isolated poll to discover threads. Sysfail does not
        // auto-detect and inject failures into new threads.
        struct None {};

        // Poll at regular intervals to discover new threads. Manual controls
        // such as add / remove / discover can also be used in conjunction with
        // automatic discovery.
        struct ProcPoll {
            const std::chrono::microseconds itvl;

            ProcPoll( std::chrono::microseconds itvl = 10ms) : itvl(itvl) {}
        };

        // Strategy for thread discovery
        using Strategy = std::variant<ProcPoll, None>;
    }

    /**
     * Plan for failure injection
     */
    struct Plan {
        // Outcome by syscall
        const std::unordered_map<Syscall, const Outcome> outcomes;
        // Predicate that picks threads eligible for failure injection
        const std::function<bool(pid_t)> selector;
        // Strategy for thread discovery
        const thread_discovery::Strategy thd_disc;

        Plan(
            const std::unordered_map<Syscall, const Outcome>& outcomes,
            const std::function<bool(pid_t)>& selector,
            const thread_discovery::Strategy& thd_disc
        ) : outcomes(outcomes), selector(selector), thd_disc(thd_disc) {}
        Plan(const Plan& plan):
            outcomes(plan.outcomes),
            selector(plan.selector),
            thd_disc(plan.thd_disc) {}
        Plan() :
            outcomes({}),
            selector([](pid_t) { return false; }),
            thd_disc(thread_discovery::None{}) {}
    };

    /**
     * Session is the top-level handle for failure-injection in the process.
     *
     * Plan controls the failure and delay injection behavior while APIs on the
     * session allow test / application to control behavior at thread or
     * process level.
     */
    class Session {
        std::shared_mutex lck;
    public:
        // Start failure / delay injection. Based on the thread-discovery
        // strategy in the plan failure injection may be enabled across all
        // or just the calling thread. All threads regardless of the strategy
        // or manual controls are first presented to the selector predicate and
        // failure-injected only if the predicate returns true.
        explicit Session(const Plan& plan);
        // Stop failure / delay injection and terminate the session.
        ~Session();
        // Enable failure / delay injection for the calling thread.
        void add();
        // Disable failure / delay injection for the calling thread.
        void remove();
        // Enable failure / delay injection for the thread with the given tid.
        void add(pid_t tid);
        // Disable failure / delay injection for the thread with the given tid.
        void remove(pid_t tid);
        // Discover threads on-demand. This can be used by the test /
        // application to trigger a single isolated poll to discover threads and
        // can be used regardless of the thread-discovery strategy in the plan.
        void discover_threads();
    };
}

#endif