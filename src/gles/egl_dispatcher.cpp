#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <cstring>

namespace portage::gles {

namespace {
void *const kSyntheticDisplay = reinterpret_cast<void *>(0x1001);
}

EglDispatcher &EglDispatcher::Instance() noexcept
{
    static EglDispatcher instance;
    return instance;
}

EglDispatcher::EglDispatcher()
{
    // Populate default configurations (standard 32-bit RGBA8888 with 24-bit depth and 8-bit
    // stencil)
    EglConfigData cfg1{.red_size = 8,
                       .green_size = 8,
                       .blue_size = 8,
                       .alpha_size = 8,
                       .depth_size = 24,
                       .stencil_size = 8,
                       .surface_type = EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                       .renderable_type = EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
                       .config_id = 1};
    configs_.push_back(cfg1);

    EglConfigData cfg2{.red_size = 8,
                       .green_size = 8,
                       .blue_size = 8,
                       .alpha_size = 0,
                       .depth_size = 24,
                       .stencil_size = 8,
                       .surface_type = EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                       .renderable_type = EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
                       .config_id = 2};
    configs_.push_back(cfg2);
}

EGLDisplay EglDispatcher::GetDisplay(EGLNativeDisplayType display_id) noexcept
{
    (void)display_id;
    return kSyntheticDisplay;
}

EGLBoolean EglDispatcher::Initialize(EGLDisplay dpy, EGLint *major, EGLint *minor) noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (dpy != kSyntheticDisplay) {
            last_error_ = EGL_BAD_DISPLAY;
            return EGL_FALSE;
        }

        if (!WglBridge::Instance().Initialize()) {
            last_error_ = EGL_NOT_INITIALIZED;
            return EGL_FALSE;
        }

        if (major)
            *major = 1;
        if (minor)
            *minor = 4;
        initialized_ = true;
        last_error_ = EGL_SUCCESS;
    }

    InitializeGlesDispatcher();
    return EGL_TRUE;
}

EGLBoolean EglDispatcher::Terminate(EGLDisplay dpy) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    surfaces_.clear();
    contexts_.clear();
    current_context_ = EGL_NO_CONTEXT;
    current_draw_ = EGL_NO_SURFACE;
    current_read_ = EGL_NO_SURFACE;
    initialized_ = false;
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

const char *EglDispatcher::QueryString(EGLDisplay dpy, EGLint name) noexcept
{
    (void)dpy;
    switch (name) {
    case EGL_VENDOR:
        return "Portage Project";
    case EGL_VERSION:
        return "1.4 Portage GLES/WGL Passthrough";
    case EGL_EXTENSIONS:
        return "EGL_KHR_image_base EGL_KHR_gl_texture_2D_image";
    case EGL_CLIENT_APIS:
        return "OpenGL_ES";
    default:
        return "";
    }
}

EGLBoolean EglDispatcher::GetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size,
                                     EGLint *num_config) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    if (!num_config) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    *num_config = static_cast<EGLint>(configs_.size());
    if (configs && config_size > 0) {
        int count = (config_size < *num_config) ? config_size : *num_config;
        for (int i = 0; i < count; ++i) {
            configs[i] = reinterpret_cast<EGLConfig>(static_cast<uintptr_t>(i + 1));
        }
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLBoolean EglDispatcher::ChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                       EGLConfig *configs, EGLint config_size,
                                       EGLint *num_config) noexcept
{
    (void)attrib_list;
    return GetConfigs(dpy, configs, config_size, num_config);
}

EGLBoolean EglDispatcher::GetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                                          EGLint *value) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(config)) - 1;
    if (idx >= configs_.size() || !value) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    const auto &c = configs_[idx];
    switch (attribute) {
    case EGL_BUFFER_SIZE:
        *value = c.red_size + c.green_size + c.blue_size + c.alpha_size;
        break;
    case EGL_RED_SIZE:
        *value = c.red_size;
        break;
    case EGL_GREEN_SIZE:
        *value = c.green_size;
        break;
    case EGL_BLUE_SIZE:
        *value = c.blue_size;
        break;
    case EGL_ALPHA_SIZE:
        *value = c.alpha_size;
        break;
    case EGL_DEPTH_SIZE:
        *value = c.depth_size;
        break;
    case EGL_STENCIL_SIZE:
        *value = c.stencil_size;
        break;
    case EGL_SURFACE_TYPE:
        *value = c.surface_type;
        break;
    case EGL_RENDERABLE_TYPE:
        *value = c.renderable_type;
        break;
    case EGL_CONFIG_ID:
        *value = c.config_id;
        break;
    default:
        *value = 0;
        break;
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLSurface EglDispatcher::CreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                              EGLNativeWindowType win,
                                              const EGLint *attrib_list) noexcept
{
    (void)config;
    (void)attrib_list;
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_NO_SURFACE;
    }

    auto *native_window = reinterpret_cast<ANativeWindow *>(win);
    int32_t width = native_window ? native_window->width : 1280;
    int32_t height = native_window ? native_window->height : 720;

    auto surface_data = std::make_unique<EglSurfaceData>();
    surface_data->native_window = native_window;
    surface_data->width = width;
    surface_data->height = height;

    // Check if a Win32 HWND was attached to native_window (if extended), otherwise use hardware
    // offscreen DC
    surface_data->is_offscreen = true;
    surface_data->hdc = WglBridge::Instance().CreateOffscreenDc(
        width, height, &surface_data->offscreen_handle, nullptr);

    if (!surface_data->hdc) {
        last_error_ = EGL_BAD_ALLOC;
        return EGL_NO_SURFACE;
    }

    EGLSurface handle = reinterpret_cast<EGLSurface>(surface_data.get());
    surfaces_[handle] = std::move(surface_data);
    last_error_ = EGL_SUCCESS;
    return handle;
}

