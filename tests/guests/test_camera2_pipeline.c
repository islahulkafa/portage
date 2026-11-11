#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ACameraManager ACameraManager;
typedef struct ACameraDevice ACameraDevice;
typedef struct ACameraCaptureSession ACameraCaptureSession;
typedef struct ACaptureRequest ACaptureRequest;
typedef struct ACameraOutputTarget ACameraOutputTarget;
typedef struct ACaptureSessionOutput ACaptureSessionOutput;
typedef struct ACaptureSessionOutputContainer ACaptureSessionOutputContainer;
typedef struct ACameraMetadata ACameraMetadata;
typedef struct ANativeWindow ANativeWindow;
typedef struct AImageReader AImageReader;
typedef struct AImage AImage;

enum {
    TEMPLATE_PREVIEW = 1,
    TEMPLATE_STILL_CAPTURE = 2,
};

enum {
    AIMAGE_FORMAT_RGBA_8888 = 1,
    AIMAGE_FORMAT_JPEG = 256,
};

enum {
    ACAMERA_LENS_FACING = 0x10000,
    ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE = 0x20000,
    ACAMERA_SENSOR_ORIENTATION = 0x30000,
};

typedef struct ACameraIdList {
    int numCameras;
    const char **cameraIds;
} ACameraIdList;

typedef struct ACameraManager_AvailabilityCallbacks {
    void *context;
    void (*onCameraAvailable)(void *context, const char *cameraId);
    void (*onCameraUnavailable)(void *context, const char *cameraId);
} ACameraManager_AvailabilityCallbacks;

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
    void (*onCaptureFailed)(void *context, ACameraCaptureSession *session, ACaptureRequest *request,
                            void *failure);
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

typedef struct AImageReader_ImageListener {
    void *context;
    void (*onImageAvailable)(void *context, AImageReader *reader);
} AImageReader_ImageListener;

extern ACameraManager *ACameraManager_create(void);
extern void ACameraManager_delete(ACameraManager *manager);
extern int32_t ACameraManager_getCameraIdList(ACameraManager *manager,
                                              ACameraIdList **cameraIdList);
extern void ACameraManager_deleteCameraIdList(ACameraIdList *cameraIdList);
extern int32_t ACameraManager_getCameraCharacteristics(ACameraManager *manager,
                                                       const char *cameraId,
                                                       ACameraMetadata **characteristics);
extern int32_t ACameraManager_openCamera(ACameraManager *manager, const char *cameraId,
                                         ACameraDevice_StateCallbacks *callback,
                                         ACameraDevice **device);
extern int32_t
ACameraManager_registerAvailabilityCallback(ACameraManager *manager,
                                            const ACameraManager_AvailabilityCallbacks *callback);
extern int32_t
ACameraManager_unregisterAvailabilityCallback(ACameraManager *manager,
                                              const ACameraManager_AvailabilityCallbacks *callback);

extern int32_t ACameraDevice_close(ACameraDevice *device);
extern int32_t ACameraDevice_createCaptureRequest(const ACameraDevice *device, int templateId,
                                                  ACaptureRequest **request);
extern int32_t ACameraDevice_createCaptureSession(
    ACameraDevice *device, const ACaptureSessionOutputContainer *outputs,
    const ACameraCaptureSession_stateCallbacks *callbacks, ACameraCaptureSession **session);

extern void ACaptureRequest_free(ACaptureRequest *request);
extern int32_t ACaptureRequest_addTarget(ACaptureRequest *request,
                                         const ACameraOutputTarget *target);

extern int32_t ACameraCaptureSession_close(ACameraCaptureSession *session);
extern int32_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession *session, ACameraCaptureSession_captureCallbacks *callbacks,
    int numRequests, ACaptureRequest **requests, int *captureSequenceId);
extern int32_t ACameraCaptureSession_capture(ACameraCaptureSession *session,
                                             ACameraCaptureSession_captureCallbacks *callbacks,
                                             int numRequests, ACaptureRequest **requests,
                                             int *captureSequenceId);
