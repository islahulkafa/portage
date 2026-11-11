#include "gles/egl_dispatcher.hpp"
#include "fs/fd_table.hpp"
#include "gles/gles_dispatcher.hpp"
#include "ndk/hardware_buffer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
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

    procs_["eglGetNativeClientBufferANDROID"] =
        reinterpret_cast<void *>(&eglGetNativeClientBufferANDROID);
    procs_["eglCreateImageKHR"] = reinterpret_cast<void *>(&eglCreateImageKHR);
    procs_["eglDestroyImageKHR"] = reinterpret_cast<void *>(&eglDestroyImageKHR);
    procs_["eglCreateSync"] = reinterpret_cast<void *>(&eglCreateSync);
    procs_["eglDestroySync"] = reinterpret_cast<void *>(&eglDestroySync);
    procs_["eglClientWaitSync"] = reinterpret_cast<void *>(&eglClientWaitSync);
    procs_["eglWaitSync"] = reinterpret_cast<void *>(&eglWaitSync);
    procs_["eglGetSyncAttrib"] = reinterpret_cast<void *>(&eglGetSyncAttrib);
    procs_["eglCreateSyncKHR"] = reinterpret_cast<void *>(&eglCreateSyncKHR);
    procs_["eglDestroySyncKHR"] = reinterpret_cast<void *>(&eglDestroySyncKHR);
    procs_["eglClientWaitSyncKHR"] = reinterpret_cast<void *>(&eglClientWaitSyncKHR);
    procs_["eglWaitSyncKHR"] = reinterpret_cast<void *>(&eglWaitSyncKHR);
    procs_["eglGetSyncAttribKHR"] = reinterpret_cast<void *>(&eglGetSyncAttribKHR);
    procs_["eglDupNativeFenceFDANDROID"] = reinterpret_cast<void *>(&eglDupNativeFenceFDANDROID);
    procs_["eglSwapInterval"] = reinterpret_cast<void *>(&eglSwapInterval);
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
            *minor = 5;
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

    const auto &tbl = WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteSync) {
        for (const auto &[_, sync] : syncs_) {
            if (sync && sync->gl_sync) {
                tbl.glDeleteSync(sync->gl_sync);
            }
        }
    }
    syncs_.clear();
    for (const auto &[_, img] : images_) {
        if (img && img->ahb && ndk::HardwareBufferManager::Instance().IsValid(img->ahb)) {
            ndk::HardwareBufferManager::Instance().Release(img->ahb);
        }
    }
    images_.clear();
    surfaces_.clear();
    contexts_.clear();
    current_context_ = EGL_NO_CONTEXT;
    current_draw_ = EGL_NO_SURFACE;
    current_read_ = EGL_NO_SURFACE;
    swap_interval_ = 1;
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
        return "1.5 Portage GLES/WGL Passthrough";
    case EGL_EXTENSIONS:
        return "EGL_KHR_image_base EGL_KHR_gl_texture_2D_image EGL_KHR_fence_sync "
               "EGL_KHR_wait_sync EGL_ANDROID_native_fence_sync "
               "EGL_ANDROID_get_native_client_buffer EGL_ANDROID_image_native_buffer";
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

    if (native_window && native_window->native_handle != nullptr) {
        HWND hwnd = reinterpret_cast<HWND>(native_window->native_handle);
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            WglBridge::Instance().SetupWindowPixelFormat(hdc);
            surface_data->is_offscreen = false;
            surface_data->hwnd = hwnd;
            surface_data->hdc = hdc;
        }
    }

    if (!surface_data->hdc) {
        surface_data->is_offscreen = true;
        surface_data->hdc = WglBridge::Instance().CreateOffscreenDc(
            width, height, &surface_data->offscreen_handle, nullptr);
    }

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
    } else if (!it->second->is_offscreen && it->second->hwnd && it->second->hdc) {
        ReleaseDC(it->second->hwnd, it->second->hdc);
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

EGLBoolean EglDispatcher::SwapInterval(EGLDisplay dpy, EGLint interval) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    swap_interval_ = interval;
    const auto &tbl = WglBridge::Instance().GetDispatchTable();
    if (tbl.wglSwapIntervalEXT) {
        tbl.wglSwapIntervalEXT(interval);
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

namespace {
EGLSync CreateSyncCommon(std::mutex &mutex, EGLint &last_error,
                         std::unordered_map<EGLSync, std::unique_ptr<EglSyncData>> &syncs,
                         EGLDisplay dpy, EGLenum type, EGLint condition, int initial_fd,
                         fs::FileDescriptorTable *fd_table) noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    if (dpy != kSyntheticDisplay) {
        last_error = EGL_BAD_DISPLAY;
        return EGL_NO_SYNC;
    }

    if (type != EGL_SYNC_FENCE && type != EGL_SYNC_NATIVE_FENCE_ANDROID) {
        last_error = EGL_BAD_ATTRIBUTE;
        return EGL_NO_SYNC;
    }

    auto data = std::make_unique<EglSyncData>();
    data->type = type;
    data->condition = condition;
    data->native_fence_fd = initial_fd;

    const auto &tbl = WglBridge::Instance().GetDispatchTable();
    if (initial_fd >= 0 && fd_table) {
        auto desc = fd_table->Get(initial_fd);
        if (desc && desc->dev == fs::DeviceType::SyncFence && desc->sync_fence) {
            data->gl_sync = static_cast<GLsync>(desc->sync_fence->gl_sync);
            data->status = desc->sync_fence->IsSignaled() ? EGL_SIGNALED : EGL_UNSIGNALED;
        } else {
            data->status = EGL_SIGNALED;
        }
    } else if (tbl.glFenceSync && tbl.wglGetCurrentContext &&
               tbl.wglGetCurrentContext() != nullptr) {
        data->gl_sync = tbl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        data->status = data->gl_sync ? EGL_UNSIGNALED : EGL_SIGNALED;
    } else {
        data->status = EGL_SIGNALED;
    }

    EGLSync handle = reinterpret_cast<EGLSync>(data.get());
    syncs[handle] = std::move(data);
    last_error = EGL_SUCCESS;
    return handle;
}
} // namespace

EGLSync EglDispatcher::CreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list) noexcept
{
    EGLint condition = EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
    int initial_fd = EGL_NO_NATIVE_FENCE_FD_ANDROID;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_SYNC_CONDITION) {
                condition = static_cast<EGLint>(attrib_list[i + 1]);
            } else if (attrib_list[i] == EGL_SYNC_NATIVE_FENCE_FD_ANDROID) {
                initial_fd = static_cast<int>(attrib_list[i + 1]);
            }
        }
    }
    fs::FileDescriptorTable *fd_table =
        fd_table_override_ ? fd_table_override_ : syscall::SyscallDispatcher::GetFdTable();
    return CreateSyncCommon(mutex_, last_error_, syncs_, dpy, type, condition, initial_fd, fd_table);
}

