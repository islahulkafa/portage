#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "fs/fd_table.hpp"
#include "audio/audio_manager.hpp"
#include "binder/binder_driver.hpp"
#include "fs/inotify_state.hpp"
#include "gles/wgl_bridge.hpp"
#include "net/netlink_socket.hpp"
#include "poll/epoll_manager.hpp"
#include "signal/signal_manager.hpp"
#include <windows.h>
#include <winsock2.h>

namespace portage::fs {

TimerFdState::~TimerFdState()
{
    if (timer_handle && timer_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(timer_handle);
        timer_handle = nullptr;
    }
    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        CloseHandle(wake_event);
        wake_event = nullptr;
    }
}

SyncFenceState::~SyncFenceState()
{
    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        CloseHandle(wake_event);
        wake_event = nullptr;
    }
}

bool SyncFenceState::IsSignaled() noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    if (signaled) {
        return true;
    }
    if (wake_event && wake_event != INVALID_HANDLE_VALUE &&
        WaitForSingleObject(wake_event, 0) == WAIT_OBJECT_0) {
        signaled = true;
        return true;
    }
    if (gl_sync) {
        const auto &tbl = gles::WglBridge::Instance().GetDispatchTable();
        if (tbl.glClientWaitSync) {
            const ::GLenum status = tbl.glClientWaitSync(static_cast<::GLsync>(gl_sync), 0, 0);
            if (status == ::GL_ALREADY_SIGNALED || status == ::GL_CONDITION_SATISFIED) {
                signaled = true;
                if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
                    SetEvent(wake_event);
                }
                return true;
            }
        }
    }
    return false;
}

void SyncFenceState::Signal() noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    signaled = true;
    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        SetEvent(wake_event);
    }
}

void TimerFdState::ArmWaitableTimerLocked(std::chrono::nanoseconds delay)
{
    if (!timer_handle || timer_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    int64_t ns = delay.count();
    if (ns <= 0) {
        ns = 100;
    }
    LARGE_INTEGER due_time;
    due_time.QuadPart = -static_cast<int64_t>((ns + 99) / 100);
    SetWaitableTimer(timer_handle, &due_time, 0, nullptr, nullptr, FALSE);
}

uint64_t TimerFdState::RefreshExpirationsLocked(bool force_signaled)
{
    if (!armed) {
        return counter;
    }

    auto now = std::chrono::steady_clock::now();
    bool signaled = force_signaled || (timer_handle && timer_handle != INVALID_HANDLE_VALUE &&
                                       WaitForSingleObject(timer_handle, 0) == WAIT_OBJECT_0);

    if (now >= next_expiration) {
        if (interval_ns > 0) {
            auto diff =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - next_expiration).count();
            uint64_t periods = 1 + static_cast<uint64_t>(diff / interval_ns);
            counter += periods;
            next_expiration +=
                std::chrono::nanoseconds(static_cast<int64_t>(periods) * interval_ns);
            ArmWaitableTimerLocked(
                std::chrono::duration_cast<std::chrono::nanoseconds>(next_expiration - now));
        } else {
            counter += 1;
            armed = false;
            if (timer_handle && timer_handle != INVALID_HANDLE_VALUE) {
                CancelWaitableTimer(timer_handle);
            }
        }
    } else if (signaled) {
        if (interval_ns > 0) {
            counter += 1;
            next_expiration += std::chrono::nanoseconds(interval_ns);
            auto rem = std::chrono::duration_cast<std::chrono::nanoseconds>(next_expiration - now);
            ArmWaitableTimerLocked(rem);
        } else {
            counter += 1;
            armed = false;
        }
    }

    return counter;
}

bool TimerFdState::IsReady()
{
    std::lock_guard<std::mutex> lock(mutex);
    return RefreshExpirationsLocked() > 0;
}

void TimerFdState::GetTime(int64_t &out_value_ns, int64_t &out_interval_ns)
{
    std::lock_guard<std::mutex> lock(mutex);
    RefreshExpirationsLocked();
    out_interval_ns = interval_ns;
    if (!armed) {
        out_value_ns = 0;
    } else {
        auto now = std::chrono::steady_clock::now();
        auto rem =
            std::chrono::duration_cast<std::chrono::nanoseconds>(next_expiration - now).count();
        out_value_ns = (rem > 0) ? rem : 0;
    }
}

