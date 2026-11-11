#include "fs/fd_table.hpp"
#include "fs/proc_fs.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "net/socket_manager.hpp"
#include "poll/epoll_manager.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace portage::syscall;
using namespace portage::net;
using namespace portage::poll;

class ProcFsAndBatch4Test : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_procfs_batch4_test";
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
        for (const auto &entry : std::filesystem::directory_iterator(test_dir_, ec)) {
            SetFileAttributesW(entry.path().wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    portage::mem::AddressSpace as_{};
    portage::fs::Vfs vfs_{};
    portage::fs::FileDescriptorTable fd_table_{};
};

TEST_F(ProcFsAndBatch4Test, ProcFsCanonicalization)
{
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/proc/1234/maps"), "/proc/self/maps");
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/proc/thread-self/comm"), "/proc/self/comm");
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/proc/4321/task/5678/comm"),
              "/proc/self/task/5678/comm");
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/proc/1234/stat"), "/proc/self/stat");
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/proc/sys/kernel/osrelease"),
              "/proc/sys/kernel/osrelease");
    EXPECT_EQ(portage::fs::ProcFs::CanonicalizePath("/sys/devices/system/cpu/online"),
              "/sys/devices/system/cpu/online");
}

TEST_F(ProcFsAndBatch4Test, ProcFsMapsAttribution)
{
    as_.InitBrk(0x40000000);
    (void)as_.Brk(as_.GetBrkStart() + 0x10000);

    std::string maps = portage::fs::ProcFs::Generate("/proc/self/maps", &as_);
    EXPECT_FALSE(maps.empty());
    EXPECT_NE(maps.find("[heap]"), std::string::npos);
}

TEST_F(ProcFsAndBatch4Test, ProcFsIntrospection)
{
    SyscallDispatcher::Dispatch(157, LINUX_PR_SET_NAME,
                                reinterpret_cast<uint64_t>("worker_thread"), 0, 0, 0, 0);
    std::string comm = portage::fs::ProcFs::Generate("/proc/self/comm");
    EXPECT_EQ(comm, "worker_thread\n");

    std::string stat = portage::fs::ProcFs::Generate("/proc/self/stat");
    EXPECT_NE(stat.find("(portage) R"), std::string::npos);

    std::string statm = portage::fs::ProcFs::Generate("/proc/self/statm");
    EXPECT_NE(statm.find("65536"), std::string::npos);

    std::string auxv = portage::fs::ProcFs::Generate("/proc/self/auxv");
    ASSERT_GE(auxv.size(), 16u);
    const auto *auxv_ptr = reinterpret_cast<const uint64_t *>(auxv.data());
    EXPECT_EQ(auxv_ptr[0], 6u);    // AT_PAGESZ
    EXPECT_EQ(auxv_ptr[1], 4096u); // 4KB

    std::string proc_stat = portage::fs::ProcFs::Generate("/proc/stat");
    EXPECT_NE(proc_stat.find("cpu"), std::string::npos);

    std::string uptime = portage::fs::ProcFs::Generate("/proc/uptime");
    EXPECT_FALSE(uptime.empty());

    std::string osrelease = portage::fs::ProcFs::Generate("/proc/sys/kernel/osrelease");
    EXPECT_EQ(osrelease, "6.1.0-android14-x86_64\n");

    std::string boot_id = portage::fs::ProcFs::Generate("/proc/sys/kernel/random/boot_id");
    EXPECT_NE(boot_id.find("4b827734"), std::string::npos);

    std::string cpu_online = portage::fs::ProcFs::Generate("/sys/devices/system/cpu/online");
    EXPECT_FALSE(cpu_online.empty());
}

TEST_F(ProcFsAndBatch4Test, ProcFsListDir)
{
    auto root_entries = portage::fs::ProcFs::ListDir("/proc");
    bool found_cpuinfo = false;
    bool found_self = false;
    bool found_sys = false;
    for (const auto &e : root_entries) {
        if (e.name == "cpuinfo")
            found_cpuinfo = true;
        if (e.name == "self")
            found_self = true;
        if (e.name == "sys")
            found_sys = true;
    }
    EXPECT_TRUE(found_cpuinfo);
    EXPECT_TRUE(found_self);
    EXPECT_TRUE(found_sys);

    auto self_entries = portage::fs::ProcFs::ListDir("/proc/self");
    bool found_maps = false;
    bool found_status = false;
    bool found_fd = false;
    bool found_task = false;
    for (const auto &e : self_entries) {
        if (e.name == "maps")
            found_maps = true;
        if (e.name == "status")
            found_status = true;
        if (e.name == "fd")
            found_fd = true;
        if (e.name == "task")
            found_task = true;
    }
    EXPECT_TRUE(found_maps);
    EXPECT_TRUE(found_status);
    EXPECT_TRUE(found_fd);
    EXPECT_TRUE(found_task);

    int pipefds[2];
    int64_t pr = SyscallDispatcher::Dispatch(293, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);
    ASSERT_EQ(pr, 0);

    auto fd_entries = portage::fs::ProcFs::ListDir("/proc/self/fd", &fd_table_);
    bool found_pipe0 = false;
    std::string p0_str = std::to_string(pipefds[0]);
    for (const auto &e : fd_entries) {
        if (e.name == p0_str) {
            found_pipe0 = true;
            break;
        }
    }
    EXPECT_TRUE(found_pipe0);

    auto sys_cpu_entries = portage::fs::ProcFs::ListDir("/sys/devices/system/cpu");
    bool found_online = false;
    bool found_cpu0 = false;
    for (const auto &e : sys_cpu_entries) {
        if (e.name == "online")
            found_online = true;
        if (e.name == "cpu0")
            found_cpu0 = true;
    }
    EXPECT_TRUE(found_online);
    EXPECT_TRUE(found_cpu0);
}

