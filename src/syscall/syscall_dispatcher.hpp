#pragma once

#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "linux_types.hpp"
#include "mem/address_space.hpp"
#include "sync/futex.hpp"
#include <cstddef>
#include <cstdint>

namespace portage::syscall {

// Linux errno codes (negative return values)
constexpr int64_t LINUX_EPERM = -1;
constexpr int64_t LINUX_ENOENT = -2;
constexpr int64_t LINUX_ESRCH = -3;
constexpr int64_t LINUX_EINTR = -4;
constexpr int64_t LINUX_EIO = -5;
constexpr int64_t LINUX_EBADF = -9;
constexpr int64_t LINUX_EAGAIN = -11;
constexpr int64_t LINUX_ENOMEM = -12;
constexpr int64_t LINUX_EACCES = -13;
constexpr int64_t LINUX_EFAULT = -14;
constexpr int64_t LINUX_EEXIST = -17;
constexpr int64_t LINUX_ENODEV = -19;
constexpr int64_t LINUX_ENOTDIR = -20;
constexpr int64_t LINUX_EISDIR = -21;
constexpr int64_t LINUX_EINVAL = -22;
constexpr int64_t LINUX_ENFILE = -23;
constexpr int64_t LINUX_EMFILE = -24;
constexpr int64_t LINUX_ENOSYS = -38;
constexpr int64_t LINUX_ENOTSOCK = -88;
constexpr int64_t LINUX_ETIMEDOUT = -110;

struct LinuxUtsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

class SyscallDispatcher {
  public:
    static void Initialize(mem::AddressSpace *as, fs::Vfs *vfs = nullptr,
                           fs::FileDescriptorTable *fd_table = nullptr,
                           bool verbose = false) noexcept;
    static void Shutdown() noexcept;

    __attribute__((sysv_abi)) static int64_t Dispatch(uint64_t nr, uint64_t a1, uint64_t a2,
                                                      uint64_t a3, uint64_t a4, uint64_t a5,
                                                      uint64_t a6) noexcept;

    [[nodiscard]] static bool HasExited() noexcept { return has_exited_; }
    [[nodiscard]] static int GetExitCode() noexcept { return exit_code_; }
    [[nodiscard]] static fs::FileDescriptorTable *GetFdTable() noexcept { return fd_table_; }

  private:
    static mem::AddressSpace *address_space_;
    static fs::Vfs *vfs_;
    static fs::FileDescriptorTable *fd_table_;
    static sync::FutexManager futex_manager_;
    static bool verbose_;
    static bool has_exited_;
    static int exit_code_;
    static uintptr_t child_tid_ptr_;
};

} // namespace portage::syscall

extern "C" __attribute__((sysv_abi)) int64_t portage_syscall_dispatch(uint64_t nr, uint64_t a1,
                                                                      uint64_t a2, uint64_t a3,
                                                                      uint64_t a4, uint64_t a5,
                                                                      uint64_t a6) noexcept;