EGLSyncKHR EglDispatcher::CreateSyncKHR(EGLDisplay dpy, EGLenum type,
                                        const EGLint *attrib_list) noexcept
{
    EGLint condition = EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
    int initial_fd = EGL_NO_NATIVE_FENCE_FD_ANDROID;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_SYNC_CONDITION) {
                condition = attrib_list[i + 1];
            } else if (attrib_list[i] == EGL_SYNC_NATIVE_FENCE_FD_ANDROID) {
                initial_fd = attrib_list[i + 1];
            }
        }
    }
    fs::FileDescriptorTable *fd_table =
        fd_table_override_ ? fd_table_override_ : syscall::SyscallDispatcher::GetFdTable();
    return CreateSyncCommon(mutex_, last_error_, syncs_, dpy, type, condition, initial_fd, fd_table);
}

EGLBoolean EglDispatcher::DestroySync(EGLDisplay dpy, EGLSync sync) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = syncs_.find(sync);
    if (it == syncs_.end()) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    const auto &tbl = WglBridge::Instance().GetDispatchTable();
    if (it->second->gl_sync && it->second->native_fence_fd < 0 && tbl.glDeleteSync) {
        tbl.glDeleteSync(it->second->gl_sync);
    }

    syncs_.erase(it);
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLint EglDispatcher::ClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                                     EGLTime timeout) noexcept
{
    GLsync gl_sync = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (dpy != kSyntheticDisplay) {
            last_error_ = EGL_BAD_DISPLAY;
            return EGL_FALSE;
        }
        auto it = syncs_.find(sync);
        if (it == syncs_.end()) {
            last_error_ = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        if (it->second->status == EGL_SIGNALED) {
            last_error_ = EGL_SUCCESS;
            return EGL_CONDITION_SATISFIED;
        }
        gl_sync = it->second->gl_sync;
    }

    EGLint result = EGL_CONDITION_SATISFIED;
    if (gl_sync) {
        const auto &tbl = WglBridge::Instance().GetDispatchTable();
        if (tbl.glClientWaitSync) {
            GLbitfield gl_flags =
                (flags & EGL_SYNC_FLUSH_COMMANDS_BIT) ? GL_SYNC_FLUSH_COMMANDS_BIT : 0;
            GLenum wait_res = tbl.glClientWaitSync(gl_sync, gl_flags, timeout);
            if (wait_res == GL_TIMEOUT_EXPIRED) {
                result = EGL_TIMEOUT_EXPIRED;
            } else if (wait_res == GL_WAIT_FAILED) {
                result = EGL_FALSE;
            } else {
                result = EGL_CONDITION_SATISFIED;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = syncs_.find(sync);
    if (it != syncs_.end() && result == EGL_CONDITION_SATISFIED) {
        it->second->status = EGL_SIGNALED;
    }
    last_error_ = (result == EGL_FALSE) ? EGL_BAD_PARAMETER : EGL_SUCCESS;
    return result;
}

EGLBoolean EglDispatcher::WaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }
    if (flags != 0) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }
    auto it = syncs_.find(sync);
    if (it == syncs_.end()) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    if (it->second->gl_sync) {
        const auto &tbl = WglBridge::Instance().GetDispatchTable();
        if (tbl.glWaitSync) {
            tbl.glWaitSync(it->second->gl_sync, 0, GL_TIMEOUT_IGNORED);
        }
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLBoolean EglDispatcher::GetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                                        EGLAttrib *value) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }
    if (!value) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }
    auto it = syncs_.find(sync);
    if (it == syncs_.end()) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    if (it->second->status == EGL_UNSIGNALED && it->second->gl_sync) {
        const auto &tbl = WglBridge::Instance().GetDispatchTable();
        if (tbl.glClientWaitSync) {
            GLenum res = tbl.glClientWaitSync(it->second->gl_sync, 0, 0);
            if (res == GL_ALREADY_SIGNALED || res == GL_CONDITION_SATISFIED) {
                it->second->status = EGL_SIGNALED;
            }
        }
    }

    switch (attribute) {
    case EGL_SYNC_TYPE:
        *value = static_cast<EGLAttrib>(it->second->type);
        break;
    case EGL_SYNC_STATUS:
        *value = static_cast<EGLAttrib>(it->second->status);
        break;
    case EGL_SYNC_CONDITION:
        *value = static_cast<EGLAttrib>(it->second->condition);
        break;
    default:
        last_error_ = EGL_BAD_ATTRIBUTE;
        return EGL_FALSE;
    }

    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