void TimerFdState::SetTime(bool arm, std::chrono::steady_clock::time_point target_tp,
                           int64_t new_interval_ns, int64_t *old_value_ns, int64_t *old_interval_ns)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (old_value_ns || old_interval_ns) {
        RefreshExpirationsLocked();
        if (old_interval_ns) {
            *old_interval_ns = interval_ns;
        }
        if (old_value_ns) {
            if (!armed) {
                *old_value_ns = 0;
            } else {
                auto now = std::chrono::steady_clock::now();
                auto rem =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(next_expiration - now)
                        .count();
                *old_value_ns = (rem > 0) ? rem : 0;
            }
        }
    }

    counter = 0;
    generation++;
    interval_ns = new_interval_ns;

    if (!arm) {
        armed = false;
        if (timer_handle && timer_handle != INVALID_HANDLE_VALUE) {
            CancelWaitableTimer(timer_handle);
        }
    } else {
        armed = true;
        next_expiration = target_tp;
        auto now = std::chrono::steady_clock::now();
        if (now >= next_expiration) {
            RefreshExpirationsLocked();
        } else {
            ArmWaitableTimerLocked(
                std::chrono::duration_cast<std::chrono::nanoseconds>(next_expiration - now));
        }
    }

    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        SetEvent(wake_event);
    }
}

int64_t TimerFdState::ReadExpirations(bool nonblock, uint64_t &out_expirations)
{
    while (true) {
        HANDLE th = nullptr;
        HANDLE we = nullptr;
        uint64_t saved_gen = 0;

        {
            std::lock_guard<std::mutex> lock(mutex);
            uint64_t cur = RefreshExpirationsLocked();
            if (cur > 0) {
                out_expirations = cur;
                counter = 0;
                return 0;
            }
            if (nonblock) {
                return -11; // -EAGAIN
            }
            th = timer_handle;
            we = wake_event;
            saved_gen = generation;
        }

        HANDLE handles[2] = {th, we};
        DWORD num_handles = 0;
        if (th && th != INVALID_HANDLE_VALUE) {
            handles[num_handles++] = th;
        }
        if (we && we != INVALID_HANDLE_VALUE) {
            handles[num_handles++] = we;
        }

        if (num_handles > 0) {
            DWORD wait_res = WaitForMultipleObjects(num_handles, handles, FALSE, INFINITE);
            if (wait_res == WAIT_OBJECT_0 && th && th != INVALID_HANDLE_VALUE && handles[0] == th) {
                std::lock_guard<std::mutex> lock(mutex);
                if (generation == saved_gen) {
                    uint64_t cur = RefreshExpirationsLocked(true);
                    if (cur > 0) {
                        out_expirations = cur;
                        counter = 0;
                        return 0;
                    }
                }
            }
        } else {
            Sleep(1);
        }
    }
}

FileDescriptorTable::FileDescriptorTable()
{
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);

    auto fd0 = std::make_shared<FileDescriptor>();
    fd0->fd = 0;
    fd0->handle = h_in;
    fd0->flags = 0; // O_RDONLY
    fd0->dev = DeviceType::Regular;
    fd0->is_pipe_or_console = true;
    m_descriptors[0] = std::move(fd0);

    auto fd1 = std::make_shared<FileDescriptor>();
    fd1->fd = 1;
    fd1->handle = h_out;
    fd1->flags = 1; // O_WRONLY
    fd1->dev = DeviceType::Regular;
    fd1->is_pipe_or_console = true;
    m_descriptors[1] = std::move(fd1);

    auto fd2 = std::make_shared<FileDescriptor>();
    fd2->fd = 2;
    fd2->handle = h_err;
    fd2->flags = 1; // O_WRONLY
    fd2->dev = DeviceType::Regular;
    fd2->is_pipe_or_console = true;
    m_descriptors[2] = std::move(fd2);
}

FileDescriptorTable::~FileDescriptorTable()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[fd, desc] : m_descriptors) {
        if (fd > 2 && desc && desc->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(desc->handle);
            desc->handle = INVALID_HANDLE_VALUE;
        }
    }
    m_descriptors.clear();
}

