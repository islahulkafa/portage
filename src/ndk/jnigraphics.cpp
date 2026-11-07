#include "jnigraphics.hpp"
#include "package/jni_env.hpp"

extern "C" {

__attribute__((sysv_abi)) int AndroidBitmap_getInfo(void *env, void *jbitmap,
                                                    AndroidBitmapInfo *info)
{
    (void)env;
    if (!jbitmap || !info) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    auto obj = portage::package::JniEnvironment::Instance().LookupObject(jbitmap);
    if (!obj || obj->type != portage::package::JniObjectType::Bitmap) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    info->width = obj->bitmap_width;
    info->height = obj->bitmap_height;
    info->stride = obj->bitmap_stride;
    info->format = obj->bitmap_format;
    info->flags = obj->bitmap_flags;
    return ANDROID_BITMAP_RESULT_SUCCESS;
}

__attribute__((sysv_abi)) int AndroidBitmap_lockPixels(void *env, void *jbitmap, void **addrPtr)
{
    (void)env;
    if (!jbitmap || !addrPtr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    auto obj = portage::package::JniEnvironment::Instance().LookupObject(jbitmap);
    if (!obj || obj->type != portage::package::JniObjectType::Bitmap) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    obj->bitmap_locked = true;
    *addrPtr = obj->bitmap_pixels.data();
    return ANDROID_BITMAP_RESULT_SUCCESS;
}

__attribute__((sysv_abi)) int AndroidBitmap_unlockPixels(void *env, void *jbitmap)
{
    (void)env;
    if (!jbitmap) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    auto obj = portage::package::JniEnvironment::Instance().LookupObject(jbitmap);
    if (!obj || obj->type != portage::package::JniObjectType::Bitmap) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    if (!obj->bitmap_locked) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    obj->bitmap_locked = false;
    return ANDROID_BITMAP_RESULT_SUCCESS;
}

__attribute__((sysv_abi)) int AndroidBitmap_getData(void *env, void *jbitmap, void **addrPtr)
{
    (void)env;
    if (!jbitmap || !addrPtr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    auto obj = portage::package::JniEnvironment::Instance().LookupObject(jbitmap);
    if (!obj || obj->type != portage::package::JniObjectType::Bitmap) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    *addrPtr = obj->bitmap_pixels.data();
    return ANDROID_BITMAP_RESULT_SUCCESS;
}

} // extern "C"
