#include "fs/inotify_state.hpp"

#include "syscall/syscall_dispatcher.hpp"

namespace portage::fs {

InotifyState::InotifyState(int flags)
    : m_flags(flags), m_wake_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      m_stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      m_watches_changed_event(CreateEventW(nullptr, FALSE, FALSE, nullptr))
{
    m_worker = std::thread(&InotifyState::WorkerLoop, this);
}

InotifyState::~InotifyState()
{
    m_stop_requested = true;
    if (m_stop_event) {
        SetEvent(m_stop_event);
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[wd, watch] : m_watches) {
        if (watch.dir_handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(watch.dir_handle, watch.overlapped.get());
            CloseHandle(watch.dir_handle);
            watch.dir_handle = INVALID_HANDLE_VALUE;
        }
        if (watch.overlap_event != INVALID_HANDLE_VALUE) {
            CloseHandle(watch.overlap_event);
            watch.overlap_event = INVALID_HANDLE_VALUE;
        }
    }
    m_watches.clear();

    if (m_wake_event) {
        CloseHandle(m_wake_event);
        m_wake_event = nullptr;
    }
    if (m_stop_event) {
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
    }
    if (m_watches_changed_event) {
        CloseHandle(m_watches_changed_event);
        m_watches_changed_event = nullptr;
    }
}

bool InotifyState::PushEvent(const InotifyRecord &rec) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ring_size >= kRingCap) {
        m_ring_head = (m_ring_head + 1) % kRingCap;
        m_ring_size--;

        InotifyRecord overflow_rec{};
        overflow_rec.wd = -1;
        overflow_rec.mask = IN_Q_OVERFLOW;
        overflow_rec.cookie = 0;
        overflow_rec.name[0] = '\0';
        m_ring[m_ring_tail] = overflow_rec;
        m_ring_tail = (m_ring_tail + 1) % kRingCap;
        m_ring_size++;

        SetEvent(m_wake_event);
        m_cv.notify_all();
        return false;
    }

    m_ring[m_ring_tail] = rec;
    m_ring_tail = (m_ring_tail + 1) % kRingCap;
    m_ring_size++;

    SetEvent(m_wake_event);
    m_cv.notify_all();
    return true;
}

bool InotifyState::PopEvent(InotifyRecord &out) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ring_size == 0) {
        return false;
    }
    out = m_ring[m_ring_head];
    m_ring_head = (m_ring_head + 1) % kRingCap;
    m_ring_size--;
    if (m_ring_size == 0) {
        ResetEvent(m_wake_event);
    }
    return true;
}

bool InotifyState::PeekEvent(InotifyRecord &out) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ring_size == 0) {
        return false;
    }
    out = m_ring[m_ring_head];
    return true;
}

bool InotifyState::IsEmpty() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ring_size == 0;
}

bool InotifyState::WaitForEvent(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_ring_size > 0) {
        return true;
    }
    if (m_stop_requested) {
        return false;
    }
    if (timeout_ms < 0) {
        m_cv.wait(lock, [this] { return m_ring_size > 0 || m_stop_requested; });
    } else {
        m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                      [this] { return m_ring_size > 0 || m_stop_requested; });
    }
    return m_ring_size > 0;
}

bool InotifyState::IssueRead(WatchEntry &entry)
{
    ResetEvent(entry.overlap_event);
    std::memset(entry.overlapped.get(), 0, sizeof(OVERLAPPED));
    entry.overlapped->hEvent = entry.overlap_event;

    BOOL ok = ReadDirectoryChangesW(
        entry.dir_handle, entry.buffer.data(), static_cast<DWORD>(entry.buffer.size()), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
        nullptr, entry.overlapped.get(), nullptr);

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    return true;
}

