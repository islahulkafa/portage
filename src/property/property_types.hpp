#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::property {

constexpr uint32_t PROP_AREA_MAGIC = 0x50524f50; // "PROP"
constexpr uint32_t PROP_AREA_VERSION = 0x00020000;
constexpr size_t PROP_AREA_SIZE = 128 * 1024; // 128 KB
constexpr size_t PROP_NAME_MAX = 32;
constexpr size_t PROP_VALUE_MAX = 92;

constexpr uint32_t PROP_MSG_SETPROP = 1;
constexpr uint32_t PROP_MSG_SETPROP2 = 2;

constexpr uint32_t PROP_SUCCESS = 0;
constexpr uint32_t PROP_ERROR_READ_CMD = 0x1000;
constexpr uint32_t PROP_ERROR_READ_DATA = 0x2000;
constexpr uint32_t PROP_ERROR_PERMISSION_DENIED = 0x3000;
constexpr uint32_t PROP_ERROR_INVALID_NAME = 0x4000;
constexpr uint32_t PROP_ERROR_INVALID_VALUE = 0x5000;
constexpr uint32_t PROP_ERROR_SET_FAILED = 0x6000;

#pragma pack(push, 4)

struct PropAreaHeader {
    uint32_t bytes_used{128};
    uint32_t serial{0};
    uint32_t magic{PROP_AREA_MAGIC};
    uint32_t version{PROP_AREA_VERSION};
    uint32_t reserved[28]{};
};

struct PropInfo {
    uint32_t serial{0};
    char value[PROP_VALUE_MAX]{};
    char name[PROP_NAME_MAX]{};
};

struct PropTrieNode {
    uint8_t namelen{0};
    uint8_t reserved[3]{};
    uint32_t prop{0};
    uint32_t left{0};
    uint32_t right{0};
    uint32_t children{0};
    char name[4]{}; // Null-padded / 4-byte aligned
};

struct PropMsgSetProp {
    uint32_t cmd{PROP_MSG_SETPROP};
    char name[PROP_NAME_MAX]{};
    char value[PROP_VALUE_MAX]{};
};

#pragma pack(pop)

static_assert(sizeof(PropAreaHeader) == 128, "PropAreaHeader must be 128 bytes");
static_assert(sizeof(PropInfo) == 128, "PropInfo must be 128 bytes");
static_assert(sizeof(PropMsgSetProp) == 128, "PropMsgSetProp must be 128 bytes");

} // namespace portage::property
