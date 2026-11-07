#include "binder_parcel.hpp"
#include "fs/fd_table.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <algorithm>
#include <cstring>

namespace {

inline size_t Pad4(size_t size) noexcept
{
    return (size + 3u) & ~3u;
}

template <typename T>
binder_status_t WriteAligned(AParcel *parcel, const T &val)
{
    if (!parcel) {
        return STATUS_BAD_VALUE;
    }
    size_t sz = sizeof(T);
    size_t padded = Pad4(sz);
    size_t cur = parcel->data_pos;
    if (cur + padded > parcel->buffer.size()) {
        parcel->buffer.resize(cur + padded, 0);
    }
    std::memcpy(parcel->buffer.data() + cur, &val, sz);
    if (padded > sz) {
        std::memset(parcel->buffer.data() + cur + sz, 0, padded - sz);
    }
    parcel->data_pos += padded;
    return STATUS_OK;
}

template <typename T>
binder_status_t ReadAligned(const AParcel *parcel, T *out)
{
    if (!parcel || !out) {
        return STATUS_BAD_VALUE;
    }
    size_t sz = sizeof(T);
    size_t padded = Pad4(sz);
    if (parcel->data_pos + padded > parcel->buffer.size()) {
        return STATUS_NOT_ENOUGH_DATA;
    }
    std::memcpy(out, parcel->buffer.data() + parcel->data_pos, sz);
    parcel->data_pos += padded;
    return STATUS_OK;
}

} // namespace

