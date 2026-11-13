#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "net/socket_manager.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace portage::syscall;

class PosixBatch3Test : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_posix3_test";
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

TEST_F(PosixBatch3Test, StatfsAndFstatfsReportCapacityAndExt4Magic)
{
    // Create a regular file in rootfs
    std::string rel_path = "test_statfs.txt";
    auto host_file = test_dir_ / rel_path;
    {
        std::ofstream ofs(host_file, std::ios::binary);
        ofs << "statfs payload test";
    }

    LinuxStatfs sfs{};
    int64_t r1 = SyscallDispatcher::Dispatch(137, reinterpret_cast<uint64_t>(rel_path.c_str()),
                                             reinterpret_cast<uint64_t>(&sfs), 0, 0, 0, 0);
    EXPECT_EQ(r1, 0);
    EXPECT_EQ(sfs.f_type, LINUX_EXT4_SUPER_MAGIC);
    EXPECT_EQ(sfs.f_bsize, 4096);
    EXPECT_EQ(sfs.f_frsize, 4096);
    EXPECT_GT(sfs.f_blocks, 0);
    EXPECT_GT(sfs.f_bfree, 0);
    EXPECT_EQ(sfs.f_namelen, 255);

    // Open file to test sys_fstatfs
    int64_t fd =
        SyscallDispatcher::Dispatch(257, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                    reinterpret_cast<uint64_t>(rel_path.c_str()), 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    LinuxStatfs fsfs{};
    int64_t r2 = SyscallDispatcher::Dispatch(138, static_cast<uint64_t>(fd),
                                             reinterpret_cast<uint64_t>(&fsfs), 0, 0, 0, 0);
    EXPECT_EQ(r2, 0);
    EXPECT_EQ(fsfs.f_type, LINUX_EXT4_SUPER_MAGIC);
    EXPECT_EQ(fsfs.f_bsize, 4096);
    EXPECT_EQ(fsfs.f_frsize, 4096);
    EXPECT_GT(fsfs.f_blocks, 0);
    EXPECT_EQ(fsfs.f_namelen, 255);

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch3Test, SocketpairCreatesBidirectionalConnectedPair)
{
    int sv[2] = {-1, -1};
    int64_t r = SyscallDispatcher::Dispatch(53, 1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0,
                                            reinterpret_cast<uint64_t>(sv), 0, 0);
    ASSERT_EQ(r, 0);
    ASSERT_GE(sv[0], 3);
    ASSERT_GE(sv[1], 3);
    EXPECT_NE(sv[0], sv[1]);

    const char msg1[] = "Hello from sv0";
    int64_t sent = SyscallDispatcher::Dispatch(
        44, static_cast<uint64_t>(sv[0]), reinterpret_cast<uint64_t>(msg1), sizeof(msg1), 0, 0, 0);
    EXPECT_EQ(sent, static_cast<int64_t>(sizeof(msg1)));

    char recv_buf[64]{};
    int64_t recvd = SyscallDispatcher::Dispatch(45, static_cast<uint64_t>(sv[1]),
                                                reinterpret_cast<uint64_t>(recv_buf),
                                                sizeof(recv_buf), 0, 0, 0);
    EXPECT_EQ(recvd, static_cast<int64_t>(sizeof(msg1)));
    EXPECT_STREQ(recv_buf, msg1);

    const char msg2[] = "Reply from sv1";
    sent = SyscallDispatcher::Dispatch(44, static_cast<uint64_t>(sv[1]),
                                       reinterpret_cast<uint64_t>(msg2), sizeof(msg2), 0, 0, 0);
    EXPECT_EQ(sent, static_cast<int64_t>(sizeof(msg2)));

    std::memset(recv_buf, 0, sizeof(recv_buf));
    recvd = SyscallDispatcher::Dispatch(45, static_cast<uint64_t>(sv[0]),
                                        reinterpret_cast<uint64_t>(recv_buf), sizeof(recv_buf), 0,
                                        0, 0);
    EXPECT_EQ(recvd, static_cast<int64_t>(sizeof(msg2)));
    EXPECT_STREQ(recv_buf, msg2);

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(sv[0]), 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(sv[1]), 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch3Test, PipeDelegatesToPipe2)
{
    int pipefds[2] = {-1, -1};
    int64_t r = SyscallDispatcher::Dispatch(22, reinterpret_cast<uint64_t>(pipefds), 0, 0, 0, 0, 0);
    ASSERT_EQ(r, 0);
    ASSERT_GE(pipefds[0], 3);
    ASSERT_GE(pipefds[1], 3);

    const char data[] = "Pipe IPC test";
    int64_t w =
        SyscallDispatcher::Dispatch(1, static_cast<uint64_t>(pipefds[1]),
                                    reinterpret_cast<uint64_t>(data), sizeof(data), 0, 0, 0);
    EXPECT_EQ(w, static_cast<int64_t>(sizeof(data)));

    char read_buf[32]{};
    int64_t rd = SyscallDispatcher::Dispatch(0, static_cast<uint64_t>(pipefds[0]),
                                             reinterpret_cast<uint64_t>(read_buf), sizeof(read_buf),
                                             0, 0, 0);
    EXPECT_EQ(rd, static_cast<int64_t>(sizeof(data)));
    EXPECT_STREQ(read_buf, data);

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(pipefds[0]), 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(pipefds[1]), 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch3Test, FchmodAndFchmodatToggleReadonlyAttribute)
{
    std::string rel_path = "fchmod_test.txt";
    auto host_file = test_dir_ / rel_path;
    {
        std::ofstream ofs(host_file);
        ofs << "write-protect test";
    }

    // sys_fchmodat with 0444 (read-only)
    int64_t r1 =
        SyscallDispatcher::Dispatch(268, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                    reinterpret_cast<uint64_t>(rel_path.c_str()), 0444, 0, 0, 0);
    EXPECT_EQ(r1, 0);
    DWORD attrs = GetFileAttributesW(host_file.wstring().c_str());
    EXPECT_TRUE((attrs & FILE_ATTRIBUTE_READONLY) != 0);

    // Open read-only to test sys_fchmod
    int64_t fd =
        SyscallDispatcher::Dispatch(257, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                    reinterpret_cast<uint64_t>(rel_path.c_str()), 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    // Restore write permissions via sys_fchmod (0644)
    int64_t r2 = SyscallDispatcher::Dispatch(91, static_cast<uint64_t>(fd), 0644, 0, 0, 0, 0);
    EXPECT_EQ(r2, 0);
    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);

    attrs = GetFileAttributesW(host_file.wstring().c_str());
    EXPECT_FALSE((attrs & FILE_ATTRIBUTE_READONLY) != 0);
}

TEST_F(PosixBatch3Test, FchownAndFchownatValidateExistenceAndReturnZero)
{
    std::string rel_path = "fchown_test.txt";
    auto host_file = test_dir_ / rel_path;
    {
        std::ofstream ofs(host_file);
        ofs << "ownership test";
    }

    // Existing file -> success
    int64_t r1 =
        SyscallDispatcher::Dispatch(260, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                    reinterpret_cast<uint64_t>(rel_path.c_str()), 1000, 1000, 0, 0);
    EXPECT_EQ(r1, 0);

    // Non-existent file -> LINUX_ENOENT
    int64_t r2 = SyscallDispatcher::Dispatch(260, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                             reinterpret_cast<uint64_t>("no_such_file.txt"), 1000,
                                             1000, 0, 0);
    EXPECT_EQ(r2, LINUX_ENOENT);

    // Open file for sys_fchown
    int64_t fd =
        SyscallDispatcher::Dispatch(257, static_cast<uint64_t>(LINUX_AT_FDCWD),
                                    reinterpret_cast<uint64_t>(rel_path.c_str()), 0, 0, 0, 0);
    ASSERT_GE(fd, 3);

    int64_t r3 = SyscallDispatcher::Dispatch(93, static_cast<uint64_t>(fd), 1000, 1000, 0, 0, 0);
    EXPECT_EQ(r3, 0);

    SyscallDispatcher::Dispatch(3, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);

    int64_t r4 = SyscallDispatcher::Dispatch(93, 9999, 1000, 1000, 0, 0, 0);
    EXPECT_EQ(r4, LINUX_EBADF);
}

TEST_F(PosixBatch3Test, LinkatCreatesHardLink)
{
    std::string orig_path = "orig.txt";
    std::string link_path = "hardlink.txt";
    auto host_orig = test_dir_ / orig_path;
    auto host_link = test_dir_ / link_path;
    {
        std::ofstream ofs(host_orig);
        ofs << "original hard link file";
    }

    int64_t r = SyscallDispatcher::Dispatch(
        265, static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>(orig_path.c_str()),
        static_cast<uint64_t>(LINUX_AT_FDCWD), reinterpret_cast<uint64_t>(link_path.c_str()), 0, 0);
    EXPECT_EQ(r, 0);
    EXPECT_TRUE(std::filesystem::exists(host_link));

    // Hard link should share file content
    std::string read_back;
    {
        std::ifstream ifs(host_link);
        std::getline(ifs, read_back);
    }
    EXPECT_EQ(read_back, "original hard link file");
}

TEST_F(PosixBatch3Test, CloseRangeClosesAndSetsCloexec)
{
    int pipe1[2]{-1, -1};
    int pipe2[2]{-1, -1};
    int pipe3[2]{-1, -1};

    ASSERT_EQ(fd_table_.CreatePipe(pipe1, 0), 0);
    ASSERT_EQ(fd_table_.CreatePipe(pipe2, 0), 0);
    ASSERT_EQ(fd_table_.CreatePipe(pipe3, 0), 0);

    // Set CLOSE_RANGE_CLOEXEC on pipe2 fds
    int64_t r_cloexec = SyscallDispatcher::Dispatch(436, static_cast<uint64_t>(pipe2[0]),
                                                    static_cast<uint64_t>(pipe2[1]),
                                                    LINUX_CLOSE_RANGE_CLOEXEC, 0, 0, 0);
    EXPECT_EQ(r_cloexec, 0);
    auto desc2_0 = fd_table_.Get(pipe2[0]);
    auto desc2_1 = fd_table_.Get(pipe2[1]);
    ASSERT_NE(desc2_0, nullptr);
    ASSERT_NE(desc2_1, nullptr);
    EXPECT_TRUE((desc2_0->flags & LINUX_O_CLOEXEC) != 0);
    EXPECT_TRUE((desc2_1->flags & LINUX_O_CLOEXEC) != 0);

    // Close range covering pipe1 and pipe2
    int64_t r_close = SyscallDispatcher::Dispatch(436, static_cast<uint64_t>(pipe1[0]),
                                                  static_cast<uint64_t>(pipe2[1]), 0, 0, 0, 0);
    EXPECT_EQ(r_close, 0);

    EXPECT_EQ(fd_table_.Get(pipe1[0]), nullptr);
    EXPECT_EQ(fd_table_.Get(pipe1[1]), nullptr);
    EXPECT_EQ(fd_table_.Get(pipe2[0]), nullptr);
    EXPECT_EQ(fd_table_.Get(pipe2[1]), nullptr);

    // pipe3 fds should still remain open
    EXPECT_NE(fd_table_.Get(pipe3[0]), nullptr);
    EXPECT_NE(fd_table_.Get(pipe3[1]), nullptr);

    fd_table_.Close(pipe3[0]);
    fd_table_.Close(pipe3[1]);
}

TEST_F(PosixBatch3Test, TimerCreateAndDelete)
{
    int tid = -1;
    int64_t ret = SyscallDispatcher::Dispatch(222, LINUX_CLOCK_REALTIME, 0,
                                              reinterpret_cast<uint64_t>(&tid), 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(tid, 0);
    ret = SyscallDispatcher::Dispatch(225, static_cast<uint64_t>(tid), 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(PosixBatch3Test, TimerSetAndGetTime)
{
    int tid = -1;
    SyscallDispatcher::Dispatch(222, LINUX_CLOCK_REALTIME, 0, reinterpret_cast<uint64_t>(&tid), 0,
                                0, 0);
    ASSERT_GE(tid, 0);

    LinuxItimerspec new_val{{0, 100'000'000LL}, {0, 100'000'000LL}};
    LinuxItimerspec old_val{};
    int64_t ret = SyscallDispatcher::Dispatch(223, static_cast<uint64_t>(tid), 0,
                                              reinterpret_cast<uint64_t>(&new_val),
                                              reinterpret_cast<uint64_t>(&old_val), 0, 0);
    EXPECT_EQ(ret, 0);

    LinuxItimerspec curr{};
    ret = SyscallDispatcher::Dispatch(224, static_cast<uint64_t>(tid),
                                      reinterpret_cast<uint64_t>(&curr), 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(curr.it_interval.tv_nsec, 100'000'000LL);

    SyscallDispatcher::Dispatch(225, static_cast<uint64_t>(tid), 0, 0, 0, 0, 0);
}

TEST_F(PosixBatch3Test, TimerGetoverrunReturnsZero)
{
    int tid = -1;
    SyscallDispatcher::Dispatch(222, LINUX_CLOCK_REALTIME, 0, reinterpret_cast<uint64_t>(&tid), 0,
                                0, 0);
    ASSERT_GE(tid, 0);
    int64_t ret = SyscallDispatcher::Dispatch(226, static_cast<uint64_t>(tid), 0, 0, 0, 0, 0);
    EXPECT_EQ(ret, 0);
    SyscallDispatcher::Dispatch(225, static_cast<uint64_t>(tid), 0, 0, 0, 0, 0);
}
