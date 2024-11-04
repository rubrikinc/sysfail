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

#ifndef _SYSCALL_HH
#define _SYSCALL_HH

#include <sysfail.hh>

namespace sysfail {
    long syscall(
        uint64_t arg1, // %rdi
        uint64_t arg2, // %rsi
        uint64_t arg3, // %rdx
        uint64_t arg4, // %rcx
        uint64_t arg5, // %r8
        uint64_t arg6, // %r9
        Syscall syscall
    );
}

#endif