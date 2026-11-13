#include "libandroid_thunks.hpp"
#include "android_log.hpp"
#include "bionic_thunks.hpp"
#include "audio/aaudio_dispatcher.hpp"
#include "audio/opensles_dispatcher.hpp"
#include "binder/binder_ndk.hpp"
#include "binder/binder_parcel.hpp"
#include "binder/binder_status.hpp"
#include "binder/service_manager.hpp"
#include "camera/camera_ndk.hpp"
#include "clipboard/clipboard_ndk.hpp"
#include "location/location_ndk.hpp"
#include "notification/notification_ndk.hpp"
#include "power/power_ndk.hpp"
#include "security/keystore_ndk.hpp"
#include "configuration.hpp"
#include "elf/link_map.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "input_queue.hpp"
#include "image_decoder.hpp"
#include "jnigraphics.hpp"
#include "looper.hpp"
#include "media/media_codec.hpp"
#include "media/media_extractor.hpp"
#include "media/media_format.hpp"
#include "media/media_muxer.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/hardware_buffer.hpp"
#include "ndk/multinetwork.hpp"
#include "ndk/performance_hint.hpp"
#include "ndk/storage_manager.hpp"
#include "ndk/swappy.hpp"
#include "ndk/thermal.hpp"
#include "ndk/atrace.hpp"
#include "package/asset_manager.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"
#include "property/property_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include "wsi/window_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace portage::ndk {

NdkSymbolRegistry &NdkSymbolRegistry::Instance() noexcept
{
    static NdkSymbolRegistry instance;
    return instance;
}

NdkSymbolRegistry::NdkSymbolRegistry() { RegisterAll(); }

void NdkSymbolRegistry::Register(std::string_view name, uintptr_t address)
{
    symbols_[std::string(name)] = address;
}

uintptr_t NdkSymbolRegistry::Lookup(std::string_view name) const
{
    auto it = symbols_.find(std::string(name));
    return (it != symbols_.end()) ? it->second : 0;
}

size_t NdkSymbolRegistry::Count() const noexcept { return symbols_.size(); }

void NdkSymbolRegistry::RegisterFallback(std::string_view name, uintptr_t address)
{
    fallback_symbols_[std::string(name)] = address;
}

uintptr_t NdkSymbolRegistry::LookupFallback(std::string_view name) const
{
    auto it = fallback_symbols_.find(std::string(name));
    return (it != fallback_symbols_.end()) ? it->second : 0;
}

size_t NdkSymbolRegistry::FallbackCount() const noexcept { return fallback_symbols_.size(); }

