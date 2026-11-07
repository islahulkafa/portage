#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t binder_status_t;
typedef int32_t binder_exception_t;
typedef uint32_t transaction_code_t;
typedef uint32_t binder_flags_t;

struct AStatus;
typedef struct AStatus AStatus;

struct AParcel;
typedef struct AParcel AParcel;

struct AIBinder;
typedef struct AIBinder AIBinder;

struct AIBinder_Class;
typedef struct AIBinder_Class AIBinder_Class;

typedef bool (*AParcel_stringAllocator)(void *stringData, int32_t length, char **outBuffer);
typedef bool (*AParcel_byteArrayAllocator)(void *arrayData, int32_t length, int8_t **outBuffer);

typedef void *(*AIBinder_Class_onCreate)(void *args);
typedef void (*AIBinder_Class_onDestroy)(void *userData);
typedef binder_status_t (*AIBinder_Class_onTransact)(AIBinder *binder, transaction_code_t code,
                                                     const AParcel *in, AParcel *out);

// AStatus exports
__attribute__((visibility("default"))) AStatus *AStatus_newOk(void)
{
    return (AStatus *)0;
}

__attribute__((visibility("default"))) AStatus *AStatus_fromExceptionCode(binder_exception_t exception)
{
    (void)exception;
    return (AStatus *)0;
}

__attribute__((visibility("default"))) AStatus *
AStatus_fromExceptionCodeWithMessage(binder_exception_t exception, const char *message)
{
    (void)exception;
    (void)message;
    return (AStatus *)0;
}

__attribute__((visibility("default"))) AStatus *
AStatus_fromServiceSpecificError(int32_t serviceSpecificError)
{
    (void)serviceSpecificError;
    return (AStatus *)0;
}

__attribute__((visibility("default"))) AStatus *
AStatus_fromServiceSpecificErrorWithMessage(int32_t serviceSpecificError, const char *message)
{
    (void)serviceSpecificError;
    (void)message;
    return (AStatus *)0;
}

__attribute__((visibility("default"))) AStatus *AStatus_fromStatus(binder_status_t status)
{
    (void)status;
    return (AStatus *)0;
}

__attribute__((visibility("default"))) void AStatus_delete(AStatus *status)
{
    (void)status;
}

__attribute__((visibility("default"))) bool AStatus_isOk(const AStatus *status)
{
    (void)status;
    return false;
}

__attribute__((visibility("default"))) binder_exception_t AStatus_getExceptionCode(const AStatus *status)
{
    (void)status;
    return 0;
}

