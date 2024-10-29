#include <variant>

#include "sysfail.hh"
#include "helpers.hh"

namespace sysfail::invp {
    static Syscall s(const greg_t* regs) {
        return regs[REG_RAX];
    }

    static A a1(const greg_t* regs) {
        return regs[REG_RDI];
    }

    static A a2(const greg_t* regs) {
        return regs[REG_RSI];
    }

    static A a3(const greg_t* regs) {
        return regs[REG_RDX];
    }

    static A a4(const greg_t* regs) {
        return regs[REG_R10];
    }

    static A a5(const greg_t* regs) {
        return regs[REG_R8];
    }

    static A a6(const greg_t* regs) {
        return regs[REG_R9];
    }

    InvocationPredicate p(P p) {
        return [=](const greg_t* r) -> bool {
            return std::visit(cases(
                [&](Zero f) {
                    return f(s(r));
                },
                [&](One f) {
                    return f(s(r), a1(r));
                },
                [&](Two f) {
                    return f(s(r), a1(r), a2(r));
                },
                [&](Three f) {
                    return f(s(r), a1(r), a2(r), a3(r));
                },
                [&](Four f) {
                    return f(s(r), a1(r), a2(r), a3(r), a4(r));
                },
                [&](Five f) {
                    return f(s(r), a1(r), a2(r), a3(r), a4(r), a5(r));
                },
                [&](Six f) {
                    return f(s(r), a1(r), a2(r), a3(r), a4(r), a5(r), a6(r));
                }),
                p);
        };
    }
}