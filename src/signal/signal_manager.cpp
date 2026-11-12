#include "signal/signal_manager.hpp"
#include "fs/fd_table.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

namespace portage::signal {

SignalManager &SignalManager::Instance() noexcept
{
    static SignalManager instance;
    return instance;
}

int SignalManager::SetAction(int sig, const LinuxSigaction &act, LinuxSigaction *old_act) noexcept
{
    if (sig <= 0 || sig > 64 || sig == LINUX_SIGKILL || sig == LINUX_SIGSTOP) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (old_act) {
        auto it = m_actions.find(sig);
        if (it != m_actions.end()) {
            *old_act = it->second;
        } else {
            *old_act = LinuxSigaction{};
        }
    }

    m_actions[sig] = act;
    return 0;
}

int SignalManager::GetAction(int sig, LinuxSigaction *act) noexcept
{
    if (sig <= 0 || sig > 64 || !act) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_actions.find(sig);
    if (it != m_actions.end()) {
        *act = it->second;
    } else {
        *act = LinuxSigaction{};
    }
    return 0;
}

bool SignalManager::DeliverFault(PEXCEPTION_POINTERS ExceptionInfo) noexcept
{
    if (!ExceptionInfo || !ExceptionInfo->ExceptionRecord || !ExceptionInfo->ContextRecord) {
        return false;
    }

    DWORD exc_code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    int sig = 0;
    uintptr_t fault_addr = 0;

    if (exc_code == EXCEPTION_ACCESS_VIOLATION) {
        fault_addr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
        if (fault_addr < 0x1000) {
            // Handled by TLS FS.BASE recovery
            return false;
        }
        sig = LINUX_SIGSEGV;
    } else if (exc_code == EXCEPTION_ILLEGAL_INSTRUCTION) {
        sig = LINUX_SIGILL;
        fault_addr = ExceptionInfo->ContextRecord->Rip;
    } else if (exc_code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
               exc_code == EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        sig = LINUX_SIGFPE;
        fault_addr = ExceptionInfo->ContextRecord->Rip;
    } else {
        return false;
    }

    LinuxSigaction act{};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_actions.find(sig);
        if (it == m_actions.end()) {
            return false;
        }
        act = it->second;
    }

    if (act.sa_handler == LINUX_SIG_DFL || act.sa_handler == 0) {
        return false;
    }

    if (act.sa_handler == LINUX_SIG_IGN) {
        return true;
    }

    auto *ctx = ExceptionInfo->ContextRecord;

    // Allocate LinuxRtSigframe on the guest stack
    uintptr_t sp = ctx->Rsp;
    sp = (sp - sizeof(LinuxRtSigframe)) & ~0xFULL;
    sp -= 8;

    auto *frame = reinterpret_cast<LinuxRtSigframe *>(sp);
    std::memset(frame, 0, sizeof(LinuxRtSigframe));

    if ((act.sa_flags & LINUX_SA_RESTORER) && act.sa_restorer != 0) {
        frame->pretcode = act.sa_restorer;
    } else {
        frame->pretcode = GetDefaultRestorer();
    }

    // Save interrupted machine context
    auto &mc = frame->uc.uc_mcontext;
    mc.r8 = ctx->R8;
    mc.r9 = ctx->R9;
    mc.r10 = ctx->R10;
    mc.r11 = ctx->R11;
    mc.r12 = ctx->R12;
    mc.r13 = ctx->R13;
    mc.r14 = ctx->R14;
    mc.r15 = ctx->R15;
    mc.rdi = ctx->Rdi;
    mc.rsi = ctx->Rsi;
    mc.rbp = ctx->Rbp;
    mc.rbx = ctx->Rbx;
    mc.rdx = ctx->Rdx;
    mc.rax = ctx->Rax;
    mc.rcx = ctx->Rcx;
    mc.rsp = ctx->Rsp;
    mc.rip = ctx->Rip;
    mc.eflags = ctx->EFlags;

    // Fill siginfo
    frame->info.si_signo = sig;
    frame->info.si_code = 1; // SEGV_MAPERR
    frame->info._sifields.sigfault.si_addr = fault_addr;

    // Redirect CPU context to signal handler
    ctx->Rsp = sp;
    ctx->Rip = act.sa_handler;
    ctx->Rdi = static_cast<DWORD64>(sig);
    ctx->Rsi = reinterpret_cast<DWORD64>(&frame->info);
    ctx->Rdx = reinterpret_cast<DWORD64>(&frame->uc);
    ctx->ContextFlags &= ~0x40ULL; // clear CONTEXT_XSTATE
    ctx->EFlags &= ~0x10000ULL;    // clear RF