__attribute__((visibility("default"))) int32_t AStatus_getServiceSpecificError(const AStatus *status)
{
    (void)status;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AStatus_getStatus(const AStatus *status)
{
    (void)status;
    return 0;
}

__attribute__((visibility("default"))) const char *AStatus_getMessage(const AStatus *status)
{
    (void)status;
    return "";
}

__attribute__((visibility("default"))) const char *AStatus_getDescription(const AStatus *status)
{
    (void)status;
    return "";
}

// AParcel exports
__attribute__((visibility("default"))) AParcel *AParcel_create(void)
{
    return (AParcel *)0;
}

__attribute__((visibility("default"))) void AParcel_delete(AParcel *parcel)
{
    (void)parcel;
}

__attribute__((visibility("default"))) void AParcel_reset(AParcel *parcel)
{
    (void)parcel;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_setDataPosition(const AParcel *parcel, int32_t position)
{
    (void)parcel;
    (void)position;
    return 0;
}

__attribute__((visibility("default"))) int32_t AParcel_getDataPosition(const AParcel *parcel)
{
    (void)parcel;
    return 0;
}

__attribute__((visibility("default"))) int32_t AParcel_getDataSize(const AParcel *parcel)
{
    (void)parcel;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_setDataSize(AParcel *parcel, int32_t size)
{
    (void)parcel;
    (void)size;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeInt32(AParcel *parcel, int32_t value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeUint32(AParcel *parcel, uint32_t value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeInt64(AParcel *parcel, int64_t value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeUint64(AParcel *parcel, uint64_t value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeFloat(AParcel *parcel, float value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeDouble(AParcel *parcel, double value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeBool(AParcel *parcel, bool value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t AParcel_writeChar(AParcel *parcel, uint16_t value)
{
    (void)parcel;
    (void)value;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_writeString(AParcel *parcel, const char *string, int32_t length)
{
    (void)parcel;
    (void)string;
    (void)length;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_writeByteArray(AParcel *parcel, const int8_t *arrayData, int32_t length)
{
    (void)parcel;
    (void)arrayData;
    (void)length;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_writeParcelFileDescriptor(AParcel *parcel, int fd)
{
    (void)parcel;
    (void)fd;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readInt32(const AParcel *parcel, int32_t *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readUint32(const AParcel *parcel, uint32_t *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readInt64(const AParcel *parcel, int64_t *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readUint64(const AParcel *parcel, uint64_t *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readFloat(const AParcel *parcel, float *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readDouble(const AParcel *parcel, double *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readBool(const AParcel *parcel, bool *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readChar(const AParcel *parcel, uint16_t *out)
{
    (void)parcel;
    (void)out;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readString(const AParcel *parcel, void *stringData, AParcel_stringAllocator allocator)
{
    (void)parcel;
    (void)stringData;
    (void)allocator;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readByteArray(const AParcel *parcel, void *arrayData, AParcel_byteArrayAllocator allocator)
{
    (void)parcel;
    (void)arrayData;
    (void)allocator;
    return 0;
}

__attribute__((visibility("default"))) binder_status_t
AParcel_readParcelFileDescriptor(const AParcel *parcel, int *outFd)
{
    (void)parcel;
    (void)outFd;
    return 0;
}

// AIBinder exports
__attribute__((visibility("default"))) AIBinder_Class *
AIBinder_Class_define(const char *interfaceDescriptor, AIBinder_Class_onCreate onCreate,
                      AIBinder_Class_onDestroy onDestroy, AIBinder_Class_onTransact onTransact)
{
    (void)interfaceDescriptor;
    (void)onCreate;
    (void)onDestroy;
    (void)onTransact;
    return (AIBinder_Class *)0;
}

__attribute__((visibility("default"))) AIBinder *AIBinder_new(const AIBinder_Class *clazz, void *args)
{
    (void)clazz;
    (void)args;
    return (AIBinder *)0;
}

__attribute__((visibility("default"))) binder_status_t AIBinder_transact(AIBinder *binder,
                                                                       transaction_code_t code,
                                                                       const AParcel *in,
                                                                       AParcel *out,
                                                                       binder_flags_t flags)
{
    (void)binder;
    (void)code;
    (void)in;
    (void)out;
    (void)flags;
    return 0;
}

__attribute__((visibility("default"))) void AIBinder_incStrong(AIBinder *binder)
{
    (void)binder;
}

__attribute__((visibility("default"))) void AIBinder_decStrong(AIBinder *binder)
{
    (void)binder;
}

__attribute__((visibility("default"))) int32_t AIBinder_getCallingPid(void)
{
    return 0;
}

__attribute__((visibility("default"))) int32_t AIBinder_getCallingUid(void)
{
    return 0;
}

__attribute__((visibility("default"))) bool AIBinder_isAlive(const AIBinder *binder)
{
    (void)binder;
    return false;
}

__attribute__((visibility("default"))) bool AIBinder_isRemote(const AIBinder *binder)
{
    (void)binder;
    return false;
}

__attribute__((visibility("default"))) void *AIBinder_getUserData(AIBinder *binder)
{
    (void)binder;
    return (void *)0;
}

__attribute__((visibility("default"))) const char *
AIBinder_Class_getDescriptor(const AIBinder_Class *clazz)
{
    (void)clazz;
    return "";
}

__attribute__((visibility("default"))) bool AIBinder_associateClass(AIBinder *binder,
                                                                  const AIBinder_Class *clazz)
{
    (void)binder;
    (void)clazz;
    return false;
}

__attribute__((visibility("default"))) const AIBinder_Class *AIBinder_getClass(AIBinder *binder)
{
    (void)binder;
    return (const AIBinder_Class *)0;
}

// AServiceManager exports
__attribute__((visibility("default"))) binder_status_t
AServiceManager_addService(AIBinder *binder, const char *instance)
{
    (void)binder;
    (void)instance;
    return 0;
}

__attribute__((visibility("default"))) AIBinder *AServiceManager_checkService(const char *instance)
{
    (void)instance;
    return (AIBinder *)0;
}

__attribute__((visibility("default"))) AIBinder *AServiceManager_getService(const char *instance)
{
    (void)instance;
    return (AIBinder *)0;
}

__attribute__((visibility("default"))) AIBinder *AServiceManager_waitForService(const char *instance)
{
    (void)instance;
    return (AIBinder *)0;
}
