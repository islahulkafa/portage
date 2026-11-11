#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct AndroidBitmapInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t format;
    uint32_t flags;
};

__attribute__((visibility("default"))) int AndroidBitmap_getInfo(void *env, void *jbitmap,
                                                                 struct AndroidBitmapInfo *info)
{
    (void)env;
    (void)jbitmap;
    (void)info;
    return -1;
}

__attribute__((visibility("default"))) int AndroidBitmap_lockPixels(void *env, void *jbitmap,
                                                                    void **addrPtr)
{
    (void)env;
    (void)jbitmap;
    (void)addrPtr;
    return -1;
}

__attribute__((visibility("default"))) int AndroidBitmap_unlockPixels(void *env, void *jbitmap)
{
    (void)env;
    (void)jbitmap;
    return -1;
}

__attribute__((visibility("default"))) int AndroidBitmap_getData(void *env, void *jbitmap,
                                                                 void **addrPtr)
{
    (void)env;
    (void)jbitmap;
    (void)addrPtr;
    return -1;
}

__attribute__((visibility("default"))) int AImageDecoder_createFromAAsset(void *asset, void **out)
{
    (void)asset;
    if (out)
        *out = (void *)0;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_createFromBuffer(const void *buf,
                                                                          size_t len, void **out)
{
    (void)buf;
    (void)len;
    if (out)
        *out = (void *)0;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_createFromFd(int fd, void **out)
{
    (void)fd;
    if (out)
        *out = (void *)0;
    return -1;
}
__attribute__((visibility("default"))) void AImageDecoder_delete(void *dec) { (void)dec; }
__attribute__((visibility("default"))) const void *AImageDecoder_getHeaderInfo(const void *dec)
{
    (void)dec;
    return (void *)0;
}
__attribute__((visibility("default"))) int32_t AImageDecoderHeaderInfo_getWidth(const void *info)
{
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) int32_t AImageDecoderHeaderInfo_getHeight(const void *info)
{
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) const char *
AImageDecoderHeaderInfo_getMimeType(const void *info)
{
    (void)info;
    return "";
}
__attribute__((visibility("default"))) int32_t
AImageDecoderHeaderInfo_getDataSpace(const void *info)
{
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) int32_t
AImageDecoderHeaderInfo_getAndroidBitmapFormat(const void *info)
{
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) int AImageDecoderHeaderInfo_getAlphaFlags(const void *info)
{
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) int AImageDecoder_setAndroidBitmapFormat(void *dec,
                                                                                int32_t fmt)
{
    (void)dec;
    (void)fmt;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_setUnpremultipliedRequired(void *dec,
                                                                                    bool req)
{
    (void)dec;
    (void)req;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_setDataSpace(void *dec, int32_t ds)
{
    (void)dec;
    (void)ds;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_setTargetSize(void *dec, int32_t w,
                                                                       int32_t h)
{
    (void)dec;
    (void)w;
    (void)h;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_setCrop(void *dec, struct ARect crop)
{
    (void)dec;
    (void)crop;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_computeSampledSize(const void *dec, int s,
                                                                            int32_t *w, int32_t *h)
{
    (void)dec;
    (void)s;
    (void)w;
    (void)h;
    return -1;
}
__attribute__((visibility("default"))) size_t AImageDecoder_getMinimumStride(const void *dec)
{
    (void)dec;
    return 0;
}
__attribute__((visibility("default"))) int AImageDecoder_decodeImage(void *dec, void *px,
                                                                     size_t stride, size_t size)
{
    (void)dec;
    (void)px;
    (void)stride;
    (void)size;
    return -1;
}
__attribute__((visibility("default"))) const char *AImageDecoder_resultToString(int result)
{
    (void)result;
    return "";
}
__attribute__((visibility("default"))) bool AImageDecoder_isAnimated(const void *dec)
{
    (void)dec;
    return false;
}
__attribute__((visibility("default"))) int AImageDecoder_setRepeatCount(void *dec, int32_t cnt)
{
    (void)dec;
    (void)cnt;
    return -1;
}
__attribute__((visibility("default"))) int32_t AImageDecoder_getRepeatCount(const void *dec)
{
    (void)dec;
    return -1;
}
__attribute__((visibility("default"))) void *AImageDecoderFrameInfo_create(void)
{
    return (void *)0;
}
__attribute__((visibility("default"))) void AImageDecoderFrameInfo_delete(void *info)
{
    (void)info;
}
__attribute__((visibility("default"))) int AImageDecoder_getFrameInfo(const void *dec, void *info)
{
    (void)dec;
    (void)info;
    return -1;
}
__attribute__((visibility("default"))) int64_t AImageDecoderFrameInfo_getDuration(const void *info)
{
    (void)info;
    return -1;
}
__attribute__((visibility("default"))) struct ARect
AImageDecoderFrameInfo_getFrameRect(const void *info)
{
    (void)info;
    struct ARect r = {0, 0, 0, 0};
    return r;
}
__attribute__((visibility("default"))) int AImageDecoder_advanceFrame(void *dec)
{
    (void)dec;
    return -1;
}
__attribute__((visibility("default"))) int AImageDecoder_rewind(void *dec)
{
    (void)dec;
    return -1;
}