    return true;
}

bool SignalManager::RestoreFromContext(const LinuxUcontext *uc, PCONTEXT win32_ctx) noexcept
{
    if (!uc || !win32_ctx) {
        return false;
    }

    const auto &mc = uc->uc_mcontext;
    win32_ctx->R8 = mc.r8;
    win32_ctx->R9 = mc.r9;
    win32_ctx->R10 = mc.r10;
    win32_ctx->R11 = mc.r11;
    win32_ctx->R12 = mc.r12;
    win32_ctx->R13 = mc.r13;
    win32_ctx->R14 = mc.r14;
    win32_ctx->R15 = mc.r15;
    win32_ctx->Rdi = mc.rdi;
    win32_ctx->Rsi = mc.rsi;
    win32_ctx->Rbp = mc.rbp;
    win32_ctx->Rbx = mc.rbx;
    win32_ctx->Rdx = mc.rdx;
    win32_ctx->Rax = mc.rax;
    win32_ctx->Rcx = mc.rcx;
    win32_ctx->Rsp = mc.rsp;
    win32_ctx->Rip = mc.rip;
    win32_ctx->EFlags = static_cast<DWORD>(mc.eflags);
    win32_ctx->ContextFlags &= ~0x40ULL;
    win32_ctx->EFlags &= ~0x10000ULL;

    return true;
}

uintptr_t SignalManager::GetDefaultRestorer() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_default_restorer) {
        return reinterpret_cast<uintptr_t>(m_default_restorer);
    }
    void *mem = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) {
        return 0;
    }
    // mov $15, %rax (48 c7 c0 0f 00 00 00)
    // int3; nop (cc 90)
    const uint8_t stub[] = {0x48, 0xc7, 0xc0, 0x0f, 0x00, 0x00, 0x00, 0xcc, 0x90};
    std::memcpy(mem, stub, sizeof(stub));
    m_default_restorer = mem;
    return reinterpret_cast<uintptr_t>(m_default_restorer);
}

void SignalManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_actions.clear();
    m_process_signals.clear();
    m_process_pending_mask = 0;
    m_thread_signals.clear();
    m_thread_pending_masks.clear();
    for (auto *sfd : m_signalfds) {
        if (sfd && sfd->wake_event) {
            ResetEvent(sfd->wake_event);
        }
    }
    m_signal_cv_.notify_all();
}

void SignalManager::RegisterSignalfd(fs::SignalfdState *sfd) noexcept
{
    if (!sfd) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (std::find(m_signalfds.begin(), m_signalfds.end(), sfd) == m_signalfds.end()) {
        m_signalfds.push_back(sfd);
    }
    if (HasPendingSignalLocked(sfd->mask, 0) && sfd->wake_event) {
        SetEvent(sfd->wake_event);
    }
}

void SignalManager::UnregisterSignalfd(fs::SignalfdState *sfd) noexcept
{
    if (!sfd) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::remove(m_signalfds.begin(), m_signalfds.end(), sfd);
    m_signalfds.erase(it, m_signalfds.end());
}

bool SignalManager::HasPendingSignalLocked(uint64_t mask, int tid) const noexcept
{
    if ((m_process_pending_mask & mask) != 0) {
        return true;
    }
    if (tid > 0) {
        auto it = m_thread_pending_masks.find(tid);
        if (it != m_thread_pending_masks.end() && (it->second & mask) != 0) {
            return true;
        }
    } else {
        for (const auto &[t, tmask] : m_thread_pending_masks) {
            (void)t;
            if ((tmask & mask) != 0) {
                return true;
            }
        }
    }
    return false;
}

bool SignalManager::HasPendingSignal(uint64_t mask, int tid) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return HasPendingSignalLocked(mask, tid);
}

void SignalManager::RefreshSignalfdsLocked() noexcept
{
    for (auto *sfd : m_signalfds) {
        if (!sfd || !sfd->wake_event) {
            continue;
        }
        if (HasPendingSignalLocked(sfd->mask, 0)) {
            SetEvent(sfd->wake_event);
        } else {
            ResetEvent(sfd->wake_event);
        }
    }
}

