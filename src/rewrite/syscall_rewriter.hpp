#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace portage::rewrite {

struct PatchSite {
    uintptr_t address{0};
    size_t displaced_bytes{0};
    uintptr_t trampoline_address{0};
};

class SyscallRewriter {
  public:
    SyscallRewriter(void *trampoline_island, size_t island_size) noexcept;

    // Scans code span for 0x0F 0x05, patches them, and emits trampoline stubs
    [[nodiscard]] size_t RewriteSegment(std::span<uint8_t> code_span,
                                        uintptr_t segment_vaddr) noexcept;

    // Dynamically patches 0x0F 0x05 in any committed memory region
    static size_t PatchCodeRegion(void *base, size_t size) noexcept;

    [[nodiscard]] const std::vector<PatchSite> &GetPatchSites() const noexcept
    {
        return patch_sites_;
    }

  private:
    uint8_t *island_base_{nullptr};
    size_t island_size_{0};
    size_t island_offset_{0};
    std::vector<PatchSite> patch_sites_;

    [[nodiscard]] void *AllocateTrampolineSlot(size_t size) noexcept;
};

} // namespace portage::rewrite
