#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AMediaFormat;
typedef struct AMediaFormat AMediaFormat;

struct AMediaExtractor;
typedef struct AMediaExtractor AMediaExtractor;

struct AMediaCodec;
typedef struct AMediaCodec AMediaCodec;

struct AMediaCrypto;
typedef struct AMediaCrypto AMediaCrypto;

struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;

typedef struct AMediaCodecBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;

// AMediaFormat exports
__attribute__((visibility("default"))) AMediaFormat *AMediaFormat_new(void)
{
    return (AMediaFormat *)0;
}

__attribute__((visibility("default"))) int32_t AMediaFormat_delete(AMediaFormat *format)
{
    (void)format;
    return 0;
}

__attribute__((visibility("default"))) bool AMediaFormat_getString(AMediaFormat *format,
                                                                  const char *name,
                                                                  const char **out)
{
    (void)format;
    (void)name;
    (void)out;
    return false;
}

__attribute__((visibility("default"))) bool AMediaFormat_getInt32(AMediaFormat *format,
                                                                 const char *name, int32_t *out)
{
    (void)format;
    (void)name;
    (void)out;
    return false;
}

__attribute__((visibility("default"))) bool AMediaFormat_getInt64(AMediaFormat *format,
                                                                 const char *name, int64_t *out)
{
    (void)format;
    (void)name;
    (void)out;
    return false;
}

__attribute__((visibility("default"))) bool AMediaFormat_getFloat(AMediaFormat *format,
                                                                 const char *name, float *out)
{
    (void)format;
    (void)name;
    (void)out;
    return false;
}

__attribute__((visibility("default"))) bool AMediaFormat_getSize(AMediaFormat *format,
                                                                const char *name, size_t *out)
{
    (void)format;
    (void)name;
    (void)out;
    return false;
}

__attribute__((visibility("default"))) bool AMediaFormat_getBuffer(AMediaFormat *format,
                                                                  const char *name, void **data,
                                                                  size_t *size)
{
    (void)format;
    (void)name;
    (void)data;
    (void)size;
    return false;
}

__attribute__((visibility("default"))) void AMediaFormat_setString(AMediaFormat *format,
                                                                  const char *name,
                                                                  const char *value)
{
    (void)format;
    (void)name;
    (void)value;
}

__attribute__((visibility("default"))) void AMediaFormat_setInt32(AMediaFormat *format,
                                                                 const char *name, int32_t value)
{
    (void)format;
    (void)name;
    (void)value;
}

__attribute__((visibility("default"))) void AMediaFormat_setInt64(AMediaFormat *format,
                                                                 const char *name, int64_t value)
{
    (void)format;
    (void)name;
    (void)value;
}

__attribute__((visibility("default"))) void AMediaFormat_setFloat(AMediaFormat *format,
                                                                 const char *name, float value)
{
    (void)format;
    (void)name;
    (void)value;
}

__attribute__((visibility("default"))) void AMediaFormat_setSize(AMediaFormat *format,
                                                                const char *name, size_t value)
{
    (void)format;
    (void)name;
    (void)value;
}

__attribute__((visibility("default"))) void AMediaFormat_setBuffer(AMediaFormat *format,
                                                                  const char *name,
                                                                  const void *data, size_t size)
{
    (void)format;
    (void)name;
    (void)data;
    (void)size;
}

__attribute__((visibility("default"))) const char *AMediaFormat_toString(AMediaFormat *format)
{
    (void)format;
    return "";
}

__attribute__((visibility("default"))) bool AMediaFormat_copy(AMediaFormat *to, AMediaFormat *from)
{
    (void)to;
    (void)from;
    return false;
}

__attribute__((visibility("default"))) void AMediaFormat_clear(AMediaFormat *format)
{
    (void)format;
}

// AMediaExtractor exports
__attribute__((visibility("default"))) AMediaExtractor *AMediaExtractor_new(void)
{
    return (AMediaExtractor *)0;
}

