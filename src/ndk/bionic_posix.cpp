#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
    registry.Register("realpath", reinterpret_cast<uintptr_t>(&portage_realpath));
    registry.Register("symlink", reinterpret_cast<uintptr_t>(&portage_symlink));
    registry.Register("link", reinterpret_cast<uintptr_t>(&portage_link));
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

__attribute__((sysv_abi)) int portage_symlink(const char *, const char *)
{
    *portage_errno() = 38; // ENOSYS
    return -1;
}

__attribute__((sysv_abi)) int portage_link(const char *, const char *)
{
    *portage_errno() = 38;
    return -1;
}

__attribute__((sysv_abi)) int portage_fchmod(int, int) { return 0; }

__attribute__((sysv_abi)) int portage_fchmodat(int, const char *, int, int) { return 0; }

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

__attribute__((sysv_abi)) int portage_utimensat(int, const char *, const GuestTimespec[2], int)
{
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

} // extern "C"