EGLint EglDispatcher::DupNativeFenceFDANDROID(EGLDisplay dpy, EGLSyncKHR sync) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_NO_NATIVE_FENCE_FD_ANDROID;
    }
    auto it = syncs_.find(sync);
    if (it == syncs_.end()) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_NO_NATIVE_FENCE_FD_ANDROID;
    }

    if (it->second->status == EGL_UNSIGNALED && it->second->gl_sync) {
        const auto &tbl = WglBridge::Instance().GetDispatchTable();
        if (tbl.glClientWaitSync) {
            GLenum res = tbl.glClientWaitSync(it->second->gl_sync, 0, 0);
            if (res == GL_ALREADY_SIGNALED || res == GL_CONDITION_SATISFIED) {
                it->second->status = EGL_SIGNALED;
            }
        }
    }

    fs::FileDescriptorTable *fd_table =
        fd_table_override_ ? fd_table_override_ : syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table) {
        last_error_ = EGL_BAD_ALLOC;
        return EGL_NO_NATIVE_FENCE_FD_ANDROID;
    }

    int fd = fd_table->AllocateSyncFence(it->second->gl_sync, nullptr,
                                         it->second->status == EGL_SIGNALED, 0);
    if (fd < 0) {
        last_error_ = EGL_BAD_ALLOC;
        return EGL_NO_NATIVE_FENCE_FD_ANDROID;
    }

    last_error_ = EGL_SUCCESS;
    return fd;
}

EGLImageKHR EglDispatcher::CreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                                          EGLClientBuffer buffer,
                                          const EGLint *attrib_list) noexcept
{
    (void)ctx;
    (void)attrib_list;
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != EGL_NO_DISPLAY && dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_NO_IMAGE_KHR;
    }
    if (!buffer) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_NO_IMAGE_KHR;
    }

    auto img = std::make_unique<EglImageData>();
    img->target = target;
    img->client_buffer = buffer;

    if (target == EGL_NATIVE_BUFFER_ANDROID) {
        auto *ahb = reinterpret_cast<AHardwareBuffer *>(buffer);
        if (ndk::HardwareBufferManager::Instance().IsValid(ahb)) {
            ndk::HardwareBufferManager::Instance().Acquire(ahb);
            img->ahb = ahb;
        }
    } else if (target == EGL_GL_TEXTURE_2D_KHR) {
        img->gl_texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(buffer));
    }

    EGLImageKHR handle = reinterpret_cast<EGLImageKHR>(img.get());
    images_[handle] = std::move(img);
    last_error_ = EGL_SUCCESS;
    return handle;
}

