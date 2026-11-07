#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::package {

#pragma pack(push, 1)

struct ResChunk_header {
    uint16_t type;
    uint16_t header_size;
    uint32_t size;
};

enum ResChunkType : uint16_t {
    RES_NULL_TYPE = 0x0000,
    RES_STRING_POOL_TYPE = 0x0001,
    RES_TABLE_TYPE = 0x0002,
    RES_XML_TYPE = 0x0003,

    RES_TABLE_PACKAGE_TYPE = 0x0200,
    RES_TABLE_TYPE_TYPE = 0x0201,
    RES_TABLE_TYPE_SPEC_TYPE = 0x0202,
    RES_TABLE_LIBRARY_TYPE = 0x0203,
};

struct ResTable_header {
    ResChunk_header header;
    uint32_t package_count;
};

struct ResStringPool_header {
    ResChunk_header header;
    uint32_t string_count;
    uint32_t style_count;
    uint32_t flags; // (1 << 8) = UTF-8
    uint32_t strings_start;
    uint32_t styles_start;
};

struct ResTable_package {
    ResChunk_header header;
    uint32_t id;
    char16_t name[128];
    uint32_t type_strings;
    uint32_t last_public_type;
    uint32_t key_strings;
    uint32_t last_public_key;
    uint32_t type_id_offset;
};

struct ResTable_typeSpec {
    ResChunk_header header;
    uint8_t id;
    uint8_t res0;
    uint16_t res1;
    uint32_t entry_count;
};

struct ResTable_config {
    uint32_t size;
    uint16_t mcc;
    uint16_t mnc;
    char language[2];
    char country[2];
    uint8_t orientation;
    uint8_t touchscreen;
    uint16_t density;
    uint8_t keyboard;
    uint8_t navigation;
    uint8_t input_flags;
    uint8_t input_pad0;
    uint16_t screen_width;
    uint16_t screen_height;
    uint16_t sdk_version;
    uint16_t minor_version;
    uint8_t screen_layout;
    uint8_t ui_mode;
    uint16_t smallest_screen_width_dp;
    uint16_t screen_width_dp;
    uint16_t screen_height_dp;
};

struct ResTable_type {
    ResChunk_header header;
    uint8_t id;
    uint8_t flags;
    uint16_t reserved;
    uint32_t entry_count;
    uint32_t entries_start;
    ResTable_config config;
};

struct ResTable_entry {
    uint16_t size;
    uint16_t flags; // 0x0001 = FLAG_COMPLEX
    uint32_t key;   // Index into key_strings pool
};

struct Res_value {
    uint16_t size;
    uint8_t res0;
    uint8_t data_type;
    uint32_t data;
};

struct ResTable_ref {
    uint32_t ident;
};

struct ResTable_map_entry {
    ResTable_entry entry;
    ResTable_ref parent;
    uint32_t count;
};

struct ResTable_map {
    ResTable_ref name;
    Res_value value;
};

#pragma pack(pop)

enum ResValueDataType : uint8_t {
    TYPE_NULL = 0x00,
    TYPE_REFERENCE = 0x01,
    TYPE_ATTRIBUTE = 0x02,
    TYPE_STRING = 0x03,
    TYPE_FLOAT = 0x04,
    TYPE_DIMENSION = 0x05,
    TYPE_FRACTION = 0x06,
    TYPE_DYNAMIC_REFERENCE = 0x07,
    TYPE_DYNAMIC_ATTRIBUTE = 0x08,
    TYPE_INT_DEC = 0x10,
    TYPE_INT_HEX = 0x11,
    TYPE_INT_BOOLEAN = 0x12,
    TYPE_INT_COLOR_ARGB8 = 0x1c,
    TYPE_INT_COLOR_RGB8 = 0x1d,
    TYPE_INT_COLOR_ARGB4 = 0x1e,
    TYPE_INT_COLOR_RGB4 = 0x1f,
};

constexpr uint32_t RES_NO_ENTRY = 0xFFFFFFFF;
constexpr uint16_t RES_FLAG_COMPLEX = 0x0001;

} // namespace portage::package
