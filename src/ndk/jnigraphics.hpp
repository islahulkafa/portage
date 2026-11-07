#pragma once

#include <cstdint>

enum {
    ANDROID_BITMAP_RESULT_SUCCESS = 0,
    ANDROID_BITMAP_RESULT_BAD_PARAMETER = -1,
    ANDROID_BITMAP_RESULT_JNI_EXCEPTION = -2,
    ANDROID_BITMAP_RESULT_ALLOCATION_FAILED = -3,
};

enum AndroidBitmapFormat {
    ANDROID_BITMAP_FORMAT_NONE = 0,
    ANDROID_BITMAP_FORMAT_RGBA_8888 = 1,
    ANDROID_BITMAP_FORMAT_RGB_565 = 4,
    ANDROID_BITMAP_FORMAT_A_8 = 8,
    ANDROID_BITMAP_FORMAT_RGBA_F16 = 9,
    ANDROID_BITMAP_FORMAT_RGBA_1010102 = 10,
};

enum {
    ANDROID_BITMAP_FLAGS_ALPHA_MASK = 0x3,
    ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE = 0,
    ANDROID_BITMAP_FLAGS_ALPHA_PREMUL = 1,
    ANDROID_BITMAP_FLAGS_ALPHA_UNPREMUL = 2,
};

struct AndroidBitmapInfo {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride{0};
    int32_t format{0};
    uint32_t flags{0};
};

extern "C" {

__attribute__((sysv_abi)) int AndroidBitmap_getInfo(void *env, void *jbitmap,
                                                    AndroidBitmapInfo *info);

__attribute__((sysv_abi)) int AndroidBitmap_lockPixels(void *env, void *jbitmap, void **addrPtr);

__attribute__((sysv_abi)) int AndroidBitmap_unlockPixels(void *env, void *jbitmap);

__attribute__((sysv_abi)) int AndroidBitmap_getData(void *env, void *jbitmap, void **addrPtr);

} // extern "C"