extern "C" {

__attribute__((sysv_abi)) AParcel *AParcel_create()
{
    return new AParcel();
}

__attribute__((sysv_abi)) void AParcel_delete(AParcel *parcel)
{
    if (!parcel) {
        return;
    }
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (fd_table) {
        for (int fd : parcel->fds) {
            if (fd >= 0) {
                fd_table->Close(fd);
            }
        }
    }
    delete parcel;
}

__attribute__((sysv_abi)) void AParcel_reset(AParcel *parcel)
{
    if (!parcel) {
        return;
    }
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (fd_table) {
        for (int fd : parcel->fds) {
            if (fd >= 0) {
                fd_table->Close(fd);
            }
        }
    }
    parcel->fds.clear();
    parcel->buffer.clear();
    parcel->data_pos = 0;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_setDataPosition(const AParcel *parcel, int32_t position)
{
    if (!parcel || position < 0 || static_cast<size_t>(position) > parcel->buffer.size()) {
        return STATUS_BAD_VALUE;
    }
    parcel->data_pos = static_cast<size_t>(position);
    return STATUS_OK;
}

__attribute__((sysv_abi)) int32_t AParcel_getDataPosition(const AParcel *parcel)
{
    return parcel ? static_cast<int32_t>(parcel->data_pos) : 0;
}

__attribute__((sysv_abi)) int32_t AParcel_getDataSize(const AParcel *parcel)
{
    return parcel ? static_cast<int32_t>(parcel->buffer.size()) : 0;
}

__attribute__((sysv_abi)) binder_status_t AParcel_setDataSize(AParcel *parcel, int32_t size)
{
    if (!parcel || size < 0) {
        return STATUS_BAD_VALUE;
    }
    parcel->buffer.resize(static_cast<size_t>(size), 0);
    if (parcel->data_pos > parcel->buffer.size()) {
        parcel->data_pos = parcel->buffer.size();
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeInt32(AParcel *parcel, int32_t value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeUint32(AParcel *parcel, uint32_t value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeInt64(AParcel *parcel, int64_t value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeUint64(AParcel *parcel, uint64_t value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeFloat(AParcel *parcel, float value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeDouble(AParcel *parcel, double value)
{
    return WriteAligned(parcel, value);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeBool(AParcel *parcel, bool value)
{
    int32_t b = value ? 1 : 0;
    return WriteAligned(parcel, b);
}

__attribute__((sysv_abi)) binder_status_t AParcel_writeChar(AParcel *parcel, char16_t value)
{
    int32_t c = static_cast<int32_t>(value);
    return WriteAligned(parcel, c);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_writeString(AParcel *parcel, const char *string, int32_t length)
{
    if (!parcel) {
        return STATUS_BAD_VALUE;
    }
    if (!string) {
        int32_t null_indicator = -1;
        return WriteAligned(parcel, null_indicator);
    }

    size_t str_len = (length < 0) ? std::strlen(string) : static_cast<size_t>(length);
    binder_status_t status = WriteAligned(parcel, static_cast<int32_t>(str_len));
    if (status != STATUS_OK) {
        return status;
    }

    size_t write_bytes = str_len + 1u; // include null terminator
    size_t padded = Pad4(write_bytes);
    size_t cur = parcel->data_pos;
    if (cur + padded > parcel->buffer.size()) {
        parcel->buffer.resize(cur + padded, 0);
    }
    std::memcpy(parcel->buffer.data() + cur, string, str_len);
    parcel->buffer[cur + str_len] = '\0';
    if (padded > write_bytes) {
        std::memset(parcel->buffer.data() + cur + write_bytes, 0, padded - write_bytes);
    }
    parcel->data_pos += padded;
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_writeByteArray(AParcel *parcel, const int8_t *arrayData, int32_t length)
{
    if (!parcel) {
        return STATUS_BAD_VALUE;
    }
    if (!arrayData || length < 0) {
        int32_t null_indicator = -1;
        return WriteAligned(parcel, null_indicator);
    }

    binder_status_t status = WriteAligned(parcel, length);
    if (status != STATUS_OK) {
        return status;
    }

    size_t raw_sz = static_cast<size_t>(length);
    size_t padded = Pad4(raw_sz);
    size_t cur = parcel->data_pos;
    if (cur + padded > parcel->buffer.size()) {
        parcel->buffer.resize(cur + padded, 0);
    }
    std::memcpy(parcel->buffer.data() + cur, arrayData, raw_sz);
    if (padded > raw_sz) {
        std::memset(parcel->buffer.data() + cur + raw_sz, 0, padded - raw_sz);
    }
    parcel->data_pos += padded;
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_writeParcelFileDescriptor(AParcel *parcel, int fd)
{
    if (!parcel) {
        return STATUS_BAD_VALUE;
    }
    if (fd < 0) {
        int32_t flag = 0;
        return WriteAligned(parcel, flag);
    }

    int dup_fd = fd;
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (fd_table) {
        dup_fd = fd_table->Duplicate(fd);
        if (dup_fd < 0) {
            return STATUS_UNKNOWN_ERROR;
        }
    }
    parcel->fds.push_back(dup_fd);

    binder_status_t s1 = WriteAligned(parcel, static_cast<int32_t>(1));
    if (s1 != STATUS_OK) {
        return s1;
    }
    return WriteAligned(parcel, dup_fd);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readInt32(const AParcel *parcel, int32_t *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readUint32(const AParcel *parcel, uint32_t *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readInt64(const AParcel *parcel, int64_t *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readUint64(const AParcel *parcel, uint64_t *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readFloat(const AParcel *parcel, float *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readDouble(const AParcel *parcel, double *out)
{
    return ReadAligned(parcel, out);
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readBool(const AParcel *parcel, bool *out)
{
    int32_t b = 0;
    binder_status_t s = ReadAligned(parcel, &b);
    if (s == STATUS_OK && out) {
        *out = (b != 0);
    }
    return s;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readChar(const AParcel *parcel, char16_t *out)
{
    int32_t c = 0;
    binder_status_t s = ReadAligned(parcel, &c);
    if (s == STATUS_OK && out) {
        *out = static_cast<char16_t>(c);
    }
    return s;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readString(const AParcel *parcel, void *stringData, AParcel_stringAllocator allocator)
{
    if (!parcel || !allocator) {
        return STATUS_BAD_VALUE;
    }
    int32_t len = 0;
    binder_status_t s = ReadAligned(parcel, &len);
    if (s != STATUS_OK) {
        return s;
    }
    if (len < 0) {
        allocator(stringData, -1, nullptr);
        return STATUS_OK;
    }

    size_t str_len = static_cast<size_t>(len);
    size_t read_bytes = str_len + 1u;
    size_t padded = Pad4(read_bytes);
    if (parcel->data_pos + padded > parcel->buffer.size()) {
        return STATUS_NOT_ENOUGH_DATA;
    }

    char *out_buf = nullptr;
    if (!allocator(stringData, len, &out_buf) || !out_buf) {
        return STATUS_NO_MEMORY;
    }

    std::memcpy(out_buf, parcel->buffer.data() + parcel->data_pos, str_len);
    out_buf[str_len] = '\0';
    parcel->data_pos += padded;
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readByteArray(const AParcel *parcel, void *arrayData, AParcel_byteArrayAllocator allocator)
{
    if (!parcel || !allocator) {
        return STATUS_BAD_VALUE;
    }
    int32_t len = 0;
    binder_status_t s = ReadAligned(parcel, &len);
    if (s != STATUS_OK) {
        return s;
    }
    if (len < 0) {
        allocator(arrayData, -1, nullptr);
        return STATUS_OK;
    }

    size_t raw_sz = static_cast<size_t>(len);
    size_t padded = Pad4(raw_sz);
    if (parcel->data_pos + padded > parcel->buffer.size()) {
        return STATUS_NOT_ENOUGH_DATA;
    }

    int8_t *out_buf = nullptr;
    if (!allocator(arrayData, len, &out_buf) || !out_buf) {
        return STATUS_NO_MEMORY;
    }

    std::memcpy(out_buf, parcel->buffer.data() + parcel->data_pos, raw_sz);
    parcel->data_pos += padded;
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
AParcel_readParcelFileDescriptor(const AParcel *parcel, int *outFd)
{
    if (!parcel || !outFd) {
        return STATUS_BAD_VALUE;
    }
    int32_t has_fd = 0;
    binder_status_t s = ReadAligned(parcel, &has_fd);
    if (s != STATUS_OK) {
        return s;
    }
    if (!has_fd) {
        *outFd = -1;
        return STATUS_OK;
    }

    int stored_fd = -1;
    s = ReadAligned(parcel, &stored_fd);
    if (s != STATUS_OK) {
        return s;
    }

    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (fd_table && stored_fd >= 0) {
        *outFd = fd_table->Duplicate(stored_fd);
    } else {
        *outFd = stored_fd;
    }
    return STATUS_OK;
}

} // extern "C"
