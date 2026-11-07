#include "syscall_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"
#include "binder/binder_driver.hpp"
#include "binder/binder_types.hpp"
#include "fs/dirent.hpp"
#include "fs/proc_fs.hpp"
#include "input/input_manager.hpp"
#include "input/input_types.hpp"
#include "net/socket_manager.hpp"
#include "net/socket_types.hpp"
#include "poll/epoll_manager.hpp"
#include "poll/poll_types.hpp"
#include "process/process_manager.hpp"
#include "property/property_manager.hpp"
#include "property/property_types.hpp"
#include "rewrite/syscall_rewriter.hpp"
#include "signal/signal_manager.hpp"
#include "thread/thread_manager.hpp"
#include "vulkan/vulkan_bridge.hpp"
#include "vulkan/vulkan_types.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <algorithm>
#include <cstring>
#include <immintrin.h>
#include <iostream>
#include <windows.h>
#include <winsock2.h>

namespace portage::arch {
void RestoreActiveTcb() noexcept;
void SetActiveTcb(uintptr_t tcb) noexcept;
void SignalGuestExit(int code) noexcept;
} // namespace portage::arch

namespace portage::syscall {

mem::AddressSpace *SyscallDispatcher::address_space_ = nullptr;
fs::Vfs *SyscallDispatcher::vfs_ = nullptr;
fs::FileDescriptorTable *SyscallDispatcher::fd_table_ = nullptr;
sync::FutexManager SyscallDispatcher::futex_manager_{};
bool SyscallDispatcher::verbose_ = false;
bool SyscallDispatcher::has_exited_ = false;
int SyscallDispatcher::exit_code_ = 0;
uintptr_t SyscallDispatcher::child_tid_ptr_ = 0;

void SyscallDispatcher::Initialize(mem::AddressSpace *as, fs::Vfs *vfs,
                                   fs::FileDescriptorTable *fd_table, bool verbose) noexcept
{
    address_space_ = as;
    vfs_ = vfs;
    fd_table_ = fd_table;
    verbose_ = verbose;
    has_exited_ = false;
    exit_code_ = 0;
    child_tid_ptr_ = 0;
    property::PropertyManager::Instance().Initialize(vfs ? vfs->GetRootfs() : "");
}

void SyscallDispatcher::Shutdown() noexcept
{
    property::PropertyManager::Instance().Reset();
    address_space_ = nullptr;
    vfs_ = nullptr;
    fd_table_ = nullptr;
}

__attribute__((sysv_abi)) int64_t SyscallDispatcher::Dispatch(uint64_t nr, uint64_t a1, uint64_t a2,
                                                              uint64_t a3, uint64_t a4, uint64_t a5,
                                                              uint64_t a6) noexcept
{
    int64_t ret = LINUX_ENOSYS;

    switch (nr) {
    case 0: { // sys_read(fd, buf, count)
        int fd = static_cast<int>(a1);
        void *buf = reinterpret_cast<void *>(a2);
        size_t count = static_cast<size_t>(a3);

        if (!buf) {
            ret = LINUX_EFAULT;
            break;
        }

        if (fd_table_) {
            auto desc = fd_table_->Get(fd);
            if (!desc) {
                ret = LINUX_EBADF;
                break;
            }

            if (desc->dev == fs::DeviceType::DevNull) {
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::DevZero) {
                std::memset(buf, 0, count);
                ret = static_cast<int64_t>(count);
                break;
            }

            if (desc->dev == fs::DeviceType::DevUrandom) {
                uint8_t *byte_ptr = static_cast<uint8_t *>(buf);
                for (size_t i = 0; i < count; i++) {
                    byte_ptr[i] = static_cast<uint8_t>(rand() & 0xFF);
                }
                ret = static_cast<int64_t>(count);
                break;
            }

            if (desc->dev == fs::DeviceType::SyntheticProc) {
                if (desc->offset >= desc->synthetic_data.size()) {
                    ret = 0; // EOF
                } else {
                    size_t avail = desc->synthetic_data.size() - desc->offset;
                    size_t to_copy = std::min(count, avail);
                    std::memcpy(buf, desc->synthetic_data.data() + desc->offset, to_copy);
                    desc->offset += to_copy;
                    ret = static_cast<int64_t>(to_copy);
                }
                break;
            }

            if (desc->dev == fs::DeviceType::Directory || desc->dev == fs::DeviceType::InputDir ||
                desc->dev == fs::DeviceType::PropAreaDir ||
                desc->dev == fs::DeviceType::PropSocketDir) {
                ret = LINUX_EISDIR;
                break;
            }

            if (desc->dev == fs::DeviceType::InputEvent0 ||
                desc->dev == fs::DeviceType::InputEvent1) {
                int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
                bool nonblocking = (desc->flags & 00004000) != 0; // O_NONBLOCK
                size_t max_events = count / sizeof(input::LinuxInputEvent);
                ret = input::InputManager::Instance().ReadEvents(
                    dev_idx, reinterpret_cast<input::LinuxInputEvent *>(buf), max_events,
                    nonblocking);
                break;
            }

            if (desc->dev == fs::DeviceType::Inotify) {
                ret = LINUX_EAGAIN;
                break;
            }

            if (desc->dev == fs::DeviceType::PropSerial) {
                const uint8_t *src = property::PropertyManager::Instance().GetSerialBuffer();
                size_t area_size = property::PropertyManager::Instance().GetSerialSize();
                if (desc->offset < area_size) {
                    size_t to_read = std::min(count, area_size - static_cast<size_t>(desc->offset));
                    std::memcpy(buf, src + desc->offset, to_read);
                    desc->offset += to_read;
                    ret = static_cast<int64_t>(to_read);
                } else {
                    ret = 0;
                }
                break;
            }

            if (desc->dev == fs::DeviceType::PropContext ||
                desc->dev == fs::DeviceType::PropLegacy) {
                const uint8_t *src = property::PropertyManager::Instance().GetPropertyAreaBuffer();
                size_t area_size = property::PropertyManager::Instance().GetPropertyAreaSize();
                if (desc->offset < area_size) {
                    size_t to_read = std::min(count, area_size - static_cast<size_t>(desc->offset));
                    std::memcpy(buf, src + desc->offset, to_read);
                    desc->offset += to_read;
                    ret = static_cast<int64_t>(to_read);
                } else {
                    ret = 0;
                }
                break;
            }

            if (desc->dev == fs::DeviceType::PropInfo) {
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::PropServiceSocket) {
                if (desc->synthetic_data.empty()) {
                    uint32_t resp = 0;
                    size_t to_read = std::min(count, sizeof(resp));
                    std::memcpy(buf, &resp, to_read);
                    ret = static_cast<int64_t>(to_read);
                } else {
                    size_t to_read = std::min(count, desc->synthetic_data.size());
                    std::memcpy(buf, desc->synthetic_data.data(), to_read);
                    desc->synthetic_data.erase(0, to_read);
                    ret = static_cast<int64_t>(to_read);
                }
                break;
            }

            if (desc->dev == fs::DeviceType::Socket) {
                if (desc->handle != INVALID_HANDLE_VALUE) {
                    SOCKET s = reinterpret_cast<SOCKET>(desc->handle);
                    int r = ::recv(s, static_cast<char *>(buf), static_cast<int>(count), 0);
                    if (r == SOCKET_ERROR) {
                        ret = net::SocketManager::TranslateWsaError(WSAGetLastError());
                    } else {
                        ret = r;
                    }
                } else {
                    ret = LINUX_EBADF;
                }
                break;
            }

            if (desc->handle != INVALID_HANDLE_VALUE) {
                DWORD read_bytes = 0;
                if (ReadFile(desc->handle, buf, static_cast<DWORD>(count), &read_bytes, nullptr)) {
                    desc->offset += read_bytes;
                    ret = static_cast<int64_t>(read_bytes);
                } else {
                    ret = LINUX_EIO;
                }
                break;
            }
        }

        if (fd == 0) {
            HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
            DWORD read_bytes = 0;
            if (ReadFile(h, buf, static_cast<DWORD>(count), &read_bytes, nullptr)) {
                ret = static_cast<int64_t>(read_bytes);
            } else {
                ret = LINUX_EIO;
            }
        } else {
            ret = LINUX_EBADF;
        }
        break;
    }

    case 1: { // sys_write(fd, buf, count)
        int fd = static_cast<int>(a1);
        const void *buf = reinterpret_cast<const void *>(a2);
        size_t count = static_cast<size_t>(a3);

        if (!buf) {
            ret = LINUX_EFAULT;
            break;
        }

        if (fd_table_) {
            auto desc = fd_table_->Get(fd);
            if (!desc) {
                ret = LINUX_EBADF;
                break;
            }

            if (desc->dev == fs::DeviceType::DevNull) {
                ret = static_cast<int64_t>(count);
                break;
            }

            if (desc->dev == fs::DeviceType::AlsaPcm) {
                ret = audio::AudioManager::Instance().WritePcmBytes(buf, count);
                break;
            }

            if (desc->dev == fs::DeviceType::Socket) {
                if (desc->handle != INVALID_HANDLE_VALUE) {
                    SOCKET s = reinterpret_cast<SOCKET>(desc->handle);
                    int r = ::send(s, static_cast<const char *>(buf), static_cast<int>(count), 0);
                    if (r == SOCKET_ERROR) {
                        ret = net::SocketManager::TranslateWsaError(WSAGetLastError());
                    } else {
                        ret = r;
                    }
                } else {
                    ret = LINUX_EBADF;
                }
                break;
            }

            if (desc->dev == fs::DeviceType::PropServiceSocket) {
                std::span<const uint8_t> payload(reinterpret_cast<const uint8_t *>(buf), count);
                uint32_t result =
                    property::PropertyManager::Instance().HandleSetPropMessage(payload);
                desc->synthetic_data.resize(sizeof(uint32_t));
                std::memcpy(desc->synthetic_data.data(), &result, sizeof(uint32_t));
                ret = static_cast<int64_t>(count);
                break;
            }

            if (desc->handle != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                if (WriteFile(desc->handle, buf, static_cast<DWORD>(count), &written, nullptr)) {
                    desc->offset += written;
                    ret = static_cast<int64_t>(written);
                } else {
                    ret = LINUX_EIO;
                }
                break;
            }
        }

        if (fd == 1 || fd == 2) {
            HANDLE h = GetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            DWORD written = 0;
            if (WriteFile(h, buf, static_cast<DWORD>(count), &written, nullptr)) {
                ret = static_cast<int64_t>(written);
            } else {
                ret = LINUX_EIO;
            }
        } else {
            ret = LINUX_EBADF;
        }
        break;
    }

    case 3: { // sys_close(fd)
        int fd = static_cast<int>(a1);
        if (fd_table_) {
            ret = fd_table_->Close(fd);
        } else {
            ret = (fd >= 0) ? 0 : LINUX_EBADF;
        }
        break;
    }

    case 5: { // sys_fstat(fd, statbuf)
        int fd = static_cast<int>(a1);
        auto *sb = reinterpret_cast<LinuxStat64 *>(a2);
        if (!sb) {
            ret = LINUX_EFAULT;
            break;
        }

        std::memset(sb, 0, sizeof(LinuxStat64));

        if (fd == 0 || fd == 1 || fd == 2) {
            sb->st_mode = LINUX_S_IFCHR | 0666;
            sb->st_nlink = 1;
            ret = 0;
            break;
        }

        if (fd_table_) {
            auto desc = fd_table_->Get(fd);
            if (!desc) {
                ret = LINUX_EBADF;
                break;
            }

            if (desc->dev == fs::DeviceType::SyntheticProc) {
                sb->st_mode = LINUX_S_IFREG | 0444;
                sb->st_size = static_cast<int64_t>(desc->synthetic_data.size());
                sb->st_nlink = 1;
                sb->st_blksize = 4096;
                sb->st_blocks = (sb->st_size + 511) / 512;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::Directory || desc->dev == fs::DeviceType::InputDir ||
                desc->dev == fs::DeviceType::PropAreaDir ||
                desc->dev == fs::DeviceType::PropSocketDir) {
                sb->st_mode = LINUX_S_IFDIR | 0755;
                sb->st_size = 4096;
                sb->st_nlink = 2;
                sb->st_blksize = 4096;
                sb->st_blocks = 8;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::PropSerial) {
                sb->st_mode = LINUX_S_IFREG | 0444;
                sb->st_size =
                    static_cast<int64_t>(property::PropertyManager::Instance().GetSerialSize());
                sb->st_nlink = 1;
                sb->st_blksize = 4096;
                sb->st_blocks = 8;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::PropContext ||
                desc->dev == fs::DeviceType::PropLegacy) {
                sb->st_mode = LINUX_S_IFREG | 0444;
                sb->st_size = static_cast<int64_t>(
                    property::PropertyManager::Instance().GetPropertyAreaSize());
                sb->st_nlink = 1;
                sb->st_blksize = 4096;
                sb->st_blocks =
                    (property::PropertyManager::Instance().GetPropertyAreaSize() + 511) / 512;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::PropInfo) {
                sb->st_mode = LINUX_S_IFREG | 0444;
                sb->st_size = 0;
                sb->st_nlink = 1;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::PropServiceSocket) {
                sb->st_mode = 0140666; // S_IFSOCK | 0666
                sb->st_nlink = 1;
                ret = 0;
                break;
            }

            if (desc->dev == fs::DeviceType::Pipe) {
                sb->st_mode = LINUX_S_IFIFO | 0600;
                sb->st_nlink = 1;
                sb->st_blksize = 4096;
                ret = 0;
                break;
            }

            if (desc->dev != fs::DeviceType::Regular) {
                sb->st_mode = LINUX_S_IFCHR | 0666;
                sb->st_nlink = 1;
                ret = 0;
                break;
            }

            BY_HANDLE_FILE_INFORMATION info{};
            if (GetFileInformationByHandle(desc->handle, &info)) {
                LARGE_INTEGER size;
                size.LowPart = info.nFileSizeLow;
                size.HighPart = static_cast<LONG>(info.nFileSizeHigh);

                sb->st_mode = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                  ? (LINUX_S_IFDIR | 0755)
                                  : (LINUX_S_IFREG | 0644);
                sb->st_size = size.QuadPart;
                sb->st_nlink = info.nNumberOfLinks;
                sb->st_blksize = 4096;
                sb->st_blocks = (sb->st_size + 511) / 512;
                ret = 0;
            } else {
                ret = LINUX_EBADF;
            }
        } else {
            ret = LINUX_EBADF;
        }
        break;
    }

    case 7: { // sys_poll(fds, nfds, timeout)
        auto *fds = reinterpret_cast<poll::LinuxPollFd *>(a1);
        size_t nfds = static_cast<size_t>(a2);
        int timeout_ms = static_cast<int>(a3);
        ret = poll::EpollManager::Instance().Poll(fds, nfds, timeout_ms, fd_table_);
        break;
    }

    case 8: { // sys_lseek(fd, offset, whence)
        int fd = static_cast<int>(a1);
        int64_t offset = static_cast<int64_t>(a2);
        int whence = static_cast<int>(a3);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }

        if (desc->dev == fs::DeviceType::SyntheticProc) {
            int64_t new_pos = 0;
            if (whence == 0) { // SEEK_SET
                new_pos = offset;
            } else if (whence == 1) { // SEEK_CUR
                new_pos = static_cast<int64_t>(desc->offset) + offset;
            } else if (whence == 2) { // SEEK_END
                new_pos = static_cast<int64_t>(desc->synthetic_data.size()) + offset;
            } else {
                ret = LINUX_EINVAL;
                break;
            }
            if (new_pos < 0) {
                ret = LINUX_EINVAL;
            } else {
                desc->offset = static_cast<uint64_t>(new_pos);
                ret = new_pos;
            }
            break;
        }

        if (desc->dev == fs::DeviceType::PropContext || desc->dev == fs::DeviceType::PropLegacy ||
            desc->dev == fs::DeviceType::PropSerial) {
            size_t max_size = (desc->dev == fs::DeviceType::PropSerial)
                                  ? property::PropertyManager::Instance().GetSerialSize()
                                  : property::PropertyManager::Instance().GetPropertyAreaSize();
            int64_t new_pos = 0;
            if (whence == 0) { // SEEK_SET
                new_pos = offset;
            } else if (whence == 1) { // SEEK_CUR
                new_pos = static_cast<int64_t>(desc->offset) + offset;
            } else if (whence == 2) { // SEEK_END
                new_pos = static_cast<int64_t>(max_size) + offset;
            } else {
                ret = LINUX_EINVAL;
                break;
            }
            if (new_pos < 0) {
                ret = LINUX_EINVAL;
            } else {
                desc->offset = static_cast<uint64_t>(new_pos);
                ret = new_pos;
            }
            break;
        }

        if (desc->dev == fs::DeviceType::Directory) {
            if (offset == 0 && whence == 0) {
                desc->dir_index = 0;
                ret = 0;
            } else {
                ret = LINUX_EINVAL;
            }
            break;
        }

        if (desc->handle == INVALID_HANDLE_VALUE) {
            ret = LINUX_EBADF;
            break;
        }

        DWORD method = FILE_BEGIN;
        if (whence == 1) {
            method = FILE_CURRENT;
        } else if (whence == 2) {
            method = FILE_END;
        } else if (whence != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        LARGE_INTEGER move_dist;
        move_dist.QuadPart = offset;
        LARGE_INTEGER new_pos;

        if (SetFilePointerEx(desc->handle, move_dist, &new_pos, method)) {
            desc->offset = new_pos.QuadPart;
            ret = new_pos.QuadPart;
        } else {
            ret = LINUX_EINVAL;
        }
        break;
    }

    case 9: { // sys_mmap(addr, length, prot, flags, fd, offset)
        if (!address_space_) {
            ret = LINUX_ENOMEM;
            break;
        }

        void *addr = reinterpret_cast<void *>(a1);
        size_t length = static_cast<size_t>(a2);
        int prot = static_cast<int>(a3);
        int flags = static_cast<int>(a4);
        int fd = static_cast<int>(a5);
        uint64_t offset = a6;

        bool is_anonymous = (flags & 32) != 0;

        if (is_anonymous || fd < 0) {
            void *res = address_space_->MmapAnonymous(addr, length, prot, flags);
            if (res && (prot & 4) != 0) { // PROT_EXEC
                rewrite::SyscallRewriter::PatchCodeRegion(res, length);
            }
            ret = reinterpret_cast<int64_t>(res);
        } else {
            // File-backed mmap
            if (!fd_table_) {
                ret = LINUX_EBADF;
                break;
            }

            auto desc = fd_table_->Get(fd);
            if (!desc) {
                ret = LINUX_EBADF;
                break;
            }

            if (desc->dev == fs::DeviceType::PropContext ||
                desc->dev == fs::DeviceType::PropLegacy) {
                void *res = address_space_->MmapAnonymous(addr, length, prot | 2, flags);
                if (!res) {
                    ret = LINUX_ENOMEM;
                    break;
                }
                size_t area_size = property::PropertyManager::Instance().GetPropertyAreaSize();
                if (offset < area_size) {
                    size_t copy_len = std::min(length, area_size - static_cast<size_t>(offset));
                    std::memcpy(
                        res, property::PropertyManager::Instance().GetPropertyAreaBuffer() + offset,
                        copy_len);
                }
                property::PropertyManager::Instance().RegisterPropertyArea(
                    static_cast<uint8_t *>(res));
                (void)address_space_->Mprotect(res, length, prot);
                ret = reinterpret_cast<int64_t>(res);
                break;
            }

            if (desc->dev == fs::DeviceType::PropSerial) {
                void *res = address_space_->MmapAnonymous(addr, length, prot | 2, flags);
                if (!res) {
                    ret = LINUX_ENOMEM;
                    break;
                }
                size_t area_size = property::PropertyManager::Instance().GetSerialSize();
                if (offset < area_size) {
                    size_t copy_len = std::min(length, area_size - static_cast<size_t>(offset));
                    std::memcpy(res,
                                property::PropertyManager::Instance().GetSerialBuffer() + offset,
                                copy_len);
                }
                property::PropertyManager::Instance().RegisterSerialArea(
                    static_cast<uint8_t *>(res));
                (void)address_space_->Mprotect(res, length, prot);
                ret = reinterpret_cast<int64_t>(res);
                break;
            }

            if (desc->dev == fs::DeviceType::PropInfo) {
                void *res = address_space_->MmapAnonymous(addr, length, prot, flags);
                if (!res) {
                    ret = LINUX_ENOMEM;
                    break;
                }
                ret = reinterpret_cast<int64_t>(res);
                break;
            }

            if (desc->handle == INVALID_HANDLE_VALUE) {
                ret = LINUX_EBADF;
                break;
            }

            void *res = address_space_->MmapAnonymous(addr, length,
                                                      prot | 2 /* write for population */, flags);
            if (!res) {
                ret = LINUX_ENOMEM;
                break;
            }

            LARGE_INTEGER seek_target;
            seek_target.QuadPart = static_cast<LONGLONG>(offset);
            SetFilePointerEx(desc->handle, seek_target, nullptr, FILE_BEGIN);

            DWORD bytes_to_read = static_cast<DWORD>(std::min<size_t>(length, 0xFFFFFFFF));
            DWORD bytes_read = 0;
            ReadFile(desc->handle, res, bytes_to_read, &bytes_read, nullptr);

            if ((prot & 4) != 0) { // PROT_EXEC
                rewrite::SyscallRewriter::PatchCodeRegion(res, length);
            }

            (void)address_space_->Mprotect(res, length, prot);
            ret = reinterpret_cast<int64_t>(res);
        }
        break;
    }

    case 10: { // sys_mprotect(addr, len, prot)
        if (address_space_) {
            void *addr = reinterpret_cast<void *>(a1);
            size_t len = static_cast<size_t>(a2);
            int prot = static_cast<int>(a3);

            int res = address_space_->Mprotect(addr, len, prot);
            if (res == 0) {
                if ((prot & 4) != 0) { // PROT_EXEC
                    rewrite::SyscallRewriter::PatchCodeRegion(addr, len);
                }
                ret = 0;
            } else {
                ret = LINUX_EACCES;
            }
        } else {
            ret = LINUX_EFAULT;
        }
        break;
    }

    case 11: { // sys_munmap(addr, len)
        if (address_space_) {
            int res = address_space_->Munmap(reinterpret_cast<void *>(a1), a2);
            ret = (res == 0) ? 0 : LINUX_EINVAL;
        } else {
            ret = LINUX_EINVAL;
        }
        break;
    }

    case 12: { // sys_brk(new_brk)
        if (address_space_) {
            ret = static_cast<int64_t>(address_space_->Brk(a1));
        } else {
            ret = static_cast<int64_t>(a1);
        }
        break;
    }

    case 13: { // sys_rt_sigaction(sig, act, oact, sigsetsize)
        int sig = static_cast<int>(a1);
        const auto *act = reinterpret_cast<const signal::LinuxSigaction *>(a2);
        auto *old_act = reinterpret_cast<signal::LinuxSigaction *>(a3);
        if (act) {
            ret = signal::SignalManager::Instance().SetAction(sig, *act, old_act);
        } else if (old_act) {
            ret = signal::SignalManager::Instance().GetAction(sig, old_act);
        } else {
            ret = 0;
        }
        break;
    }

    case 14: { // sys_rt_sigprocmask(how, set, oldset, sigsetsize)
        int how = static_cast<int>(a1);
        const auto *set = reinterpret_cast<const uint64_t *>(a2);
        auto *oldset = reinterpret_cast<uint64_t *>(a3);
        auto th = thread::ThreadManager::Instance().GetCurrentGuestThread();
        if (oldset && th) {
            *oldset = th->sigmask;
        }
        if (set && th) {
            if (how == signal::LINUX_SIG_BLOCK) {
                th->sigmask |= *set;
            } else if (how == signal::LINUX_SIG_UNBLOCK) {
                th->sigmask &= ~*set;
            } else if (how == signal::LINUX_SIG_SETMASK) {
                th->sigmask = *set;
            }
        }
        ret = 0;
        break;
    }

    case 16: { // sys_ioctl(fd, cmd, arg)
        int fd = static_cast<int>(a1);
        uint32_t cmd = static_cast<uint32_t>(a2);
        void *arg = reinterpret_cast<void *>(a3);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }

        if (desc->dev == fs::DeviceType::Binder || desc->dev == fs::DeviceType::HwBinder) {
            ret = binder::BinderDriver::Instance().Ioctl(desc->binder_id, cmd, arg);
        } else if (desc->dev == fs::DeviceType::VulkanDevice) {
            if (cmd == vulkan::VULKAN_IOCTL_GET_DISPATCH_TABLE) {
                if (!arg) {
                    ret = LINUX_EFAULT;
                    break;
                }
                if (!vulkan::VulkanBridge::Instance().Initialize()) {
                    ret = LINUX_ENODEV;
                    break;
                }
                const void *table_ptr = vulkan::VulkanBridge::Instance().GetDispatchTable();
                std::memcpy(arg, &table_ptr, sizeof(const void *));
                ret = 0;
            } else {
                ret = LINUX_EINVAL;
            }
        } else if (desc->dev == fs::DeviceType::AlsaPcm) {
            ret = audio::AudioManager::Instance().HandlePcmIoctl(cmd, arg);
        } else if (desc->dev == fs::DeviceType::AlsaControl) {
            ret = audio::AudioManager::Instance().HandleControlIoctl(cmd, arg);
        } else if (desc->dev == fs::DeviceType::InputEvent0 ||
                   desc->dev == fs::DeviceType::InputEvent1) {
            int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
            ret = input::InputManager::Instance().HandleIoctl(dev_idx, cmd, arg);
        } else {
            ret = LINUX_ENOSYS;
        }
        break;
    }

    case 17: { // sys_pread64(fd, buf, count, offset)
        int fd = static_cast<int>(a1);
        void *buf = reinterpret_cast<void *>(a2);
        size_t count = static_cast<size_t>(a3);
        uint64_t offset = a4;

        if (!buf) {
            ret = LINUX_EFAULT;
            break;
        }

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }

        if (desc->dev == fs::DeviceType::SyntheticProc) {
            if (offset >= desc->synthetic_data.size()) {
                ret = 0;
            } else {
                size_t avail = desc->synthetic_data.size() - offset;
                size_t to_copy = std::min(count, avail);
                std::memcpy(buf, desc->synthetic_data.data() + offset, to_copy);
                ret = static_cast<int64_t>(to_copy);
            }
            break;
        }

        if (desc->handle == INVALID_HANDLE_VALUE) {
            ret = LINUX_EBADF;
            break;
        }

        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);

