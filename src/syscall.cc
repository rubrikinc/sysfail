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

#include "syscall.hh"

long sysfail::syscall(
    uint64_t arg1, // %rdi
    uint64_t arg2, // %rsi
    uint64_t arg3, // %rdx
    uint64_t arg4, // %rcx
    uint64_t arg5, // %r8
    uint64_t arg6, // %r9
    Syscall syscall
) {
    register long rax asm("rax") = syscall;
    register long rdi asm("rdi") = arg1;
    register long rsi asm("rsi") = arg2;
    register long rdx asm("rdx") = arg3;
    register long r10 asm("r10") = arg4; // kernel uses r10 in place of rcx
    register long r8 asm("r8") = arg5;
    register long r9 asm("r9") = arg6;

    asm volatile(
        "syscall \n"
        : "=a"(rax)
        : "a"(rax),
          "D"(rdi),
          "S"(rsi),
          "d"(rdx),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory"
    );

    return rax;
}


long sysfail::set_rsp_and_exec_syscall(
    uint64_t arg1, // %rdi
    uint64_t arg2, // %rsi
    uint64_t arg3, // %rdx
    uint64_t arg4, // %rcx
    uint64_t arg5, // %r8
    uint64_t arg6, // %r9
    Syscall syscall,
    uint64_t rsp
) {
    register long rax asm("rax") = syscall;
    register long rdi asm("rdi") = arg1;
    register long rsi asm("rsi") = arg2;
    register long rdx asm("rdx") = arg3;
    register long r10 asm("r10") = arg4; // kernel uses r10 in place of rcx
    register long r8 asm("r8") = arg5;
    register long r9 asm("r9") = arg6;

    asm volatile(
        "mov %1, %%rsp \n"
        "syscall \n"
        : "=a"(rax),
          "=m"(rsp)
        : "a"(rax),
          "D"(rdi),
          "S"(rsi),
          "d"(rdx),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory"
    );

    return rax;
}