TEST_F(ProcFsAndBatch4Test, ReadlinkProcSelfFd)
{
    vfs_.SetExecutablePath("C:/portage/bin/portage.exe");

    char buf[256] = {0};
    int64_t r = SyscallDispatcher::Dispatch(89, reinterpret_cast<uint64_t>("/proc/self/exe"),
                                            reinterpret_cast<uint64_t>(buf), sizeof(buf), 0, 0, 0);
    EXPECT_GT(r, 0);
    buf[r] = '\0';
    EXPECT_STREQ(buf, "C:/portage/bin/portage.exe");

    std::memset(buf, 0, sizeof(buf));
    r = SyscallDispatcher::Dispatch(89, reinterpret_cast<uint64_t>("/proc/9999/exe"),
                                    reinterpret_cast<uint64_t>(buf), sizeof(buf), 0, 0, 0);
    EXPECT_GT(r, 0);
    buf[r] = '\0';
    EXPECT_STREQ(buf, "C:/portage/bin/portage.exe");

    int pipefds[2];
    int64_t pr = SyscallDispatcher::Dispatch(293, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);
    ASSERT_EQ(pr, 0);

    std::string link_path = "/proc/self/fd/" + std::to_string(pipefds[0]);
    std::memset(buf, 0, sizeof(buf));
    r = SyscallDispatcher::Dispatch(89, reinterpret_cast<uint64_t>(link_path.c_str()),
                                    reinterpret_cast<uint64_t>(buf), sizeof(buf), 0, 0, 0);
    EXPECT_GT(r, 0);
    buf[r] = '\0';
    EXPECT_NE(std::string(buf).find("pipe:"), std::string::npos);
}

TEST_F(ProcFsAndBatch4Test, MremapFixedAndDontUnmap)
{
    void *addr = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        9, 0, 4096, 3 /* PROT_READ | PROT_WRITE */, 0x22 /* MAP_PRIVATE | MAP_ANON */, -1, 0));
    ASSERT_TRUE(addr != nullptr);
    std::strcpy(static_cast<char *>(addr), "mremap_magic");

    // Expand in place or move
    void *moved = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        25, reinterpret_cast<uint64_t>(addr), 4096, 8192, LINUX_MREMAP_MAYMOVE, 0, 0));
    ASSERT_TRUE(moved != nullptr);
    EXPECT_STREQ(static_cast<char *>(moved), "mremap_magic");

    // Fixed move to target
    void *target = reinterpret_cast<void *>(0x55000000ULL);
    void *fixed = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        25, reinterpret_cast<uint64_t>(moved), 8192, 8192,
        LINUX_MREMAP_MAYMOVE | LINUX_MREMAP_FIXED, reinterpret_cast<uint64_t>(target), 0));
    EXPECT_EQ(fixed, target);
    EXPECT_STREQ(static_cast<char *>(fixed), "mremap_magic");

    // DONTUNMAP move
    void *dup_addr = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        25, reinterpret_cast<uint64_t>(fixed), 8192, 8192,
        LINUX_MREMAP_MAYMOVE | LINUX_MREMAP_DONTUNMAP, 0, 0));
    ASSERT_TRUE(dup_addr != nullptr);
    EXPECT_NE(dup_addr, fixed);
    EXPECT_STREQ(static_cast<char *>(dup_addr), "mremap_magic");
    EXPECT_STREQ(static_cast<char *>(fixed), "mremap_magic");
}

