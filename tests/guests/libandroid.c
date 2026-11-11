#include <stddef.h>
#include <stdint.h>

struct ANativeWindow {
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t ref_count;
};

static struct ANativeWindow g_default_window = {.width = 1280,
                                                .height = 720,
                                                .format = 1, // WINDOW_FORMAT_RGBA_8888
                                                .ref_count = 1};

__attribute__((visibility("default"))) void ANativeWindow_acquire(struct ANativeWindow *window)
{
    if (window) {
        window->ref_count++;
    }
}

__attribute__((visibility("default"))) void ANativeWindow_release(struct ANativeWindow *window)
{
    if (window && window->ref_count > 0) {
        window->ref_count--;
    }
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getWidth(struct ANativeWindow *window)
{
    return window ? window->width : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getHeight(struct ANativeWindow *window)
{
    return window ? window->height : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getFormat(struct ANativeWindow *window)
{
    return window ? window->format : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_setBuffersGeometry(
    struct ANativeWindow *window, int32_t width, int32_t height, int32_t format)
{
    if (!window)
        return -1;
    if (width > 0)
        window->width = width;
    if (height > 0)
        window->height = height;
    if (format > 0)
        window->format = format;
    return 0;
}

__attribute__((visibility("default"))) struct ANativeWindow *ANativeWindow_create(int32_t width,
                                                                                  int32_t height)
{
    g_default_window.width = width > 0 ? width : 1280;
    g_default_window.height = height > 0 ? height : 720;
    g_default_window.format = 1;
    g_default_window.ref_count = 1;
    return &g_default_window;
}

struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
};

struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

__attribute__((visibility("default"))) int32_t
ANativeWindow_lock(struct ANativeWindow *window, struct ANativeWindow_Buffer *outBuffer,
                   struct ARect *inOutDirtyBounds)
{
    (void)window;
    (void)outBuffer;
    (void)inOutDirtyBounds;
    return -1;
}

__attribute__((visibility("default"))) int32_t
ANativeWindow_unlockAndPost(struct ANativeWindow *window)
{
    (void)window;
    return -1;
}

struct AAssetDir {
    void *internal_ptr;
    const char *(*getNextFileNameFunc)(struct AAssetDir *dir);
    void (*rewindFunc)(struct AAssetDir *dir);
    void (*closeFunc)(struct AAssetDir *dir);
};

struct AAssetManager {
    void *internal_ptr;
    struct AAsset *(*openFunc)(struct AAssetManager *mgr, const char *filename, int mode);
    int (*readFunc)(struct AAsset *asset, void *buf, size_t count);
    int64_t (*seekFunc)(struct AAsset *asset, int64_t offset, int whence);
    int64_t (*getLengthFunc)(struct AAsset *asset);
    const void *(*getBufferFunc)(struct AAsset *asset);
    void (*closeFunc)(struct AAsset *asset);
    struct AAssetDir *(*openDirFunc)(struct AAssetManager *mgr, const char *dirName);
};

struct AAsset {
    void *internal_ptr;
    int (*readFunc)(struct AAsset *asset, void *buf, size_t count);
    int64_t (*seekFunc)(struct AAsset *asset, int64_t offset, int whence);
    int64_t (*getLengthFunc)(struct AAsset *asset);
    const void *(*getBufferFunc)(struct AAsset *asset);
    void (*closeFunc)(struct AAsset *asset);
};

__attribute__((visibility("default"))) struct AAssetManager *
AAssetManager_fromJava(void *env, void *assetManager)
{
    (void)env;
    return (struct AAssetManager *)assetManager;
}

__attribute__((visibility("default"))) struct AAsset *
AAssetManager_open(struct AAssetManager *mgr, const char *filename, int mode)
{
    if (!mgr || !mgr->openFunc)
        return NULL;
    return mgr->openFunc(mgr, filename, mode);
}

__attribute__((visibility("default"))) struct AAssetDir *
AAssetManager_openDir(struct AAssetManager *mgr, const char *dirName)
{
    if (!mgr || !mgr->openDirFunc)
        return NULL;
    return mgr->openDirFunc(mgr, dirName);
}

__attribute__((visibility("default"))) int AAsset_read(struct AAsset *asset, void *buf,
                                                       size_t count)
{
    if (!asset || !asset->readFunc)
        return -1;
    return asset->readFunc(asset, buf, count);
}

__attribute__((visibility("default"))) int64_t AAsset_seek(struct AAsset *asset, int64_t offset,
                                                           int whence)
{
    if (!asset || !asset->seekFunc)
        return -1;
    return asset->seekFunc(asset, offset, whence);
}

__attribute__((visibility("default"))) int64_t AAsset_getLength(struct AAsset *asset)
{
    if (!asset || !asset->getLengthFunc)
        return -1;
    return asset->getLengthFunc(asset);
}

__attribute__((visibility("default"))) const void *AAsset_getBuffer(struct AAsset *asset)
{
    if (!asset || !asset->getBufferFunc)
        return NULL;
    return asset->getBufferFunc(asset);
}

__attribute__((visibility("default"))) void AAsset_close(struct AAsset *asset)
{
    if (asset && asset->closeFunc) {
        asset->closeFunc(asset);
    }
}

__attribute__((visibility("default"))) const char *
AAssetDir_getNextFileName(struct AAssetDir *assetDir)
{
    if (!assetDir || !assetDir->getNextFileNameFunc)
        return NULL;
    return assetDir->getNextFileNameFunc(assetDir);
}

__attribute__((visibility("default"))) void AAssetDir_rewind(struct AAssetDir *assetDir)
{
    if (assetDir && assetDir->rewindFunc) {
        assetDir->rewindFunc(assetDir);
    }
}

__attribute__((visibility("default"))) void AAssetDir_close(struct AAssetDir *assetDir)
{
    if (assetDir && assetDir->closeFunc) {
        assetDir->closeFunc(assetDir);
    }
}

struct AChoreographer;

__attribute__((visibility("default"))) struct AChoreographer *AChoreographer_getInstance(void)
{
    return (struct AChoreographer *)0;
}

__attribute__((visibility("default"))) void
AChoreographer_postFrameCallback(struct AChoreographer *choreographer,
                                 void (*callback)(long frameTimeNanos, void *data), void *data)
{
    (void)choreographer;
    (void)callback;
    (void)data;
}

__attribute__((visibility("default"))) void
AChoreographer_postFrameCallbackDelayed(struct AChoreographer *choreographer,
                                        void (*callback)(long frameTimeNanos, void *data),
                                        void *data, long delayMillis)
{
    (void)choreographer;
    (void)callback;
    (void)data;
    (void)delayMillis;
}

__attribute__((visibility("default"))) void
AChoreographer_postFrameCallback64(struct AChoreographer *choreographer,
                                   void (*callback)(int64_t frameTimeNanos, void *data), void *data)
{
    (void)choreographer;
    (void)callback;
    (void)data;
}

__attribute__((visibility("default"))) void
AChoreographer_postFrameCallbackDelayed64(struct AChoreographer *choreographer,
                                          void (*callback)(int64_t frameTimeNanos, void *data),
                                          void *data, uint32_t delayMillis)
{
    (void)choreographer;
    (void)callback;
    (void)data;
    (void)delayMillis;
}

__attribute__((visibility("default"))) void
AChoreographer_registerRefreshRateCallback(struct AChoreographer *choreographer,
                                           void (*callback)(int64_t vsyncPeriodNanos, void *data),
                                           void *data)
{
    (void)choreographer;
    (void)callback;
    (void)data;
}

__attribute__((visibility("default"))) void
AChoreographer_unregisterRefreshRateCallback(struct AChoreographer *choreographer,
                                             void (*callback)(int64_t vsyncPeriodNanos, void *data),
                                             void *data)
{
    (void)choreographer;
    (void)callback;
    (void)data;
}

struct ASensorManager;
struct ASensorEventQueue;
struct ASensor;
struct AHardwareBuffer;
struct ASensorEvent;
typedef const struct ASensor *ASensorRef;
typedef ASensorRef const *ASensorList;

__attribute__((visibility("default"))) struct ASensorManager *ASensorManager_getInstance(void)
{
    return (struct ASensorManager *)0;
}

__attribute__((visibility("default"))) struct ASensorManager *
ASensorManager_getInstanceForPackage(const char *packageName)
{
    (void)packageName;
    return (struct ASensorManager *)0;
}

__attribute__((visibility("default"))) int
ASensorManager_getSensorList(struct ASensorManager *manager, ASensorList *list)
{
    (void)manager;
    (void)list;
    return 0;
}

__attribute__((visibility("default"))) const struct ASensor *
ASensorManager_getDefaultSensor(struct ASensorManager *manager, int type)
{
    (void)manager;
    (void)type;
    return (const struct ASensor *)0;
}

__attribute__((visibility("default"))) const struct ASensor *
ASensorManager_getDefaultSensorEx(struct ASensorManager *manager, int type, int wakeUp)
{
    (void)manager;
    (void)type;
    (void)wakeUp;
    return (const struct ASensor *)0;
}

__attribute__((visibility("default"))) struct ASensorEventQueue *
ASensorManager_createEventQueue(struct ASensorManager *manager, void *looper, int ident,
                                int (*callback)(int, int, void *), void *data)
{
    (void)manager;
    (void)looper;
    (void)ident;
    (void)callback;
    (void)data;
    return (struct ASensorEventQueue *)0;
}

__attribute__((visibility("default"))) int
ASensorManager_destroyEventQueue(struct ASensorManager *manager, struct ASensorEventQueue *queue)
{
    (void)manager;
    (void)queue;
    return 0;
}

__attribute__((visibility("default"))) int
ASensorManager_createSharedMemoryDirectChannel(struct ASensorManager *manager, int fd, size_t size)
{
    (void)manager;
    (void)fd;
    (void)size;
    return -38;
}

__attribute__((visibility("default"))) int
ASensorManager_createHardwareBufferDirectChannel(struct ASensorManager *manager,
                                                 const struct AHardwareBuffer *buffer, size_t size)
{
    (void)manager;
    (void)buffer;
    (void)size;
    return -38;
}

__attribute__((visibility("default"))) int
ASensorManager_destroyDirectChannel(struct ASensorManager *manager, int channelId)
{
    (void)manager;
    (void)channelId;
    return -38;
}

__attribute__((visibility("default"))) int
ASensorManager_configureDirectReport(struct ASensorManager *manager, const struct ASensor *sensor,
                                     int channelId, int rate)
{
    (void)manager;
    (void)sensor;
    (void)channelId;
    (void)rate;
    return -38;
}

__attribute__((visibility("default"))) int
ASensorEventQueue_registerSensor(struct ASensorEventQueue *queue, const struct ASensor *sensor,
                                 int32_t samplingPeriodUs, int64_t maxBatchReportLatencyUs)
{
    (void)queue;
    (void)sensor;
    (void)samplingPeriodUs;
    (void)maxBatchReportLatencyUs;
    return 0;
}

__attribute__((visibility("default"))) int
ASensorEventQueue_enableSensor(struct ASensorEventQueue *queue, const struct ASensor *sensor)
{
    (void)queue;
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int
ASensorEventQueue_disableSensor(struct ASensorEventQueue *queue, const struct ASensor *sensor)
{
    (void)queue;
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int
ASensorEventQueue_setEventRate(struct ASensorEventQueue *queue, const struct ASensor *sensor,
                               int32_t usec)
{
    (void)queue;
    (void)sensor;
    (void)usec;
    return 0;
}

__attribute__((visibility("default"))) int
ASensorEventQueue_hasEvents(struct ASensorEventQueue *queue)
{
    (void)queue;
    return 0;
}

__attribute__((visibility("default"))) int64_t ASensorEventQueue_getEvents(
    struct ASensorEventQueue *queue, struct ASensorEvent *events, size_t count)
{
    (void)queue;
    (void)events;
    (void)count;
    return 0;
}

__attribute__((visibility("default"))) const char *ASensor_getName(const struct ASensor *sensor)
{
    (void)sensor;
    return "";
}

__attribute__((visibility("default"))) const char *ASensor_getVendor(const struct ASensor *sensor)
{
    (void)sensor;
    return "";
}

__attribute__((visibility("default"))) int ASensor_getType(const struct ASensor *sensor)
{
    (void)sensor;
    return -1;
}

__attribute__((visibility("default"))) float ASensor_getResolution(const struct ASensor *sensor)
{
    (void)sensor;
    return 0.0f;
}

__attribute__((visibility("default"))) int ASensor_getMinDelay(const struct ASensor *sensor)
{
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int
ASensor_getFifoMaxEventCount(const struct ASensor *sensor)
{
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int
ASensor_getFifoReservedEventCount(const struct ASensor *sensor)
{
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) const char *
ASensor_getStringType(const struct ASensor *sensor)
{
    (void)sensor;
    return "";
}

__attribute__((visibility("default"))) int ASensor_getReportingMode(const struct ASensor *sensor)
{
    (void)sensor;
    return -1;
}

__attribute__((visibility("default"))) int ASensor_isWakeUpSensor(const struct ASensor *sensor)
{
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int
ASensor_isDirectChannelTypeSupported(const struct ASensor *sensor, int channelType)
{
    (void)sensor;
    (void)channelType;
    return 0;
}

__attribute__((visibility("default"))) int
ASensor_getHighestDirectReportRateLevel(const struct ASensor *sensor)
{
    (void)sensor;
    return 0;
}

__attribute__((visibility("default"))) int ASensor_getHandle(const struct ASensor *sensor)
{
    (void)sensor;
    return -1;
}

__attribute__((visibility("default"))) int32_t ASharedMemory_create(const char *name, size_t size)
{
    (void)name;
    (void)size;
    return -1;
}

__attribute__((visibility("default"))) size_t ASharedMemory_getSize(int fd)
{
    (void)fd;
    return 0;
}

__attribute__((visibility("default"))) int32_t ASharedMemory_setProt(int fd, int prot)
{
    (void)fd;
    (void)prot;
    return -1;
}

__attribute__((visibility("default"))) int32_t ASharedMemory_dup(int fd)
{
    (void)fd;
    return -1;
}

typedef struct AHardwareBuffer AHardwareBuffer;

typedef struct AHardwareBuffer_Desc {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t format;
    uint64_t usage;
    uint32_t stride;
    uint32_t rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

typedef struct AHardwareBuffer_Plane {
    void *data;
    uint32_t pixelStride;
    uint32_t rowStride;
} AHardwareBuffer_Plane;

typedef struct AHardwareBuffer_Planes {
    uint32_t planeCount;
    AHardwareBuffer_Plane planes[4];
} AHardwareBuffer_Planes;

__attribute__((visibility("default"))) int
AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc, AHardwareBuffer **outBuffer)
{
    (void)desc;
    (void)outBuffer;
    return -1;
}

__attribute__((visibility("default"))) void AHardwareBuffer_acquire(AHardwareBuffer *buffer)
{
    (void)buffer;
}

__attribute__((visibility("default"))) void AHardwareBuffer_release(AHardwareBuffer *buffer)
{
    (void)buffer;
}

__attribute__((visibility("default"))) void
AHardwareBuffer_describe(const AHardwareBuffer *buffer, AHardwareBuffer_Desc *outDesc)
{
    (void)buffer;
    (void)outDesc;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_lock(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                     const struct ARect *rect, void **outVirtualAddress)
{
    (void)buffer;
    (void)usage;
    (void)fence;
    (void)rect;
    (void)outVirtualAddress;
    return -1;
}

__attribute__((visibility("default"))) int AHardwareBuffer_unlock(AHardwareBuffer *buffer,
                                                                 int32_t *fence)
{
    (void)buffer;
    (void)fence;
    return -1;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                           const struct ARect *rect, AHardwareBuffer_Planes *outPlanes)
{
    (void)buffer;
    (void)usage;
    (void)fence;
    (void)rect;
    (void)outPlanes;
    return -1;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_getId(const AHardwareBuffer *buffer, uint64_t *outId)
{
    (void)buffer;
    (void)outId;
    return -1;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_isSupported(const AHardwareBuffer_Desc *desc)
{
    (void)desc;
    return 0;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_lockAndGetInfo(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                              const struct ARect *rect, void **outVirtualAddress,
                              int32_t *outBytesPerPixel, int32_t *outBytesPerStride)
{
    (void)buffer;
    (void)usage;
    (void)fence;
    (void)rect;
    (void)outVirtualAddress;
    (void)outBytesPerPixel;
    (void)outBytesPerStride;
    return -1;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_sendHandleToUnixSocket(const AHardwareBuffer *buffer, int socketFd)
{
    (void)buffer;
    (void)socketFd;
    return -1;
}

__attribute__((visibility("default"))) int
AHardwareBuffer_recvHandleFromUnixSocket(int socketFd, AHardwareBuffer **outBuffer)
{
    (void)socketFd;
    (void)outBuffer;
    return -1;
}

struct AInputEvent;
struct AInputQueue;

__attribute__((visibility("default"))) int32_t AInputEvent_getType(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AInputEvent_getDeviceId(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AInputEvent_getSource(const struct AInputEvent *event) { (void)event; return 0; }

__attribute__((visibility("default"))) int32_t AKeyEvent_getAction(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AKeyEvent_getFlags(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AKeyEvent_getKeyCode(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AKeyEvent_getScanCode(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AKeyEvent_getMetaState(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AKeyEvent_getRepeatCount(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int64_t AKeyEvent_getDownTime(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int64_t AKeyEvent_getEventTime(const struct AInputEvent *event) { (void)event; return 0; }

__attribute__((visibility("default"))) int32_t AMotionEvent_getAction(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int32_t AMotionEvent_getFlags(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int64_t AMotionEvent_getDownTime(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int64_t AMotionEvent_getEventTime(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) size_t AMotionEvent_getPointerCount(const struct AInputEvent *event) { (void)event; return 1; }
__attribute__((visibility("default"))) int32_t AMotionEvent_getPointerId(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0; }
__attribute__((visibility("default"))) float AMotionEvent_getX(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getY(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getPressure(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 1.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getSize(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 1.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getRawX(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getRawY(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getAxisValue(const struct AInputEvent *event, int32_t axis, size_t pointer_index) { (void)event; (void)axis; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getTouchMajor(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getTouchMinor(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getToolMajor(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getToolMinor(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getOrientation(const struct AInputEvent *event, size_t pointer_index) { (void)event; (void)pointer_index; return 0.0f; }

__attribute__((visibility("default"))) size_t AMotionEvent_getHistorySize(const struct AInputEvent *event) { (void)event; return 0; }
__attribute__((visibility("default"))) int64_t AMotionEvent_getHistoricalEventTime(const struct AInputEvent *event, size_t history_index) { (void)event; (void)history_index; return 0; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalRawX(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalRawY(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalX(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalY(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalPressure(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalSize(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalTouchMajor(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalTouchMinor(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalToolMajor(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalToolMinor(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalOrientation(const struct AInputEvent *event, size_t pointer_index, size_t history_index) { (void)event; (void)pointer_index; (void)history_index; return 0.0f; }
__attribute__((visibility("default"))) float AMotionEvent_getHistoricalAxisValue(const struct AInputEvent *event, int32_t axis, size_t pointer_index, size_t history_index) { (void)event; (void)axis; (void)pointer_index; (void)history_index; return 0.0f; }

struct AStorageManager;
typedef void (*AStorageManager_obbCallbackFunc)(const char *filename, const int32_t state, void *data);

__attribute__((visibility("default"))) struct AStorageManager *AStorageManager_new(void) { return NULL; }
__attribute__((visibility("default"))) void AStorageManager_delete(struct AStorageManager *mgr) { (void)mgr; }
__attribute__((visibility("default"))) void AStorageManager_mountObb(struct AStorageManager *mgr, const char *filename, const char *key, AStorageManager_obbCallbackFunc cb, void *data) { (void)mgr; (void)filename; (void)key; (void)cb; (void)data; }
__attribute__((visibility("default"))) void AStorageManager_unmountObb(struct AStorageManager *mgr, const char *filename, const int force, AStorageManager_obbCallbackFunc cb, void *data) { (void)mgr; (void)filename; (void)force; (void)cb; (void)data; }
__attribute__((visibility("default"))) int AStorageManager_isObbMounted(struct AStorageManager *mgr, const char *filename) { (void)mgr; (void)filename; return 0; }
__attribute__((visibility("default"))) const char *AStorageManager_getMountedObbPath(struct AStorageManager *mgr, const char *filename) { (void)mgr; (void)filename; return NULL; }

typedef int64_t net_handle_t;
struct addrinfo;

__attribute__((visibility("default"))) int android_setsocknetwork(net_handle_t network, int fd) { (void)network; (void)fd; return 0; }
__attribute__((visibility("default"))) int android_setprocnetwork(net_handle_t network) { (void)network; return 0; }
__attribute__((visibility("default"))) int android_getprocnetwork(net_handle_t *network) { if (network) *network = 0; return 0; }
__attribute__((visibility("default"))) int android_getaddrinfofornetwork(net_handle_t network, const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) { (void)network; (void)node; (void)service; (void)hints; if (res) *res = NULL; return 0; }
__attribute__((visibility("default"))) void android_freeaddrinfofornetwork(struct addrinfo *ai) { (void)ai; }
__attribute__((visibility("default"))) int android_res_nquery(net_handle_t network, const char *dname, int ns_class, int ns_type, uint32_t flags) { (void)network; (void)dname; (void)ns_class; (void)ns_type; (void)flags; return -1; }
__attribute__((visibility("default"))) int android_res_nsearch(net_handle_t network, const char *dname, int ns_class, int ns_type, uint32_t flags) { (void)network; (void)dname; (void)ns_class; (void)ns_type; (void)flags; return -1; }
__attribute__((visibility("default"))) int android_res_nsend(net_handle_t network, const uint8_t *msg, size_t msglen, uint32_t flags) { (void)network; (void)msg; (void)msglen; (void)flags; return -1; }
__attribute__((visibility("default"))) int android_res_nresult(int fd, int *rcode) { (void)fd; if (rcode) *rcode = 0; return -1; }
__attribute__((visibility("default"))) void android_res_cancel(int nsend_fd) { (void)nsend_fd; }
__attribute__((visibility("default"))) void android_freeaddrinfo(struct addrinfo *ai) { (void)ai; }

enum AThermalStatus;
struct AThermalManager;
typedef void (*AThermal_StatusCallback)(void *data, int status);

__attribute__((visibility("default"))) struct AThermalManager *AThermalManager_acquireManager(void) { return (struct AThermalManager *)0x1; }
__attribute__((visibility("default"))) void AThermalManager_releaseManager(struct AThermalManager *manager) { (void)manager; }
__attribute__((visibility("default"))) int AThermalManager_getCurrentThermalStatus(struct AThermalManager *manager) { (void)manager; return 0; }
__attribute__((visibility("default"))) int AThermalManager_registerThermalStatusListener(struct AThermalManager *manager, AThermal_StatusCallback callback, void *data) { (void)manager; (void)callback; (void)data; return 0; }
__attribute__((visibility("default"))) int AThermalManager_unregisterThermalStatusListener(struct AThermalManager *manager, AThermal_StatusCallback callback, void *data) { (void)manager; (void)callback; (void)data; return 0; }
__attribute__((visibility("default"))) float AThermalManager_getThermalHeadroom(struct AThermalManager *manager, int forecastSeconds) { (void)manager; (void)forecastSeconds; return 0.5f; }

struct APerformanceHintManager;
struct APerformanceHintSession;

__attribute__((visibility("default"))) struct APerformanceHintManager *APerformanceHintManager_create(void) { return (struct APerformanceHintManager *)0x1; }
__attribute__((visibility("default"))) struct APerformanceHintSession *APerformanceHintManager_createSession(struct APerformanceHintManager *manager, const int32_t *threadIds, size_t size, int64_t initialTargetWorkDurationNanos) { (void)manager; (void)threadIds; (void)size; (void)initialTargetWorkDurationNanos; return (struct APerformanceHintSession *)0x1; }
__attribute__((visibility("default"))) int64_t APerformanceHintManager_getPreferredUpdateRateNanos(struct APerformanceHintManager *manager) { (void)manager; return 16666666LL; }
__attribute__((visibility("default"))) int APerformanceHintSession_updateTargetWorkDuration(struct APerformanceHintSession *session, int64_t targetDurationNanos) { (void)session; (void)targetDurationNanos; return 0; }
__attribute__((visibility("default"))) int APerformanceHintSession_reportActualWorkDuration(struct APerformanceHintSession *session, int64_t actualDurationNanos) { (void)session; (void)actualDurationNanos; return 0; }
__attribute__((visibility("default"))) void APerformanceHintSession_close(struct APerformanceHintSession *session) { (void)session; }
__attribute__((visibility("default"))) int APerformanceHintSession_setThreads(struct APerformanceHintSession *session, const int32_t *threadIds, size_t size) { (void)session; (void)threadIds; (void)size; return 0; }

__attribute__((visibility("default"))) int ATrace_isEnabled(void) { return 1; }
__attribute__((visibility("default"))) void ATrace_beginSection(const char *sectionName) { (void)sectionName; }
__attribute__((visibility("default"))) void ATrace_endSection(void) {}
__attribute__((visibility("default"))) void ATrace_beginAsyncSection(const char *sectionName, int32_t cookie) { (void)sectionName; (void)cookie; }
__attribute__((visibility("default"))) void ATrace_endAsyncSection(const char *sectionName, int32_t cookie) { (void)sectionName; (void)cookie; }
__attribute__((visibility("default"))) void ATrace_setCounter(const char *counterName, int64_t counterValue) { (void)counterName; (void)counterValue; }

__attribute__((visibility("default"))) int android_get_device_api_level(void) { return 34; }
__attribute__((visibility("default"))) int android_get_application_target_sdk_version(void) { return 34; }

__attribute__((visibility("default"))) uintptr_t pthread_self(void) { return 0; }
__attribute__((visibility("default"))) int pthread_equal(uintptr_t t1, uintptr_t t2) { return t1 == t2; }
__attribute__((visibility("default"))) int pthread_setname_np(uintptr_t thread, const char *name) { (void)thread; (void)name; return 0; }
__attribute__((visibility("default"))) int pthread_getname_np(uintptr_t thread, char *buf, size_t len) { (void)thread; (void)buf; (void)len; return 0; }
__attribute__((visibility("default"))) int pthread_getattr_np(uintptr_t thread, void *attr) { (void)thread; (void)attr; return 0; }
__attribute__((visibility("default"))) int pthread_attr_init(void *attr) { (void)attr; return 0; }
__attribute__((visibility("default"))) int pthread_attr_destroy(void *attr) { (void)attr; return 0; }
__attribute__((visibility("default"))) int pthread_attr_getstack(const void *attr, void **stackaddr, size_t *stacksize) { (void)attr; (void)stackaddr; (void)stacksize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_setstack(void *attr, void *stackaddr, size_t stacksize) { (void)attr; (void)stackaddr; (void)stacksize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_getstacksize(const void *attr, size_t *stacksize) { (void)attr; (void)stacksize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_setstacksize(void *attr, size_t stacksize) { (void)attr; (void)stacksize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_getguardsize(const void *attr, size_t *guardsize) { (void)attr; (void)guardsize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_setguardsize(void *attr, size_t guardsize) { (void)attr; (void)guardsize; return 0; }
__attribute__((visibility("default"))) int pthread_attr_getdetachstate(const void *attr, int *detachstate) { (void)attr; (void)detachstate; return 0; }
__attribute__((visibility("default"))) int pthread_attr_setdetachstate(void *attr, int detachstate) { (void)attr; (void)detachstate; return 0; }
