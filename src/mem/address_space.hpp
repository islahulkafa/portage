#pragma once

#include "elf/elf_types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace portage::mem {

constexpr size_t PAGE_SIZE_4KB = 4096;
constexpr size_t ALLOC_GRANULARITY_64KB = 65536;

enum class MemProt : uint32_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Execute = 1 << 2,
};

inline MemProt operator|(MemProt a, MemProt b) noexcept
{
    return static_cast<MemProt>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(MemProt a, MemProt b) noexcept
{
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

class AddressSpace {
  public:
    AddressSpace() noexcept;
    ~AddressSpace() noexcept;

    AddressSpace(const AddressSpace &) = delete;
    AddressSpace &operator=(const AddressSpace &) = delete;
    AddressSpace(AddressSpace &&) noexcept;
    AddressSpace &operator=(AddressSpace &&) noexcept;

    // Reserve a contiguous virtual address region
    [[nodiscard]] void *Reserve(void *preferred_base, size_t size) noexcept;

    // Commit memory within a reserved region with the given protection
    [[nodiscard]] bool Commit(void *addr, size_t size, uint32_t pf_flags) noexcept;

    // Allocate memory within +/- 1.5GB of a target address (for trampoline islands)
    [[nodiscard]] void *AllocateNear(void *target, size_t size) noexcept;

    // Initialize and handle sys_brk
    void InitBrk(uintptr_t initial_brk) noexcept;
    [[nodiscard]] uintptr_t Brk(uintptr_t new_brk) noexcept;
    [[nodiscard]] uintptr_t GetBrkStart() const noexcept { return brk_start_; }
    [[nodiscard]] uintptr_t GetBrkCurrent() const noexcept { return brk_current_; }
    [[nodiscard]] uintptr_t GetBrkLimit() const noexcept { return brk_limit_; }
    void AddReservation(void *ptr) noexcept
    {
        if (ptr) {
            reservations_.push_back(ptr);
        }
    }

    // Anonymous mmap / munmap / mprotect
    [[nodiscard]] void *MmapAnonymous(void *addr, size_t length, int prot, int flags) noexcept;
    [[nodiscard]] int Munmap(void *addr, size_t length) noexcept;
    [[nodiscard]] int Mprotect(void *addr, size_t length, int prot) noexcept;

  private:
    [[nodiscard]] static uint32_t ElfFlagsToWinProt(uint32_t pf_flags) noexcept;
    [[nodiscard]] static uint32_t LinuxProtToWinProt(int prot) noexcept;

    std::vector<void *> reservations_;
    uintptr_t brk_start_{0};
    uintptr_t brk_current_{0};
    uintptr_t brk_limit_{0};
};

} // namespace portage::mem