int SignalManager::SendSignal(int pid, int tid, int sig, const LinuxSiginfo *info) noexcept
{
    (void)pid;
    if (sig == 0) {
        return 0;
    }
    if (sig < 1 || sig > 64) {
        return -22; // -EINVAL
    }

    QueuedSignal qs{};
    qs.sig = sig;
    if (info) {
        qs.info = *info;
        qs.info.si_signo = sig;
    } else {
        qs.info.si_signo = sig;
        qs.info.si_code = LINUX_SI_USER;
        qs.info._sifields.rt.si_pid = static_cast<int32_t>(GetCurrentProcessId());
    }

    uint64_t bit = 1ULL << (sig - 1);
    std::lock_guard<std::mutex> lock(m_mutex);

    if (tid > 0) {
        auto &q = m_thread_signals[tid];
        uint64_t &tmask = m_thread_pending_masks[tid];
        if (sig < LINUX_SIGRTMIN) {
            if ((tmask & bit) == 0) {
                q.push_back(qs);
                tmask |= bit;
            }
        } else {
            if (q.size() < 1024) {
                q.push_back(qs);
                tmask |= bit;
            }
        }
    } else {
        if (sig < LINUX_SIGRTMIN) {
            if ((m_process_pending_mask & bit) == 0) {
                m_process_signals.push_back(qs);
                m_process_pending_mask |= bit;
            }
        } else {
            if (m_process_signals.size() < 1024) {
                m_process_signals.push_back(qs);
                m_process_pending_mask |= bit;
            }
        }
    }

    m_signal_cv_.notify_all();
    RefreshSignalfdsLocked();
    return 0;
}

int SignalManager::DequeueSignalLocked(int tid, uint64_t mask, LinuxSiginfo *out_info) noexcept
{
    uint64_t combined = m_process_pending_mask;
    if (tid > 0) {
        auto it = m_thread_pending_masks.find(tid);
        if (it != m_thread_pending_masks.end()) {
            combined |= it->second;
        }
    } else {
        for (const auto &[t, tmask] : m_thread_pending_masks) {
            (void)t;
            combined |= tmask;
        }
    }

    uint64_t ready = combined & mask;
    if (ready == 0) {
        return 0;
    }

    int target_sig = 0;
    for (int s = 1; s <= 64; ++s) {
        if (ready & (1ULL << (s - 1))) {
            target_sig = s;
            break;
        }
    }

    uint64_t bit = 1ULL << (target_sig - 1);

    auto pop_from_queue = [&](std::deque<QueuedSignal> &q, uint64_t &pmask) -> bool {
        if ((pmask & bit) == 0) {
            return false;
        }
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (it->sig == target_sig) {
                if (out_info) {
                    *out_info = it->info;
                }
                q.erase(it);
                bool still_present = false;
                for (const auto &rem : q) {
                    if (rem.sig == target_sig) {
                        still_present = true;
                        break;
                    }
                }
                if (!still_present) {
                    pmask &= ~bit;
                }
                return true;
            }
        }
        pmask &= ~bit;
        return false;
    };

    bool dequeued = false;
    if (tid > 0) {
        auto it = m_thread_signals.find(tid);
        if (it != m_thread_signals.end()) {
            dequeued = pop_from_queue(it->second, m_thread_pending_masks[tid]);
        }
    } else {
        for (auto &[t, q] : m_thread_signals) {
            if (pop_from_queue(q, m_thread_pending_masks[t])) {
                dequeued = true;
                break;
            }
        }
    }

    if (!dequeued) {
        dequeued = pop_from_queue(m_process_signals, m_process_pending_mask);
    }

    if (dequeued) {
        RefreshSignalfdsLocked();
        return target_sig;
    }
    return 0;
}

int SignalManager::DequeueSignal(int tid, uint64_t mask, LinuxSiginfo *out_info) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return DequeueSignalLocked(tid, mask, out_info);
}

int SignalManager::TimedWait(int tid, uint64_t mask, LinuxSiginfo *out_info,
                             int64_t timeout_ns) noexcept
{
    std::unique_lock<std::mutex> lock(m_mutex);
    int sig = DequeueSignalLocked(tid, mask, out_info);
    if (sig > 0) {
        return sig;
    }

    if (timeout_ns == 0) {
        return -11; // -EAGAIN
    }

    if (timeout_ns < 0) {
        m_signal_cv_.wait(lock, [&]() { return HasPendingSignalLocked(mask, tid); });
    } else {
        m_signal_cv_.wait_for(lock, std::chrono::nanoseconds(timeout_ns),
                              [&]() { return HasPendingSignalLocked(mask, tid); });
    }

    sig = DequeueSignalLocked(tid, mask, out_info);
    if (sig > 0) {
        return sig;
    }
    return -11; // -EAGAIN
}

} // namespace portage::signal
