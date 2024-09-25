.section .note.GNU-stack,"",@progbits
.section .text
.globl sysfail_restore
sysfail_restore:
    # param: *greg_t
    # look at ucontext.h (__USE_GNU enum section)
    # improve later
    # TL;DR
    # janmejay@jdell:~/projects/rubrik/sysfail(master⚡) » cat '/nix/store/1vp54ln0frvhzgasr2a377mfbwvqdm6i-glibc-2.39-52-dev/include/sys/ucontext.h' | sed '1,50d' | head -49 | grep -P , | awk  -v i=0 '{print i"->"$0; i+=1}'
    # 0->  REG_R8 = 0,
    # 1->  REG_R9,
    # 2->  REG_R10,
    # 3->  REG_R11,
    # 4->  REG_R12,
    # 5->  REG_R13,
    # 6->  REG_R14,
    # 7->  REG_R15,
    # 8->  REG_RDI,
    # 9->  REG_RSI,
    # 10->  REG_RBP,
    # 11->  REG_RBX,
    # 12->  REG_RDX,
    # 13->  REG_RAX,
    # 14->  REG_RCX,
    # 15->  REG_RSP,
    # 16->  REG_RIP,
    # 17->  REG_EFL,
    # 18->  REG_CSGSFS,               /* Actually short cs, gs, fs, __pad0.  */
    # 19->  REG_ERR,
    # 20->  REG_TRAPNO,
    # 21->  REG_OLDMASK,
    # janmejay@jdell:~/projects/rubrik/sysfail(master⚡) »

    movq (%rdi), %r8
    movq 8(%rdi), %r9
    movq 16(%rdi), %r10
    movq 24(%rdi), %r11
    movq 32(%rdi), %r12
    movq 40(%rdi), %r13
    movq 48(%rdi), %r14
    movq 56(%rdi), %r15
    # rdi later
    movq 72(%rdi), %rsi
    movq 80(%rdi), %rbp
    movq 88(%rdi), %rbx
    movq 96(%rdi), %rdx
    # rax later, used to setup stack
    movq 112(%rdi), %rcx
    # 120 - 8 (eflags is pushed)
    movq 120(%rdi), %rsp

    # PC
    movq 128(%rdi), %rax
    pushq %rax

    # eflags
    movq 136(%rdi), %rax
    pushq %rax

    movq 104(%rdi), %rax
    movq 64(%rdi), %rdi

    # eflags
    popfq

    ret