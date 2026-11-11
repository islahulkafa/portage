#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "poll/poll_types.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

using namespace portage::syscall;

class PosixBatch2Test : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_posix2_test";
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
        std::filesystem::create_directories(test_dir_, ec);

        vfs_.SetRootfs(test_dir_);
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        portage::thread::ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override
    {
        SyscallDispatcher::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    portage::mem::AddressSpace as_{};
    portage::fs::Vfs vfs_{};
    portage::fs::FileDescriptorTable fd_table_{};
};

TEST_F(PosixBatch2Test, SelectAndPselectPollPipesAndEventFds)
{
    int pipefds[2] = {-1, -1};
    ASSERT_EQ(fd_table_.CreatePipe(pipefds, 0), 0);
    ASSERT_GE(pipefds[0], 3);
    ASSERT_GE(pipefds[1], 3);

    LinuxFdSet readfds{};
    size_t idx = static_cast<size_t>(pipefds[0]) / 64;
    size_t bit = static_cast<size_t>(pipefds[0]) % 64;
    readfds.fds_bits[idx] |= (1ULL << bit);

    char byte = 'X';
    EXPECT_EQ(
        SyscallDispatcher::Dispatch(1, pipefds[1], reinterpret_cast<uint64_t>(&byte), 1, 0, 0, 0),
        1);

    LinuxTimeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100'000;
    int64_t sel_res = SyscallDispatcher::Dispatch(
        23, pipefds[0] + 1, reinterpret_cast<uint64_t>(&readfds), 0, 0,
        reinterpret_cast<uint64_t>(&tv), 0);
    EXPECT_EQ(sel_res, 1);
    EXPECT_NE(readfds.fds_bits[idx] & (1ULL << bit), 0ULL);

    char out_byte = 0;
    EXPECT_EQ(SyscallDispatcher::Dispatch(0, pipefds[0], reinterpret_cast<uint64_t>(&out_byte), 1,
                                          0, 0, 0),
              1);
    EXPECT_EQ(out_byte, 'X');

    int evfd = SyscallDispatcher::Dispatch(290, 0, LINUX_EFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(evfd, 3);
    uint64_t ev_val = 5;
    EXPECT_EQ(SyscallDispatcher::Dispatch(1, evfd, reinterpret_cast<uint64_t>(&ev_val),
                                          sizeof(ev_val), 0, 0, 0),
              8);

    std::memset(&readfds, 0, sizeof(readfds));
    idx = static_cast<size_t>(evfd) / 64;
    bit = static_cast<size_t>(evfd) % 64;
    readfds.fds_bits[idx] |= (1ULL << bit);

    LinuxTimespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = 100'000'000;
    int64_t psel_res = SyscallDispatcher::Dispatch(
        270, evfd + 1, reinterpret_cast<uint64_t>(&readfds), 0, 0, reinterpret_cast<uint64_t>(&ts),
        0);
    EXPECT_EQ(psel_res, 1);
    EXPECT_NE(readfds.fds_bits[idx] & (1ULL << bit), 0ULL);

    SyscallDispatcher::Dispatch(3, pipefds[0], 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, pipefds[1], 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, evfd, 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch2Test, SchedYieldAndAffinityMasks)
{
    EXPECT_EQ(SyscallDispatcher::Dispatch(24, 0, 0, 0, 0, 0, 0), 0);

    uint64_t mask = 0;
    int64_t get_res = SyscallDispatcher::Dispatch(204, 0, sizeof(mask),
                                                  reinterpret_cast<uint64_t>(&mask), 0, 0, 0);
    EXPECT_EQ(get_res, 8);
    EXPECT_NE(mask, 0ULL);

    int64_t set_res = SyscallDispatcher::Dispatch(203, 0, sizeof(mask),
                                                  reinterpret_cast<uint64_t>(&mask), 0, 0, 0);
    EXPECT_EQ(set_res, 0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(204, 0, 4, reinterpret_cast<uint64_t>(&mask), 0, 0, 0),
              LINUX_EINVAL);
    EXPECT_EQ(SyscallDispatcher::Dispatch(204, 0, sizeof(mask), 0, 0, 0, 0), LINUX_EFAULT);
}

TEST_F(PosixBatch2Test, GettimeofdayReturnsValidWallClock)
{
    LinuxTimeval tv{};
    LinuxTimezone tz{};
    int64_t ret = SyscallDispatcher::Dispatch(
        96, reinterpret_cast<uint64_t>(&tv), reinterpret_cast<uint64_t>(&tz), 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_GT(tv.tv_sec, 1700000000LL);
    EXPECT_GE(tv.tv_usec, 0LL);
    EXPECT_LT(tv.tv_usec, 1000000LL);
    EXPECT_EQ(tz.tz_minuteswest, 0);
    EXPECT_EQ(tz.tz_dsttime, 0);
}

TEST_F(PosixBatch2Test, SetAndGetRobustListPerThread)
{
    uintptr_t head_in = 0x5000;
    size_t len_in = 24;
    EXPECT_EQ(SyscallDispatcher::Dispatch(273, head_in, len_in, 0, 0, 0, 0), 0);

    uintptr_t head_out = 0;
    size_t len_out = 0;
    int64_t get_ret = SyscallDispatcher::Dispatch(
        274, 0, reinterpret_cast<uint64_t>(&head_out), reinterpret_cast<uint64_t>(&len_out), 0, 0,
        0);
    EXPECT_EQ(get_ret, 0);
    EXPECT_EQ(head_out, 0x5000ULL);
    EXPECT_EQ(len_out, 24ULL);

    EXPECT_EQ(SyscallDispatcher::Dispatch(273, head_in, 16, 0, 0, 0, 0), LINUX_EINVAL);
}

TEST_F(PosixBatch2Test, FsyncAndFdatasyncFlushRegularFile)
{
    const char *rel_path = "/test_flush.dat";
    int fd = SyscallDispatcher::Dispatch(257, LINUX_AT_FDCWD, reinterpret_cast<uint64_t>(rel_path),
                                         LINUX_O_CREAT | LINUX_O_RDWR, 0644, 0, 0);
    ASSERT_GE(fd, 3);

    const char *payload = "TestDataPayload12345678";
    EXPECT_GT(SyscallDispatcher::Dispatch(1, fd, reinterpret_cast<uint64_t>(payload),
                                          std::strlen(payload), 0, 0, 0),
              0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(74, fd, 0, 0, 0, 0, 0), 0);
    EXPECT_EQ(SyscallDispatcher::Dispatch(75, fd, 0, 0, 0, 0, 0), 0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(74, 999, 0, 0, 0, 0, 0), LINUX_EBADF);
    EXPECT_EQ(SyscallDispatcher::Dispatch(75, 999, 0, 0, 0, 0, 0), LINUX_EBADF);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch2Test, TruncateAndFallocateExtendAndShrinkFile)
{
    const char *rel_path = "/test_alloc.dat";
    int fd = SyscallDispatcher::Dispatch(257, LINUX_AT_FDCWD, reinterpret_cast<uint64_t>(rel_path),
                                         LINUX_O_CREAT | LINUX_O_RDWR, 0644, 0, 0);
    ASSERT_GE(fd, 3);

    const char *initial = "12345678";
    EXPECT_EQ(SyscallDispatcher::Dispatch(1, fd, reinterpret_cast<uint64_t>(initial), 8, 0, 0, 0),
              8);

    EXPECT_EQ(SyscallDispatcher::Dispatch(285, fd, 0, 0, 128, 0, 0), 0);

    LinuxStat64 sb{};
    EXPECT_EQ(SyscallDispatcher::Dispatch(5, fd, reinterpret_cast<uint64_t>(&sb), 0, 0, 0, 0), 0);
    EXPECT_EQ(sb.st_size, 128);

    EXPECT_EQ(SyscallDispatcher::Dispatch(285, fd, 0x10, 0, 64, 0, 0), LINUX_EOPNOTSUPP);

    EXPECT_EQ(SyscallDispatcher::Dispatch(76, reinterpret_cast<uint64_t>(rel_path), 32, 0, 0, 0, 0),
              0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(5, fd, reinterpret_cast<uint64_t>(&sb), 0, 0, 0, 0), 0);
    EXPECT_EQ(sb.st_size, 32);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch2Test, UtimensatUpdatesAndOmitsTimestamps)
{
    const char *rel_path = "/test_utime.dat";
    int fd = SyscallDispatcher::Dispatch(257, LINUX_AT_FDCWD, reinterpret_cast<uint64_t>(rel_path),
                                         LINUX_O_CREAT | LINUX_O_RDWR, 0644, 0, 0);
    ASSERT_GE(fd, 3);

    LinuxTimespec times[2]{};
    times[0].tv_sec = 1700000100LL;
    times[0].tv_nsec = 0;
    times[1].tv_sec = 1700000200LL;
    times[1].tv_nsec = 0;

    int64_t u_ret = SyscallDispatcher::Dispatch(280, fd, 0, reinterpret_cast<uint64_t>(times), 0, 0, 0);
    EXPECT_EQ(u_ret, 0);

    LinuxStat64 sb{};
    EXPECT_EQ(SyscallDispatcher::Dispatch(5, fd, reinterpret_cast<uint64_t>(&sb), 0, 0, 0, 0), 0);
    EXPECT_EQ(sb.st_mtime, 1700000200LL);

    times[0].tv_sec = 1700000300LL;
    times[0].tv_nsec = 0;
    times[1].tv_sec = 0;
    times[1].tv_nsec = LINUX_UTIME_OMIT;

    EXPECT_EQ(
        SyscallDispatcher::Dispatch(280, fd, 0, reinterpret_cast<uint64_t>(times), 0, 0, 0), 0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(5, fd, reinterpret_cast<uint64_t>(&sb), 0, 0, 0, 0), 0);
    EXPECT_EQ(sb.st_mtime, 1700000200LL);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);
}
