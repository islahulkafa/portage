#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace portage::thread {

constexpr uint64_t LINUX_CLONE_VM = 0x00000100;
constexpr uint64_t LINUX_CLONE_FS = 0x00000200;
constexpr uint64_t LINUX_CLONE_FILES = 0x00000400;
constexpr uint64_t LINUX_CLONE_SIGHAND = 0x00000800;
constexpr uint64_t LINUX_CLONE_THREAD = 0x00010000;
constexpr uint64_t LINUX_CLONE_SYSVSEM = 0x00040000;
constexpr uint64_t LINUX_CLONE_SETTLS = 0x00080000;
constexpr uint64_t LINUX_CLONE_PARENT_SETTID = 0x00100000;
constexpr uint64_t LINUX_CLONE_CHILD_CLEARTID = 0x00200000;
constexpr uint64_t LINUX_CLONE_DETACHED = 0x00400000;
constexpr uint64_t LINUX_CLONE_CHILD_SETTID = 0x01000000;

struct GuestThread {
    int tid{0};
    HANDLE win32_handle{INVALID_HANDLE_VALUE};
    DWORD win32_thread_id{0};
    uintptr_t child_stack{0};
    uintptr_t tcb{0};
    uintptr_t clear_child_tid{0};
    uint64_t sigmask{0};
    std::atomic<uint64_t> sigpending{0};
    bool is_exited{false};
    int exit_code{0};
};

class ThreadManager {
  public:
    static ThreadManager &Instance() noexcept;

    int RegisterMainThread() noexcept;

    int CreateGuestThread(uint64_t flags, uintptr_t child_stack, uintptr_t parent_tid_ptr,
                          uintptr_t child_tid_ptr, uintptr_t tls, uintptr_t child_rip) noexcept;

    void OnThreadExit(int tid, int exit_code) noexcept;

    void SetClearChildTid(int tid, uintptr_t ctid_ptr) noexcept;

    [[nodiscard]] std::shared_ptr<GuestThread> GetThread(int tid) noexcept;
    [[nodiscard]] std::shared_ptr<GuestThread> GetCurrentGuestThread() noexcept;

    int GetCurrentTid() noexcept;

  private:
    ThreadManager() = default;

    std::mutex m_mutex{};
    std::unordered_map<int, std::shared_ptr<GuestThread>> m_threads{};
    std::unordered_map<DWORD, int> m_win32_to_tid{};
    std::atomic<int> m_next_tid{1000};
};

} // namespace portage::thread
