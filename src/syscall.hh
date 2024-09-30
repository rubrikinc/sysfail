
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
}