extern int32_t ACameraCaptureSession_stopRepeating(ACameraCaptureSession *session);
extern int32_t ACameraCaptureSession_abortCaptures(ACameraCaptureSession *session);

extern int32_t ACameraOutputTarget_create(ANativeWindow *window, ACameraOutputTarget **output);
extern void ACameraOutputTarget_free(ACameraOutputTarget *output);
extern int32_t ACaptureSessionOutput_create(ANativeWindow *window, ACaptureSessionOutput **output);
extern void ACaptureSessionOutput_free(ACaptureSessionOutput *output);
extern int32_t ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer **container);
extern void ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer *container);
extern int32_t ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer *container,
                                                  const ACaptureSessionOutput *output);

extern void ACameraMetadata_free(ACameraMetadata *metadata);
extern int32_t ACameraMetadata_getConstEntry(const ACameraMetadata *metadata, uint32_t tag,
                                             ACameraMetadata_const_entry *entry);

extern int32_t AImageReader_new(int32_t width, int32_t height, int32_t format, int32_t maxImages,
                                AImageReader **reader);
extern void AImageReader_delete(AImageReader *reader);
extern int32_t AImageReader_getWindow(AImageReader *reader, ANativeWindow **window);
extern int32_t AImageReader_acquireNextImage(AImageReader *reader, AImage **image);
extern int32_t AImageReader_setImageListener(AImageReader *reader,
                                             AImageReader_ImageListener *listener);
extern void AImage_delete(AImage *image);
extern int32_t AImage_getWidth(const AImage *image, int32_t *width);
extern int32_t AImage_getHeight(const AImage *image, int32_t *height);
extern int32_t AImage_getFormat(const AImage *image, int32_t *format);
extern int32_t AImage_getNumberOfPlanes(const AImage *image, int32_t *numPlanes);
extern int32_t AImage_getPlaneData(const AImage *image, int32_t planeIdx, uint8_t **data,
                                   int *dataLength);

struct timespec_raw {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"((int64_t)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static inline void sys_sleep_ms(int64_t ms)
{
    struct timespec_raw req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(35), "D"(&req), "S"((uint64_t)0)
                     : "rcx", "r11", "memory");
}

typedef struct CameraTrace {
    int32_t available_count;
    int32_t unavailable_count;
    int32_t disconnected_count;
    int32_t session_ready;
    int32_t session_active;
    int32_t session_closed;
    int32_t cap_started;
    int32_t cap_completed;
    int32_t seq_completed;
    int32_t seq_aborted;
    int32_t last_seq_id;
    int32_t image_available;
    int64_t last_frame_num;
    int64_t last_started_ts;
} CameraTrace;

static CameraTrace g_trace;

static void on_cam_available(void *ctx, const char *camera_id)
{
    (void)camera_id;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->available_count, 1, __ATOMIC_SEQ_CST);
}

static void on_cam_unavailable(void *ctx, const char *camera_id)
{
    (void)camera_id;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->unavailable_count, 1, __ATOMIC_SEQ_CST);
}

static void on_dev_disconnected(void *ctx, ACameraDevice *device)
{
    if (!device) {
        return;
    }
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->disconnected_count, 1, __ATOMIC_SEQ_CST);
}

static void on_sess_ready(void *ctx, ACameraCaptureSession *session)
{
    if (!session) {
        return;
    }
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->session_ready, 1, __ATOMIC_SEQ_CST);
}

static void on_sess_active(void *ctx, ACameraCaptureSession *session)
{
    if (!session) {
        return;
    }
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->session_active, 1, __ATOMIC_SEQ_CST);
}

static void on_sess_closed(void *ctx, ACameraCaptureSession *session)
{
    if (!session) {
        return;
    }
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->session_closed, 1, __ATOMIC_SEQ_CST);
}

