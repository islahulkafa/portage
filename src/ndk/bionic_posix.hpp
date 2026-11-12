#pragma once

#include "ndk/bionic_thunks.hpp"
#include "syscall/linux_types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace portage::ndk {

struct BionicDirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
};
static_assert(sizeof(BionicDirent64) == 280, "BionicDirent64 size must be exactly 280 bytes");

struct BionicDir {
    int fd{-1};
    uint8_t buffer[2048]{};
    int buf_len{0};
    int buf_pos{0};
    BionicDirent64 current{};
    std::vector<BionicDirent64> host_entries{};
    size_t host_idx{0};
    bool is_host{false};
};

void RegisterBionicPosix(NdkSymbolRegistry &registry);

} // namespace portage::ndk

extern "C" {

// POSIX File & FS
__attribute__((sysv_abi)) int portage_open(const char *path, int flags, int mode = 0);
__attribute__((sysv_abi)) int portage_openat(int dfd, const char *path, int flags, int mode = 0);
__attribute__((sysv_abi)) int portage_close(int fd);
__attribute__((sysv_abi)) int64_t portage_read(int fd, void *buf, size_t count);
__attribute__((sysv_abi)) int64_t portage_write(int fd, const void *buf, size_t count);
__attribute__((sysv_abi)) int64_t portage_lseek(int fd, int64_t offset, int whence);
__attribute__((sysv_abi)) int portage_stat(const char *path, portage::syscall::LinuxStat64 *buf);
__attribute__((sysv_abi)) int portage_lstat(const char *path, portage::syscall::LinuxStat64 *buf);
__attribute__((sysv_abi)) int portage_fstat(int fd, portage::syscall::LinuxStat64 *buf);
__attribute__((sysv_abi)) int portage_ftruncate(int fd, int64_t length);
__attribute__((sysv_abi)) int portage_truncate(const char *path, int64_t length);
__attribute__((sysv_abi)) int portage_chdir(const char *path);
__attribute__((sysv_abi)) char *portage_getcwd(char *buf, size_t size);
__attribute__((sysv_abi)) int portage_mkdir(const char *path, int mode);
__attribute__((sysv_abi)) int portage_unlink(const char *path);
__attribute__((sysv_abi)) int portage_unlinkat(int dfd, const char *path, int flags);
__attribute__((sysv_abi)) int portage_remove(const char *path);
__attribute__((sysv_abi)) int portage_rename(const char *oldpath, const char *newpath);
__attribute__((sysv_abi)) int64_t portage_readlink(const char *path, char *buf, size_t bufsiz);
__attribute__((sysv_abi)) int64_t portage_readlinkat(int dfd, const char *pathname, char *buf,
                                                     size_t bufsiz);
__attribute__((sysv_abi)) char *portage_realpath(const char *path, char *resolved_path);
__attribute__((sysv_abi)) int portage_symlink(const char *target, const char *linkpath);
__attribute__((sysv_abi)) int portage_symlinkat(const char *target, int newdfd,
                                                const char *linkpath);
__attribute__((sysv_abi)) int portage_link(const char *oldpath, const char *newpath);
__attribute__((sysv_abi)) int portage_linkat(int olddfd, const char *oldpath, int newdfd,
                                             const char *newpath, int flags);
__attribute__((sysv_abi)) int portage_chmod(const char *path, int mode);
__attribute__((sysv_abi)) int portage_fchmod(int fd, int mode);
__attribute__((sysv_abi)) int portage_fchmodat(int dfd, const char *path, int mode, int flags);
__attribute__((sysv_abi)) int portage_isatty(int fd);
__attribute__((sysv_abi)) int portage_ioctl(int fd, uint64_t request, uintptr_t arg);
__attribute__((sysv_abi)) int portage_statvfs(const char *path, void *buf);
__attribute__((sysv_abi)) int
portage_utimensat(int dfd, const char *path, const portage::ndk::GuestTimespec times[2], int flags);
__attribute__((sysv_abi)) int64_t portage_sendfile(int out_fd, int in_fd, int64_t *offset,
                                                   size_t count);
__attribute__((sysv_abi)) long portage_sysconf(int name);
__attribute__((sysv_abi)) long portage_pathconf(const char *path, int name);
__attribute__((sysv_abi)) const char *portage_getenv(const char *name);

// Directory stream
__attribute__((sysv_abi)) portage::ndk::BionicDir *portage_opendir(const char *name);
__attribute__((sysv_abi)) portage::ndk::BionicDir *portage_fdopendir(int fd);
__attribute__((sysv_abi)) portage::ndk::BionicDirent64 *
portage_readdir(portage::ndk::BionicDir *dirp);
__attribute__((sysv_abi)) portage::ndk::BionicDirent64 *
portage_readdir64(portage::ndk::BionicDir *dirp);
__attribute__((sysv_abi)) int portage_closedir(portage::ndk::BionicDir *dirp);

// Clocks and time
__attribute__((sysv_abi)) int portage_clock_gettime(int clk_id, portage::ndk::GuestTimespec *tp);
__attribute__((sysv_abi)) int portage_gettimeofday(void *tv, void *tz);
__attribute__((sysv_abi)) int64_t portage_time(int64_t *tloc);
__attribute__((sysv_abi)) int portage_nanosleep(const portage::ndk::GuestTimespec *req,
                                                portage::ndk::GuestTimespec *rem);
__attribute__((sysv_abi)) int portage_usleep(uint32_t usec);

// Syscall bridge
__attribute__((sysv_abi)) int64_t portage_syscall(long nr, uintptr_t a1 = 0, uintptr_t a2 = 0,
                                                  uintptr_t a3 = 0, uintptr_t a4 = 0,
                                                  uintptr_t a5 = 0, uintptr_t a6 = 0);

// Reader-writer locks
__attribute__((sysv_abi)) int portage_pthread_rwlock_init(void *rwlock, const void *attr);
__attribute__((sysv_abi)) int portage_pthread_rwlock_destroy(void *rwlock);
__attribute__((sysv_abi)) int portage_pthread_rwlock_rdlock(void *rwlock);
__attribute__((sysv_abi)) int portage_pthread_rwlock_tryrdlock(void *rwlock);
__attribute__((sysv_abi)) int portage_pthread_rwlock_wrlock(void *rwlock);
__attribute__((sysv_abi)) int portage_pthread_rwlock_trywrlock(void *rwlock);
__attribute__((sysv_abi)) int portage_pthread_rwlock_unlock(void *rwlock);

// Real-time signals & signalfd
__attribute__((sysv_abi)) int portage_sigwaitinfo(const uint64_t *set, void *info);
__attribute__((sysv_abi)) int portage_sigtimedwait(const uint64_t *set, void *info,
                                                   const portage::ndk::GuestTimespec *timeout);
__attribute__((sysv_abi)) int portage_sigqueue(int pid, int sig, uintptr_t value);
__attribute__((sysv_abi)) int portage_signalfd(int fd, const uint64_t *mask, int flags);
__attribute__((sysv_abi)) int portage_signalfd4(int fd, const uint64_t *mask, size_t sizemask,
                                                int flags);
__attribute__((sysv_abi)) int portage_sigsuspend(const uint64_t *mask);

} // extern "C"
