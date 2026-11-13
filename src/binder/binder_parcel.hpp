#pragma once

#include "binder_status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace portage::package {
struct JNIEnv;
}

typedef bool (*AParcel_stringAllocator)(void *stringData, int32_t length, char **outBuffer)
    __attribute__((sysv_abi));
typedef bool (*AParcel_byteArrayAllocator)(void *arrayData, int32_t length, int8_t **outBuffer)
    __attribute__((sysv_abi));

struct AParcel {
    std::vector<uint8_t> buffer{};
    mutable size_t data_pos{0};
    std::vector<int> fds{};
};

extern "C" {

__attribute__((sysv_abi)) AParcel *AParcel_create();

__attribute__((sysv_abi)) void AParcel_delete(AParcel *parcel);

__attribute__((sysv_abi)) void AParcel_reset(AParcel *parcel);

__attribute__((sysv_abi)) binder_status_t AParcel_setDataPosition(const AParcel *parcel,
                                                                  int32_t position);

__attribute__((sysv_abi)) int32_t AParcel_getDataPosition(const AParcel *parcel);

__attribute__((sysv_abi)) int32_t AParcel_getDataSize(const AParcel *parcel);

__attribute__((sysv_abi)) binder_status_t AParcel_setDataSize(AParcel *parcel, int32_t size);

__attribute__((sysv_abi)) binder_status_t AParcel_writeInt32(AParcel *parcel, int32_t value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeUint32(AParcel *parcel, uint32_t value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeInt64(AParcel *parcel, int64_t value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeUint64(AParcel *parcel, uint64_t value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeFloat(AParcel *parcel, float value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeDouble(AParcel *parcel, double value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeBool(AParcel *parcel, bool value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeChar(AParcel *parcel, char16_t value);

__attribute__((sysv_abi)) binder_status_t AParcel_writeString(AParcel *parcel, const char *string,
                                                              int32_t length);

__attribute__((sysv_abi)) binder_status_t AParcel_writeByteArray(AParcel *parcel,
                                                                 const int8_t *arrayData,
                                                                 int32_t length);

__attribute__((sysv_abi)) binder_status_t AParcel_writeParcelFileDescriptor(AParcel *parcel,
                                                                            int fd);

__attribute__((sysv_abi)) binder_status_t AParcel_writeStrongBinder(AParcel *parcel,
                                                                    struct AIBinder *binder);

__attribute__((sysv_abi)) binder_status_t AParcel_readInt32(const AParcel *parcel, int32_t *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readUint32(const AParcel *parcel, uint32_t *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readInt64(const AParcel *parcel, int64_t *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readUint64(const AParcel *parcel, uint64_t *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readFloat(const AParcel *parcel, float *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readDouble(const AParcel *parcel, double *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readBool(const AParcel *parcel, bool *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readChar(const AParcel *parcel, char16_t *out);

__attribute__((sysv_abi)) binder_status_t AParcel_readString(const AParcel *parcel,
                                                             void *stringData,
                                                             AParcel_stringAllocator allocator);

__attribute__((sysv_abi)) binder_status_t
AParcel_readByteArray(const AParcel *parcel, void *arrayData, AParcel_byteArrayAllocator allocator);

__attribute__((sysv_abi)) binder_status_t AParcel_readParcelFileDescriptor(const AParcel *parcel,
                                                                           int *outFd);

__attribute__((sysv_abi)) binder_status_t AParcel_readStrongBinder(const AParcel *parcel,
                                                                   struct AIBinder **binder);

__attribute__((sysv_abi)) AParcel *AParcel_fromJavaParcel(portage::package::JNIEnv *env,
                                                          void *javaParcel);

__attribute__((sysv_abi)) void *AParcel_toJavaParcel(const AParcel *parcel,
                                                     portage::package::JNIEnv *env);

} // extern "C"