static void on_cap_started(void *ctx, ACameraCaptureSession *session, const ACaptureRequest *req,
                           int64_t ts)
{
    (void)session;
    (void)req;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->cap_started, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&t->last_started_ts, ts, __ATOMIC_SEQ_CST);
}

static void on_cap_completed(void *ctx, ACameraCaptureSession *session, ACaptureRequest *req,
                             const ACameraMetadata *result)
{
    (void)session;
    (void)req;
    if (!result) {
        return;
    }
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->cap_completed, 1, __ATOMIC_SEQ_CST);
}

static void on_seq_completed(void *ctx, ACameraCaptureSession *session, int seq_id,
                             int64_t frame_num)
{
    (void)session;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_store_n(&t->last_seq_id, seq_id, __ATOMIC_SEQ_CST);
    __atomic_store_n(&t->last_frame_num, frame_num, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&t->seq_completed, 1, __ATOMIC_SEQ_CST);
}

static void on_seq_aborted(void *ctx, ACameraCaptureSession *session, int seq_id)
{
    (void)session;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_store_n(&t->last_seq_id, seq_id, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&t->seq_aborted, 1, __ATOMIC_SEQ_CST);
}

static void on_image_available(void *ctx, AImageReader *reader)
{
    (void)reader;
    CameraTrace *t = (CameraTrace *)ctx;
    __atomic_fetch_add(&t->image_available, 1, __ATOMIC_SEQ_CST);
}

