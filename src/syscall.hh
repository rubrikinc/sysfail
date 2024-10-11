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