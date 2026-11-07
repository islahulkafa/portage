#include "libandroid_thunks.hpp"
#include "android_log.hpp"
#include "configuration.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "input_queue.hpp"
#include "looper.hpp"
#include "package/asset_manager.hpp"

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

} // extern "C"
