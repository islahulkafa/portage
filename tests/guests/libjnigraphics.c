#include <stdint.h>

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
