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