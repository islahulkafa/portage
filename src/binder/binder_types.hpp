#pragma once

#include <cstdint>

namespace portage::binder {

inline constexpr int32_t BINDER_CURRENT_PROTOCOL_VERSION = 8;
inline constexpr uint32_t TF_ONE_WAY = 0x01u;

// Standard Linux ioctls (64-bit and generic)
inline constexpr uint32_t BINDER_WRITE_READ_64 = 0xc0306201u;
inline constexpr uint32_t BINDER_WRITE_READ_32 = 0xc0186201u;
inline constexpr uint32_t BINDER_SET_MAX_THREADS = 0x40046205u;
inline constexpr uint32_t BINDER_VERSION = 0xc0046209u;
inline constexpr uint32_t BINDER_SET_IDLE_TIMEOUT = 0x40086203u;

// Binder Command Protocol (BC_*)
inline constexpr uint32_t BC_TRANSACTION_64 = 0x40406300u;
inline constexpr uint32_t BC_REPLY_64 = 0x40406301u;
inline constexpr uint32_t BC_TRANSACTION_RAW = 0x63u;
inline constexpr uint32_t BC_REPLY_RAW = 0x64u;
inline constexpr uint32_t BC_FREE_BUFFER_RAW = 0x65u;
inline constexpr uint32_t BC_ACQUIRE_RAW = 0x62u;
inline constexpr uint32_t BC_RELEASE_RAW = 0x61u;
inline constexpr uint32_t BC_INCREFS_RAW = 0x60u;
inline constexpr uint32_t BC_DECREFS_RAW = 0x6eu;

// Binder Return Protocol (BR_*)
inline constexpr uint32_t BR_ERROR = 0xff006201u;
inline constexpr uint32_t BR_OK = 0x7201u;
inline constexpr uint32_t BR_TRANSACTION = 0x80407202u;
inline constexpr uint32_t BR_REPLY = 0x80407203u;
inline constexpr uint32_t BR_DEAD_REPLY = 0x7205u;
inline constexpr uint32_t BR_FAILED_REPLY = 0x7211u;
inline constexpr uint32_t BR_NOOP = 0x720cu;
inline constexpr uint32_t BR_ACQUIRE = 0x726bu;
inline constexpr uint32_t BR_RELEASE = 0x726cu;
inline constexpr uint32_t BR_INCREFS = 0x726du;
inline constexpr uint32_t BR_DECREFS = 0x7272u;

struct BinderWriteRead {
    int64_t write_size{0};
    int64_t write_consumed{0};
    uint64_t write_buffer{0};
    int64_t read_size{0};
    int64_t read_consumed{0};
    uint64_t read_buffer{0};
};

struct BinderVersion {
    int32_t protocol_version{0};
};

struct BinderTransactionData {
    union {
        uint32_t handle;
        uint64_t ptr;
    } target{};
    uint64_t cookie{0};
    uint32_t code{0};
    uint32_t flags{0};
    int32_t sender_pid{0};
    uint32_t sender_euid{0};
    uint64_t data_size{0};
    uint64_t offsets_size{0};
    union {
        struct {
            uint64_t buffer;
            uint64_t offsets;
        } ptr;
        uint8_t buf[8];
    } data{};
};

struct FlatBinderObject {
    uint32_t type{0};
    uint32_t flags{0};
    union {
        uint64_t binder;
        uint32_t handle;
    };
    uint64_t cookie{0};
};

} // namespace portage::binder