void _start(void)
{
    ACameraManager *mgr = ACameraManager_create();
    if (!mgr) {
        sys_exit(1);
    }

    ACameraManager_AvailabilityCallbacks avail_cb = {
        .context = &g_trace,
        .onCameraAvailable = on_cam_available,
        .onCameraUnavailable = on_cam_unavailable,
    };
    if (ACameraManager_registerAvailabilityCallback(mgr, &avail_cb) != 0) {
        sys_exit(2);
    }
    if (__atomic_load_n(&g_trace.available_count, __ATOMIC_SEQ_CST) < 1) {
        sys_exit(3);
    }

    ACameraIdList *id_list = NULL;
    if (ACameraManager_getCameraIdList(mgr, &id_list) != 0 || !id_list || id_list->numCameras < 1) {
        sys_exit(4);
    }

    ACameraMetadata *chars = NULL;
    if (ACameraManager_getCameraCharacteristics(mgr, id_list->cameraIds[0], &chars) != 0 ||
        !chars) {
        sys_exit(5);
    }
    ACameraMetadata_const_entry size_entry;
    if (ACameraMetadata_getConstEntry(chars, ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE, &size_entry) !=
            0 ||
        size_entry.count < 2 || size_entry.data.i32[0] <= 0 || size_entry.data.i32[1] <= 0) {
        sys_exit(6);
    }
    ACameraMetadata_free(chars);

    int32_t unavail_before = __atomic_load_n(&g_trace.unavailable_count, __ATOMIC_SEQ_CST);
    ACameraDevice_StateCallbacks dev_cb = {
        .context = &g_trace,
        .onDisconnected = on_dev_disconnected,
        .onError = NULL,
    };
    ACameraDevice *device = NULL;
    if (ACameraManager_openCamera(mgr, id_list->cameraIds[0], &dev_cb, &device) != 0 || !device) {
        sys_exit(7);
    }
    if (__atomic_load_n(&g_trace.unavailable_count, __ATOMIC_SEQ_CST) <= unavail_before) {
        sys_exit(8);
    }

    AImageReader *preview_reader = NULL;
    if (AImageReader_new(64, 48, AIMAGE_FORMAT_RGBA_8888, 4, &preview_reader) != 0 ||
        !preview_reader) {
        sys_exit(9);
    }
    AImageReader_ImageListener img_listener = {
        .context = &g_trace,
        .onImageAvailable = on_image_available,
    };
    if (AImageReader_setImageListener(preview_reader, &img_listener) != 0) {
        sys_exit(10);
    }
    ANativeWindow *preview_win = NULL;
    if (AImageReader_getWindow(preview_reader, &preview_win) != 0 || !preview_win) {
        sys_exit(11);
    }

    AImageReader *jpeg_reader = NULL;
    if (AImageReader_new(160, 120, AIMAGE_FORMAT_JPEG, 2, &jpeg_reader) != 0 || !jpeg_reader) {
        sys_exit(12);
    }
    ANativeWindow *jpeg_win = NULL;
    if (AImageReader_getWindow(jpeg_reader, &jpeg_win) != 0 || !jpeg_win) {
        sys_exit(13);
    }

    ACaptureSessionOutput *out_preview = NULL;
    ACaptureSessionOutput *out_jpeg = NULL;
    ACaptureSessionOutput_create(preview_win, &out_preview);
    ACaptureSessionOutput_create(jpeg_win, &out_jpeg);

    ACaptureSessionOutputContainer *container = NULL;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutputContainer_add(container, out_preview);
    ACaptureSessionOutputContainer_add(container, out_jpeg);

    ACameraCaptureSession_stateCallbacks sess_cb = {
        .context = &g_trace,
        .onClosed = on_sess_closed,
        .onReady = on_sess_ready,
        .onActive = on_sess_active,
    };
    ACameraCaptureSession *session = NULL;
    if (ACameraDevice_createCaptureSession(device, container, &sess_cb, &session) != 0 ||
        !session) {
        sys_exit(14);
    }
    if (__atomic_load_n(&g_trace.session_ready, __ATOMIC_SEQ_CST) < 1) {
        sys_exit(15);
    }

    ACameraOutputTarget *preview_target = NULL;
    ACameraOutputTarget_create(preview_win, &preview_target);
    ACaptureRequest *preview_req = NULL;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &preview_req);
    ACaptureRequest_addTarget(preview_req, preview_target);

    ACameraCaptureSession_captureCallbacks cap_cb = {
        .context = &g_trace,
        .onCaptureStarted = on_cap_started,
        .onCaptureProgressed = NULL,
        .onCaptureCompleted = on_cap_completed,
        .onCaptureFailed = NULL,
        .onCaptureSequenceCompleted = on_seq_completed,
        .onCaptureSequenceAborted = on_seq_aborted,
        .onCaptureBufferLost = NULL,
    };

    int repeat_seq = 0;
    if (ACameraCaptureSession_setRepeatingRequest(session, &cap_cb, 1, &preview_req, &repeat_seq) !=
        0) {
        sys_exit(16);
    }
    for (int i = 0; i < 40; ++i) {
        if (__atomic_load_n(&g_trace.image_available, __ATOMIC_SEQ_CST) >= 1 &&
            __atomic_load_n(&g_trace.cap_completed, __ATOMIC_SEQ_CST) >= 1) {
            break;
        }
        sys_sleep_ms(10);
    }
    if (__atomic_load_n(&g_trace.session_active, __ATOMIC_SEQ_CST) < 1 ||
        __atomic_load_n(&g_trace.image_available, __ATOMIC_SEQ_CST) < 1 ||
        __atomic_load_n(&g_trace.cap_started, __ATOMIC_SEQ_CST) < 1 ||
        __atomic_load_n(&g_trace.cap_completed, __ATOMIC_SEQ_CST) < 1) {
        sys_exit(17);
    }

    if (ACameraCaptureSession_stopRepeating(session) != 0) {
        sys_exit(18);
    }
    if (__atomic_load_n(&g_trace.seq_completed, __ATOMIC_SEQ_CST) != 1 ||
        __atomic_load_n(&g_trace.last_seq_id, __ATOMIC_SEQ_CST) != repeat_seq) {
        sys_exit(19);
    }

    AImage *preview_img = NULL;
    if (AImageReader_acquireNextImage(preview_reader, &preview_img) != 0 || !preview_img) {
        sys_exit(20);
    }
    int32_t pw = 0;
    int32_t ph = 0;
    uint8_t *pdata = NULL;
    int plen = 0;
    AImage_getWidth(preview_img, &pw);
    AImage_getHeight(preview_img, &ph);
    AImage_getPlaneData(preview_img, 0, &pdata, &plen);
    if (pw != 64 || ph != 48 || !pdata || plen != 64 * 48 * 4) {
        sys_exit(21);
    }
    AImage_delete(preview_img);

    ACameraOutputTarget *jpeg_target = NULL;
    ACameraOutputTarget_create(jpeg_win, &jpeg_target);
    ACaptureRequest *jpeg_req = NULL;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_STILL_CAPTURE, &jpeg_req);
    ACaptureRequest_addTarget(jpeg_req, jpeg_target);

    int still_seq = 0;
    if (ACameraCaptureSession_capture(session, &cap_cb, 1, &jpeg_req, &still_seq) != 0) {
        sys_exit(22);
    }
    if (__atomic_load_n(&g_trace.seq_completed, __ATOMIC_SEQ_CST) != 2 ||
        __atomic_load_n(&g_trace.last_seq_id, __ATOMIC_SEQ_CST) != still_seq) {
        sys_exit(23);
    }

    AImage *jpeg_img = NULL;
    if (AImageReader_acquireNextImage(jpeg_reader, &jpeg_img) != 0 || !jpeg_img) {
        sys_exit(24);
    }
    int32_t jfmt = 0;
    int32_t jplanes = 0;
    uint8_t *jdata = NULL;
    int jlen = 0;
    AImage_getFormat(jpeg_img, &jfmt);
    AImage_getNumberOfPlanes(jpeg_img, &jplanes);
    AImage_getPlaneData(jpeg_img, 0, &jdata, &jlen);
    if (jfmt != AIMAGE_FORMAT_JPEG || jplanes != 1 || !jdata || jlen < 4 || jdata[0] != 0xFF ||
        jdata[1] != 0xD8 || jdata[jlen - 2] != 0xFF || jdata[jlen - 1] != 0xD9) {
        sys_exit(25);
    }
    AImage_delete(jpeg_img);

    int abort_seq = 0;
    if (ACameraCaptureSession_setRepeatingRequest(session, &cap_cb, 1, &preview_req, &abort_seq) !=
        0) {
        sys_exit(26);
    }
    if (ACameraCaptureSession_abortCaptures(session) != 0) {
        sys_exit(27);
    }
    if (__atomic_load_n(&g_trace.seq_aborted, __ATOMIC_SEQ_CST) != 1 ||
        __atomic_load_n(&g_trace.last_seq_id, __ATOMIC_SEQ_CST) != abort_seq) {
        sys_exit(28);
    }

    ACameraCaptureSession_close(session);
    if (__atomic_load_n(&g_trace.session_closed, __ATOMIC_SEQ_CST) != 1) {
        sys_exit(29);
    }

    int32_t avail_before_close = __atomic_load_n(&g_trace.available_count, __ATOMIC_SEQ_CST);
    ACameraDevice_close(device);
    if (__atomic_load_n(&g_trace.disconnected_count, __ATOMIC_SEQ_CST) != 1 ||
        __atomic_load_n(&g_trace.available_count, __ATOMIC_SEQ_CST) <= avail_before_close) {
        sys_exit(30);
    }

    ACameraManager_unregisterAvailabilityCallback(mgr, &avail_cb);
    ACaptureRequest_free(jpeg_req);
    ACameraOutputTarget_free(jpeg_target);
    ACaptureRequest_free(preview_req);
    ACameraOutputTarget_free(preview_target);
    ACaptureSessionOutputContainer_free(container);
    ACaptureSessionOutput_free(out_jpeg);
    ACaptureSessionOutput_free(out_preview);
    AImageReader_delete(jpeg_reader);
    AImageReader_delete(preview_reader);
    ACameraManager_deleteCameraIdList(id_list);
    ACameraManager_delete(mgr);

    sys_exit(0);
}