int32_t InotifyState::AddWatch(const std::wstring &host_path, uint32_t mask) noexcept
{
    std::error_code ec;
    bool is_directory = std::filesystem::is_directory(host_path, ec);
    std::wstring dir_to_open;
    std::wstring target_file;

    if (is_directory) {
        dir_to_open = host_path;
    } else if (std::filesystem::is_regular_file(host_path, ec)) {
        if (mask & IN_ONLYDIR) {
            return static_cast<int32_t>(portage::syscall::LINUX_ENOTDIR);
        }
        std::filesystem::path p(host_path);
        dir_to_open = p.parent_path().wstring();
        target_file = p.filename().wstring();
    } else {
        return static_cast<int32_t>(portage::syscall::LINUX_ENOENT);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &[existing_wd, existing_watch] : m_watches) {
            if (existing_watch.path == dir_to_open &&
                existing_watch.target_filename == target_file) {
                if (mask & IN_MASK_ADD) {
                    existing_watch.mask |= mask;
                } else {
                    existing_watch.mask = mask;
                }
                return existing_wd;
            }
        }
    }

    HANDLE dir_handle =
        CreateFileW(dir_to_open.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (dir_handle == INVALID_HANDLE_VALUE) {
        dir_handle =
            CreateFileW(dir_to_open.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    }

    if (dir_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            return static_cast<int32_t>(portage::syscall::LINUX_EACCES);
        }
        return static_cast<int32_t>(portage::syscall::LINUX_ENOENT);
    }

    HANDLE overlap_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlap_event) {
        CloseHandle(dir_handle);
        return static_cast<int32_t>(portage::syscall::LINUX_EIO);
    }

    int32_t wd = m_next_wd.fetch_add(1, std::memory_order_relaxed);

    WatchEntry entry{};
    entry.wd = wd;
    entry.path = dir_to_open;
    entry.target_filename = target_file;
    entry.mask = mask;
    entry.dir_handle = dir_handle;
    entry.overlap_event = overlap_event;
    entry.buffer.resize(8192);
    entry.is_dir = is_directory;

    if (!IssueRead(entry)) {
        CloseHandle(dir_handle);
        CloseHandle(overlap_event);
        return static_cast<int32_t>(portage::syscall::LINUX_EIO);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watches[wd] = std::move(entry);
    }

    SetEvent(m_watches_changed_event);
    return wd;
}

int InotifyState::RemoveWatch(int32_t wd) noexcept
{
    WatchEntry removed{};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_watches.find(wd);
        if (it == m_watches.end()) {
            return static_cast<int>(portage::syscall::LINUX_EINVAL);
        }
        removed = std::move(it->second);
        m_watches.erase(it);
    }

    if (removed.dir_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(removed.dir_handle, removed.overlapped.get());
        CloseHandle(removed.dir_handle);
    }
    if (removed.overlap_event != INVALID_HANDLE_VALUE) {
        CloseHandle(removed.overlap_event);
    }

    SetEvent(m_watches_changed_event);

    InotifyRecord rec{};
    rec.wd = wd;
    rec.mask = IN_IGNORED;
    rec.cookie = 0;
    rec.name[0] = '\0';
    PushEvent(rec);

    return 0;
}

