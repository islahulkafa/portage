#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ACameraManager;
typedef struct ACameraManager ACameraManager;

struct ACameraDevice;
typedef struct ACameraDevice ACameraDevice;

struct ACameraCaptureSession;
typedef struct ACameraCaptureSession ACameraCaptureSession;

struct ACaptureRequest;
typedef struct ACaptureRequest ACaptureRequest;

struct ACameraOutputTarget;
typedef struct ACameraOutputTarget ACameraOutputTarget;

struct ACaptureSessionOutput;
typedef struct ACaptureSessionOutput ACaptureSessionOutput;

struct ACaptureSessionOutputContainer;
typedef struct ACaptureSessionOutputContainer ACaptureSessionOutputContainer;

struct ACameraMetadata;
typedef struct ACameraMetadata ACameraMetadata;

struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;

typedef struct ACameraIdList {
    int numCameras;
    const char **cameraIds;
} ACameraIdList;

typedef struct ACameraDevice_StateCallbacks {
    void *context;
    void (*onDisconnected)(void *context, ACameraDevice *device);
    void (*onError)(void *context, ACameraDevice *device, int error);
} ACameraDevice_StateCallbacks;

typedef struct ACameraCaptureSession_stateCallbacks {
    void *context;
    void (*onClosed)(void *context, ACameraCaptureSession *session);
    void (*onReady)(void *context, ACameraCaptureSession *session);
    void (*onActive)(void *context, ACameraCaptureSession *session);
} ACameraCaptureSession_stateCallbacks;

typedef struct ACameraCaptureSession_captureCallbacks {
    void *context;
    void (*onCaptureStarted)(void *context, ACameraCaptureSession *session,
                             const ACaptureRequest *request, int64_t timestamp);
    void (*onCaptureProgressed)(void *context, ACameraCaptureSession *session,
                               ACaptureRequest *request, const ACameraMetadata *result);
    void (*onCaptureCompleted)(void *context, ACameraCaptureSession *session,
                              ACaptureRequest *request, const ACameraMetadata *result);
    void (*onCaptureFailed)(void *context, ACameraCaptureSession *session,
                            ACaptureRequest *request, void *failure);
    void (*onCaptureSequenceCompleted)(void *context, ACameraCaptureSession *session,
                                       int sequenceId, int64_t frameNumber);
    void (*onCaptureSequenceAborted)(void *context, ACameraCaptureSession *session, int sequenceId);
    void (*onCaptureBufferLost)(void *context, ACameraCaptureSession *session,
                               ACaptureRequest *request, void *window, int64_t frameNumber);
} ACameraCaptureSession_captureCallbacks;

typedef struct ACameraMetadata_const_entry {
    uint32_t tag;
    uint8_t type;
    uint32_t count;
    union {
        const uint8_t *u8;
        const int32_t *i32;
        const float *f;
        const int64_t *i64;
        const double *d;
        const void *rational;
    } data;
} ACameraMetadata_const_entry;

__attribute__((visibility("default"))) ACameraManager *ACameraManager_create(void)
{
    return (ACameraManager *)0;
}

__attribute__((visibility("default"))) void ACameraManager_delete(ACameraManager *manager)
{
    (void)manager;
}

__attribute__((visibility("default"))) int32_t
ACameraManager_getCameraIdList(ACameraManager *manager, ACameraIdList **cameraIdList)
{
    (void)manager;
    (void)cameraIdList;
    return 0;
}

__attribute__((visibility("default"))) void
ACameraManager_deleteCameraIdList(ACameraIdList *cameraIdList)
{
    (void)cameraIdList;
}

