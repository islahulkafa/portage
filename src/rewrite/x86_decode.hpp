#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace portage::rewrite {

struct InstructionInfo {
    size_t length{0};
    bool is_valid{false};
};

// Decodes length of a single x86_64 instruction (1..15 bytes)
[[nodiscard]] InstructionInfo DecodeInstruction(std::span<const uint8_t> code) noexcept;

// Calculates total bytes covering at least min_bytes from instruction boundaries
[[nodiscard]] size_t CalculateRelocationSpan(std::span<const uint8_t> code,
                                             size_t min_bytes) noexcept;

} // namespace portage::rewrite
