#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace portage::fs {

#define LINUX_DT_UNKNOWN 0
#define LINUX_DT_FIFO 1
#define LINUX_DT_CHR 2
#define LINUX_DT_DIR 4
#define LINUX_DT_BLK 6
#define LINUX_DT_REG 8
#define LINUX_DT_LNK 10
#define LINUX_DT_SOCK 12

#pragma pack(push, 1)
struct LinuxDirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[1]; // Flexible array member
};
#pragma pack(pop)

static_assert(offsetof(LinuxDirent64, d_ino) == 0);
static_assert(offsetof(LinuxDirent64, d_off) == 8);
static_assert(offsetof(LinuxDirent64, d_reclen) == 16);
static_assert(offsetof(LinuxDirent64, d_type) == 18);
static_assert(offsetof(LinuxDirent64, d_name) == 19);

struct DirEntry {
    uint64_t ino{0};
    uint8_t type{LINUX_DT_UNKNOWN};
    std::string name{};
};

inline uint16_t CalculateDirent64Reclen(size_t name_len) noexcept
{
    // 19 bytes header + name_len + 1 (null terminator), aligned up to 8 bytes
    size_t size = 19 + name_len + 1;
    return static_cast<uint16_t>((size + 7) & ~static_cast<size_t>(7));
}

} // namespace portage::fs
