#pragma once

#include "media/media_types.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

typedef enum {
    ACAMERA_OK = 0,
    ACAMERA_ERROR_BASE = -10000,
    ACAMERA_ERROR_UNKNOWN = ACAMERA_ERROR_BASE,
    ACAMERA_ERROR_INVALID_PARAMETER = ACAMERA_ERROR_BASE - 1,
    ACAMERA_ERROR_CAMERA_DISCONNECTED = ACAMERA_ERROR_BASE - 2,
    ACAMERA_ERROR_NOT_ENOUGH_MEMORY = ACAMERA_ERROR_BASE - 3,
    ACAMERA_ERROR_METADATA_NOT_FOUND = ACAMERA_ERROR_BASE - 4,
    ACAMERA_ERROR_CAMERA_DEVICE = ACAMERA_ERROR_BASE - 5,
    ACAMERA_ERROR_CAMERA_SERVICE = ACAMERA_ERROR_BASE - 6,
    ACAMERA_ERROR_SESSION_CLOSED = ACAMERA_ERROR_BASE - 7,
    ACAMERA_ERROR_INVALID_OPERATION = ACAMERA_ERROR_BASE - 8,
    ACAMERA_ERROR_STREAM_CONFIGURE_FAIL = ACAMERA_ERROR_BASE - 9,
    ACAMERA_ERROR_CAMERA_IN_USE = ACAMERA_ERROR_BASE - 10,
    ACAMERA_ERROR_MAX_CAMERA_IN_USE = ACAMERA_ERROR_BASE - 11,
    ACAMERA_ERROR_CAMERA_DISABLED = ACAMERA_ERROR_BASE - 12,
    ACAMERA_ERROR_PERMISSION_DENIED = ACAMERA_ERROR_BASE - 13,
    ACAMERA_ERROR_UNSUPPORTED_OPERATION = ACAMERA_ERROR_BASE - 14,
} camera_status_t;

enum AIMAGE_FORMATS {
    AIMAGE_FORMAT_RGBA_8888 = 1,
    AIMAGE_FORMAT_RGBX_8888 = 2,
    AIMAGE_FORMAT_RGB_888 = 3,
    AIMAGE_FORMAT_RGB_565 = 4,
    AIMAGE_FORMAT_RGBA_FP16 = 22,
    AIMAGE_FORMAT_YUV_420_888 = 35,
    AIMAGE_FORMAT_JPEG = 256,
};

enum {
    ACAMERA_LENS_FACING_FRONT = 0,
    ACAMERA_LENS_FACING_BACK = 1,
    ACAMERA_LENS_FACING_EXTERNAL = 2,
};

enum {
    TEMPLATE_PREVIEW = 1,
    TEMPLATE_STILL_CAPTURE = 2,
    TEMPLATE_RECORD = 3,
    TEMPLATE_VIDEO_SNAPSHOT = 4,
    TEMPLATE_ZERO_SHUTTER_LAG = 5,
    TEMPLATE_MANUAL = 6,
};
typedef int ACameraDevice_request_template;

enum {
    ACAMERA_LENS_FACING = 0x10000,
    ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE = 0x20000,
    ACAMERA_SENSOR_ORIENTATION = 0x30000,
};

typedef struct ACameraManager ACameraManager;
typedef struct ACameraDevice ACameraDevice;
typedef struct ACameraCaptureSession ACameraCaptureSession;
typedef struct ACaptureRequest ACaptureRequest;
typedef struct ACameraOutputTarget ACameraOutputTarget;
typedef struct ACaptureSessionOutput ACaptureSessionOutput;
typedef struct ACaptureSessionOutputContainer ACaptureSessionOutputContainer;
typedef struct ACameraMetadata ACameraMetadata;
typedef struct AImageReader AImageReader;
typedef struct AImage AImage;

typedef struct ACameraIdList {
    int numCameras;
    const char **cameraIds;
} ACameraIdList;

typedef struct ACameraManager_AvailabilityCallbacks {
    void *context;
    void(__attribute__((sysv_abi)) * onCameraAvailable)(void *context, const char *cameraId);
    void(__attribute__((sysv_abi)) * onCameraUnavailable)(void *context, const char *cameraId);
} ACameraManager_AvailabilityCallbacks;

typedef struct ACameraDevice_StateCallbacks {
    void *context;
    void(__attribute__((sysv_abi)) * onDisconnected)(void *context, ACameraDevice *device);
    void(__attribute__((sysv_abi)) * onError)(void *context, ACameraDevice *device, int error);
} ACameraDevice_StateCallbacks;

typedef struct ACameraCaptureSession_stateCallbacks {
    void *context;
    void(__attribute__((sysv_abi)) * onClosed)(void *context, ACameraCaptureSession *session);
    void(__attribute__((sysv_abi)) * onReady)(void *context, ACameraCaptureSession *session);
    void(__attribute__((sysv_abi)) * onActive)(void *context, ACameraCaptureSession *session);
} ACameraCaptureSession_stateCallbacks;

typedef struct ACameraCaptureSession_captureCallbacks {
    void *context;
    void(__attribute__((sysv_abi)) *
         onCaptureStarted)(void *context, ACameraCaptureSession *session,
                           const ACaptureRequest *request, int64_t timestamp);
    void(__attribute__((sysv_abi)) *
         onCaptureProgressed)(void *context, ACameraCaptureSession *session,
                              ACaptureRequest *request, const ACameraMetadata *result);
    void(__attribute__((sysv_abi)) *
         onCaptureCompleted)(void *context, ACameraCaptureSession *session,
                             ACaptureRequest *request, const ACameraMetadata *result);
    void(__attribute__((sysv_abi)) * onCaptureFailed)(void *context, ACameraCaptureSession *session,
                                                      ACaptureRequest *request, void *failure);
    void(__attribute__((sysv_abi)) *
         onCaptureSequenceCompleted)(void *context, ACameraCaptureSession *session, int sequenceId,
                                     int64_t frameNumber);
    void(__attribute__((sysv_abi)) *
         onCaptureSequenceAborted)(void *context, ACameraCaptureSession *session, int sequenceId);
    void(__attribute__((sysv_abi)) *
         onCaptureBufferLost)(void *context, ACameraCaptureSession *session,
                              ACaptureRequest *request, void *window, int64_t frameNumber);
} ACameraCaptureSession_captureCallbacks;

typedef struct AImageReader_ImageListener {
    void *context;
    void(__attribute__((sysv_abi)) * onImageAvailable)(void *context, AImageReader *reader);
} AImageReader_ImageListener;

typedef struct AImage_Plane {
    int32_t width{0};
    int32_t height{0};
    int32_t pixelStride{1};
    int32_t rowStride{0};
    uint8_t *data{nullptr};
    int32_t dataLength{0};
} AImage_Plane;

typedef struct ACameraMetadata_const_entry {
    uint32_t tag{0};
    uint8_t type{0};
    uint32_t count{0};
    union {
        const uint8_t *u8;
        const int32_t *i32;
        const float *f;
        const int64_t *i64;
        const double *d;
        const void *rational;
    } data{};
} ACameraMetadata_const_entry;