EGLSurface EglDispatcher::CreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                               const EGLint *attrib_list) noexcept
{
    int32_t width = 256;
    int32_t height = 256;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_WIDTH)
                width = attrib_list[i + 1];
            if (attrib_list[i] == EGL_HEIGHT)
                height = attrib_list[i + 1];
        }
    }
    ANativeWindow win{
        .width = width, .height = height, .format = 1, .ref_count = 1, .native_handle = nullptr};
    return CreateWindowSurface(dpy, config, reinterpret_cast<EGLNativeWindowType>(&win),
                               attrib_list);
}

EGLBoolean EglDispatcher::DestroySurface(EGLDisplay dpy, EGLSurface surface) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = surfaces_.find(surface);
    if (it == surfaces_.end()) {
        last_error_ = EGL_BAD_SURFACE;
        return EGL_FALSE;
    }

    if (it->second->is_offscreen && it->second->offscreen_handle) {
        WglBridge::Instance().DestroyOffscreenDc(it->second->hdc, it->second->offscreen_handle);
    }

    if (current_draw_ == surface)
        current_draw_ = EGL_NO_SURFACE;
    if (current_read_ == surface)
        current_read_ = EGL_NO_SURFACE;

    surfaces_.erase(it);
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLBoolean EglDispatcher::QuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                                       EGLint *value) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = surfaces_.find(surface);
    if (it == surfaces_.end() || !value) {
        last_error_ = EGL_BAD_SURFACE;
        return EGL_FALSE;
    }

    switch (attribute) {
    case EGL_WIDTH:
        *value = it->second->width;
        break;
    case EGL_HEIGHT:
        *value = it->second->height;
        break;
    case EGL_CONFIG_ID:
        *value = 1;
        break;
    default:
        *value = 0;
        break;
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLContext EglDispatcher::CreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context,
                                        const EGLint *attrib_list) noexcept
{
    (void)share_context;
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_NO_CONTEXT;
    }

    int client_version = 2;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_CONTEXT_CLIENT_VERSION) {
                client_version = attrib_list[i + 1];
            }
        }
    }

    // Allocate context with a helper DC
    HDC helper_dc = WglBridge::Instance().CreateOffscreenDc(64, 64, nullptr, nullptr);
    if (!helper_dc) {
        last_error_ = EGL_BAD_ALLOC;
        return EGL_NO_CONTEXT;
    }

    HGLRC hglrc = WglBridge::Instance().CreateContext(helper_dc, client_version);
    WglBridge::Instance().DestroyOffscreenDc(helper_dc, nullptr);

    if (!hglrc) {
        last_error_ = EGL_BAD_ALLOC;
        return EGL_NO_CONTEXT;
    }

    auto ctx_data = std::make_unique<EglContextData>();
    ctx_data->hglrc = hglrc;
    ctx_data->client_version = client_version;

    auto idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(config)) - 1;
    if (idx < configs_.size()) {
        ctx_data->config = configs_[idx];
    }

    EGLContext handle = reinterpret_cast<EGLContext>(ctx_data.get());
    contexts_[handle] = std::move(ctx_data);
    last_error_ = EGL_SUCCESS;
    return handle;
}

EGLBoolean EglDispatcher::DestroyContext(EGLDisplay dpy, EGLContext ctx) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = contexts_.find(ctx);
    if (it == contexts_.end()) {
        last_error_ = EGL_BAD_CONTEXT;
        return EGL_FALSE;
    }

    if (current_context_ == ctx) {
        WglBridge::Instance().MakeCurrent(nullptr, nullptr);
        current_context_ = EGL_NO_CONTEXT;
    }

    WglBridge::Instance().DeleteContext(it->second->hglrc);
    contexts_.erase(it);
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLBoolean EglDispatcher::MakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                                      EGLContext ctx) noexcept
{
    (void)read;
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE) {
        WglBridge::Instance().MakeCurrent(nullptr, nullptr);
        current_context_ = EGL_NO_CONTEXT;
        current_draw_ = EGL_NO_SURFACE;
        current_read_ = EGL_NO_SURFACE;
        last_error_ = EGL_SUCCESS;
        return EGL_TRUE;
    }

    auto ctx_it = contexts_.find(ctx);
    auto surf_it = surfaces_.find(draw);
    if (ctx_it == contexts_.end() || surf_it == surfaces_.end()) {
        last_error_ = EGL_BAD_MATCH;
        return EGL_FALSE;
    }

    if (!WglBridge::Instance().MakeCurrent(surf_it->second->hdc, ctx_it->second->hglrc)) {
        last_error_ = EGL_BAD_ACCESS;
        return EGL_FALSE;
    }

    current_context_ = ctx;
    current_draw_ = draw;
    current_read_ = read;
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLContext EglDispatcher::GetCurrentContext() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return current_context_;
}

