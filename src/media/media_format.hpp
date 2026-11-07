#pragma once

#include "media/media_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct AMediaFormat {
    struct Value {
        enum class Type { String, Int32, Int64, Float, Size, Buffer };
        Type type;
        std::string str_val{};
        int32_t i32_val{0};
        int64_t i64_val{0};
        float f_val{0.0f};
        size_t size_val{0};
        std::vector<uint8_t> buf_val{};
    };

    std::unordered_map<std::string, Value> entries{};
    mutable std::string to_string_cache{};
};

extern "C" {

__attribute__((sysv_abi)) AMediaFormat *AMediaFormat_new();

__attribute__((sysv_abi)) media_status_t AMediaFormat_delete(AMediaFormat *format);

__attribute__((sysv_abi)) bool AMediaFormat_getString(AMediaFormat *format, const char *name,
                                                      const char **out);

__attribute__((sysv_abi)) bool AMediaFormat_getInt32(AMediaFormat *format, const char *name,
                                                     int32_t *out);

__attribute__((sysv_abi)) bool AMediaFormat_getInt64(AMediaFormat *format, const char *name,
                                                     int64_t *out);

__attribute__((sysv_abi)) bool AMediaFormat_getFloat(AMediaFormat *format, const char *name,
                                                     float *out);

__attribute__((sysv_abi)) bool AMediaFormat_getSize(AMediaFormat *format, const char *name,
                                                    size_t *out);

__attribute__((sysv_abi)) bool AMediaFormat_getBuffer(AMediaFormat *format, const char *name,
                                                     void **data, size_t *size);

__attribute__((sysv_abi)) void AMediaFormat_setString(AMediaFormat *format, const char *name,
                                                      const char *value);

__attribute__((sysv_abi)) void AMediaFormat_setInt32(AMediaFormat *format, const char *name,
                                                     int32_t value);

__attribute__((sysv_abi)) void AMediaFormat_setInt64(AMediaFormat *format, const char *name,
                                                     int64_t value);

__attribute__((sysv_abi)) void AMediaFormat_setFloat(AMediaFormat *format, const char *name,
                                                     float value);

__attribute__((sysv_abi)) void AMediaFormat_setSize(AMediaFormat *format, const char *name,
                                                    size_t value);

__attribute__((sysv_abi)) void AMediaFormat_setBuffer(AMediaFormat *format, const char *name,
                                                      const void *data, size_t size);

__attribute__((sysv_abi)) const char *AMediaFormat_toString(AMediaFormat *format);

__attribute__((sysv_abi)) bool AMediaFormat_copy(AMediaFormat *to, AMediaFormat *from);

__attribute__((sysv_abi)) void AMediaFormat_clear(AMediaFormat *format);

} // extern "C"
