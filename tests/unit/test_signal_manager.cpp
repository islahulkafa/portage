#include "signal/signal_manager.hpp"
#include <gtest/gtest.h>

using namespace portage::signal;

TEST(SignalManagerTest, SetsAndGetsSignalAction)
{
    auto &sm = SignalManager::Instance();

    LinuxSigaction act{};
    act.sa_handler = 0x12345678;
    act.sa_flags = LINUX_SA_SIGINFO | LINUX_SA_RESTORER;
    act.sa_restorer = 0x87654321;
    act.sa_mask = 0xFF;

    int ret = sm.SetAction(LINUX_SIGSEGV, act, nullptr);
    EXPECT_EQ(ret, 0);

    LinuxSigaction retrieved{};
    ret = sm.GetAction(LINUX_SIGSEGV, &retrieved);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(retrieved.sa_handler, act.sa_handler);
    EXPECT_EQ(retrieved.sa_flags, act.sa_flags);
    EXPECT_EQ(retrieved.sa_restorer, act.sa_restorer);
    EXPECT_EQ(retrieved.sa_mask, act.sa_mask);
}

TEST(SignalManagerTest, RejectsUnmaskableSignals)
{
    auto &sm = SignalManager::Instance();
    LinuxSigaction act{};
    act.sa_handler = 0x12345;

    EXPECT_EQ(sm.SetAction(LINUX_SIGKILL, act, nullptr), -22); // -EINVAL
    EXPECT_EQ(sm.SetAction(LINUX_SIGSTOP, act, nullptr), -22); // -EINVAL
    EXPECT_EQ(sm.SetAction(0, act, nullptr), -22);             // -EINVAL
}

TEST(SignalManagerTest, RestoresRegisterContext)
{
    auto &sm = SignalManager::Instance();

    LinuxUcontext uc{};
    uc.uc_mcontext.rax = 0x1111;
    uc.uc_mcontext.rbx = 0x2222;
    uc.uc_mcontext.rip = 0x400500;
    uc.uc_mcontext.rsp = 0x7ffffff0;

    CONTEXT win32_ctx{};
    bool ok = sm.RestoreFromContext(&uc, &win32_ctx);
    EXPECT_TRUE(ok);
    EXPECT_EQ(win32_ctx.Rax, 0x1111ULL);
    EXPECT_EQ(win32_ctx.Rbx, 0x2222ULL);
    EXPECT_EQ(win32_ctx.Rip, 0x400500ULL);
    EXPECT_EQ(win32_ctx.Rsp, 0x7ffffff0ULL);
}
