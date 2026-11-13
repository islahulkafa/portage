#pragma once

#include <cstdint>

namespace portage::package {

constexpr uint32_t DEX_ENDIAN_CONSTANT = 0x12345678u;
constexpr uint32_t DEX_NO_INDEX = 0xFFFFFFFFu;

enum DexAccessFlags : uint32_t {
    ACC_PUBLIC = 0x00001u,
    ACC_PRIVATE = 0x00002u,
    ACC_PROTECTED = 0x00004u,
    ACC_STATIC = 0x00008u,
    ACC_FINAL = 0x00010u,
    ACC_SYNCHRONIZED = 0x00020u,
    ACC_VOLATILE = 0x00040u,
    ACC_TRANSIENT = 0x00080u,
    ACC_NATIVE = 0x00100u,
    ACC_INTERFACE = 0x00200u,
    ACC_ABSTRACT = 0x00400u,
    ACC_STRICT = 0x00800u,
    ACC_SYNTHETIC = 0x01000u,
    ACC_ANNOTATION = 0x02000u,
    ACC_ENUM = 0x04000u,
    ACC_CONSTRUCTOR = 0x10000u,
    ACC_DECLARED_SYNCHRONIZED = 0x20000u,
};

#pragma pack(push, 1)
struct DexHeader {
    uint8_t magic[8];
    uint32_t checksum;
    uint8_t signature[20];
    uint32_t file_size;
    uint32_t header_size;
    uint32_t endian_tag;
    uint32_t link_size;
    uint32_t link_off;
    uint32_t map_off;
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t proto_ids_size;
    uint32_t proto_ids_off;
    uint32_t field_ids_size;
    uint32_t field_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    uint32_t data_size;
    uint32_t data_off;
};

struct DexStringId {
    uint32_t string_data_off;
};

struct DexTypeId {
    uint32_t descriptor_idx;
};

struct DexProtoId {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t parameters_off;
};

struct DexFieldId {
    uint16_t class_idx;
    uint16_t type_idx;
    uint32_t name_idx;
};

struct DexMethodId {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
};

struct DexClassDef {
    uint32_t class_idx;
    uint32_t access_flags;
    uint32_t superclass_idx;
    uint32_t interfaces_off;
    uint32_t source_file_idx;
    uint32_t annotations_off;
    uint32_t class_data_off;
    uint32_t static_values_off;
};

struct DexCodeItemHeader {
    uint16_t registers_size;
    uint16_t ins_size;
    uint16_t outs_size;
    uint16_t tries_size;
    uint32_t debug_info_off;
    uint32_t insns_size;
};

struct DexTryItem {
    uint32_t start_addr;
    uint16_t insn_count;
    uint16_t handler_off;
};

struct DexMapItem {
    uint16_t type;
    uint16_t unused;
    uint32_t size;
    uint32_t offset;
};

struct DexMethodHandleItem {
    uint16_t method_handle_type;
    uint16_t unused1;
    uint16_t field_or_method_id;
    uint16_t unused2;
};

struct DexCallSiteIdItem {
    uint32_t call_site_off;
};
#pragma pack(pop)

constexpr uint16_t kDexTypeCallSiteIdItem = 0x0007u;
constexpr uint16_t kDexTypeMethodHandleItem = 0x0008u;

enum class DexMethodHandleType : uint16_t {
    StaticPut = 0x00,
    StaticGet = 0x01,
    InstancePut = 0x02,
    InstanceGet = 0x03,
    InvokeStatic = 0x04,
    InvokeInstance = 0x05,
    InvokeConstructor = 0x06,
    InvokeDirect = 0x07,
    InvokeInterface = 0x08,
};

enum DexOpcode : uint8_t {
    OP_NOP = 0x00,
    OP_MOVE = 0x01,
    OP_MOVE_FROM16 = 0x02,
    OP_MOVE_16 = 0x03,
    OP_MOVE_WIDE = 0x04,
    OP_MOVE_WIDE_FROM16 = 0x05,
    OP_MOVE_WIDE_16 = 0x06,
    OP_MOVE_OBJECT = 0x07,
    OP_MOVE_OBJECT_FROM16 = 0x08,
    OP_MOVE_OBJECT_16 = 0x09,
    OP_MOVE_RESULT = 0x0A,
    OP_MOVE_RESULT_WIDE = 0x0B,
    OP_MOVE_RESULT_OBJECT = 0x0C,
    OP_MOVE_EXCEPTION = 0x0D,
    OP_RETURN_VOID = 0x0E,
    OP_RETURN = 0x0F,
    OP_RETURN_WIDE = 0x10,
    OP_RETURN_OBJECT = 0x11,
    OP_CONST_4 = 0x12,
    OP_CONST_16 = 0x13,
    OP_CONST = 0x14,
    OP_CONST_HIGH16 = 0x15,
    OP_CONST_WIDE_16 = 0x16,
    OP_CONST_WIDE_32 = 0x17,
    OP_CONST_WIDE = 0x18,
    OP_CONST_WIDE_HIGH16 = 0x19,
    OP_CONST_STRING = 0x1A,
    OP_CONST_STRING_JUMBO = 0x1B,
    OP_CONST_CLASS = 0x1C,
    OP_MONITOR_ENTER = 0x1D,
    OP_MONITOR_EXIT = 0x1E,
    OP_CHECK_CAST = 0x1F,
    OP_INSTANCE_OF = 0x20,
    OP_ARRAY_LENGTH = 0x21,
    OP_NEW_INSTANCE = 0x22,
    OP_NEW_ARRAY = 0x23,
    OP_FILLED_NEW_ARRAY = 0x24,
    OP_FILLED_NEW_ARRAY_RANGE = 0x25,
    OP_FILL_ARRAY_DATA = 0x26,
    OP_THROW = 0x27,
    OP_GOTO = 0x28,
    OP_GOTO_16 = 0x29,
    OP_GOTO_32 = 0x2A,
    OP_PACKED_SWITCH = 0x2B,
    OP_SPARSE_SWITCH = 0x2C,
    OP_CMPL_FLOAT = 0x2D,
    OP_CMPG_FLOAT = 0x2E,
    OP_CMPL_DOUBLE = 0x2F,
    OP_CMPG_DOUBLE = 0x30,
    OP_CMP_LONG = 0x31,
    OP_IF_EQ = 0x32,
    OP_IF_NE = 0x33,
    OP_IF_LT = 0x34,
    OP_IF_GE = 0x35,
    OP_IF_GT = 0x36,
    OP_IF_LE = 0x37,
    OP_IF_EQZ = 0x38,
    OP_IF_NEZ = 0x39,
    OP_IF_LTZ = 0x3A,
    OP_IF_GEZ = 0x3B,
    OP_IF_GTZ = 0x3C,
    OP_IF_LEZ = 0x3D,
    OP_AGET = 0x44,
    OP_AGET_WIDE = 0x45,
    OP_AGET_OBJECT = 0x46,
    OP_AGET_BOOLEAN = 0x47,
    OP_AGET_BYTE = 0x48,
    OP_AGET_CHAR = 0x49,
    OP_AGET_SHORT = 0x4A,
    OP_APUT = 0x4B,
    OP_APUT_WIDE = 0x4C,
    OP_APUT_OBJECT = 0x4D,
    OP_APUT_BOOLEAN = 0x4E,
    OP_APUT_BYTE = 0x4F,
    OP_APUT_CHAR = 0x50,
    OP_APUT_SHORT = 0x51,
    OP_IGET = 0x52,
    OP_IGET_WIDE = 0x53,
    OP_IGET_OBJECT = 0x54,
    OP_IGET_BOOLEAN = 0x55,
    OP_IGET_BYTE = 0x56,
    OP_IGET_CHAR = 0x57,
    OP_IGET_SHORT = 0x58,
    OP_IPUT = 0x59,
    OP_IPUT_WIDE = 0x5A,
    OP_IPUT_OBJECT = 0x5B,
    OP_IPUT_BOOLEAN = 0x5C,
    OP_IPUT_BYTE = 0x5D,
    OP_IPUT_CHAR = 0x5E,
    OP_IPUT_SHORT = 0x5F,
    OP_SGET = 0x60,
    OP_SGET_WIDE = 0x61,
    OP_SGET_OBJECT = 0x62,
    OP_SGET_BOOLEAN = 0x63,
    OP_SGET_BYTE = 0x64,
    OP_SGET_CHAR = 0x65,
    OP_SGET_SHORT = 0x66,
    OP_SPUT = 0x67,
    OP_SPUT_WIDE = 0x68,
    OP_SPUT_OBJECT = 0x69,
    OP_SPUT_BOOLEAN = 0x6A,
    OP_SPUT_BYTE = 0x6B,
    OP_SPUT_CHAR = 0x6C,
    OP_SPUT_SHORT = 0x6D,
    OP_INVOKE_VIRTUAL = 0x6E,
    OP_INVOKE_SUPER = 0x6F,
    OP_INVOKE_DIRECT = 0x70,
    OP_INVOKE_STATIC = 0x71,
    OP_INVOKE_INTERFACE = 0x72,
    OP_INVOKE_VIRTUAL_RANGE = 0x74,
    OP_INVOKE_SUPER_RANGE = 0x75,
    OP_INVOKE_DIRECT_RANGE = 0x76,
    OP_INVOKE_STATIC_RANGE = 0x77,
    OP_INVOKE_INTERFACE_RANGE = 0x78,
    OP_NEG_INT = 0x7B,
    OP_NOT_INT = 0x7C,
    OP_NEG_LONG = 0x7D,
    OP_NOT_LONG = 0x7E,
    OP_NEG_FLOAT = 0x7F,
    OP_NEG_DOUBLE = 0x80,
    OP_INT_TO_LONG = 0x81,
    OP_INT_TO_FLOAT = 0x82,
    OP_INT_TO_DOUBLE = 0x83,
    OP_LONG_TO_INT = 0x84,
    OP_LONG_TO_FLOAT = 0x85,
    OP_LONG_TO_DOUBLE = 0x86,
    OP_FLOAT_TO_INT = 0x87,
    OP_FLOAT_TO_LONG = 0x88,
    OP_FLOAT_TO_DOUBLE = 0x89,
    OP_DOUBLE_TO_INT = 0x8A,
    OP_DOUBLE_TO_LONG = 0x8B,
    OP_DOUBLE_TO_FLOAT = 0x8C,
    OP_INT_TO_BYTE = 0x8D,
    OP_INT_TO_CHAR = 0x8E,
    OP_INT_TO_SHORT = 0x8F,
    OP_ADD_INT = 0x90,
    OP_SUB_INT = 0x91,
    OP_MUL_INT = 0x92,
    OP_DIV_INT = 0x93,
    OP_REM_INT = 0x94,
    OP_AND_INT = 0x95,
    OP_OR_INT = 0x96,
    OP_XOR_INT = 0x97,
    OP_SHL_INT = 0x98,
    OP_SHR_INT = 0x99,
    OP_USHR_INT = 0x9A,
    OP_ADD_LONG = 0x9B,
    OP_SUB_LONG = 0x9C,
    OP_MUL_LONG = 0x9D,
    OP_DIV_LONG = 0x9E,
    OP_REM_LONG = 0x9F,
    OP_AND_LONG = 0xA0,
    OP_OR_LONG = 0xA1,
    OP_XOR_LONG = 0xA2,
    OP_SHL_LONG = 0xA3,
    OP_SHR_LONG = 0xA4,
    OP_USHR_LONG = 0xA5,
    OP_ADD_FLOAT = 0xA6,
    OP_SUB_FLOAT = 0xA7,
    OP_MUL_FLOAT = 0xA8,
    OP_DIV_FLOAT = 0xA9,
    OP_REM_FLOAT = 0xAA,
    OP_ADD_DOUBLE = 0xAB,
    OP_SUB_DOUBLE = 0xAC,
    OP_MUL_DOUBLE = 0xAD,
    OP_DIV_DOUBLE = 0xAE,
    OP_REM_DOUBLE = 0xAF,
    OP_ADD_INT_2ADDR = 0xB0,
    OP_SUB_INT_2ADDR = 0xB1,
    OP_MUL_INT_2ADDR = 0xB2,
    OP_DIV_INT_2ADDR = 0xB3,
    OP_REM_INT_2ADDR = 0xB4,
    OP_AND_INT_2ADDR = 0xB5,
    OP_OR_INT_2ADDR = 0xB6,
    OP_XOR_INT_2ADDR = 0xB7,
    OP_SHL_INT_2ADDR = 0xB8,
    OP_SHR_INT_2ADDR = 0xB9,
    OP_USHR_INT_2ADDR = 0xBA,
    OP_ADD_LONG_2ADDR = 0xBB,
    OP_SUB_LONG_2ADDR = 0xBC,
    OP_MUL_LONG_2ADDR = 0xBD,
    OP_DIV_LONG_2ADDR = 0xBE,
    OP_REM_LONG_2ADDR = 0xBF,
    OP_AND_LONG_2ADDR = 0xC0,
    OP_OR_LONG_2ADDR = 0xC1,
    OP_XOR_LONG_2ADDR = 0xC2,
    OP_SHL_LONG_2ADDR = 0xC3,
    OP_SHR_LONG_2ADDR = 0xC4,
    OP_USHR_LONG_2ADDR = 0xC5,
    OP_ADD_FLOAT_2ADDR = 0xC6,
    OP_SUB_FLOAT_2ADDR = 0xC7,
    OP_MUL_FLOAT_2ADDR = 0xC8,
    OP_DIV_FLOAT_2ADDR = 0xC9,
    OP_REM_FLOAT_2ADDR = 0xCA,
    OP_ADD_DOUBLE_2ADDR = 0xCB,
    OP_SUB_DOUBLE_2ADDR = 0xCC,
    OP_MUL_DOUBLE_2ADDR = 0xCD,
    OP_DIV_DOUBLE_2ADDR = 0xCE,
    OP_REM_DOUBLE_2ADDR = 0xCF,
    OP_ADD_INT_LIT16 = 0xD0,
    OP_RSUB_INT = 0xD1,
    OP_MUL_INT_LIT16 = 0xD2,
    OP_DIV_INT_LIT16 = 0xD3,
    OP_REM_INT_LIT16 = 0xD4,
    OP_AND_INT_LIT16 = 0xD5,
    OP_OR_INT_LIT16 = 0xD6,
    OP_XOR_INT_LIT16 = 0xD7,
    OP_ADD_INT_LIT8 = 0xD8,
    OP_RSUB_INT_LIT8 = 0xD9,
    OP_MUL_INT_LIT8 = 0xDA,
    OP_DIV_INT_LIT8 = 0xDB,
    OP_REM_INT_LIT8 = 0xDC,
    OP_AND_INT_LIT8 = 0xDD,
    OP_OR_INT_LIT8 = 0xDE,
    OP_XOR_INT_LIT8 = 0xDF,
    OP_SHL_INT_LIT8 = 0xE0,
    OP_SHR_INT_LIT8 = 0xE1,
    OP_USHR_INT_LIT8 = 0xE2,
    OP_INVOKE_POLYMORPHIC = 0xFA,
    OP_INVOKE_POLYMORPHIC_RANGE = 0xFB,
    OP_INVOKE_CUSTOM = 0xFC,
    OP_INVOKE_CUSTOM_RANGE = 0xFD,
    OP_CONST_METHOD_HANDLE = 0xFE,
    OP_CONST_METHOD_TYPE = 0xFF,
};

} // namespace portage::package
