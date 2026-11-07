#include <stddef.h>
#include <stdint.h>

struct ANativeWindow;
struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
};

struct AHardwareBuffer;
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

// ANativeWindow exports
__attribute__((visibility("default"))) void ANativeWindow_acquire(struct ANativeWindow *window)
{
    (void)window;
}

__attribute__((visibility("default"))) void ANativeWindow_release(struct ANativeWindow *window)
{
    (void)window;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getWidth(struct ANativeWindow *window)
{
    (void)window;
    return 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getHeight(struct ANativeWindow *window)
{
    (void)window;
    return 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getFormat(struct ANativeWindow *window)
{
    (void)window;
    return 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_setBuffersGeometry(
    struct ANativeWindow *window, int32_t width, int32_t height, int32_t format)
{
    (void)window;
    (void)width;
    (void)height;
    (void)format;
    return 0;
}

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

// AHardwareBuffer exports
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

__attribute__((visibility("default"))) void AHardwareBuffer_describe(const AHardwareBuffer *buffer,
                                                                     AHardwareBuffer_Desc *outDesc)
{
    (void)buffer;
    (void)outDesc;
}

__attribute__((visibility("default"))) int AHardwareBuffer_lock(AHardwareBuffer *buffer,
                                                                uint64_t usage, int32_t fence,
                                                                const struct ARect *rect,
                                                                void **outVirtualAddress)
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

__attribute__((visibility("default"))) int AHardwareBuffer_getId(const AHardwareBuffer *buffer,
                                                                 uint64_t *outId)
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
