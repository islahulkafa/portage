#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <cstring>
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