int FileDescriptorTable::Allocate(HANDLE handle, int flags, DeviceType dev, std::string_view path,
                                  bool is_pipe_or_console)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = handle;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->is_pipe_or_console = is_pipe_or_console;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSynthetic(int flags, DeviceType dev, std::string_view path,
                                           std::string data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->synthetic_data = std::move(data);
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateDirectory(int flags, std::string_view path,
                                           std::vector<DirEntry> entries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Directory;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->dir_entries = std::move(entries);
    desc->dir_index = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSocket(UINT_PTR s, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = reinterpret_cast<HANDLE>(s);
    desc->flags = flags;
    desc->dev = DeviceType::Socket;
    desc->offset = 0;
    desc->is_pipe_or_console = true;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateNetlinkSocket(
    std::shared_ptr<portage::net::NetlinkSocketState> netlink, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Netlink;
    desc->offset = 0;
    desc->is_pipe_or_console = true;
    desc->netlink_socket = std::move(netlink);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateEpoll(int epoll_id, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Epoll;
    desc->epoll_id = epoll_id;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateBinder(int binder_id, DeviceType dev, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->binder_id = binder_id;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateAudio(DeviceType dev, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateInput(int device_index, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = (device_index == 0) ? DeviceType::InputEvent0 : DeviceType::InputEvent1;
    desc->input_device_index = device_index;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateInotify(int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Inotify;
    desc->offset = 0;
    desc->is_pipe_or_console = false;
    desc->inotify = std::make_shared<InotifyState>(flags);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSharedMemory(std::shared_ptr<portage::mem::SharedMemoryRegion> shm,
                                              DeviceType dev, int flags, std::string_view path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->guest_path = std::string(path);
    desc->shared_memory = std::move(shm);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateEventFd(uint64_t initval, bool is_semaphore, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::EventFd;
    desc->is_pipe_or_console = false;

    auto ev_state = std::make_shared<EventFdState>();
    ev_state->counter = initval;
    ev_state->is_semaphore = is_semaphore;
    desc->eventfd = std::move(ev_state);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateTimerFd(int clockid, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::TimerFd;
    desc->is_pipe_or_console = false;

    auto tfd_state = std::make_shared<TimerFdState>();
    tfd_state->clockid = clockid;
    tfd_state->flags = flags;
    constexpr DWORD kCreateWaitableTimerHighResolution = 0x00000002;
    tfd_state->timer_handle = CreateWaitableTimerExW(
        nullptr, nullptr, kCreateWaitableTimerHighResolution, TIMER_ALL_ACCESS);
    if (!tfd_state->timer_handle) {
        tfd_state->timer_handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    tfd_state->wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    desc->timerfd = std::move(tfd_state);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSyncFence(void *gl_sync, HANDLE wake_event, bool signaled,
                                           int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::SyncFence;
    desc->is_pipe_or_console = false;

    auto fence_state = std::make_shared<SyncFenceState>();
    fence_state->gl_sync = gl_sync;
    fence_state->signaled = signaled;
    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        fence_state->wake_event = wake_event;
        if (signaled) {
            SetEvent(fence_state->wake_event);
        }
    } else {
        fence_state->wake_event = CreateEventW(nullptr, TRUE, signaled ? TRUE : FALSE, nullptr);
    }
    desc->sync_fence = std::move(fence_state);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

SignalfdState::SignalfdState(uint64_t m) : mask(m)
{
    wake_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    portage::signal::SignalManager::Instance().RegisterSignalfd(this);
}

SignalfdState::~SignalfdState()
{
    portage::signal::SignalManager::Instance().UnregisterSignalfd(this);
    if (wake_event && wake_event != INVALID_HANDLE_VALUE) {
        CloseHandle(wake_event);
        wake_event = nullptr;
    }
}

int FileDescriptorTable::AllocateSignalfd(uint64_t mask, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Signalfd;
    desc->is_pipe_or_console = false;
    desc->signalfd = std::make_shared<SignalfdState>(mask);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::CreatePipe(int fds[2], int flags)
{
    if (!fds) {
        return -14; // -EFAULT
    }

    HANDLE h_read = INVALID_HANDLE_VALUE;
    HANDLE h_write = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!::CreatePipe(&h_read, &h_write, &sa, 0)) {
        return -24; // -EMFILE
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    int r_fd = m_next_fd++;
    while (m_descriptors.contains(r_fd)) {
        r_fd = m_next_fd++;
    }

    auto r_desc = std::make_shared<FileDescriptor>();
    r_desc->fd = r_fd;
    r_desc->handle = h_read;
    r_desc->flags = 0 | flags; // O_RDONLY
    r_desc->dev = DeviceType::Pipe;
    r_desc->is_pipe_or_console = true;
    m_descriptors[r_fd] = std::move(r_desc);

    int w_fd = m_next_fd++;
    while (m_descriptors.contains(w_fd)) {
        w_fd = m_next_fd++;
    }

    auto w_desc = std::make_shared<FileDescriptor>();
    w_desc->fd = w_fd;
    w_desc->handle = h_write;
    w_desc->flags = 1 | flags; // O_WRONLY
    w_desc->dev = DeviceType::Pipe;
    w_desc->is_pipe_or_console = true;
    m_descriptors[w_fd] = std::move(w_desc);

    fds[0] = r_fd;
    fds[1] = w_fd;
    return 0;
}

std::shared_ptr<FileDescriptor> FileDescriptorTable::Get(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(fd);
    if (it != m_descriptors.end()) {
        return it->second;
    }
    return nullptr;
}

int FileDescriptorTable::Close(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(fd);
    if (it == m_descriptors.end()) {
        return -9; // -EBADF
    }

    auto desc = it->second;
    m_descriptors.erase(it);

    if (desc) {
        if (desc->dev == DeviceType::Socket) {
            if (desc->handle != INVALID_HANDLE_VALUE) {
                ::closesocket(reinterpret_cast<SOCKET>(desc->handle));
                desc->handle = INVALID_HANDLE_VALUE;
            }
        } else if (desc->dev == DeviceType::Epoll) {
            poll::EpollManager::Instance().CloseInstance(desc->epoll_id);
        } else if (desc->dev == DeviceType::Binder || desc->dev == DeviceType::HwBinder) {
            binder::BinderDriver::Instance().CloseInstance(desc->binder_id);
        } else if (desc->dev == DeviceType::AlsaPcm) {
            audio::AudioManager::Instance().CloseStream();
        } else if (fd > 2 && desc->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(desc->handle);
            desc->handle = INVALID_HANDLE_VALUE;
        }
    }

    return 0;
}

int FileDescriptorTable::CloseRange(unsigned int first, unsigned int last, unsigned int flags)
{
    if (first > last) {
        return -22; // -EINVAL
    }
    constexpr unsigned int valid_flags =
        0x2 /* CLOSE_RANGE_UNSHARE */ | 0x4 /* CLOSE_RANGE_CLOEXEC */;
    if ((flags & ~valid_flags) != 0) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (flags & 0x4 /* CLOSE_RANGE_CLOEXEC */) {
        for (auto &[fd, desc] : m_descriptors) {
            if (static_cast<unsigned int>(fd) >= first && static_cast<unsigned int>(fd) <= last) {
                if (desc) {
                    desc->flags |= 02000000; // LINUX_O_CLOEXEC
                }
            }
        }
        return 0;
    }

    std::vector<int> to_close;
    for (const auto &[fd, desc] : m_descriptors) {
        if (static_cast<unsigned int>(fd) >= first && static_cast<unsigned int>(fd) <= last) {
            to_close.push_back(fd);
        }
    }

    for (int fd : to_close) {
        auto it = m_descriptors.find(fd);
        if (it != m_descriptors.end()) {
            auto desc = it->second;
            m_descriptors.erase(it);
            if (desc) {
                if (desc->dev == DeviceType::Socket) {
                    if (desc->handle != INVALID_HANDLE_VALUE) {
                        ::closesocket(reinterpret_cast<SOCKET>(desc->handle));
                        desc->handle = INVALID_HANDLE_VALUE;
                    }
                } else if (desc->dev == DeviceType::Epoll) {
                    poll::EpollManager::Instance().CloseInstance(desc->epoll_id);
                } else if (desc->dev == DeviceType::Binder || desc->dev == DeviceType::HwBinder) {
                    binder::BinderDriver::Instance().CloseInstance(desc->binder_id);
                } else if (desc->dev == DeviceType::AlsaPcm) {
                    audio::AudioManager::Instance().CloseStream();
                } else if (fd > 2 && desc->handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(desc->handle);
                    desc->handle = INVALID_HANDLE_VALUE;
                }
            }
        }
    }

    return 0;
}

int FileDescriptorTable::Duplicate(int oldfd, int target_fd, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(oldfd);
    if (it == m_descriptors.end() || !it->second) {
        return -9; // -EBADF
    }

    auto old_desc = it->second;
    HANDLE dup_handle = INVALID_HANDLE_VALUE;

    if (old_desc->handle != INVALID_HANDLE_VALUE) {
        HANDLE current_proc = GetCurrentProcess();
        if (!DuplicateHandle(current_proc, old_desc->handle, current_proc, &dup_handle, 0, TRUE,
                             DUPLICATE_SAME_ACCESS)) {
            return -9; // -EBADF
        }
    }

    int assigned_fd = target_fd;
    if (assigned_fd < 0) {
        assigned_fd = m_next_fd++;
        while (m_descriptors.contains(assigned_fd)) {
            assigned_fd = m_next_fd++;
        }
    } else {
        auto existing = m_descriptors.find(assigned_fd);
        if (existing != m_descriptors.end()) {
            if (existing->second) {
                if (existing->second->dev == DeviceType::Socket) {
                    if (existing->second->handle != INVALID_HANDLE_VALUE) {
                        ::closesocket(reinterpret_cast<SOCKET>(existing->second->handle));
                    }
                } else if (existing->second->dev == DeviceType::Epoll) {
                    poll::EpollManager::Instance().CloseInstance(existing->second->epoll_id);
                } else if (existing->second->dev == DeviceType::Binder ||
                           existing->second->dev == DeviceType::HwBinder) {
                    binder::BinderDriver::Instance().CloseInstance(existing->second->binder_id);
                } else if (assigned_fd > 2 && existing->second->handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(existing->second->handle);
                }
            }
            m_descriptors.erase(existing);
        }
    }

    auto new_desc = std::make_shared<FileDescriptor>();
    new_desc->fd = assigned_fd;
    new_desc->handle = dup_handle;
    new_desc->flags = old_desc->flags | flags;
    new_desc->dev = old_desc->dev;
    new_desc->offset = old_desc->offset;
    new_desc->guest_path = old_desc->guest_path;
    new_desc->is_pipe_or_console = old_desc->is_pipe_or_console;
    new_desc->synthetic_data = old_desc->synthetic_data;
    new_desc->dir_entries = old_desc->dir_entries;
    new_desc->dir_index = old_desc->dir_index;
    new_desc->epoll_id = old_desc->epoll_id;
    new_desc->binder_id = old_desc->binder_id;
    new_desc->input_device_index = old_desc->input_device_index;
    new_desc->shared_memory = old_desc->shared_memory;
    new_desc->eventfd = old_desc->eventfd;
    new_desc->timerfd = old_desc->timerfd;
    new_desc->sync_fence = old_desc->sync_fence;
    new_desc->signalfd = old_desc->signalfd;
    new_desc->inotify = old_desc->inotify;
    new_desc->namespace_id = old_desc->namespace_id;
    new_desc->namespace_type = old_desc->namespace_type;
    new_desc->netlink_socket = old_desc->netlink_socket;

    m_descriptors[assigned_fd] = std::move(new_desc);
    return assigned_fd;
}

std::vector<int> FileDescriptorTable::GetOpenFds()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int> fds;
    fds.reserve(m_descriptors.size());
    for (const auto &[fd, desc] : m_descriptors) {
        if (desc) {
            fds.push_back(fd);
        }
    }
    std::sort(fds.begin(), fds.end());
    return fds;
}

void FileDescriptorTable::CloseOnExec()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int> to_close;
    for (const auto &[fd, desc] : m_descriptors) {
        if (fd > 2 && desc && (desc->flags & 02000000)) { // O_CLOEXEC
            to_close.push_back(fd);
        }
    }
    for (int fd : to_close) {
        auto it = m_descriptors.find(fd);
        if (it != m_descriptors.end()) {
            auto desc = it->second;
            m_descriptors.erase(it);
            if (desc) {
                if (desc->dev == DeviceType::Socket) {
                    if (desc->handle != INVALID_HANDLE_VALUE) {
                        ::closesocket(reinterpret_cast<SOCKET>(desc->handle));
                        desc->handle = INVALID_HANDLE_VALUE;
                    }
                } else if (desc->dev == DeviceType::Epoll) {
                    poll::EpollManager::Instance().CloseInstance(desc->epoll_id);
                } else if (desc->dev == DeviceType::Binder || desc->dev == DeviceType::HwBinder) {
                    binder::BinderDriver::Instance().CloseInstance(desc->binder_id);
                } else if (desc->handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(desc->handle);
                    desc->handle = INVALID_HANDLE_VALUE;
                }
            }
        }
    }
}

} // namespace portage::fs
