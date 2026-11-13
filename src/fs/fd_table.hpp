#pragma once

#include "fs/dirent.hpp"
#include "fs/vfs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::mem {
struct SharedMemoryRegion;
}

namespace portage::net {
class NetlinkSocketState;
}

namespace portage::fs {

class InotifyState;

struct EventFdState {
    std::mutex mutex{};
    std::condition_variable cv{};
    uint64_t counter{0};
    bool is_semaphore{false};
};

struct TimerFdState {
    HANDLE timer_handle{nullptr};
    HANDLE wake_event{nullptr};
    int clockid{0};
    int flags{0};
    std::mutex mutex{};
    uint64_t counter{0};
    int64_t interval_ns{0};
    std::chrono::steady_clock::time_point next_expiration{};
    bool armed{false};
    uint64_t generation{0};

    TimerFdState() = default;
    ~TimerFdState();

    TimerFdState(const TimerFdState &) = delete;
    TimerFdState &operator=(const TimerFdState &) = delete;

    void ArmWaitableTimerLocked(std::chrono::nanoseconds delay);
    uint64_t RefreshExpirationsLocked(bool force_signaled = false);
    [[nodiscard]] bool IsReady();
    void GetTime(int64_t &out_value_ns, int64_t &out_interval_ns);
    void SetTime(bool arm, std::chrono::steady_clock::time_point target_tp, int64_t new_interval_ns,
                 int64_t *old_value_ns = nullptr, int64_t *old_interval_ns = nullptr);
    int64_t ReadExpirations(bool nonblock, uint64_t &out_expirations);
};

struct SyncFenceState {
    void *gl_sync{nullptr};
    HANDLE wake_event{nullptr};
    bool signaled{false};
    std::mutex mutex{};

    SyncFenceState() = default;
    ~SyncFenceState();

    SyncFenceState(const SyncFenceState &) = delete;
    SyncFenceState &operator=(const SyncFenceState &) = delete;

    [[nodiscard]] bool IsSignaled() noexcept;
    void Signal() noexcept;
};

struct SignalfdState {
    uint64_t mask{0};
    HANDLE wake_event{nullptr};
    std::mutex mutex{};

    explicit SignalfdState(uint64_t m);
    ~SignalfdState();

    SignalfdState(const SignalfdState &) = delete;
    SignalfdState &operator=(const SignalfdState &) = delete;
};

struct FileDescriptor {
    int fd{-1};
    HANDLE handle{INVALID_HANDLE_VALUE};
    int flags{0};
    DeviceType dev{DeviceType::Regular};
    uint64_t offset{0};
    std::string guest_path{};
    bool is_pipe_or_console{false};

    // Synthetic virtual file support
    std::string synthetic_data{};

    // Directory enumeration support
    std::vector<DirEntry> dir_entries{};
    size_t dir_index{0};

    // Epoll instance ID
    int epoll_id{-1};

    // Binder instance ID
    int binder_id{-1};

    // Input device index (0 = keyboard, 1 = touchscreen)
    int input_device_index{-1};

    // Shared memory region (ashmem / memfd)
    std::shared_ptr<portage::mem::SharedMemoryRegion> shared_memory{nullptr};

    // Eventfd support
    std::shared_ptr<EventFdState> eventfd{nullptr};

    // Timerfd support
    std::shared_ptr<TimerFdState> timerfd{nullptr};

    // Sync fence support
    std::shared_ptr<SyncFenceState> sync_fence{nullptr};

    // Signalfd support
    std::shared_ptr<SignalfdState> signalfd{nullptr};

    // Inotify support
    std::shared_ptr<InotifyState> inotify{nullptr};

    // Namespace fd support
    uint64_t namespace_id{0};
    int namespace_type{0};

    // Netlink socket support
    std::shared_ptr<portage::net::NetlinkSocketState> netlink_socket{nullptr};
};

class FileDescriptorTable {
  public:
    FileDescriptorTable();
    ~FileDescriptorTable();

    FileDescriptorTable(const FileDescriptorTable &) = delete;
    FileDescriptorTable &operator=(const FileDescriptorTable &) = delete;

    int Allocate(HANDLE handle, int flags, DeviceType dev = DeviceType::Regular,
                 std::string_view path = "", bool is_pipe_or_console = false);

    int AllocateSynthetic(int flags, DeviceType dev, std::string_view path, std::string data);

    int AllocateDirectory(int flags, std::string_view path, std::vector<DirEntry> entries);

    int AllocateSocket(UINT_PTR s, int flags);

    int AllocateNetlinkSocket(std::shared_ptr<portage::net::NetlinkSocketState> netlink, int flags);

    int AllocateEpoll(int epoll_id, int flags);

    int AllocateBinder(int binder_id, DeviceType dev, int flags);

    int AllocateAudio(DeviceType dev, int flags);

    int AllocateInput(int device_index, int flags);

    int AllocateInotify(int flags);

    int AllocateSharedMemory(std::shared_ptr<portage::mem::SharedMemoryRegion> shm, DeviceType dev,
                             int flags, std::string_view path);

    int AllocateEventFd(uint64_t initval, bool is_semaphore, int flags);

    int AllocateTimerFd(int clockid, int flags);

    int AllocateSyncFence(void *gl_sync, HANDLE wake_event, bool signaled, int flags = 0);

    int AllocateSignalfd(uint64_t mask, int flags = 0);

    int CreatePipe(int fds[2], int flags = 0);

    [[nodiscard]] std::shared_ptr<FileDescriptor> Get(int fd);

    [[nodiscard]] std::vector<int> GetOpenFds();

    int Close(int fd);

    int CloseRange(unsigned int first, unsigned int last, unsigned int flags);

    int Duplicate(int oldfd, int target_fd = -1, int flags = 0);

    void CloseOnExec();

  private:
    std::mutex m_mutex{};
    std::unordered_map<int, std::shared_ptr<FileDescriptor>> m_descriptors{};
    int m_next_fd{3};
};

} // namespace portage::fs