void NdkSymbolRegistry::RegisterAll()
{
    // ALooper
    Register("ALooper_forThread", reinterpret_cast<uintptr_t>(&ALooper_forThread));
    Register("ALooper_prepare", reinterpret_cast<uintptr_t>(&ALooper_prepare));
    Register("ALooper_acquire", reinterpret_cast<uintptr_t>(&ALooper_acquire));
    Register("ALooper_release", reinterpret_cast<uintptr_t>(&ALooper_release));
    Register("ALooper_pollOnce", reinterpret_cast<uintptr_t>(&ALooper_pollOnce));
    Register("ALooper_pollAll", reinterpret_cast<uintptr_t>(&ALooper_pollAll));
    Register("ALooper_wake", reinterpret_cast<uintptr_t>(&ALooper_wake));
    Register("ALooper_addFd", reinterpret_cast<uintptr_t>(&ALooper_addFd));
    Register("ALooper_removeFd", reinterpret_cast<uintptr_t>(&ALooper_removeFd));

    // AChoreographer
    Register("AChoreographer_getInstance",
             reinterpret_cast<uintptr_t>(&AChoreographer_getInstance));
    Register("AChoreographer_postFrameCallback",
             reinterpret_cast<uintptr_t>(&AChoreographer_postFrameCallback));
    Register("AChoreographer_postFrameCallbackDelayed",
             reinterpret_cast<uintptr_t>(&AChoreographer_postFrameCallbackDelayed));
    Register("AChoreographer_postFrameCallback64",
             reinterpret_cast<uintptr_t>(&AChoreographer_postFrameCallback64));
    Register("AChoreographer_postFrameCallbackDelayed64",
             reinterpret_cast<uintptr_t>(&AChoreographer_postFrameCallbackDelayed64));
    Register("AChoreographer_registerRefreshRateCallback",
             reinterpret_cast<uintptr_t>(&AChoreographer_registerRefreshRateCallback));
    Register("AChoreographer_unregisterRefreshRateCallback",
             reinterpret_cast<uintptr_t>(&AChoreographer_unregisterRefreshRateCallback));

    // ASensorManager
    Register("ASensorManager_getInstance",
             reinterpret_cast<uintptr_t>(&ASensorManager_getInstance));
    Register("ASensorManager_getInstanceForPackage",
             reinterpret_cast<uintptr_t>(&ASensorManager_getInstanceForPackage));
    Register("ASensorManager_getSensorList",
             reinterpret_cast<uintptr_t>(&ASensorManager_getSensorList));
    Register("ASensorManager_getDefaultSensor",
             reinterpret_cast<uintptr_t>(&ASensorManager_getDefaultSensor));
    Register("ASensorManager_getDefaultSensorEx",
             reinterpret_cast<uintptr_t>(&ASensorManager_getDefaultSensorEx));
    Register("ASensorManager_createEventQueue",
             reinterpret_cast<uintptr_t>(&ASensorManager_createEventQueue));
    Register("ASensorManager_destroyEventQueue",
             reinterpret_cast<uintptr_t>(&ASensorManager_destroyEventQueue));
    Register("ASensorManager_createSharedMemoryDirectChannel",
             reinterpret_cast<uintptr_t>(&ASensorManager_createSharedMemoryDirectChannel));
    Register("ASensorManager_createHardwareBufferDirectChannel",
             reinterpret_cast<uintptr_t>(&ASensorManager_createHardwareBufferDirectChannel));
    Register("ASensorManager_destroyDirectChannel",
             reinterpret_cast<uintptr_t>(&ASensorManager_destroyDirectChannel));
    Register("ASensorManager_configureDirectReport",
             reinterpret_cast<uintptr_t>(&ASensorManager_configureDirectReport));

    // ASensorEventQueue
    Register("ASensorEventQueue_registerSensor",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_registerSensor));
    Register("ASensorEventQueue_enableSensor",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_enableSensor));
    Register("ASensorEventQueue_disableSensor",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_disableSensor));
    Register("ASensorEventQueue_setEventRate",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_setEventRate));
    Register("ASensorEventQueue_hasEvents",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_hasEvents));
    Register("ASensorEventQueue_getEvents",
             reinterpret_cast<uintptr_t>(&ASensorEventQueue_getEvents));

    // ASensor
    Register("ASensor_getName", reinterpret_cast<uintptr_t>(&ASensor_getName));
    Register("ASensor_getVendor", reinterpret_cast<uintptr_t>(&ASensor_getVendor));
    Register("ASensor_getType", reinterpret_cast<uintptr_t>(&ASensor_getType));
    Register("ASensor_getResolution", reinterpret_cast<uintptr_t>(&ASensor_getResolution));
    Register("ASensor_getMinDelay", reinterpret_cast<uintptr_t>(&ASensor_getMinDelay));
    Register("ASensor_getFifoMaxEventCount",
             reinterpret_cast<uintptr_t>(&ASensor_getFifoMaxEventCount));
    Register("ASensor_getFifoReservedEventCount",
             reinterpret_cast<uintptr_t>(&ASensor_getFifoReservedEventCount));
    Register("ASensor_getStringType", reinterpret_cast<uintptr_t>(&ASensor_getStringType));
    Register("ASensor_getReportingMode", reinterpret_cast<uintptr_t>(&ASensor_getReportingMode));
    Register("ASensor_isWakeUpSensor", reinterpret_cast<uintptr_t>(&ASensor_isWakeUpSensor));
    Register("ASensor_isDirectChannelTypeSupported",
             reinterpret_cast<uintptr_t>(&ASensor_isDirectChannelTypeSupported));
    Register("ASensor_getHighestDirectReportRateLevel",
             reinterpret_cast<uintptr_t>(&ASensor_getHighestDirectReportRateLevel));
    Register("ASensor_getHandle", reinterpret_cast<uintptr_t>(&ASensor_getHandle));

    // AInputQueue
    Register("AInputQueue_attachLooper", reinterpret_cast<uintptr_t>(&AInputQueue_attachLooper));
    Register("AInputQueue_detachLooper", reinterpret_cast<uintptr_t>(&AInputQueue_detachLooper));
    Register("AInputQueue_hasEvents", reinterpret_cast<uintptr_t>(&AInputQueue_hasEvents));
    Register("AInputQueue_getEvent", reinterpret_cast<uintptr_t>(&AInputQueue_getEvent));
    Register("AInputQueue_preDispatchEvent",
             reinterpret_cast<uintptr_t>(&AInputQueue_preDispatchEvent));
    Register("AInputQueue_finishEvent", reinterpret_cast<uintptr_t>(&AInputQueue_finishEvent));

    // AInputEvent & AKeyEvent & AMotionEvent
    Register("AInputEvent_getType", reinterpret_cast<uintptr_t>(&AInputEvent_getType));
    Register("AInputEvent_getDeviceId", reinterpret_cast<uintptr_t>(&AInputEvent_getDeviceId));
    Register("AInputEvent_getSource", reinterpret_cast<uintptr_t>(&AInputEvent_getSource));
    Register("AKeyEvent_getAction", reinterpret_cast<uintptr_t>(&AKeyEvent_getAction));
    Register("AKeyEvent_getFlags", reinterpret_cast<uintptr_t>(&AKeyEvent_getFlags));
    Register("AKeyEvent_getKeyCode", reinterpret_cast<uintptr_t>(&AKeyEvent_getKeyCode));
    Register("AKeyEvent_getScanCode", reinterpret_cast<uintptr_t>(&AKeyEvent_getScanCode));
    Register("AKeyEvent_getMetaState", reinterpret_cast<uintptr_t>(&AKeyEvent_getMetaState));
    Register("AKeyEvent_getRepeatCount", reinterpret_cast<uintptr_t>(&AKeyEvent_getRepeatCount));
    Register("AKeyEvent_getDownTime", reinterpret_cast<uintptr_t>(&AKeyEvent_getDownTime));
    Register("AKeyEvent_getEventTime", reinterpret_cast<uintptr_t>(&AKeyEvent_getEventTime));
    Register("AMotionEvent_getAction", reinterpret_cast<uintptr_t>(&AMotionEvent_getAction));
    Register("AMotionEvent_getFlags", reinterpret_cast<uintptr_t>(&AMotionEvent_getFlags));
    Register("AMotionEvent_getDownTime", reinterpret_cast<uintptr_t>(&AMotionEvent_getDownTime));
    Register("AMotionEvent_getEventTime", reinterpret_cast<uintptr_t>(&AMotionEvent_getEventTime));
    Register("AMotionEvent_getPointerCount",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getPointerCount));
    Register("AMotionEvent_getPointerId", reinterpret_cast<uintptr_t>(&AMotionEvent_getPointerId));
    Register("AMotionEvent_getX", reinterpret_cast<uintptr_t>(&AMotionEvent_getX));
    Register("AMotionEvent_getY", reinterpret_cast<uintptr_t>(&AMotionEvent_getY));
    Register("AMotionEvent_getPressure", reinterpret_cast<uintptr_t>(&AMotionEvent_getPressure));
    Register("AMotionEvent_getSize", reinterpret_cast<uintptr_t>(&AMotionEvent_getSize));
    Register("AMotionEvent_getRawX", reinterpret_cast<uintptr_t>(&AMotionEvent_getRawX));
    Register("AMotionEvent_getRawY", reinterpret_cast<uintptr_t>(&AMotionEvent_getRawY));
    Register("AMotionEvent_getAxisValue", reinterpret_cast<uintptr_t>(&AMotionEvent_getAxisValue));
    Register("AMotionEvent_getTouchMajor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getTouchMajor));
    Register("AMotionEvent_getTouchMinor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getTouchMinor));
    Register("AMotionEvent_getToolMajor", reinterpret_cast<uintptr_t>(&AMotionEvent_getToolMajor));
    Register("AMotionEvent_getToolMinor", reinterpret_cast<uintptr_t>(&AMotionEvent_getToolMinor));
    Register("AMotionEvent_getOrientation",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getOrientation));
    Register("AMotionEvent_getHistorySize",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistorySize));
    Register("AMotionEvent_getHistoricalEventTime",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalEventTime));
    Register("AMotionEvent_getHistoricalRawX",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalRawX));
    Register("AMotionEvent_getHistoricalRawY",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalRawY));
    Register("AMotionEvent_getHistoricalX",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalX));
    Register("AMotionEvent_getHistoricalY",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalY));
    Register("AMotionEvent_getHistoricalPressure",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalPressure));
    Register("AMotionEvent_getHistoricalSize",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalSize));
    Register("AMotionEvent_getHistoricalTouchMajor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalTouchMajor));
    Register("AMotionEvent_getHistoricalTouchMinor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalTouchMinor));
    Register("AMotionEvent_getHistoricalToolMajor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalToolMajor));
    Register("AMotionEvent_getHistoricalToolMinor",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalToolMinor));
    Register("AMotionEvent_getHistoricalOrientation",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalOrientation));
    Register("AMotionEvent_getHistoricalAxisValue",
             reinterpret_cast<uintptr_t>(&AMotionEvent_getHistoricalAxisValue));

    // AConfiguration
    Register("AConfiguration_new", reinterpret_cast<uintptr_t>(&AConfiguration_new));
    Register("AConfiguration_delete", reinterpret_cast<uintptr_t>(&AConfiguration_delete));
    Register("AConfiguration_fromAssetManager",
             reinterpret_cast<uintptr_t>(&AConfiguration_fromAssetManager));
    Register("AConfiguration_copy", reinterpret_cast<uintptr_t>(&AConfiguration_copy));
    Register("AConfiguration_getOrientation",
             reinterpret_cast<uintptr_t>(&AConfiguration_getOrientation));
    Register("AConfiguration_setOrientation",
             reinterpret_cast<uintptr_t>(&AConfiguration_setOrientation));
    Register("AConfiguration_getTouchscreen",
             reinterpret_cast<uintptr_t>(&AConfiguration_getTouchscreen));
    Register("AConfiguration_setTouchscreen",
             reinterpret_cast<uintptr_t>(&AConfiguration_setTouchscreen));
    Register("AConfiguration_getDensity", reinterpret_cast<uintptr_t>(&AConfiguration_getDensity));
    Register("AConfiguration_setDensity", reinterpret_cast<uintptr_t>(&AConfiguration_setDensity));
    Register("AConfiguration_getKeyboard",
             reinterpret_cast<uintptr_t>(&AConfiguration_getKeyboard));
    Register("AConfiguration_setKeyboard",
             reinterpret_cast<uintptr_t>(&AConfiguration_setKeyboard));
    Register("AConfiguration_getNavigation",
             reinterpret_cast<uintptr_t>(&AConfiguration_getNavigation));
    Register("AConfiguration_setNavigation",
             reinterpret_cast<uintptr_t>(&AConfiguration_setNavigation));
    Register("AConfiguration_getKeysHidden",
             reinterpret_cast<uintptr_t>(&AConfiguration_getKeysHidden));
    Register("AConfiguration_setKeysHidden",
             reinterpret_cast<uintptr_t>(&AConfiguration_setKeysHidden));
    Register("AConfiguration_getNavHidden",
             reinterpret_cast<uintptr_t>(&AConfiguration_getNavHidden));
    Register("AConfiguration_setNavHidden",
             reinterpret_cast<uintptr_t>(&AConfiguration_setNavHidden));
    Register("AConfiguration_getSdkVersion",
             reinterpret_cast<uintptr_t>(&AConfiguration_getSdkVersion));
    Register("AConfiguration_setSdkVersion",
             reinterpret_cast<uintptr_t>(&AConfiguration_setSdkVersion));
    Register("AConfiguration_getScreenSize",
             reinterpret_cast<uintptr_t>(&AConfiguration_getScreenSize));
    Register("AConfiguration_setScreenSize",
             reinterpret_cast<uintptr_t>(&AConfiguration_setScreenSize));
    Register("AConfiguration_getScreenLong",
             reinterpret_cast<uintptr_t>(&AConfiguration_getScreenLong));
    Register("AConfiguration_setScreenLong",
             reinterpret_cast<uintptr_t>(&AConfiguration_setScreenLong));
    Register("AConfiguration_getUiModeType",
             reinterpret_cast<uintptr_t>(&AConfiguration_getUiModeType));
    Register("AConfiguration_setUiModeType",
             reinterpret_cast<uintptr_t>(&AConfiguration_setUiModeType));
    Register("AConfiguration_getUiModeNight",
             reinterpret_cast<uintptr_t>(&AConfiguration_getUiModeNight));
    Register("AConfiguration_setUiModeNight",
             reinterpret_cast<uintptr_t>(&AConfiguration_setUiModeNight));
    Register("AConfiguration_getScreenWidthDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_getScreenWidthDp));
    Register("AConfiguration_setScreenWidthDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_setScreenWidthDp));
    Register("AConfiguration_getScreenHeightDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_getScreenHeightDp));
    Register("AConfiguration_setScreenHeightDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_setScreenHeightDp));
    Register("AConfiguration_getSmallestScreenWidthDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_getSmallestScreenWidthDp));
    Register("AConfiguration_setSmallestScreenWidthDp",
             reinterpret_cast<uintptr_t>(&AConfiguration_setSmallestScreenWidthDp));

    // ANativeWindow
    Register("ANativeWindow_setBuffersGeometry",
             reinterpret_cast<uintptr_t>(&ANativeWindow_setBuffersGeometry));
    Register("ANativeWindow_getWidth", reinterpret_cast<uintptr_t>(&ANativeWindow_getWidth));
    Register("ANativeWindow_getHeight", reinterpret_cast<uintptr_t>(&ANativeWindow_getHeight));
    Register("ANativeWindow_getFormat", reinterpret_cast<uintptr_t>(&ANativeWindow_getFormat));
    Register("ANativeWindow_acquire", reinterpret_cast<uintptr_t>(&ANativeWindow_acquire));
    Register("ANativeWindow_release", reinterpret_cast<uintptr_t>(&ANativeWindow_release));
    Register("ANativeWindow_fromSurface", reinterpret_cast<uintptr_t>(&ANativeWindow_fromSurface));
    Register("ANativeWindow_lock", reinterpret_cast<uintptr_t>(&ANativeWindow_lock));
    Register("ANativeWindow_unlockAndPost",
             reinterpret_cast<uintptr_t>(&ANativeWindow_unlockAndPost));
    Register("ANativeWindow_getLastQueueDuration",
             reinterpret_cast<uintptr_t>(&ANativeWindow_getLastQueueDuration));
    Register("ANativeWindow_getFrameTimestamps",
             reinterpret_cast<uintptr_t>(&ANativeWindow_getFrameTimestamps));

    // AAssetManager & AAsset
    Register("AAssetManager_fromJava", reinterpret_cast<uintptr_t>(&AAssetManager_fromJava));
    Register("AAssetManager_open", reinterpret_cast<uintptr_t>(&AAssetManager_open));
    Register("AAssetManager_openDir", reinterpret_cast<uintptr_t>(&AAssetManager_openDir));
    Register("AAsset_read", reinterpret_cast<uintptr_t>(&AAsset_read));
    Register("AAsset_seek", reinterpret_cast<uintptr_t>(&AAsset_seek));
    Register("AAsset_seek64", reinterpret_cast<uintptr_t>(&AAsset_seek64));
    Register("AAsset_getLength", reinterpret_cast<uintptr_t>(&AAsset_getLength));
    Register("AAsset_getLength64", reinterpret_cast<uintptr_t>(&AAsset_getLength64));
    Register("AAsset_getRemainingLength", reinterpret_cast<uintptr_t>(&AAsset_getRemainingLength));
    Register("AAsset_getRemainingLength64",
             reinterpret_cast<uintptr_t>(&AAsset_getRemainingLength64));
    Register("AAsset_getBuffer", reinterpret_cast<uintptr_t>(&AAsset_getBuffer));
    Register("AAsset_isAllocated", reinterpret_cast<uintptr_t>(&AAsset_isAllocated));
    Register("AAsset_close", reinterpret_cast<uintptr_t>(&AAsset_close));
    Register("AAssetDir_getNextFileName", reinterpret_cast<uintptr_t>(&AAssetDir_getNextFileName));
    Register("AAssetDir_rewind", reinterpret_cast<uintptr_t>(&AAssetDir_rewind));
    Register("AAssetDir_close", reinterpret_cast<uintptr_t>(&AAssetDir_close));

    // liblog
    Register("__android_log_write", reinterpret_cast<uintptr_t>(&__android_log_write));
    Register("__android_log_print", reinterpret_cast<uintptr_t>(&__android_log_print));
    Register("__android_log_vprint", reinterpret_cast<uintptr_t>(&__android_log_vprint));
    Register("__android_log_assert", reinterpret_cast<uintptr_t>(&__android_log_assert));

    // EGL
    Register("eglGetDisplay", reinterpret_cast<uintptr_t>(&eglGetDisplay));
    Register("eglInitialize", reinterpret_cast<uintptr_t>(&eglInitialize));
    Register("eglTerminate", reinterpret_cast<uintptr_t>(&eglTerminate));
    Register("eglQueryString", reinterpret_cast<uintptr_t>(&eglQueryString));
    Register("eglGetConfigs", reinterpret_cast<uintptr_t>(&eglGetConfigs));
    Register("eglChooseConfig", reinterpret_cast<uintptr_t>(&eglChooseConfig));
    Register("eglGetConfigAttrib", reinterpret_cast<uintptr_t>(&eglGetConfigAttrib));
    Register("eglCreateWindowSurface", reinterpret_cast<uintptr_t>(&eglCreateWindowSurface));
    Register("eglCreatePbufferSurface", reinterpret_cast<uintptr_t>(&eglCreatePbufferSurface));
    Register("eglDestroySurface", reinterpret_cast<uintptr_t>(&eglDestroySurface));
    Register("eglQuerySurface", reinterpret_cast<uintptr_t>(&eglQuerySurface));
    Register("eglCreateContext", reinterpret_cast<uintptr_t>(&eglCreateContext));
    Register("eglDestroyContext", reinterpret_cast<uintptr_t>(&eglDestroyContext));
    Register("eglMakeCurrent", reinterpret_cast<uintptr_t>(&eglMakeCurrent));
    Register("eglGetCurrentContext", reinterpret_cast<uintptr_t>(&eglGetCurrentContext));
    Register("eglGetCurrentSurface", reinterpret_cast<uintptr_t>(&eglGetCurrentSurface));
    Register("eglGetCurrentDisplay", reinterpret_cast<uintptr_t>(&eglGetCurrentDisplay));
    Register("eglSwapBuffers", reinterpret_cast<uintptr_t>(&eglSwapBuffers));
    Register("eglSwapInterval", reinterpret_cast<uintptr_t>(&eglSwapInterval));
    Register("eglGetError", reinterpret_cast<uintptr_t>(&eglGetError));
    Register("eglGetProcAddress", reinterpret_cast<uintptr_t>(&eglGetProcAddress));
    Register("eglGetNativeClientBufferANDROID",
             reinterpret_cast<uintptr_t>(&eglGetNativeClientBufferANDROID));
    Register("eglCreateImageKHR", reinterpret_cast<uintptr_t>(&eglCreateImageKHR));
    Register("eglDestroyImageKHR", reinterpret_cast<uintptr_t>(&eglDestroyImageKHR));
    Register("eglCreateSync", reinterpret_cast<uintptr_t>(&eglCreateSync));
    Register("eglDestroySync", reinterpret_cast<uintptr_t>(&eglDestroySync));
    Register("eglClientWaitSync", reinterpret_cast<uintptr_t>(&eglClientWaitSync));
    Register("eglWaitSync", reinterpret_cast<uintptr_t>(&eglWaitSync));
    Register("eglGetSyncAttrib", reinterpret_cast<uintptr_t>(&eglGetSyncAttrib));
    Register("eglCreateSyncKHR", reinterpret_cast<uintptr_t>(&eglCreateSyncKHR));
    Register("eglDestroySyncKHR", reinterpret_cast<uintptr_t>(&eglDestroySyncKHR));
    Register("eglClientWaitSyncKHR", reinterpret_cast<uintptr_t>(&eglClientWaitSyncKHR));
    Register("eglWaitSyncKHR", reinterpret_cast<uintptr_t>(&eglWaitSyncKHR));
    Register("eglGetSyncAttribKHR", reinterpret_cast<uintptr_t>(&eglGetSyncAttribKHR));
    Register("eglDupNativeFenceFDANDROID",
             reinterpret_cast<uintptr_t>(&eglDupNativeFenceFDANDROID));

    // OpenGL ES 2.0 / 3.0
    Register("glCreateShader", reinterpret_cast<uintptr_t>(&glCreateShader));
    Register("glShaderSource", reinterpret_cast<uintptr_t>(&glShaderSource));
    Register("glCompileShader", reinterpret_cast<uintptr_t>(&glCompileShader));
    Register("glGetShaderiv", reinterpret_cast<uintptr_t>(&glGetShaderiv));
    Register("glGetShaderInfoLog", reinterpret_cast<uintptr_t>(&glGetShaderInfoLog));
    Register("glDeleteShader", reinterpret_cast<uintptr_t>(&glDeleteShader));
    Register("glCreateProgram", reinterpret_cast<uintptr_t>(&glCreateProgram));
    Register("glAttachShader", reinterpret_cast<uintptr_t>(&glAttachShader));
    Register("glLinkProgram", reinterpret_cast<uintptr_t>(&glLinkProgram));
    Register("glGetProgramiv", reinterpret_cast<uintptr_t>(&glGetProgramiv));
    Register("glGetProgramInfoLog", reinterpret_cast<uintptr_t>(&glGetProgramInfoLog));
    Register("glUseProgram", reinterpret_cast<uintptr_t>(&glUseProgram));
    Register("glDeleteProgram", reinterpret_cast<uintptr_t>(&glDeleteProgram));
    Register("glGetAttribLocation", reinterpret_cast<uintptr_t>(&glGetAttribLocation));
    Register("glEnableVertexAttribArray", reinterpret_cast<uintptr_t>(&glEnableVertexAttribArray));
    Register("glDisableVertexAttribArray",
             reinterpret_cast<uintptr_t>(&glDisableVertexAttribArray));
    Register("glVertexAttribPointer", reinterpret_cast<uintptr_t>(&glVertexAttribPointer));
    Register("glGetUniformLocation", reinterpret_cast<uintptr_t>(&glGetUniformLocation));
    Register("glUniform1i", reinterpret_cast<uintptr_t>(&glUniform1i));
    Register("glUniform1f", reinterpret_cast<uintptr_t>(&glUniform1f));
    Register("glUniform4f", reinterpret_cast<uintptr_t>(&glUniform4f));
    Register("glUniformMatrix4fv", reinterpret_cast<uintptr_t>(&glUniformMatrix4fv));
    Register("glGenBuffers", reinterpret_cast<uintptr_t>(&glGenBuffers));
    Register("glBindBuffer", reinterpret_cast<uintptr_t>(&glBindBuffer));
    Register("glBufferData", reinterpret_cast<uintptr_t>(&glBufferData));
    Register("glBufferSubData", reinterpret_cast<uintptr_t>(&glBufferSubData));
    Register("glDeleteBuffers", reinterpret_cast<uintptr_t>(&glDeleteBuffers));
    Register("glGenVertexArrays", reinterpret_cast<uintptr_t>(&glGenVertexArrays));
    Register("glBindVertexArray", reinterpret_cast<uintptr_t>(&glBindVertexArray));
    Register("glDeleteVertexArrays", reinterpret_cast<uintptr_t>(&glDeleteVertexArrays));
    Register("glActiveTexture", reinterpret_cast<uintptr_t>(&glActiveTexture));
    Register("glGenTextures", reinterpret_cast<uintptr_t>(&glGenTextures));
    Register("glBindTexture", reinterpret_cast<uintptr_t>(&glBindTexture));
    Register("glDeleteTextures", reinterpret_cast<uintptr_t>(&glDeleteTextures));
    Register("glTexImage2D", reinterpret_cast<uintptr_t>(&glTexImage2D));
    Register("glTexParameteri", reinterpret_cast<uintptr_t>(&glTexParameteri));
    Register("glGenFramebuffers", reinterpret_cast<uintptr_t>(&glGenFramebuffers));
    Register("glBindFramebuffer", reinterpret_cast<uintptr_t>(&glBindFramebuffer));
    Register("glFramebufferTexture2D", reinterpret_cast<uintptr_t>(&glFramebufferTexture2D));
    Register("glCheckFramebufferStatus", reinterpret_cast<uintptr_t>(&glCheckFramebufferStatus));
    Register("glDeleteFramebuffers", reinterpret_cast<uintptr_t>(&glDeleteFramebuffers));
    Register("glClear", reinterpret_cast<uintptr_t>(&glClear));
    Register("glClearColor", reinterpret_cast<uintptr_t>(&glClearColor));
    Register("glViewport", reinterpret_cast<uintptr_t>(&glViewport));
    Register("glEnable", reinterpret_cast<uintptr_t>(&glEnable));
    Register("glDisable", reinterpret_cast<uintptr_t>(&glDisable));
    Register("glDrawArrays", reinterpret_cast<uintptr_t>(&glDrawArrays));
    Register("glDrawElements", reinterpret_cast<uintptr_t>(&glDrawElements));
    Register("glGetError", reinterpret_cast<uintptr_t>(&glGetError));
    Register("glGetString", reinterpret_cast<uintptr_t>(&glGetString));
    Register("glReadPixels", reinterpret_cast<uintptr_t>(&glReadPixels));
    Register("glEGLImageTargetTexture2DOES",
             reinterpret_cast<uintptr_t>(&glEGLImageTargetTexture2DOES));
    Register("glFenceSync", reinterpret_cast<uintptr_t>(&glFenceSync));
    Register("glIsSync", reinterpret_cast<uintptr_t>(&glIsSync));
    Register("glDeleteSync", reinterpret_cast<uintptr_t>(&glDeleteSync));
    Register("glClientWaitSync", reinterpret_cast<uintptr_t>(&glClientWaitSync));
    Register("glWaitSync", reinterpret_cast<uintptr_t>(&glWaitSync));
    Register("glGetSynciv", reinterpret_cast<uintptr_t>(&glGetSynciv));
    Register("glBindBufferBase", reinterpret_cast<uintptr_t>(&glBindBufferBase));
    Register("glBindBufferRange", reinterpret_cast<uintptr_t>(&glBindBufferRange));
    Register("glGetUniformBlockIndex", reinterpret_cast<uintptr_t>(&glGetUniformBlockIndex));
    Register("glUniformBlockBinding", reinterpret_cast<uintptr_t>(&glUniformBlockBinding));
    Register("glDrawArraysInstanced", reinterpret_cast<uintptr_t>(&glDrawArraysInstanced));
    Register("glDrawElementsInstanced", reinterpret_cast<uintptr_t>(&glDrawElementsInstanced));
    Register("glVertexAttribDivisor", reinterpret_cast<uintptr_t>(&glVertexAttribDivisor));
    Register("glGenTransformFeedbacks", reinterpret_cast<uintptr_t>(&glGenTransformFeedbacks));
    Register("glBindTransformFeedback", reinterpret_cast<uintptr_t>(&glBindTransformFeedback));
    Register("glBeginTransformFeedback", reinterpret_cast<uintptr_t>(&glBeginTransformFeedback));
    Register("glEndTransformFeedback", reinterpret_cast<uintptr_t>(&glEndTransformFeedback));
    Register("glTransformFeedbackVaryings",
             reinterpret_cast<uintptr_t>(&glTransformFeedbackVaryings));
    Register("glDeleteTransformFeedbacks",
             reinterpret_cast<uintptr_t>(&glDeleteTransformFeedbacks));
    Register("glGenSamplers", reinterpret_cast<uintptr_t>(&glGenSamplers));
    Register("glBindSampler", reinterpret_cast<uintptr_t>(&glBindSampler));
    Register("glSamplerParameteri", reinterpret_cast<uintptr_t>(&glSamplerParameteri));
    Register("glDeleteSamplers", reinterpret_cast<uintptr_t>(&glDeleteSamplers));
    Register("glMapBufferRange", reinterpret_cast<uintptr_t>(&glMapBufferRange));
    Register("glUnmapBuffer", reinterpret_cast<uintptr_t>(&glUnmapBuffer));
    Register("glFlushMappedBufferRange", reinterpret_cast<uintptr_t>(&glFlushMappedBufferRange));
    Register("glPixelStorei", reinterpret_cast<uintptr_t>(&glPixelStorei));
    Register("glTexStorage2D", reinterpret_cast<uintptr_t>(&glTexStorage2D));
    Register("glTexStorage3D", reinterpret_cast<uintptr_t>(&glTexStorage3D));
    Register("glTexImage3D", reinterpret_cast<uintptr_t>(&glTexImage3D));
    Register("glTexSubImage3D", reinterpret_cast<uintptr_t>(&glTexSubImage3D));
    Register("glDispatchCompute", reinterpret_cast<uintptr_t>(&glDispatchCompute));
    Register("glMemoryBarrier", reinterpret_cast<uintptr_t>(&glMemoryBarrier));

    // Pipeline State
    Register("glDepthFunc", reinterpret_cast<uintptr_t>(&glDepthFunc));
    Register("glDepthMask", reinterpret_cast<uintptr_t>(&glDepthMask));
    Register("glClearDepthf", reinterpret_cast<uintptr_t>(&glClearDepthf));
    Register("glDepthRangef", reinterpret_cast<uintptr_t>(&glDepthRangef));
    Register("glColorMask", reinterpret_cast<uintptr_t>(&glColorMask));
    Register("glBlendFunc", reinterpret_cast<uintptr_t>(&glBlendFunc));
    Register("glBlendFuncSeparate", reinterpret_cast<uintptr_t>(&glBlendFuncSeparate));
    Register("glBlendEquation", reinterpret_cast<uintptr_t>(&glBlendEquation));
    Register("glBlendEquationSeparate", reinterpret_cast<uintptr_t>(&glBlendEquationSeparate));
    Register("glBlendColor", reinterpret_cast<uintptr_t>(&glBlendColor));
    Register("glCullFace", reinterpret_cast<uintptr_t>(&glCullFace));
    Register("glFrontFace", reinterpret_cast<uintptr_t>(&glFrontFace));
    Register("glScissor", reinterpret_cast<uintptr_t>(&glScissor));
    Register("glStencilFunc", reinterpret_cast<uintptr_t>(&glStencilFunc));
    Register("glStencilFuncSeparate", reinterpret_cast<uintptr_t>(&glStencilFuncSeparate));
    Register("glStencilOp", reinterpret_cast<uintptr_t>(&glStencilOp));
    Register("glStencilOpSeparate", reinterpret_cast<uintptr_t>(&glStencilOpSeparate));
    Register("glStencilMask", reinterpret_cast<uintptr_t>(&glStencilMask));
    Register("glStencilMaskSeparate", reinterpret_cast<uintptr_t>(&glStencilMaskSeparate));
    Register("glLineWidth", reinterpret_cast<uintptr_t>(&glLineWidth));
    Register("glPolygonOffset", reinterpret_cast<uintptr_t>(&glPolygonOffset));
    Register("glSampleCoverage", reinterpret_cast<uintptr_t>(&glSampleCoverage));
    Register("glFlush", reinterpret_cast<uintptr_t>(&glFlush));
    Register("glFinish", reinterpret_cast<uintptr_t>(&glFinish));
    Register("glHint", reinterpret_cast<uintptr_t>(&glHint));

    // Renderbuffers & Framebuffers
    Register("glGenRenderbuffers", reinterpret_cast<uintptr_t>(&glGenRenderbuffers));
    Register("glBindRenderbuffer", reinterpret_cast<uintptr_t>(&glBindRenderbuffer));
    Register("glDeleteRenderbuffers", reinterpret_cast<uintptr_t>(&glDeleteRenderbuffers));
    Register("glIsRenderbuffer", reinterpret_cast<uintptr_t>(&glIsRenderbuffer));
    Register("glRenderbufferStorage", reinterpret_cast<uintptr_t>(&glRenderbufferStorage));
    Register("glRenderbufferStorageMultisample",
             reinterpret_cast<uintptr_t>(&glRenderbufferStorageMultisample));
    Register("glFramebufferRenderbuffer", reinterpret_cast<uintptr_t>(&glFramebufferRenderbuffer));
    Register("glBlitFramebuffer", reinterpret_cast<uintptr_t>(&glBlitFramebuffer));
    Register("glDrawBuffers", reinterpret_cast<uintptr_t>(&glDrawBuffers));
    Register("glReadBuffer", reinterpret_cast<uintptr_t>(&glReadBuffer));
    Register("glInvalidateFramebuffer", reinterpret_cast<uintptr_t>(&glInvalidateFramebuffer));
    Register("glFramebufferTextureLayer", reinterpret_cast<uintptr_t>(&glFramebufferTextureLayer));

    // Textures & Mipmaps
    Register("glTexSubImage2D", reinterpret_cast<uintptr_t>(&glTexSubImage2D));
    Register("glCopyTexImage2D", reinterpret_cast<uintptr_t>(&glCopyTexImage2D));
    Register("glCopyTexSubImage2D", reinterpret_cast<uintptr_t>(&glCopyTexSubImage2D));
    Register("glCompressedTexImage2D", reinterpret_cast<uintptr_t>(&glCompressedTexImage2D));
    Register("glCompressedTexSubImage2D", reinterpret_cast<uintptr_t>(&glCompressedTexSubImage2D));
    Register("glGenerateMipmap", reinterpret_cast<uintptr_t>(&glGenerateMipmap));
    Register("glTexParameterf", reinterpret_cast<uintptr_t>(&glTexParameterf));
    Register("glTexParameterfv", reinterpret_cast<uintptr_t>(&glTexParameterfv));
    Register("glTexParameteriv", reinterpret_cast<uintptr_t>(&glTexParameteriv));

    // Uniforms, Attributes & Program
    Register("glUniform2i", reinterpret_cast<uintptr_t>(&glUniform2i));
    Register("glUniform3i", reinterpret_cast<uintptr_t>(&glUniform3i));
    Register("glUniform4i", reinterpret_cast<uintptr_t>(&glUniform4i));
    Register("glUniform1iv", reinterpret_cast<uintptr_t>(&glUniform1iv));
    Register("glUniform2iv", reinterpret_cast<uintptr_t>(&glUniform2iv));
    Register("glUniform3iv", reinterpret_cast<uintptr_t>(&glUniform3iv));
    Register("glUniform4iv", reinterpret_cast<uintptr_t>(&glUniform4iv));
    Register("glUniform2f", reinterpret_cast<uintptr_t>(&glUniform2f));
    Register("glUniform3f", reinterpret_cast<uintptr_t>(&glUniform3f));
    Register("glUniform1fv", reinterpret_cast<uintptr_t>(&glUniform1fv));
    Register("glUniform2fv", reinterpret_cast<uintptr_t>(&glUniform2fv));
    Register("glUniform3fv", reinterpret_cast<uintptr_t>(&glUniform3fv));
    Register("glUniform4fv", reinterpret_cast<uintptr_t>(&glUniform4fv));
    Register("glUniform1uiv", reinterpret_cast<uintptr_t>(&glUniform1uiv));
    Register("glUniform2uiv", reinterpret_cast<uintptr_t>(&glUniform2uiv));
    Register("glUniform3uiv", reinterpret_cast<uintptr_t>(&glUniform3uiv));
    Register("glUniform4uiv", reinterpret_cast<uintptr_t>(&glUniform4uiv));
    Register("glUniformMatrix2fv", reinterpret_cast<uintptr_t>(&glUniformMatrix2fv));
    Register("glUniformMatrix3fv", reinterpret_cast<uintptr_t>(&glUniformMatrix3fv));
    Register("glVertexAttribIPointer", reinterpret_cast<uintptr_t>(&glVertexAttribIPointer));
    Register("glVertexAttrib1f", reinterpret_cast<uintptr_t>(&glVertexAttrib1f));
    Register("glVertexAttrib2f", reinterpret_cast<uintptr_t>(&glVertexAttrib2f));
    Register("glVertexAttrib3f", reinterpret_cast<uintptr_t>(&glVertexAttrib3f));
    Register("glVertexAttrib4f", reinterpret_cast<uintptr_t>(&glVertexAttrib4f));
    Register("glBindAttribLocation", reinterpret_cast<uintptr_t>(&glBindAttribLocation));
    Register("glGetActiveUniform", reinterpret_cast<uintptr_t>(&glGetActiveUniform));
    Register("glGetActiveAttrib", reinterpret_cast<uintptr_t>(&glGetActiveAttrib));
    Register("glDetachShader", reinterpret_cast<uintptr_t>(&glDetachShader));
    Register("glValidateProgram", reinterpret_cast<uintptr_t>(&glValidateProgram));
    Register("glIsShader", reinterpret_cast<uintptr_t>(&glIsShader));
    Register("glIsProgram", reinterpret_cast<uintptr_t>(&glIsProgram));
    Register("glIsBuffer", reinterpret_cast<uintptr_t>(&glIsBuffer));
    Register("glIsTexture", reinterpret_cast<uintptr_t>(&glIsTexture));
    Register("glIsFramebuffer", reinterpret_cast<uintptr_t>(&glIsFramebuffer));
    Register("glIsVertexArray", reinterpret_cast<uintptr_t>(&glIsVertexArray));
    Register("glIsSampler", reinterpret_cast<uintptr_t>(&glIsSampler));

    // Clear Buffers
    Register("glClearBufferfv", reinterpret_cast<uintptr_t>(&glClearBufferfv));
    Register("glClearBufferiv", reinterpret_cast<uintptr_t>(&glClearBufferiv));
    Register("glClearBufferuiv", reinterpret_cast<uintptr_t>(&glClearBufferuiv));
    Register("glClearBufferfi", reinterpret_cast<uintptr_t>(&glClearBufferfi));

    // State & Queries
    Register("glGetIntegerv", reinterpret_cast<uintptr_t>(&glGetIntegerv));
    Register("glGetInteger64v", reinterpret_cast<uintptr_t>(&glGetInteger64v));
    Register("glGetFloatv", reinterpret_cast<uintptr_t>(&glGetFloatv));
    Register("glGetBooleanv", reinterpret_cast<uintptr_t>(&glGetBooleanv));
    Register("glGetStringi", reinterpret_cast<uintptr_t>(&glGetStringi));
    Register("glGetTexParameteriv", reinterpret_cast<uintptr_t>(&glGetTexParameteriv));
    Register("glGetTexParameterfv", reinterpret_cast<uintptr_t>(&glGetTexParameterfv));
    Register("glGetFramebufferAttachmentParameteriv",
             reinterpret_cast<uintptr_t>(&glGetFramebufferAttachmentParameteriv));
    Register("glGetRenderbufferParameteriv",
             reinterpret_cast<uintptr_t>(&glGetRenderbufferParameteriv));
    Register("glGetBufferParameteriv", reinterpret_cast<uintptr_t>(&glGetBufferParameteriv));
    Register("glGetVertexAttribiv", reinterpret_cast<uintptr_t>(&glGetVertexAttribiv));
    Register("glIsEnabled", reinterpret_cast<uintptr_t>(&glIsEnabled));

    // AAudio
    Register("AAudio_createStreamBuilder",
             reinterpret_cast<uintptr_t>(&audio::AAudio_createStreamBuilder));
    Register("AAudioStreamBuilder_setSampleRate",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setSampleRate));
    Register("AAudioStreamBuilder_setChannelCount",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setChannelCount));
    Register("AAudioStreamBuilder_setFormat",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setFormat));
    Register("AAudioStreamBuilder_setDirection",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setDirection));
    Register("AAudioStreamBuilder_setDataCallback",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setDataCallback));
    Register("AAudioStreamBuilder_setErrorCallback",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setErrorCallback));
    Register("AAudioStreamBuilder_setFramesPerDataCallback",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setFramesPerDataCallback));
    Register("AAudioStreamBuilder_setBufferCapacityInFrames",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setBufferCapacityInFrames));
    Register("AAudioStreamBuilder_setPerformanceMode",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setPerformanceMode));
    Register("AAudioStreamBuilder_setSharingMode",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setSharingMode));
    Register("AAudioStreamBuilder_setUsage",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setUsage));
    Register("AAudioStreamBuilder_setContentType",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setContentType));
    Register("AAudioStreamBuilder_setInputPreset",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_setInputPreset));
    Register("AAudioStreamBuilder_openStream",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_openStream));
    Register("AAudioStreamBuilder_delete",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_delete));
    Register("AAudioStream_requestStart",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestStart));
    Register("AAudioStream_requestPause",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestPause));
    Register("AAudioStream_requestFlush",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestFlush));
    Register("AAudioStream_requestStop",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestStop));
    Register("AAudioStream_close", reinterpret_cast<uintptr_t>(&audio::AAudioStream_close));
    Register("AAudioStream_waitForStateChange",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_waitForStateChange));
    Register("AAudioStream_write", reinterpret_cast<uintptr_t>(&audio::AAudioStream_write));
    Register("AAudioStream_read", reinterpret_cast<uintptr_t>(&audio::AAudioStream_read));
    Register("AAudioStream_getSampleRate",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getSampleRate));
    Register("AAudioStream_getChannelCount",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getChannelCount));
    Register("AAudioStream_getFormat", reinterpret_cast<uintptr_t>(&audio::AAudioStream_getFormat));
    Register("AAudioStream_getState", reinterpret_cast<uintptr_t>(&audio::AAudioStream_getState));
    Register("AAudioStream_getFramesPerDataCallback",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getFramesPerDataCallback));
    Register("AAudioStream_getBufferSizeInFrames",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getBufferSizeInFrames));
    Register("AAudioStream_getFramesWritten",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getFramesWritten));
    Register("AAudioStream_getFramesRead",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getFramesRead));
    Register("AAudioStream_getTimestamp",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getTimestamp));
    Register("AAudioStream_getXRunCount",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getXRunCount));
    Register("AAudioStream_getPerformanceMode",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getPerformanceMode));
    Register("AAudioStream_getSharingMode",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getSharingMode));
    Register("AAudioStream_getDeviceId",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_getDeviceId));

    // OpenSL ES
    Register("slCreateEngine", reinterpret_cast<uintptr_t>(&audio::slCreateEngine));
    Register("SL_IID_ENGINE", reinterpret_cast<uintptr_t>(&audio::SL_IID_ENGINE));
    Register("SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
             reinterpret_cast<uintptr_t>(&audio::SL_IID_ANDROIDSIMPLEBUFFERQUEUE));
    Register("SL_IID_PLAY", reinterpret_cast<uintptr_t>(&audio::SL_IID_PLAY));
    Register("SL_IID_VOLUME", reinterpret_cast<uintptr_t>(&audio::SL_IID_VOLUME));
    Register("SL_IID_BUFFERQUEUE", reinterpret_cast<uintptr_t>(&audio::SL_IID_BUFFERQUEUE));
    Register("SL_IID_RECORD", reinterpret_cast<uintptr_t>(&audio::SL_IID_RECORD));
    Register("SL_IID_MUTESOLO", reinterpret_cast<uintptr_t>(&audio::SL_IID_MUTESOLO));
    Register("SL_IID_ANDROIDCONFIGURATION",
             reinterpret_cast<uintptr_t>(&audio::SL_IID_ANDROIDCONFIGURATION));

    // Swappy Frame Pacing (SwappyVk & SwappyGL)
    Register("SwappyVk_initAndGetRefreshCycleDuration",
             reinterpret_cast<uintptr_t>(&SwappyVk_initAndGetRefreshCycleDuration));
    Register("SwappyVk_setWindow", reinterpret_cast<uintptr_t>(&SwappyVk_setWindow));
    Register("SwappyVk_queuePresent", reinterpret_cast<uintptr_t>(&SwappyVk_queuePresent));
    Register("SwappyVk_destroySwapchain", reinterpret_cast<uintptr_t>(&SwappyVk_destroySwapchain));
    Register("SwappyVk_destroyDevice", reinterpret_cast<uintptr_t>(&SwappyVk_destroyDevice));
    Register("SwappyVk_setTargetFrameRateHz",
             reinterpret_cast<uintptr_t>(&SwappyVk_setTargetFrameRateHz));
    Register("SwappyVk_getSwapIntervalNS",
             reinterpret_cast<uintptr_t>(&SwappyVk_getSwapIntervalNS));
    Register("SwappyVk_setAutoSwapInterval",
             reinterpret_cast<uintptr_t>(&SwappyVk_setAutoSwapInterval));

    Register("SwappyGL_init", reinterpret_cast<uintptr_t>(&SwappyGL_init));
    Register("SwappyGL_setWindow", reinterpret_cast<uintptr_t>(&SwappyGL_setWindow));
    Register("SwappyGL_swap", reinterpret_cast<uintptr_t>(&SwappyGL_swap));
    Register("SwappyGL_destroy", reinterpret_cast<uintptr_t>(&SwappyGL_destroy));
    Register("SwappyGL_setTargetFrameRateHz",
             reinterpret_cast<uintptr_t>(&SwappyGL_setTargetFrameRateHz));
    Register("SwappyGL_getSwapIntervalNS",
             reinterpret_cast<uintptr_t>(&SwappyGL_getSwapIntervalNS));
    Register("SwappyGL_setAutoSwapInterval",
             reinterpret_cast<uintptr_t>(&SwappyGL_setAutoSwapInterval));
    Register("SwappyGL_isEnabled", reinterpret_cast<uintptr_t>(&SwappyGL_isEnabled));

    // Camera2 NDK
    Register("ACameraManager_create", reinterpret_cast<uintptr_t>(&ACameraManager_create));
    Register("ACameraManager_delete", reinterpret_cast<uintptr_t>(&ACameraManager_delete));
    Register("ACameraManager_getCameraIdList",
             reinterpret_cast<uintptr_t>(&ACameraManager_getCameraIdList));
    Register("ACameraManager_deleteCameraIdList",
             reinterpret_cast<uintptr_t>(&ACameraManager_deleteCameraIdList));
    Register("ACameraManager_getCameraCharacteristics",
             reinterpret_cast<uintptr_t>(&ACameraManager_getCameraCharacteristics));
    Register("ACameraManager_openCamera", reinterpret_cast<uintptr_t>(&ACameraManager_openCamera));
    Register("ACameraManager_registerAvailabilityCallback",
             reinterpret_cast<uintptr_t>(&ACameraManager_registerAvailabilityCallback));
    Register("ACameraManager_unregisterAvailabilityCallback",
             reinterpret_cast<uintptr_t>(&ACameraManager_unregisterAvailabilityCallback));
    Register("ACameraDevice_close", reinterpret_cast<uintptr_t>(&ACameraDevice_close));
    Register("ACameraDevice_getId", reinterpret_cast<uintptr_t>(&ACameraDevice_getId));
    Register("ACameraDevice_createCaptureRequest",
             reinterpret_cast<uintptr_t>(&ACameraDevice_createCaptureRequest));
    Register("ACameraDevice_createCaptureSession",
             reinterpret_cast<uintptr_t>(&ACameraDevice_createCaptureSession));
    Register("ACameraDevice_createCaptureSessionWithSessionParameters",
             reinterpret_cast<uintptr_t>(&ACameraDevice_createCaptureSessionWithSessionParameters));
    Register("ACaptureRequest_free", reinterpret_cast<uintptr_t>(&ACaptureRequest_free));
    Register("ACaptureRequest_addTarget", reinterpret_cast<uintptr_t>(&ACaptureRequest_addTarget));
    Register("ACaptureRequest_removeTarget",
             reinterpret_cast<uintptr_t>(&ACaptureRequest_removeTarget));
    Register("ACaptureRequest_getAllOptions",
             reinterpret_cast<uintptr_t>(&ACaptureRequest_getAllOptions));
    Register("ACaptureRequest_setEntry_u8",
             reinterpret_cast<uintptr_t>(&ACaptureRequest_setEntry_u8));
    Register("ACaptureRequest_setEntry_i32",
             reinterpret_cast<uintptr_t>(&ACaptureRequest_setEntry_i32));
    Register("ACaptureRequest_setEntry_float",
             reinterpret_cast<uintptr_t>(&ACaptureRequest_setEntry_float));
    Register("ACameraCaptureSession_close",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_close));
    Register("ACameraCaptureSession_getDevice",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_getDevice));
    Register("ACameraCaptureSession_setRepeatingRequest",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_setRepeatingRequest));
    Register("ACameraCaptureSession_capture",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_capture));
    Register("ACameraCaptureSession_stopRepeating",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_stopRepeating));
    Register("ACameraCaptureSession_abortCaptures",
             reinterpret_cast<uintptr_t>(&ACameraCaptureSession_abortCaptures));
    Register("ACameraOutputTarget_create",
             reinterpret_cast<uintptr_t>(&ACameraOutputTarget_create));
    Register("ACameraOutputTarget_free", reinterpret_cast<uintptr_t>(&ACameraOutputTarget_free));
    Register("ACaptureSessionOutput_create",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutput_create));
    Register("ACaptureSessionOutput_free",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutput_free));
    Register("ACaptureSessionOutputContainer_create",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutputContainer_create));
    Register("ACaptureSessionOutputContainer_free",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutputContainer_free));
    Register("ACaptureSessionOutputContainer_add",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutputContainer_add));
    Register("ACaptureSessionOutputContainer_remove",
             reinterpret_cast<uintptr_t>(&ACaptureSessionOutputContainer_remove));
    Register("ACameraMetadata_free", reinterpret_cast<uintptr_t>(&ACameraMetadata_free));
    Register("ACameraMetadata_getConstEntry",
             reinterpret_cast<uintptr_t>(&ACameraMetadata_getConstEntry));

    // Media NDK (ImageReader & Image)
    Register("AImageReader_new", reinterpret_cast<uintptr_t>(&AImageReader_new));
    Register("AImageReader_newWithUsage", reinterpret_cast<uintptr_t>(&AImageReader_newWithUsage));
    Register("AImageReader_delete", reinterpret_cast<uintptr_t>(&AImageReader_delete));
    Register("AImageReader_getWindow", reinterpret_cast<uintptr_t>(&AImageReader_getWindow));
    Register("AImageReader_acquireNextImage",
             reinterpret_cast<uintptr_t>(&AImageReader_acquireNextImage));
    Register("AImageReader_acquireLatestImage",
             reinterpret_cast<uintptr_t>(&AImageReader_acquireLatestImage));
    Register("AImageReader_setImageListener",
             reinterpret_cast<uintptr_t>(&AImageReader_setImageListener));
    Register("AImage_delete", reinterpret_cast<uintptr_t>(&AImage_delete));
    Register("AImage_getWidth", reinterpret_cast<uintptr_t>(&AImage_getWidth));
    Register("AImage_getHeight", reinterpret_cast<uintptr_t>(&AImage_getHeight));
    Register("AImage_getFormat", reinterpret_cast<uintptr_t>(&AImage_getFormat));
    Register("AImage_getTimestamp", reinterpret_cast<uintptr_t>(&AImage_getTimestamp));
    Register("AImage_getNumberOfPlanes", reinterpret_cast<uintptr_t>(&AImage_getNumberOfPlanes));
    Register("AImage_getPlanePixelStride",
             reinterpret_cast<uintptr_t>(&AImage_getPlanePixelStride));
    Register("AImage_getPlaneRowStride", reinterpret_cast<uintptr_t>(&AImage_getPlaneRowStride));
    Register("AImage_getPlaneData", reinterpret_cast<uintptr_t>(&AImage_getPlaneData));

    // KeyStore NDK
    Register("AKeyStore_create", reinterpret_cast<uintptr_t>(&AKeyStore_create));
    Register("AKeyStore_delete", reinterpret_cast<uintptr_t>(&AKeyStore_delete));
    Register("AKeyStore_containsAlias", reinterpret_cast<uintptr_t>(&AKeyStore_containsAlias));
    Register("AKeyStore_deleteKey", reinterpret_cast<uintptr_t>(&AKeyStore_deleteKey));
    Register("AKeyStore_listAliases", reinterpret_cast<uintptr_t>(&AKeyStore_listAliases));
    Register("AKeyStore_deleteAliasList", reinterpret_cast<uintptr_t>(&AKeyStore_deleteAliasList));
    Register("AKeyStore_generateAesKey", reinterpret_cast<uintptr_t>(&AKeyStore_generateAesKey));
    Register("AKeyStore_generateRsaKeyPair",
             reinterpret_cast<uintptr_t>(&AKeyStore_generateRsaKeyPair));
    Register("AKeyStore_generateEcKeyPair",
             reinterpret_cast<uintptr_t>(&AKeyStore_generateEcKeyPair));
    Register("AKeyStore_encrypt", reinterpret_cast<uintptr_t>(&AKeyStore_encrypt));
    Register("AKeyStore_decrypt", reinterpret_cast<uintptr_t>(&AKeyStore_decrypt));
    Register("AKeyStore_sign", reinterpret_cast<uintptr_t>(&AKeyStore_sign));
    Register("AKeyStore_verify", reinterpret_cast<uintptr_t>(&AKeyStore_verify));
    Register("AKeyStore_freeBuffer", reinterpret_cast<uintptr_t>(&AKeyStore_freeBuffer));

    // Location NDK
    Register("ALocationManager_create", reinterpret_cast<uintptr_t>(&ALocationManager_create));
    Register("ALocationManager_destroy", reinterpret_cast<uintptr_t>(&ALocationManager_destroy));
    Register("ALocationManager_isProviderEnabled",
             reinterpret_cast<uintptr_t>(&ALocationManager_isProviderEnabled));
    Register("ALocationManager_getLastKnownLocation",
             reinterpret_cast<uintptr_t>(&ALocationManager_getLastKnownLocation));
    Register("ALocationManager_requestLocationUpdates",
             reinterpret_cast<uintptr_t>(&ALocationManager_requestLocationUpdates));
    Register("ALocationManager_removeUpdates",
             reinterpret_cast<uintptr_t>(&ALocationManager_removeUpdates));
    Register("ALocation_getLatitude", reinterpret_cast<uintptr_t>(&ALocation_getLatitude));
    Register("ALocation_getLongitude", reinterpret_cast<uintptr_t>(&ALocation_getLongitude));
    Register("ALocation_getAltitude", reinterpret_cast<uintptr_t>(&ALocation_getAltitude));
    Register("ALocation_getAccuracy", reinterpret_cast<uintptr_t>(&ALocation_getAccuracy));
    Register("ALocation_getSpeed", reinterpret_cast<uintptr_t>(&ALocation_getSpeed));
    Register("ALocation_getBearing", reinterpret_cast<uintptr_t>(&ALocation_getBearing));
    Register("ALocation_getTime", reinterpret_cast<uintptr_t>(&ALocation_getTime));
    Register("ALocation_getElapsedRealtimeNanos",
             reinterpret_cast<uintptr_t>(&ALocation_getElapsedRealtimeNanos));
    Register("ALocation_hasAltitude", reinterpret_cast<uintptr_t>(&ALocation_hasAltitude));
    Register("ALocation_hasSpeed", reinterpret_cast<uintptr_t>(&ALocation_hasSpeed));
    Register("ALocation_hasBearing", reinterpret_cast<uintptr_t>(&ALocation_hasBearing));
    Register("ALocation_hasAccuracy", reinterpret_cast<uintptr_t>(&ALocation_hasAccuracy));
    Register("ALocation_destroy", reinterpret_cast<uintptr_t>(&ALocation_destroy));
    Register("AGeofenceManager_addCircularGeofence",
             reinterpret_cast<uintptr_t>(&AGeofenceManager_addCircularGeofence));
    Register("AGeofenceManager_removeGeofence",
             reinterpret_cast<uintptr_t>(&AGeofenceManager_removeGeofence));

    // Clipboard & DragDrop NDK
    Register("AClipboard_create", reinterpret_cast<uintptr_t>(&AClipboard_create));
    Register("AClipboard_destroy", reinterpret_cast<uintptr_t>(&AClipboard_destroy));
    Register("AClipboard_hasText", reinterpret_cast<uintptr_t>(&AClipboard_hasText));
    Register("AClipboard_getText", reinterpret_cast<uintptr_t>(&AClipboard_getText));
    Register("AClipboard_setText", reinterpret_cast<uintptr_t>(&AClipboard_setText));
    Register("AClipboard_hasPrimaryClip", reinterpret_cast<uintptr_t>(&AClipboard_hasPrimaryClip));
    Register("AClipboard_clear", reinterpret_cast<uintptr_t>(&AClipboard_clear));
    Register("AClipboard_addPrimaryClipChangedListener",
             reinterpret_cast<uintptr_t>(&AClipboard_addPrimaryClipChangedListener));
    Register("AClipboard_removePrimaryClipChangedListener",
             reinterpret_cast<uintptr_t>(&AClipboard_removePrimaryClipChangedListener));
    Register("ADragDrop_registerDropListener",
             reinterpret_cast<uintptr_t>(&ADragDrop_registerDropListener));
    Register("ADragDrop_unregisterDropListener",
             reinterpret_cast<uintptr_t>(&ADragDrop_unregisterDropListener));

    // Power & Battery NDK
    Register("APowerManager_create", reinterpret_cast<uintptr_t>(&APowerManager_create));
    Register("APowerManager_destroy", reinterpret_cast<uintptr_t>(&APowerManager_destroy));
    Register("APowerManager_isInteractive",
             reinterpret_cast<uintptr_t>(&APowerManager_isInteractive));
    Register("APowerManager_isPowerSaveMode",
             reinterpret_cast<uintptr_t>(&APowerManager_isPowerSaveMode));
    Register("AWakeLock_create", reinterpret_cast<uintptr_t>(&AWakeLock_create));
    Register("AWakeLock_acquire", reinterpret_cast<uintptr_t>(&AWakeLock_acquire));
    Register("AWakeLock_release", reinterpret_cast<uintptr_t>(&AWakeLock_release));
    Register("AWakeLock_isHeld", reinterpret_cast<uintptr_t>(&AWakeLock_isHeld));
    Register("AWakeLock_destroy", reinterpret_cast<uintptr_t>(&AWakeLock_destroy));
    Register("ABatteryManager_create", reinterpret_cast<uintptr_t>(&ABatteryManager_create));
    Register("ABatteryManager_destroy", reinterpret_cast<uintptr_t>(&ABatteryManager_destroy));
    Register("ABatteryManager_getCapacity",
             reinterpret_cast<uintptr_t>(&ABatteryManager_getCapacity));
    Register("ABatteryManager_isCharging",
             reinterpret_cast<uintptr_t>(&ABatteryManager_isCharging));
    Register("ABatteryManager_getStatus", reinterpret_cast<uintptr_t>(&ABatteryManager_getStatus));
    Register("ABatteryManager_getHealth", reinterpret_cast<uintptr_t>(&ABatteryManager_getHealth));
    Register("ABatteryManager_getPluggedType",
             reinterpret_cast<uintptr_t>(&ABatteryManager_getPluggedType));
    Register("ABatteryManager_getVoltage",
             reinterpret_cast<uintptr_t>(&ABatteryManager_getVoltage));
    Register("ABatteryManager_getTemperature",
             reinterpret_cast<uintptr_t>(&ABatteryManager_getTemperature));

    // Notification & Toast NDK
    Register("ANotificationManager_create",
             reinterpret_cast<uintptr_t>(&ANotificationManager_create));
    Register("ANotificationManager_destroy",
             reinterpret_cast<uintptr_t>(&ANotificationManager_destroy));
    Register("ANotificationChannel_create",
             reinterpret_cast<uintptr_t>(&ANotificationChannel_create));
    Register("ANotificationChannel_destroy",
             reinterpret_cast<uintptr_t>(&ANotificationChannel_destroy));
    Register("ANotificationChannel_setDescription",
             reinterpret_cast<uintptr_t>(&ANotificationChannel_setDescription));
    Register("ANotificationChannel_getImportance",
             reinterpret_cast<uintptr_t>(&ANotificationChannel_getImportance));
    Register("ANotificationManager_createNotificationChannel",
             reinterpret_cast<uintptr_t>(&ANotificationManager_createNotificationChannel));
    Register("ANotificationManager_getNotificationChannel",
             reinterpret_cast<uintptr_t>(&ANotificationManager_getNotificationChannel));
    Register("ANotificationManager_deleteNotificationChannel",
             reinterpret_cast<uintptr_t>(&ANotificationManager_deleteNotificationChannel));
    Register("ANotification_create", reinterpret_cast<uintptr_t>(&ANotification_create));
    Register("ANotification_destroy", reinterpret_cast<uintptr_t>(&ANotification_destroy));
    Register("ANotification_setTitle", reinterpret_cast<uintptr_t>(&ANotification_setTitle));
    Register("ANotification_setText", reinterpret_cast<uintptr_t>(&ANotification_setText));
    Register("ANotification_setProgress", reinterpret_cast<uintptr_t>(&ANotification_setProgress));
    Register("ANotification_setOngoing", reinterpret_cast<uintptr_t>(&ANotification_setOngoing));
    Register("ANotificationManager_notify",
             reinterpret_cast<uintptr_t>(&ANotificationManager_notify));
    Register("ANotificationManager_cancel",
             reinterpret_cast<uintptr_t>(&ANotificationManager_cancel));
    Register("ANotificationManager_cancelAll",
             reinterpret_cast<uintptr_t>(&ANotificationManager_cancelAll));
    Register("AToast_show", reinterpret_cast<uintptr_t>(&AToast_show));

    // libdl & Bionic runtime symbols
    Register("dlopen", reinterpret_cast<uintptr_t>(&dlopen));
    Register("__loader_dlopen", reinterpret_cast<uintptr_t>(&dlopen));
    Register("android_dlopen_ext", reinterpret_cast<uintptr_t>(&android_dlopen_ext));
    Register("__loader_android_dlopen_ext", reinterpret_cast<uintptr_t>(&android_dlopen_ext));
    Register("dlsym", reinterpret_cast<uintptr_t>(&dlsym));
    Register("__loader_dlsym", reinterpret_cast<uintptr_t>(&dlsym));
    Register("dlvsym", reinterpret_cast<uintptr_t>(&dlvsym));
    Register("__loader_dlvsym", reinterpret_cast<uintptr_t>(&dlvsym));
    Register("dlclose", reinterpret_cast<uintptr_t>(&dlclose));
    Register("__loader_dlclose", reinterpret_cast<uintptr_t>(&dlclose));
    Register("dlerror", reinterpret_cast<uintptr_t>(&dlerror));
    Register("__loader_dlerror", reinterpret_cast<uintptr_t>(&dlerror));
    Register("dladdr", reinterpret_cast<uintptr_t>(&dladdr));
    Register("__loader_dladdr", reinterpret_cast<uintptr_t>(&dladdr));
    Register("dlinfo", reinterpret_cast<uintptr_t>(&dlinfo));
    Register("__loader_dlinfo", reinterpret_cast<uintptr_t>(&dlinfo));
    Register("dl_iterate_phdr", reinterpret_cast<uintptr_t>(&dl_iterate_phdr));
    Register("__loader_dl_iterate_phdr", reinterpret_cast<uintptr_t>(&dl_iterate_phdr));
    Register("__tls_get_addr", reinterpret_cast<uintptr_t>(&__tls_get_addr));
    Register("__cxa_atexit", reinterpret_cast<uintptr_t>(&portage_cxa_atexit));
    Register("__cxa_finalize", reinterpret_cast<uintptr_t>(&portage_cxa_finalize));
    Register("__cxa_thread_atexit_impl",
             reinterpret_cast<uintptr_t>(&portage_cxa_thread_atexit_impl));
    Register("atexit", reinterpret_cast<uintptr_t>(&portage_guest_atexit));
    Register("android_get_device_api_level",
             reinterpret_cast<uintptr_t>(&android_get_device_api_level));
    Register("android_get_application_target_sdk_version",
             reinterpret_cast<uintptr_t>(&android_get_application_target_sdk_version));

    // Bionic pthread introspection & attribute APIs
    Register("pthread_self", reinterpret_cast<uintptr_t>(&portage_pthread_self));
    Register("pthread_equal", reinterpret_cast<uintptr_t>(&portage_pthread_equal));
    Register("pthread_setname_np", reinterpret_cast<uintptr_t>(&portage_pthread_setname_np));
    Register("pthread_getname_np", reinterpret_cast<uintptr_t>(&portage_pthread_getname_np));
    Register("pthread_getattr_np", reinterpret_cast<uintptr_t>(&portage_pthread_getattr_np));
    Register("pthread_attr_init", reinterpret_cast<uintptr_t>(&portage_pthread_attr_init));
    Register("pthread_attr_destroy", reinterpret_cast<uintptr_t>(&portage_pthread_attr_destroy));
    Register("pthread_attr_getstack", reinterpret_cast<uintptr_t>(&portage_pthread_attr_getstack));
    Register("pthread_attr_setstack", reinterpret_cast<uintptr_t>(&portage_pthread_attr_setstack));
    Register("pthread_attr_getstacksize",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_getstacksize));
    Register("pthread_attr_setstacksize",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_setstacksize));
    Register("pthread_attr_getguardsize",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_getguardsize));
    Register("pthread_attr_setguardsize",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_setguardsize));
    Register("pthread_attr_getdetachstate",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_getdetachstate));
    Register("pthread_attr_setdetachstate",
             reinterpret_cast<uintptr_t>(&portage_pthread_attr_setdetachstate));
    RegisterBionicAndRuntimeThunks(*this);

    // ATrace
    Register("ATrace_isEnabled", reinterpret_cast<uintptr_t>(&ATrace_isEnabled));
    Register("ATrace_beginSection", reinterpret_cast<uintptr_t>(&ATrace_beginSection));
    Register("ATrace_endSection", reinterpret_cast<uintptr_t>(&ATrace_endSection));
    Register("ATrace_beginAsyncSection", reinterpret_cast<uintptr_t>(&ATrace_beginAsyncSection));
    Register("ATrace_endAsyncSection", reinterpret_cast<uintptr_t>(&ATrace_endAsyncSection));
    Register("ATrace_setCounter", reinterpret_cast<uintptr_t>(&ATrace_setCounter));

    // libjnigraphics
    Register("AndroidBitmap_getInfo", reinterpret_cast<uintptr_t>(&AndroidBitmap_getInfo));
    Register("AndroidBitmap_lockPixels", reinterpret_cast<uintptr_t>(&AndroidBitmap_lockPixels));
    Register("AndroidBitmap_unlockPixels",
             reinterpret_cast<uintptr_t>(&AndroidBitmap_unlockPixels));
    Register("AndroidBitmap_getData", reinterpret_cast<uintptr_t>(&AndroidBitmap_getData));

    // AImageDecoder (libjnigraphics API 30+)
    Register("AImageDecoder_createFromAAsset",
             reinterpret_cast<uintptr_t>(&AImageDecoder_createFromAAsset));
    Register("AImageDecoder_createFromBuffer",
             reinterpret_cast<uintptr_t>(&AImageDecoder_createFromBuffer));
    Register("AImageDecoder_createFromFd",
             reinterpret_cast<uintptr_t>(&AImageDecoder_createFromFd));
    Register("AImageDecoder_delete", reinterpret_cast<uintptr_t>(&AImageDecoder_delete));
    Register("AImageDecoder_getHeaderInfo",
             reinterpret_cast<uintptr_t>(&AImageDecoder_getHeaderInfo));
    Register("AImageDecoderHeaderInfo_getWidth",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getWidth));
    Register("AImageDecoderHeaderInfo_getHeight",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getHeight));
    Register("AImageDecoderHeaderInfo_getMimeType",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getMimeType));
    Register("AImageDecoderHeaderInfo_getDataSpace",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getDataSpace));
    Register("AImageDecoderHeaderInfo_getAndroidBitmapFormat",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getAndroidBitmapFormat));
    Register("AImageDecoderHeaderInfo_getAlphaFlags",
             reinterpret_cast<uintptr_t>(&AImageDecoderHeaderInfo_getAlphaFlags));
    Register("AImageDecoder_setAndroidBitmapFormat",
             reinterpret_cast<uintptr_t>(&AImageDecoder_setAndroidBitmapFormat));
    Register("AImageDecoder_setUnpremultipliedRequired",
             reinterpret_cast<uintptr_t>(&AImageDecoder_setUnpremultipliedRequired));
    Register("AImageDecoder_setDataSpace",
             reinterpret_cast<uintptr_t>(&AImageDecoder_setDataSpace));
    Register("AImageDecoder_setTargetSize",
             reinterpret_cast<uintptr_t>(&AImageDecoder_setTargetSize));
    Register("AImageDecoder_setCrop", reinterpret_cast<uintptr_t>(&AImageDecoder_setCrop));
    Register("AImageDecoder_computeSampledSize",
             reinterpret_cast<uintptr_t>(&AImageDecoder_computeSampledSize));
    Register("AImageDecoder_getMinimumStride",
             reinterpret_cast<uintptr_t>(&AImageDecoder_getMinimumStride));
    Register("AImageDecoder_decodeImage", reinterpret_cast<uintptr_t>(&AImageDecoder_decodeImage));
    Register("AImageDecoder_resultToString",
             reinterpret_cast<uintptr_t>(&AImageDecoder_resultToString));
    Register("AImageDecoder_isAnimated", reinterpret_cast<uintptr_t>(&AImageDecoder_isAnimated));
    Register("AImageDecoder_setRepeatCount",
             reinterpret_cast<uintptr_t>(&AImageDecoder_setRepeatCount));
    Register("AImageDecoder_getRepeatCount",
             reinterpret_cast<uintptr_t>(&AImageDecoder_getRepeatCount));
    Register("AImageDecoderFrameInfo_create",
             reinterpret_cast<uintptr_t>(&AImageDecoderFrameInfo_create));
    Register("AImageDecoderFrameInfo_delete",
             reinterpret_cast<uintptr_t>(&AImageDecoderFrameInfo_delete));
    Register("AImageDecoder_getFrameInfo",
             reinterpret_cast<uintptr_t>(&AImageDecoder_getFrameInfo));
    Register("AImageDecoderFrameInfo_getDuration",
             reinterpret_cast<uintptr_t>(&AImageDecoderFrameInfo_getDuration));
    Register("AImageDecoderFrameInfo_getFrameRect",
             reinterpret_cast<uintptr_t>(&AImageDecoderFrameInfo_getFrameRect));
    Register("AImageDecoder_advanceFrame",
             reinterpret_cast<uintptr_t>(&AImageDecoder_advanceFrame));
    Register("AImageDecoder_rewind", reinterpret_cast<uintptr_t>(&AImageDecoder_rewind));

    // ASharedMemory
    Register("ASharedMemory_create", reinterpret_cast<uintptr_t>(&ASharedMemory_create));
    Register("ASharedMemory_getSize", reinterpret_cast<uintptr_t>(&ASharedMemory_getSize));
    Register("ASharedMemory_setProt", reinterpret_cast<uintptr_t>(&ASharedMemory_setProt));
    Register("ASharedMemory_dup", reinterpret_cast<uintptr_t>(&ASharedMemory_dup));

    // AHardwareBuffer
    Register("AHardwareBuffer_allocate", reinterpret_cast<uintptr_t>(&AHardwareBuffer_allocate));
    Register("AHardwareBuffer_acquire", reinterpret_cast<uintptr_t>(&AHardwareBuffer_acquire));
    Register("AHardwareBuffer_release", reinterpret_cast<uintptr_t>(&AHardwareBuffer_release));
    Register("AHardwareBuffer_describe", reinterpret_cast<uintptr_t>(&AHardwareBuffer_describe));
    Register("AHardwareBuffer_lock", reinterpret_cast<uintptr_t>(&AHardwareBuffer_lock));
    Register("AHardwareBuffer_unlock", reinterpret_cast<uintptr_t>(&AHardwareBuffer_unlock));
    Register("AHardwareBuffer_lockPlanes",
             reinterpret_cast<uintptr_t>(&AHardwareBuffer_lockPlanes));
    Register("AHardwareBuffer_getId", reinterpret_cast<uintptr_t>(&AHardwareBuffer_getId));
    Register("AHardwareBuffer_isSupported",
             reinterpret_cast<uintptr_t>(&AHardwareBuffer_isSupported));
    Register("AHardwareBuffer_lockAndGetInfo",
             reinterpret_cast<uintptr_t>(&AHardwareBuffer_lockAndGetInfo));
    Register("AHardwareBuffer_sendHandleToUnixSocket",
             reinterpret_cast<uintptr_t>(&AHardwareBuffer_sendHandleToUnixSocket));
    Register("AHardwareBuffer_recvHandleFromUnixSocket",
             reinterpret_cast<uintptr_t>(&AHardwareBuffer_recvHandleFromUnixSocket));

    // AMediaFormat
    Register("AMediaFormat_new", reinterpret_cast<uintptr_t>(&AMediaFormat_new));
    Register("AMediaFormat_delete", reinterpret_cast<uintptr_t>(&AMediaFormat_delete));
    Register("AMediaFormat_getString", reinterpret_cast<uintptr_t>(&AMediaFormat_getString));
    Register("AMediaFormat_getInt32", reinterpret_cast<uintptr_t>(&AMediaFormat_getInt32));
    Register("AMediaFormat_getInt64", reinterpret_cast<uintptr_t>(&AMediaFormat_getInt64));
    Register("AMediaFormat_getFloat", reinterpret_cast<uintptr_t>(&AMediaFormat_getFloat));
    Register("AMediaFormat_getSize", reinterpret_cast<uintptr_t>(&AMediaFormat_getSize));
    Register("AMediaFormat_getBuffer", reinterpret_cast<uintptr_t>(&AMediaFormat_getBuffer));
    Register("AMediaFormat_setString", reinterpret_cast<uintptr_t>(&AMediaFormat_setString));
    Register("AMediaFormat_setInt32", reinterpret_cast<uintptr_t>(&AMediaFormat_setInt32));
    Register("AMediaFormat_setInt64", reinterpret_cast<uintptr_t>(&AMediaFormat_setInt64));
    Register("AMediaFormat_setFloat", reinterpret_cast<uintptr_t>(&AMediaFormat_setFloat));
    Register("AMediaFormat_setSize", reinterpret_cast<uintptr_t>(&AMediaFormat_setSize));
    Register("AMediaFormat_setBuffer", reinterpret_cast<uintptr_t>(&AMediaFormat_setBuffer));
    Register("AMediaFormat_toString", reinterpret_cast<uintptr_t>(&AMediaFormat_toString));
    Register("AMediaFormat_copy", reinterpret_cast<uintptr_t>(&AMediaFormat_copy));
    Register("AMediaFormat_clear", reinterpret_cast<uintptr_t>(&AMediaFormat_clear));

    // AMediaExtractor
    Register("AMediaExtractor_new", reinterpret_cast<uintptr_t>(&AMediaExtractor_new));
    Register("AMediaExtractor_delete", reinterpret_cast<uintptr_t>(&AMediaExtractor_delete));
    Register("AMediaExtractor_setDataSourceFd",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_setDataSourceFd));
    Register("AMediaExtractor_setDataSource",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_setDataSource));
    Register("AMediaExtractor_getTrackCount",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getTrackCount));
    Register("AMediaExtractor_getTrackFormat",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getTrackFormat));
    Register("AMediaExtractor_selectTrack",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_selectTrack));
    Register("AMediaExtractor_unselectTrack",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_unselectTrack));
    Register("AMediaExtractor_readSampleData",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_readSampleData));
    Register("AMediaExtractor_getSampleSize",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getSampleSize));
    Register("AMediaExtractor_getSampleTrackIndex",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getSampleTrackIndex));
    Register("AMediaExtractor_getSampleTime",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getSampleTime));
    Register("AMediaExtractor_getSampleFlags",
             reinterpret_cast<uintptr_t>(&AMediaExtractor_getSampleFlags));
    Register("AMediaExtractor_advance", reinterpret_cast<uintptr_t>(&AMediaExtractor_advance));
    Register("AMediaExtractor_seekTo", reinterpret_cast<uintptr_t>(&AMediaExtractor_seekTo));

    // AMediaCodec
    Register("AMediaCodec_createCodecByName",
             reinterpret_cast<uintptr_t>(&AMediaCodec_createCodecByName));
    Register("AMediaCodec_createDecoderByType",
             reinterpret_cast<uintptr_t>(&AMediaCodec_createDecoderByType));
    Register("AMediaCodec_createEncoderByType",
             reinterpret_cast<uintptr_t>(&AMediaCodec_createEncoderByType));
    Register("AMediaCodec_delete", reinterpret_cast<uintptr_t>(&AMediaCodec_delete));
    Register("AMediaCodec_configure", reinterpret_cast<uintptr_t>(&AMediaCodec_configure));
    Register("AMediaCodec_start", reinterpret_cast<uintptr_t>(&AMediaCodec_start));
    Register("AMediaCodec_stop", reinterpret_cast<uintptr_t>(&AMediaCodec_stop));
    Register("AMediaCodec_flush", reinterpret_cast<uintptr_t>(&AMediaCodec_flush));
    Register("AMediaCodec_dequeueInputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_dequeueInputBuffer));
    Register("AMediaCodec_getInputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_getInputBuffer));
    Register("AMediaCodec_queueInputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_queueInputBuffer));
    Register("AMediaCodec_dequeueOutputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_dequeueOutputBuffer));
    Register("AMediaCodec_getOutputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_getOutputBuffer));
    Register("AMediaCodec_getOutputFormat",
             reinterpret_cast<uintptr_t>(&AMediaCodec_getOutputFormat));
    Register("AMediaCodec_releaseOutputBuffer",
             reinterpret_cast<uintptr_t>(&AMediaCodec_releaseOutputBuffer));
    Register("AMediaCodec_releaseOutputBufferAtTime",
             reinterpret_cast<uintptr_t>(&AMediaCodec_releaseOutputBufferAtTime));
    Register("AMediaCodec_getName", reinterpret_cast<uintptr_t>(&AMediaCodec_getName));
    Register("AMediaCodec_releaseName", reinterpret_cast<uintptr_t>(&AMediaCodec_releaseName));
    Register("AMediaCodec_getInputFormat",
             reinterpret_cast<uintptr_t>(&AMediaCodec_getInputFormat));
    Register("AMediaCodec_getBufferFormat",
             reinterpret_cast<uintptr_t>(&AMediaCodec_getBufferFormat));
    Register("AMediaCodec_setParameters", reinterpret_cast<uintptr_t>(&AMediaCodec_setParameters));
    Register("AMediaCodec_signalEndOfInputStream",
             reinterpret_cast<uintptr_t>(&AMediaCodec_signalEndOfInputStream));
    Register("AMediaCodec_createInputSurface",
             reinterpret_cast<uintptr_t>(&AMediaCodec_createInputSurface));
    Register("AMediaCodec_createPersistentInputSurface",
             reinterpret_cast<uintptr_t>(&AMediaCodec_createPersistentInputSurface));
    Register("AMediaCodec_setInputSurface",
             reinterpret_cast<uintptr_t>(&AMediaCodec_setInputSurface));
    Register("AMediaCodec_setAsyncNotifyCallback",
             reinterpret_cast<uintptr_t>(&AMediaCodec_setAsyncNotifyCallback));

    // AMediaMuxer
    Register("AMediaMuxer_new", reinterpret_cast<uintptr_t>(&AMediaMuxer_new));
    Register("AMediaMuxer_delete", reinterpret_cast<uintptr_t>(&AMediaMuxer_delete));
    Register("AMediaMuxer_setLocation", reinterpret_cast<uintptr_t>(&AMediaMuxer_setLocation));
    Register("AMediaMuxer_setOrientationHint",
             reinterpret_cast<uintptr_t>(&AMediaMuxer_setOrientationHint));
    Register("AMediaMuxer_addTrack", reinterpret_cast<uintptr_t>(&AMediaMuxer_addTrack));
    Register("AMediaMuxer_start", reinterpret_cast<uintptr_t>(&AMediaMuxer_start));
    Register("AMediaMuxer_writeSampleData",
             reinterpret_cast<uintptr_t>(&AMediaMuxer_writeSampleData));
    Register("AMediaMuxer_stop", reinterpret_cast<uintptr_t>(&AMediaMuxer_stop));

    // AStatus
    Register("AStatus_newOk", reinterpret_cast<uintptr_t>(&AStatus_newOk));
    Register("AStatus_fromExceptionCode", reinterpret_cast<uintptr_t>(&AStatus_fromExceptionCode));
    Register("AStatus_fromExceptionCodeWithMessage",
             reinterpret_cast<uintptr_t>(&AStatus_fromExceptionCodeWithMessage));
    Register("AStatus_fromServiceSpecificError",
             reinterpret_cast<uintptr_t>(&AStatus_fromServiceSpecificError));
    Register("AStatus_fromServiceSpecificErrorWithMessage",
             reinterpret_cast<uintptr_t>(&AStatus_fromServiceSpecificErrorWithMessage));
    Register("AStatus_fromStatus", reinterpret_cast<uintptr_t>(&AStatus_fromStatus));
    Register("AStatus_delete", reinterpret_cast<uintptr_t>(&AStatus_delete));
    Register("AStatus_isOk", reinterpret_cast<uintptr_t>(&AStatus_isOk));
    Register("AStatus_getExceptionCode", reinterpret_cast<uintptr_t>(&AStatus_getExceptionCode));
    Register("AStatus_getServiceSpecificError",
             reinterpret_cast<uintptr_t>(&AStatus_getServiceSpecificError));
    Register("AStatus_getStatus", reinterpret_cast<uintptr_t>(&AStatus_getStatus));
    Register("AStatus_getMessage", reinterpret_cast<uintptr_t>(&AStatus_getMessage));
    Register("AStatus_getDescription", reinterpret_cast<uintptr_t>(&AStatus_getDescription));

    // AParcel
    Register("AParcel_create", reinterpret_cast<uintptr_t>(&AParcel_create));
    Register("AParcel_delete", reinterpret_cast<uintptr_t>(&AParcel_delete));
    Register("AParcel_reset", reinterpret_cast<uintptr_t>(&AParcel_reset));
    Register("AParcel_setDataPosition", reinterpret_cast<uintptr_t>(&AParcel_setDataPosition));
    Register("AParcel_getDataPosition", reinterpret_cast<uintptr_t>(&AParcel_getDataPosition));
    Register("AParcel_getDataSize", reinterpret_cast<uintptr_t>(&AParcel_getDataSize));
    Register("AParcel_setDataSize", reinterpret_cast<uintptr_t>(&AParcel_setDataSize));
    Register("AParcel_writeInt32", reinterpret_cast<uintptr_t>(&AParcel_writeInt32));
    Register("AParcel_writeUint32", reinterpret_cast<uintptr_t>(&AParcel_writeUint32));
    Register("AParcel_writeInt64", reinterpret_cast<uintptr_t>(&AParcel_writeInt64));
    Register("AParcel_writeUint64", reinterpret_cast<uintptr_t>(&AParcel_writeUint64));
    Register("AParcel_writeFloat", reinterpret_cast<uintptr_t>(&AParcel_writeFloat));
    Register("AParcel_writeDouble", reinterpret_cast<uintptr_t>(&AParcel_writeDouble));
    Register("AParcel_writeBool", reinterpret_cast<uintptr_t>(&AParcel_writeBool));
    Register("AParcel_writeChar", reinterpret_cast<uintptr_t>(&AParcel_writeChar));
    Register("AParcel_writeString", reinterpret_cast<uintptr_t>(&AParcel_writeString));
    Register("AParcel_writeByteArray", reinterpret_cast<uintptr_t>(&AParcel_writeByteArray));
    Register("AParcel_writeParcelFileDescriptor",
             reinterpret_cast<uintptr_t>(&AParcel_writeParcelFileDescriptor));
    Register("AParcel_readInt32", reinterpret_cast<uintptr_t>(&AParcel_readInt32));
    Register("AParcel_readUint32", reinterpret_cast<uintptr_t>(&AParcel_readUint32));
    Register("AParcel_readInt64", reinterpret_cast<uintptr_t>(&AParcel_readInt64));
    Register("AParcel_readUint64", reinterpret_cast<uintptr_t>(&AParcel_readUint64));
    Register("AParcel_readFloat", reinterpret_cast<uintptr_t>(&AParcel_readFloat));
    Register("AParcel_readDouble", reinterpret_cast<uintptr_t>(&AParcel_readDouble));
    Register("AParcel_readBool", reinterpret_cast<uintptr_t>(&AParcel_readBool));
    Register("AParcel_readChar", reinterpret_cast<uintptr_t>(&AParcel_readChar));
    Register("AParcel_readString", reinterpret_cast<uintptr_t>(&AParcel_readString));
    Register("AParcel_readByteArray", reinterpret_cast<uintptr_t>(&AParcel_readByteArray));
    Register("AParcel_readParcelFileDescriptor",
             reinterpret_cast<uintptr_t>(&AParcel_readParcelFileDescriptor));
    Register("AParcel_writeStrongBinder", reinterpret_cast<uintptr_t>(&AParcel_writeStrongBinder));
    Register("AParcel_readStrongBinder", reinterpret_cast<uintptr_t>(&AParcel_readStrongBinder));

    // AIBinder
    Register("AIBinder_Class_define", reinterpret_cast<uintptr_t>(&AIBinder_Class_define));
    Register("AIBinder_new", reinterpret_cast<uintptr_t>(&AIBinder_new));
    Register("AIBinder_transact", reinterpret_cast<uintptr_t>(&AIBinder_transact));
    Register("AIBinder_incStrong", reinterpret_cast<uintptr_t>(&AIBinder_incStrong));
    Register("AIBinder_decStrong", reinterpret_cast<uintptr_t>(&AIBinder_decStrong));
    Register("AIBinder_getCallingPid", reinterpret_cast<uintptr_t>(&AIBinder_getCallingPid));
    Register("AIBinder_getCallingUid", reinterpret_cast<uintptr_t>(&AIBinder_getCallingUid));
    Register("AIBinder_isAlive", reinterpret_cast<uintptr_t>(&AIBinder_isAlive));
    Register("AIBinder_isRemote", reinterpret_cast<uintptr_t>(&AIBinder_isRemote));
    Register("AIBinder_getUserData", reinterpret_cast<uintptr_t>(&AIBinder_getUserData));
    Register("AIBinder_Class_getDescriptor",
             reinterpret_cast<uintptr_t>(&AIBinder_Class_getDescriptor));
    Register("AIBinder_associateClass", reinterpret_cast<uintptr_t>(&AIBinder_associateClass));
    Register("AIBinder_getClass", reinterpret_cast<uintptr_t>(&AIBinder_getClass));
    Register("AIBinder_DeathRecipient_new",
             reinterpret_cast<uintptr_t>(&AIBinder_DeathRecipient_new));
    Register("AIBinder_DeathRecipient_delete",
             reinterpret_cast<uintptr_t>(&AIBinder_DeathRecipient_delete));
    Register("AIBinder_linkToDeath", reinterpret_cast<uintptr_t>(&AIBinder_linkToDeath));
    Register("AIBinder_unlinkToDeath", reinterpret_cast<uintptr_t>(&AIBinder_unlinkToDeath));
    Register("AIBinder_ping", reinterpret_cast<uintptr_t>(&AIBinder_ping));
    Register("AIBinder_dump", reinterpret_cast<uintptr_t>(&AIBinder_dump));
    Register("AIBinder_getExtension", reinterpret_cast<uintptr_t>(&AIBinder_getExtension));
    Register("AIBinder_setExtension", reinterpret_cast<uintptr_t>(&AIBinder_setExtension));

    // AServiceManager
    Register("AServiceManager_addService",
             reinterpret_cast<uintptr_t>(&AServiceManager_addService));
    Register("AServiceManager_checkService",
             reinterpret_cast<uintptr_t>(&AServiceManager_checkService));
    Register("AServiceManager_getService",
             reinterpret_cast<uintptr_t>(&AServiceManager_getService));
    Register("AServiceManager_waitForService",
             reinterpret_cast<uintptr_t>(&AServiceManager_waitForService));
    Register("AServiceManager_isDeclared",
             reinterpret_cast<uintptr_t>(&AServiceManager_isDeclared));
    Register("AServiceManager_registerForNotifications",
             reinterpret_cast<uintptr_t>(&AServiceManager_registerForNotifications));
    Register("AServiceManager_unregisterForNotifications",
             reinterpret_cast<uintptr_t>(&AServiceManager_unregisterForNotifications));

    // AParcel Java Interop
    Register("AParcel_fromJavaParcel", reinterpret_cast<uintptr_t>(&AParcel_fromJavaParcel));
    Register("AParcel_toJavaParcel", reinterpret_cast<uintptr_t>(&AParcel_toJavaParcel));

    // AStorageManager
    Register("AStorageManager_new", reinterpret_cast<uintptr_t>(&AStorageManager_new));
    Register("AStorageManager_delete", reinterpret_cast<uintptr_t>(&AStorageManager_delete));
    Register("AStorageManager_mountObb", reinterpret_cast<uintptr_t>(&AStorageManager_mountObb));
    Register("AStorageManager_unmountObb",
             reinterpret_cast<uintptr_t>(&AStorageManager_unmountObb));
    Register("AStorageManager_isObbMounted",
             reinterpret_cast<uintptr_t>(&AStorageManager_isObbMounted));
    Register("AStorageManager_getMountedObbPath",
             reinterpret_cast<uintptr_t>(&AStorageManager_getMountedObbPath));

    // Multinetwork
    Register("android_setsocknetwork", reinterpret_cast<uintptr_t>(&android_setsocknetwork));
    Register("android_setprocnetwork", reinterpret_cast<uintptr_t>(&android_setprocnetwork));
    Register("android_getprocnetwork", reinterpret_cast<uintptr_t>(&android_getprocnetwork));
    Register("android_getaddrinfofornetwork",
             reinterpret_cast<uintptr_t>(&android_getaddrinfofornetwork));
    Register("android_freeaddrinfofornetwork",
             reinterpret_cast<uintptr_t>(&android_freeaddrinfofornetwork));
    Register("android_res_nquery", reinterpret_cast<uintptr_t>(&android_res_nquery));
    Register("android_res_nsearch", reinterpret_cast<uintptr_t>(&android_res_nsearch));
    Register("android_res_nsend", reinterpret_cast<uintptr_t>(&android_res_nsend));
    Register("android_res_nresult", reinterpret_cast<uintptr_t>(&android_res_nresult));
    Register("android_res_cancel", reinterpret_cast<uintptr_t>(&android_res_cancel));
    Register("android_freeaddrinfo", reinterpret_cast<uintptr_t>(&android_freeaddrinfo));
    Register("freeaddrinfo", reinterpret_cast<uintptr_t>(&android_freeaddrinfo));

    // AThermalManager
    Register("AThermalManager_acquireManager",
             reinterpret_cast<uintptr_t>(&AThermalManager_acquireManager));
    Register("AThermalManager_releaseManager",
             reinterpret_cast<uintptr_t>(&AThermalManager_releaseManager));
    Register("AThermalManager_getCurrentThermalStatus",
             reinterpret_cast<uintptr_t>(&AThermalManager_getCurrentThermalStatus));
    Register("AThermalManager_registerThermalStatusListener",
             reinterpret_cast<uintptr_t>(&AThermalManager_registerThermalStatusListener));
    Register("AThermalManager_unregisterThermalStatusListener",
             reinterpret_cast<uintptr_t>(&AThermalManager_unregisterThermalStatusListener));
    Register("AThermalManager_getThermalHeadroom",
             reinterpret_cast<uintptr_t>(&AThermalManager_getThermalHeadroom));

    // APerformanceHintManager & APerformanceHintSession
    Register("APerformanceHintManager_create",
             reinterpret_cast<uintptr_t>(&APerformanceHintManager_create));
    Register("APerformanceHintManager_createSession",
             reinterpret_cast<uintptr_t>(&APerformanceHintManager_createSession));
    Register("APerformanceHintManager_getPreferredUpdateRateNanos",
             reinterpret_cast<uintptr_t>(&APerformanceHintManager_getPreferredUpdateRateNanos));
    Register("APerformanceHintSession_updateTargetWorkDuration",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_updateTargetWorkDuration));
    Register("APerformanceHintSession_reportActualWorkDuration",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_reportActualWorkDuration));
    Register("APerformanceHintSession_reportActualWorkDuration2",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_reportActualWorkDuration2));
    Register("APerformanceHintSession_sendHint",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_sendHint));
    Register("APerformanceHintSession_setPreferPowerEfficiency",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_setPreferPowerEfficiency));
    Register("APerformanceHintSession_close",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_close));
    Register("APerformanceHintSession_setThreads",
             reinterpret_cast<uintptr_t>(&APerformanceHintSession_setThreads));

    // AWorkDuration
    Register("AWorkDuration_create", reinterpret_cast<uintptr_t>(&AWorkDuration_create));
    Register("AWorkDuration_release", reinterpret_cast<uintptr_t>(&AWorkDuration_release));
    Register("AWorkDuration_setWorkPeriodStartTimestampNanos",
             reinterpret_cast<uintptr_t>(&AWorkDuration_setWorkPeriodStartTimestampNanos));
    Register("AWorkDuration_setActualTotalDurationNanos",
             reinterpret_cast<uintptr_t>(&AWorkDuration_setActualTotalDurationNanos));
    Register("AWorkDuration_setActualCpuDurationNanos",
             reinterpret_cast<uintptr_t>(&AWorkDuration_setActualCpuDurationNanos));
    Register("AWorkDuration_setActualGpuDurationNanos",
             reinterpret_cast<uintptr_t>(&AWorkDuration_setActualGpuDurationNanos));

    portage::gles::InitializeGlesDispatcher();
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window,
                                                                   int32_t width, int32_t height,
                                                                   int32_t format)
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

