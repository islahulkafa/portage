#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

using namespace portage::ndk;
using namespace portage::syscall;

TEST(BionicPosixTest, PosixFileAndStatOperations)
{
    const char *test_path = "portage_posix_file.tmp";
    int fd = portage_open(test_path, LINUX_O_CREAT | LINUX_O_RDWR | LINUX_O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    const char data[] = "Portage Bionic POSIX test data 2026";
    int64_t written = portage_write(fd, data, sizeof(data) - 1);
    EXPECT_EQ(written, static_cast<int64_t>(sizeof(data) - 1));

    LinuxStat64 st{};
    EXPECT_EQ(portage_fstat(fd, &st), 0);
    EXPECT_EQ(st.st_size, static_cast<int64_t>(sizeof(data) - 1));

    EXPECT_EQ(portage_close(fd), 0);

    LinuxStat64 path_st{};
    EXPECT_EQ(portage_stat(test_path, &path_st), 0);
    EXPECT_EQ(path_st.st_size, static_cast<int64_t>(sizeof(data) - 1));

    // Test ENOENT error path sets *portage_errno()
    *portage_errno() = 0;
    int bad_fd = portage_open("nonexistent_file_definitely_absent.tmp", LINUX_O_RDONLY, 0);
    EXPECT_EQ(bad_fd, -1);
    EXPECT_EQ(*portage_errno(), 2 /* ENOENT */);

    portage_unlink(test_path);
}

TEST(BionicPosixTest, DirectoryEnumerationAnd280ByteDirent)
{
    EXPECT_EQ(sizeof(BionicDirent64), 280U);

    BionicDir *dir = portage_opendir(".");
    ASSERT_NE(dir, nullptr);

    bool found_any = false;
    while (BionicDirent64 *entry = portage_readdir(dir)) {
        found_any = true;
        EXPECT_GT(std::strlen(entry->d_name), 0U);
        EXPECT_NE(entry->d_ino, 0ULL);
    }
    EXPECT_TRUE(found_any);
    EXPECT_EQ(portage_closedir(dir), 0);
}

TEST(BionicPosixTest, ClockGettimeNanosleepAndSyscallBridge)
{
    GuestTimespec ts{};
    int res = portage_clock_gettime(1 /* CLOCK_MONOTONIC */, &ts);
    EXPECT_EQ(res, 0);
    EXPECT_GT(ts.tv_sec, 0);

    GuestTimespec req{0, 1000000}; // 1ms
    EXPECT_EQ(portage_nanosleep(&req, nullptr), 0);

    int64_t pid = portage_syscall(39 /* SYS_getpid */, 0, 0, 0, 0, 0, 0);
    EXPECT_GT(pid, 0);
}

TEST(BionicPosixTest, ReaderWriterLocksSharedAndExclusive)
{
    uint8_t rwlock_storage[56]{};
    EXPECT_EQ(portage_pthread_rwlock_init(rwlock_storage, nullptr), 0);

    // Multiple shared reads
    EXPECT_EQ(portage_pthread_rwlock_rdlock(rwlock_storage), 0);
    EXPECT_EQ(portage_pthread_rwlock_tryrdlock(rwlock_storage), 0);
    EXPECT_EQ(portage_pthread_rwlock_unlock(rwlock_storage), 0);
    EXPECT_EQ(portage_pthread_rwlock_unlock(rwlock_storage), 0);

    // Exclusive write
    EXPECT_EQ(portage_pthread_rwlock_wrlock(rwlock_storage), 0);
    EXPECT_EQ(portage_pthread_rwlock_unlock(rwlock_storage), 0);

    EXPECT_EQ(portage_pthread_rwlock_destroy(rwlock_storage), 0);
}

TEST(BionicPosixTest, SysconfPathconfAndGetenv)
{
    long pagesize = portage_sysconf(30 /* _SC_PAGE_SIZE */);
    EXPECT_EQ(pagesize, 4096);

    long nproc = portage_sysconf(84 /* _SC_NPROCESSORS_ONLN */);
    EXPECT_GE(nproc, 1);

    long maxpath = portage_pathconf(".", 4 /* _PC_PATH_MAX */);
    EXPECT_GE(maxpath, 256);

    const char *android_data = portage_getenv("ANDROID_DATA");
    ASSERT_NE(android_data, nullptr);
    EXPECT_STREQ(android_data, "/data");
}

class BionicPosixFsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        temp_dir_ = std::filesystem::temp_directory_path() / "portage_bionic_posix_fs_test";
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

    std::filesystem::path temp_dir_;
    portage::mem::AddressSpace as_;
    portage::fs::Vfs vfs_;
    portage::fs::FileDescriptorTable fd_table_;
};

