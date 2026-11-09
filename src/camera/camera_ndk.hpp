#pragma once

#include "camera/camera_types.hpp"

extern "C" {

__attribute__((sysv_abi)) ACameraManager *ACameraManager_create();
__attribute__((sysv_abi)) void ACameraManager_delete(ACameraManager *manager);
__attribute__((sysv_abi)) camera_status_t ACameraManager_getCameraIdList(ACameraManager *manager,
                                                                         ACameraIdList **cameraIdList);
__attribute__((sysv_abi)) void ACameraManager_deleteCameraIdList(ACameraIdList *cameraIdList);
__attribute__((sysv_abi)) camera_status_t
ACameraManager_getCameraCharacteristics(ACameraManager *manager, const char *cameraId,
                                        ACameraMetadata **characteristics);
__attribute__((sysv_abi)) camera_status_t
ACameraManager_openCamera(ACameraManager *manager, const char *cameraId,
                          ACameraDevice_StateCallbacks *callback, ACameraDevice **device);
__attribute__((sysv_abi)) camera_status_t
ACameraManager_registerAvailabilityCallback(ACameraManager *manager, const void *callback);
__attribute__((sysv_abi)) camera_status_t
ACameraManager_unregisterAvailabilityCallback(ACameraManager *manager, const void *callback);

__attribute__((sysv_abi)) camera_status_t ACameraDevice_close(ACameraDevice *device);
__attribute__((sysv_abi)) const char *ACameraDevice_getId(const ACameraDevice *device);
__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureRequest(const ACameraDevice *device,
                                   ACameraDevice_request_template templateId,
                                   ACaptureRequest **request);
__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureSession(ACameraDevice *device,
                                   const ACaptureSessionOutputContainer *outputs,
                                   const ACameraCaptureSession_stateCallbacks *callbacks,
                                   ACameraCaptureSession **session);
__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureSessionWithSessionParameters(
    ACameraDevice *device, const ACaptureSessionOutputContainer *outputs,
    const ACameraMetadata *sessionParameters,
    const ACameraCaptureSession_stateCallbacks *callbacks, ACameraCaptureSession **session);

__attribute__((sysv_abi)) void ACaptureRequest_free(ACaptureRequest *request);
__attribute__((sysv_abi)) camera_status_t ACaptureRequest_addTarget(ACaptureRequest *request,
                                                                    const ACameraOutputTarget *target);
__attribute__((sysv_abi)) camera_status_t
ACaptureRequest_removeTarget(ACaptureRequest *request, const ACameraOutputTarget *target);
__attribute__((sysv_abi)) camera_status_t
ACaptureRequest_getAllOptions(const ACaptureRequest *request, ACameraMetadata **metadata);
__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_u8(ACaptureRequest *request,
                                                                      uint32_t tag, uint32_t count,
                                                                      const uint8_t *data);
__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_i32(ACaptureRequest *request,
                                                                       uint32_t tag, uint32_t count,
                                                                       const int32_t *data);
__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_float(ACaptureRequest *request,
                                                                         uint32_t tag,
                                                                         uint32_t count,
                                                                         const float *data);

__attribute__((sysv_abi)) camera_status_t ACameraCaptureSession_close(ACameraCaptureSession *session);
__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_getDevice(ACameraCaptureSession *session, ACameraDevice **device);
__attribute__((sysv_abi)) camera_status_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession *session, ACameraCaptureSession_captureCallbacks *callbacks,
    int numRequests, ACaptureRequest **requests, int *captureSequenceId);
__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_capture(ACameraCaptureSession *session,
                              ACameraCaptureSession_captureCallbacks *callbacks, int numRequests,
                              ACaptureRequest **requests, int *captureSequenceId);
__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_stopRepeating(ACameraCaptureSession *session);
__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_abortCaptures(ACameraCaptureSession *session);

__attribute__((sysv_abi)) camera_status_t ACameraOutputTarget_create(ANativeWindow *window,
                                                                     ACameraOutputTarget **output);
__attribute__((sysv_abi)) void ACameraOutputTarget_free(ACameraOutputTarget *output);
__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutput_create(ANativeWindow *window, ACaptureSessionOutput **output);
__attribute__((sysv_abi)) void ACaptureSessionOutput_free(ACaptureSessionOutput *output);
__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer **container);
__attribute__((sysv_abi)) void
ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer *container);
__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer *container,
                                   const ACaptureSessionOutput *output);
__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_remove(ACaptureSessionOutputContainer *container,
                                      const ACaptureSessionOutput *output);

__attribute__((sysv_abi)) void ACameraMetadata_free(ACameraMetadata *metadata);
__attribute__((sysv_abi)) camera_status_t
ACameraMetadata_getConstEntry(const ACameraMetadata *metadata, uint32_t tag,
                              ACameraMetadata_const_entry *entry);

__attribute__((sysv_abi)) media_status_t AImageReader_new(int32_t width, int32_t height,
                                                          int32_t format, int32_t maxImages,
                                                          AImageReader **reader);
__attribute__((sysv_abi)) media_status_t AImageReader_newWithUsage(int32_t width, int32_t height,
                                                                   int32_t format, uint64_t usage,
                                                                   int32_t maxImages,
                                                                   AImageReader **reader);
__attribute__((sysv_abi)) void AImageReader_delete(AImageReader *reader);
__attribute__((sysv_abi)) media_status_t AImageReader_getWindow(AImageReader *reader,
                                                                ANativeWindow **window);
__attribute__((sysv_abi)) media_status_t AImageReader_acquireNextImage(AImageReader *reader,
                                                                       AImage **image);
__attribute__((sysv_abi)) media_status_t AImageReader_acquireLatestImage(AImageReader *reader,
                                                                         AImage **image);
__attribute__((sysv_abi)) media_status_t
AImageReader_setImageListener(AImageReader *reader, AImageReader_ImageListener *listener);

__attribute__((sysv_abi)) void AImage_delete(AImage *image);
__attribute__((sysv_abi)) media_status_t AImage_getWidth(const AImage *image, int32_t *width);
__attribute__((sysv_abi)) media_status_t AImage_getHeight(const AImage *image, int32_t *height);
__attribute__((sysv_abi)) media_status_t AImage_getFormat(const AImage *image, int32_t *format);
__attribute__((sysv_abi)) media_status_t AImage_getTimestamp(const AImage *image,
                                                             int64_t *timestamp);
__attribute__((sysv_abi)) media_status_t AImage_getNumberOfPlanes(const AImage *image,
                                                                  int32_t *numPlanes);
__attribute__((sysv_abi)) media_status_t AImage_getPlanePixelStride(const AImage *image,
                                                                    int32_t planeIdx,
                                                                    int32_t *pixelStride);
__attribute__((sysv_abi)) media_status_t AImage_getPlaneRowStride(const AImage *image,
                                                                  int32_t planeIdx,
                                                                  int32_t *rowStride);
__attribute__((sysv_abi)) media_status_t AImage_getPlaneData(const AImage *image, int32_t planeIdx,
                                                             uint8_t **data, int *dataLength);

} // extern "C"
