#pragma once

#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portage::fs {

// Linux inotify_event wire format (variable-length, name[] is padded to 4-byte boundary)
struct LinuxInotifyEvent {
    int32_t wd{0};
    uint32_t mask{0};
    uint32_t cookie{0};
    uint32_t len{0};
};
static_assert(sizeof(LinuxInotifyEvent) == 16, "LinuxInotifyEvent must be exactly 16 bytes");

// Linux inotify mask bits
constexpr uint32_t IN_ACCESS = 0x00000001;
constexpr uint32_t IN_MODIFY = 0x00000002;
constexpr uint32_t IN_ATTRIB = 0x00000004;
constexpr uint32_t IN_CLOSE_WRITE = 0x00000008;
constexpr uint32_t IN_CLOSE_NOWRITE = 0x00000010;
constexpr uint32_t IN_OPEN = 0x00000020;
constexpr uint32_t IN_MOVED_FROM = 0x00000040;
constexpr uint32_t IN_MOVED_TO = 0x00000080;
constexpr uint32_t IN_CREATE = 0x00000100;
constexpr uint32_t IN_DELETE = 0x00000200;
constexpr uint32_t IN_DELETE_SELF = 0x00000400;
constexpr uint32_t IN_MOVE_SELF = 0x00000800;

constexpr uint32_t IN_UNMOUNT = 0x00002000;
constexpr uint32_t IN_Q_OVERFLOW = 0x00004000;
constexpr uint32_t IN_IGNORED = 0x00008000;

constexpr uint32_t IN_ONLYDIR = 0x01000000;
constexpr uint32_t IN_DONT_FOLLOW = 0x02000000;
constexpr uint32_t IN_EXCL_UNLINK = 0x04000000;
constexpr uint32_t IN_MASK_CREATE = 0x10000000;
constexpr uint32_t IN_MASK_ADD = 0x20000000;
constexpr uint32_t IN_ISDIR = 0x40000000;
constexpr uint32_t IN_ONESHOT = 0x80000000;

// inotify_init1 flags
constexpr int LINUX_IN_CLOEXEC = 02000000;
constexpr int LINUX_IN_NONBLOCK = 00004000;

// Stored event record
struct InotifyRecord {
    int32_t wd{0};
    uint32_t mask{0};
    uint32_t cookie{0};
    char name[256]{};
};

struct WatchEntry {
    int32_t wd{0};
    std::wstring path{};
    std::wstring target_filename{};
    uint32_t mask{0};
    HANDLE dir_handle{INVALID_HANDLE_VALUE};
    HANDLE overlap_event{INVALID_HANDLE_VALUE};
    std::shared_ptr<OVERLAPPED> overlapped{std::make_shared<OVERLAPPED>()};
    std::vector<uint8_t> buffer{};
    bool is_dir{true};
};

class InotifyState {
  public:
    explicit InotifyState(int flags);
    ~InotifyState();

    InotifyState(const InotifyState &) = delete;
    InotifyState &operator=(const InotifyState &) = delete;

    [[nodiscard]] int GetFlags() const noexcept { return m_flags; }
    [[nodiscard]] HANDLE GetWakeEvent() const noexcept { return m_wake_event; }

    bool PushEvent(const InotifyRecord &rec) noexcept;
    bool PopEvent(InotifyRecord &out) noexcept;
    bool PeekEvent(InotifyRecord &out) noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    bool WaitForEvent(int timeout_ms);

    int32_t AddWatch(const std::wstring &host_path, uint32_t mask) noexcept;
    int RemoveWatch(int32_t wd) noexcept;

  private:
    void WorkerLoop();
    void ProcessWatchNotification(int32_t wd);
    bool IssueRead(WatchEntry &entry);

    int m_flags{0};
    HANDLE m_wake_event{nullptr};
    HANDLE m_stop_event{nullptr};
    HANDLE m_watches_changed_event{nullptr};

    mutable std::mutex m_mutex{};
    std::condition_variable m_cv{};
    std::unordered_map<int32_t, WatchEntry> m_watches{};
    std::atomic<int32_t> m_next_wd{1};
    std::atomic<uint32_t> m_cookie_counter{1};
    uint32_t m_pending_cookie{0};

    static constexpr size_t kRingCap = 512;
    InotifyRecord m_ring[kRingCap]{};
    size_t m_ring_head{0};
    size_t m_ring_tail{0};
    size_t m_ring_size{0};

    std::atomic<bool> m_stop_requested{false};
    std::thread m_worker{};
};

} // namespace portage::fs