        DWORD bytes_read = 0;
        if (ReadFile(desc->handle, buf, static_cast<DWORD>(count), &bytes_read, &ov)) {
            ret = static_cast<int64_t>(bytes_read);
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_HANDLE_EOF) {
                ret = 0;
            } else {
                ret = LINUX_EIO;
            }
        }
        break;
    }

    case 20: { // sys_writev(fd, iov, iovcnt)
        int fd = static_cast<int>(a1);
        const auto *iov = reinterpret_cast<const LinuxIovec *>(a2);
        int iovcnt = static_cast<int>(a3);

        if (!iov || iovcnt < 0) {
            ret = LINUX_EINVAL;
            break;
        }

        int64_t total_written = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len == 0)
                continue;
            int64_t w = Dispatch(1, fd, reinterpret_cast<uint64_t>(iov[i].iov_base), iov[i].iov_len,
                                 0, 0, 0);
            if (w < 0) {
                if (total_written > 0) {
                    ret = total_written;
                } else {
                    ret = w;
                }
                break;
            }
            total_written += w;
        }
        ret = total_written;
        break;
    }

    case 32: { // sys_dup(oldfd)
        int oldfd = static_cast<int>(a1);
        ret = fd_table_ ? fd_table_->Duplicate(oldfd, -1) : LINUX_EBADF;
        break;
    }

    case 33: { // sys_dup2(oldfd, newfd)
        int oldfd = static_cast<int>(a1);
        int newfd = static_cast<int>(a2);
        ret = fd_table_ ? fd_table_->Duplicate(oldfd, newfd) : LINUX_EBADF;
        break;
    }

    case 35: { // sys_nanosleep(req, rem)
        const auto *req = reinterpret_cast<const sync::LinuxTimespec *>(a1);
        auto *rem = reinterpret_cast<sync::LinuxTimespec *>(a2);
        if (!req) {
            ret = LINUX_EFAULT;
            break;
        }
        if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1'000'000'000) {
            ret = LINUX_EINVAL;
            break;
        }

        int64_t intervals =
            -static_cast<int64_t>(req->tv_sec * 10'000'000LL + (req->tv_nsec + 99) / 100);
        LARGE_INTEGER due_time;
        due_time.QuadPart = intervals;

        HANDLE timer = CreateWaitableTimerExW(
            nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
            TIMER_ALL_ACCESS);
        if (!timer) {
            timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
        }

        if (timer) {
            SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
        } else {
            DWORD ms = static_cast<DWORD>(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000);
            Sleep(ms);
        }

        if (rem) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        ret = 0;
        break;
    }

    case 39: { // sys_getpid()
        ret = static_cast<int64_t>(GetCurrentProcessId());
        break;
    }

    case 41: { // sys_socket(domain, type, protocol)
        int domain = static_cast<int>(a1);
        int type = static_cast<int>(a2);
        int protocol = static_cast<int>(a3);
        int out_flags = 0;
        SOCKET s = net::SocketManager::Instance().CreateSocket(domain, type, protocol, out_flags);
        if (s == INVALID_SOCKET) {
            ret = net::SocketManager::TranslateWsaError(WSAGetLastError());
        } else {
            ret = fd_table_ ? fd_table_->AllocateSocket(s, out_flags) : LINUX_ENOSYS;
        }
        break;
    }

    case 42: { // sys_connect(fd, addr, addrlen)
        int fd = static_cast<int>(a1);
        const auto *addr = reinterpret_cast<const net::LinuxSockaddr *>(a2);
        socklen_t len = static_cast<socklen_t>(a3);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || (desc->dev != fs::DeviceType::Socket &&
                      desc->dev != fs::DeviceType::PropServiceSocket)) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        if (addr && addr->sa_family == net::LINUX_AF_UNIX) {
            const auto *lun = reinterpret_cast<const net::LinuxSockaddrUn *>(addr);
            std::string_view sun_path(lun->sun_path);
            if (sun_path == "/dev/socket/property_service" || sun_path == "property_service") {
                desc->dev = fs::DeviceType::PropServiceSocket;
                ret = 0;
                break;
            }
        }
        ret = net::SocketManager::Instance().Connect(reinterpret_cast<SOCKET>(desc->handle), addr,
                                                     len);
        break;
    }

    case 43: { // sys_accept(fd, addr, addrlen)
        int fd = static_cast<int>(a1);
        auto *addr = reinterpret_cast<net::LinuxSockaddr *>(a2);
        auto *len = reinterpret_cast<socklen_t *>(a3);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        SOCKET client_s = net::SocketManager::Instance().Accept(
            reinterpret_cast<SOCKET>(desc->handle), addr, len, 0);
        if (client_s == INVALID_SOCKET) {
            ret = net::SocketManager::TranslateWsaError(WSAGetLastError());
        } else {
            ret = fd_table_->AllocateSocket(client_s, 0);
        }
        break;
    }

    case 44: { // sys_sendto(fd, buf, len, flags, dest_addr, addrlen)
        int fd = static_cast<int>(a1);
        const void *buf = reinterpret_cast<const void *>(a2);
        size_t len = static_cast<size_t>(a3);
        int flags = static_cast<int>(a4);
        const auto *addr = reinterpret_cast<const net::LinuxSockaddr *>(a5);
        socklen_t addrlen = static_cast<socklen_t>(a6);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev == fs::DeviceType::PropServiceSocket) {
            std::span<const uint8_t> payload(reinterpret_cast<const uint8_t *>(buf), len);
            uint32_t result = property::PropertyManager::Instance().HandleSetPropMessage(payload);
            desc->synthetic_data.resize(sizeof(uint32_t));
            std::memcpy(desc->synthetic_data.data(), &result, sizeof(uint32_t));
            ret = static_cast<int64_t>(len);
            break;
        }
        if (desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().SendTo(reinterpret_cast<SOCKET>(desc->handle), buf,
                                                    len, flags, addr, addrlen);
        break;
    }

    case 45: { // sys_recvfrom(fd, buf, len, flags, src_addr, addrlen)
        int fd = static_cast<int>(a1);
        void *buf = reinterpret_cast<void *>(a2);
        size_t len = static_cast<size_t>(a3);
        int flags = static_cast<int>(a4);
        auto *addr = reinterpret_cast<net::LinuxSockaddr *>(a5);
        auto *addrlen = reinterpret_cast<socklen_t *>(a6);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev == fs::DeviceType::PropServiceSocket) {
            if (desc->synthetic_data.empty()) {
                uint32_t resp = 0;
                size_t to_read = std::min(len, sizeof(resp));
                std::memcpy(buf, &resp, to_read);
                ret = static_cast<int64_t>(to_read);
            } else {
                size_t to_read = std::min(len, desc->synthetic_data.size());
                std::memcpy(buf, desc->synthetic_data.data(), to_read);
                desc->synthetic_data.erase(0, to_read);
                ret = static_cast<int64_t>(to_read);
            }
            break;
        }
        if (desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().RecvFrom(reinterpret_cast<SOCKET>(desc->handle), buf,
                                                      len, flags, addr, addrlen);
        break;
    }

    case 48: { // sys_shutdown(fd, how)
        int fd = static_cast<int>(a1);
        int how = static_cast<int>(a2);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().ShutdownSocket(reinterpret_cast<SOCKET>(desc->handle),
                                                            how);
        break;
    }

    case 49: { // sys_bind(fd, addr, addrlen)
        int fd = static_cast<int>(a1);
        const auto *addr = reinterpret_cast<const net::LinuxSockaddr *>(a2);
        socklen_t len = static_cast<socklen_t>(a3);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret =
            net::SocketManager::Instance().Bind(reinterpret_cast<SOCKET>(desc->handle), addr, len);
        break;
    }

    case 50: { // sys_listen(fd, backlog)
        int fd = static_cast<int>(a1);
        int backlog = static_cast<int>(a2);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret =
            net::SocketManager::Instance().Listen(reinterpret_cast<SOCKET>(desc->handle), backlog);
        break;
    }

    case 51: { // sys_getsockname(fd, addr, addrlen)
        int fd = static_cast<int>(a1);
        auto *addr = reinterpret_cast<net::LinuxSockaddr *>(a2);
        auto *len = reinterpret_cast<socklen_t *>(a3);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().GetSockName(reinterpret_cast<SOCKET>(desc->handle),
                                                         addr, len);
        break;
    }

    case 52: { // sys_getpeername(fd, addr, addrlen)
        int fd = static_cast<int>(a1);
        auto *addr = reinterpret_cast<net::LinuxSockaddr *>(a2);
        auto *len = reinterpret_cast<socklen_t *>(a3);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().GetPeerName(reinterpret_cast<SOCKET>(desc->handle),
                                                         addr, len);
        break;
    }

    case 54: { // sys_setsockopt(fd, level, optname, optval, optlen)
        int fd = static_cast<int>(a1);
        int level = static_cast<int>(a2);
        int optname = static_cast<int>(a3);
        const void *optval = reinterpret_cast<const void *>(a4);
        socklen_t optlen = static_cast<socklen_t>(a5);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().SetSockOpt(reinterpret_cast<SOCKET>(desc->handle),
                                                        level, optname, optval, optlen);
        break;
    }

    case 55: { // sys_getsockopt(fd, level, optname, optval, optlen)
        int fd = static_cast<int>(a1);
        int level = static_cast<int>(a2);
        int optname = static_cast<int>(a3);
        void *optval = reinterpret_cast<void *>(a4);
        auto *optlen = reinterpret_cast<socklen_t *>(a5);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        ret = net::SocketManager::Instance().GetSockOpt(reinterpret_cast<SOCKET>(desc->handle),
                                                        level, optname, optval, optlen);
        break;
    }

    case 56: { // sys_clone(flags, child_stack, parent_tid, child_tid, tls)
        uint64_t flags = a1;
        uintptr_t child_stack = a2;
        uintptr_t parent_tid = a3;
        uintptr_t child_tid = a4;
        uintptr_t tls = a5;
        uintptr_t child_rip = a6;

        ret = thread::ThreadManager::Instance().CreateGuestThread(flags, child_stack, parent_tid,
                                                                  child_tid, tls, child_rip);
        break;
    }

    case 59: { // sys_execve(filename, argv, envp)
        const char *filename = reinterpret_cast<const char *>(a1);
        char *const *argv = reinterpret_cast<char *const *>(a2);
        char *const *envp = reinterpret_cast<char *const *>(a3);

        if (!filename) {
            ret = LINUX_EFAULT;
            break;
        }

        std::vector<std::string> args;
        if (argv) {
            for (size_t i = 0; argv[i] != nullptr; ++i) {
                args.emplace_back(argv[i]);
            }
        }

        std::vector<std::string> env;
        if (envp) {
            for (size_t i = 0; envp[i] != nullptr; ++i) {
                env.emplace_back(envp[i]);
            }
        }

        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }

        auto resolved = vfs_->Resolve(filename);
        if (!std::filesystem::exists(resolved.host_path)) {
            ret = LINUX_ENOENT;
            break;
        }

        if (fd_table_) {
            fd_table_->CloseOnExec();
        }

        // Spawn host portage process running the new guest ELF
        std::string portage_exe = vfs_->GetExecutablePath().string();
        int child_pid = process::ProcessManager::Instance().Spawn(
            portage_exe, resolved.host_path.string(), args, env);
        if (child_pid < 0) {
            ret = child_pid;
            break;
        }

        // In POSIX execve, caller does not return on success; wait and exit current process
        int wstatus = 0;
        process::ProcessManager::Instance().Wait4(child_pid, &wstatus, 0, nullptr);
        int exit_code = (wstatus >> 8) & 0xFF;
        arch::SignalGuestExit(exit_code);
        ExitProcess(static_cast<UINT>(exit_code));
        break;
    }

    case 61: { // sys_wait4(pid, wstatus, options, rusage)
        int pid = static_cast<int>(a1);
        int *wstatus = reinterpret_cast<int *>(a2);
        int options = static_cast<int>(a3);
        void *rusage = reinterpret_cast<void *>(a4);
        ret = process::ProcessManager::Instance().Wait4(pid, wstatus, options, rusage);
        break;
    }

    case 60:    // sys_exit
    case 231: { // sys_exit_group
        has_exited_ = true;
        exit_code_ = static_cast<int>(a1);
        if (child_tid_ptr_) {
            auto *ptr = reinterpret_cast<int *>(child_tid_ptr_);
            *ptr = 0;
        }
        int current_tid = thread::ThreadManager::Instance().GetCurrentTid();
        thread::ThreadManager::Instance().OnThreadExit(current_tid, exit_code_);
        break;
    }

    case 62: { // sys_kill(pid, sig)
        ret = 0;
        break;
    }

    case 63: { // sys_uname(struct utsname *buf)
        if (!a1) {
            ret = LINUX_EFAULT;
            break;
        }
        auto *u = reinterpret_cast<LinuxUtsname *>(a1);
        std::memset(u, 0, sizeof(LinuxUtsname));
        std::strncpy(u->sysname, "Linux", sizeof(u->sysname) - 1);
        std::strncpy(u->nodename, "portage-win", sizeof(u->nodename) - 1);
        std::strncpy(u->release, "6.1.0-android14-x86_64", sizeof(u->release) - 1);
        std::strncpy(u->version, "#1 SMP PREEMPT Portage", sizeof(u->version) - 1);
        std::strncpy(u->machine, "x86_64", sizeof(u->machine) - 1);
        ret = 0;
        break;
    }

    case 72: { // sys_fcntl(fd, cmd, arg)
        int fd = static_cast<int>(a1);
        int cmd = static_cast<int>(a2);
        uint64_t arg = a3;

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }

        switch (cmd) {
        case 0:    // F_DUPFD
        case 1030: // F_DUPFD_CLOEXEC
            ret = fd_table_->Duplicate(fd, static_cast<int>(arg));
            break;
        case 1: // F_GETFD
            ret = (desc->flags & LINUX_O_CLOEXEC) ? 1 : 0;
            break;
        case 2: // F_SETFD
            ret = 0;
            break;
        case 3: // F_GETFL
            ret = desc->flags;
            break;
        case 4: // F_SETFL
            desc->flags = static_cast<int>(arg);
            ret = 0;
            break;
        default:
            ret = LINUX_EINVAL;
            break;
        }
        break;
    }

    case 79: { // sys_getcwd(buf, size)
        char *buf = reinterpret_cast<char *>(a1);
        size_t size = static_cast<size_t>(a2);

        if (!buf) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }

        const auto &cwd = vfs_->GetCwd();
        if (cwd.size() + 1 > size) {
            ret = -34; // -ERANGE
            break;
        }

        std::memcpy(buf, cwd.c_str(), cwd.size() + 1);
        ret = static_cast<int64_t>(cwd.size() + 1);
        break;
    }

    case 80: { // sys_chdir(path)
        const char *path = reinterpret_cast<const char *>(a1);
        if (!path) {
            ret = LINUX_EFAULT;
            break;
        }
        ret = vfs_ ? vfs_->ChDir(path) : LINUX_ENOENT;
        break;
    }

    case 102: // sys_getuid
    case 104: // sys_getgid
    case 107: // sys_geteuid
    case 108: // sys_getegid
        ret = 0;
        break;

    case 131: { // sys_sigaltstack(ss, old_ss)
        ret = 0;
        break;
    }

    case 158: { // sys_arch_prctl(int code, unsigned long addr)
        // ARCH_SET_FS = 0x1002, ARCH_GET_FS = 0x1003
        if (a1 == 0x1002) {
            arch::SetActiveTcb(a2);
            ret = 0;
        } else if (a1 == 0x1003) {
            if (a2) {
                *reinterpret_cast<uint64_t *>(a2) = _readfsbase_u64();
                ret = 0;
            } else {
                ret = LINUX_EFAULT;
            }
        } else {
            ret = LINUX_EINVAL;
        }
        break;
    }

    case 186: { // sys_gettid()
        ret = static_cast<int64_t>(thread::ThreadManager::Instance().GetCurrentTid());
        break;
    }

    case 202: { // sys_futex(uaddr, op, val, timeout, uaddr2, val3)
        uint32_t *uaddr = reinterpret_cast<uint32_t *>(a1);
        int op = static_cast<int>(a2);
        uint32_t val = static_cast<uint32_t>(a3);
        const auto *timeout = reinterpret_cast<const sync::LinuxTimespec *>(a4);
        uint32_t *uaddr2 = reinterpret_cast<uint32_t *>(a5);
        uint32_t val3 = static_cast<uint32_t>(a6);

        ret = futex_manager_.Futex(uaddr, op, val, timeout, uaddr2, val3);
        break;
    }

    case 217: { // sys_getdents64(fd, dirp, count)
        int fd = static_cast<int>(a1);
        void *dirp = reinterpret_cast<void *>(a2);
        size_t count = static_cast<size_t>(a3);

        if (!dirp) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev != fs::DeviceType::Directory) {
            ret = LINUX_ENOTDIR;
            break;
        }

        uint8_t *out = reinterpret_cast<uint8_t *>(dirp);
        size_t bytes_written = 0;

        while (desc->dir_index < desc->dir_entries.size()) {
            const auto &entry = desc->dir_entries[desc->dir_index];
            uint16_t reclen = fs::CalculateDirent64Reclen(entry.name.length());
            if (bytes_written + reclen > count) {
                if (bytes_written == 0) {
                    ret = LINUX_EINVAL;
                    break;
                }
                break;
            }

            auto *d = reinterpret_cast<fs::LinuxDirent64 *>(out + bytes_written);
            std::memset(d, 0, reclen);
            d->d_ino = entry.ino;
            d->d_off = static_cast<int64_t>(desc->dir_index + 1);
            d->d_reclen = reclen;
            d->d_type = entry.type;
            std::memcpy(d->d_name, entry.name.c_str(), entry.name.length() + 1);

            bytes_written += reclen;
            desc->dir_index++;
        }

        if (ret != LINUX_EINVAL) {
            ret = static_cast<int64_t>(bytes_written);
        }
        break;
    }

    case 218: { // sys_set_tid_address(int *tidptr)
        child_tid_ptr_ = a1;
        int cur_tid = thread::ThreadManager::Instance().GetCurrentTid();
        thread::ThreadManager::Instance().SetClearChildTid(cur_tid, a1);
        ret = static_cast<int64_t>(cur_tid);
        break;
    }

    case 234: { // sys_tgkill(tgid, tid, sig)
        int tid = static_cast<int>(a2);
        int sig = static_cast<int>(a3);
        auto th = thread::ThreadManager::Instance().GetThread(tid);
        if (!th) {
            ret = LINUX_ESRCH;
        } else {
            th->sigpending.fetch_or(1ULL << sig);
            ret = 0;
        }
        break;
    }

    case 228: { // sys_clock_gettime(clockid_t which_clock, struct timespec *tp)
        if (!a2) {
            ret = LINUX_EFAULT;
            break;
        }
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);

        auto *tp = reinterpret_cast<sync::LinuxTimespec *>(a2);
        tp->tv_sec = counter.QuadPart / freq.QuadPart;
        tp->tv_nsec = (counter.QuadPart % freq.QuadPart) * 1'000'000'000 / freq.QuadPart;
        ret = 0;
        break;
    }

    case 229: { // sys_clock_getres(which_clock, tp)
        auto *tp = reinterpret_cast<sync::LinuxTimespec *>(a2);
        if (tp) {
            tp->tv_sec = 0;
            tp->tv_nsec = 100;
        }
        ret = 0;
        break;
    }

    case 230: { // sys_clock_nanosleep(which_clock, flags, req, rem)
        int flags = static_cast<int>(a2);
        const auto *req = reinterpret_cast<const sync::LinuxTimespec *>(a3);
        auto *rem = reinterpret_cast<sync::LinuxTimespec *>(a4);
        if (!req) {
            ret = LINUX_EFAULT;
            break;
        }
        if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1'000'000'000) {
            ret = LINUX_EINVAL;
            break;
        }

        int64_t intervals = 0;
        if (flags & 1) { // TIMER_ABSTIME
            LARGE_INTEGER freq, counter;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&counter);
            int64_t now_nsec = (counter.QuadPart * 1'000'000'000LL) / freq.QuadPart;
            int64_t target_nsec = req->tv_sec * 1'000'000'000LL + req->tv_nsec;
            int64_t diff_nsec = target_nsec - now_nsec;
            if (diff_nsec <= 0) {
                ret = 0;
                break;
            }
            intervals = -static_cast<int64_t>((diff_nsec + 99) / 100);
        } else {
            intervals =
                -static_cast<int64_t>(req->tv_sec * 10'000'000LL + (req->tv_nsec + 99) / 100);
        }

        LARGE_INTEGER due_time;
        due_time.QuadPart = intervals;
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
        if (!timer) {
            timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
        }

        if (timer) {
            SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
        } else {
            DWORD ms = static_cast<DWORD>(((-intervals) * 100 + 999999) / 1000000);
            Sleep(ms);
        }

        if (rem) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        ret = 0;
        break;
    }

    case 232: { // sys_epoll_wait(epfd, events, maxevents, timeout)
        int epfd = static_cast<int>(a1);
        auto *events = reinterpret_cast<poll::LinuxEpollEvent *>(a2);
        int maxevents = static_cast<int>(a3);
        int timeout = static_cast<int>(a4);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(epfd);
        if (!desc || desc->dev != fs::DeviceType::Epoll) {
            ret = LINUX_EBADF;
            break;
        }
        ret = poll::EpollManager::Instance().Wait(desc->epoll_id, events, maxevents, timeout,
                                                  fd_table_);
        break;
    }

    case 233: { // sys_epoll_ctl(epfd, op, fd, event)
        int epfd = static_cast<int>(a1);
        int op = static_cast<int>(a2);
        int fd = static_cast<int>(a3);
        const auto *event = reinterpret_cast<const poll::LinuxEpollEvent *>(a4);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(epfd);
        if (!desc || desc->dev != fs::DeviceType::Epoll) {
            ret = LINUX_EBADF;
            break;
        }
        ret = poll::EpollManager::Instance().Ctl(desc->epoll_id, op, fd, event);
        break;
    }

    case 257: { // sys_openat(dfd, pathname, flags, mode)
        int dfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        int flags = static_cast<int>(a3);

        if (!pathname) {
            ret = LINUX_EFAULT;
            break;
        }

        if (!vfs_ || !fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }

        std::string dirfd_path;
        if (dfd >= 0) {
            auto dfd_desc = fd_table_->Get(dfd);
            if (dfd_desc) {
                dirfd_path = dfd_desc->guest_path;
            }
        }

        auto resolved = vfs_->ResolveAt(dfd, pathname, dirfd_path);

        if (resolved.dev_type == fs::DeviceType::DevNull ||
            resolved.dev_type == fs::DeviceType::DevZero ||
            resolved.dev_type == fs::DeviceType::DevUrandom) {
            ret = fd_table_->Allocate(INVALID_HANDLE_VALUE, flags, resolved.dev_type, pathname);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::Binder ||
            resolved.dev_type == fs::DeviceType::HwBinder) {
            int bid = binder::BinderDriver::Instance().CreateInstance(resolved.dev_type ==
                                                                      fs::DeviceType::HwBinder);
            if (bid < 0) {
                ret = LINUX_ENOMEM;
                break;
            }
            ret = fd_table_->AllocateBinder(bid, resolved.dev_type, flags);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::VulkanDevice) {
            ret = fd_table_->Allocate(INVALID_HANDLE_VALUE, flags, resolved.dev_type, pathname);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::AlsaPcm ||
            resolved.dev_type == fs::DeviceType::AlsaControl) {
            ret = fd_table_->AllocateAudio(resolved.dev_type, flags);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::InputEvent0 ||
            resolved.dev_type == fs::DeviceType::InputEvent1) {
            int dev_idx = (resolved.dev_type == fs::DeviceType::InputEvent0) ? 0 : 1;
            ret = fd_table_->AllocateInput(dev_idx, flags);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::InputDir) {
            std::vector<fs::DirEntry> entries;
            entries.push_back({1, LINUX_DT_DIR, "."});
            entries.push_back({2, LINUX_DT_DIR, ".."});
            entries.push_back({3, LINUX_DT_CHR, "event0"});
            entries.push_back({4, LINUX_DT_CHR, "event1"});
            ret = fd_table_->AllocateDirectory(flags, "/dev/input", std::move(entries));
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropAreaDir) {
            if (flags & LINUX_O_DIRECTORY) {
                std::vector<fs::DirEntry> entries;
                entries.push_back({1, LINUX_DT_DIR, "."});
                entries.push_back({2, LINUX_DT_DIR, ".."});
                entries.push_back({3, LINUX_DT_REG, "properties_serial"});
                entries.push_back({4, LINUX_DT_REG, "property_info"});
                entries.push_back({5, LINUX_DT_REG, "u:object_r:default_prop:s0"});
                ret =
                    fd_table_->AllocateDirectory(flags, "/dev/__properties__", std::move(entries));
                break;
            } else {
                ret = fd_table_->Allocate(INVALID_HANDLE_VALUE, flags, fs::DeviceType::PropLegacy,
                                          pathname);
                break;
            }
        }

        if (resolved.dev_type == fs::DeviceType::PropSocketDir) {
            std::vector<fs::DirEntry> entries;
            entries.push_back({1, LINUX_DT_DIR, "."});
            entries.push_back({2, LINUX_DT_DIR, ".."});
            entries.push_back({3, LINUX_DT_SOCK, "property_service"});
            ret = fd_table_->AllocateDirectory(flags, "/dev/socket", std::move(entries));
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropSerial ||
            resolved.dev_type == fs::DeviceType::PropInfo ||
            resolved.dev_type == fs::DeviceType::PropContext ||
            resolved.dev_type == fs::DeviceType::PropLegacy) {
            ret = fd_table_->Allocate(INVALID_HANDLE_VALUE, flags, resolved.dev_type, pathname);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropServiceSocket) {
            ret = fd_table_->Allocate(INVALID_HANDLE_VALUE, flags,
                                      fs::DeviceType::PropServiceSocket, pathname);
            break;
        }

        if (resolved.dev_type == fs::DeviceType::SyntheticProc) {
            std::string data = fs::ProcFs::Generate(pathname, address_space_);
            ret = fd_table_->AllocateSynthetic(flags, resolved.dev_type, pathname, std::move(data));
            break;
        }

        if (resolved.dev_type == fs::DeviceType::Directory || (flags & LINUX_O_DIRECTORY)) {
            if (pathname == std::string_view("/proc") ||
                pathname == std::string_view("/proc/self")) {
                auto entries = fs::ProcFs::ListDir(pathname);
                ret = fd_table_->AllocateDirectory(flags, pathname, std::move(entries));
                break;
            }

            std::vector<fs::DirEntry> entries;
            entries.push_back({1, LINUX_DT_DIR, "."});
            entries.push_back({2, LINUX_DT_DIR, ".."});
            try {
                if (std::filesystem::exists(resolved.host_path) &&
                    std::filesystem::is_directory(resolved.host_path)) {
                    for (const auto &item : std::filesystem::directory_iterator(
                             resolved.host_path,
                             std::filesystem::directory_options::skip_permission_denied)) {
                        uint8_t dt = LINUX_DT_UNKNOWN;
                        if (item.is_directory())
                            dt = LINUX_DT_DIR;
                        else if (item.is_symlink())
                            dt = LINUX_DT_LNK;
                        else if (item.is_regular_file())
                            dt = LINUX_DT_REG;

                        std::string item_name = item.path().filename().string();
                        uint64_t ino = std::hash<std::string>{}(item_name) & 0x7FFFFFFFFFFFFFFFULL;
                        entries.push_back({ino, dt, std::move(item_name)});
                    }
                }
            } catch (...) {
            }
            ret = fd_table_->AllocateDirectory(flags, pathname, std::move(entries));
            break;
        }

        if (resolved.host_path.empty()) {
            ret = LINUX_ENOENT;
            break;
        }

        DWORD desired_access = GENERIC_READ;
        int acc = flags & LINUX_O_ACCMODE;
        if (acc == LINUX_O_WRONLY) {
            desired_access = GENERIC_WRITE;
        } else if (acc == LINUX_O_RDWR) {
            desired_access = GENERIC_READ | GENERIC_WRITE;
        }

        DWORD creation_disposition = OPEN_EXISTING;
        if (flags & LINUX_O_CREAT) {
            if (flags & LINUX_O_EXCL) {
                creation_disposition = CREATE_NEW;
            } else if (flags & LINUX_O_TRUNC) {
                creation_disposition = CREATE_ALWAYS;
            } else {
                creation_disposition = OPEN_ALWAYS;
            }
        } else if (flags & LINUX_O_TRUNC) {
            creation_disposition = TRUNCATE_EXISTING;
        }

        HANDLE h = CreateFileW(resolved.host_path.wstring().c_str(), desired_access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                ret = LINUX_ENOENT;
            } else if (err == ERROR_ACCESS_DENIED) {
                ret = LINUX_EACCES;
            } else {
                ret = LINUX_EIO;
            }
            break;
        }

        ret = fd_table_->Allocate(h, flags, fs::DeviceType::Regular, pathname);
        break;
    }

    case 258: { // sys_mkdirat(dfd, pathname, mode)
        int dfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        if (!pathname) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string dirfd_path;
        if (dfd >= 0 && fd_table_) {
            auto dfd_desc = fd_table_->Get(dfd);
            if (dfd_desc)
                dirfd_path = dfd_desc->guest_path;
        }
        auto resolved = vfs_->ResolveAt(dfd, pathname, dirfd_path);
        if (std::filesystem::exists(resolved.host_path)) {
            ret = LINUX_EEXIST;
            break;
        }
        std::error_code ec;
        if (std::filesystem::create_directories(resolved.host_path, ec)) {
            ret = 0;
        } else {
            ret = ec ? LINUX_EIO : LINUX_EEXIST;
        }
        break;
    }

    case 262: { // sys_newfstatat(dfd, filename, statbuf, flag)
        int dfd = static_cast<int>(a1);
        const char *filename = reinterpret_cast<const char *>(a2);
        auto *sb = reinterpret_cast<LinuxStat64 *>(a3);
        int flag = static_cast<int>(a4);

        if (!sb) {
            ret = LINUX_EFAULT;
            break;
        }

        if ((!filename || filename[0] == '\0') && (flag & 0x1000)) { // AT_EMPTY_PATH
            ret = Dispatch(5, dfd, a3, 0, 0, 0, 0);
            break;
        }

        if (!filename) {
            ret = LINUX_EFAULT;
            break;
        }

        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }

        std::string dirfd_path;
        if (dfd >= 0 && fd_table_) {
            auto dfd_desc = fd_table_->Get(dfd);
            if (dfd_desc)
                dirfd_path = dfd_desc->guest_path;
        }

        auto resolved = vfs_->ResolveAt(dfd, filename, dirfd_path);
        std::memset(sb, 0, sizeof(LinuxStat64));

        if (resolved.dev_type == fs::DeviceType::SyntheticProc) {
            std::string data = fs::ProcFs::Generate(filename, address_space_);
            sb->st_mode = LINUX_S_IFREG | 0444;
            sb->st_size = static_cast<int64_t>(data.size());
            sb->st_nlink = 1;
            sb->st_blksize = 4096;
            sb->st_blocks = (sb->st_size + 511) / 512;
            ret = 0;
            break;
        }

        if (resolved.dev_type == fs::DeviceType::Directory ||
            resolved.dev_type == fs::DeviceType::InputDir ||
            resolved.dev_type == fs::DeviceType::PropAreaDir ||
            resolved.dev_type == fs::DeviceType::PropSocketDir) {
            sb->st_mode = LINUX_S_IFDIR | 0755;
            sb->st_size = 4096;
            sb->st_nlink = 2;
            sb->st_blksize = 4096;
            sb->st_blocks = 8;
            ret = 0;
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropSerial) {
            sb->st_mode = LINUX_S_IFREG | 0444;
            sb->st_size =
                static_cast<int64_t>(property::PropertyManager::Instance().GetSerialSize());
            sb->st_nlink = 1;
            sb->st_blksize = 4096;
            sb->st_blocks = 8;
            ret = 0;
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropContext ||
            resolved.dev_type == fs::DeviceType::PropLegacy) {
            sb->st_mode = LINUX_S_IFREG | 0444;
            sb->st_size =
                static_cast<int64_t>(property::PropertyManager::Instance().GetPropertyAreaSize());
            sb->st_nlink = 1;
            sb->st_blksize = 4096;
            sb->st_blocks =
                (property::PropertyManager::Instance().GetPropertyAreaSize() + 511) / 512;
            ret = 0;
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropInfo) {
            sb->st_mode = LINUX_S_IFREG | 0444;
            sb->st_size = 0;
            sb->st_nlink = 1;
            ret = 0;
            break;
        }

        if (resolved.dev_type == fs::DeviceType::PropServiceSocket) {
            sb->st_mode = 0140666; // S_IFSOCK | 0666
            sb->st_nlink = 1;
            ret = 0;
            break;
        }

        if (resolved.dev_type != fs::DeviceType::Regular) {
            sb->st_mode = LINUX_S_IFCHR | 0666;
            sb->st_nlink = 1;
            ret = 0;
            break;
        }

        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (GetFileAttributesExW(resolved.host_path.wstring().c_str(), GetFileExInfoStandard,
                                 &attr)) {
            LARGE_INTEGER size;
            size.LowPart = attr.nFileSizeLow;
            size.HighPart = static_cast<LONG>(attr.nFileSizeHigh);

            sb->st_mode = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                              ? (LINUX_S_IFDIR | 0755)
                              : (LINUX_S_IFREG | 0644);
            sb->st_size = size.QuadPart;
            sb->st_nlink = 1;
            sb->st_blksize = 4096;
            sb->st_blocks = (sb->st_size + 511) / 512;
            ret = 0;
        } else {
            ret = LINUX_ENOENT;
        }
        break;
    }

    case 263: { // sys_unlinkat(dfd, pathname, flags)
        int dfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        if (!pathname) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string dirfd_path;
        if (dfd >= 0 && fd_table_) {
            auto dfd_desc = fd_table_->Get(dfd);
            if (dfd_desc)
                dirfd_path = dfd_desc->guest_path;
        }
        auto resolved = vfs_->ResolveAt(dfd, pathname, dirfd_path);
        if (!std::filesystem::exists(resolved.host_path)) {
            ret = LINUX_ENOENT;
            break;
        }
        std::error_code ec;
        if (std::filesystem::remove(resolved.host_path, ec)) {
            ret = 0;
        } else {
            ret = LINUX_EIO;
        }
        break;
    }

    case 267: { // sys_readlinkat(dfd, pathname, buf, bufsiz)
        const char *pathname = reinterpret_cast<const char *>(a2);
        char *buf = reinterpret_cast<char *>(a3);
        size_t bufsiz = static_cast<size_t>(a4);

        if (!pathname || !buf) {
            ret = LINUX_EFAULT;
            break;
        }

        if (std::strcmp(pathname, "/proc/self/exe") == 0 && vfs_) {
            std::string exe_str = vfs_->GetExecutablePath().string();
            size_t copy_len = std::min(bufsiz, exe_str.size());
            std::memcpy(buf, exe_str.data(), copy_len);
            ret = static_cast<int64_t>(copy_len);
        } else {
            ret = LINUX_EINVAL;
        }
        break;
    }

    case 269: { // sys_faccessat(dfd, filename, mode, flag)
        int dfd = static_cast<int>(a1);
        const char *filename = reinterpret_cast<const char *>(a2);
        if (!filename) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string dirfd_path;
        if (dfd >= 0 && fd_table_) {
            auto dfd_desc = fd_table_->Get(dfd);
            if (dfd_desc)
                dirfd_path = dfd_desc->guest_path;
        }
        auto resolved = vfs_->ResolveAt(dfd, filename, dirfd_path);
        if (resolved.dev_type != fs::DeviceType::Regular &&
            resolved.dev_type != fs::DeviceType::Directory &&
            resolved.dev_type != fs::DeviceType::None) {
            ret = 0;
            break;
        }
        if (std::filesystem::exists(resolved.host_path)) {
            ret = 0;
        } else {
            ret = LINUX_ENOENT;
        }
        break;
    }

    case 271: { // sys_ppoll(fds, nfds, tmo_p, sigmask, sigsetsize)
        auto *fds = reinterpret_cast<poll::LinuxPollFd *>(a1);
        size_t nfds = static_cast<size_t>(a2);
        const auto *tmo = reinterpret_cast<const sync::LinuxTimespec *>(a3);
        int timeout_ms = -1;
        if (tmo) {
            timeout_ms = static_cast<int>(tmo->tv_sec * 1000 + tmo->tv_nsec / 1000000);
        }
        ret = poll::EpollManager::Instance().Poll(fds, nfds, timeout_ms, fd_table_);
        break;
    }

    case 281: { // sys_epoll_pwait(epfd, events, maxevents, timeout, sigmask, sigsetsize)
        int epfd = static_cast<int>(a1);
        auto *events = reinterpret_cast<poll::LinuxEpollEvent *>(a2);
        int maxevents = static_cast<int>(a3);
        int timeout = static_cast<int>(a4);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(epfd);
        if (!desc || desc->dev != fs::DeviceType::Epoll) {
            ret = LINUX_EBADF;
            break;
        }
        ret = poll::EpollManager::Instance().Wait(desc->epoll_id, events, maxevents, timeout,
                                                  fd_table_);
        break;
    }

    case 288: { // sys_accept4(fd, addr, addrlen, flags)
        int fd = static_cast<int>(a1);
        auto *addr = reinterpret_cast<net::LinuxSockaddr *>(a2);
        auto *len = reinterpret_cast<socklen_t *>(a3);
        int flags = static_cast<int>(a4);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        SOCKET client_s = net::SocketManager::Instance().Accept(
            reinterpret_cast<SOCKET>(desc->handle), addr, len, flags);
        if (client_s == INVALID_SOCKET) {
            ret = net::SocketManager::TranslateWsaError(WSAGetLastError());
        } else {
            ret = fd_table_->AllocateSocket(client_s, flags);
        }
        break;
    }

    case 291: { // sys_epoll_create1(flags)
        int flags = static_cast<int>(a1);
        int epoll_id = poll::EpollManager::Instance().CreateInstance();
        if (epoll_id < 0) {
            ret = epoll_id;
        } else {
            ret = fd_table_ ? fd_table_->AllocateEpoll(epoll_id, flags) : LINUX_ENOSYS;
        }
        break;
    }

    case 292: { // sys_dup3(oldfd, newfd, flags)
        int oldfd = static_cast<int>(a1);
        int newfd = static_cast<int>(a2);
        int flags = static_cast<int>(a3);
        if (oldfd == newfd) {
            ret = LINUX_EINVAL;
            break;
        }
        ret = fd_table_ ? fd_table_->Duplicate(oldfd, newfd, flags) : LINUX_EBADF;
        break;
    }

    case 253:   // sys_inotify_init
    case 294: { // sys_inotify_init1(flags)
        int flags = (nr == 294) ? static_cast<int>(a1) : 0;
        ret = fd_table_ ? fd_table_->AllocateInotify(flags) : LINUX_ENOSYS;
        break;
    }

    case 254: { // sys_inotify_add_watch(fd, pathname, mask)
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(static_cast<int>(a1));
        if (!desc || desc->dev != fs::DeviceType::Inotify) {
            ret = LINUX_EBADF;
            break;
        }
        ret = 1; // Default watch descriptor 1
        break;
    }

    case 255: { // sys_inotify_rm_watch(fd, wd)
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(static_cast<int>(a1));
        if (!desc || desc->dev != fs::DeviceType::Inotify) {
            ret = LINUX_EBADF;
            break;
        }
        ret = 0;
        break;
    }

    case 293: { // sys_pipe2(pipefd, flags)
        int *pipefd = reinterpret_cast<int *>(a1);
        int flags = static_cast<int>(a2);
        ret = fd_table_ ? fd_table_->CreatePipe(pipefd, flags) : LINUX_ENOSYS;
        break;
    }

    case 302: { // sys_prlimit64(pid, resource, new_limit, old_limit)
        int resource = static_cast<int>(a2);
        auto *old_limit = reinterpret_cast<LinuxRlimit64 *>(a4);

        if (old_limit) {
            if (resource == 3) { // RLIMIT_STACK
                old_limit->rlim_cur = 8 * 1024 * 1024;
                old_limit->rlim_max = 8 * 1024 * 1024;
            } else if (resource == 7) { // RLIMIT_NOFILE
                old_limit->rlim_cur = 1024;
                old_limit->rlim_max = 4096;
            } else {
                old_limit->rlim_cur = 0x7FFFFFFFFFFFFFFFULL;
                old_limit->rlim_max = 0x7FFFFFFFFFFFFFFFULL;
            }
        }
        ret = 0;
        break;
    }

    default:
        if (verbose_) {
            std::cerr << "[portage] Unimplemented syscall: nr=" << nr << " (" << a1 << ", " << a2
                      << ", " << a3 << ", " << a4 << ")\n";
        }
        ret = LINUX_ENOSYS;
        break;
    }

    // Always restore the active TCB for guest before returning
    arch::RestoreActiveTcb();
    return ret;
}

} // namespace portage::syscall

extern "C" __attribute__((sysv_abi)) int64_t portage_syscall_dispatch(uint64_t nr, uint64_t a1,
                                                                      uint64_t a2, uint64_t a3,
                                                                      uint64_t a4, uint64_t a5,
                                                                      uint64_t a6) noexcept
{
    return portage::syscall::SyscallDispatcher::Dispatch(nr, a1, a2, a3, a4, a5, a6);
}
