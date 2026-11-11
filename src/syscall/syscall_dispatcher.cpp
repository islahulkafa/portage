#include "syscall_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "mem/shared_memory.hpp"
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
#include <bcrypt.h>
#include <chrono>
#include <cstring>
#include <immintrin.h>
#include <iostream>
#include <psapi.h>
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

static int64_t DoSelect(int nfds, LinuxFdSet *readfds, LinuxFdSet *writefds, LinuxFdSet *exceptfds,
                        int timeout_ms, fs::FileDescriptorTable *fd_table)
{
    if (nfds < 0 || nfds > static_cast<int>(LINUX_FD_SETSIZE)) {
        return LINUX_EINVAL;
    }
    if (nfds == 0 && timeout_ms >= 0) {
        Sleep(static_cast<DWORD>(timeout_ms));
        return 0;
    }

    std::vector<poll::LinuxPollFd> poll_fds;
    struct FdWatch {
        int fd;
        bool in_read{false};
        bool in_write{false};
        bool in_except{false};
    };
    std::vector<FdWatch> watched;

    auto test_bit = [](const LinuxFdSet *set, int fd) -> bool {
        if (!set)
            return false;
        size_t idx = static_cast<size_t>(fd) / 64;
        size_t bit = static_cast<size_t>(fd) % 64;
        return (set->fds_bits[idx] & (1ULL << bit)) != 0;
    };

    for (int fd = 0; fd < nfds; ++fd) {
        bool r = test_bit(readfds, fd);
        bool w = test_bit(writefds, fd);
        bool e = test_bit(exceptfds, fd);
        if (r || w || e) {
            int16_t events = 0;
            if (r)
                events |= poll::LINUX_POLLIN | poll::LINUX_POLLPRI;
            if (w)
                events |= poll::LINUX_POLLOUT;
            if (e)
                events |= poll::LINUX_POLLPRI;
            poll_fds.push_back(poll::LinuxPollFd{.fd = fd, .events = events, .revents = 0});
            watched.push_back(FdWatch{.fd = fd, .in_read = r, .in_write = w, .in_except = e});
        }
    }

    if (readfds)
        std::memset(readfds, 0, sizeof(LinuxFdSet));
    if (writefds)
        std::memset(writefds, 0, sizeof(LinuxFdSet));
    if (exceptfds)
        std::memset(exceptfds, 0, sizeof(LinuxFdSet));

    if (watched.empty()) {
        if (timeout_ms > 0) {
            Sleep(static_cast<DWORD>(timeout_ms));
        }
        return 0;
    }

    int poll_res =
        poll::EpollManager::Instance().Poll(poll_fds.data(), poll_fds.size(), timeout_ms, fd_table);
    if (poll_res < 0) {
        return poll_res;
    }

    auto set_bit = [](LinuxFdSet *set, int fd) {
        if (!set)
            return;
        size_t idx = static_cast<size_t>(fd) / 64;
        size_t bit = static_cast<size_t>(fd) % 64;
        set->fds_bits[idx] |= (1ULL << bit);
    };

    int total_ready = 0;
    for (size_t i = 0; i < poll_fds.size(); ++i) {
        const auto &pfd = poll_fds[i];
        const auto &w = watched[i];
        bool ready_r =
            w.in_read &&
            (pfd.revents & (poll::LINUX_POLLIN | poll::LINUX_POLLHUP | poll::LINUX_POLLERR));
        bool ready_w = w.in_write && (pfd.revents & (poll::LINUX_POLLOUT | poll::LINUX_POLLERR));
        bool ready_e = w.in_except && (pfd.revents & (poll::LINUX_POLLPRI | poll::LINUX_POLLERR));

        if (ready_r) {
            set_bit(readfds, w.fd);
            total_ready++;
        }
        if (ready_w) {
            set_bit(writefds, w.fd);
            total_ready++;
        }
        if (ready_e) {
            set_bit(exceptfds, w.fd);
            total_ready++;
        }
    }

    return total_ready;
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

            if (desc->dev == fs::DeviceType::EventFd) {
                if (count < sizeof(uint64_t)) {
                    ret = LINUX_EINVAL;
                    break;
                }
                if (!desc->eventfd) {
                    ret = LINUX_EBADF;
                    break;
                }
                std::unique_lock<std::mutex> lock(desc->eventfd->mutex);
                if (desc->eventfd->counter == 0) {
                    if (desc->flags & LINUX_O_NONBLOCK) {
                        ret = LINUX_EAGAIN;
                        break;
                    }
                    desc->eventfd->cv.wait(lock, [&] { return desc->eventfd->counter > 0; });
                }
                uint64_t val = 0;
                if (desc->eventfd->is_semaphore) {
                    val = 1;
                    desc->eventfd->counter -= 1;
                } else {
                    val = desc->eventfd->counter;
                    desc->eventfd->counter = 0;
                }
                desc->eventfd->cv.notify_all();
                std::memcpy(buf, &val, sizeof(uint64_t));
                ret = static_cast<int64_t>(sizeof(uint64_t));
                break;
            }

            if (desc->dev == fs::DeviceType::TimerFd) {
                if (count < sizeof(uint64_t)) {
                    ret = LINUX_EINVAL;
                    break;
                }
                if (!desc->timerfd) {
                    ret = LINUX_EBADF;
                    break;
                }
                bool nonblock = (desc->flags & LINUX_O_NONBLOCK) != 0;
                uint64_t expirations = 0;
                int64_t read_res = desc->timerfd->ReadExpirations(nonblock, expirations);
                if (read_res < 0) {
                    ret = read_res;
                } else {
                    std::memcpy(buf, &expirations, sizeof(uint64_t));
                    ret = static_cast<int64_t>(sizeof(uint64_t));
                }
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

            if (desc->dev == fs::DeviceType::InputEvent0 ||
                desc->dev == fs::DeviceType::InputEvent1) {
                int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
                size_t num_events = count / sizeof(input::LinuxInputEvent);
                ret = input::InputManager::Instance().WriteEvents(
                    dev_idx, reinterpret_cast<const input::LinuxInputEvent *>(buf), num_events);
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

            if (desc->dev == fs::DeviceType::EventFd) {
                if (count < sizeof(uint64_t)) {
                    ret = LINUX_EINVAL;
                    break;
                }
                if (!desc->eventfd) {
                    ret = LINUX_EBADF;
                    break;
                }
                uint64_t val = 0;
                std::memcpy(&val, buf, sizeof(uint64_t));
                if (val == 0xFFFFFFFFFFFFFFFFULL) {
                    ret = LINUX_EINVAL;
                    break;
                }
                std::unique_lock<std::mutex> lock(desc->eventfd->mutex);
                if (0xFFFFFFFFFFFFFFFFULL - 1 - desc->eventfd->counter < val) {
                    if (desc->flags & LINUX_O_NONBLOCK) {
                        ret = LINUX_EAGAIN;
                        break;
                    }
                    desc->eventfd->cv.wait(lock, [&] {
                        return 0xFFFFFFFFFFFFFFFFULL - 1 - desc->eventfd->counter >= val;
                    });
                }
                desc->eventfd->counter += val;
                desc->eventfd->cv.notify_all();
                ret = static_cast<int64_t>(sizeof(uint64_t));
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

    case 4: { // sys_stat(filename, statbuf)
        ret = Dispatch(262, static_cast<uint64_t>(-100), a1, a2, 0, 0, 0);
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

                auto filetime_to_unix = [](const FILETIME &ft, int64_t &sec, uint64_t &nsec) {
                    ULARGE_INTEGER ull;
                    ull.LowPart = ft.dwLowDateTime;
                    ull.HighPart = ft.dwHighDateTime;
                    constexpr uint64_t kEpochOffset = 116444736000000000ULL;
                    if (ull.QuadPart >= kEpochOffset) {
                        uint64_t diff = ull.QuadPart - kEpochOffset;
                        sec = static_cast<int64_t>(diff / 10'000'000ULL);
                        nsec = (diff % 10'000'000ULL) * 100ULL;
                    } else {
                        sec = 0;
                        nsec = 0;
                    }
                };
                filetime_to_unix(info.ftLastAccessTime, sb->st_atime, sb->st_atime_nsec);
                filetime_to_unix(info.ftLastWriteTime, sb->st_mtime, sb->st_mtime_nsec);
                filetime_to_unix(info.ftCreationTime, sb->st_ctime, sb->st_ctime_nsec);

                ret = 0;
            } else {
                ret = LINUX_EBADF;
            }
        } else {
            ret = LINUX_EBADF;
        }
        break;
    }

    case 6: { // sys_lstat(filename, statbuf)
        ret = Dispatch(262, static_cast<uint64_t>(-100), a1, a2, 0x100, 0, 0);
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

            if (desc->shared_memory) {
                if ((prot & ~desc->shared_memory->prot_mask) != 0) {
                    ret = LINUX_EACCES;
                    break;
                }
                void *mapped = mem::SharedMemoryManager::Instance().MapShared(
                    *desc->shared_memory, addr, length, prot, flags, offset);
                if (!mapped) {
                    ret = LINUX_ENOMEM;
                    break;
                }
                if ((prot & 4) != 0) { // PROT_EXEC
                    rewrite::SyscallRewriter::PatchCodeRegion(mapped, length);
                }
                ret = reinterpret_cast<int64_t>(mapped);
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
        void *unmap_addr = reinterpret_cast<void *>(a1);
        if (mem::SharedMemoryManager::Instance().UnmapShared(unmap_addr)) {
            ret = 0;
            break;
        }
        if (address_space_) {
            int res = address_space_->Munmap(unmap_addr, a2);
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
        } else if (desc->dev == fs::DeviceType::Ashmem || desc->shared_memory) {
            auto shm = desc->shared_memory;
            if (!shm) {
                ret = LINUX_EINVAL;
                break;
            }
            switch (cmd) {
            case 0x41007701: { // ASHMEM_SET_NAME
                const char *name = reinterpret_cast<const char *>(arg);
                if (!name) {
                    ret = LINUX_EFAULT;
                    break;
                }
                ret = mem::SharedMemoryManager::Instance().SetRegionName(*shm, name);
                break;
            }
            case 0x81007702: { // ASHMEM_GET_NAME
                char *buf = reinterpret_cast<char *>(arg);
                if (!buf) {
                    ret = LINUX_EFAULT;
                    break;
                }
                std::strncpy(buf, shm->name.c_str(), 256);
                ret = 0;
                break;
            }
            case 0x40087703:   // ASHMEM_SET_SIZE (64-bit)
            case 0x40047703: { // ASHMEM_SET_SIZE (32-bit)
                size_t sz = reinterpret_cast<uintptr_t>(arg);
                ret = mem::SharedMemoryManager::Instance().SetRegionSize(*shm, sz);
                break;
            }
            case 0x00007704: { // ASHMEM_GET_SIZE
                ret = static_cast<int64_t>(shm->size);
                break;
            }
            case 0x40087705:   // ASHMEM_SET_PROT_MASK (64-bit)
            case 0x40047705: { // ASHMEM_SET_PROT_MASK (32-bit)
                int prot = static_cast<int>(reinterpret_cast<uintptr_t>(arg));
                ret = mem::SharedMemoryManager::Instance().SetProtMask(*shm, prot);
                break;
            }
            case 0x00007706: { // ASHMEM_GET_PROT_MASK
                ret = shm->prot_mask;
                break;
            }
            case 0x40087707: { // ASHMEM_PIN
                ret = mem::SharedMemoryManager::Instance().PinRegion(*shm, 0, 0);
                break;
            }
            case 0x40087708: { // ASHMEM_UNPIN
                ret = mem::SharedMemoryManager::Instance().UnpinRegion(*shm, 0, 0);
                break;
            }
            case 0x00007709: { // ASHMEM_GET_PIN_STATUS
                ret = mem::SharedMemoryManager::Instance().GetPinStatus(*shm);
                break;
            }
            case 0x0000770a: { // ASHMEM_PURGE_ALL_CACHES
                ret = 0;
                break;
            }
            default:
                ret = LINUX_EINVAL;
                break;
            }
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

    case 19: { // sys_readv(fd, iov, iovcnt)
        int fd = static_cast<int>(a1);
        const auto *iov = reinterpret_cast<const LinuxIovec *>(a2);
        int iovcnt = static_cast<int>(a3);

        if (!iov || iovcnt < 0) {
            ret = LINUX_EINVAL;
            break;
        }

        int64_t total_read = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len == 0)
                continue;
            if (!iov[i].iov_base) {
                if (total_read > 0) {
                    ret = total_read;
                } else {
                    ret = LINUX_EFAULT;
                }
                break;
            }
            int64_t r = Dispatch(0, fd, reinterpret_cast<uint64_t>(iov[i].iov_base), iov[i].iov_len,
                                 0, 0, 0);
            if (r < 0) {
                if (total_read > 0) {
                    ret = total_read;
                } else {
                    ret = r;
                }
                break;
            }
            total_read += r;
            if (static_cast<size_t>(r) < iov[i].iov_len)
                break;
        }
        ret = total_read;
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

    case 22: { // sys_pipe(pipefd)
        ret = Dispatch(293, a1, 0, 0, 0, 0, 0);
        break;
    }

    case 23: { // sys_select(nfds, readfds, writefds, exceptfds, timeout)
        int nfds = static_cast<int>(a1);
        auto *readfds = reinterpret_cast<LinuxFdSet *>(a2);
        auto *writefds = reinterpret_cast<LinuxFdSet *>(a3);
        auto *exceptfds = reinterpret_cast<LinuxFdSet *>(a4);
        auto *timeout = reinterpret_cast<LinuxTimeval *>(a5);

        int timeout_ms = -1;
        auto start = std::chrono::steady_clock::now();
        if (timeout) {
            if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) {
                ret = LINUX_EINVAL;
                break;
            }
            uint64_t ms = static_cast<uint64_t>(timeout->tv_sec) * 1000 +
                          (static_cast<uint64_t>(timeout->tv_usec) + 999) / 1000;
            timeout_ms = (ms > MAXDWORD) ? static_cast<int>(MAXDWORD) : static_cast<int>(ms);
        }

        ret = DoSelect(nfds, readfds, writefds, exceptfds, timeout_ms, fd_table_);
        if (timeout && ret >= 0) {
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
            int64_t orig_us = timeout->tv_sec * 1000000LL + timeout->tv_usec;
            int64_t rem_us = (orig_us > elapsed_us) ? (orig_us - elapsed_us) : 0;
            timeout->tv_sec = rem_us / 1000000LL;
            timeout->tv_usec = rem_us % 1000000LL;
        }
        break;
    }

    case 24: { // sys_sched_yield()
        SwitchToThread();
        ret = 0;
        break;
    }

    case 25: { // sys_mremap(old_address, old_size, new_size, flags, new_address)
        void *old_address = reinterpret_cast<void *>(a1);
        size_t old_size = static_cast<size_t>(a2);
        size_t new_size = static_cast<size_t>(a3);
        int flags = static_cast<int>(a4);
        void *new_address = reinterpret_cast<void *>(a5);

        if (!old_address || old_size == 0 || new_size == 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (reinterpret_cast<uintptr_t>(old_address) % mem::PAGE_SIZE_4KB != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        if (flags & LINUX_MREMAP_FIXED) {
            if ((flags & LINUX_MREMAP_MAYMOVE) == 0) {
                ret = LINUX_EINVAL;
                break;
            }
            if (!new_address ||
                (reinterpret_cast<uintptr_t>(new_address) % mem::PAGE_SIZE_4KB != 0)) {
                ret = LINUX_EINVAL;
                break;
            }
        }

        if (new_size <= old_size && (flags & (LINUX_MREMAP_FIXED | LINUX_MREMAP_DONTUNMAP)) == 0) {
            size_t old_aligned = (old_size + mem::PAGE_SIZE_4KB - 1) & ~(mem::PAGE_SIZE_4KB - 1);
            size_t new_aligned = (new_size + mem::PAGE_SIZE_4KB - 1) & ~(mem::PAGE_SIZE_4KB - 1);
            if (old_aligned > new_aligned) {
                uintptr_t free_start = reinterpret_cast<uintptr_t>(old_address) + new_aligned;
                VirtualFree(reinterpret_cast<void *>(free_start), old_aligned - new_aligned,
                            MEM_DECOMMIT);
            }
            ret = reinterpret_cast<int64_t>(old_address);
            break;
        }

        if ((flags & LINUX_MREMAP_MAYMOVE) == 0) {
            ret = LINUX_ENOMEM;
            break;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        DWORD old_prot = PAGE_READWRITE;
        if (VirtualQuery(old_address, &mbi, sizeof(mbi)) != 0 && mbi.Protect != 0) {
            old_prot = mbi.Protect;
        }

        size_t new_aligned = (new_size + mem::PAGE_SIZE_4KB - 1) & ~(mem::PAGE_SIZE_4KB - 1);
        void *target_addr = (flags & LINUX_MREMAP_FIXED) ? new_address : nullptr;
        void *new_addr =
            VirtualAlloc(target_addr, new_aligned, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!new_addr) {
            ret = LINUX_ENOMEM;
            break;
        }

        DWORD prev_prot = 0;
        bool restore_src = false;
        if ((old_prot &
             (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
            VirtualProtect(old_address, old_size, PAGE_READONLY, &prev_prot);
            restore_src = true;
        }

        std::memcpy(new_addr, old_address, std::min(old_size, new_size));

        if (restore_src) {
            VirtualProtect(old_address, old_size, prev_prot, &prev_prot);
        }
        if (old_prot != PAGE_READWRITE) {
            VirtualProtect(new_addr, new_aligned, old_prot, &prev_prot);
        }

        if (address_space_) {
            address_space_->AddReservation(new_addr);
        }

        if ((flags & LINUX_MREMAP_DONTUNMAP) == 0) {
            size_t old_aligned = (old_size + mem::PAGE_SIZE_4KB - 1) & ~(mem::PAGE_SIZE_4KB - 1);
            VirtualFree(old_address, old_aligned, MEM_DECOMMIT);
        }

        ret = reinterpret_cast<int64_t>(new_addr);
        break;
    }

    case 27: { // sys_mincore(start, length, vec)
        void *start = reinterpret_cast<void *>(a1);
        size_t length = static_cast<size_t>(a2);
        auto *vec = reinterpret_cast<unsigned char *>(a3);

        if (!start || !vec || length == 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (reinterpret_cast<uintptr_t>(start) % mem::PAGE_SIZE_4KB != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        size_t num_pages = (length + mem::PAGE_SIZE_4KB - 1) / mem::PAGE_SIZE_4KB;
        bool valid = true;
        for (size_t i = 0; i < num_pages; ++i) {
            void *p = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(start) +
                                               i * mem::PAGE_SIZE_4KB);
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State == MEM_FREE) {
                valid = false;
                break;
            }
            vec[i] = (mbi.State == MEM_COMMIT) ? 1 : 0;
        }

        ret = valid ? 0 : LINUX_ENOMEM;
        break;
    }

    case 28: { // sys_madvise(addr, length, advice)
        ret = 0;
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

    case 40: { // sys_sendfile(out_fd, in_fd, offset, count)
        int out_fd = static_cast<int>(a1);
        int in_fd = static_cast<int>(a2);
        auto *offset_ptr = reinterpret_cast<int64_t *>(a3);
        size_t count = static_cast<size_t>(a4);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto in_desc = fd_table_->Get(in_fd);
        auto out_desc = fd_table_->Get(out_fd);
        if (!in_desc || !out_desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (count == 0) {
            ret = 0;
            break;
        }

        std::vector<uint8_t> buffer(std::min<size_t>(count, 65536));
        size_t total_transferred = 0;

        while (total_transferred < count) {
            size_t to_read = std::min(buffer.size(), count - total_transferred);
            int64_t nread = 0;

            if (offset_ptr) {
                if (in_desc->handle != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER orig_pos{};
                    LARGE_INTEGER zero{};
                    SetFilePointerEx(in_desc->handle, zero, &orig_pos, FILE_CURRENT);

                    LARGE_INTEGER target_pos{};
                    target_pos.QuadPart = *offset_ptr;
                    SetFilePointerEx(in_desc->handle, target_pos, nullptr, FILE_BEGIN);

                    DWORD dw_read = 0;
                    if (ReadFile(in_desc->handle, buffer.data(), static_cast<DWORD>(to_read),
                                 &dw_read, nullptr)) {
                        nread = dw_read;
                        *offset_ptr += dw_read;
                    } else {
                        nread = -1;
                    }
                    SetFilePointerEx(in_desc->handle, orig_pos, nullptr, FILE_BEGIN);
                } else {
                    nread = Dispatch(0, in_fd, reinterpret_cast<uint64_t>(buffer.data()), to_read,
                                     0, 0, 0);
                    if (nread > 0) {
                        *offset_ptr += nread;
                    }
                }
            } else {
                nread =
                    Dispatch(0, in_fd, reinterpret_cast<uint64_t>(buffer.data()), to_read, 0, 0, 0);
            }

            if (nread <= 0) {
                break;
            }

            int64_t nwritten = Dispatch(1, out_fd, reinterpret_cast<uint64_t>(buffer.data()),
                                        static_cast<size_t>(nread), 0, 0, 0);
            if (nwritten <= 0) {
                break;
            }
            total_transferred += static_cast<size_t>(nwritten);
            if (nwritten < nread) {
                break;
            }
        }

        ret = static_cast<int64_t>(total_transferred);
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

    case 46: { // sys_sendmsg(fd, msg, flags)
        int fd = static_cast<int>(a1);
        const auto *msg = reinterpret_cast<const LinuxMsghdr *>(a2);
        int flags = static_cast<int>(a3);
        if (!msg) {
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
        if (desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        if (!msg->msg_iov && msg->msg_iovlen > 0) {
            ret = LINUX_EFAULT;
            break;
        }
        const auto *dest_addr = reinterpret_cast<const net::LinuxSockaddr *>(msg->msg_name);
        socklen_t addrlen = static_cast<socklen_t>(msg->msg_namelen);
        int64_t total_sent = 0;
        for (size_t i = 0; i < msg->msg_iovlen; ++i) {
            if (msg->msg_iov[i].iov_len == 0)
                continue;
            if (!msg->msg_iov[i].iov_base) {
                if (total_sent > 0) {
                    ret = total_sent;
                } else {
                    ret = LINUX_EFAULT;
                }
                break;
            }
            int64_t n = net::SocketManager::Instance().SendTo(
                reinterpret_cast<SOCKET>(desc->handle), msg->msg_iov[i].iov_base,
                msg->msg_iov[i].iov_len, flags, dest_addr, addrlen);
            if (n < 0) {
                if (total_sent > 0) {
                    ret = total_sent;
                } else {
                    ret = n;
                }
                break;
            }
            total_sent += n;
            if (static_cast<size_t>(n) < msg->msg_iov[i].iov_len)
                break;
        }
        ret = total_sent;
        break;
    }

    case 47: { // sys_recvmsg(fd, msg, flags)
        int fd = static_cast<int>(a1);
        auto *msg = reinterpret_cast<LinuxMsghdr *>(a2);
        int flags = static_cast<int>(a3);
        if (!msg) {
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
        if (desc->dev != fs::DeviceType::Socket) {
            ret = LINUX_ENOTSOCK;
            break;
        }
        if (!msg->msg_iov && msg->msg_iovlen > 0) {
            ret = LINUX_EFAULT;
            break;
        }
        auto *src_addr = reinterpret_cast<net::LinuxSockaddr *>(msg->msg_name);
        socklen_t addrlen = static_cast<socklen_t>(msg->msg_namelen);
        socklen_t out_addrlen = addrlen;
        int64_t total_received = 0;
        for (size_t i = 0; i < msg->msg_iovlen; ++i) {
            if (msg->msg_iov[i].iov_len == 0)
                continue;
            if (!msg->msg_iov[i].iov_base) {
                if (total_received > 0) {
                    ret = total_received;
                } else {
                    ret = LINUX_EFAULT;
                }
                break;
            }
            int64_t n = net::SocketManager::Instance().RecvFrom(
                reinterpret_cast<SOCKET>(desc->handle), msg->msg_iov[i].iov_base,
                msg->msg_iov[i].iov_len, flags, src_addr, src_addr ? &out_addrlen : nullptr);
            if (n < 0) {
                if (total_received > 0) {
                    ret = total_received;
                } else {
                    ret = n;
                }
                break;
            }
            total_received += n;
            if (static_cast<size_t>(n) < msg->msg_iov[i].iov_len)
                break;
        }
        if (src_addr) {
            msg->msg_namelen = out_addrlen;
        }
        msg->msg_controllen = 0;
        msg->msg_flags = 0;
        ret = total_received;
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

    case 53: { // sys_socketpair(domain, type, protocol, sv)
        int domain = static_cast<int>(a1);
        int type = static_cast<int>(a2);
        int protocol = static_cast<int>(a3);
        auto *sv = reinterpret_cast<int *>(a4);

        if (!sv) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }

        SOCKET socks[2]{INVALID_SOCKET, INVALID_SOCKET};
        int out_flags = 0;
        int64_t r =
            net::SocketManager::Instance().SocketPair(domain, type, protocol, socks, out_flags);
        if (r < 0) {
            ret = r;
            break;
        }

        int fd0 = fd_table_->AllocateSocket(static_cast<UINT_PTR>(socks[0]), out_flags);
        if (fd0 < 0) {
            ::closesocket(socks[0]);
            ::closesocket(socks[1]);
            ret = fd0;
            break;
        }
        int fd1 = fd_table_->AllocateSocket(static_cast<UINT_PTR>(socks[1]), out_flags);
        if (fd1 < 0) {
            fd_table_->Close(fd0);
            ::closesocket(socks[1]);
            ret = fd1;
            break;
        }

        sv[0] = fd0;
        sv[1] = fd1;
        ret = 0;
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
        case 1033: { // F_ADD_SEALS
            if (!desc->shared_memory) {
                ret = LINUX_EINVAL;
                break;
            }
            int new_seals = static_cast<int>(arg);
            if (desc->shared_memory->seals & 1 /* F_SEAL_SEAL */) {
                ret = LINUX_EPERM;
                break;
            }
            desc->shared_memory->seals |= new_seals;
            if (desc->shared_memory->seals &
                (8 /* F_SEAL_WRITE */ | 16 /* F_SEAL_FUTURE_WRITE */)) {
                desc->shared_memory->prot_mask &= ~2; // remove PROT_WRITE
            }
            ret = 0;
            break;
        }
        case 1034: { // F_GET_SEALS
            if (!desc->shared_memory) {
                ret = LINUX_EINVAL;
                break;
            }
            ret = desc->shared_memory->seals;
            break;
        }
        default:
            ret = LINUX_EINVAL;
            break;
        }
        break;
    }

    case 73: { // sys_flock(fd, operation)
        ret = 0;
        break;
    }

    case 74:   // sys_fsync(fd)
    case 75: { // sys_fdatasync(fd)
        int fd = static_cast<int>(a1);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev == fs::DeviceType::Regular && desc->handle != INVALID_HANDLE_VALUE &&
            !desc->is_pipe_or_console) {
            if (!FlushFileBuffers(desc->handle)) {
                ret = LINUX_EINVAL;
                break;
            }
        }
        ret = 0;
        break;
    }

    case 76: { // sys_truncate(path, length)
        const char *path = reinterpret_cast<const char *>(a1);
        int64_t length = static_cast<int64_t>(a2);
        if (!path) {
            ret = LINUX_EFAULT;
            break;
        }
        if (length < 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        auto resolved = vfs_->Resolve(path);
        std::error_code ec;
        if (!std::filesystem::exists(resolved.host_path, ec)) {
            ret = LINUX_ENOENT;
            break;
        }
        std::filesystem::resize_file(resolved.host_path, static_cast<uintmax_t>(length), ec);
        ret = ec ? LINUX_EINVAL : 0;
        break;
    }

    case 77: { // sys_ftruncate(fd, length)
        int fd = static_cast<int>(a1);
        int64_t length = static_cast<int64_t>(a2);
        if (length < 0) {
            ret = LINUX_EINVAL;
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
        if (desc->shared_memory) {
            ret = mem::SharedMemoryManager::Instance().SetRegionSize(*desc->shared_memory,
                                                                     static_cast<size_t>(length));
            break;
        }
        if (desc->handle != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER li;
            li.QuadPart = length;
            if (SetFilePointerEx(desc->handle, li, nullptr, FILE_BEGIN) &&
                SetEndOfFile(desc->handle)) {
                ret = 0;
            } else {
                ret = LINUX_EINVAL;
            }
            break;
        }
        ret = LINUX_EINVAL;
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

    case 82: { // sys_rename(oldpath, newpath)
        ret = Dispatch(264, static_cast<uint64_t>(LINUX_AT_FDCWD), a1,
                       static_cast<uint64_t>(LINUX_AT_FDCWD), a2, 0, 0);
        break;
    }

    case 89: { // sys_readlink(path, buf, bufsiz)
        ret = Dispatch(267, static_cast<uint64_t>(LINUX_AT_FDCWD), a1, a2, a3, 0, 0);
        break;
    }

    case 91: { // sys_fchmod(fd, mode)
        int fd = static_cast<int>(a1);
        uint32_t mode = static_cast<uint32_t>(a2);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->handle == INVALID_HANDLE_VALUE) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev == fs::DeviceType::Regular) {
            std::vector<wchar_t> path_buf(MAX_PATH);
            DWORD len = GetFinalPathNameByHandleW(desc->handle, path_buf.data(),
                                                  static_cast<DWORD>(path_buf.size()),
                                                  FILE_NAME_NORMALIZED);
            if (len > 0 && len < path_buf.size()) {
                const wchar_t *actual_path = path_buf.data();
                if (wcsncmp(actual_path, L"\\\\?\\", 4) == 0) {
                    actual_path += 4;
                }
                DWORD attrs = GetFileAttributesW(actual_path);
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    if ((mode & 0222) == 0) {
                        attrs |= FILE_ATTRIBUTE_READONLY;
                    } else {
                        attrs &= ~FILE_ATTRIBUTE_READONLY;
                    }
                    SetFileAttributesW(actual_path, attrs);
                }
            } else {
                FILE_BASIC_INFO info{};
                if (GetFileInformationByHandleEx(desc->handle, FileBasicInfo, &info,
                                                 sizeof(info))) {
                    if ((mode & 0222) == 0) {
                        info.FileAttributes |= FILE_ATTRIBUTE_READONLY;
                    } else {
                        info.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
                    }
                    SetFileInformationByHandle(desc->handle, FileBasicInfo, &info, sizeof(info));
                }
            }
        }
        ret = 0;
        break;
    }

    case 93: { // sys_fchown(fd, user, group)
        int fd = static_cast<int>(a1);
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        ret = 0;
        break;
    }

    case 96: { // sys_gettimeofday(tv, tz)
        auto *tv = reinterpret_cast<LinuxTimeval *>(a1);
        auto *tz = reinterpret_cast<LinuxTimezone *>(a2);
        if (tv) {
            auto now = std::chrono::system_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch())
                          .count();
            tv->tv_sec = us / 1000000LL;
            tv->tv_usec = us % 1000000LL;
        }
        if (tz) {
            tz->tz_minuteswest = 0;
            tz->tz_dsttime = 0;
        }
        ret = 0;
        break;
    }

    case 98: { // sys_getrusage(who, usage)
        int who = static_cast<int>(a1);
        auto *usage = reinterpret_cast<LinuxRusage *>(a2);
        if (!usage) {
            ret = LINUX_EFAULT;
            break;
        }
        std::memset(usage, 0, sizeof(LinuxRusage));

        FILETIME creation_time{}, exit_time{}, kernel_time{}, user_time{};
        HANDLE target = (who == LINUX_RUSAGE_THREAD) ? GetCurrentThread() : GetCurrentProcess();
        BOOL ok =
            (who == LINUX_RUSAGE_THREAD)
                ? GetThreadTimes(target, &creation_time, &exit_time, &kernel_time, &user_time)
                : GetProcessTimes(target, &creation_time, &exit_time, &kernel_time, &user_time);

        if (ok) {
            ULARGE_INTEGER u_user, u_kern;
            u_user.LowPart = user_time.dwLowDateTime;
            u_user.HighPart = user_time.dwHighDateTime;
            u_kern.LowPart = kernel_time.dwLowDateTime;
            u_kern.HighPart = kernel_time.dwHighDateTime;

            uint64_t user_us = u_user.QuadPart / 10;
            usage->ru_utime.tv_sec = static_cast<int64_t>(user_us / 1000000);
            usage->ru_utime.tv_usec = static_cast<int64_t>(user_us % 1000000);

            uint64_t kern_us = u_kern.QuadPart / 10;
            usage->ru_stime.tv_sec = static_cast<int64_t>(kern_us / 1000000);
            usage->ru_stime.tv_usec = static_cast<int64_t>(kern_us % 1000000);
        }

        PROCESS_MEMORY_COUNTERS pmc{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            usage->ru_maxrss = static_cast<int64_t>(pmc.PeakWorkingSetSize / 1024);
        }

        ret = 0;
        break;
    }

    case 99: { // sys_sysinfo(info)
        auto *info = reinterpret_cast<LinuxSysinfo *>(a1);
        if (!info) {
            ret = LINUX_EFAULT;
            break;
        }
        std::memset(info, 0, sizeof(LinuxSysinfo));
        info->uptime = static_cast<int64_t>(GetTickCount64() / 1000);

        MEMORYSTATUSEX statex{};
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            info->totalram = statex.ullTotalPhys;
            info->freeram = statex.ullAvailPhys;
            info->totalswap = statex.ullTotalPageFile;
            info->freeswap = statex.ullAvailPageFile;
        }
        info->procs = 1;
        info->mem_unit = 1;
        ret = 0;
        break;
    }

    case 102: // sys_getuid
    case 104: // sys_getgid
    case 107: // sys_geteuid
    case 108: // sys_getegid
        ret = 0;
        break;

    case 109: { // sys_setpgid(pid, pgid)
        ret = 0;
        break;
    }

    case 110: { // sys_getppid()
        ret = 1;
        break;
    }

    case 111: { // sys_getpgrp()
        ret = Dispatch(39, 0, 0, 0, 0, 0, 0);
        break;
    }

    case 131: { // sys_sigaltstack(ss, old_ss)
        ret = 0;
        break;
    }

    case 137: { // sys_statfs(path, buf)
        const char *path = reinterpret_cast<const char *>(a1);
        auto *buf = reinterpret_cast<LinuxStatfs *>(a2);
        if (!path || !buf) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        auto resolved = vfs_->Resolve(path);
        std::wstring host_wpath = resolved.host_path.wstring();
        ULARGE_INTEGER free_bytes_avail{}, total_bytes{}, total_free_bytes{};
        if (!GetDiskFreeSpaceExW(host_wpath.c_str(), &free_bytes_avail, &total_bytes,
                                 &total_free_bytes)) {
            std::filesystem::path parent = resolved.host_path.parent_path();
            std::wstring parent_wpath = parent.wstring();
            if (!GetDiskFreeSpaceExW(parent_wpath.c_str(), &free_bytes_avail, &total_bytes,
                                     &total_free_bytes)) {
                GetDiskFreeSpaceExW(L"C:\\", &free_bytes_avail, &total_bytes, &total_free_bytes);
            }
        }

        std::memset(buf, 0, sizeof(LinuxStatfs));
        buf->f_type = LINUX_EXT4_SUPER_MAGIC;
        buf->f_bsize = 4096;
        buf->f_frsize = 4096;
        buf->f_blocks = total_bytes.QuadPart / 4096;
        buf->f_bfree = total_free_bytes.QuadPart / 4096;
        buf->f_bavail = free_bytes_avail.QuadPart / 4096;
        buf->f_files = buf->f_blocks / 4;
        buf->f_ffree = buf->f_bfree / 4;
        buf->f_namelen = 255;
        ret = 0;
        break;
    }

    case 138: { // sys_fstatfs(fd, buf)
        int fd = static_cast<int>(a1);
        auto *buf = reinterpret_cast<LinuxStatfs *>(a2);
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

        ULARGE_INTEGER free_bytes_avail{}, total_bytes{}, total_free_bytes{};
        bool queried = false;
        if (desc->handle != INVALID_HANDLE_VALUE) {
            std::vector<wchar_t> path_buf(MAX_PATH);
            DWORD len = GetFinalPathNameByHandleW(desc->handle, path_buf.data(),
                                                  static_cast<DWORD>(path_buf.size()),
                                                  FILE_NAME_NORMALIZED);
            if (len > 0 && len < path_buf.size()) {
                queried = GetDiskFreeSpaceExW(path_buf.data(), &free_bytes_avail, &total_bytes,
                                              &total_free_bytes);
            }
        }
        if (!queried) {
            GetDiskFreeSpaceExW(L"C:\\", &free_bytes_avail, &total_bytes, &total_free_bytes);
        }

        std::memset(buf, 0, sizeof(LinuxStatfs));
        buf->f_type = LINUX_EXT4_SUPER_MAGIC;
        buf->f_bsize = 4096;
        buf->f_frsize = 4096;
        buf->f_blocks = total_bytes.QuadPart / 4096;
        buf->f_bfree = total_free_bytes.QuadPart / 4096;
        buf->f_bavail = free_bytes_avail.QuadPart / 4096;
        buf->f_files = buf->f_blocks / 4;
        buf->f_ffree = buf->f_bfree / 4;
        buf->f_namelen = 255;
        ret = 0;
        break;
    }

    case 142: { // sys_sched_getparam(pid, param)
        auto *param = reinterpret_cast<LinuxSchedParam *>(a2);
        if (!param) {
            ret = LINUX_EFAULT;
            break;
        }
        param->sched_priority = 0;
        ret = 0;
        break;
    }

    case 144: { // sys_sched_setscheduler(pid, policy, param)
        ret = 0;
        break;
    }

    case 145: { // sys_sched_getscheduler(pid)
        ret = LINUX_SCHED_OTHER;
        break;
    }

    case 149:   // sys_mlock(addr, len)
    case 150:   // sys_munlock(addr, len)
    case 151:   // sys_mlockall(flags)
    case 152:   // sys_munlockall()
    case 325: { // sys_mlock2(addr, len, flags)
        ret = 0;
        break;
    }

    case 157: { // sys_prctl(option, arg2, arg3, arg4, arg5)
        int option = static_cast<int>(a1);
        if (option == LINUX_PR_SET_NAME) {
            const char *name = reinterpret_cast<const char *>(a2);
            if (!name) {
                ret = LINUX_EFAULT;
                break;
            }
            wchar_t wname[16]{};
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 16);
            wname[15] = L'\0';
            SetThreadDescription(GetCurrentThread(), wname);
            auto th = thread::ThreadManager::Instance().GetCurrentGuestThread();
            if (th) {
                th->name = std::string(name, std::min<size_t>(std::strlen(name), 15));
            }
            ret = 0;
        } else if (option == LINUX_PR_GET_NAME) {
            char *name = reinterpret_cast<char *>(a2);
            if (!name) {
                ret = LINUX_EFAULT;
                break;
            }
            auto th = thread::ThreadManager::Instance().GetCurrentGuestThread();
            if (th && !th->name.empty()) {
                std::strncpy(name, th->name.c_str(), 16);
                name[15] = '\0';
            } else {
                PWSTR wname = nullptr;
                if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)) && wname) {
                    WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, 16, nullptr, nullptr);
                    name[15] = '\0';
                    LocalFree(wname);
                } else {
                    name[0] = '\0';
                }
            }
            ret = 0;
        } else {
            ret = 0;
        }
        break;
    }

    case 158: { // sys_arch_prctl(int code, unsigned long addr)
        // ARCH_SET_FS = 0x1002, ARCH_GET_FS = 0x1003
        if (a1 == 0x1002) {
            arch::SetActiveTcb(a2);
            thread::ThreadManager::Instance().SetCurrentThreadTcb(a2);
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

    case 203: { // sys_sched_setaffinity(pid, cpusetsize, mask)
        int pid = static_cast<int>(a1);
        size_t cpusetsize = static_cast<size_t>(a2);
        const auto *mask = reinterpret_cast<const uint64_t *>(a3);
        if (!mask) {
            ret = LINUX_EFAULT;
            break;
        }
        if (cpusetsize < sizeof(uint64_t) || *mask == 0) {
            ret = LINUX_EINVAL;
            break;
        }
        DWORD_PTR proc_mask = 0, sys_mask = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask)) {
            DWORD_PTR desired = static_cast<DWORD_PTR>(*mask) & proc_mask;
            if (desired == 0) {
                ret = LINUX_EINVAL;
                break;
            }
            HANDLE target = GetCurrentThread();
            if (pid != 0) {
                auto th = thread::ThreadManager::Instance().GetThread(pid);
                if (th && th->win32_handle != INVALID_HANDLE_VALUE) {
                    target = th->win32_handle;
                }
            }
            SetThreadAffinityMask(target, desired);
        }
        ret = 0;
        break;
    }

    case 204: { // sys_sched_getaffinity(pid, cpusetsize, mask)
        size_t cpusetsize = static_cast<size_t>(a2);
        auto *mask = reinterpret_cast<uint64_t *>(a3);
        if (!mask) {
            ret = LINUX_EFAULT;
            break;
        }
        if (cpusetsize < sizeof(uint64_t)) {
            ret = LINUX_EINVAL;
            break;
        }
        std::memset(mask, 0, cpusetsize);
        DWORD_PTR proc_mask = 1, sys_mask = 1;
        GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask);
        *mask = static_cast<uint64_t>(proc_mask);
        ret = static_cast<int64_t>(sizeof(uint64_t));
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

        if (resolved.dev_type == fs::DeviceType::Ashmem) {
            auto shm = mem::SharedMemoryManager::Instance().CreateAshmem();
            ret = fd_table_->AllocateSharedMemory(shm, fs::DeviceType::Ashmem, flags, pathname);
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
            std::string target_path = resolved.host_path.string();
            if (target_path.empty()) {
                target_path = pathname;
            }
            std::string data = fs::ProcFs::Generate(target_path, address_space_);
            ret = fd_table_->AllocateSynthetic(flags, resolved.dev_type, pathname, std::move(data));
            break;
        }

        if (resolved.dev_type == fs::DeviceType::Directory || (flags & LINUX_O_DIRECTORY)) {
            std::string proc_target = fs::ProcFs::CanonicalizePath(
                fs::ProcFs::IsProcPath(resolved.host_path.string()) ? resolved.host_path.string()
                                                                    : pathname);
            if (fs::ProcFs::IsProcPath(proc_target)) {
                auto entries = fs::ProcFs::ListDir(proc_target, fd_table_);
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

    case 260: { // sys_fchownat(dfd, pathname, owner, group, flags)
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
        ret = 0;
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
            std::string target_path = resolved.host_path.string();
            if (target_path.empty()) {
                target_path = filename;
            }
            if (target_path.starts_with("/proc/self/fd/")) {
                sb->st_mode = LINUX_S_IFLNK | 0777;
                sb->st_size = 64;
                sb->st_nlink = 1;
                sb->st_blksize = 4096;
                sb->st_blocks = 1;
                ret = 0;
                break;
            }
            std::string data = fs::ProcFs::Generate(target_path, address_space_);
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

            auto filetime_to_unix = [](const FILETIME &ft, int64_t &sec, uint64_t &nsec) {
                ULARGE_INTEGER ull;
                ull.LowPart = ft.dwLowDateTime;
                ull.HighPart = ft.dwHighDateTime;
                constexpr uint64_t kEpochOffset = 116444736000000000ULL;
                if (ull.QuadPart >= kEpochOffset) {
                    uint64_t diff = ull.QuadPart - kEpochOffset;
                    sec = static_cast<int64_t>(diff / 10'000'000ULL);
                    nsec = (diff % 10'000'000ULL) * 100ULL;
                } else {
                    sec = 0;
                    nsec = 0;
                }
            };
            filetime_to_unix(attr.ftLastAccessTime, sb->st_atime, sb->st_atime_nsec);
            filetime_to_unix(attr.ftLastWriteTime, sb->st_mtime, sb->st_mtime_nsec);
            filetime_to_unix(attr.ftCreationTime, sb->st_ctime, sb->st_ctime_nsec);

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

    case 264: { // sys_renameat(olddfd, oldname, newdfd, newname)
        int olddfd = static_cast<int>(a1);
        const char *oldname = reinterpret_cast<const char *>(a2);
        int newdfd = static_cast<int>(a3);
        const char *newname = reinterpret_cast<const char *>(a4);

        if (!oldname || !newname) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string old_dirfd_path;
        if (olddfd >= 0 && fd_table_) {
            auto old_desc = fd_table_->Get(olddfd);
            if (old_desc)
                old_dirfd_path = old_desc->guest_path;
        }
        std::string new_dirfd_path;
        if (newdfd >= 0 && fd_table_) {
            auto new_desc = fd_table_->Get(newdfd);
            if (new_desc)
                new_dirfd_path = new_desc->guest_path;
        }

        auto old_resolved = vfs_->ResolveAt(olddfd, oldname, old_dirfd_path);
        auto new_resolved = vfs_->ResolveAt(newdfd, newname, new_dirfd_path);

        if (old_resolved.host_path.empty() || new_resolved.host_path.empty()) {
            ret = LINUX_ENOENT;
            break;
        }
        if (!std::filesystem::exists(old_resolved.host_path)) {
            ret = LINUX_ENOENT;
            break;
        }

        std::error_code ec;
        std::filesystem::rename(old_resolved.host_path, new_resolved.host_path, ec);
        if (!ec) {
            ret = 0;
        } else {
            ret = LINUX_EIO;
        }
        break;
    }

    case 265: { // sys_linkat(olddfd, oldpath, newdfd, newpath, flags)
        int olddfd = static_cast<int>(a1);
        const char *oldpath = reinterpret_cast<const char *>(a2);
        int newdfd = static_cast<int>(a3);
        const char *newpath = reinterpret_cast<const char *>(a4);

        if (!oldpath || !newpath) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string old_dirfd_path;
        if (olddfd >= 0 && fd_table_) {
            auto old_desc = fd_table_->Get(olddfd);
            if (old_desc)
                old_dirfd_path = old_desc->guest_path;
        }
        std::string new_dirfd_path;
        if (newdfd >= 0 && fd_table_) {
            auto new_desc = fd_table_->Get(newdfd);
            if (new_desc)
                new_dirfd_path = new_desc->guest_path;
        }

        auto old_resolved = vfs_->ResolveAt(olddfd, oldpath, old_dirfd_path);
        auto new_resolved = vfs_->ResolveAt(newdfd, newpath, new_dirfd_path);

        if (!std::filesystem::exists(old_resolved.host_path)) {
            ret = LINUX_ENOENT;
            break;
        }

        std::wstring old_wpath = old_resolved.host_path.wstring();
        std::wstring new_wpath = new_resolved.host_path.wstring();
        if (CreateHardLinkW(new_wpath.c_str(), old_wpath.c_str(), nullptr)) {
            ret = 0;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) {
                ret = LINUX_EEXIST;
            } else if (err == ERROR_ACCESS_DENIED) {
                ret = LINUX_EACCES;
            } else {
                ret = LINUX_EIO;
            }
        }
        break;
    }

    case 266: { // sys_symlinkat(target, newdfd, linkpath)
        const char *target = reinterpret_cast<const char *>(a1);
        int newdfd = static_cast<int>(a2);
        const char *linkpath = reinterpret_cast<const char *>(a3);

        if (!target || !linkpath) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!vfs_) {
            ret = LINUX_ENOENT;
            break;
        }
        std::string new_dirfd_path;
        if (newdfd >= 0 && fd_table_) {
            auto new_desc = fd_table_->Get(newdfd);
            if (new_desc)
                new_dirfd_path = new_desc->guest_path;
        }

        auto new_resolved = vfs_->ResolveAt(newdfd, linkpath, new_dirfd_path);
        auto target_resolved = vfs_->Resolve(target);

        std::wstring target_wpath = target_resolved.host_path.wstring();
        std::wstring link_wpath = new_resolved.host_path.wstring();

        DWORD symlink_flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (std::filesystem::is_directory(target_resolved.host_path)) {
            symlink_flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
        }

        if (CreateSymbolicLinkW(link_wpath.c_str(), target_wpath.c_str(), symlink_flags)) {
            ret = 0;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_PRIVILEGE_NOT_HELD) {
                ret = LINUX_EPERM;
            } else if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) {
                ret = LINUX_EEXIST;
            } else if (err == ERROR_ACCESS_DENIED) {
                ret = LINUX_EACCES;
            } else {
                ret = LINUX_EIO;
            }
        }
        break;
    }

    case 267: { // sys_readlinkat(dfd, pathname, buf, bufsiz)
        int dfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        char *buf = reinterpret_cast<char *>(a3);
        size_t bufsiz = static_cast<size_t>(a4);

        if (!pathname || !buf) {
            ret = LINUX_EFAULT;
            break;
        }

        std::string canon = fs::ProcFs::CanonicalizePath(pathname);
        if (canon == "/proc/self/exe" && vfs_) {
            std::string exe_str = vfs_->GetExecutablePath().string();
            size_t copy_len = std::min(bufsiz, exe_str.size());
            std::memcpy(buf, exe_str.data(), copy_len);
            ret = static_cast<int64_t>(copy_len);
            break;
        }

        if (canon.starts_with("/proc/self/fd/")) {
            std::string fd_str = canon.substr(14);
            int target_fd = -1;
            try {
                target_fd = std::stoi(fd_str);
            } catch (...) {
                target_fd = -1;
            }
            if (!fd_table_ || target_fd < 0) {
                ret = LINUX_ENOENT;
                break;
            }
            auto desc = fd_table_->Get(target_fd);
            if (!desc) {
                ret = LINUX_ENOENT;
                break;
            }
            std::string target_link;
            if (!desc->guest_path.empty()) {
                target_link = desc->guest_path;
            } else if (desc->dev == fs::DeviceType::Socket) {
                target_link = "socket:[" + std::to_string(target_fd) + "]";
            } else if (desc->dev == fs::DeviceType::Pipe) {
                target_link = "pipe:[" + std::to_string(target_fd) + "]";
            } else if (desc->dev == fs::DeviceType::Epoll) {
                target_link = "anon_inode:[eventpoll]";
            } else if (desc->dev == fs::DeviceType::EventFd) {
                target_link = "anon_inode:[eventfd]";
            } else if (desc->dev == fs::DeviceType::TimerFd) {
                target_link = "anon_inode:[timerfd]";
            } else if (desc->dev == fs::DeviceType::SyncFence) {
                target_link = "anon_inode:[sync_fence]";
            } else {
                target_link = "anon_inode:[fd" + std::to_string(target_fd) + "]";
            }
            size_t copy_len = std::min(bufsiz, target_link.size());
            std::memcpy(buf, target_link.data(), copy_len);
            ret = static_cast<int64_t>(copy_len);
            break;
        }

        if (vfs_) {
            std::string dirfd_path;
            if (dfd >= 0 && fd_table_) {
                auto dfd_desc = fd_table_->Get(dfd);
                if (dfd_desc)
                    dirfd_path = dfd_desc->guest_path;
            }
            auto resolved = vfs_->ResolveAt(dfd, pathname, dirfd_path);
            if (std::filesystem::exists(resolved.host_path) &&
                std::filesystem::is_symlink(resolved.host_path)) {
                std::error_code ec;
                auto target = std::filesystem::read_symlink(resolved.host_path, ec);
                if (!ec) {
                    std::string target_str = target.string();
                    size_t copy_len = std::min(bufsiz, target_str.size());
                    std::memcpy(buf, target_str.data(), copy_len);
                    ret = static_cast<int64_t>(copy_len);
                    break;
                }
            }
        }

        ret = LINUX_EINVAL;
        break;
    }

    case 268: { // sys_fchmodat(dfd, pathname, mode, flags)
        int dfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        uint32_t mode = static_cast<uint32_t>(a3);
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

        std::wstring host_wpath = resolved.host_path.wstring();
        DWORD attrs = GetFileAttributesW(host_wpath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            if ((mode & 0222) == 0) {
                attrs |= FILE_ATTRIBUTE_READONLY;
            } else {
                attrs &= ~FILE_ATTRIBUTE_READONLY;
            }
            SetFileAttributesW(host_wpath.c_str(), attrs);
        }
        ret = 0;
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

    case 270: { // sys_pselect6(nfds, readfds, writefds, exceptfds, tsp, sigmask)
        int nfds = static_cast<int>(a1);
        auto *readfds = reinterpret_cast<LinuxFdSet *>(a2);
        auto *writefds = reinterpret_cast<LinuxFdSet *>(a3);
        auto *exceptfds = reinterpret_cast<LinuxFdSet *>(a4);
        const auto *tsp = reinterpret_cast<const LinuxTimespec *>(a5);

        int timeout_ms = -1;
        if (tsp) {
            if (tsp->tv_sec < 0 || tsp->tv_nsec < 0 || tsp->tv_nsec >= 1000000000LL) {
                ret = LINUX_EINVAL;
                break;
            }
            uint64_t ms = static_cast<uint64_t>(tsp->tv_sec) * 1000 +
                          (static_cast<uint64_t>(tsp->tv_nsec) + 999999) / 1000000;
            timeout_ms = (ms > MAXDWORD) ? static_cast<int>(MAXDWORD) : static_cast<int>(ms);
        }

        ret = DoSelect(nfds, readfds, writefds, exceptfds, timeout_ms, fd_table_);
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

    case 273: { // sys_set_robust_list(head, len)
        uintptr_t head = static_cast<uintptr_t>(a1);
        size_t len = static_cast<size_t>(a2);
        if (len != 24) { // sizeof(struct robust_list_head) on x86_64
            ret = LINUX_EINVAL;
            break;
        }
        auto th = thread::ThreadManager::Instance().GetCurrentGuestThread();
        if (th) {
            th->robust_list_head = head;
            th->robust_list_len = len;
        }
        ret = 0;
        break;
    }

    case 274: { // sys_get_robust_list(pid, head_ptr, len_ptr)
        int pid = static_cast<int>(a1);
        auto *head_ptr = reinterpret_cast<uintptr_t *>(a2);
        auto *len_ptr = reinterpret_cast<size_t *>(a3);
        if (!head_ptr || !len_ptr) {
            ret = LINUX_EFAULT;
            break;
        }
        std::shared_ptr<thread::GuestThread> th;
        if (pid == 0) {
            th = thread::ThreadManager::Instance().GetCurrentGuestThread();
        } else {
            th = thread::ThreadManager::Instance().GetThread(pid);
        }
        if (th) {
            *head_ptr = th->robust_list_head;
            *len_ptr = th->robust_list_len;
        } else {
            *head_ptr = 0;
            *len_ptr = 0;
        }
        ret = 0;
        break;
    }

    case 275: { // sys_splice(fd_in, off_in, fd_out, off_out, len, flags)
        int fd_in = static_cast<int>(a1);
        auto *off_in = reinterpret_cast<int64_t *>(a2);
        int fd_out = static_cast<int>(a3);
        auto *off_out = reinterpret_cast<int64_t *>(a4);
        size_t len = static_cast<size_t>(a5);
        unsigned int flags = static_cast<unsigned int>(a6);
        (void)flags;

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto in_desc = fd_table_->Get(fd_in);
        auto out_desc = fd_table_->Get(fd_out);
        if (!in_desc || !out_desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (len == 0) {
            ret = 0;
            break;
        }

        std::vector<uint8_t> buffer(std::min<size_t>(len, 65536));
        size_t to_read = std::min(buffer.size(), len);
        int64_t nread = 0;

        if (off_in && in_desc->handle != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER orig_pos{}, zero{}, target_pos{};
            SetFilePointerEx(in_desc->handle, zero, &orig_pos, FILE_CURRENT);
            target_pos.QuadPart = *off_in;
            SetFilePointerEx(in_desc->handle, target_pos, nullptr, FILE_BEGIN);
            DWORD dw_read = 0;
            if (ReadFile(in_desc->handle, buffer.data(), static_cast<DWORD>(to_read), &dw_read,
                         nullptr)) {
                nread = dw_read;
                *off_in += dw_read;
            }
            SetFilePointerEx(in_desc->handle, orig_pos, nullptr, FILE_BEGIN);
        } else {
            nread = Dispatch(0, fd_in, reinterpret_cast<uint64_t>(buffer.data()), to_read, 0, 0, 0);
            if (off_in && nread > 0) {
                *off_in += nread;
            }
        }

        if (nread <= 0) {
            ret = nread;
            break;
        }

        int64_t nwritten = 0;
        if (off_out && out_desc->handle != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER orig_pos{}, zero{}, target_pos{};
            SetFilePointerEx(out_desc->handle, zero, &orig_pos, FILE_CURRENT);
            target_pos.QuadPart = *off_out;
            SetFilePointerEx(out_desc->handle, target_pos, nullptr, FILE_BEGIN);
            DWORD dw_written = 0;
            if (WriteFile(out_desc->handle, buffer.data(), static_cast<DWORD>(nread), &dw_written,
                          nullptr)) {
                nwritten = dw_written;
                *off_out += dw_written;
            }
            SetFilePointerEx(out_desc->handle, orig_pos, nullptr, FILE_BEGIN);
        } else {
            nwritten = Dispatch(1, fd_out, reinterpret_cast<uint64_t>(buffer.data()),
                                static_cast<size_t>(nread), 0, 0, 0);
            if (off_out && nwritten > 0) {
                *off_out += nwritten;
            }
        }

        ret = nwritten;
        break;
    }

    case 280: { // sys_utimensat(dirfd, pathname, times, flags)
        int dirfd = static_cast<int>(a1);
        const char *pathname = reinterpret_cast<const char *>(a2);
        const auto *times = reinterpret_cast<const LinuxTimespec *>(a3);
        HANDLE file_handle = INVALID_HANDLE_VALUE;
        bool close_handle = false;

        if (!pathname) {
            if (!fd_table_) {
                ret = LINUX_EBADF;
                break;
            }
            auto desc = fd_table_->Get(dirfd);
            if (!desc || desc->handle == INVALID_HANDLE_VALUE) {
                ret = LINUX_EBADF;
                break;
            }
            file_handle = desc->handle;
        } else {
            if (!vfs_) {
                ret = LINUX_ENOENT;
                break;
            }
            auto resolved = vfs_->ResolveAt(dirfd, pathname);
            std::wstring wpath = resolved.host_path.wstring();
            file_handle = CreateFileW(wpath.c_str(), FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (file_handle == INVALID_HANDLE_VALUE) {
                ret = LINUX_ENOENT;
                break;
            }
            close_handle = true;
        }

        auto to_filetime = [](int64_t sec, int64_t nsec, FILETIME *ft) -> bool {
            if (nsec == LINUX_UTIME_OMIT)
                return false;
            uint64_t ft_val = 0;
            if (nsec == LINUX_UTIME_NOW || (sec == 0 && nsec == 0)) {
                FILETIME now_ft;
                GetSystemTimeAsFileTime(&now_ft);
                *ft = now_ft;
                return true;
            }
            ft_val = static_cast<uint64_t>(sec) * 10'000'000ULL +
                     static_cast<uint64_t>(nsec) / 100ULL + 116444736000000000ULL;
            ft->dwLowDateTime = static_cast<DWORD>(ft_val & 0xFFFFFFFF);
            ft->dwHighDateTime = static_cast<DWORD>(ft_val >> 32);
            return true;
        };

        FILETIME atime_ft{}, mtime_ft{};
        FILETIME *p_atime = nullptr;
        FILETIME *p_mtime = nullptr;

        if (!times) {
            FILETIME now_ft;
            GetSystemTimeAsFileTime(&now_ft);
            atime_ft = now_ft;
            mtime_ft = now_ft;
            p_atime = &atime_ft;
            p_mtime = &mtime_ft;
        } else {
            if (to_filetime(times[0].tv_sec, times[0].tv_nsec, &atime_ft)) {
                p_atime = &atime_ft;
            }
            if (to_filetime(times[1].tv_sec, times[1].tv_nsec, &mtime_ft)) {
                p_mtime = &mtime_ft;
            }
        }

        BOOL ok = SetFileTime(file_handle, nullptr, p_atime, p_mtime);
        if (close_handle) {
            CloseHandle(file_handle);
        }
        ret = ok ? 0 : LINUX_EINVAL;
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

    case 283: { // sys_timerfd_create(clockid, flags)
        int clockid = static_cast<int>(a1);
        int flags = static_cast<int>(a2);

        if (clockid != LINUX_CLOCK_REALTIME && clockid != LINUX_CLOCK_MONOTONIC &&
            clockid != LINUX_CLOCK_BOOTTIME && clockid != LINUX_CLOCK_REALTIME_ALARM &&
            clockid != LINUX_CLOCK_BOOTTIME_ALARM) {
            ret = LINUX_EINVAL;
            break;
        }

        constexpr int valid_flags = LINUX_TFD_NONBLOCK | LINUX_TFD_CLOEXEC;
        if ((flags & ~valid_flags) != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        if (!fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }

        int fd_flags = 0;
        if (flags & LINUX_TFD_NONBLOCK) {
            fd_flags |= LINUX_O_NONBLOCK;
        }
        if (flags & LINUX_TFD_CLOEXEC) {
            fd_flags |= LINUX_O_CLOEXEC;
        }

        ret = fd_table_->AllocateTimerFd(clockid, fd_flags);
        break;
    }

    case 285: { // sys_fallocate(fd, mode, offset, len)
        int fd = static_cast<int>(a1);
        int mode = static_cast<int>(a2);
        int64_t offset = static_cast<int64_t>(a3);
        int64_t len = static_cast<int64_t>(a4);

        if (offset < 0 || len <= 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (mode != 0 && mode != LINUX_FALLOC_FL_KEEP_SIZE) {
            ret = LINUX_EOPNOTSUPP;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc || desc->handle == INVALID_HANDLE_VALUE) {
            ret = LINUX_EBADF;
            break;
        }

        if (mode == 0) {
            LARGE_INTEGER cur_pos{}, cur_size{};
            LARGE_INTEGER zero{};
            SetFilePointerEx(desc->handle, zero, &cur_pos, FILE_CURRENT);
            GetFileSizeEx(desc->handle, &cur_size);
            int64_t target_size = offset + len;
            if (target_size > cur_size.QuadPart) {
                LARGE_INTEGER target_pos;
                target_pos.QuadPart = target_size;
                SetFilePointerEx(desc->handle, target_pos, nullptr, FILE_BEGIN);
                SetEndOfFile(desc->handle);
                SetFilePointerEx(desc->handle, cur_pos, nullptr, FILE_BEGIN);
            }
        }
        ret = 0;
        break;
    }

    case 286: { // sys_timerfd_settime(fd, flags, new_value, old_value)
        int fd = static_cast<int>(a1);
        int flags = static_cast<int>(a2);
        const auto *new_value = reinterpret_cast<const LinuxItimerspec *>(a3);
        auto *old_value = reinterpret_cast<LinuxItimerspec *>(a4);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev != fs::DeviceType::TimerFd || !desc->timerfd) {
            ret = LINUX_EINVAL;
            break;
        }

        constexpr int valid_flags = LINUX_TFD_TIMER_ABSTIME | LINUX_TFD_TIMER_CANCEL_ON_SET;
        if ((flags & ~valid_flags) != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        if (!new_value) {
            ret = LINUX_EFAULT;
            break;
        }

        auto is_valid_ts = [](const LinuxTimespec &ts) {
            return ts.tv_sec >= 0 && ts.tv_nsec >= 0 && ts.tv_nsec < 1'000'000'000LL;
        };
        if (!is_valid_ts(new_value->it_value) || !is_valid_ts(new_value->it_interval)) {
            ret = LINUX_EINVAL;
            break;
        }

        int64_t interval_ns =
            new_value->it_interval.tv_sec * 1'000'000'000LL + new_value->it_interval.tv_nsec;
        bool arm = (new_value->it_value.tv_sec != 0 || new_value->it_value.tv_nsec != 0);

        std::chrono::steady_clock::time_point target_tp{};
        if (arm) {
            int64_t val_ns =
                new_value->it_value.tv_sec * 1'000'000'000LL + new_value->it_value.tv_nsec;
            if ((flags & LINUX_TFD_TIMER_ABSTIME) != 0) {
                int64_t now_ns = 0;
                if (desc->timerfd->clockid == LINUX_CLOCK_REALTIME ||
                    desc->timerfd->clockid == LINUX_CLOCK_REALTIME_ALARM) {
                    now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
                } else {
                    LARGE_INTEGER freq, counter;
                    QueryPerformanceFrequency(&freq);
                    QueryPerformanceCounter(&counter);
                    now_ns = (counter.QuadPart / freq.QuadPart) * 1'000'000'000LL +
                             (counter.QuadPart % freq.QuadPart) * 1'000'000'000LL / freq.QuadPart;
                }
                target_tp =
                    std::chrono::steady_clock::now() + std::chrono::nanoseconds(val_ns - now_ns);
            } else {
                target_tp = std::chrono::steady_clock::now() + std::chrono::nanoseconds(val_ns);
            }
        }

        int64_t old_val_ns = 0;
        int64_t old_int_ns = 0;
        desc->timerfd->SetTime(arm, target_tp, interval_ns, old_value ? &old_val_ns : nullptr,
                               old_value ? &old_int_ns : nullptr);

        if (old_value) {
            old_value->it_value.tv_sec = old_val_ns / 1'000'000'000LL;
            old_value->it_value.tv_nsec = old_val_ns % 1'000'000'000LL;
            old_value->it_interval.tv_sec = old_int_ns / 1'000'000'000LL;
            old_value->it_interval.tv_nsec = old_int_ns % 1'000'000'000LL;
        }

        ret = 0;
        break;
    }

    case 287: { // sys_timerfd_gettime(fd, curr_value)
        int fd = static_cast<int>(a1);
        auto *curr_value = reinterpret_cast<LinuxItimerspec *>(a2);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(fd);
        if (!desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (desc->dev != fs::DeviceType::TimerFd || !desc->timerfd) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!curr_value) {
            ret = LINUX_EFAULT;
            break;
        }

        int64_t val_ns = 0;
        int64_t int_ns = 0;
        desc->timerfd->GetTime(val_ns, int_ns);

        curr_value->it_value.tv_sec = val_ns / 1'000'000'000LL;
        curr_value->it_value.tv_nsec = val_ns % 1'000'000'000LL;
        curr_value->it_interval.tv_sec = int_ns / 1'000'000'000LL;
        curr_value->it_interval.tv_nsec = int_ns % 1'000'000'000LL;

        ret = 0;
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

    case 290: { // sys_eventfd2(initval, flags)
        uint64_t initval = a1;
        int flags = static_cast<int>(a2);

        constexpr int valid_flags = LINUX_EFD_SEMAPHORE | LINUX_EFD_NONBLOCK | LINUX_EFD_CLOEXEC;
        if ((flags & ~valid_flags) != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        if (!fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }

        bool is_semaphore = (flags & LINUX_EFD_SEMAPHORE) != 0;
        int fd_flags = 0;
        if (flags & LINUX_EFD_NONBLOCK) {
            fd_flags |= LINUX_O_NONBLOCK;
        }
        if (flags & LINUX_EFD_CLOEXEC) {
            fd_flags |= LINUX_O_CLOEXEC;
        }

        ret = fd_table_->AllocateEventFd(initval, is_semaphore, fd_flags);
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

    case 299: { // sys_recvmmsg(sockfd, msgvec, vlen, flags, timeout)
        int sockfd = static_cast<int>(a1);
        auto *msgvec = reinterpret_cast<LinuxMmsghdr *>(a2);
        unsigned int vlen = static_cast<unsigned int>(a3);
        unsigned int flags = static_cast<unsigned int>(a4);
        (void)a5;

        if (!msgvec || vlen == 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        unsigned int count = 0;
        for (unsigned int i = 0; i < vlen; ++i) {
            int64_t res = Dispatch(47, sockfd, reinterpret_cast<uint64_t>(&msgvec[i].msg_hdr),
                                   flags, 0, 0, 0);
            if (res < 0) {
                if (count == 0) {
                    ret = res;
                    break;
                }
                break;
            }
            msgvec[i].msg_len = static_cast<unsigned int>(res);
            count++;
        }
        if (count > 0) {
            ret = static_cast<int64_t>(count);
        }
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

    case 307: { // sys_sendmmsg(sockfd, msgvec, vlen, flags)
        int sockfd = static_cast<int>(a1);
        auto *msgvec = reinterpret_cast<LinuxMmsghdr *>(a2);
        unsigned int vlen = static_cast<unsigned int>(a3);
        unsigned int flags = static_cast<unsigned int>(a4);

        if (!msgvec || vlen == 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }

        unsigned int count = 0;
        for (unsigned int i = 0; i < vlen; ++i) {
            int64_t res = Dispatch(46, sockfd, reinterpret_cast<uint64_t>(&msgvec[i].msg_hdr),
                                   flags, 0, 0, 0);
            if (res < 0) {
                if (count == 0) {
                    ret = res;
                    break;
                }
                break;
            }
            msgvec[i].msg_len = static_cast<unsigned int>(res);
            count++;
        }
        if (count > 0) {
            ret = static_cast<int64_t>(count);
        }
        break;
    }

    case 316: { // sys_renameat2(olddfd, oldname, newdfd, newname, flags)
        unsigned int flags = static_cast<unsigned int>(a5);
        if (flags != 0) {
            ret = LINUX_EINVAL;
            break;
        }
        ret = Dispatch(264, a1, a2, a3, a4, 0, 0);
        break;
    }

    case 318: { // sys_getrandom(buf, buflen, flags)
        void *buf = reinterpret_cast<void *>(a1);
        size_t buflen = static_cast<size_t>(a2);
        unsigned int flags = static_cast<unsigned int>(a3);

        constexpr unsigned int valid_flags = LINUX_GRND_NONBLOCK | LINUX_GRND_RANDOM;
        if ((flags & ~valid_flags) != 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!buf && buflen > 0) {
            ret = LINUX_EFAULT;
            break;
        }
        if (buflen == 0) {
            ret = 0;
            break;
        }

        NTSTATUS status =
            BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), static_cast<ULONG>(buflen),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (BCRYPT_SUCCESS(status)) {
            ret = static_cast<int64_t>(buflen);
        } else {
            ret = LINUX_EIO;
        }
        break;
    }

    case 319: { // sys_memfd_create(uname, flags)
        const char *uname = reinterpret_cast<const char *>(a1);
        unsigned int flags = static_cast<unsigned int>(a2);
        if (!uname) {
            ret = LINUX_EFAULT;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }
        auto shm = mem::SharedMemoryManager::Instance().CreateRegion(uname, 0);
        if (!shm) {
            ret = LINUX_ENOMEM;
            break;
        }
        int fd_flags = 0;
        if (flags & 1 /* MFD_CLOEXEC */) {
            fd_flags |= LINUX_O_CLOEXEC;
        }
        ret = fd_table_->AllocateSharedMemory(shm, fs::DeviceType::SharedMemory, fd_flags, uname);
        break;
    }

    case 324: { // sys_membarrier(cmd, flags, cpu_id)
        int cmd = static_cast<int>(a1);
        unsigned int flags = static_cast<unsigned int>(a2);
        (void)a3;

        if (cmd == LINUX_MEMBARRIER_CMD_QUERY) {
            ret = (LINUX_MEMBARRIER_CMD_GLOBAL | LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED |
                   LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED |
                   LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED |
                   LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |
                   LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE |
                   LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE);
            break;
        }

        if (flags != 0) {
            ret = LINUX_EINVAL;
            break;
        }

        if (cmd == LINUX_MEMBARRIER_CMD_GLOBAL || cmd == LINUX_MEMBARRIER_CMD_GLOBAL_EXPEDITED ||
            cmd == LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED ||
            cmd == LINUX_MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE) {
            FlushProcessWriteBuffers();
            ret = 0;
            break;
        }

        if (cmd == LINUX_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED ||
            cmd == LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED ||
            cmd == LINUX_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE) {
            ret = 0;
            break;
        }

        ret = LINUX_EINVAL;
        break;
    }

    case 326: { // sys_copy_file_range(fd_in, off_in, fd_out, off_out, len, flags)
        int fd_in = static_cast<int>(a1);
        auto *off_in = reinterpret_cast<int64_t *>(a2);
        int fd_out = static_cast<int>(a3);
        auto *off_out = reinterpret_cast<int64_t *>(a4);
        size_t len = static_cast<size_t>(a5);
        unsigned int flags = static_cast<unsigned int>(a6);

        if (flags != 0) {
            ret = LINUX_EINVAL;
            break;
        }
        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto in_desc = fd_table_->Get(fd_in);
        auto out_desc = fd_table_->Get(fd_out);
        if (!in_desc || !out_desc) {
            ret = LINUX_EBADF;
            break;
        }
        if (len == 0) {
            ret = 0;
            break;
        }

        std::vector<uint8_t> buffer(std::min<size_t>(len, 65536));
        size_t total_copied = 0;

        while (total_copied < len) {
            size_t chunk = std::min(buffer.size(), len - total_copied);
            int64_t nread = 0;

            if (off_in && in_desc->handle != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER orig_pos{}, zero{}, target_pos{};
                SetFilePointerEx(in_desc->handle, zero, &orig_pos, FILE_CURRENT);
                target_pos.QuadPart = *off_in;
                SetFilePointerEx(in_desc->handle, target_pos, nullptr, FILE_BEGIN);
                DWORD dw_read = 0;
                if (ReadFile(in_desc->handle, buffer.data(), static_cast<DWORD>(chunk), &dw_read,
                             nullptr)) {
                    nread = dw_read;
                    *off_in += dw_read;
                }
                SetFilePointerEx(in_desc->handle, orig_pos, nullptr, FILE_BEGIN);
            } else {
                nread =
                    Dispatch(0, fd_in, reinterpret_cast<uint64_t>(buffer.data()), chunk, 0, 0, 0);
                if (off_in && nread > 0) {
                    *off_in += nread;
                }
            }

            if (nread <= 0) {
                break;
            }

            int64_t nwritten = 0;
            if (off_out && out_desc->handle != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER orig_pos{}, zero{}, target_pos{};
                SetFilePointerEx(out_desc->handle, zero, &orig_pos, FILE_CURRENT);
                target_pos.QuadPart = *off_out;
                SetFilePointerEx(out_desc->handle, target_pos, nullptr, FILE_BEGIN);
                DWORD dw_written = 0;
                if (WriteFile(out_desc->handle, buffer.data(), static_cast<DWORD>(nread),
                              &dw_written, nullptr)) {
                    nwritten = dw_written;
                    *off_out += dw_written;
                }
                SetFilePointerEx(out_desc->handle, orig_pos, nullptr, FILE_BEGIN);
            } else {
                nwritten = Dispatch(1, fd_out, reinterpret_cast<uint64_t>(buffer.data()),
                                    static_cast<size_t>(nread), 0, 0, 0);
                if (off_out && nwritten > 0) {
                    *off_out += nwritten;
                }
            }

            if (nwritten <= 0) {
                break;
            }
            total_copied += static_cast<size_t>(nwritten);
            if (nwritten < nread) {
                break;
            }
        }

        ret = static_cast<int64_t>(total_copied);
        break;
    }

    case 332: { // sys_statx(dfd, filename, flags, mask, statxbuf)
        auto *sx = reinterpret_cast<LinuxStatx *>(a5);
        if (!sx) {
            ret = LINUX_EFAULT;
            break;
        }
        LinuxStat64 sb{};
        int64_t r = Dispatch(262, a1, a2, reinterpret_cast<uint64_t>(&sb), a3, 0, 0);
        if (r < 0) {
            ret = r;
            break;
        }

        std::memset(sx, 0, sizeof(LinuxStatx));
        uint32_t req_mask = static_cast<uint32_t>(a4);
        sx->stx_mask =
            LINUX_STATX_BASIC_STATS & (req_mask != 0 ? req_mask : LINUX_STATX_BASIC_STATS);
        sx->stx_blksize = static_cast<uint32_t>(sb.st_blksize);
        sx->stx_nlink = static_cast<uint32_t>(sb.st_nlink);
        sx->stx_uid = sb.st_uid;
        sx->stx_gid = sb.st_gid;
        sx->stx_mode = static_cast<uint16_t>(sb.st_mode);
        sx->stx_ino = sb.st_ino;
        sx->stx_size = static_cast<uint64_t>(sb.st_size);
        sx->stx_blocks = static_cast<uint64_t>(sb.st_blocks);
        sx->stx_atime.tv_sec = sb.st_atime;
        sx->stx_atime.tv_nsec = static_cast<uint32_t>(sb.st_atime_nsec);
        sx->stx_mtime.tv_sec = sb.st_mtime;
        sx->stx_mtime.tv_nsec = static_cast<uint32_t>(sb.st_mtime_nsec);
        sx->stx_ctime.tv_sec = sb.st_ctime;
        sx->stx_ctime.tv_nsec = static_cast<uint32_t>(sb.st_ctime_nsec);
        ret = 0;
        break;
    }

    case 436: { // sys_close_range(first, last, flags)
        unsigned int first = static_cast<unsigned int>(a1);
        unsigned int last = static_cast<unsigned int>(a2);
        unsigned int flags = static_cast<unsigned int>(a3);
        if (!fd_table_) {
            ret = LINUX_ENOSYS;
            break;
        }
        ret = fd_table_->CloseRange(first, last, flags);
        break;
    }

    case 439: { // sys_faccessat2(dfd, filename, mode, flags)
        int dfd = static_cast<int>(a1);
        const char *filename = reinterpret_cast<const char *>(a2);
        int mode = static_cast<int>(a3);
        int flags = static_cast<int>(a4);

        if (flags & ~(LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EACCESS | LINUX_AT_EMPTY_PATH)) {
            ret = LINUX_EINVAL;
            break;
        }
        if (mode & ~7) {
            ret = LINUX_EINVAL;
            break;
        }

        if (flags & LINUX_AT_EMPTY_PATH) {
            if (!filename || filename[0] == '\0') {
                if (!fd_table_) {
                    ret = LINUX_EBADF;
                    break;
                }
                auto desc = fd_table_->Get(dfd);
                if (!desc) {
                    ret = LINUX_EBADF;
                    break;
                }
                ret = 0;
                break;
            }
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

    case 441: { // sys_epoll_pwait2(epfd, events, maxevents, timespec, sigmask, sigsetsize)
        int epfd = static_cast<int>(a1);
        auto *events = reinterpret_cast<poll::LinuxEpollEvent *>(a2);
        int maxevents = static_cast<int>(a3);
        const auto *tsp = reinterpret_cast<const LinuxTimespec *>(a4);

        if (!fd_table_) {
            ret = LINUX_EBADF;
            break;
        }
        auto desc = fd_table_->Get(epfd);
        if (!desc || desc->dev != fs::DeviceType::Epoll) {
            ret = LINUX_EBADF;
            break;
        }

        int timeout_ms = -1;
        if (tsp) {
            if (tsp->tv_sec < 0 || tsp->tv_nsec < 0 || tsp->tv_nsec >= 1000000000LL) {
                ret = LINUX_EINVAL;
                break;
            }
            uint64_t ms = static_cast<uint64_t>(tsp->tv_sec) * 1000 +
                          (static_cast<uint64_t>(tsp->tv_nsec) + 999999) / 1000000;
            timeout_ms = (ms > MAXDWORD) ? static_cast<int>(MAXDWORD) : static_cast<int>(ms);
        }

        ret = poll::EpollManager::Instance().Wait(desc->epoll_id, events, maxevents, timeout_ms,
                                                  fd_table_);
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
