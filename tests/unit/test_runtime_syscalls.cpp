#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "poll/poll_types.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace portage::syscall;
using namespace portage::poll;

class RuntimeSyscallsTest : public ::testing::Test {
  protected:
    void SetUp() override { SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_); }

    void TearDown() override { SyscallDispatcher::Shutdown(); }

    portage::mem::AddressSpace as_{};
    portage::fs::Vfs vfs_{};
    portage::fs::FileDescriptorTable fd_table_{};
};

TEST_F(RuntimeSyscallsTest, GetrandomProducesBytesAndValidatesArgs)
{
    uint8_t buf[32]{};
    int64_t ret = SyscallDispatcher::Dispatch(318, reinterpret_cast<uint64_t>(buf), 32, 0, 0, 0, 0);
    EXPECT_EQ(ret, 32);

    bool non_zero = false;
    for (uint8_t b : buf) {
        if (b != 0) {
            non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(non_zero);

    EXPECT_EQ(SyscallDispatcher::Dispatch(318, 0, 32, 0, 0, 0, 0), LINUX_EFAULT);
    EXPECT_EQ(SyscallDispatcher::Dispatch(318, reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0, 0), 0);
    EXPECT_EQ(SyscallDispatcher::Dispatch(318, reinterpret_cast<uint64_t>(buf), 32, 0x100, 0, 0, 0),
              LINUX_EINVAL);
}

TEST_F(RuntimeSyscallsTest, SysinfoPopulatesRamAndUptime)
{
    LinuxSysinfo info{};
    int64_t ret = SyscallDispatcher::Dispatch(99, reinterpret_cast<uint64_t>(&info), 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(info.uptime, 0);
    EXPECT_GT(info.totalram, 0ULL);
    EXPECT_GT(info.freeram, 0ULL);
    EXPECT_EQ(info.mem_unit, 1U);
    EXPECT_EQ(info.procs, 1U);

    EXPECT_EQ(SyscallDispatcher::Dispatch(99, 0, 0, 0, 0, 0, 0), LINUX_EFAULT);
}

TEST_F(RuntimeSyscallsTest, GetrusagePopulatesTimesAndMaxRss)
{
    LinuxRusage usage{};
    int64_t ret_self = SyscallDispatcher::Dispatch(98, LINUX_RUSAGE_SELF,
                                                   reinterpret_cast<uint64_t>(&usage), 0, 0, 0, 0);
    EXPECT_EQ(ret_self, 0);
    EXPECT_GE(usage.ru_utime.tv_sec, 0);
    EXPECT_GE(usage.ru_stime.tv_sec, 0);
    EXPECT_GT(usage.ru_maxrss, 0);

    std::memset(&usage, 0, sizeof(usage));
    int64_t ret_thread = SyscallDispatcher::Dispatch(
        98, LINUX_RUSAGE_THREAD, reinterpret_cast<uint64_t>(&usage), 0, 0, 0, 0);
    EXPECT_EQ(ret_thread, 0);

    EXPECT_EQ(SyscallDispatcher::Dispatch(98, LINUX_RUSAGE_SELF, 0, 0, 0, 0, 0), LINUX_EFAULT);
}

TEST_F(RuntimeSyscallsTest, MremapShrinksAndGrowsWithMaymove)
{
    void *addr = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        9, 0, 4096, 3 /* PROT_READ|PROT_WRITE */, 0x22 /* MAP_PRIVATE|MAP_ANONYMOUS */, -1, 0));
    ASSERT_NE(addr, reinterpret_cast<void *>(-1));

    std::strcpy(static_cast<char *>(addr), "PortageMremapPayload");

    int64_t fail_grow =
        SyscallDispatcher::Dispatch(25, reinterpret_cast<uint64_t>(addr), 4096, 8192, 0, 0, 0);
    EXPECT_EQ(fail_grow, LINUX_ENOMEM);

    int64_t new_addr = SyscallDispatcher::Dispatch(25, reinterpret_cast<uint64_t>(addr), 4096, 8192,
                                                   LINUX_MREMAP_MAYMOVE, 0, 0);
    ASSERT_GT(new_addr, 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(new_addr), "PortageMremapPayload");

    int64_t shrunk = SyscallDispatcher::Dispatch(25, new_addr, 8192, 4096, 0, 0, 0);
    EXPECT_EQ(shrunk, new_addr);
    EXPECT_STREQ(reinterpret_cast<const char *>(shrunk), "PortageMremapPayload");

    SyscallDispatcher::Dispatch(11, shrunk, 4096, 0, 0, 0, 0);
}

TEST_F(RuntimeSyscallsTest, Eventfd2CounterReadWriteAndSemaphore)
{
    int fd = SyscallDispatcher::Dispatch(290, 5, 0, 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    uint64_t add_val = 10;
    int64_t w_ret =
        SyscallDispatcher::Dispatch(1, fd, reinterpret_cast<uint64_t>(&add_val), 8, 0, 0, 0);
    EXPECT_EQ(w_ret, 8);

    uint64_t read_val = 0;
    int64_t r_ret =
        SyscallDispatcher::Dispatch(0, fd, reinterpret_cast<uint64_t>(&read_val), 8, 0, 0, 0);
    EXPECT_EQ(r_ret, 8);
    EXPECT_EQ(read_val, 15ULL);

    SyscallDispatcher::Dispatch(3, fd, 0, 0, 0, 0, 0);

    int sem_fd =
        SyscallDispatcher::Dispatch(290, 2, LINUX_EFD_SEMAPHORE | LINUX_EFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(sem_fd, 3);

    read_val = 0;
    r_ret =
        SyscallDispatcher::Dispatch(0, sem_fd, reinterpret_cast<uint64_t>(&read_val), 8, 0, 0, 0);
    EXPECT_EQ(r_ret, 8);
    EXPECT_EQ(read_val, 1ULL);

    read_val = 0;
    r_ret =
        SyscallDispatcher::Dispatch(0, sem_fd, reinterpret_cast<uint64_t>(&read_val), 8, 0, 0, 0);
    EXPECT_EQ(r_ret, 8);
    EXPECT_EQ(read_val, 1ULL);

    r_ret =
        SyscallDispatcher::Dispatch(0, sem_fd, reinterpret_cast<uint64_t>(&read_val), 8, 0, 0, 0);
    EXPECT_EQ(r_ret, LINUX_EAGAIN);

    SyscallDispatcher::Dispatch(3, sem_fd, 0, 0, 0, 0, 0);
}

TEST_F(RuntimeSyscallsTest, Eventfd2IntegratesWithEpollAndPoll)
{
    int efd = SyscallDispatcher::Dispatch(290, 0, LINUX_EFD_NONBLOCK, 0, 0, 0, 0);
    ASSERT_GE(efd, 3);

    int epfd = SyscallDispatcher::Dispatch(291, 0, 0, 0, 0, 0, 0);
    ASSERT_GE(epfd, 3);

    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = 0xAA;
    int64_t ctl_ret = SyscallDispatcher::Dispatch(233, epfd, 1 /* EPOLL_CTL_ADD */, efd,
                                                  reinterpret_cast<uint64_t>(&ev), 0, 0);
    EXPECT_EQ(ctl_ret, 0);

    LinuxEpollEvent out_evs[1]{};
    int64_t w_cnt =
        SyscallDispatcher::Dispatch(232, epfd, reinterpret_cast<uint64_t>(out_evs), 1, 0, 0, 0);
    EXPECT_EQ(w_cnt, 0);

    LinuxPollFd pfd{};
    pfd.fd = efd;
    pfd.events = LINUX_POLLIN;
    int64_t p_cnt = SyscallDispatcher::Dispatch(7, reinterpret_cast<uint64_t>(&pfd), 1, 0, 0, 0, 0);
    EXPECT_EQ(p_cnt, 0);
    EXPECT_EQ(pfd.revents, 0);

    uint64_t one = 1;
    SyscallDispatcher::Dispatch(1, efd, reinterpret_cast<uint64_t>(&one), 8, 0, 0, 0);

    w_cnt = SyscallDispatcher::Dispatch(232, epfd, reinterpret_cast<uint64_t>(out_evs), 1, 0, 0, 0);
    EXPECT_EQ(w_cnt, 1);
    EXPECT_EQ(out_evs[0].data, 0xAA);

    p_cnt = SyscallDispatcher::Dispatch(7, reinterpret_cast<uint64_t>(&pfd), 1, 0, 0, 0, 0);
    EXPECT_EQ(p_cnt, 1);
    EXPECT_TRUE(pfd.revents & LINUX_POLLIN);

    SyscallDispatcher::Dispatch(3, efd, 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, epfd, 0, 0, 0, 0, 0);
}

TEST_F(RuntimeSyscallsTest, Pwrite64WritesToFile)
{
    auto tmp = std::filesystem::temp_directory_path() / "pwrite64_test.bin";
    auto ws = tmp.wstring();
    HANDLE h = CreateFileW(ws.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    int fd = fd_table_.Allocate(h, 0, portage::fs::DeviceType::Regular);
    ASSERT_GE(fd, 0);

    const char data[] = "hello";
    int64_t ret = SyscallDispatcher::Dispatch(18, static_cast<uint64_t>(fd),
                                              reinterpret_cast<uint64_t>(data), 5, 0, 0, 0);
    EXPECT_EQ(ret, 5);
    fd_table_.Close(fd);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST_F(RuntimeSyscallsTest, PwritevWritesToFile)
{
    auto tmp = std::filesystem::temp_directory_path() / "pwritev_test.bin";
    auto ws = tmp.wstring();
    HANDLE h = CreateFileW(ws.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    int fd = fd_table_.Allocate(h, 0, portage::fs::DeviceType::Regular);
    ASSERT_GE(fd, 0);

    char part1[] = "hello ";
    char part2[] = "world";
    LinuxIovec iov[2]{
        {.iov_base = part1, .iov_len = 6},
        {.iov_base = part2, .iov_len = 5},
    };
    int64_t ret = SyscallDispatcher::Dispatch(296, static_cast<uint64_t>(fd),
                                              reinterpret_cast<uint64_t>(iov), 2, 0, 0, 0);
    EXPECT_EQ(ret, 11);
    fd_table_.Close(fd);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST_F(RuntimeSyscallsTest, ProcessVmReadvSameProcess)
{
    uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dst[8] = {};
    LinuxIovec local_iov{.iov_base = dst, .iov_len = 8};
    LinuxIovec remote_iov{.iov_base = src, .iov_len = 8};
    int64_t ret = SyscallDispatcher::Dispatch(310, static_cast<uint64_t>(GetCurrentProcessId()),
                                              reinterpret_cast<uint64_t>(&local_iov), 1,
                                              reinterpret_cast<uint64_t>(&remote_iov), 1, 0);
    EXPECT_EQ(ret, 8);
    EXPECT_EQ(std::memcmp(src, dst, 8), 0);
}

TEST_F(RuntimeSyscallsTest, ProcessVmReadvCrossProcessReturnsEperm)
{
    LinuxIovec local_iov{.iov_base = nullptr, .iov_len = 0};
    LinuxIovec remote_iov{.iov_base = nullptr, .iov_len = 0};
    int64_t ret = SyscallDispatcher::Dispatch(310, 1, reinterpret_cast<uint64_t>(&local_iov), 0,
                                              reinterpret_cast<uint64_t>(&remote_iov), 0, 0);
    EXPECT_EQ(ret, LINUX_EPERM);
}

TEST_F(RuntimeSyscallsTest, ProcessVmWritevSameProcess)
{
    uint8_t src[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t dst[8] = {};
    LinuxIovec local_iov{.iov_base = src, .iov_len = 8};
    LinuxIovec remote_iov{.iov_base = dst, .iov_len = 8};
    int64_t ret = SyscallDispatcher::Dispatch(311, static_cast<uint64_t>(GetCurrentProcessId()),
                                              reinterpret_cast<uint64_t>(&local_iov), 1,
                                              reinterpret_cast<uint64_t>(&remote_iov), 1, 0);
    EXPECT_EQ(ret, 8);
    EXPECT_EQ(std::memcmp(src, dst, 8), 0);
}
