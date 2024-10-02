#ifndef _SYSCALL_HH
#define _SYSCALL_HH

#include <sysfail.hh>

namespace sysfail {
    long syscall(
        long arg1, // %rdi
        long arg2, // %rsi
        long arg3, // %rdx
        long arg4, // %rcx
        long arg5, // %r8
        long arg6, // %r9
        Syscall syscall
    );
}

#endif