__attribute__((visibility("default"))) int32_t
ACameraManager_getCameraCharacteristics(ACameraManager *manager, const char *cameraId,
                                        ACameraMetadata **characteristics)
{
    (void)manager;
    (void)cameraId;
    (void)characteristics;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraManager_openCamera(ACameraManager *manager, const char *cameraId,
                          ACameraDevice_StateCallbacks *callback, ACameraDevice **device)
{
    (void)manager;
    (void)cameraId;
    (void)callback;
    (void)device;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraManager_registerAvailabilityCallback(ACameraManager *manager, const void *callback)
{
    (void)manager;
    (void)callback;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraManager_unregisterAvailabilityCallback(ACameraManager *manager, const void *callback)
{
    (void)manager;
    (void)callback;
    return 0;
}

__attribute__((visibility("default"))) int32_t ACameraDevice_close(ACameraDevice *device)
{
    (void)device;
    return 0;
}

__attribute__((visibility("default"))) const char *ACameraDevice_getId(const ACameraDevice *device)
{
    (void)device;
    return "";
}

__attribute__((visibility("default"))) int32_t
ACameraDevice_createCaptureRequest(const ACameraDevice *device, int templateId,
                                   ACaptureRequest **request)
{
    (void)device;
    (void)templateId;
    (void)request;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraDevice_createCaptureSession(ACameraDevice *device,
                                   const ACaptureSessionOutputContainer *outputs,
                                   const ACameraCaptureSession_stateCallbacks *callbacks,
                                   ACameraCaptureSession **session)
{
    (void)device;
    (void)outputs;
    (void)callbacks;
    (void)session;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraDevice_createCaptureSessionWithSessionParameters(
    ACameraDevice *device, const ACaptureSessionOutputContainer *outputs,
    const ACameraMetadata *sessionParameters,
    const ACameraCaptureSession_stateCallbacks *callbacks, ACameraCaptureSession **session)
{
    (void)device;
    (void)outputs;
    (void)sessionParameters;
    (void)callbacks;
    (void)session;
    return 0;
}

__attribute__((visibility("default"))) void ACaptureRequest_free(ACaptureRequest *request)
{
    (void)request;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_addTarget(ACaptureRequest *request, const ACameraOutputTarget *target)
{
    (void)request;
    (void)target;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_removeTarget(ACaptureRequest *request, const ACameraOutputTarget *target)
{
    (void)request;
    (void)target;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_getAllOptions(const ACaptureRequest *request, ACameraMetadata **metadata)
{
    (void)request;
    (void)metadata;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_setEntry_u8(ACaptureRequest *request, uint32_t tag, uint32_t count,
                            const uint8_t *data)
{
    (void)request;
    (void)tag;
    (void)count;
    (void)data;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_setEntry_i32(ACaptureRequest *request, uint32_t tag, uint32_t count,
                             const int32_t *data)
{
    (void)request;
    (void)tag;
    (void)count;
    (void)data;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureRequest_setEntry_float(ACaptureRequest *request, uint32_t tag, uint32_t count,
                               const float *data)
{
    (void)request;
    (void)tag;
    (void)count;
    (void)data;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraCaptureSession_close(ACameraCaptureSession *session)
{
    (void)session;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraCaptureSession_getDevice(ACameraCaptureSession *session, ACameraDevice **device)
{
    (void)session;
    (void)device;
    return 0;
}

__attribute__((visibility("default"))) int32_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession *session, ACameraCaptureSession_captureCallbacks *callbacks,
    int numRequests, ACaptureRequest **requests, int *captureSequenceId)
{
    (void)session;
    (void)callbacks;
    (void)numRequests;
    (void)requests;
    (void)captureSequenceId;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraCaptureSession_capture(ACameraCaptureSession *session,
                              ACameraCaptureSession_captureCallbacks *callbacks, int numRequests,
                              ACaptureRequest **requests, int *captureSequenceId)
{
    (void)session;
    (void)callbacks;
    (void)numRequests;
    (void)requests;
    (void)captureSequenceId;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraCaptureSession_stopRepeating(ACameraCaptureSession *session)
{
    (void)session;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraCaptureSession_abortCaptures(ACameraCaptureSession *session)
{
    (void)session;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACameraOutputTarget_create(ANativeWindow *window, ACameraOutputTarget **output)
{
    (void)window;
    (void)output;
    return 0;
}

__attribute__((visibility("default"))) void ACameraOutputTarget_free(ACameraOutputTarget *output)
{
    (void)output;
}

__attribute__((visibility("default"))) int32_t
ACaptureSessionOutput_create(ANativeWindow *window, ACaptureSessionOutput **output)
{
    (void)window;
    (void)output;
    return 0;
}

__attribute__((visibility("default"))) void
ACaptureSessionOutput_free(ACaptureSessionOutput *output)
{
    (void)output;
}

__attribute__((visibility("default"))) int32_t
ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer **container)
{
    (void)container;
    return 0;
}

__attribute__((visibility("default"))) void
ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer *container)
{
    (void)container;
}

__attribute__((visibility("default"))) int32_t
ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer *container,
                                   const ACaptureSessionOutput *output)
{
    (void)container;
    (void)output;
    return 0;
}

__attribute__((visibility("default"))) int32_t
ACaptureSessionOutputContainer_remove(ACaptureSessionOutputContainer *container,
                                      const ACaptureSessionOutput *output)
{
    (void)container;
    (void)output;
    return 0;
}

__attribute__((visibility("default"))) void ACameraMetadata_free(ACameraMetadata *metadata)
{
    (void)metadata;
}

__attribute__((visibility("default"))) int32_t
ACameraMetadata_getConstEntry(const ACameraMetadata *metadata, uint32_t tag,
                              ACameraMetadata_const_entry *entry)
{
    (void)metadata;
    (void)tag;
    (void)entry;
    return 0;
}
