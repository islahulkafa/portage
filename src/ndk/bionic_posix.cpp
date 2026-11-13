#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "signal/signal_types.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace portage::ndk {

struct HostRwlock {
    std::shared_mutex smtx{};
    std::atomic<DWORD> writer_tid{0};
};

static std::mutex g_rwlock_mtx;
static std::unordered_map<uintptr_t, std::shared_ptr<HostRwlock>> g_rwlocks;

static std::shared_ptr<HostRwlock> GetRwlock(void *rwlock)
{
    uintptr_t key = reinterpret_cast<uintptr_t>(rwlock);
    std::lock_guard lock(g_rwlock_mtx);
    auto &ptr = g_rwlocks[key];
    if (!ptr) {
        ptr = std::make_shared<HostRwlock>();
    }
    return ptr;
}

static inline int64_t SetErrnoFromSyscall(int64_t ret)
{
    if (ret < 0) {
        *portage_errno() = static_cast<int>(-ret);
        return -1;
    }
    return ret;
}

void RegisterBionicPosix(NdkSymbolRegistry &registry)
{
    registry.Register("open", reinterpret_cast<uintptr_t>(&portage_open));
    registry.Register("openat", reinterpret_cast<uintptr_t>(&portage_openat));
    registry.Register("close", reinterpret_cast<uintptr_t>(&portage_close));
    registry.Register("read", reinterpret_cast<uintptr_t>(&portage_read));
    registry.Register("write", reinterpret_cast<uintptr_t>(&portage_write));
    registry.Register("lseek", reinterpret_cast<uintptr_t>(&portage_lseek));
    registry.Register("stat", reinterpret_cast<uintptr_t>(&portage_stat));
    registry.Register("lstat", reinterpret_cast<uintptr_t>(&portage_lstat));
    registry.Register("fstat", reinterpret_cast<uintptr_t>(&portage_fstat));
    registry.Register("ftruncate", reinterpret_cast<uintptr_t>(&portage_ftruncate));
    registry.Register("truncate", reinterpret_cast<uintptr_t>(&portage_truncate));
    registry.Register("chdir", reinterpret_cast<uintptr_t>(&portage_chdir));
    registry.Register("getcwd", reinterpret_cast<uintptr_t>(&portage_getcwd));
    registry.Register("mkdir", reinterpret_cast<uintptr_t>(&portage_mkdir));
    registry.Register("unlink", reinterpret_cast<uintptr_t>(&portage_unlink));
    registry.Register("unlinkat", reinterpret_cast<uintptr_t>(&portage_unlinkat));
    registry.Register("remove", reinterpret_cast<uintptr_t>(&portage_remove));
    registry.Register("rename", reinterpret_cast<uintptr_t>(&portage_rename));
    registry.Register("readlink", reinterpret_cast<uintptr_t>(&portage_readlink));
    registry.Register("readlinkat", reinterpret_cast<uintptr_t>(&portage_readlinkat));
    registry.Register("realpath", reinterpret_cast<uintptr_t>(&portage_realpath));
    registry.Register("symlink", reinterpret_cast<uintptr_t>(&portage_symlink));
    registry.Register("symlinkat", reinterpret_cast<uintptr_t>(&portage_symlinkat));
    registry.Register("link", reinterpret_cast<uintptr_t>(&portage_link));
    registry.Register("linkat", reinterpret_cast<uintptr_t>(&portage_linkat));
    registry.Register("chmod", reinterpret_cast<uintptr_t>(&portage_chmod));
    registry.Register("fchmod", reinterpret_cast<uintptr_t>(&portage_fchmod));
    registry.Register("fchmodat", reinterpret_cast<uintptr_t>(&portage_fchmodat));
    registry.Register("isatty", reinterpret_cast<uintptr_t>(&portage_isatty));
    registry.Register("ioctl", reinterpret_cast<uintptr_t>(&portage_ioctl));
    registry.Register("statvfs", reinterpret_cast<uintptr_t>(&portage_statvfs));
    registry.Register("utimensat", reinterpret_cast<uintptr_t>(&portage_utimensat));
    registry.Register("sendfile", reinterpret_cast<uintptr_t>(&portage_sendfile));
    registry.Register("sysconf", reinterpret_cast<uintptr_t>(&portage_sysconf));
    registry.Register("pathconf", reinterpret_cast<uintptr_t>(&portage_pathconf));
    registry.Register("getenv", reinterpret_cast<uintptr_t>(&portage_getenv));

    registry.Register("opendir", reinterpret_cast<uintptr_t>(&portage_opendir));
    registry.Register("fdopendir", reinterpret_cast<uintptr_t>(&portage_fdopendir));
    registry.Register("readdir", reinterpret_cast<uintptr_t>(&portage_readdir));
    registry.Register("readdir64", reinterpret_cast<uintptr_t>(&portage_readdir64));
    registry.Register("closedir", reinterpret_cast<uintptr_t>(&portage_closedir));

    registry.Register("clock_gettime", reinterpret_cast<uintptr_t>(&portage_clock_gettime));
    registry.Register("gettimeofday", reinterpret_cast<uintptr_t>(&portage_gettimeofday));
    registry.Register("time", reinterpret_cast<uintptr_t>(&portage_time));
    registry.Register("nanosleep", reinterpret_cast<uintptr_t>(&portage_nanosleep));
    registry.Register("usleep", reinterpret_cast<uintptr_t>(&portage_usleep));

    registry.Register("syscall", reinterpret_cast<uintptr_t>(&portage_syscall));

    registry.Register("pthread_rwlock_init",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_init));
    registry.Register("pthread_rwlock_destroy",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_destroy));
    registry.Register("pthread_rwlock_rdlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_rdlock));
    registry.Register("pthread_rwlock_tryrdlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_tryrdlock));
    registry.Register("pthread_rwlock_wrlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_wrlock));
    registry.Register("pthread_rwlock_trywrlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_trywrlock));
    registry.Register("pthread_rwlock_unlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_rwlock_unlock));

    registry.Register("sigwaitinfo", reinterpret_cast<uintptr_t>(&portage_sigwaitinfo));
    registry.Register("sigtimedwait", reinterpret_cast<uintptr_t>(&portage_sigtimedwait));
    registry.Register("sigqueue", reinterpret_cast<uintptr_t>(&portage_sigqueue));
    registry.Register("signalfd", reinterpret_cast<uintptr_t>(&portage_signalfd));
    registry.Register("signalfd4", reinterpret_cast<uintptr_t>(&portage_signalfd4));
    registry.Register("sigsuspend", reinterpret_cast<uintptr_t>(&portage_sigsuspend));

    registry.Register("inotify_init", reinterpret_cast<uintptr_t>(&portage_inotify_init));
    registry.Register("inotify_init1", reinterpret_cast<uintptr_t>(&portage_inotify_init1));
    registry.Register("inotify_add_watch", reinterpret_cast<uintptr_t>(&portage_inotify_add_watch));
    registry.Register("inotify_rm_watch", reinterpret_cast<uintptr_t>(&portage_inotify_rm_watch));

    registry.Register("unshare", reinterpret_cast<uintptr_t>(&portage_unshare));
    registry.Register("setns", reinterpret_cast<uintptr_t>(&portage_setns));
    registry.Register("sched_setaffinity", reinterpret_cast<uintptr_t>(&portage_sched_setaffinity));
    registry.Register("sched_getaffinity", reinterpret_cast<uintptr_t>(&portage_sched_getaffinity));

    registry.Register("getifaddrs", reinterpret_cast<uintptr_t>(&portage_getifaddrs));
    registry.Register("freeifaddrs", reinterpret_cast<uintptr_t>(&portage_freeifaddrs));
    registry.Register("if_nametoindex", reinterpret_cast<uintptr_t>(&portage_if_nametoindex));
    registry.Register("if_indextoname", reinterpret_cast<uintptr_t>(&portage_if_indextoname));
    registry.Register("if_nameindex", reinterpret_cast<uintptr_t>(&portage_if_nameindex));
    registry.Register("if_freenameindex", reinterpret_cast<uintptr_t>(&portage_if_freenameindex));
}

} // namespace portage::ndk

extern "C" {

using namespace portage::ndk;
using namespace portage::syscall;

__attribute__((sysv_abi)) int portage_open(const char *path, int flags, int mode)
{
    return portage_openat(LINUX_AT_FDCWD, path, flags, mode);
}

__attribute__((sysv_abi)) int portage_openat(int dfd, const char *path, int flags, int mode)
{
    if (!path) {
        *portage_errno() = 14; // EFAULT
        return -1;
    }
    if (SyscallDispatcher::GetVfs() != nullptr && SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            257 /* SYS_openat */, static_cast<uint64_t>(dfd), reinterpret_cast<uint64_t>(path),
            static_cast<uint64_t>(flags), static_cast<uint64_t>(mode), 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }

    int host_flags = 0;
    if ((flags & LINUX_O_ACCMODE) == LINUX_O_RDONLY) {
        host_flags |= _O_RDONLY;
    } else if ((flags & LINUX_O_ACCMODE) == LINUX_O_WRONLY) {
        host_flags |= _O_WRONLY;
    } else if ((flags & LINUX_O_ACCMODE) == LINUX_O_RDWR) {
        host_flags |= _O_RDWR;
    }
    if (flags & LINUX_O_CREAT) {
        host_flags |= _O_CREAT;
    }
    if (flags & LINUX_O_TRUNC) {
        host_flags |= _O_TRUNC;
    }
    if (flags & LINUX_O_APPEND) {
        host_flags |= _O_APPEND;
    }
    host_flags |= _O_BINARY;

    int fd = _open(path, host_flags, mode ? mode : (_S_IREAD | _S_IWRITE));
    if (fd < 0) {
        *portage_errno() = errno;
        return -1;
    }
    return fd;
}

__attribute__((sysv_abi)) int portage_close(int fd)
{
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(fd), 0,
                                                  0, 0, 0, 0);
        if (ret >= 0) {
            return 0;
        }
    }
    int res = _close(fd);
    if (res < 0) {
        *portage_errno() = errno;
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) int64_t portage_read(int fd, void *buf, size_t count)
{
    if (!buf) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(0 /* SYS_read */, static_cast<uint64_t>(fd),
                                                  reinterpret_cast<uint64_t>(buf), count, 0, 0, 0);
        return SetErrnoFromSyscall(ret);
    }
    int res = _read(fd, buf, static_cast<unsigned int>(count));
    if (res < 0) {
        *portage_errno() = errno;
        return -1;
    }
    return res;
}

__attribute__((sysv_abi)) int64_t portage_write(int fd, const void *buf, size_t count)
{
    if (!buf) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(1 /* SYS_write */, static_cast<uint64_t>(fd),
                                                  reinterpret_cast<uint64_t>(buf), count, 0, 0, 0);
        return SetErrnoFromSyscall(ret);
    }
    int res = _write(fd, buf, static_cast<unsigned int>(count));
    if (res < 0) {
        *portage_errno() = errno;
        return -1;
    }
    return res;
}

__attribute__((sysv_abi)) int64_t portage_lseek(int fd, int64_t offset, int whence)
{
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(8 /* SYS_lseek */, static_cast<uint64_t>(fd),
                                                  static_cast<uint64_t>(offset),
                                                  static_cast<uint64_t>(whence), 0, 0, 0);
        return SetErrnoFromSyscall(ret);
    }
    __int64 res = _lseeki64(fd, offset, whence);
    if (res < 0) {
        *portage_errno() = errno;
        return -1;
    }
    return res;
}

