#include "address_space.hpp"
#include <algorithm>
#include <utility>
#include <windows.h>

namespace portage::mem {

namespace {

constexpr int LINUX_PROT_READ = 1;
constexpr int LINUX_PROT_WRITE = 2;
constexpr int LINUX_PROT_EXEC = 4;

constexpr int LINUX_MAP_FIXED = 0x10;

uintptr_t AlignUp(uintptr_t val, uintptr_t align) noexcept
{
    return (val + align - 1) & ~(align - 1);
}

uintptr_t AlignDown(uintptr_t val, uintptr_t align) noexcept { return val & ~(align - 1); }

} // namespace

AddressSpace::AddressSpace() noexcept = default;

AddressSpace::~AddressSpace() noexcept
{
    for (void *ptr : reservations_) {
        if (ptr) {
            VirtualFree(ptr, 0, MEM_RELEASE);
        }
    }
}

AddressSpace::AddressSpace(AddressSpace &&other) noexcept
    : reservations_(std::move(other.reservations_)), brk_start_(other.brk_start_),
      brk_current_(other.brk_current_), brk_limit_(other.brk_limit_)
{
    other.brk_start_ = 0;
    other.brk_current_ = 0;
    other.brk_limit_ = 0;
}

AddressSpace &AddressSpace::operator=(AddressSpace &&other) noexcept
{
    if (this != &other) {
        for (void *ptr : reservations_) {
            if (ptr) {
                VirtualFree(ptr, 0, MEM_RELEASE);
            }
        }
        reservations_ = std::move(other.reservations_);
        brk_start_ = other.brk_start_;
        brk_current_ = other.brk_current_;
        brk_limit_ = other.brk_limit_;

        other.brk_start_ = 0;
        other.brk_current_ = 0;
        other.brk_limit_ = 0;
    }
    return *this;
}

uint32_t AddressSpace::ElfFlagsToWinProt(uint32_t pf_flags) noexcept
{
    const bool r = (pf_flags & elf::PF_R) != 0;
    const bool w = (pf_flags & elf::PF_W) != 0;
    const bool x = (pf_flags & elf::PF_X) != 0;

    if (x && w)
        return PAGE_EXECUTE_READWRITE;
    if (x && r)
        return PAGE_EXECUTE_READ;
    if (x)
        return PAGE_EXECUTE;
    if (w)
        return PAGE_READWRITE;
    if (r)
        return PAGE_READONLY;
    return PAGE_NOACCESS;
}

uint32_t AddressSpace::LinuxProtToWinProt(int prot) noexcept
{
    const bool r = (prot & LINUX_PROT_READ) != 0;
    const bool w = (prot & LINUX_PROT_WRITE) != 0;
    const bool x = (prot & LINUX_PROT_EXEC) != 0;

    if (x && w)
        return PAGE_EXECUTE_READWRITE;
    if (x && r)
        return PAGE_EXECUTE_READ;
    if (x)
        return PAGE_EXECUTE;
    if (w)
        return PAGE_READWRITE;
    if (r)
        return PAGE_READONLY;
    return PAGE_NOACCESS;
}

void *AddressSpace::Reserve(void *preferred_base, size_t size) noexcept
{
    const size_t aligned_size = AlignUp(size, ALLOC_GRANULARITY_64KB);
    void *ptr = VirtualAlloc(preferred_base, aligned_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!ptr && preferred_base != nullptr) {
        ptr = VirtualAlloc(nullptr, aligned_size, MEM_RESERVE, PAGE_NOACCESS);
    }
    if (ptr) {
        reservations_.push_back(ptr);
    }
    return ptr;
}

bool AddressSpace::Commit(void *addr, size_t size, uint32_t pf_flags) noexcept
{
    const uintptr_t start = AlignDown(reinterpret_cast<uintptr_t>(addr), PAGE_SIZE_4KB);
    const uintptr_t end = AlignUp(reinterpret_cast<uintptr_t>(addr) + size, PAGE_SIZE_4KB);
    const size_t aligned_size = end - start;

    const DWORD win_prot = ElfFlagsToWinProt(pf_flags);
    void *result =
        VirtualAlloc(reinterpret_cast<void *>(start), aligned_size, MEM_COMMIT, win_prot);
    return result != nullptr;
}

void *AddressSpace::AllocateNear(void *target, size_t size) noexcept
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    const size_t aligned_size = AlignUp(size, ALLOC_GRANULARITY_64KB);
    const uintptr_t target_addr = reinterpret_cast<uintptr_t>(target);
    const uintptr_t min_addr = (target_addr > 0x60000000ULL)
                                   ? (target_addr - 0x60000000ULL)
                                   : reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t max_addr = target_addr + 0x60000000ULL;

