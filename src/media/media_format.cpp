#include "media/media_format.hpp"

#include <sstream>

extern "C" {

__attribute__((sysv_abi)) AMediaFormat *AMediaFormat_new()
{
    return new AMediaFormat();
}

__attribute__((sysv_abi)) media_status_t AMediaFormat_delete(AMediaFormat *format)
{
    if (!format) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    delete format;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) bool AMediaFormat_getString(AMediaFormat *format, const char *name,
                                                      const char **out)
{
    if (!format || !name || !out) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end() && it->second.type == AMediaFormat::Value::Type::String) {
        *out = it->second.str_val.c_str();
        return true;
    }
    return false;
}

__attribute__((sysv_abi)) bool AMediaFormat_getInt32(AMediaFormat *format, const char *name,
                                                     int32_t *out)
{
    if (!format || !name || !out) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end()) {
        if (it->second.type == AMediaFormat::Value::Type::Int32) {
            *out = it->second.i32_val;
            return true;
        }
        if (it->second.type == AMediaFormat::Value::Type::Int64) {
            *out = static_cast<int32_t>(it->second.i64_val);
            return true;
        }
    }
    return false;
}

__attribute__((sysv_abi)) bool AMediaFormat_getInt64(AMediaFormat *format, const char *name,
                                                     int64_t *out)
{
    if (!format || !name || !out) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end()) {
        if (it->second.type == AMediaFormat::Value::Type::Int64) {
            *out = it->second.i64_val;
            return true;
        }
        if (it->second.type == AMediaFormat::Value::Type::Int32) {
            *out = it->second.i32_val;
            return true;
        }
    }
    return false;
}

__attribute__((sysv_abi)) bool AMediaFormat_getFloat(AMediaFormat *format, const char *name,
                                                     float *out)
{
    if (!format || !name || !out) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end() && it->second.type == AMediaFormat::Value::Type::Float) {
        *out = it->second.f_val;
        return true;
    }
    return false;
}

__attribute__((sysv_abi)) bool AMediaFormat_getSize(AMediaFormat *format, const char *name,
                                                    size_t *out)
{
    if (!format || !name || !out) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end()) {
        if (it->second.type == AMediaFormat::Value::Type::Size) {
            *out = it->second.size_val;
            return true;
        }
        if (it->second.type == AMediaFormat::Value::Type::Int32) {
            *out = static_cast<size_t>(it->second.i32_val);
            return true;
        }
    }
    return false;
}

__attribute__((sysv_abi)) bool AMediaFormat_getBuffer(AMediaFormat *format, const char *name,
                                                     void **data, size_t *size)
{
    if (!format || !name || !data || !size) {
        return false;
    }
    auto it = format->entries.find(name);
    if (it != format->entries.end() && it->second.type == AMediaFormat::Value::Type::Buffer) {
        *data = it->second.buf_val.data();
        *size = it->second.buf_val.size();
        return true;
    }
    return false;
}

__attribute__((sysv_abi)) void AMediaFormat_setString(AMediaFormat *format, const char *name,
                                                      const char *value)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::String;
    val.str_val = value ? value : "";
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) void AMediaFormat_setInt32(AMediaFormat *format, const char *name,
                                                     int32_t value)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::Int32;
    val.i32_val = value;
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) void AMediaFormat_setInt64(AMediaFormat *format, const char *name,
                                                     int64_t value)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::Int64;
    val.i64_val = value;
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) void AMediaFormat_setFloat(AMediaFormat *format, const char *name,
                                                     float value)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::Float;
    val.f_val = value;
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) void AMediaFormat_setSize(AMediaFormat *format, const char *name,
                                                    size_t value)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::Size;
    val.size_val = value;
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) void AMediaFormat_setBuffer(AMediaFormat *format, const char *name,
                                                      const void *data, size_t size)
{
    if (!format || !name) {
        return;
    }
    AMediaFormat::Value val;
    val.type = AMediaFormat::Value::Type::Buffer;
    if (data && size > 0) {
        const uint8_t *b = static_cast<const uint8_t *>(data);
        val.buf_val.assign(b, b + size);
    }
    format->entries[name] = std::move(val);
}

__attribute__((sysv_abi)) const char *AMediaFormat_toString(AMediaFormat *format)
{
    if (!format) {
        return "null";
    }
    std::ostringstream ss;
    ss << "AMediaFormat{";
    bool first = true;
    for (const auto &[k, v] : format->entries) {
        if (!first) {
            ss << ", ";
        }
        first = false;
        ss << k << "=";
        switch (v.type) {
        case AMediaFormat::Value::Type::String:
            ss << '"' << v.str_val << '"';
            break;
        case AMediaFormat::Value::Type::Int32:
            ss << v.i32_val;
            break;
        case AMediaFormat::Value::Type::Int64:
            ss << v.i64_val;
            break;
        case AMediaFormat::Value::Type::Float:
            ss << v.f_val;
            break;
        case AMediaFormat::Value::Type::Size:
            ss << v.size_val;
            break;
        case AMediaFormat::Value::Type::Buffer:
            ss << "Buffer[" << v.buf_val.size() << "]";
            break;
        }
    }
    ss << "}";
    format->to_string_cache = ss.str();
    return format->to_string_cache.c_str();
}

__attribute__((sysv_abi)) bool AMediaFormat_copy(AMediaFormat *to, AMediaFormat *from)
{
    if (!to || !from) {
        return false;
    }
    to->entries = from->entries;
    return true;
}

__attribute__((sysv_abi)) void AMediaFormat_clear(AMediaFormat *format)
{
    if (format) {
        format->entries.clear();
        format->to_string_cache.clear();
    }
}

} // extern "C"
