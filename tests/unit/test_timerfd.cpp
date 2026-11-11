#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/looper.hpp"
#include "poll/poll_types.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>

using namespace portage::syscall;
using namespace portage::poll;

namespace {

struct LooperCallbackState {
    int calls{0};
    int last_fd{-1};
    int last_events{0};
    uint64_t read_val{0};
};

__attribute__((sysv_abi)) int LooperTimerCallback(int fd, int events, void *data)
{
    auto *state = static_cast<LooperCallbackState *>(data);
    state->calls++;
    state->last_fd = fd;
    state->last_events = events;
    uint64_t val = 0;
    if (SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&val), sizeof(val), 0, 0,
                                    0) == 8) {
        state->read_val = val;
    }
    return 1;
}

} // namespace

class TimerfdTest : public ::testing::Test {
  protected:
    void SetUp() override { SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_); }

    void TearDown() override { SyscallDispatcher::Shutdown(); }

    portage::mem::AddressSpace as_{};
    portage::fs::Vfs vfs_{};
    portage::fs::FileDescriptorTable fd_table_{};
};

TEST_F(TimerfdTest, CreateValidatesClockAndFlags)
{
    int fd_mono = SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, 0, 0, 0, 0, 0);
    EXPECT_GE(fd_mono, 3);

    int fd_real = SyscallDispatcher::Dispatch(283, LINUX_CLOCK_REALTIME,
                                              LINUX_TFD_NONBLOCK | LINUX_TFD_CLOEXEC, 0, 0, 0, 0);
    EXPECT_GE(fd_real, 3);

    EXPECT_EQ(SyscallDispatcher::Dispatch(283, 99, 0, 0, 0, 0, 0), LINUX_EINVAL);
    EXPECT_EQ(SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, 0x100, 0, 0, 0, 0),
              LINUX_EINVAL);

    SyscallDispatcher::Dispatch(3, fd_mono, 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, fd_real, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, SettimeArmsOneShotTimer)
{
    int fd = SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, 0, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    LinuxItimerspec spec{};
    spec.it_value.tv_sec = 0;
    spec.it_value.tv_nsec = 15'000'000;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    int64_t set_ret =
        SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0);
    ASSERT_EQ(set_ret, 0);

    uint64_t exp = 0;
    int64_t read_ret =
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), sizeof(exp), 0, 0, 0);
    EXPECT_EQ(read_ret, 8);
    EXPECT_EQ(exp, 1ULL);

    LinuxItimerspec curr{};
    int64_t get_ret =
        SyscallDispatcher::Dispatch(287, fd, reinterpret_cast<uint64_t>(&curr), 0, 0, 0, 0);
    EXPECT_EQ(get_ret, 0);
    EXPECT_EQ(curr.it_value.tv_sec, 0);
    EXPECT_EQ(curr.it_value.tv_nsec, 0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, 0);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, SettimePeriodicTimerAccumulatesExpirations)
{
    int fd = SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, 0, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    LinuxItimerspec spec{};
    spec.it_value.tv_sec = 0;
    spec.it_value.tv_nsec = 10'000'000;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 10'000'000;

    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);

    std::this_thread::sleep_for(std::chrono::milliseconds(45));

    uint64_t exp = 0;
    int64_t read_ret =
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), sizeof(exp), 0, 0, 0);
    EXPECT_EQ(read_ret, 8);
    EXPECT_GE(exp, 3ULL);

    LinuxItimerspec disarm{};
    LinuxItimerspec old_spec{};
    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&disarm),
                                          reinterpret_cast<uint64_t>(&old_spec), 0, 0),
              0);
    EXPECT_EQ(old_spec.it_interval.tv_nsec, 10'000'000);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, GettimeReturnsRemainingTimeAndInterval)
{
    int fd = SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, 0, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    LinuxItimerspec spec{};
    spec.it_value.tv_sec = 1;
    spec.it_value.tv_nsec = 0;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 50'000'000;

    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);

    LinuxItimerspec curr{};
    ASSERT_EQ(SyscallDispatcher::Dispatch(287, fd, reinterpret_cast<uint64_t>(&curr), 0, 0, 0, 0),
              0);
    EXPECT_EQ(curr.it_interval.tv_sec, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, 50'000'000);
    int64_t rem_ns = curr.it_value.tv_sec * 1'000'000'000LL + curr.it_value.tv_nsec;
    EXPECT_GT(rem_ns, 0LL);
    EXPECT_LE(rem_ns, 1'000'000'000LL);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, TimerfdNonblockingReturnsEagain)
{
    int fd =
        SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, LINUX_TFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    uint64_t exp = 0;
    EXPECT_EQ(
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), sizeof(exp), 0, 0, 0),
        LINUX_EAGAIN);
    EXPECT_EQ(SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), 4, 0, 0, 0),
              LINUX_EINVAL);

    LinuxItimerspec spec{};
    spec.it_value.tv_sec = 1;
    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);
    EXPECT_EQ(
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), sizeof(exp), 0, 0, 0),
        LINUX_EAGAIN);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, TimerfdIntegratesWithEpollAndPoll)
{
    int fd =
        SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, LINUX_TFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    int epfd = SyscallDispatcher::Dispatch(291, 0, 0, 0, 0, 0, 0);
    ASSERT_GE(epfd, 3);

    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = 0xCAFE;
    ASSERT_EQ(SyscallDispatcher::Dispatch(233, epfd, LINUX_EPOLL_CTL_ADD, fd,
                                          reinterpret_cast<uint64_t>(&ev), 0, 0),
              0);

    LinuxItimerspec spec{};
    spec.it_value.tv_sec = 0;
    spec.it_value.tv_nsec = 15'000'000;
    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);

    LinuxEpollEvent out_ev{};
    EXPECT_EQ(
        SyscallDispatcher::Dispatch(232, epfd, reinterpret_cast<uint64_t>(&out_ev), 1, 0, 0, 0), 0);

    int64_t n =
        SyscallDispatcher::Dispatch(232, epfd, reinterpret_cast<uint64_t>(&out_ev), 1, 200, 0, 0);
    EXPECT_EQ(n, 1);
    EXPECT_NE(out_ev.events & LINUX_EPOLLIN, 0U);
    EXPECT_EQ(out_ev.data, 0xCAFEULL);

    uint64_t exp = 0;
    EXPECT_EQ(
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&exp), sizeof(exp), 0, 0, 0),
        8);
    EXPECT_EQ(exp, 1ULL);

    spec.it_value.tv_nsec = 15'000'000;
    ASSERT_EQ(SyscallDispatcher::Dispatch(286, fd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);

    LinuxPollFd pfd{};
    pfd.fd = fd;
    pfd.events = LINUX_POLLIN;
    int64_t p_res =
        SyscallDispatcher::Dispatch(7, reinterpret_cast<uint64_t>(&pfd), 1, 200, 0, 0, 0);
    EXPECT_EQ(p_res, 1);
    EXPECT_NE(pfd.revents & LINUX_POLLIN, 0);

    SyscallDispatcher::Dispatch(3, epfd, 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(TimerfdTest, TimerfdIntegratesWithALooper)
{
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASSERT_NE(looper, nullptr);

    int evfd = SyscallDispatcher::Dispatch(290, 0, LINUX_EFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(evfd, 3);
    uint64_t ev_val = 2;
    ASSERT_EQ(SyscallDispatcher::Dispatch(1, evfd, reinterpret_cast<uint64_t>(&ev_val),
                                          sizeof(ev_val), 0, 0, 0),
              8);

    LooperCallbackState ev_state{};
    ASSERT_EQ(ALooper_addFd(looper, evfd, 1, ALOOPER_EVENT_INPUT, LooperTimerCallback, &ev_state),
              1);
    int poll_ev = ALooper_pollOnce(100, nullptr, nullptr, nullptr);
    EXPECT_EQ(poll_ev, ALOOPER_POLL_CALLBACK);
    EXPECT_EQ(ev_state.calls, 1);
    EXPECT_EQ(ev_state.read_val, 2ULL);
    ALooper_removeFd(looper, evfd);

    int tfd =
        SyscallDispatcher::Dispatch(283, LINUX_CLOCK_MONOTONIC, LINUX_TFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(tfd, 3);

    LinuxItimerspec spec{};
    spec.it_value.tv_nsec = 10'000'000;
    ASSERT_EQ(SyscallDispatcher::Dispatch(286, tfd, 0, reinterpret_cast<uint64_t>(&spec), 0, 0, 0),
              0);

    LooperCallbackState t_state{};
    ASSERT_EQ(ALooper_addFd(looper, tfd, 2, ALOOPER_EVENT_INPUT, LooperTimerCallback, &t_state), 1);
    int poll_t = ALooper_pollOnce(200, nullptr, nullptr, nullptr);
    EXPECT_EQ(poll_t, ALOOPER_POLL_CALLBACK);
    EXPECT_EQ(t_state.calls, 1);
    EXPECT_NE(t_state.last_events & ALOOPER_EVENT_INPUT, 0);
    EXPECT_EQ(t_state.read_val, 1ULL);

    ALooper_removeFd(looper, tfd);
    SyscallDispatcher::Dispatch(3, evfd, 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, tfd, 0, 0, 0, 0, 0);
}
