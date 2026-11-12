#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "net/socket_manager.hpp"
#include "net/socket_types.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace portage::syscall;
using namespace portage::net;

class SyscallCoverageTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        portage::net::SocketManager::Instance().Initialize();
        temp_dir_ = std::filesystem::temp_directory_path() / "portage_syscall_coverage_test";
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
        std::filesystem::create_directories(temp_dir_, ec);
        vfs_.SetRootfs(temp_dir_);
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
    }

    void TearDown() override
    {
        SyscallDispatcher::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    portage::mem::AddressSpace as_{};
    portage::fs::Vfs vfs_{};
    portage::fs::FileDescriptorTable fd_table_{};
    std::filesystem::path temp_dir_{};
};

TEST_F(SyscallCoverageTest, MadviseReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(28, 0x10000, 4096, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, FlockReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(73, 1, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, SetpgidReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(109, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, GetppidReturnsPositive)
{
    int64_t ppid = SyscallDispatcher::Dispatch(110, 0, 0, 0, 0, 0, 0);
    EXPECT_GT(ppid, 0);
}

TEST_F(SyscallCoverageTest, GetpgrpEqualsGetpid)
{
    int64_t pid = SyscallDispatcher::Dispatch(39, 0, 0, 0, 0, 0, 0);
    int64_t pgrp = SyscallDispatcher::Dispatch(111, 0, 0, 0, 0, 0, 0);
    EXPECT_GT(pid, 0);
    EXPECT_EQ(pid, pgrp);
}

TEST_F(SyscallCoverageTest, SchedGetparamWritesZeroPriority)
{
    LinuxSchedParam param{.sched_priority = 42};
    int64_t ret =
        SyscallDispatcher::Dispatch(142, 0, reinterpret_cast<uint64_t>(&param), 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(param.sched_priority, 0);

    int64_t fault = SyscallDispatcher::Dispatch(142, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(fault, LINUX_EFAULT);
}

TEST_F(SyscallCoverageTest, SchedSetschedulerReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(144, 0, LINUX_SCHED_FIFO, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, SchedGetschedulerReturnsSchedOther)
{
    int64_t ret = SyscallDispatcher::Dispatch(145, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, LINUX_SCHED_OTHER);
}

TEST_F(SyscallCoverageTest, PrctlSetNameAndGetName)
{
    const char name[] = "portage_worker";
    int64_t ret_set = SyscallDispatcher::Dispatch(157, LINUX_PR_SET_NAME,
                                                  reinterpret_cast<uint64_t>(name), 0, 0, 0, 0);
    EXPECT_EQ(ret_set, 0);

    char buf[16]{};
    int64_t ret_get = SyscallDispatcher::Dispatch(157, LINUX_PR_GET_NAME,
                                                  reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0);
    EXPECT_EQ(ret_get, 0);
    EXPECT_STREQ(buf, "portage_worker");

    int64_t fault = SyscallDispatcher::Dispatch(157, LINUX_PR_SET_NAME, 0, 0, 0, 0, 0);
    EXPECT_EQ(fault, LINUX_EFAULT);
}

TEST_F(SyscallCoverageTest, ReadvReadsFromPipe)
{
    int pipefds[2]{-1, -1};
    int64_t r_pipe =
        SyscallDispatcher::Dispatch(293, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);
    ASSERT_EQ(r_pipe, 0);

    const char msg[] = "PortagePipeData";
    int64_t r_write = SyscallDispatcher::Dispatch(1, static_cast<uint64_t>(pipefds[1]),
                                                  reinterpret_cast<uint64_t>(msg), 15, 0, 0, 0);
    EXPECT_EQ(r_write, 15);

    char b1[7]{};
    char b2[8]{};
    LinuxIovec iov[2]{
        {.iov_base = b1, .iov_len = 7},
        {.iov_base = b2, .iov_len = 8},
    };

    int64_t r_readv = SyscallDispatcher::Dispatch(19, static_cast<uint64_t>(pipefds[0]),
                                                  reinterpret_cast<uint64_t>(iov), 2, 0, 0, 0);
    EXPECT_EQ(r_readv, 15);
    EXPECT_EQ(std::string_view(b1, 7), "Portage");
    EXPECT_EQ(std::string_view(b2, 8), "PipeData");

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(pipefds[0]), 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(pipefds[1]), 0, 0, 0, 0, 0);
}

TEST_F(SyscallCoverageTest, RenameMovesFile)
{
    std::filesystem::path src = temp_dir_ / "original.txt";
    std::filesystem::path dst = temp_dir_ / "renamed.txt";

    {
        std::ofstream ofs(src);
        ofs << "test payload";
    }
    ASSERT_TRUE(std::filesystem::exists(src));

    int64_t ret =
        SyscallDispatcher::Dispatch(82, reinterpret_cast<uint64_t>("/original.txt"),
                                    reinterpret_cast<uint64_t>("/renamed.txt"), 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_TRUE(std::filesystem::exists(dst));
}

TEST_F(SyscallCoverageTest, RenameatMovesFile)
{
    std::filesystem::path src = temp_dir_ / "at_orig.txt";
    std::filesystem::path dst = temp_dir_ / "at_renamed.txt";

    {
        std::ofstream ofs(src);
        ofs << "at payload";
    }
    ASSERT_TRUE(std::filesystem::exists(src));

    int64_t ret = SyscallDispatcher::Dispatch(
        264, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>("/at_orig.txt"),
        static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>("/at_renamed.txt"), 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_TRUE(std::filesystem::exists(dst));
}

TEST_F(SyscallCoverageTest, Renameat2RejectsNonZeroFlags)
{
    int64_t ret = SyscallDispatcher::Dispatch(
        316, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>("/at_orig.txt"),
        static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>("/at_renamed.txt"), 1, 0);
    EXPECT_EQ(ret, LINUX_EINVAL);
}

TEST_F(SyscallCoverageTest, StatxFillsBasicStats)
{
    std::filesystem::path file = temp_dir_ / "sample.txt";
    {
        std::ofstream ofs(file);
        ofs << "Hello Statx!";
    }
    ASSERT_TRUE(std::filesystem::exists(file));

    LinuxStatx sx{};
    int64_t ret = SyscallDispatcher::Dispatch(
        332, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>("/sample.txt"), 0,
        LINUX_STATX_BASIC_STATS, reinterpret_cast<uint64_t>(&sx), 0);
    EXPECT_EQ(ret, 0);
    EXPECT_NE(sx.stx_mask & LINUX_STATX_SIZE, 0U);
    EXPECT_EQ(sx.stx_size, 12U);
    EXPECT_NE(sx.stx_mode, 0U);
}

TEST_F(SyscallCoverageTest, SendmsgRecvmsgLoopback)
{
    int64_t sock = SyscallDispatcher::Dispatch(41, LINUX_AF_INET, LINUX_SOCK_DGRAM, 0, 0, 0, 0);
    ASSERT_GE(sock, 0);

    portage::net::LinuxSockaddrIn sin{};
    sin.sin_family = LINUX_AF_INET;
    sin.sin_addr.addr = 0x0100007f; // 127.0.0.1 in network byte order
    sin.sin_port = 0;

    int64_t r_bind = SyscallDispatcher::Dispatch(
        49, static_cast<uint64_t>(sock), reinterpret_cast<uint64_t>(&sin), sizeof(sin), 0, 0, 0);
    ASSERT_EQ(r_bind, 0);

    socklen_t slen = sizeof(sin);
    int64_t r_name = SyscallDispatcher::Dispatch(51, static_cast<uint64_t>(sock),
                                                 reinterpret_cast<uint64_t>(&sin),
                                                 reinterpret_cast<uint64_t>(&slen), 0, 0, 0);
    ASSERT_EQ(r_name, 0);

    char chunk1[] = "Hello ";
    char chunk2[] = "Network!";
    LinuxIovec send_iov[2]{
        {.iov_base = chunk1, .iov_len = 6},
        {.iov_base = chunk2, .iov_len = 8},
    };
    LinuxMsghdr send_msg{};
    send_msg.msg_name = &sin;
    send_msg.msg_namelen = sizeof(sin);
    send_msg.msg_iov = send_iov;
    send_msg.msg_iovlen = 2;

    int64_t s_ret = SyscallDispatcher::Dispatch(46, static_cast<uint64_t>(sock),
                                                reinterpret_cast<uint64_t>(&send_msg), 0, 0, 0, 0);
    EXPECT_EQ(s_ret, 14);

    char rchunk1[6]{};
    char rchunk2[8]{};
    LinuxIovec recv_iov[2]{
        {.iov_base = rchunk1, .iov_len = 6},
        {.iov_base = rchunk2, .iov_len = 8},
    };
    portage::net::LinuxSockaddrIn from_sin{};
    LinuxMsghdr recv_msg{};
    recv_msg.msg_name = &from_sin;
    recv_msg.msg_namelen = sizeof(from_sin);
    recv_msg.msg_iov = recv_iov;
    recv_msg.msg_iovlen = 2;

    int64_t r_ret = SyscallDispatcher::Dispatch(47, static_cast<uint64_t>(sock),
                                                reinterpret_cast<uint64_t>(&recv_msg), 0, 0, 0, 0);
    EXPECT_EQ(r_ret, 14);
    EXPECT_EQ(std::string_view(rchunk1, 6), "Hello ");
    EXPECT_EQ(std::string_view(rchunk2, 8), "Network!");

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(sock), 0, 0, 0, 0, 0);
}

TEST_F(SyscallCoverageTest, GetpgidReturnsPid)
{
    int64_t pid = SyscallDispatcher::Dispatch(39, 0, 0, 0, 0, 0, 0);
    int64_t pgid = SyscallDispatcher::Dispatch(121, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(pgid, pid);
}

TEST_F(SyscallCoverageTest, SetsidReturnsPositive)
{
    int64_t ret = SyscallDispatcher::Dispatch(112, 0, 0, 0, 0, 0, 0);
    EXPECT_GT(ret, 0);
}

TEST_F(SyscallCoverageTest, CapgetReturnsZero)
{
    uint8_t buf[64]{};
    int64_t ret = SyscallDispatcher::Dispatch(125, 0, reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, CapsetReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(126, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, GetpriorityReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(140, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, SetpriorityReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(141, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, SchedSetparamReturnZeroAndNullFaults)
{
    LinuxSchedParam param{.sched_priority = 50};
    EXPECT_EQ(SyscallDispatcher::Dispatch(143, 0, reinterpret_cast<uint64_t>(&param), 0, 0, 0, 0), 0);
    EXPECT_EQ(SyscallDispatcher::Dispatch(143, 0, 0, 0, 0, 0, 0), LINUX_EFAULT);
}

TEST_F(SyscallCoverageTest, ClockAdjtimeReturnsZero)
{
    int64_t ret = SyscallDispatcher::Dispatch(305, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(SyscallCoverageTest, PrctlGetDumpable)
{
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_GET_DUMPABLE, 0, 0, 0, 0, 0), 1);
}

TEST_F(SyscallCoverageTest, PrctlSetDumpable)
{
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_SET_DUMPABLE, 1, 0, 0, 0, 0), 0);
}

TEST_F(SyscallCoverageTest, PrctlChildSubreaper)
{
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0, 0), 0);
    int val = -1;
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_GET_CHILD_SUBREAPER,
                                          reinterpret_cast<uint64_t>(&val), 0, 0, 0, 0), 0);
    EXPECT_EQ(val, 0);
}

TEST_F(SyscallCoverageTest, PrctlPdeathsig)
{
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_SET_PDEATHSIG, 15, 0, 0, 0, 0), 0);
    int sig = -1;
    EXPECT_EQ(SyscallDispatcher::Dispatch(157, LINUX_PR_GET_PDEATHSIG,
                                          reinterpret_cast<uint64_t>(&sig), 0, 0, 0, 0), 0);
    EXPECT_EQ(sig, 0);
}
