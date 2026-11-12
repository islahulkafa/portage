#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "poll/epoll_manager.hpp"
#include "signal/signal_manager.hpp"
#include "signal/signal_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <thread>

using namespace portage::signal;
using namespace portage::syscall;
using namespace portage::ndk;
using namespace portage::fs;
using namespace portage::poll;

class RealTimeSignalTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        SignalManager::Instance().Reset();
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        portage::thread::ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override
    {
        SyscallDispatcher::Shutdown();
        SignalManager::Instance().Reset();
    }

    portage::mem::AddressSpace as_;
    Vfs vfs_;
    FileDescriptorTable fd_table_;
};

TEST_F(RealTimeSignalTest, RealTimeSignalQueuingAndFifoOrdering)
{
    auto &sm = SignalManager::Instance();

    LinuxSiginfo info1{};
    info1.si_code = LINUX_SI_QUEUE;
    info1._sifields.rt.si_sigval.sival_int = 100;

    LinuxSiginfo info2{};
    info2.si_code = LINUX_SI_QUEUE;
    info2._sifields.rt.si_sigval.sival_int = 200;

    LinuxSiginfo info_high{};
    info_high.si_code = LINUX_SI_QUEUE;
    info_high._sifields.rt.si_sigval.sival_int = 300;

    // Queue SIGRTMIN + 2 first, then two SIGRTMIN + 1 instances
    EXPECT_EQ(sm.SendSignal(0, 0, LINUX_SIGRTMIN + 2, &info_high), 0);
    EXPECT_EQ(sm.SendSignal(0, 0, LINUX_SIGRTMIN + 1, &info1), 0);
    EXPECT_EQ(sm.SendSignal(0, 0, LINUX_SIGRTMIN + 1, &info2), 0);

    uint64_t mask = (1ULL << (LINUX_SIGRTMIN + 1 - 1)) | (1ULL << (LINUX_SIGRTMIN + 2 - 1));

    // Lower signal number (SIGRTMIN + 1) must be dequeued first in FIFO order
    LinuxSiginfo out{};
    EXPECT_EQ(sm.DequeueSignal(0, mask, &out), LINUX_SIGRTMIN + 1);
    EXPECT_EQ(out._sifields.rt.si_sigval.sival_int, 100);

    EXPECT_EQ(sm.DequeueSignal(0, mask, &out), LINUX_SIGRTMIN + 1);
    EXPECT_EQ(out._sifields.rt.si_sigval.sival_int, 200);

    // Then SIGRTMIN + 2
    EXPECT_EQ(sm.DequeueSignal(0, mask, &out), LINUX_SIGRTMIN + 2);
    EXPECT_EQ(out._sifields.rt.si_sigval.sival_int, 300);

    EXPECT_EQ(sm.DequeueSignal(0, mask, &out), 0);
}

TEST_F(RealTimeSignalTest, SigtimedwaitWithImmediateTimeoutAndSignals)
{
    uint64_t mask = 1ULL << (LINUX_SIGRTMIN - 1);
    LinuxSiginfo out{};
    GuestTimespec zero_ts{0, 0};

    *portage_errno() = 0;
    EXPECT_EQ(portage_sigtimedwait(&mask, &out, &zero_ts), -1);
    EXPECT_EQ(*portage_errno(), 11 /* EAGAIN */);

    EXPECT_EQ(portage_sigqueue(0, LINUX_SIGRTMIN, 0x12345), 0);
    int sig = portage_sigtimedwait(&mask, &out, &zero_ts);
    EXPECT_EQ(sig, LINUX_SIGRTMIN);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(out._sifields.rt.si_sigval.sival_ptr), 0x12345ULL);
}

