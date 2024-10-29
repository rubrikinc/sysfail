#include <gtest/gtest.h>
#include <sysfail.hh>
#include <ucontext.h>
#include <random>
#include <cstring>

using namespace testing;
using namespace std::chrono_literals;

namespace sysfail {
    using namespace invp;

    void z(greg_t *regs) {
        for (int i = 0; i < NGREG; i++) regs[i] = 0;
    }

    TEST(InvPred, GeneratesGeneralInvocationPredicate) {
        std::random_device rd;
        thread_local std::mt19937 rnd_eng(rd());
        std::uniform_int_distribution<A> n(0, 100);

        Syscall e_sys = n(rnd_eng);
        A a0 = n(rnd_eng);
        A e1 = n(rnd_eng);
        A e2 = n(rnd_eng);
        A e3 = n(rnd_eng);
        A e4 = n(rnd_eng);
        A e5 = n(rnd_eng);
        A e6 = n(rnd_eng);


        gregset_t regs;
        std::memset(regs, 0, sizeof(regs));
        regs[REG_RDI] = e1;
        regs[REG_RSI] = e2;
        regs[REG_RDX] = e3;
        regs[REG_R10] = e4;
        regs[REG_R8] = e5;
        regs[REG_R9] = e6;
        regs[REG_RAX] = e_sys;

        auto ret = true;
        if (n(rnd_eng) % 2 == 0) {
            ret = false;
        }

        EXPECT_EQ(
            p(
            [&](Syscall a_sys) {
                EXPECT_EQ(a_sys, e_sys);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1, A a2) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                EXPECT_EQ(a2, e2);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1, A a2, A a3) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                EXPECT_EQ(a2, e2);
                EXPECT_EQ(a3, e3);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1, A a2, A a3, A a4) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                EXPECT_EQ(a2, e2);
                EXPECT_EQ(a3, e3);
                EXPECT_EQ(a4, e4);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1, A a2, A a3, A a4, A a5) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                EXPECT_EQ(a2, e2);
                EXPECT_EQ(a3, e3);
                EXPECT_EQ(a4, e4);
                EXPECT_EQ(a5, e5);
                return ret;
            })(regs),
            ret);

        EXPECT_EQ(
            p(
            [&](Syscall a_sys, A a1, A a2, A a3, A a4, A a5, A a6) {
                EXPECT_EQ(a_sys, e_sys);
                EXPECT_EQ(a1, e1);
                EXPECT_EQ(a2, e2);
                EXPECT_EQ(a3, e3);
                EXPECT_EQ(a4, e4);
                EXPECT_EQ(a5, e5);
                EXPECT_EQ(a6, e6);
                return ret;
            })(regs),
            ret);
    }
}