EGLBoolean EglDispatcher::DestroyImageKHR(EGLDisplay dpy, EGLImageKHR image) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dpy != EGL_NO_DISPLAY && dpy != kSyntheticDisplay) {
        last_error_ = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    auto it = images_.find(image);
    if (it == images_.end()) {
        last_error_ = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    if (it->second->ahb && ndk::HardwareBufferManager::Instance().IsValid(it->second->ahb)) {
        ndk::HardwareBufferManager::Instance().Release(it->second->ahb);
    }

    images_.erase(it);
    last_error_ = EGL_SUCCESS;
    return EGL_TRUE;
}

const EglImageData *EglDispatcher::FindImage(EGLImageKHR image) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(image);
    if (it == images_.end()) {
        return nullptr;
    }
    return it->second.get();
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

__attribute__((sysv_abi)) EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    return portage::gles::EglDispatcher::Instance().SwapInterval(dpy, interval);
}

__attribute__((sysv_abi)) EGLint eglGetError(void)
{
    return portage::gles::EglDispatcher::Instance().GetError();
}

__attribute__((sysv_abi)) void *eglGetProcAddress(const char *procname)
{
    return portage::gles::EglDispatcher::Instance().GetProcAddress(procname);
}

__attribute__((sysv_abi)) EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type,
                                                 const EGLAttrib *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreateSync(dpy, type, attrib_list);
}

__attribute__((sysv_abi)) EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync)
{
    return portage::gles::EglDispatcher::Instance().DestroySync(dpy, sync);
}

__attribute__((sysv_abi)) EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                                                   EGLTime timeout)
{
    return portage::gles::EglDispatcher::Instance().ClientWaitSync(dpy, sync, flags, timeout);
}

__attribute__((sysv_abi)) EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags)
{
    return portage::gles::EglDispatcher::Instance().WaitSync(dpy, sync, flags);
}

__attribute__((sysv_abi)) EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                                                      EGLAttrib *value)
{
    return portage::gles::EglDispatcher::Instance().GetSyncAttrib(dpy, sync, attribute, value);
}

__attribute__((sysv_abi)) EGLSyncKHR eglCreateSyncKHR(EGLDisplay dpy, EGLenum type,
                                                       const EGLint *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreateSyncKHR(dpy, type, attrib_list);
}

__attribute__((sysv_abi)) EGLBoolean eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
{
    return portage::gles::EglDispatcher::Instance().DestroySync(dpy, sync);
}

__attribute__((sysv_abi)) EGLint eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags,
                                                      EGLTimeKHR timeout)
{
    return portage::gles::EglDispatcher::Instance().ClientWaitSync(dpy, sync, flags, timeout);
}

__attribute__((sysv_abi)) EGLBoolean eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags)
{
    return portage::gles::EglDispatcher::Instance().WaitSync(dpy, sync, flags);
}

__attribute__((sysv_abi)) EGLBoolean eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                                         EGLint attribute, EGLint *value)
{
    EGLAttrib attr_val = 0;
    EGLBoolean ok =
        portage::gles::EglDispatcher::Instance().GetSyncAttrib(dpy, sync, attribute, &attr_val);
    if (ok && value) {
        *value = static_cast<EGLint>(attr_val);
    }
    return ok;
}

__attribute__((sysv_abi)) EGLint eglDupNativeFenceFDANDROID(EGLDisplay dpy, EGLSyncKHR sync)
{
    return portage::gles::EglDispatcher::Instance().DupNativeFenceFDANDROID(dpy, sync);
}

__attribute__((sysv_abi)) EGLClientBuffer
eglGetNativeClientBufferANDROID(const struct AHardwareBuffer *buffer)
{
    return reinterpret_cast<EGLClientBuffer>(const_cast<struct AHardwareBuffer *>(buffer));
}

__attribute__((sysv_abi)) EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx,
                                                        EGLenum target, EGLClientBuffer buffer,
                                                        const EGLint *attrib_list)
{
    return portage::gles::EglDispatcher::Instance().CreateImageKHR(dpy, ctx, target, buffer,
                                                                   attrib_list);
}

__attribute__((sysv_abi)) EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
    return portage::gles::EglDispatcher::Instance().DestroyImageKHR(dpy, image);
}

} // extern "C"