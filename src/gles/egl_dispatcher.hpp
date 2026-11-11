#pragma once

#include "gles/egl_types.hpp"
#include "gles/wgl_bridge.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ANativeWindow;
struct AHardwareBuffer;

namespace portage::fs {
class FileDescriptorTable;
}

namespace portage::gles {

struct EglSyncData {
    EGLenum type{EGL_SYNC_FENCE};
    EGLint status{EGL_UNSIGNALED};
    EGLint condition{EGL_SYNC_PRIOR_COMMANDS_COMPLETE};
    GLsync gl_sync{nullptr};
    int native_fence_fd{-1};
};

struct EglImageData {
    EGLenum target{0};
    EGLClientBuffer client_buffer{nullptr};
    AHardwareBuffer *ahb{nullptr};
    GLuint gl_texture{0};
};

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
    EGLBoolean SwapInterval(EGLDisplay dpy, EGLint interval) noexcept;
    EGLint GetSwapInterval() const noexcept { return swap_interval_; }
    EGLint GetError() noexcept;

    EGLSync CreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list) noexcept;
    EGLSyncKHR CreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list) noexcept;
    EGLBoolean DestroySync(EGLDisplay dpy, EGLSync sync) noexcept;
    EGLint ClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) noexcept;
    EGLBoolean WaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) noexcept;
    EGLBoolean GetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                             EGLAttrib *value) noexcept;
    EGLint DupNativeFenceFDANDROID(EGLDisplay dpy, EGLSyncKHR sync) noexcept;

    EGLImageKHR CreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                               EGLClientBuffer buffer, const EGLint *attrib_list) noexcept;
    EGLBoolean DestroyImageKHR(EGLDisplay dpy, EGLImageKHR image) noexcept;
    [[nodiscard]] const EglImageData *FindImage(EGLImageKHR image) const noexcept;

    void SetFdTable(fs::FileDescriptorTable *fd_table) noexcept { fd_table_override_ = fd_table; }

    void *GetProcAddress(const char *procname) noexcept;
    void RegisterProc(std::string_view name, void *proc);

  private:
    EglDispatcher();
    ~EglDispatcher() = default;

    EglDispatcher(const EglDispatcher &) = delete;
    EglDispatcher &operator=(const EglDispatcher &) = delete;

    mutable std::mutex mutex_{};
    bool initialized_{false};
    EGLint last_error_{EGL_SUCCESS};
    EGLint swap_interval_{1};
    fs::FileDescriptorTable *fd_table_override_{nullptr};

    EGLContext current_context_{EGL_NO_CONTEXT};
    EGLSurface current_draw_{EGL_NO_SURFACE};
    EGLSurface current_read_{EGL_NO_SURFACE};

    std::vector<EglConfigData> configs_{};
    std::unordered_map<EGLSurface, std::unique_ptr<EglSurfaceData>> surfaces_{};
    std::unordered_map<EGLContext, std::unique_ptr<EglContextData>> contexts_{};
    std::unordered_map<EGLSync, std::unique_ptr<EglSyncData>> syncs_{};
    std::unordered_map<EGLImageKHR, std::unique_ptr<EglImageData>> images_{};
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
__attribute__((sysv_abi)) EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
__attribute__((sysv_abi)) EGLint eglGetError(void);
__attribute__((sysv_abi)) void *eglGetProcAddress(const char *procname);
__attribute__((sysv_abi)) EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type,
                                                const EGLAttrib *attrib_list);
__attribute__((sysv_abi)) EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync);
__attribute__((sysv_abi)) EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                                                   EGLTime timeout);
__attribute__((sysv_abi)) EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags);
__attribute__((sysv_abi)) EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync,
                                                      EGLint attribute, EGLAttrib *value);
__attribute__((sysv_abi)) EGLSyncKHR eglCreateSyncKHR(EGLDisplay dpy, EGLenum type,
                                                      const EGLint *attrib_list);
__attribute__((sysv_abi)) EGLBoolean eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync);
__attribute__((sysv_abi)) EGLint eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags,
                                                      EGLTimeKHR timeout);
__attribute__((sysv_abi)) EGLBoolean eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags);
__attribute__((sysv_abi)) EGLBoolean eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                                         EGLint attribute, EGLint *value);
__attribute__((sysv_abi)) EGLint eglDupNativeFenceFDANDROID(EGLDisplay dpy, EGLSyncKHR sync);
__attribute__((sysv_abi)) EGLClientBuffer
eglGetNativeClientBufferANDROID(const struct AHardwareBuffer *buffer);
__attribute__((sysv_abi)) EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx,
                                                        EGLenum target, EGLClientBuffer buffer,
                                                        const EGLint *attrib_list);
__attribute__((sysv_abi)) EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image);
}