EGLSurface EglDispatcher::GetCurrentSurface(EGLint readdraw) noexcept
{
    (void)readdraw;
    std::lock_guard<std::mutex> lock(mutex_);
    return current_draw_;
}

EGLDisplay EglDispatcher::GetCurrentDisplay() noexcept { return kSyntheticDisplay; }

EGLBoolean EglDispatcher::SwapBuffers(EGLDisplay dpy, EGLSurface surface) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = surfaces_.find(surface);
    if (it == surfaces_.end()) {
        last_error_ = EGL_BAD_SURFACE;
        return EGL_FALSE;
    }

    WglBridge::Instance().SwapBuffers(it->second->hdc);
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLint EglDispatcher::GetError() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    EGLint err = last_error_;
    last_error_ = EGL_SUCCESS;
    return err;
}

void *EglDispatcher::GetProcAddress(const char *procname) noexcept
{
    if (!procname)
        return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = procs_.find(procname);
    if (it != procs_.end()) {
        return it->second;
    }
    return nullptr;
}

void EglDispatcher::RegisterProc(std::string_view name, void *proc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    procs_[std::string(name)] = proc;
}

} // namespace portage::gles

// SysV ABI C Exports
extern "C" {

__attribute__((sysv_abi)) EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    return portage::gles::EglDispatcher::Instance().GetDisplay(display_id);
}

__attribute__((sysv_abi)) EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    return portage::gles::EglDispatcher::Instance().Initialize(dpy, major, minor);
}

__attribute__((sysv_abi)) EGLBoolean eglTerminate(EGLDisplay dpy)
{
    return portage::gles::EglDispatcher::Instance().Terminate(dpy);
}

__attribute__((sysv_abi)) const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    return portage::gles::EglDispatcher::Instance().QueryString(dpy, name);
}

__attribute__((sysv_abi)) EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                                   EGLint config_size, EGLint *num_config)
{
    return portage::gles::EglDispatcher::Instance().GetConfigs(dpy, configs, config_size,
                                                               num_config);
}

__attribute__((sysv_abi)) EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                                     EGLConfig *configs, EGLint config_size,
                                                     EGLint *num_config)
{
    return portage::gles::EglDispatcher::Instance().ChooseConfig(dpy, attrib_list, configs,
                                                                 config_size, num_config);
}

__attribute__((sysv_abi)) EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                        EGLint attribute, EGLint *value)
{
    return portage::gles::EglDispatcher::Instance().GetConfigAttrib(dpy, config, attribute, value);
}

__attribute__((sysv_abi)) EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                            EGLNativeWindowType win,
                                                            const EGLint *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreateWindowSurface(dpy, config, win,
                                                                        attrib_list);
}

__attribute__((sysv_abi)) EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                             const EGLint *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreatePbufferSurface(dpy, config, attrib_list);
}

__attribute__((sysv_abi)) EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    return portage::gles::EglDispatcher::Instance().DestroySurface(dpy, surface);
}

__attribute__((sysv_abi)) EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                                     EGLint attribute, EGLint *value)
{
    return portage::gles::EglDispatcher::Instance().QuerySurface(dpy, surface, attribute, value);
}

__attribute__((sysv_abi)) EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                                      EGLContext share_context,
                                                      const EGLint *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreateContext(dpy, config, share_context,
                                                                  attrib_list);
}

__attribute__((sysv_abi)) EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    return portage::gles::EglDispatcher::Instance().DestroyContext(dpy, ctx);
}

__attribute__((sysv_abi)) EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                                    EGLSurface read, EGLContext ctx)
{
    return portage::gles::EglDispatcher::Instance().MakeCurrent(dpy, draw, read, ctx);
}

__attribute__((sysv_abi)) EGLContext eglGetCurrentContext(void)
{
    return portage::gles::EglDispatcher::Instance().GetCurrentContext();
}

__attribute__((sysv_abi)) EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
    return portage::gles::EglDispatcher::Instance().GetCurrentSurface(readdraw);
}

__attribute__((sysv_abi)) EGLDisplay eglGetCurrentDisplay(void)
{
    return portage::gles::EglDispatcher::Instance().GetCurrentDisplay();
}

__attribute__((sysv_abi)) EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    return portage::gles::EglDispatcher::Instance().SwapBuffers(dpy, surface);
}

__attribute__((sysv_abi)) EGLint eglGetError(void)
{
    return portage::gles::EglDispatcher::Instance().GetError();
}

__attribute__((sysv_abi)) void *eglGetProcAddress(const char *procname)
{
    return portage::gles::EglDispatcher::Instance().GetProcAddress(procname);
}

} // extern "C"