#include "libandroid_thunks.hpp"
#include "android_log.hpp"
#include "audio/aaudio_dispatcher.hpp"
#include "audio/opensles_dispatcher.hpp"
#include "binder/binder_ndk.hpp"
#include "binder/binder_parcel.hpp"
#include "binder/binder_status.hpp"
#include "binder/service_manager.hpp"
#include "configuration.hpp"
#include "elf/link_map.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "input_queue.hpp"
#include "jnigraphics.hpp"
#include "looper.hpp"
#include "media/media_codec.hpp"
#include "media/media_extractor.hpp"
#include "media/media_format.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/hardware_buffer.hpp"
#include "package/asset_manager.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
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
    Register("ANativeWindow_lock", reinterpret_cast<uintptr_t>(&ANativeWindow_lock));
    Register("ANativeWindow_unlockAndPost",
             reinterpret_cast<uintptr_t>(&ANativeWindow_unlockAndPost));

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
    Register("eglGetError", reinterpret_cast<uintptr_t>(&eglGetError));
    Register("eglGetProcAddress", reinterpret_cast<uintptr_t>(&eglGetProcAddress));

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
    Register("AAudioStreamBuilder_openStream",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_openStream));
    Register("AAudioStreamBuilder_delete",
             reinterpret_cast<uintptr_t>(&audio::AAudioStreamBuilder_delete));
    Register("AAudioStream_requestStart",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestStart));
    Register("AAudioStream_requestPause",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestPause));
    Register("AAudioStream_requestStop",
             reinterpret_cast<uintptr_t>(&audio::AAudioStream_requestStop));
    Register("AAudioStream_close", reinterpret_cast<uintptr_t>(&audio::AAudioStream_close));
    Register("AAudioStream_write", reinterpret_cast<uintptr_t>(&audio::AAudioStream_write));
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

    // OpenSL ES
    Register("slCreateEngine", reinterpret_cast<uintptr_t>(&audio::slCreateEngine));
    Register("SL_IID_ENGINE", reinterpret_cast<uintptr_t>(&audio::SL_IID_ENGINE));
    Register("SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
             reinterpret_cast<uintptr_t>(&audio::SL_IID_ANDROIDSIMPLEBUFFERQUEUE));
    Register("SL_IID_PLAY", reinterpret_cast<uintptr_t>(&audio::SL_IID_PLAY));
    Register("SL_IID_VOLUME", reinterpret_cast<uintptr_t>(&audio::SL_IID_VOLUME));
    Register("SL_IID_BUFFERQUEUE", reinterpret_cast<uintptr_t>(&audio::SL_IID_BUFFERQUEUE));

    // libdl
    Register("dlopen", reinterpret_cast<uintptr_t>(&dlopen));
    Register("__loader_dlopen", reinterpret_cast<uintptr_t>(&dlopen));
    Register("dlsym", reinterpret_cast<uintptr_t>(&dlsym));
    Register("__loader_dlsym", reinterpret_cast<uintptr_t>(&dlsym));
    Register("dlclose", reinterpret_cast<uintptr_t>(&dlclose));
    Register("__loader_dlclose", reinterpret_cast<uintptr_t>(&dlclose));
    Register("dlerror", reinterpret_cast<uintptr_t>(&dlerror));
    Register("__loader_dlerror", reinterpret_cast<uintptr_t>(&dlerror));
    Register("dladdr", reinterpret_cast<uintptr_t>(&dladdr));
    Register("__loader_dladdr", reinterpret_cast<uintptr_t>(&dladdr));

    // libjnigraphics
    Register("AndroidBitmap_getInfo", reinterpret_cast<uintptr_t>(&AndroidBitmap_getInfo));
    Register("AndroidBitmap_lockPixels", reinterpret_cast<uintptr_t>(&AndroidBitmap_lockPixels));
    Register("AndroidBitmap_unlockPixels",
             reinterpret_cast<uintptr_t>(&AndroidBitmap_unlockPixels));
    Register("AndroidBitmap_getData", reinterpret_cast<uintptr_t>(&AndroidBitmap_getData));

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

    // AServiceManager
    Register("AServiceManager_addService",
             reinterpret_cast<uintptr_t>(&AServiceManager_addService));
    Register("AServiceManager_checkService",
             reinterpret_cast<uintptr_t>(&AServiceManager_checkService));
    Register("AServiceManager_getService",
             reinterpret_cast<uintptr_t>(&AServiceManager_getService));
    Register("AServiceManager_waitForService",
             reinterpret_cast<uintptr_t>(&AServiceManager_waitForService));

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
    }
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
    window->is_locked = false;
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

} // extern "C"