TEST_F(ProcFsAndBatch4Test, MincoreTracking)
{
    void *addr = reinterpret_cast<void *>(SyscallDispatcher::Dispatch(
        9, 0, 8192, 3 /* PROT_READ | PROT_WRITE */, 0x22 /* MAP_PRIVATE | MAP_ANON */, -1, 0));
    ASSERT_TRUE(addr != nullptr);

    unsigned char vec[2] = {0, 0};
    int64_t r = SyscallDispatcher::Dispatch(27, reinterpret_cast<uint64_t>(addr), 8192,
                                            reinterpret_cast<uint64_t>(vec), 0, 0, 0);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 1);

    unsigned char vec_err[1] = {0};
    int64_t r_err = SyscallDispatcher::Dispatch(27, 0x1000, 4096,
                                                reinterpret_cast<uint64_t>(vec_err), 0, 0, 0);
    EXPECT_EQ(r_err, LINUX_ENOMEM);
}

TEST_F(ProcFsAndBatch4Test, SendfileAndSpliceAndCopyFileRange)
{
    std::string rel_in = "test_in.txt";
    auto host_in = test_dir_ / rel_in;
    {
        std::ofstream ofs(host_in, std::ios::binary);
        ofs << "0123456789ABCDEF0123456789ABCDEF";
    }

    int in_fd = static_cast<int>(SyscallDispatcher::Dispatch(
        257, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>(rel_in.c_str()),
        LINUX_O_RDONLY, 0, 0, 0));
    ASSERT_GE(in_fd, 0);

    int pipefds[2];
    int64_t pr = SyscallDispatcher::Dispatch(293, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);
    ASSERT_EQ(pr, 0);

    // Test sendfile: from file to pipe
    int64_t offset = 0;
    int64_t sent = SyscallDispatcher::Dispatch(40, pipefds[1], in_fd,
                                               reinterpret_cast<uint64_t>(&offset), 16, 0, 0);
    EXPECT_EQ(sent, 16);
    EXPECT_EQ(offset, 16);

    char read_buf[32] = {0};
    int64_t r = SyscallDispatcher::Dispatch(0, pipefds[0], reinterpret_cast<uint64_t>(read_buf), 16,
                                            0, 0, 0);
    EXPECT_EQ(r, 16);
    read_buf[16] = '\0';
    EXPECT_STREQ(read_buf, "0123456789ABCDEF");

    // Test splice: from pipe back into another file
    std::string rel_out = "test_out.txt";
    int out_fd = static_cast<int>(SyscallDispatcher::Dispatch(
        257, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>(rel_out.c_str()),
        LINUX_O_RDWR | LINUX_O_CREAT, 0644, 0, 0));
    ASSERT_GE(out_fd, 0);

    // Write to pipe first
    const char *splice_data = "SPLICE_PAYLOAD";
    SyscallDispatcher::Dispatch(1, pipefds[1], reinterpret_cast<uint64_t>(splice_data), 14, 0, 0, 0);

    int64_t spliced = SyscallDispatcher::Dispatch(275, pipefds[0], 0, out_fd, 0, 14, 0);
    EXPECT_EQ(spliced, 14);

    // Test copy_file_range: copy 10 bytes from in_fd to out_fd
    int64_t off_in = 5;
    int64_t off_out = 14;
    int64_t copied = SyscallDispatcher::Dispatch(326, in_fd, reinterpret_cast<uint64_t>(&off_in),
                                                 out_fd, reinterpret_cast<uint64_t>(&off_out), 10, 0);
    EXPECT_EQ(copied, 10);
    EXPECT_EQ(off_in, 15);
    EXPECT_EQ(off_out, 24);
}