void InotifyState::ProcessWatchNotification(int32_t wd)
{
    std::vector<InotifyRecord> events_to_push;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_watches.find(wd);
        if (it == m_watches.end()) {
            return;
        }

        WatchEntry &entry = it->second;
        DWORD bytes = 0;
        if (!GetOverlappedResult(entry.dir_handle, entry.overlapped.get(), &bytes, FALSE)) {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED || err == ERROR_INVALID_HANDLE ||
                err == ERROR_FILE_NOT_FOUND) {
                if (entry.mask & IN_DELETE_SELF) {
                    InotifyRecord rec{};
                    rec.wd = wd;
                    rec.mask = IN_DELETE_SELF;
                    events_to_push.push_back(rec);
                }
            }
            return;
        }

        if (bytes > 0) {
            uint8_t *ptr = entry.buffer.data();

            while (ptr < entry.buffer.data() + bytes) {
                auto *fni = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(ptr);
                size_t char_count = fni->FileNameLength / sizeof(WCHAR);
                std::wstring filename_w(fni->FileName, char_count);

                bool matches = true;
                if (!entry.target_filename.empty()) {
                    if (_wcsicmp(filename_w.c_str(), entry.target_filename.c_str()) != 0) {
                        matches = false;
                    }
                }

                if (matches) {
                    uint32_t mask = 0;
                    uint32_t cookie = 0;

                    switch (fni->Action) {
                    case FILE_ACTION_ADDED:
                        mask = IN_CREATE;
                        break;
                    case FILE_ACTION_REMOVED:
                        mask = IN_DELETE;
                        break;
                    case FILE_ACTION_MODIFIED:
                        mask = IN_MODIFY;
                        if (entry.mask & IN_CLOSE_WRITE) {
                            mask |= IN_CLOSE_WRITE;
                        }
                        if (entry.mask & IN_ATTRIB) {
                            mask |= IN_ATTRIB;
                        }
                        break;
                    case FILE_ACTION_RENAMED_OLD_NAME:
                        mask = IN_MOVED_FROM;
                        cookie = m_cookie_counter.fetch_add(1, std::memory_order_relaxed) + 1;
                        m_pending_cookie = cookie;
                        break;
                    case FILE_ACTION_RENAMED_NEW_NAME:
                        mask = IN_MOVED_TO;
                        if (m_pending_cookie != 0) {
                            cookie = m_pending_cookie;
                            m_pending_cookie = 0;
                        } else {
                            cookie = m_cookie_counter.fetch_add(1, std::memory_order_relaxed) + 1;
                        }
                        break;
                    default:
                        break;
                    }

                    uint32_t effective_mask = mask & entry.mask;
                    if (effective_mask != 0) {
                        InotifyRecord rec{};
                        rec.wd = wd;
                        rec.mask = effective_mask;
                        rec.cookie = cookie;

                        if (entry.target_filename.empty()) {
                            int u8_len = WideCharToMultiByte(
                                CP_UTF8, 0, filename_w.c_str(), static_cast<int>(filename_w.size()),
                                rec.name, sizeof(rec.name) - 1, nullptr, nullptr);
                            if (u8_len >= 0 && u8_len < static_cast<int>(sizeof(rec.name))) {
                                rec.name[u8_len] = '\0';
                            }
                        } else {
                            rec.name[0] = '\0';
                        }

                        if (entry.target_filename.empty() && rec.name[0] != '\0') {
                            std::error_code ec;
                            std::filesystem::path full =
                                std::filesystem::path(entry.path) / filename_w;
                            if (std::filesystem::is_directory(full, ec)) {
                                rec.mask |= IN_ISDIR;
                            }
                        }

                        events_to_push.push_back(rec);
                    }
                }

                if (fni->NextEntryOffset == 0) {
                    break;
                }
                ptr += fni->NextEntryOffset;
            }
        }

        IssueRead(entry);
    }

    for (const auto &ev : events_to_push) {
        PushEvent(ev);
    }
}

void InotifyState::WorkerLoop()
{
    while (!m_stop_requested) {
        std::vector<HANDLE> wait_handles;
        std::vector<int32_t> handle_wds;

        wait_handles.push_back(m_stop_event);
        wait_handles.push_back(m_watches_changed_event);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &[wd, watch] : m_watches) {
                if (watch.overlap_event != INVALID_HANDLE_VALUE && wait_handles.size() < 64) {
                    wait_handles.push_back(watch.overlap_event);
                    handle_wds.push_back(wd);
                }
            }
        }

        DWORD wait_res = WaitForMultipleObjects(static_cast<DWORD>(wait_handles.size()),
                                                wait_handles.data(), FALSE, INFINITE);

        if (wait_res == WAIT_OBJECT_0) {
            break;
        }

        if (wait_res == WAIT_OBJECT_0 + 1) {
            continue;
        }

        if (wait_res > WAIT_OBJECT_0 + 1 && wait_res < WAIT_OBJECT_0 + wait_handles.size()) {
            size_t watch_idx = (wait_res - WAIT_OBJECT_0) - 2;
            int32_t signaled_wd = handle_wds[watch_idx];
            ProcessWatchNotification(signaled_wd);
        }
    }
}

} // namespace portage::fs
