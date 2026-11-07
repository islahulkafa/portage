#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::syscall {

constexpr int LINUX_O_RDONLY = 00;
constexpr int LINUX_O_WRONLY = 01;
constexpr int LINUX_O_RDWR = 02;
constexpr int LINUX_O_ACCMODE = 03;
constexpr int LINUX_O_CREAT = 0100;
constexpr int LINUX_O_EXCL = 0200;
constexpr int LINUX_O_NOCTTY = 0400;
constexpr int LINUX_O_TRUNC = 01000;
constexpr int LINUX_O_APPEND = 02000;
constexpr int LINUX_O_NONBLOCK = 04000;
constexpr int LINUX_O_DIRECTORY = 0200000;
constexpr int LINUX_O_CLOEXEC = 02000000;

constexpr int LINUX_AT_FDCWD = -100;

constexpr uint32_t LINUX_S_IFMT = 0170000;
constexpr uint32_t LINUX_S_IFSOCK = 0140000;
constexpr uint32_t LINUX_S_IFLNK = 0120000;
constexpr uint32_t LINUX_S_IFREG = 0100000;
constexpr uint32_t LINUX_S_IFBLK = 0060000;
constexpr uint32_t LINUX_S_IFDIR = 0040000;
constexpr uint32_t LINUX_S_IFCHR = 0020000;
constexpr uint32_t LINUX_S_IFIFO = 0010000;

struct LinuxStat64 {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    uint64_t st_atime_nsec;
    int64_t st_mtime;
    uint64_t st_mtime_nsec;
    int64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t __unused[3];
};

struct LinuxIovec {
    void *iov_base;
    size_t iov_len;
};

struct LinuxRlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

} // namespace portage::syscall
