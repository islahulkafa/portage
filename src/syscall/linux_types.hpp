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

struct LinuxMsghdr {
    void *msg_name;
    uint32_t msg_namelen;
    LinuxIovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct LinuxMmsghdr {
    LinuxMsghdr msg_hdr;
    unsigned int msg_len;
};

struct LinuxSchedParam {
    int sched_priority;
};

struct LinuxStatxTimestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct LinuxStatx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    LinuxStatxTimestamp stx_atime;
    LinuxStatxTimestamp stx_btime;
    LinuxStatxTimestamp stx_ctime;
    LinuxStatxTimestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint64_t __spare2;
    uint64_t __spare3[12];
};

constexpr int LINUX_PR_SET_PDEATHSIG = 1;
constexpr int LINUX_PR_GET_PDEATHSIG = 2;
constexpr int LINUX_PR_GET_DUMPABLE = 3;
constexpr int LINUX_PR_SET_DUMPABLE = 4;
constexpr int LINUX_PR_SET_NAME = 15;
constexpr int LINUX_PR_GET_NAME = 16;
constexpr int LINUX_PR_SET_CHILD_SUBREAPER = 36;
constexpr int LINUX_PR_GET_CHILD_SUBREAPER = 37;

constexpr int LINUX_PRIO_PROCESS = 0;
constexpr int LINUX_PRIO_PGRP = 1;
constexpr int LINUX_PRIO_USER = 2;

constexpr int LINUX_SCHED_OTHER = 0;
constexpr int LINUX_SCHED_FIFO = 1;
constexpr int LINUX_SCHED_RR = 2;

constexpr uint32_t LINUX_STATX_TYPE = 0x0001;
constexpr uint32_t LINUX_STATX_MODE = 0x0002;
constexpr uint32_t LINUX_STATX_NLINK = 0x0004;
constexpr uint32_t LINUX_STATX_UID = 0x0008;
constexpr uint32_t LINUX_STATX_GID = 0x0010;
constexpr uint32_t LINUX_STATX_ATIME = 0x0020;
constexpr uint32_t LINUX_STATX_MTIME = 0x0040;
constexpr uint32_t LINUX_STATX_CTIME = 0x0080;
constexpr uint32_t LINUX_STATX_INO = 0x0100;
constexpr uint32_t LINUX_STATX_SIZE = 0x0200;
constexpr uint32_t LINUX_STATX_BLOCKS = 0x0400;
constexpr uint32_t LINUX_STATX_BASIC_STATS = 0x07ff;
constexpr uint32_t LINUX_STATX_BTIME = 0x0800;
constexpr uint32_t LINUX_STATX_ALL = 0x0fff;

struct LinuxSysinfo {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
};

struct LinuxRusageTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct LinuxRusage {
    LinuxRusageTimeval ru_utime;
    LinuxRusageTimeval ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};

constexpr int LINUX_MREMAP_MAYMOVE = 1;
constexpr int LINUX_MREMAP_FIXED = 2;
constexpr int LINUX_MREMAP_DONTUNMAP = 4;

constexpr int LINUX_MEMBARRIER_CMD_QUERY = 0;
constexpr int LINUX_MEMBARRIER_CMD_GLOBAL = (1 << 0);
constexpr int LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED = (1 << 1);
constexpr int LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED = (1 << 2);
constexpr int LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED = (1 << 3);
constexpr int LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED = (1 << 4);
constexpr int LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE = (1 << 5);
constexpr int LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE = (1 << 6);

constexpr int LINUX_AT_SYMLINK_NOFOLLOW = 0x100;
constexpr int LINUX_AT_EACCESS = 0x200;
constexpr int LINUX_AT_EMPTY_PATH = 0x1000;

constexpr unsigned int LINUX_SPLICE_F_MOVE = 1;
constexpr unsigned int LINUX_SPLICE_F_NONBLOCK = 2;
constexpr unsigned int LINUX_SPLICE_F_MORE = 4;
constexpr unsigned int LINUX_SPLICE_F_GIFT = 8;

constexpr int LINUX_RUSAGE_SELF = 0;
constexpr int LINUX_RUSAGE_CHILDREN = -1;
constexpr int LINUX_RUSAGE_THREAD = 1;

constexpr int LINUX_EFD_SEMAPHORE = 1;
constexpr int LINUX_EFD_NONBLOCK = 04000;
constexpr int LINUX_EFD_CLOEXEC = 02000000;

constexpr unsigned int LINUX_GRND_NONBLOCK = 1;
constexpr unsigned int LINUX_GRND_RANDOM = 2;

struct LinuxTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct LinuxItimerspec {
    LinuxTimespec it_interval;
    LinuxTimespec it_value;
};

constexpr int LINUX_TFD_TIMER_ABSTIME = 1;
constexpr int LINUX_TFD_TIMER_CANCEL_ON_SET = 2;
constexpr int LINUX_TFD_CLOEXEC = 02000000;
constexpr int LINUX_TFD_NONBLOCK = 04000;

constexpr int LINUX_CLOCK_REALTIME = 0;
constexpr int LINUX_CLOCK_MONOTONIC = 1;
constexpr int LINUX_CLOCK_BOOTTIME = 7;
constexpr int LINUX_CLOCK_REALTIME_ALARM = 8;
constexpr int LINUX_CLOCK_BOOTTIME_ALARM = 9;

struct LinuxTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct LinuxTimezone {
    int tz_minuteswest;
    int tz_dsttime;
};

constexpr size_t LINUX_FD_SETSIZE = 1024;
struct LinuxFdSet {
    uint64_t fds_bits[LINUX_FD_SETSIZE / 64];
};

constexpr int64_t LINUX_UTIME_NOW = 0x3fffffffL;
constexpr int64_t LINUX_UTIME_OMIT = 0x3ffffffeL;

constexpr int LINUX_FALLOC_FL_KEEP_SIZE = 0x01;
constexpr int LINUX_EOPNOTSUPP = -95;

struct LinuxFsid {
    int32_t val[2];
};

struct LinuxStatfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    LinuxFsid f_fsid;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

constexpr uint64_t LINUX_EXT4_SUPER_MAGIC = 0xEF53;
constexpr uint64_t LINUX_TMPFS_MAGIC = 0x01021994;

constexpr unsigned int LINUX_CLOSE_RANGE_UNSHARE = (1U << 1);
constexpr unsigned int LINUX_CLOSE_RANGE_CLOEXEC = (1U << 2);

} // namespace portage::syscall