__attribute__((sysv_abi)) int32_t ANativeWindow_getWidth(ANativeWindow *window)
{
    return window ? window->width : 0;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_getHeight(ANativeWindow *window)
{
    return window ? window->height : 0;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_getFormat(ANativeWindow *window)
{
    return window ? window->format : 0;
}

__attribute__((sysv_abi)) void ANativeWindow_acquire(ANativeWindow *window)
{
    if (window) {
        window->ref_count++;
    }
}

__attribute__((sysv_abi)) void ANativeWindow_release(ANativeWindow *window)
{
    if (window && window->ref_count > 0) {
        window->ref_count--;
        if (window->ref_count == 0 && window->owned_heap) {
            delete window;
        }
    }
}

__attribute__((sysv_abi)) ANativeWindow *ANativeWindow_fromSurface(void *, void *surface)
{
    if (!surface) {
        return nullptr;
    }
    ANativeWindow *win = nullptr;
    auto syn = portage::package::JniEnvironment::Instance().LookupObject(surface);
    if (syn) {
        if (syn->native_ptr) {
            win = reinterpret_cast<ANativeWindow *>(syn->native_ptr);
        } else {
            auto it = syn->long_fields.find("mNativeObject");
            if (it != syn->long_fields.end() && it->second != 0) {
                win = reinterpret_cast<ANativeWindow *>(static_cast<uintptr_t>(it->second));
            }
        }
    }
    if (win) {
        ANativeWindow_acquire(win);
    }
    return win;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_lock(ANativeWindow *window,
                                                     ANativeWindow_Buffer *outBuffer,
                                                     ARect *inOutDirtyBounds)
{
    if (!window || !outBuffer) {
        return -1;
    }
    if (window->is_locked) {
        return -1;
    }

    int32_t width = (window->width > 0) ? window->width : 1280;
    int32_t height = (window->height > 0) ? window->height : 720;
    int32_t format = (window->format > 0) ? window->format : 1; // 1 = WINDOW_FORMAT_RGBA_8888

    int32_t bpp = (format == 4 /* RGB_565 */) ? 2 : 4;
    int32_t row_bytes = ((width * bpp + 63) / 64) * 64;
    int32_t stride = row_bytes / bpp;
    size_t total_bytes = static_cast<size_t>(row_bytes) * static_cast<size_t>(height);

    if (window->backbuffer.size() < total_bytes) {
        window->backbuffer.resize(total_bytes, 0);
    }

    outBuffer->width = width;
    outBuffer->height = height;
    outBuffer->stride = stride;
    outBuffer->format = format;
    outBuffer->bits = window->backbuffer.data();

    if (inOutDirtyBounds) {
        window->dirty_rect = *inOutDirtyBounds;
    } else {
        window->dirty_rect = ARect{0, 0, width, height};
    }

    window->locked_width = width;
    window->locked_height = height;
    window->locked_stride = stride;
    window->locked_format = format;
    window->is_locked = true;

    return 0;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_unlockAndPost(ANativeWindow *window)
{
    if (!window || !window->is_locked) {
        return -1;
    }

    window->frontbuffer = window->backbuffer;

    if (window->native_handle != nullptr) {
        HWND hwnd = static_cast<HWND>(window->native_handle);
        HDC hdc = GetDC(hwnd);
        if (hdc != NULL) {
            struct {
                BITMAPINFOHEADER bmiHeader;
                DWORD bmiColors[3];
            } bmi{};

            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = window->locked_stride;
            bmi.bmiHeader.biHeight = -window->locked_height; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = (window->locked_format == 4) ? 16 : 32;
            bmi.bmiHeader.biCompression = BI_BITFIELDS;

            if (window->locked_format == 4) {
                bmi.bmiColors[0] = 0xF800;
                bmi.bmiColors[1] = 0x07E0;
                bmi.bmiColors[2] = 0x001F;
            } else {
                bmi.bmiColors[0] = 0x000000FF; // Red
                bmi.bmiColors[1] = 0x0000FF00; // Green
                bmi.bmiColors[2] = 0x00FF0000; // Blue
            }

            StretchDIBits(hdc, 0, 0, window->width, window->height, 0, 0, window->locked_width,
                          window->locked_height, window->frontbuffer.data(),
                          reinterpret_cast<const BITMAPINFO *>(&bmi), DIB_RGB_COLORS, SRCCOPY);
            ReleaseDC(hwnd, hdc);
        }
    }

    portage::wsi::WindowManager::Instance().RecordFramePresented();
    auto now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    window->last_queue_time_nanos = now_nanos;
    window->last_latch_time_nanos = now_nanos;
    window->last_present_time_nanos = now_nanos;
    window->frame_index++;
    window->is_locked = false;
    auto post_hook = window->on_post;
    if (post_hook) {
        post_hook(window);
    }
    return 0;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_getLastQueueDuration(ANativeWindow *window,
                                                                     int64_t *outDuration)
{
    if (!window || !outDuration) {
        return -1;
    }
    *outDuration = window->last_queue_duration_nanos;
    return 0;
}

__attribute__((sysv_abi)) int32_t ANativeWindow_getFrameTimestamps(ANativeWindow *window,
                                                                   uint64_t frameId,
                                                                   int64_t *outQueueTime,
                                                                   int64_t *outLatchTime,
                                                                   int64_t *outPresentTime)
{
    if (!window) {
        return -1;
    }
    (void)frameId;
    if (outQueueTime) {
        *outQueueTime = window->last_queue_time_nanos;
    }
    if (outLatchTime) {
        *outLatchTime = window->last_latch_time_nanos;
    }
    if (outPresentTime) {
        *outPresentTime = window->last_present_time_nanos;
    }
    return 0;
}

__attribute__((sysv_abi)) struct AAssetManager *AAssetManager_fromJava(void *env,
                                                                       void *assetManager)
{
    (void)env;
    if (assetManager) {
        auto obj = portage::package::JniEnvironment::Instance().LookupObject(
            reinterpret_cast<portage::package::jobject>(assetManager));
        if (obj && obj->native_ptr) {
            return reinterpret_cast<struct AAssetManager *>(obj->native_ptr);
        }
    }
    const auto *pkg = portage::package::PackageManager::Instance().GetActivePackage();
    if (pkg && pkg->asset_manager) {
        return reinterpret_cast<struct AAssetManager *>(pkg->asset_manager);
    }
    return nullptr;
}

__attribute__((sysv_abi)) struct AAsset *AAssetManager_open(struct AAssetManager *mgr,
                                                            const char *filename, int mode)
{
    return reinterpret_cast<struct AAsset *>(
        portage::package::AssetManagerBridge::Sysv_AAssetManager_open(
            reinterpret_cast<portage::package::AAssetManager *>(mgr), filename, mode));
}

__attribute__((sysv_abi)) struct AAssetDir *AAssetManager_openDir(struct AAssetManager *mgr,
                                                                  const char *dirName)
{
    return reinterpret_cast<struct AAssetDir *>(
        portage::package::AssetManagerBridge::Sysv_AAssetManager_openDir(
            reinterpret_cast<portage::package::AAssetManager *>(mgr), dirName));
}

__attribute__((sysv_abi)) int AAsset_read(struct AAsset *asset, void *buf, size_t count)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_read(
        reinterpret_cast<portage::package::AAsset *>(asset), buf, count);
}

__attribute__((sysv_abi)) int64_t AAsset_seek(struct AAsset *asset, int64_t offset, int whence)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_seek(
        reinterpret_cast<portage::package::AAsset *>(asset), offset, whence);
}

__attribute__((sysv_abi)) int64_t AAsset_seek64(struct AAsset *asset, int64_t offset, int whence)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_seek64(
        reinterpret_cast<portage::package::AAsset *>(asset), offset, whence);
}

__attribute__((sysv_abi)) int64_t AAsset_getLength(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_getLength(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) int64_t AAsset_getLength64(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_getLength64(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) int64_t AAsset_getRemainingLength(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_getRemainingLength(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) int64_t AAsset_getRemainingLength64(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_getRemainingLength64(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) const void *AAsset_getBuffer(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_getBuffer(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) int AAsset_isAllocated(struct AAsset *asset)
{
    return portage::package::AssetManagerBridge::Sysv_AAsset_isAllocated(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) void AAsset_close(struct AAsset *asset)
{
    portage::package::AssetManagerBridge::Sysv_AAsset_close(
        reinterpret_cast<portage::package::AAsset *>(asset));
}

__attribute__((sysv_abi)) const char *AAssetDir_getNextFileName(struct AAssetDir *assetDir)
{
    return portage::package::AssetManagerBridge::Sysv_AAssetDir_getNextFileName(
        reinterpret_cast<portage::package::AAssetDir *>(assetDir));
}

__attribute__((sysv_abi)) void AAssetDir_rewind(struct AAssetDir *assetDir)
{
    portage::package::AssetManagerBridge::Sysv_AAssetDir_rewind(
        reinterpret_cast<portage::package::AAssetDir *>(assetDir));
}

__attribute__((sysv_abi)) void AAssetDir_close(struct AAssetDir *assetDir)
{
    portage::package::AssetManagerBridge::Sysv_AAssetDir_close(
        reinterpret_cast<portage::package::AAssetDir *>(assetDir));
}

__attribute__((sysv_abi)) int32_t ASharedMemory_create(const char *name, size_t size)
{
    if (size == 0) {
        return -22; // -EINVAL
    }
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table) {
        return -1;
    }
    auto shm = portage::mem::SharedMemoryManager::Instance().CreateRegion(name ? name : "", size);
    if (!shm || shm->mapping_handle == INVALID_HANDLE_VALUE) {
        return -12; // -ENOMEM
    }
    return fd_table->AllocateSharedMemory(shm, portage::fs::DeviceType::SharedMemory, 0,
                                          name ? name : "ashmem");
}

__attribute__((sysv_abi)) size_t ASharedMemory_getSize(int fd)
{
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table) {
        return 0;
    }
    auto desc = fd_table->Get(fd);
    if (!desc || !desc->shared_memory) {
        return 0;
    }
    return desc->shared_memory->size;
}

__attribute__((sysv_abi)) int32_t ASharedMemory_setProt(int fd, int prot)
{
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table) {
        return -1;
    }
    auto desc = fd_table->Get(fd);
    if (!desc || !desc->shared_memory) {
        return -9; // -EBADF
    }
    return portage::mem::SharedMemoryManager::Instance().SetProtMask(*desc->shared_memory, prot);
}

__attribute__((sysv_abi)) int32_t ASharedMemory_dup(int fd)
{
    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table) {
        return -1;
    }
    return fd_table->Duplicate(fd);
}

__attribute__((sysv_abi)) int android_get_device_api_level()
{
    std::string val =
        portage::property::PropertyManager::Instance().GetProperty("ro.build.version.sdk", "34");
    if (!val.empty()) {
        int parsed = 0;
        bool ok = true;
        for (char c : val) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            parsed = parsed * 10 + (c - '0');
        }
        if (ok && parsed > 0) {
            return parsed;
        }
    }
    return 34;
}

__attribute__((sysv_abi)) int android_get_application_target_sdk_version()
{
    const auto *pkg = portage::package::PackageManager::Instance().GetActivePackage();
    if (pkg && pkg->manifest.target_sdk_version > 0) {
        return pkg->manifest.target_sdk_version;
    }
    return 34;
}

__attribute__((sysv_abi)) uintptr_t portage_pthread_self() noexcept
{
    return portage::thread::ThreadManager::Instance().GetCurrentPthreadHandle();
}

__attribute__((sysv_abi)) int portage_pthread_equal(uintptr_t t1, uintptr_t t2) noexcept
{
    return (t1 == t2) ? 1 : 0;
}

__attribute__((sysv_abi)) int portage_pthread_setname_np(uintptr_t thread,
                                                         const char *name) noexcept
{
    if (name == nullptr) {
        return 22; // EINVAL
    }
    return portage::thread::ThreadManager::Instance().SetThreadName(thread, name);
}

__attribute__((sysv_abi)) int portage_pthread_getname_np(uintptr_t thread, char *buf,
                                                         size_t len) noexcept
{
    return portage::thread::ThreadManager::Instance().GetThreadName(thread, buf, len);
}

__attribute__((sysv_abi)) int portage_pthread_attr_init(BionicPthreadAttr *attr) noexcept
{
    if (attr == nullptr) {
        return 22; // EINVAL
    }
    *attr = BionicPthreadAttr{};
    attr->flags = 0;
    attr->stack_base = nullptr;
    attr->stack_size = 1024 * 1024;
    attr->guard_size = 4096;
    attr->sched_policy = 0;
    attr->sched_priority = 0;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_destroy(BionicPthreadAttr *attr) noexcept
{
    if (attr == nullptr) {
        return 22; // EINVAL
    }
    *attr = BionicPthreadAttr{};
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_getattr_np(uintptr_t thread,
                                                         BionicPthreadAttr *attr) noexcept
{
    if (attr == nullptr) {
        return 22; // EINVAL
    }
    *attr = BionicPthreadAttr{};
    attr->guard_size = 4096;
    void *stack_addr = nullptr;
    size_t stack_sz = 0;
    if (!portage::thread::ThreadManager::Instance().GetThreadStack(thread, &stack_addr,
                                                                   &stack_sz)) {
        return 3; // ESRCH
    }
    attr->stack_base = stack_addr;
    attr->stack_size = stack_sz;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_getstack(const BionicPthreadAttr *attr,
                                                            void **stackaddr,
                                                            size_t *stacksize) noexcept
{
    if (attr == nullptr) {
        return 22;
    }
    if (stackaddr != nullptr) {
        *stackaddr = attr->stack_base;
    }
    if (stacksize != nullptr) {
        *stacksize = attr->stack_size;
    }
    return 0;
}

__attribute__((sysv_abi)) int
portage_pthread_attr_setstack(BionicPthreadAttr *attr, void *stackaddr, size_t stacksize) noexcept
{
    if (attr == nullptr || stacksize < 16384) {
        return 22;
    }
    attr->stack_base = stackaddr;
    attr->stack_size = stacksize;
    attr->flags |= 0x2U;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_getstacksize(const BionicPthreadAttr *attr,
                                                                size_t *stacksize) noexcept
{
    if (attr == nullptr || stacksize == nullptr) {
        return 22;
    }
    *stacksize = attr->stack_size;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_setstacksize(BionicPthreadAttr *attr,
                                                                size_t stacksize) noexcept
{
    if (attr == nullptr || stacksize < 16384) {
        return 22;
    }
    attr->stack_size = stacksize;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_getguardsize(const BionicPthreadAttr *attr,
                                                                size_t *guardsize) noexcept
{
    if (attr == nullptr || guardsize == nullptr) {
        return 22;
    }
    *guardsize = attr->guard_size;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_setguardsize(BionicPthreadAttr *attr,
                                                                size_t guardsize) noexcept
{
    if (attr == nullptr) {
        return 22;
    }
    attr->guard_size = guardsize;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_getdetachstate(const BionicPthreadAttr *attr,
                                                                  int *detachstate) noexcept
{
    if (attr == nullptr || detachstate == nullptr) {
        return 22;
    }
    *detachstate = (attr->flags & 0x1U) ? 1 : 0;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_attr_setdetachstate(BionicPthreadAttr *attr,
                                                                  int detachstate) noexcept
{
    if (attr == nullptr || (detachstate != 0 && detachstate != 1)) {
        return 22;
    }
    if (detachstate == 1) {
        attr->flags |= 0x1U;
    } else {
        attr->flags &= ~0x1U;
    }
    return 0;
}

} // extern "C"
