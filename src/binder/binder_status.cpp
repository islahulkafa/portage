#include "binder_status.hpp"

#include <sstream>

extern "C" {

__attribute__((sysv_abi)) AStatus *AStatus_newOk()
{
    return new AStatus{STATUS_OK, EX_NONE, 0, "", ""};
}

__attribute__((sysv_abi)) AStatus *AStatus_fromExceptionCode(binder_exception_t exception)
{
    return new AStatus{STATUS_UNKNOWN_ERROR, exception, 0, "", ""};
}

__attribute__((sysv_abi)) AStatus *
AStatus_fromExceptionCodeWithMessage(binder_exception_t exception, const char *message)
{
    return new AStatus{STATUS_UNKNOWN_ERROR, exception, 0, message ? message : "", ""};
}

__attribute__((sysv_abi)) AStatus *
AStatus_fromServiceSpecificError(int32_t serviceSpecificError)
{
    return new AStatus{STATUS_UNKNOWN_ERROR, EX_SERVICE_SPECIFIC, serviceSpecificError, "", ""};
}

__attribute__((sysv_abi)) AStatus *
AStatus_fromServiceSpecificErrorWithMessage(int32_t serviceSpecificError, const char *message)
{
    return new AStatus{STATUS_UNKNOWN_ERROR, EX_SERVICE_SPECIFIC, serviceSpecificError,
                       message ? message : "", ""};
}

__attribute__((sysv_abi)) AStatus *AStatus_fromStatus(binder_status_t status)
{
    return new AStatus{status, status == STATUS_OK ? EX_NONE : EX_TRANSACTION_FAILED, 0, "", ""};
}

__attribute__((sysv_abi)) void AStatus_delete(AStatus *status)
{
    delete status;
}

__attribute__((sysv_abi)) bool AStatus_isOk(const AStatus *status)
{
    return status && status->status == STATUS_OK && status->exception_code == EX_NONE;
}

__attribute__((sysv_abi)) binder_exception_t AStatus_getExceptionCode(const AStatus *status)
{
    return status ? status->exception_code : EX_NONE;
}

__attribute__((sysv_abi)) int32_t AStatus_getServiceSpecificError(const AStatus *status)
{
    return status ? status->service_specific_error : 0;
}

__attribute__((sysv_abi)) binder_status_t AStatus_getStatus(const AStatus *status)
{
    return status ? status->status : STATUS_BAD_VALUE;
}

__attribute__((sysv_abi)) const char *AStatus_getMessage(const AStatus *status)
{
    return (status && !status->message.empty()) ? status->message.c_str() : "";
}

__attribute__((sysv_abi)) const char *AStatus_getDescription(const AStatus *status)
{
    if (!status) {
        return "(null status)";
    }
    if (status->status == STATUS_OK && status->exception_code == EX_NONE) {
        return "Status: OK";
    }

    std::ostringstream oss;
    oss << "Status: " << status->status << ", Exception: " << status->exception_code;
    if (status->exception_code == EX_SERVICE_SPECIFIC) {
        oss << ", ServiceSpecific: " << status->service_specific_error;
    }
    if (!status->message.empty()) {
        oss << " (" << status->message << ")";
    }
    status->description_cache = oss.str();
    return status->description_cache.c_str();
}

} // extern "C"
