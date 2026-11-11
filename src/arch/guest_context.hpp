#pragma once

#include "elf/elf_types.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace portage::arch {

constexpr size_t DEFAULT_STACK_SIZE = 8 * 1024 * 1024; // 8 MB

struct HostSavedContext {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
    uint64_t stack_base;
    uint64_t stack_limit;
    uint64_t deallocation_stack;
};

void RestoreActiveTcb() noexcept;
void SetActiveTcb(uintptr_t tcb) noexcept;
[[nodiscard]] uintptr_t GetActiveTcb() noexcept;
void EnsureVectoredHandlerInstalled() noexcept;
void SignalGuestExit(int code) noexcept;
HostSavedContext *GetThreadHostContext() noexcept;
void SetThreadHostContextActive(bool active) noexcept;
extern "C" void portage_guest_exit_trampoline(int code);

extern "C" int portage_save_and_enter(void *entry_point, void *guest_rsp, HostSavedContext *ctx);
extern "C" void portage_restore_and_exit(HostSavedContext *ctx, int exit_code);

struct AuxvEntry {
    uint64_t type;
    uint64_t val;
};

class GuestContext {
  public:
    GuestContext() noexcept;
    ~GuestContext() noexcept;

    GuestContext(const GuestContext &) = delete;
    GuestContext &operator=(const GuestContext &) = delete;
    GuestContext(GuestContext &&) noexcept;
    GuestContext &operator=(GuestContext &&) noexcept;

    // Allocate 8MB stack
    [[nodiscard]] bool InitializeStack(size_t stack_size = DEFAULT_STACK_SIZE) noexcept;

    // Construct Linux process initial stack frame
    [[nodiscard]] void *SetupStack(const std::vector<std::string> &args,
                                   const std::vector<std::string> &env,
                                   std::span<const AuxvEntry> auxv_entries) noexcept;

    // Run guest code from entry_point on the prepared stack
    int Run(void *entry_point, void *guest_rsp) noexcept;

  private:
    void *stack_base_{nullptr};
    size_t stack_size_{0};
    void *veh_handle_{nullptr};
};

} // namespace portage::arch
