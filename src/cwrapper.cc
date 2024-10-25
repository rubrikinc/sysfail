
extern "C" {
    #include "sysfail.h"
}
#include "session.hh"

extern "C" {
    using namespace sysfail;
    sysfail_session_t* sysfail_start(const sysfail_plan_t *c_plan) {
        std::unordered_map<Syscall, const Outcome> outcomes;
        for (auto o = c_plan->syscall_outcomes; o != nullptr; o = o->next) {
            std::map<Errno, double> error_weights;
            for (uint32_t i = 0; i < o->outcome.num_errors; i++) {
                error_weights.insert({
                    o->outcome.error_wts[i].nerror,
                    o->outcome.error_wts[i].weight});
            }
            Outcome outcome{
                {o->outcome.fail.p, o->outcome.fail.after_bias},
                {o->outcome.delay.p, o->outcome.delay.after_bias},
                std::chrono::microseconds(o->outcome.max_delay_usec),
                error_weights,
                [
                    e=o->outcome.eligible,
                    ctx=o->outcome.ctx
                ](const greg_t* regs) -> bool { return e(ctx, regs);}};
            outcomes.insert({o->syscall, outcome});
        }
        auto selector = [
            s=c_plan->selector,
            ctx=c_plan->ctx
        ](pid_t tid) -> bool { return s(ctx, tid); };
        thread_discovery::Strategy tdisc_strategy{
            [&]() -> thread_discovery::Strategy {
                switch (c_plan->strategy) {
                    case sysfail_tdisc_none:
                        return thread_discovery::None{};
                    case sysfail_tdisk_poll:
                        return thread_discovery::ProcPoll(
                            std::chrono::microseconds(c_plan->config.poll_itvl_usec));
                    default:
                        std::cerr << "Invalid thread discovery strategy, "
                                  << ", defaulting to `none`" << std::endl;
                        return thread_discovery::None{};
                }
            }()};

        auto session = new sysfail::Session({
            outcomes,
            selector,
            tdisc_strategy});
        return new sysfail_session_t{
            session,
            [](sysfail_session_t* s) {
                delete static_cast<sysfail::Session*>(s->data);
                delete s;
            }};
    }
}