TEST_F(ProcFsAndBatch4Test, RecvmmsgSendmmsgAndFaccessat2AndEpollPwait2)
{
    // Test membarrier query and global flush
    int64_t q = SyscallDispatcher::Dispatch(324, LINUX_MEMBARRIER_CMD_QUERY, 0, 0, 0, 0, 0);
    EXPECT_GT(q, 0);
    EXPECT_TRUE((q & LINUX_MEMBARRIER_CMD_GLOBAL) != 0);

    int64_t b = SyscallDispatcher::Dispatch(324, LINUX_MEMBARRIER_CMD_GLOBAL, 0, 0, 0, 0, 0);
    EXPECT_EQ(b, 0);

    // Test mlock / munlock / mlock2
    EXPECT_EQ(SyscallDispatcher::Dispatch(149, 0x10000, 4096, 0, 0, 0, 0), 0);
    EXPECT_EQ(SyscallDispatcher::Dispatch(150, 0x10000, 4096, 0, 0, 0, 0), 0);
    EXPECT_EQ(SyscallDispatcher::Dispatch(325, 0x10000, 4096, 0, 0, 0, 0), 0);

    // Test faccessat2
    int pipefds[2];
    SyscallDispatcher::Dispatch(293, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);

    // AT_EMPTY_PATH on pipefd
    int64_t fa = SyscallDispatcher::Dispatch(439, pipefds[0], reinterpret_cast<uint64_t>(""), 0,
                                             LINUX_AT_EMPTY_PATH, 0, 0);
    EXPECT_EQ(fa, 0);

    // Invalid flags on faccessat2
    int64_t fa_inv = SyscallDispatcher::Dispatch(439, pipefds[0], reinterpret_cast<uint64_t>(""), 0,
                                                 0x8000, 0, 0);
    EXPECT_EQ(fa_inv, LINUX_EINVAL);

    // Test epoll_pwait2 timeout
    int epfd = static_cast<int>(SyscallDispatcher::Dispatch(291, 0, 0, 0, 0, 0, 0));
    ASSERT_GE(epfd, 0);

    portage::poll::LinuxEpollEvent events[4];
    LinuxTimespec ts{0, 10'000'000LL}; // 10ms
    int64_t w = SyscallDispatcher::Dispatch(441, epfd, reinterpret_cast<uint64_t>(events), 4,
                                            reinterpret_cast<uint64_t>(&ts), 0, 0);
    EXPECT_EQ(w, 0); // Timeout, 0 events

    // Test sendmmsg and recvmmsg over UDP socket pair
    int s1 = static_cast<int>(
        SyscallDispatcher::Dispatch(41, portage::net::LINUX_AF_INET,
                                    portage::net::LINUX_SOCK_DGRAM, 0, 0, 0, 0));
    int s2 = static_cast<int>(
        SyscallDispatcher::Dispatch(41, portage::net::LINUX_AF_INET,
                                    portage::net::LINUX_SOCK_DGRAM, 0, 0, 0, 0));
    ASSERT_GE(s1, 0);
    ASSERT_GE(s2, 0);

    portage::net::LinuxSockaddrIn sin{};
    sin.sin_family = portage::net::LINUX_AF_INET;
    sin.sin_port = htons(0);
    sin.sin_addr.addr = htonl(0x7F000001); // 127.0.0.1
    int64_t br = SyscallDispatcher::Dispatch(49, s2, reinterpret_cast<uint64_t>(&sin), sizeof(sin),
                                             0, 0, 0);
    ASSERT_EQ(br, 0);

    uint32_t slen = sizeof(sin);
    SyscallDispatcher::Dispatch(51, s2, reinterpret_cast<uint64_t>(&sin),
                                reinterpret_cast<uint64_t>(&slen), 0, 0, 0);

    char msg1[] = "HelloBatch4_1";
    char msg2[] = "HelloBatch4_2";
    LinuxIovec iov[2];
    iov[0].iov_base = msg1;
    iov[0].iov_len = sizeof(msg1);
    iov[1].iov_base = msg2;
    iov[1].iov_len = sizeof(msg2);

    LinuxMmsghdr send_mmsg[2]{};
    send_mmsg[0].msg_hdr.msg_name = &sin;
    send_mmsg[0].msg_hdr.msg_namelen = sizeof(sin);
    send_mmsg[0].msg_hdr.msg_iov = &iov[0];
    send_mmsg[0].msg_hdr.msg_iovlen = 1;

    send_mmsg[1].msg_hdr.msg_name = &sin;
    send_mmsg[1].msg_hdr.msg_namelen = sizeof(sin);
    send_mmsg[1].msg_hdr.msg_iov = &iov[1];
    send_mmsg[1].msg_hdr.msg_iovlen = 1;

    int64_t sent_msgs = SyscallDispatcher::Dispatch(
        307, s1, reinterpret_cast<uint64_t>(send_mmsg), 2, 0, 0, 0);
    EXPECT_EQ(sent_msgs, 2);
    EXPECT_EQ(send_mmsg[0].msg_len, sizeof(msg1));
    EXPECT_EQ(send_mmsg[1].msg_len, sizeof(msg2));

    char recv_buf1[64] = {0};
    char recv_buf2[64] = {0};
    LinuxIovec r_iov[2];
    r_iov[0].iov_base = recv_buf1;
    r_iov[0].iov_len = sizeof(recv_buf1);
    r_iov[1].iov_base = recv_buf2;
    r_iov[1].iov_len = sizeof(recv_buf2);

    LinuxMmsghdr recv_mmsg[2]{};
    recv_mmsg[0].msg_hdr.msg_iov = &r_iov[0];
    recv_mmsg[0].msg_hdr.msg_iovlen = 1;
    recv_mmsg[1].msg_hdr.msg_iov = &r_iov[1];
    recv_mmsg[1].msg_hdr.msg_iovlen = 1;

    int64_t recv_msgs = SyscallDispatcher::Dispatch(
        299, s2, reinterpret_cast<uint64_t>(recv_mmsg), 2, 0, 0, 0);
    EXPECT_EQ(recv_msgs, 2);
    EXPECT_STREQ(recv_buf1, "HelloBatch4_1");
    EXPECT_STREQ(recv_buf2, "HelloBatch4_2");
}
