#include "syscall_rewriter.hpp"
#include <cstring>
#include <windows.h>

namespace portage::rewrite {

SyscallRewriter::SyscallRewriter(void *trampoline_island, size_t island_size) noexcept
    : island_base_(static_cast<uint8_t *>(trampoline_island)), island_size_(island_size),
      island_offset_(0)
{
}

void *SyscallRewriter::AllocateTrampolineSlot(size_t size) noexcept
{
    size_t aligned_offset = (island_offset_ + 15) & ~15ULL;
    if (aligned_offset + size > island_size_) {
        return nullptr;
    }
    void *ptr = island_base_ + aligned_offset;
    island_offset_ = aligned_offset + size;
    return ptr;
}

size_t SyscallRewriter::PatchCodeRegion(void *base, size_t size) noexcept
{
    if (!base || size < 2) {
        return 0;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(base, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return 0;
    }

    uint8_t *ptr = static_cast<uint8_t *>(base);
    size_t patches = 0;

    for (size_t i = 0; i + 1 < size; i++) {
        if (ptr[i] == 0x0F && ptr[i + 1] == 0x05) {
            ptr[i] = 0xCC;
            ptr[i + 1] = 0x90;
            patches++;
            i++;
        }
    }

    VirtualProtect(base, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), base, size);
    return patches;
}

size_t SyscallRewriter::RewriteSegment(std::span<uint8_t> code_span,
                                       uintptr_t segment_vaddr) noexcept
{
    if (code_span.size() < 2) {
        return 0;
    }

    size_t patches_count = 0;
    for (size_t i = 0; i + 1 < code_span.size(); i++) {
        // Look for 0x0F, 0x05 (syscall)
        if (code_span[i] == 0x0F && code_span[i + 1] == 0x05) {
            // Surgical 2-byte patch: 0xCC (INT 3) + 0x90 (NOP)
            // Preserves 100% of adjacent code and prevents any branch target corruption
            code_span[i] = 0xCC;
            code_span[i + 1] = 0x90;

            patch_sites_.push_back(
                {.address = segment_vaddr + i, .displaced_bytes = 2, .trampoline_address = 0});
            patches_count++;
            i++; // skip 0x90
        }
    }

    FlushInstructionCache(GetCurrentProcess(), code_span.data(), code_span.size());
    if (island_base_ && island_size_ > 0) {
        FlushInstructionCache(GetCurrentProcess(), island_base_, island_size_);
    }

    return patches_count;
}

} // namespace portage::rewrite