__attribute__((sysv_abi)) int portage_stat(const char *path, LinuxStat64 *buf)
{
    if (!path || !buf) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret =
            SyscallDispatcher::Dispatch(4 /* SYS_stat */, reinterpret_cast<uint64_t>(path),
                                        reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    std::error_code ec;
    auto s = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(s)) {
        *portage_errno() = 2; // ENOENT
        return -1;
    }
    std::memset(buf, 0, sizeof(LinuxStat64));
    buf->st_size = static_cast<int64_t>(std::filesystem::file_size(path, ec));
    buf->st_mode =
        std::filesystem::is_directory(s) ? (LINUX_S_IFDIR | 0755) : (LINUX_S_IFREG | 0644);
    buf->st_nlink = 1;
    return 0;
}

__attribute__((sysv_abi)) int portage_lstat(const char *path, LinuxStat64 *buf)
{
    if (!path || !buf) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret =
            SyscallDispatcher::Dispatch(6 /* SYS_lstat */, reinterpret_cast<uint64_t>(path),
                                        reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return portage_stat(path, buf);
}

__attribute__((sysv_abi)) int portage_fstat(int fd, LinuxStat64 *buf)
{
    if (!buf) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(5 /* SYS_fstat */, static_cast<uint64_t>(fd),
                                                  reinterpret_cast<uint64_t>(buf), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    struct _stat64 st{};
    if (_fstat64(fd, &st) < 0) {
        *portage_errno() = errno;
        return -1;
    }
    std::memset(buf, 0, sizeof(LinuxStat64));
    buf->st_size = st.st_size;
    buf->st_mode = (st.st_mode & _S_IFDIR) ? (LINUX_S_IFDIR | 0755) : (LINUX_S_IFREG | 0644);
    buf->st_nlink = 1;
    return 0;
}

__attribute__((sysv_abi)) int portage_ftruncate(int fd, int64_t length)
{
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(77 /* SYS_ftruncate */, static_cast<uint64_t>(fd),
                                                  static_cast<uint64_t>(length), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    int res = _chsize_s(fd, length);
    if (res != 0) {
        *portage_errno() = res;
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_truncate(const char *path, int64_t length)
{
    int fd = portage_open(path, LINUX_O_WRONLY, 0644);
    if (fd < 0) {
        return -1;
    }
    int res = portage_ftruncate(fd, length);
    portage_close(fd);
    return res;
}

__attribute__((sysv_abi)) int portage_chdir(const char *path)
{
    if (!path) {
        *portage_errno() = 14;
        return -1;
    }
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(80 /* SYS_chdir */,
                                                  reinterpret_cast<uint64_t>(path), 0, 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    if (ec) {
        *portage_errno() = ec.value();
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) char *portage_getcwd(char *buf, size_t size)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            79 /* SYS_getcwd */, reinterpret_cast<uint64_t>(buf), size, 0, 0, 0, 0);
        if (ret > 0) {
            return buf;
        }
        SetErrnoFromSyscall(ret);
        return nullptr;
    }
    std::error_code ec;
    auto cur = std::filesystem::current_path(ec);
    if (ec || cur.string().size() >= size) {
        *portage_errno() = 34; // ERANGE
        return nullptr;
    }
    std::string s = cur.generic_string();
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    return buf;
}

__attribute__((sysv_abi)) int portage_mkdir(const char *path, int mode)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            83 /* SYS_mkdir */, reinterpret_cast<uint64_t>(path), mode, 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec) && ec) {
        *portage_errno() = ec.value();
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_unlink(const char *path)
{
    return portage_unlinkat(LINUX_AT_FDCWD, path, 0);
}

__attribute__((sysv_abi)) int portage_unlinkat(int dfd, const char *path, int flags)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret =
            SyscallDispatcher::Dispatch(263 /* SYS_unlinkat */, static_cast<uint64_t>(dfd),
                                        reinterpret_cast<uint64_t>(path), flags, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    std::error_code ec;
    if (!std::filesystem::remove(path, ec) && ec) {
        *portage_errno() = ec.value();
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_remove(const char *path) { return portage_unlink(path); }

__attribute__((sysv_abi)) int portage_rename(const char *oldpath, const char *newpath)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret =
            SyscallDispatcher::Dispatch(82 /* SYS_rename */, reinterpret_cast<uint64_t>(oldpath),
                                        reinterpret_cast<uint64_t>(newpath), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    std::error_code ec;
    std::filesystem::rename(oldpath, newpath, ec);
    if (ec) {
        *portage_errno() = ec.value();
        return -1;
    }
    return 0;
}

__attribute__((sysv_abi)) int64_t portage_readlink(const char *path, char *buf, size_t bufsiz)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret =
            SyscallDispatcher::Dispatch(89 /* SYS_readlink */, reinterpret_cast<uint64_t>(path),
                                        reinterpret_cast<uint64_t>(buf), bufsiz, 0, 0, 0);
        return SetErrnoFromSyscall(ret);
    }
    *portage_errno() = 22; // EINVAL
    return -1;
}

__attribute__((sysv_abi)) int64_t portage_readlinkat(int dfd, const char *pathname, char *buf,
                                                     size_t bufsiz)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            267 /* SYS_readlinkat */, static_cast<uint64_t>(dfd),
            reinterpret_cast<uint64_t>(pathname), reinterpret_cast<uint64_t>(buf), bufsiz, 0, 0);
        return SetErrnoFromSyscall(ret);
    }
    *portage_errno() = 22; // EINVAL
    return -1;
}

