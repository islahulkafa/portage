#pragma once

#include "signal_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace portage::fs {
struct SignalfdState;
}

namespace portage::signal {

struct QueuedSignal {
    int sig{0};
    LinuxSiginfo info{};
};

class SignalManager {
  public:
    static SignalManager &Instance() noexcept;

    void Reset() noexcept;

    int SetAction(int sig, const LinuxSigaction &act, LinuxSigaction *old_act) noexcept;
    int GetAction(int sig, LinuxSigaction *act) noexcept;

    int SendSignal(int pid, int tid, int sig, const LinuxSiginfo *info = nullptr) noexcept;
    int DequeueSignal(int tid, uint64_t mask, LinuxSiginfo *out_info) noexcept;
    int TimedWait(int tid, uint64_t mask, LinuxSiginfo *out_info, int64_t timeout_ns) noexcept;

    void RegisterSignalfd(fs::SignalfdState *sfd) noexcept;
    void UnregisterSignalfd(fs::SignalfdState *sfd) noexcept;
    [[nodiscard]] bool HasPendingSignal(uint64_t mask, int tid = 0) noexcept;

    bool DeliverFault(PEXCEPTION_POINTERS ExceptionInfo) noexcept;
    bool RestoreFromContext(const LinuxUcontext *uc, PCONTEXT win32_ctx) noexcept;
    uintptr_t GetDefaultRestorer() noexcept;

  private:
    SignalManager() = default;

    [[nodiscard]] bool HasPendingSignalLocked(uint64_t mask, int tid) const noexcept;
    int DequeueSignalLocked(int tid, uint64_t mask, LinuxSiginfo *out_info) noexcept;
    void RefreshSignalfdsLocked() noexcept;

    std::mutex m_mutex{};
    std::condition_variable m_signal_cv_{};
    std::unordered_map<int, LinuxSigaction> m_actions{};
    void *m_default_restorer{nullptr};

    std::deque<QueuedSignal> m_process_signals{};
    uint64_t m_process_pending_mask{0};
    std::unordered_map<int, std::deque<QueuedSignal>> m_thread_signals{};
    std::unordered_map<int, uint64_t> m_thread_pending_masks{};
    std::vector<fs::SignalfdState *> m_signalfds{};
};

} // namespace portage::signal