    for (uintptr_t curr = (target_addr & ~0xFFFFULL) + ALLOC_GRANULARITY_64KB; curr < max_addr;
         curr += ALLOC_GRANULARITY_64KB) {
        void *p = VirtualAlloc(reinterpret_cast<void *>(curr), aligned_size,
                               MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) {
            reservations_.push_back(p);
            return p;
        }
    }
    for (uintptr_t curr = (target_addr & ~0xFFFFULL) - ALLOC_GRANULARITY_64KB; curr > min_addr;
         curr -= ALLOC_GRANULARITY_64KB) {
        void *p = VirtualAlloc(reinterpret_cast<void *>(curr), aligned_size,
                               MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) {
            reservations_.push_back(p);
            return p;
        }
    }
    return nullptr;
}

void AddressSpace::InitBrk(uintptr_t initial_brk) noexcept
{
    constexpr size_t BRK_ARENA_SIZE = 64 * 1024 * 1024; // 64 MB
    const uintptr_t preferred_arena = AlignUp(initial_brk, ALLOC_GRANULARITY_64KB);

    void *arena = VirtualAlloc(reinterpret_cast<void *>(preferred_arena), BRK_ARENA_SIZE,
                               MEM_RESERVE, PAGE_NOACCESS);
    if (!arena) {
        arena = VirtualAlloc(nullptr, BRK_ARENA_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    }
    if (arena) {
        reservations_.push_back(arena);
        brk_start_ = reinterpret_cast<uintptr_t>(arena);
        brk_current_ = brk_start_;
        brk_limit_ = brk_start_ + BRK_ARENA_SIZE;
    }
}

uintptr_t AddressSpace::Brk(uintptr_t new_brk) noexcept
{
    if (new_brk == 0 || new_brk <= brk_start_) {
        return brk_current_;
    }
    if (new_brk > brk_limit_) {
        return brk_current_;
    }

    const uintptr_t current_page = AlignUp(brk_current_, PAGE_SIZE_4KB);
    const uintptr_t target_page = AlignUp(new_brk, PAGE_SIZE_4KB);

    if (target_page > current_page) {
        const size_t commit_size = target_page - current_page;
        void *res = VirtualAlloc(reinterpret_cast<void *>(current_page), commit_size, MEM_COMMIT,
                                 PAGE_READWRITE);
        if (!res) {
            return brk_current_;
        }
    } else if (target_page < current_page) {
        const size_t decommit_size = current_page - target_page;
        VirtualFree(reinterpret_cast<void *>(target_page), decommit_size, MEM_DECOMMIT);
    }

    brk_current_ = new_brk;
    return brk_current_;
}

void *AddressSpace::MmapAnonymous(void *addr, size_t length, int prot, int flags) noexcept
{
    if (length == 0) {
        return reinterpret_cast<void *>(-1);
    }

    const size_t aligned_len = AlignUp(length, PAGE_SIZE_4KB);
    const DWORD win_prot = LinuxProtToWinProt(prot);

    if ((flags & LINUX_MAP_FIXED) != 0 && addr != nullptr) {
        const uintptr_t aligned_addr = AlignDown(reinterpret_cast<uintptr_t>(addr), PAGE_SIZE_4KB);
        void *res = VirtualAlloc(reinterpret_cast<void *>(aligned_addr), aligned_len,
                                 MEM_COMMIT | MEM_RESERVE, win_prot);
        if (!res) {
            res = VirtualAlloc(reinterpret_cast<void *>(aligned_addr), aligned_len, MEM_COMMIT,
                               win_prot);
        }
        return res ? res : reinterpret_cast<void *>(-1);
    }

    void *res = VirtualAlloc(nullptr, aligned_len, MEM_COMMIT | MEM_RESERVE, win_prot);
    if (res) {
        reservations_.push_back(res);
        return res;
    }
    return reinterpret_cast<void *>(-1);
}

int AddressSpace::Munmap(void *addr, size_t length) noexcept
{
    if (!addr || length == 0) {
        return -1;
    }
    const uintptr_t aligned_addr = AlignDown(reinterpret_cast<uintptr_t>(addr), PAGE_SIZE_4KB);
    const size_t aligned_len = AlignUp(length, PAGE_SIZE_4KB);

    BOOL success = VirtualFree(reinterpret_cast<void *>(aligned_addr), aligned_len, MEM_DECOMMIT);
    return success ? 0 : -1;
}

int AddressSpace::Mprotect(void *addr, size_t length, int prot) noexcept
{
    if (!addr || length == 0) {
        return -1;
    }
    const uintptr_t aligned_addr = AlignDown(reinterpret_cast<uintptr_t>(addr), PAGE_SIZE_4KB);
    const size_t aligned_len = AlignUp(length, PAGE_SIZE_4KB);
    const DWORD win_prot = LinuxProtToWinProt(prot);

    DWORD old_prot = 0;
    BOOL success =
        VirtualProtect(reinterpret_cast<void *>(aligned_addr), aligned_len, win_prot, &old_prot);
    return success ? 0 : -1;
}

} // namespace portage::mem