__attribute__((visibility("default"))) int32_t AMediaExtractor_delete(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AMediaExtractor_setDataSourceFd(AMediaExtractor *extractor, int fd, int64_t offset,
                                int64_t length)
{
    (void)extractor;
    (void)fd;
    (void)offset;
    (void)length;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AMediaExtractor_setDataSource(AMediaExtractor *extractor, const char *location)
{
    (void)extractor;
    (void)location;
    return 0;
}

__attribute__((visibility("default"))) size_t
AMediaExtractor_getTrackCount(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) AMediaFormat *
AMediaExtractor_getTrackFormat(AMediaExtractor *extractor, size_t idx)
{
    (void)extractor;
    (void)idx;
    return (AMediaFormat *)0;
}

__attribute__((visibility("default"))) int32_t
AMediaExtractor_selectTrack(AMediaExtractor *extractor, size_t idx)
{
    (void)extractor;
    (void)idx;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AMediaExtractor_unselectTrack(AMediaExtractor *extractor, size_t idx)
{
    (void)extractor;
    (void)idx;
    return 0;
}

__attribute__((visibility("default"))) int64_t
AMediaExtractor_readSampleData(AMediaExtractor *extractor, uint8_t *buffer, size_t capacity)
{
    (void)extractor;
    (void)buffer;
    (void)capacity;
    return 0;
}

__attribute__((visibility("default"))) int64_t
AMediaExtractor_getSampleSize(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) int
AMediaExtractor_getSampleTrackIndex(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) int64_t
AMediaExtractor_getSampleTime(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) uint32_t
AMediaExtractor_getSampleFlags(AMediaExtractor *extractor)
{
    (void)extractor;
    return 0;
}

__attribute__((visibility("default"))) bool AMediaExtractor_advance(AMediaExtractor *extractor)
{
    (void)extractor;
    return false;
}

__attribute__((visibility("default"))) int32_t
AMediaExtractor_seekTo(AMediaExtractor *extractor, int64_t timeUs, int mode)
{
    (void)extractor;
    (void)timeUs;
    (void)mode;
    return 0;
}

// AMediaCodec exports
__attribute__((visibility("default"))) AMediaCodec *
AMediaCodec_createCodecByName(const char *name)
{
    (void)name;
    return (AMediaCodec *)0;
}

__attribute__((visibility("default"))) AMediaCodec *
AMediaCodec_createDecoderByType(const char *mime_type)
{
    (void)mime_type;
    return (AMediaCodec *)0;
}

__attribute__((visibility("default"))) AMediaCodec *
AMediaCodec_createEncoderByType(const char *mime_type)
{
    (void)mime_type;
    return (AMediaCodec *)0;
}

__attribute__((visibility("default"))) int32_t AMediaCodec_delete(AMediaCodec *codec)
{
    (void)codec;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AMediaCodec_configure(AMediaCodec *codec, const AMediaFormat *format, ANativeWindow *surface,
                      AMediaCrypto *crypto, uint32_t flags)
{
    (void)codec;
    (void)format;
    (void)surface;
    (void)crypto;
    (void)flags;
    return 0;
}

__attribute__((visibility("default"))) int32_t AMediaCodec_start(AMediaCodec *codec)
{
    (void)codec;
    return 0;
}

__attribute__((visibility("default"))) int32_t AMediaCodec_stop(AMediaCodec *codec)
{
    (void)codec;
    return 0;
}

__attribute__((visibility("default"))) int32_t AMediaCodec_flush(AMediaCodec *codec)
{
    (void)codec;
    return 0;
}

__attribute__((visibility("default"))) int64_t
AMediaCodec_dequeueInputBuffer(AMediaCodec *codec, int64_t timeoutUs)
{
    (void)codec;
    (void)timeoutUs;
    return 0;
}

__attribute__((visibility("default"))) uint8_t *
AMediaCodec_getInputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size)
{
    (void)codec;
    (void)idx;
    (void)out_size;
    return (uint8_t *)0;
}

__attribute__((visibility("default"))) int32_t
AMediaCodec_queueInputBuffer(AMediaCodec *codec, size_t idx, int64_t offset, size_t size,
                             uint64_t timeUs, uint32_t flags)
{
    (void)codec;
    (void)idx;
    (void)offset;
    (void)size;
    (void)timeUs;
    (void)flags;
    return 0;
}

__attribute__((visibility("default"))) int64_t
AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    (void)codec;
    (void)info;
    (void)timeoutUs;
    return 0;
}

__attribute__((visibility("default"))) uint8_t *
AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size)
{
    (void)codec;
    (void)idx;
    (void)out_size;
    return (uint8_t *)0;
}

__attribute__((visibility("default"))) AMediaFormat *
AMediaCodec_getOutputFormat(AMediaCodec *codec)
{
    (void)codec;
    return (AMediaFormat *)0;
}

__attribute__((visibility("default"))) int32_t
AMediaCodec_releaseOutputBuffer(AMediaCodec *codec, size_t idx, bool render)
{
    (void)codec;
    (void)idx;
    (void)render;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AMediaCodec_releaseOutputBufferAtTime(AMediaCodec *codec, size_t idx, int64_t renderTimestampNs)
{
    (void)codec;
    (void)idx;
    (void)renderTimestampNs;
    return 0;
}

struct AImageReader;
typedef struct AImageReader AImageReader;

struct AImage;
typedef struct AImage AImage;

typedef struct AImageReader_ImageListener {
    void *context;
    void (*onImageAvailable)(void *context, AImageReader *reader);
} AImageReader_ImageListener;

__attribute__((visibility("default"))) int32_t
AImageReader_new(int32_t width, int32_t height, int32_t format, int32_t maxImages,
                 AImageReader **reader)
{
    (void)width;
    (void)height;
    (void)format;
    (void)maxImages;
    (void)reader;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImageReader_newWithUsage(int32_t width, int32_t height, int32_t format, uint64_t usage,
                          int32_t maxImages, AImageReader **reader)
{
    (void)width;
    (void)height;
    (void)format;
    (void)usage;
    (void)maxImages;
    (void)reader;
    return 0;
}

__attribute__((visibility("default"))) void AImageReader_delete(AImageReader *reader)
{
    (void)reader;
}

__attribute__((visibility("default"))) int32_t
AImageReader_getWindow(AImageReader *reader, ANativeWindow **window)
{
    (void)reader;
    (void)window;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImageReader_acquireNextImage(AImageReader *reader, AImage **image)
{
    (void)reader;
    (void)image;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImageReader_acquireLatestImage(AImageReader *reader, AImage **image)
{
    (void)reader;
    (void)image;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImageReader_setImageListener(AImageReader *reader, AImageReader_ImageListener *listener)
{
    (void)reader;
    (void)listener;
    return 0;
}

__attribute__((visibility("default"))) void AImage_delete(AImage *image)
{
    (void)image;
}

__attribute__((visibility("default"))) int32_t AImage_getWidth(const AImage *image, int32_t *width)
{
    (void)image;
    (void)width;
    return 0;
}

__attribute__((visibility("default"))) int32_t AImage_getHeight(const AImage *image, int32_t *height)
{
    (void)image;
    (void)height;
    return 0;
}

__attribute__((visibility("default"))) int32_t AImage_getFormat(const AImage *image, int32_t *format)
{
    (void)image;
    (void)format;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImage_getTimestamp(const AImage *image, int64_t *timestamp)
{
    (void)image;
    (void)timestamp;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImage_getNumberOfPlanes(const AImage *image, int32_t *numPlanes)
{
    (void)image;
    (void)numPlanes;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImage_getPlanePixelStride(const AImage *image, int32_t planeIdx, int32_t *pixelStride)
{
    (void)image;
    (void)planeIdx;
    (void)pixelStride;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImage_getPlaneRowStride(const AImage *image, int32_t planeIdx, int32_t *rowStride)
{
    (void)image;
    (void)planeIdx;
    (void)rowStride;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AImage_getPlaneData(const AImage *image, int32_t planeIdx, uint8_t **data, int *dataLength)
{
    (void)image;
    (void)planeIdx;
    (void)data;
    (void)dataLength;
    return 0;
}