__attribute__((sysv_abi)) char *portage_realpath(const char *path, char *resolved_path)
{
    if (!path) {
        *portage_errno() = 14;
        return nullptr;
    }
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) {
        *portage_errno() = ec.value();
        return nullptr;
    }
    std::string s = abs.generic_string();
    if (!resolved_path) {
        resolved_path = static_cast<char *>(std::malloc(s.size() + 1));
    }
    std::memcpy(resolved_path, s.data(), s.size());
    resolved_path[s.size()] = '\0';
    return resolved_path;
}

__attribute__((sysv_abi)) int portage_symlink(const char *target, const char *linkpath)
{
    return portage_symlinkat(target, -100 /* AT_FDCWD */, linkpath);
}

__attribute__((sysv_abi)) int portage_symlinkat(const char *target, int newdfd,
                                                const char *linkpath)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            266 /* SYS_symlinkat */, reinterpret_cast<uint64_t>(target),
            static_cast<uint64_t>(newdfd), reinterpret_cast<uint64_t>(linkpath), 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    *portage_errno() = 38;
    return -1;
}

__attribute__((sysv_abi)) int portage_link(const char *oldpath, const char *newpath)
{
    return portage_linkat(-100 /* AT_FDCWD */, oldpath, -100 /* AT_FDCWD */, newpath, 0);
}

