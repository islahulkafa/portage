#pragma once

#include <cstdint>
#include <string>

typedef int32_t binder_status_t;
typedef int32_t binder_exception_t;

enum : binder_status_t {
    STATUS_OK = 0,
    STATUS_UNKNOWN_ERROR = -1,
    STATUS_NO_MEMORY = -12,
    STATUS_INVALID_OPERATION = -38,
    STATUS_BAD_VALUE = -22,
    STATUS_BAD_TYPE = -1001,
    STATUS_NAME_NOT_FOUND = -2,
    STATUS_PERMISSION_DENIED = -1,
    STATUS_NO_INIT = -19,
    STATUS_ALREADY_EXISTS = -17,
    STATUS_DEAD_OBJECT = -32,
    STATUS_FAILED_TRANSACTION = -1002,
    STATUS_BAD_INDEX = -75,
    STATUS_NOT_ENOUGH_DATA = -61,
    STATUS_WOULD_BLOCK = -11,
    STATUS_TIMED_OUT = -110,
    STATUS_UNKNOWN_TRANSACTION = -74,
    STATUS_FDS_NOT_ALLOWED = -1003,
    STATUS_UNEXPECTED_NULL = -1004,
};

enum : binder_exception_t {
    EX_NONE = 0,
    EX_SECURITY = -1,
    EX_BAD_PARCELABLE = -2,
    EX_ILLEGAL_ARGUMENT = -3,
    EX_NULL_POINTER = -4,
    EX_ILLEGAL_STATE = -5,
    EX_NETWORK_MAIN_THREAD = -6,
    EX_UNSUPPORTED_OPERATION = -7,
    EX_SERVICE_SPECIFIC = -8,
    EX_PARCELABLE = -9,
    EX_HAS_NOT_PROMOTED = -10,
    EX_TRANSACTION_FAILED = -129,
};

struct AStatus {
    binder_status_t status{STATUS_OK};
    binder_exception_t exception_code{EX_NONE};
    int32_t service_specific_error{0};
    std::string message{};
    mutable std::string description_cache{};
};

extern "C" {

__attribute__((sysv_abi)) AStatus *AStatus_newOk();

__attribute__((sysv_abi)) AStatus *AStatus_fromExceptionCode(binder_exception_t exception);

__attribute__((sysv_abi)) AStatus *
AStatus_fromExceptionCodeWithMessage(binder_exception_t exception, const char *message);

__attribute__((sysv_abi)) AStatus *
AStatus_fromServiceSpecificError(int32_t serviceSpecificError);

__attribute__((sysv_abi)) AStatus *
AStatus_fromServiceSpecificErrorWithMessage(int32_t serviceSpecificError, const char *message);

__attribute__((sysv_abi)) AStatus *AStatus_fromStatus(binder_status_t status);

__attribute__((sysv_abi)) void AStatus_delete(AStatus *status);

__attribute__((sysv_abi)) bool AStatus_isOk(const AStatus *status);

__attribute__((sysv_abi)) binder_exception_t AStatus_getExceptionCode(const AStatus *status);

__attribute__((sysv_abi)) int32_t AStatus_getServiceSpecificError(const AStatus *status);

__attribute__((sysv_abi)) binder_status_t AStatus_getStatus(const AStatus *status);

__attribute__((sysv_abi)) const char *AStatus_getMessage(const AStatus *status);

__attribute__((sysv_abi)) const char *AStatus_getDescription(const AStatus *status);

} // extern "C"
