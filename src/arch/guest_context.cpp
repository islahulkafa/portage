#include "guest_context.hpp"
#include "signal/signal_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <algorithm>
#include <cstring>
#include <immintrin.h>
#include <iostream>
#include <windows.h>

namespace portage::arch {

namespace {

thread_local uintptr_t t_active_tcb = 0;
thread_local HostSavedContext t_host_ctx{};
thread_local bool t_has_host_ctx = false;

LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;

    // Ignore debugger print exceptions from Windows DLLs
    if (code == 0x40010006 || code == 0x4001000A) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Recover guest FS base on Access Violation
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault_addr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
        if (fault_addr < 0x1000 && t_active_tcb != 0) {
            _writefsbase_u64(t_active_tcb);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    // Translate hardware faults to Linux signals (SIGSEGV, SIGILL, SIGFPE)
    if (signal::SignalManager::Instance().DeliverFault(ExceptionInfo)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Syscall trap fallback via INT 3 (0xCC)
    if (code == EXCEPTION_BREAKPOINT) {
        auto *ctx = ExceptionInfo->ContextRecord;
        uint8_t *exc_addr =
            reinterpret_cast<uint8_t *>(ExceptionInfo->ExceptionRecord->ExceptionAddress);
        if (exc_addr && *exc_addr == 0xCC) {
            uint64_t nr = ctx->Rax;
            uint64_t a1 = ctx->Rdi;
            uint64_t a2 = ctx->Rsi;
            uint64_t a3 = ctx->Rdx;
            uint64_t a4 = ctx->R10;
            uint64_t a5 = ctx->R8;
            uint64_t a6 = ctx->R9;

            // Advance Rip past 0xCC and optional trailing NOP (0x90)
            uint8_t *next_ip = exc_addr + 1;
            if (*next_ip == 0x90) {
                next_ip++;
            }

            // sys_rt_sigreturn (15) restores machine context from guest stack
            if (nr == 15) {
                const auto *uc = reinterpret_cast<const signal::LinuxUcontext *>(ctx->Rsp);
                if (signal::SignalManager::Instance().RestoreFromContext(uc, ctx)) {
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                uc = reinterpret_cast<const signal::LinuxUcontext *>(ctx->Rsp + 8);
                if (signal::SignalManager::Instance().RestoreFromContext(uc, ctx)) {
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            // For sys_clone (56), provide next_ip in a6 so child thread resumes after syscall
            if (nr == 56) {
                a6 = reinterpret_cast<uint64_t>(next_ip);
            }

            int64_t ret = portage_syscall_dispatch(nr, a1, a2, a3, a4, a5, a6);
            if (nr == 60 || nr == 231) {
                ctx->Rip = reinterpret_cast<DWORD64>(&portage_guest_exit_trampoline);
                ctx->Rcx = static_cast<DWORD64>(a1);
                ctx->Rdi = static_cast<DWORD64>(a1);
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            ctx->Rax = static_cast<DWORD64>(ret);
            ctx->Rip = reinterpret_cast<DWORD64>(next_ip);

            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

// Low-level assembly context switch
extern "C" __attribute__((naked)) int portage_save_and_enter(void *entry_point, void *guest_rsp,
                                                             HostSavedContext *ctx)
{
    __asm__ volatile(
        // Save host callee-saved registers into ctx (r8)
        "movq %rbx, 0(%r8)\n\t"
        "movq %rbp, 8(%r8)\n\t"
        "movq %r12, 16(%r8)\n\t"
        "movq %r13, 24(%r8)\n\t"
        "movq %r14, 32(%r8)\n\t"
        "movq %r15, 40(%r8)\n\t"
        "movq %rsp, 48(%r8)\n\t"

        // Switch to guest stack
        "movq %rdx, %rsp\n\t"

        // Clear registers for guest per System V AMD64 ABI specification
        "xorq %rdx, %rdx\n\t"
        "xorq %rax, %rax\n\t"
        "xorq %rbx, %rbx\n\t"
        "xorq %rsi, %rsi\n\t"
        "xorq %rdi, %rdi\n\t"
        "xorq %rbp, %rbp\n\t"
        "xorq %r8, %r8\n\t"
        "xorq %r9, %r9\n\t"
        "xorq %r10, %r10\n\t"
        "xorq %r11, %r11\n\t"
        "xorq %r12, %r12\n\t"
        "xorq %r13, %r13\n\t"
        "xorq %r14, %r14\n\t"
        "xorq %r15, %r15\n\t"

        // Jump to guest entry point
        "jmpq *%rcx\n\t");
}

// Low-level assembly context restore
extern "C" __attribute__((naked)) void portage_restore_and_exit(HostSavedContext *ctx,
                                                                int exit_code)
{
    __asm__ volatile(
        // Restore host callee-saved registers from ctx (rcx)
        "movq 0(%rcx), %rbx\n\t"
        "movq 8(%rcx), %rbp\n\t"
        "movq 16(%rcx), %r12\n\t"
        "movq 24(%rcx), %r13\n\t"
        "movq 32(%rcx), %r14\n\t"
        "movq 40(%rcx), %r15\n\t"
        "movq 48(%rcx), %rsp\n\t"

        // Return exit_code in eax
        "movl %edx, %eax\n\t"

        // Return cleanly to the caller of portage_save_and_enter!
        "ret\n\t");
}

void RestoreActiveTcb() noexcept
{
    if (t_active_tcb != 0) {
        _writefsbase_u64(t_active_tcb);
    }
}

void SetActiveTcb(uintptr_t tcb) noexcept
{
    t_active_tcb = tcb;
    if (tcb != 0) {
        _writefsbase_u64(tcb);
    }
}

HostSavedContext *GetThreadHostContext() noexcept { return &t_host_ctx; }

void SetThreadHostContextActive(bool active) noexcept { t_has_host_ctx = active; }

void SignalGuestExit(int code) noexcept
{
    if (t_has_host_ctx) {
        t_has_host_ctx = false;
        __writegsqword(0x08, t_host_ctx.stack_base);
        __writegsqword(0x10, t_host_ctx.stack_limit);
        __writegsqword(0x1478, t_host_ctx.deallocation_stack);
        portage_restore_and_exit(&t_host_ctx, code);
    }
}

extern "C" void portage_guest_exit_trampoline(int code)
{
    SignalGuestExit(code);
    ExitThread(static_cast<DWORD>(code));
}

GuestContext::GuestContext() noexcept = default;

GuestContext::~GuestContext() noexcept
{
    if (veh_handle_) {
        RemoveVectoredExceptionHandler(veh_handle_);
        veh_handle_ = nullptr;
    }
    if (stack_base_) {
        VirtualFree(stack_base_, 0, MEM_RELEASE);
        stack_base_ = nullptr;
    }
}

GuestContext::GuestContext(GuestContext &&other) noexcept
    : stack_base_(other.stack_base_), stack_size_(other.stack_size_), veh_handle_(other.veh_handle_)
{
    other.stack_base_ = nullptr;
    other.stack_size_ = 0;
    other.veh_handle_ = nullptr;
}

GuestContext &GuestContext::operator=(GuestContext &&other) noexcept
{
    if (this != &other) {
        if (veh_handle_) {
            RemoveVectoredExceptionHandler(veh_handle_);
        }
        if (stack_base_) {
            VirtualFree(stack_base_, 0, MEM_RELEASE);
        }
        stack_base_ = other.stack_base_;
        stack_size_ = other.stack_size_;
        veh_handle_ = other.veh_handle_;

        other.stack_base_ = nullptr;
        other.stack_size_ = 0;
        other.veh_handle_ = nullptr;
    }
    return *this;
}

bool GuestContext::InitializeStack(size_t stack_size) noexcept
{
    stack_size_ = stack_size;
    stack_base_ = VirtualAlloc(nullptr, stack_size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!stack_base_) {
        return false;
    }
    veh_handle_ = AddVectoredExceptionHandler(1, VectoredHandler);
    return veh_handle_ != nullptr;
}

void *GuestContext::SetupStack(const std::vector<std::string> &args,
                               const std::vector<std::string> &env,
                               std::span<const AuxvEntry> auxv_entries) noexcept
{
    if (!stack_base_ || stack_size_ == 0) {
        return nullptr;
    }

    uint8_t *stack_top = static_cast<uint8_t *>(stack_base_) + stack_size_;
    uint8_t *sp = stack_top;

    std::vector<uintptr_t> arg_ptrs(args.size());
    std::vector<uintptr_t> env_ptrs(env.size());

    // Copy platform string: "x86_64"
    const char platform_str[] = "x86_64";
    sp -= sizeof(platform_str);
    std::memcpy(sp, platform_str, sizeof(platform_str));
    uintptr_t platform_ptr = reinterpret_cast<uintptr_t>(sp);

    // 16 random bytes for AT_RANDOM
    const uint8_t random_bytes[16] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                                      0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    sp -= 16;
    std::memcpy(sp, random_bytes, 16);
    uintptr_t random_ptr = reinterpret_cast<uintptr_t>(sp);

    // Copy env strings
    for (size_t i = 0; i < env.size(); i++) {
        size_t len = env[i].size() + 1;
        sp -= len;
        std::memcpy(sp, env[i].c_str(), len);
        env_ptrs[i] = reinterpret_cast<uintptr_t>(sp);
    }

    // Copy arg strings
    for (size_t i = 0; i < args.size(); i++) {
        size_t len = args[i].size() + 1;
        sp -= len;
        std::memcpy(sp, args[i].c_str(), len);
        arg_ptrs[i] = reinterpret_cast<uintptr_t>(sp);
    }

    // Align down to 16 bytes
    uintptr_t sp_val = reinterpret_cast<uintptr_t>(sp);
    sp_val &= ~0xFULL;
    sp = reinterpret_cast<uint8_t *>(sp_val);

    size_t pointer_slots = 1 + args.size() + 1 + env.size() + 1 + (auxv_entries.size() + 2 + 1) * 2;
    if (pointer_slots % 2 != 0) {
        pointer_slots++;
    }

    sp -= pointer_slots * sizeof(uint64_t);
    uint64_t *stack_ptr = reinterpret_cast<uint64_t *>(sp);
    size_t idx = 0;

    // argc
    stack_ptr[idx++] = args.size();

    // argv pointers
    for (uintptr_t p : arg_ptrs) {
        stack_ptr[idx++] = p;
    }
    stack_ptr[idx++] = 0; // NULL

    // envp pointers
    for (uintptr_t p : env_ptrs) {
        stack_ptr[idx++] = p;
    }
    stack_ptr[idx++] = 0; // NULL

    // auxv entries
    for (const auto &entry : auxv_entries) {
        stack_ptr[idx++] = entry.type;
        stack_ptr[idx++] = entry.val;
    }
    stack_ptr[idx++] = elf::AT_RANDOM;
    stack_ptr[idx++] = random_ptr;
    stack_ptr[idx++] = elf::AT_PLATFORM;
    stack_ptr[idx++] = platform_ptr;
    stack_ptr[idx++] = elf::AT_NULL;
    stack_ptr[idx++] = 0;

    return sp;
}

int GuestContext::Run(void *entry_point, void *guest_rsp) noexcept
{
    t_has_host_ctx = true;
    t_host_ctx.stack_base = __readgsqword(0x08);
    t_host_ctx.stack_limit = __readgsqword(0x10);
    t_host_ctx.deallocation_stack = __readgsqword(0x1478);

    uintptr_t g_stack_limit = reinterpret_cast<uintptr_t>(stack_base_);
    uintptr_t g_stack_base = g_stack_limit + stack_size_;

    __writegsqword(0x08, g_stack_base);
    __writegsqword(0x10, g_stack_limit);
    __writegsqword(0x1478, g_stack_limit);

    int ret = portage_save_and_enter(entry_point, guest_rsp, &t_host_ctx);

    __writegsqword(0x08, t_host_ctx.stack_base);
    __writegsqword(0x10, t_host_ctx.stack_limit);
    __writegsqword(0x1478, t_host_ctx.deallocation_stack);
    return ret;
}

} // namespace portage::arch
