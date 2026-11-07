#include "signal/signal_manager.hpp"
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

} // namespace portage::signal
