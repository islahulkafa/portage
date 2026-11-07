#pragma once

#include "signal_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mutex>
#include <unordered_map>

namespace portage::signal {

class SignalManager {
  public:
    static SignalManager &Instance() noexcept;

    int SetAction(int sig, const LinuxSigaction &act, LinuxSigaction *old_act) noexcept;
    int GetAction(int sig, LinuxSigaction *act) noexcept;

    bool DeliverFault(PEXCEPTION_POINTERS ExceptionInfo) noexcept;
    bool RestoreFromContext(const LinuxUcontext *uc, PCONTEXT win32_ctx) noexcept;
    uintptr_t GetDefaultRestorer() noexcept;

  private:
    SignalManager() = default;
    std::mutex m_mutex{};
    std::unordered_map<int, LinuxSigaction> m_actions{};
    void *m_default_restorer{nullptr};
};

} // namespace portage::signal
