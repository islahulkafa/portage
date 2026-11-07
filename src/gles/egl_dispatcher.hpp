#pragma once

#include "gles/egl_types.hpp"
#include "gles/wgl_bridge.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ANativeWindow;

namespace portage::gles {

struct EglConfigData {
    EGLint red_size{8};
    EGLint green_size{8};
    EGLint blue_size{8};
    EGLint alpha_size{8};
    EGLint depth_size{24};
    EGLint stencil_size{8};
    EGLint surface_type{EGL_WINDOW_BIT | EGL_PBUFFER_BIT};
    EGLint renderable_type{EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT};
    EGLint config_id{1};
};

struct EglSurfaceData {
    HDC hdc{nullptr};
    HBITMAP offscreen_handle{nullptr};
    ANativeWindow *native_window{nullptr};
    HWND hwnd{nullptr};
    int32_t width{1280};
    int32_t height{720};
    bool is_offscreen{false};
};

struct EglContextData {
    HGLRC hglrc{nullptr};
    int client_version{2};
    EglConfigData config{};
};

class EglDispatcher {
  public:
    static EglDispatcher &Instance() noexcept;

    EGLDisplay GetDisplay(EGLNativeDisplayType display_id) noexcept;
    EGLBoolean Initialize(EGLDisplay dpy, EGLint *major, EGLint *minor) noexcept;
    EGLBoolean Terminate(EGLDisplay dpy) noexcept;
    const char *QueryString(EGLDisplay dpy, EGLint name) noexcept;

    EGLBoolean GetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size,
                          EGLint *num_config) noexcept;
    EGLBoolean ChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs,
                            EGLint config_size, EGLint *num_config) noexcept;
    EGLBoolean GetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                               EGLint *value) noexcept;

    EGLSurface CreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win,
                                   const EGLint *attrib_list) noexcept;
    EGLSurface CreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                    const EGLint *attrib_list) noexcept;
    EGLBoolean DestroySurface(EGLDisplay dpy, EGLSurface surface) noexcept;
    EGLBoolean QuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                            EGLint *value) noexcept;

    EGLContext CreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context,
                             const EGLint *attrib_list) noexcept;
    EGLBoolean DestroyContext(EGLDisplay dpy, EGLContext ctx) noexcept;
    EGLBoolean MakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                           EGLContext ctx) noexcept;

    EGLContext GetCurrentContext() noexcept;
    EGLSurface GetCurrentSurface(EGLint readdraw) noexcept;
    EGLDisplay GetCurrentDisplay() noexcept;

    EGLBoolean SwapBuffers(EGLDisplay dpy, EGLSurface surface) noexcept;
    EGLint GetError() noexcept;

    void *GetProcAddress(const char *procname) noexcept;
    void RegisterProc(std::string_view name, void *proc);

  private:
    EglDispatcher();
    ~EglDispatcher() = default;

    EglDispatcher(const EglDispatcher &) = delete;
    EglDispatcher &operator=(const EglDispatcher &) = delete;

    std::mutex mutex_{};
    bool initialized_{false};
    EGLint last_error_{EGL_SUCCESS};

    EGLContext current_context_{EGL_NO_CONTEXT};
    EGLSurface current_draw_{EGL_NO_SURFACE};
    EGLSurface current_read_{EGL_NO_SURFACE};

    std::vector<EglConfigData> configs_{};
    std::unordered_map<EGLSurface, std::unique_ptr<EglSurfaceData>> surfaces_{};
    std::unordered_map<EGLContext, std::unique_ptr<EglContextData>> contexts_{};
    std::unordered_map<std::string, void *> procs_{};
};

} // namespace portage::gles

extern "C" {
__attribute__((sysv_abi)) EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id);
__attribute__((sysv_abi)) EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
__attribute__((sysv_abi)) EGLBoolean eglTerminate(EGLDisplay dpy);
__attribute__((sysv_abi)) const char *eglQueryString(EGLDisplay dpy, EGLint name);
__attribute__((sysv_abi)) EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                                   EGLint config_size, EGLint *num_config);
__attribute__((sysv_abi)) EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                                     EGLConfig *configs, EGLint config_size,
                                                     EGLint *num_config);
__attribute__((sysv_abi)) EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                        EGLint attribute, EGLint *value);
__attribute__((sysv_abi)) EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                            EGLNativeWindowType win,
                                                            const EGLint *attrib_list);
__attribute__((sysv_abi)) EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                             const EGLint *attrib_list);
__attribute__((sysv_abi)) EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
__attribute__((sysv_abi)) EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                                     EGLint attribute, EGLint *value);
__attribute__((sysv_abi)) EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                                      EGLContext share_context,
                                                      const EGLint *attrib_list);
__attribute__((sysv_abi)) EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
__attribute__((sysv_abi)) EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                                    EGLSurface read, EGLContext ctx);
__attribute__((sysv_abi)) EGLContext eglGetCurrentContext(void);
__attribute__((sysv_abi)) EGLSurface eglGetCurrentSurface(EGLint readdraw);
__attribute__((sysv_abi)) EGLDisplay eglGetCurrentDisplay(void);
__attribute__((sysv_abi)) EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
__attribute__((sysv_abi)) EGLint eglGetError(void);
__attribute__((sysv_abi)) void *eglGetProcAddress(const char *procname);
}