__attribute__((sysv_abi)) int portage_linkat(int olddfd, const char *oldpath, int newdfd,
                                             const char *newpath, int flags)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            265 /* SYS_linkat */, static_cast<uint64_t>(olddfd),
            reinterpret_cast<uint64_t>(oldpath), static_cast<uint64_t>(newdfd),
            reinterpret_cast<uint64_t>(newpath), static_cast<uint64_t>(flags), 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    *portage_errno() = 38;
    return -1;
}

__attribute__((sysv_abi)) int portage_chmod(const char *path, int mode)
{
    return portage_fchmodat(-100 /* AT_FDCWD */, path, mode, 0);
}

__attribute__((sysv_abi)) int portage_fchmod(int fd, int mode)
{
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(91 /* SYS_fchmod */, static_cast<uint64_t>(fd),
                                                  static_cast<uint64_t>(mode), 0, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_fchmodat(int dfd, const char *path, int mode, int flags)
{
    if (SyscallDispatcher::GetVfs() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            268 /* SYS_fchmodat */, static_cast<uint64_t>(dfd), reinterpret_cast<uint64_t>(path),
            static_cast<uint64_t>(mode), static_cast<uint64_t>(flags), 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

__attribute__((sysv_abi)) int portage_ioctl(int fd, uint64_t request, uintptr_t arg)
{
    if (SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(16 /* SYS_ioctl */, static_cast<uint64_t>(fd),
                                                  request, arg, 0, 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_statvfs(const char *, void *buf)
{
    if (buf) {
        std::memset(buf, 0, 112); // struct statvfs size
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_utimensat(int dfd, const char *path,
                                                const GuestTimespec times[2], int flags)
{
    if (SyscallDispatcher::GetVfs() != nullptr || SyscallDispatcher::GetFdTable() != nullptr) {
        int64_t ret = SyscallDispatcher::Dispatch(
            280 /* SYS_utimensat */, static_cast<uint64_t>(dfd), reinterpret_cast<uint64_t>(path),
            reinterpret_cast<uint64_t>(times), static_cast<uint64_t>(flags), 0, 0);
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return 0;
}

__attribute__((sysv_abi)) int64_t portage_sendfile(int out_fd, int in_fd, int64_t *offset,
                                                   size_t count)
{
    char transfer_buf[4096];
    size_t total = 0;
    while (total < count) {
        size_t chunk = std::min(sizeof(transfer_buf), count - total);
        int64_t r = 0;
        if (offset) {
            portage_lseek(in_fd, *offset, 0);
            r = portage_read(in_fd, transfer_buf, chunk);
            if (r > 0) {
                *offset += r;
            }
        } else {
            r = portage_read(in_fd, transfer_buf, chunk);
        }
        if (r <= 0) {
            break;
        }
        int64_t w = portage_write(out_fd, transfer_buf, static_cast<size_t>(r));
        if (w <= 0) {
            break;
        }
        total += static_cast<size_t>(w);
    }
    return static_cast<int64_t>(total);
}

__attribute__((sysv_abi)) long portage_sysconf(int name)
{
    switch (name) {
    case 30: // _SC_PAGE_SIZE
    case 40: // _SC_PAGESIZE
        return 4096;
    case 83: // _SC_NPROCESSORS_CONF
    case 84: // _SC_NPROCESSORS_ONLN
        return static_cast<long>(std::max(1U, std::thread::hardware_concurrency()));
    case 2: // _SC_CLK_TCK
        return 100;
    default:
        return 4096;
    }
}

__attribute__((sysv_abi)) long portage_pathconf(const char *, int name)
{
    switch (name) {
    case 4: // _PC_PATH_MAX
        return 4096;
    case 3: // _PC_NAME_MAX
        return 255;
    default:
        return 4096;
    }
}

__attribute__((sysv_abi)) const char *portage_getenv(const char *name)
{
    if (!name) {
        return nullptr;
    }
    if (std::strcmp(name, "ANDROID_DATA") == 0) {
        return "/data";
    }
    if (std::strcmp(name, "ANDROID_ROOT") == 0) {
        return "/system";
    }
    if (std::strcmp(name, "TMPDIR") == 0) {
        return "/data/local/tmp";
    }
    return std::getenv(name);
}

__attribute__((sysv_abi)) BionicDir *portage_opendir(const char *name)
{
    if (!name) {
        *portage_errno() = 14;
        return nullptr;
    }

    if (SyscallDispatcher::GetVfs() != nullptr && SyscallDispatcher::GetFdTable() != nullptr) {
        int fd = portage_open(name, LINUX_O_RDONLY | LINUX_O_DIRECTORY, 0);
        if (fd >= 0) {
            auto *d = new BionicDir();
            d->fd = fd;
            d->is_host = false;
            return d;
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(name, ec) && std::filesystem::is_directory(name, ec)) {
        auto *d = new BionicDir();
        d->is_host = true;
        uint64_t ino_seed = 1000;
        for (const auto &entry : std::filesystem::directory_iterator(name, ec)) {
            BionicDirent64 de{};
            de.d_ino = ino_seed++;
            de.d_off = static_cast<int64_t>(d->host_entries.size() + 1);
            de.d_reclen = sizeof(BionicDirent64);
            de.d_type = entry.is_directory() ? 4 : 8; // DT_DIR / DT_REG
            std::string fname = entry.path().filename().string();
            std::memcpy(de.d_name, fname.data(), std::min(sizeof(de.d_name) - 1, fname.size()));
            d->host_entries.push_back(de);
        }
        return d;
    }

    *portage_errno() = 2; // ENOENT
    return nullptr;
}

__attribute__((sysv_abi)) BionicDir *portage_fdopendir(int fd)
{
    if (fd < 0) {
        *portage_errno() = 9; // EBADF
        return nullptr;
    }
    auto *d = new BionicDir();
    d->fd = fd;
    d->is_host = false;
    return d;
}

__attribute__((sysv_abi)) BionicDirent64 *portage_readdir(BionicDir *dirp)
{
    if (!dirp) {
        return nullptr;
    }
    if (dirp->is_host) {
        if (dirp->host_idx < dirp->host_entries.size()) {
            dirp->current = dirp->host_entries[dirp->host_idx++];
            return &dirp->current;
        }
        return nullptr;
    }

    if (dirp->buf_pos >= dirp->buf_len) {
        int64_t n = SyscallDispatcher::Dispatch(
            217 /* SYS_getdents64 */, static_cast<uint64_t>(dirp->fd),
            reinterpret_cast<uint64_t>(dirp->buffer), sizeof(dirp->buffer), 0, 0, 0);
        if (n <= 0) {
            return nullptr;
        }
        dirp->buf_len = static_cast<int>(n);
        dirp->buf_pos = 0;
    }

    auto *linux_de = reinterpret_cast<portage::fs::LinuxDirent64 *>(dirp->buffer + dirp->buf_pos);
    std::memset(&dirp->current, 0, sizeof(BionicDirent64));
    dirp->current.d_ino = linux_de->d_ino;
    dirp->current.d_off = linux_de->d_off;
    dirp->current.d_reclen = sizeof(BionicDirent64);
    dirp->current.d_type = linux_de->d_type;
    std::strncpy(dirp->current.d_name, linux_de->d_name, sizeof(dirp->current.d_name) - 1);
    dirp->buf_pos += linux_de->d_reclen;
    return &dirp->current;
}

__attribute__((sysv_abi)) BionicDirent64 *portage_readdir64(BionicDir *dirp)
{
    return portage_readdir(dirp);
}

__attribute__((sysv_abi)) int portage_closedir(BionicDir *dirp)
{
    if (!dirp) {
        return -1;
    }
    if (!dirp->is_host && dirp->fd >= 0) {
        portage_close(dirp->fd);
    }
    delete dirp;
    return 0;
}

__attribute__((sysv_abi)) int portage_clock_gettime(int clk_id, GuestTimespec *tp)
{
    if (!tp) {
        *portage_errno() = 14;
        return -1;
    }
    int64_t ret =
        SyscallDispatcher::Dispatch(228 /* SYS_clock_gettime */, static_cast<uint64_t>(clk_id),
                                    reinterpret_cast<uint64_t>(tp), 0, 0, 0, 0);
    if (ret == 0) {
        return 0;
    }
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    tp->tv_sec = counter.QuadPart / freq.QuadPart;
    tp->tv_nsec = (counter.QuadPart % freq.QuadPart) * 1'000'000'000 / freq.QuadPart;
    return 0;
}

__attribute__((sysv_abi)) int portage_gettimeofday(void *tv, void *tz)
{
    int64_t ret =
        SyscallDispatcher::Dispatch(96 /* SYS_gettimeofday */, reinterpret_cast<uint64_t>(tv),
                                    reinterpret_cast<uint64_t>(tz), 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int64_t portage_time(int64_t *tloc)
{
    int64_t ret = SyscallDispatcher::Dispatch(201 /* SYS_time */, reinterpret_cast<uint64_t>(tloc),
                                              0, 0, 0, 0, 0);
    if (ret >= 0) {
        return ret;
    }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    int64_t epoch_sec = static_cast<int64_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
    if (tloc) {
        *tloc = epoch_sec;
    }
    return epoch_sec;
}

__attribute__((sysv_abi)) int portage_nanosleep(const GuestTimespec *req, GuestTimespec *rem)
{
    if (!req) {
        *portage_errno() = 14;
        return -1;
    }
    DWORD ms = static_cast<DWORD>(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000);
    Sleep(ms);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_usleep(uint32_t usec)
{
    DWORD ms = (usec + 999) / 1000;
    Sleep(ms);
    return 0;
}

__attribute__((sysv_abi)) int64_t portage_syscall(long nr, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                                  uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
    int64_t ret = SyscallDispatcher::Dispatch(static_cast<uint64_t>(nr), a1, a2, a3, a4, a5, a6);
    if (ret >= 0) {
        return ret;
    }
    if (nr == 39) { // SYS_getpid fallback if dispatcher not initialized
        return static_cast<int64_t>(GetCurrentProcessId());
    }
    *portage_errno() = static_cast<int>(-ret);
    return -1;
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_init(void *rwlock, const void *)
{
    GetRwlock(rwlock);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_destroy(void *rwlock)
{
    uintptr_t key = reinterpret_cast<uintptr_t>(rwlock);
    std::lock_guard lock(g_rwlock_mtx);
    g_rwlocks.erase(key);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_rdlock(void *rwlock)
{
    GetRwlock(rwlock)->smtx.lock_shared();
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_tryrdlock(void *rwlock)
{
    return GetRwlock(rwlock)->smtx.try_lock_shared() ? 0 : 16; // EBUSY
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_wrlock(void *rwlock)
{
    auto r = GetRwlock(rwlock);
    r->smtx.lock();
    r->writer_tid.store(GetCurrentThreadId());
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_trywrlock(void *rwlock)
{
    auto r = GetRwlock(rwlock);
    if (r->smtx.try_lock()) {
        r->writer_tid.store(GetCurrentThreadId());
        return 0;
    }
    return 16; // EBUSY
}

__attribute__((sysv_abi)) int portage_pthread_rwlock_unlock(void *rwlock)
{
    auto r = GetRwlock(rwlock);
    if (r->writer_tid.load() == GetCurrentThreadId()) {
        r->writer_tid.store(0);
        r->smtx.unlock();
    } else {
        r->smtx.unlock_shared();
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_sigwaitinfo(const uint64_t *set, void *info)
{
    int64_t ret =
        SyscallDispatcher::Dispatch(128 /* SYS_rt_sigtimedwait */, reinterpret_cast<uint64_t>(set),
                                    reinterpret_cast<uint64_t>(info), 0, sizeof(uint64_t), 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_sigtimedwait(const uint64_t *set, void *info,
                                                   const GuestTimespec *timeout)
{
    int64_t ret =
        SyscallDispatcher::Dispatch(128 /* SYS_rt_sigtimedwait */, reinterpret_cast<uint64_t>(set),
                                    reinterpret_cast<uint64_t>(info),
                                    reinterpret_cast<uint64_t>(timeout), sizeof(uint64_t), 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_sigqueue(int pid, int sig, uintptr_t value)
{
    portage::signal::LinuxSiginfo info{};
    info.si_signo = sig;
    info.si_code = portage::signal::LINUX_SI_QUEUE;
    info._sifields.rt.si_pid = static_cast<int32_t>(GetCurrentProcessId());
    info._sifields.rt.si_sigval.sival_ptr = reinterpret_cast<void *>(value);

    int64_t ret = SyscallDispatcher::Dispatch(
        129 /* SYS_rt_sigqueueinfo */, static_cast<uint64_t>(pid), static_cast<uint64_t>(sig),
        reinterpret_cast<uint64_t>(&info), 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_signalfd(int fd, const uint64_t *mask, int flags)
{
    return portage_signalfd4(fd, mask, sizeof(uint64_t), flags);
}

__attribute__((sysv_abi)) int portage_signalfd4(int fd, const uint64_t *mask, size_t sizemask,
                                                int flags)
{
    int64_t ret = SyscallDispatcher::Dispatch(
        289 /* SYS_signalfd4 */, static_cast<uint64_t>(fd), reinterpret_cast<uint64_t>(mask),
        static_cast<uint64_t>(sizemask), static_cast<uint64_t>(flags), 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_sigsuspend(const uint64_t *mask)
{
    int64_t ret =
        SyscallDispatcher::Dispatch(130 /* SYS_rt_sigsuspend */, reinterpret_cast<uint64_t>(mask),
                                    sizeof(uint64_t), 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_inotify_init()
{
    int64_t ret = SyscallDispatcher::Dispatch(253 /* SYS_inotify_init */, 0, 0, 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_inotify_init1(int flags)
{
    int64_t ret = SyscallDispatcher::Dispatch(294 /* SYS_inotify_init1 */,
                                              static_cast<uint64_t>(flags), 0, 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_inotify_add_watch(int fd, const char *path, uint32_t mask)
{
    int64_t ret = SyscallDispatcher::Dispatch(
        254 /* SYS_inotify_add_watch */, static_cast<uint64_t>(fd),
        reinterpret_cast<uint64_t>(path), static_cast<uint64_t>(mask), 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_inotify_rm_watch(int fd, int wd)
{
    int64_t ret =
        SyscallDispatcher::Dispatch(255 /* SYS_inotify_rm_watch */, static_cast<uint64_t>(fd),
                                    static_cast<uint64_t>(wd), 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_unshare(int flags)
{
    int64_t ret = SyscallDispatcher::Dispatch(272 /* SYS_unshare */, static_cast<uint64_t>(flags),
                                              0, 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_setns(int fd, int nstype)
{
    int64_t ret = SyscallDispatcher::Dispatch(308 /* SYS_setns */, static_cast<uint64_t>(fd),
                                              static_cast<uint64_t>(nstype), 0, 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_sched_setaffinity(int pid, size_t cpusetsize,
                                                        const void *mask)
{
    int64_t ret = SyscallDispatcher::Dispatch(
        203 /* SYS_sched_setaffinity */, static_cast<uint64_t>(pid),
        static_cast<uint64_t>(cpusetsize), reinterpret_cast<uint64_t>(mask), 0, 0, 0);
    return static_cast<int>(SetErrnoFromSyscall(ret));
}

__attribute__((sysv_abi)) int portage_sched_getaffinity(int pid, size_t cpusetsize, void *mask)
{
    int64_t ret = SyscallDispatcher::Dispatch(
        204 /* SYS_sched_getaffinity */, static_cast<uint64_t>(pid),
        static_cast<uint64_t>(cpusetsize), reinterpret_cast<uint64_t>(mask), 0, 0, 0);
    if (ret < 0) {
        return static_cast<int>(SetErrnoFromSyscall(ret));
    }
    return 0;
}

namespace {

struct LinuxSockaddrIn {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
};

struct LinuxSockaddrIn6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t sin6_addr[16];
    uint32_t sin6_scope_id;
};

} // namespace

__attribute__((sysv_abi)) int portage_getifaddrs(struct ifaddrs **ifap)
{
    if (!ifap) {
        *portage_errno() = 22; // EINVAL
        return -1;
    }

    struct ifaddrs *head = nullptr;
    struct ifaddrs **tail = &head;

    auto add_entry = [&](const char *name, unsigned int flags, const void *addr_ptr,
                         size_t addr_len, const void *mask_ptr, size_t mask_len,
                         const void *bcast_ptr, size_t bcast_len) {
        auto *entry = static_cast<struct ifaddrs *>(std::calloc(1, sizeof(struct ifaddrs)));
        if (!entry) {
            return;
        }

        entry->ifa_flags = flags;
        if (name) {
            size_t nlen = std::strlen(name) + 1;
            entry->ifa_name = static_cast<char *>(std::malloc(nlen));
            if (entry->ifa_name) {
                std::memcpy(entry->ifa_name, name, nlen);
            }
        }
        if (addr_ptr && addr_len > 0) {
            entry->ifa_addr = static_cast<struct sockaddr *>(std::calloc(1, addr_len));
            if (entry->ifa_addr) {
                std::memcpy(entry->ifa_addr, addr_ptr, addr_len);
            }
        }
        if (mask_ptr && mask_len > 0) {
            entry->ifa_netmask = static_cast<struct sockaddr *>(std::calloc(1, mask_len));
            if (entry->ifa_netmask) {
                std::memcpy(entry->ifa_netmask, mask_ptr, mask_len);
            }
        }
        if (bcast_ptr && bcast_len > 0) {
            entry->ifa_ifu.ifu_broadaddr =
                static_cast<struct sockaddr *>(std::calloc(1, bcast_len));
            if (entry->ifa_ifu.ifu_broadaddr) {
                std::memcpy(entry->ifa_ifu.ifu_broadaddr, bcast_ptr, bcast_len);
            }
        }

        *tail = entry;
        tail = &entry->ifa_next;
    };

    // Loopback IPv4 (127.0.0.1/8)
    constexpr unsigned int loopback_flags =
        0x1 /* IFF_UP */ | 0x8 /* IFF_LOOPBACK */ | 0x40 /* IFF_RUNNING */;
    LinuxSockaddrIn lo_in{};
    lo_in.sin_family = 2; // AF_INET
    lo_in.sin_addr = htonl(0x7F000001);

    LinuxSockaddrIn lo_mask{};
    lo_mask.sin_family = 2; // AF_INET
    lo_mask.sin_addr = htonl(0xFF000000);

    add_entry("lo", loopback_flags, &lo_in, sizeof(lo_in), &lo_mask, sizeof(lo_mask), nullptr, 0);

    // Loopback IPv6 (::1/128)
    LinuxSockaddrIn6 lo_in6{};
    lo_in6.sin6_family = 10; // AF_INET6
    lo_in6.sin6_addr[15] = 1;

    LinuxSockaddrIn6 lo_mask6{};
    lo_mask6.sin6_family = 10; // AF_INET6
    std::memset(lo_mask6.sin6_addr, 0xFF, 16);

    add_entry("lo", loopback_flags, &lo_in6, sizeof(lo_in6), &lo_mask6, sizeof(lo_mask6), nullptr,
              0);

    // Host adapters via GetAdaptersAddresses
    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            char ifname[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(ifname, sizeof(ifname), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(ifname, sizeof(ifname), "eth%d", next_eth_idx++);
            }

            unsigned int flags = 0x2 /* IFF_BROADCAST */ | 0x1000 /* IFF_MULTICAST */;
            if (curr->OperStatus == IfOperStatusUp) {
                flags |= 0x1 /* IFF_UP */ | 0x40 /* IFF_RUNNING */;
            }

            int ifindex = static_cast<int>(curr->IfIndex > 1 ? curr->IfIndex : 2);

            for (auto *uni = curr->FirstUnicastAddress; uni != nullptr; uni = uni->Next) {
                if (!uni->Address.lpSockaddr) {
                    continue;
                }
                ADDRESS_FAMILY family = uni->Address.lpSockaddr->sa_family;
                if (family == AF_INET) {
                    auto *sin = reinterpret_cast<sockaddr_in *>(uni->Address.lpSockaddr);
                    LinuxSockaddrIn host_in{};
                    host_in.sin_family = 2; // AF_INET
                    host_in.sin_port = sin->sin_port;
                    host_in.sin_addr = sin->sin_addr.S_un.S_addr;

                    uint8_t prefix = uni->OnLinkPrefixLength;
                    uint32_t mask_val =
                        (prefix >= 32) ? 0xFFFFFFFF : htonl(~((1ULL << (32 - prefix)) - 1));
                    LinuxSockaddrIn host_mask{};
                    host_mask.sin_family = 2;
                    host_mask.sin_addr = mask_val;

                    uint32_t bcast_val = host_in.sin_addr | ~mask_val;
                    LinuxSockaddrIn host_bcast{};
                    host_bcast.sin_family = 2;
                    host_bcast.sin_addr = bcast_val;

                    add_entry(ifname, flags, &host_in, sizeof(host_in), &host_mask,
                              sizeof(host_mask), &host_bcast, sizeof(host_bcast));
                } else if (family == AF_INET6) {
                    auto *sin6 = reinterpret_cast<sockaddr_in6 *>(uni->Address.lpSockaddr);
                    LinuxSockaddrIn6 host_in6{};
                    host_in6.sin6_family = 10; // AF_INET6
                    host_in6.sin6_port = sin6->sin6_port;
                    std::memcpy(host_in6.sin6_addr, &sin6->sin6_addr, 16);
                    host_in6.sin6_scope_id = static_cast<uint32_t>(ifindex);

                    uint8_t prefix = uni->OnLinkPrefixLength;
                    LinuxSockaddrIn6 host_mask6{};
                    host_mask6.sin6_family = 10;
                    for (int b = 0; b < 16; ++b) {
                        if (prefix >= 8) {
                            host_mask6.sin6_addr[b] = 0xFF;
                            prefix -= 8;
                        } else if (prefix > 0) {
                            host_mask6.sin6_addr[b] =
                                static_cast<uint8_t>(~((1 << (8 - prefix)) - 1));
                            prefix = 0;
                        }
                    }

                    add_entry(ifname, flags, &host_in6, sizeof(host_in6), &host_mask6,
                              sizeof(host_mask6), nullptr, 0);
                }
            }
        }
    }

    *ifap = head;
    return 0;
}

__attribute__((sysv_abi)) void portage_freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        if (ifa->ifa_name) {
            std::free(ifa->ifa_name);
        }
        if (ifa->ifa_addr) {
            std::free(ifa->ifa_addr);
        }
        if (ifa->ifa_netmask) {
            std::free(ifa->ifa_netmask);
        }
        if (ifa->ifa_ifu.ifu_broadaddr) {
            std::free(ifa->ifa_ifu.ifu_broadaddr);
        }
        std::free(ifa);
        ifa = next;
    }
}

__attribute__((sysv_abi)) unsigned int portage_if_nametoindex(const char *ifname)
{
    if (!ifname || !*ifname) {
        *portage_errno() = 19; // ENODEV
        return 0;
    }
    if (std::strcmp(ifname, "lo") == 0) {
        return 1;
    }

    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            char name[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(name, sizeof(name), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(name, sizeof(name), "eth%d", next_eth_idx++);
            }

            if (std::strcmp(ifname, name) == 0) {
                return curr->IfIndex > 1 ? curr->IfIndex : 2;
            }
        }
    }

    if (std::strncmp(ifname, "eth", 3) == 0) {
        int n = std::atoi(ifname + 3);
        return static_cast<unsigned int>(n + 2);
    }
    if (std::strncmp(ifname, "wlan", 4) == 0) {
        int n = std::atoi(ifname + 4);
        return static_cast<unsigned int>(n + 2);
    }

    *portage_errno() = 19; // ENODEV
    return 0;
}

__attribute__((sysv_abi)) char *portage_if_indextoname(unsigned int ifindex, char *ifname)
{
    if (!ifname || ifindex == 0) {
        *portage_errno() = 22; // EINVAL
        return nullptr;
    }
    if (ifindex == 1) {
        std::strncpy(ifname, "lo", 16);
        ifname[15] = '\0';
        return ifname;
    }

    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            char name[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(name, sizeof(name), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(name, sizeof(name), "eth%d", next_eth_idx++);
            }

            unsigned int curr_idx = curr->IfIndex > 1 ? curr->IfIndex : 2;
            if (curr_idx == ifindex) {
                std::strncpy(ifname, name, 16);
                ifname[15] = '\0';
                return ifname;
            }
        }
    }

    std::snprintf(ifname, 16, "eth%u", ifindex >= 2 ? ifindex - 2 : 0);
    ifname[15] = '\0';
    return ifname;
}

__attribute__((sysv_abi)) struct if_nameindex *portage_if_nameindex(void)
{
    std::vector<std::pair<unsigned int, std::string>> entries;
    entries.emplace_back(1, "lo");

    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            char name[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(name, sizeof(name), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(name, sizeof(name), "eth%d", next_eth_idx++);
            }

            unsigned int idx = curr->IfIndex > 1 ? curr->IfIndex : 2;
            entries.emplace_back(idx, std::string(name));
        }
    }

    size_t total_size = (entries.size() + 1) * sizeof(struct if_nameindex);
    auto *result = static_cast<struct if_nameindex *>(std::calloc(1, total_size));
    if (!result) {
        *portage_errno() = 12; // ENOMEM
        return nullptr;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        result[i].if_index = entries[i].first;
        size_t nlen = entries[i].second.size() + 1;
        result[i].if_name = static_cast<char *>(std::malloc(nlen));
        if (result[i].if_name) {
            std::memcpy(result[i].if_name, entries[i].second.c_str(), nlen);
        }
    }

    result[entries.size()].if_index = 0;
    result[entries.size()].if_name = nullptr;

    return result;
}

__attribute__((sysv_abi)) void portage_if_freenameindex(struct if_nameindex *ptr)
{
    if (!ptr) {
        return;
    }
    for (auto *p = ptr; p->if_index != 0 || p->if_name != nullptr; ++p) {
        if (p->if_name) {
            std::free(p->if_name);
        }
    }
    std::free(ptr);
}

} // extern "C"