TEST_F(RealTimeSignalTest, SignalfdCreateReadAndDrain)
{
    uint64_t mask = (1ULL << (LINUX_SIGUSR1 - 1)) | (1ULL << (LINUX_SIGRTMIN + 3 - 1));
    int sfd = portage_signalfd(-1, &mask, LINUX_SFD_NONBLOCK | LINUX_SFD_CLOEXEC);
    ASSERT_GE(sfd, 0);

    // Non-blocking read when empty returns EAGAIN
    LinuxSignalfdSiginfo ssi[2]{};
    *portage_errno() = 0;
    EXPECT_EQ(portage_read(sfd, ssi, sizeof(ssi)), -1);
    EXPECT_EQ(*portage_errno(), 11 /* EAGAIN */);

    // Post SIGRTMIN + 3 with payload and SIGUSR1
    EXPECT_EQ(portage_sigqueue(0, LINUX_SIGRTMIN + 3, 777), 0);
    EXPECT_EQ(SignalManager::Instance().SendSignal(0, 0, LINUX_SIGUSR1), 0);

    int64_t bytes = portage_read(sfd, ssi, sizeof(ssi));
    ASSERT_EQ(bytes, static_cast<int64_t>(2 * sizeof(LinuxSignalfdSiginfo)));

    // SIGUSR1 (10) < SIGRTMIN + 3 (35), so SIGUSR1 is delivered first
    EXPECT_EQ(ssi[0].ssi_signo, static_cast<uint32_t>(LINUX_SIGUSR1));
    EXPECT_EQ(ssi[1].ssi_signo, static_cast<uint32_t>(LINUX_SIGRTMIN + 3));
    EXPECT_EQ(ssi[1].ssi_ptr, 777ULL);

    // Drained
    *portage_errno() = 0;
    EXPECT_EQ(portage_read(sfd, ssi, sizeof(ssi)), -1);
    EXPECT_EQ(*portage_errno(), 11 /* EAGAIN */);

    EXPECT_EQ(portage_close(sfd), 0);
}

TEST_F(RealTimeSignalTest, SignalfdEpollIntegration)
{
    uint64_t mask = 1ULL << (LINUX_SIGRTMIN + 5 - 1);
    int sfd = portage_signalfd4(-1, &mask, sizeof(mask), LINUX_SFD_NONBLOCK);
    ASSERT_GE(sfd, 0);

    int epfd = static_cast<int>(SyscallDispatcher::Dispatch(291 /* epoll_create1 */, 0, 0, 0, 0, 0, 0));
    ASSERT_GE(epfd, 0);

    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = 42;
    EXPECT_EQ(SyscallDispatcher::Dispatch(233 /* epoll_ctl */, static_cast<uint64_t>(epfd),
                                          1 /* EPOLL_CTL_ADD */, static_cast<uint64_t>(sfd),
                                          reinterpret_cast<uint64_t>(&ev), 0, 0),
              0);

    LinuxEpollEvent out_ev{};
    EXPECT_EQ(SyscallDispatcher::Dispatch(232 /* epoll_wait */, static_cast<uint64_t>(epfd),
                                          reinterpret_cast<uint64_t>(&out_ev), 1, 0, 0, 0),
              0);

    // Send matching RT signal from a background thread
    std::thread sender([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        portage_sigqueue(0, LINUX_SIGRTMIN + 5, 999);
    });

    int ready = static_cast<int>(SyscallDispatcher::Dispatch(
        232 /* epoll_wait */, static_cast<uint64_t>(epfd), reinterpret_cast<uint64_t>(&out_ev), 1,
        1000, 0, 0));
    sender.join();

    EXPECT_EQ(ready, 1);
    EXPECT_EQ(out_ev.events & LINUX_EPOLLIN, LINUX_EPOLLIN);
    EXPECT_EQ(out_ev.data, 42ULL);

    LinuxSignalfdSiginfo ssi{};
    EXPECT_EQ(portage_read(sfd, &ssi, sizeof(ssi)),
              static_cast<int64_t>(sizeof(LinuxSignalfdSiginfo)));
    EXPECT_EQ(ssi.ssi_signo, static_cast<uint32_t>(LINUX_SIGRTMIN + 5));
    EXPECT_EQ(ssi.ssi_ptr, 999ULL);

    // Now epoll should report 0 ready events
    EXPECT_EQ(SyscallDispatcher::Dispatch(232 /* epoll_wait */, static_cast<uint64_t>(epfd),
                                          reinterpret_cast<uint64_t>(&out_ev), 1, 0, 0, 0),
              0);

    EXPECT_EQ(portage_close(epfd), 0);
    EXPECT_EQ(portage_close(sfd), 0);
}

TEST_F(RealTimeSignalTest, SigqueueAndSiginfoPayload)
{
    uint64_t mask = 1ULL << (LINUX_SIGRTMAX - 1);
    uintptr_t magic_ptr = 0xDEADBEEFCAFEBABEULL;

    EXPECT_EQ(portage_sigqueue(0, LINUX_SIGRTMAX, magic_ptr), 0);

    LinuxSiginfo out{};
    int sig = portage_sigwaitinfo(&mask, &out);
    EXPECT_EQ(sig, LINUX_SIGRTMAX);
    EXPECT_EQ(out.si_signo, LINUX_SIGRTMAX);
    EXPECT_EQ(out.si_code, LINUX_SI_QUEUE);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(out._sifields.rt.si_sigval.sival_ptr), magic_ptr);
}