TEST_F(BionicPosixFsTest, SymlinkAndReadlink)
{
    int fd = portage_open("/target.txt", LINUX_O_CREAT | LINUX_O_RDWR | LINUX_O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    const char payload[] = "symlink_payload";
    EXPECT_EQ(portage_write(fd, payload, sizeof(payload) - 1),
              static_cast<int64_t>(sizeof(payload) - 1));
    EXPECT_EQ(portage_close(fd), 0);

    EXPECT_EQ(portage_symlink("/target.txt", "/sym_target.txt"), 0);
    EXPECT_EQ(portage_symlinkat("/target.txt", -100, "/symat_target.txt"), 0);

    char buf[128]{};
    int64_t len = portage_readlink("/sym_target.txt", buf, sizeof(buf) - 1);
    ASSERT_GT(len, 0);
    buf[len] = '\0';
    EXPECT_STREQ(buf, "/target.txt");

    std::memset(buf, 0, sizeof(buf));
    int64_t len2 = portage_readlinkat(-100, "/symat_target.txt", buf, sizeof(buf) - 1);
    ASSERT_GT(len2, 0);
    buf[len2] = '\0';
    EXPECT_STREQ(buf, "/target.txt");

    int sfd = portage_open("/sym_target.txt", LINUX_O_RDONLY, 0);
    ASSERT_GE(sfd, 0);
    char read_buf[64]{};
    EXPECT_EQ(portage_read(sfd, read_buf, sizeof(read_buf)),
              static_cast<int64_t>(sizeof(payload) - 1));
    EXPECT_STREQ(read_buf, "symlink_payload");
    EXPECT_EQ(portage_close(sfd), 0);
}

TEST_F(BionicPosixFsTest, LstatSymlinkAttributes)
{
    int fd = portage_open("/real.bin", LINUX_O_CREAT | LINUX_O_RDWR | LINUX_O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    const char data[100]{};
    EXPECT_EQ(portage_write(fd, data, sizeof(data)), 100);
    EXPECT_EQ(portage_close(fd), 0);

    EXPECT_EQ(portage_symlink("/real.bin", "/link.bin"), 0);

    LinuxStat64 lst{};
    EXPECT_EQ(portage_lstat("/link.bin", &lst), 0);
    EXPECT_EQ(lst.st_mode & 0170000U, LINUX_S_IFLNK);
    EXPECT_EQ(lst.st_size, 9);

    LinuxStat64 st{};
    EXPECT_EQ(portage_stat("/link.bin", &st), 0);
    EXPECT_EQ(st.st_mode & 0170000U, LINUX_S_IFREG);
    EXPECT_EQ(st.st_size, 100);
}

TEST_F(BionicPosixFsTest, SymlinkLoopEloop)
{
    EXPECT_EQ(portage_symlink("/loop_b", "/loop_a"), 0);
    EXPECT_EQ(portage_symlink("/loop_a", "/loop_b"), 0);

    *portage_errno() = 0;
    EXPECT_EQ(portage_open("/loop_a", LINUX_O_RDONLY, 0), -1);
    EXPECT_EQ(*portage_errno(), 40 /* ELOOP */);

    *portage_errno() = 0;
    LinuxStat64 st{};
    EXPECT_EQ(portage_stat("/loop_a", &st), -1);
    EXPECT_EQ(*portage_errno(), 40 /* ELOOP */);
}

TEST_F(BionicPosixFsTest, HardlinkOperations)
{
    int fd = portage_open("/orig.dat", LINUX_O_CREAT | LINUX_O_RDWR | LINUX_O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    const char msg[] = "hardlink_data";
    EXPECT_EQ(portage_write(fd, msg, sizeof(msg) - 1), static_cast<int64_t>(sizeof(msg) - 1));
    EXPECT_EQ(portage_close(fd), 0);

    EXPECT_EQ(portage_link("/orig.dat", "/hard1.dat"), 0);
    EXPECT_EQ(portage_linkat(-100, "/orig.dat", -100, "/hard2.dat", 0), 0);

    int hfd = portage_open("/hard2.dat", LINUX_O_RDONLY, 0);
    ASSERT_GE(hfd, 0);
    char buf[64]{};
    EXPECT_EQ(portage_read(hfd, buf, sizeof(buf)), static_cast<int64_t>(sizeof(msg) - 1));
    EXPECT_STREQ(buf, "hardlink_data");
    EXPECT_EQ(portage_close(hfd), 0);

    EXPECT_EQ(portage_unlink("/orig.dat"), 0);
    LinuxStat64 st{};
    EXPECT_EQ(portage_stat("/hard1.dat", &st), 0);
    EXPECT_EQ(st.st_size, static_cast<int64_t>(sizeof(msg) - 1));
}

TEST_F(BionicPosixFsTest, ChmodAndUtimensat)
{
    int fd = portage_open("/perm.txt", LINUX_O_CREAT | LINUX_O_RDWR | LINUX_O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(portage_chmod("/perm.txt", 0444), 0);

    LinuxStat64 st{};
    EXPECT_EQ(portage_stat("/perm.txt", &st), 0);
    EXPECT_EQ(st.st_mode & 0777U, 0444U);

    EXPECT_EQ(portage_fchmod(fd, 0755), 0);
    EXPECT_EQ(portage_stat("/perm.txt", &st), 0);
    EXPECT_EQ(st.st_mode & 0777U, 0755U);
    EXPECT_EQ(portage_close(fd), 0);

    GuestTimespec times[2]{{1700000000, 0}, {1700000000, 0}};
    EXPECT_EQ(portage_utimensat(-100, "/perm.txt", times, 0), 0);
    EXPECT_EQ(portage_stat("/perm.txt", &st), 0);
    EXPECT_EQ(st.st_mtime, 1700000000);
}
