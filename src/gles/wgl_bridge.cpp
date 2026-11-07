#include "gles/wgl_bridge.hpp"
#include <iostream>

namespace portage::gles {

namespace {

template <typename T> T LoadProc(HMODULE mod, const char *name)
{
    if (!mod)
        return nullptr;
    return reinterpret_cast<T>(GetProcAddress(mod, name));
}

template <typename T>
T LoadWglProc(PFN_wglGetProcAddress wglGetProcAddress, HMODULE mod, const char *name)
{
    if (wglGetProcAddress) {
        PROC proc = wglGetProcAddress(name);
        if (proc)
            return reinterpret_cast<T>(proc);
    }
    if (mod) {
        FARPROC proc = GetProcAddress(mod, name);
        if (proc)
            return reinterpret_cast<T>(proc);
    }
    return nullptr;
}

} // namespace

WglBridge &WglBridge::Instance() noexcept
{
    static WglBridge instance;
    return instance;
}

bool WglBridge::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_)
        return true;

    opengl_module_ = LoadLibraryA("opengl32.dll");
    if (!opengl_module_) {
        return false;
    }

    // Standard WGL functions
    table_.wglCreateContext = LoadProc<PFN_wglCreateContext>(opengl_module_, "wglCreateContext");
    table_.wglDeleteContext = LoadProc<PFN_wglDeleteContext>(opengl_module_, "wglDeleteContext");
    table_.wglMakeCurrent = LoadProc<PFN_wglMakeCurrent>(opengl_module_, "wglMakeCurrent");
    table_.wglGetProcAddress = LoadProc<PFN_wglGetProcAddress>(opengl_module_, "wglGetProcAddress");
    table_.wglGetCurrentContext =
        LoadProc<PFN_wglGetCurrentContext>(opengl_module_, "wglGetCurrentContext");
    table_.wglGetCurrentDC = LoadProc<PFN_wglGetCurrentDC>(opengl_module_, "wglGetCurrentDC");

    // Standard Core OpenGL 1.1 functions
    table_.glClear = LoadProc<PFN_glClear>(opengl_module_, "glClear");
    table_.glClearColor = LoadProc<PFN_glClearColor>(opengl_module_, "glClearColor");
    table_.glViewport = LoadProc<PFN_glViewport>(opengl_module_, "glViewport");
    table_.glEnable = LoadProc<PFN_glEnable>(opengl_module_, "glEnable");
    table_.glDisable = LoadProc<PFN_glDisable>(opengl_module_, "glDisable");
    table_.glDrawArrays = LoadProc<PFN_glDrawArrays>(opengl_module_, "glDrawArrays");
    table_.glDrawElements = LoadProc<PFN_glDrawElements>(opengl_module_, "glDrawElements");
    table_.glGetError = LoadProc<PFN_glGetError>(opengl_module_, "glGetError");
    table_.glGetString = LoadProc<PFN_glGetString>(opengl_module_, "glGetString");
    table_.glReadPixels = LoadProc<PFN_glReadPixels>(opengl_module_, "glReadPixels");
    table_.glGenTextures = LoadProc<PFN_glGenTextures>(opengl_module_, "glGenTextures");
    table_.glBindTexture = LoadProc<PFN_glBindTexture>(opengl_module_, "glBindTexture");
    table_.glDeleteTextures = LoadProc<PFN_glDeleteTextures>(opengl_module_, "glDeleteTextures");
    table_.glTexImage2D = LoadProc<PFN_glTexImage2D>(opengl_module_, "glTexImage2D");
    table_.glTexParameteri = LoadProc<PFN_glTexParameteri>(opengl_module_, "glTexParameteri");

    // Register dummy window class for extension loading
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "PortageWglDummyClass";
    RegisterClassA(&wc);

    dummy_hwnd_ = CreateWindowA("PortageWglDummyClass", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (dummy_hwnd_) {
        dummy_hdc_ = GetDC(dummy_hwnd_);
        if (dummy_hdc_) {
            if (SetupWindowPixelFormat(dummy_hdc_)) {
                dummy_hglrc_ = table_.wglCreateContext(dummy_hdc_);
                if (dummy_hglrc_) {
                    table_.wglMakeCurrent(dummy_hdc_, dummy_hglrc_);
                }
            }
        }
    }

    // Resolve OpenGL 2.0+ shader functions
    auto load_fn = [this](const char *name) {
        return LoadWglProc<PROC>(table_.wglGetProcAddress, opengl_module_, name);
    };

    table_.glCreateShader = reinterpret_cast<PFN_glCreateShader>(load_fn("glCreateShader"));
    table_.glShaderSource = reinterpret_cast<PFN_glShaderSource>(load_fn("glShaderSource"));
    table_.glCompileShader = reinterpret_cast<PFN_glCompileShader>(load_fn("glCompileShader"));
    table_.glGetShaderiv = reinterpret_cast<PFN_glGetShaderiv>(load_fn("glGetShaderiv"));
    table_.glGetShaderInfoLog =
        reinterpret_cast<PFN_glGetShaderInfoLog>(load_fn("glGetShaderInfoLog"));
    table_.glDeleteShader = reinterpret_cast<PFN_glDeleteShader>(load_fn("glDeleteShader"));
    table_.glCreateProgram = reinterpret_cast<PFN_glCreateProgram>(load_fn("glCreateProgram"));
    table_.glAttachShader = reinterpret_cast<PFN_glAttachShader>(load_fn("glAttachShader"));
    table_.glLinkProgram = reinterpret_cast<PFN_glLinkProgram>(load_fn("glLinkProgram"));
    table_.glGetProgramiv = reinterpret_cast<PFN_glGetProgramiv>(load_fn("glGetProgramiv"));
    table_.glGetProgramInfoLog =
        reinterpret_cast<PFN_glGetProgramInfoLog>(load_fn("glGetProgramInfoLog"));
    table_.glUseProgram = reinterpret_cast<PFN_glUseProgram>(load_fn("glUseProgram"));
    table_.glDeleteProgram = reinterpret_cast<PFN_glDeleteProgram>(load_fn("glDeleteProgram"));

    // Attributes & Uniforms
    table_.glGetAttribLocation =
        reinterpret_cast<PFN_glGetAttribLocation>(load_fn("glGetAttribLocation"));
    table_.glEnableVertexAttribArray =
        reinterpret_cast<PFN_glEnableVertexAttribArray>(load_fn("glEnableVertexAttribArray"));
    table_.glDisableVertexAttribArray =
        reinterpret_cast<PFN_glDisableVertexAttribArray>(load_fn("glDisableVertexAttribArray"));
    table_.glVertexAttribPointer =
        reinterpret_cast<PFN_glVertexAttribPointer>(load_fn("glVertexAttribPointer"));
    table_.glGetUniformLocation =
        reinterpret_cast<PFN_glGetUniformLocation>(load_fn("glGetUniformLocation"));
    table_.glUniform1i = reinterpret_cast<PFN_glUniform1i>(load_fn("glUniform1i"));
    table_.glUniform1f = reinterpret_cast<PFN_glUniform1f>(load_fn("glUniform1f"));
    table_.glUniform4f = reinterpret_cast<PFN_glUniform4f>(load_fn("glUniform4f"));
    table_.glUniformMatrix4fv =
        reinterpret_cast<PFN_glUniformMatrix4fv>(load_fn("glUniformMatrix4fv"));

    // Buffers & VAOs
    table_.glGenBuffers = reinterpret_cast<PFN_glGenBuffers>(load_fn("glGenBuffers"));
    table_.glBindBuffer = reinterpret_cast<PFN_glBindBuffer>(load_fn("glBindBuffer"));
    table_.glBufferData = reinterpret_cast<PFN_glBufferData>(load_fn("glBufferData"));
    table_.glBufferSubData = reinterpret_cast<PFN_glBufferSubData>(load_fn("glBufferSubData"));
    table_.glDeleteBuffers = reinterpret_cast<PFN_glDeleteBuffers>(load_fn("glDeleteBuffers"));

    table_.glGenVertexArrays =
        reinterpret_cast<PFN_glGenVertexArrays>(load_fn("glGenVertexArrays"));
    if (!table_.glGenVertexArrays) {
        table_.glGenVertexArrays =
            reinterpret_cast<PFN_glGenVertexArrays>(load_fn("glGenVertexArraysOES"));
    }
    table_.glBindVertexArray =
        reinterpret_cast<PFN_glBindVertexArray>(load_fn("glBindVertexArray"));
    if (!table_.glBindVertexArray) {
        table_.glBindVertexArray =
            reinterpret_cast<PFN_glBindVertexArray>(load_fn("glBindVertexArrayOES"));
    }
    table_.glDeleteVertexArrays =
        reinterpret_cast<PFN_glDeleteVertexArrays>(load_fn("glDeleteVertexArrays"));
    if (!table_.glDeleteVertexArrays) {
        table_.glDeleteVertexArrays =
            reinterpret_cast<PFN_glDeleteVertexArrays>(load_fn("glDeleteVertexArraysOES"));
    }

    // Textures & Framebuffers
    table_.glActiveTexture = reinterpret_cast<PFN_glActiveTexture>(load_fn("glActiveTexture"));
    table_.glGenFramebuffers =
        reinterpret_cast<PFN_glGenFramebuffers>(load_fn("glGenFramebuffers"));
    if (!table_.glGenFramebuffers) {
        table_.glGenFramebuffers =
            reinterpret_cast<PFN_glGenFramebuffers>(load_fn("glGenFramebuffersEXT"));
    }
    table_.glBindFramebuffer =
        reinterpret_cast<PFN_glBindFramebuffer>(load_fn("glBindFramebuffer"));
    if (!table_.glBindFramebuffer) {
        table_.glBindFramebuffer =
            reinterpret_cast<PFN_glBindFramebuffer>(load_fn("glBindFramebufferEXT"));
    }
    table_.glFramebufferTexture2D =
        reinterpret_cast<PFN_glFramebufferTexture2D>(load_fn("glFramebufferTexture2D"));
    if (!table_.glFramebufferTexture2D) {
        table_.glFramebufferTexture2D =
            reinterpret_cast<PFN_glFramebufferTexture2D>(load_fn("glFramebufferTexture2DEXT"));
    }
    table_.glCheckFramebufferStatus =
        reinterpret_cast<PFN_glCheckFramebufferStatus>(load_fn("glCheckFramebufferStatus"));
    if (!table_.glCheckFramebufferStatus) {
        table_.glCheckFramebufferStatus =
            reinterpret_cast<PFN_glCheckFramebufferStatus>(load_fn("glCheckFramebufferStatusEXT"));
    }
    table_.glDeleteFramebuffers =
        reinterpret_cast<PFN_glDeleteFramebuffers>(load_fn("glDeleteFramebuffers"));
    if (!table_.glDeleteFramebuffers) {
        table_.glDeleteFramebuffers =
            reinterpret_cast<PFN_glDeleteFramebuffers>(load_fn("glDeleteFramebuffersEXT"));
    }

    if (table_.wglMakeCurrent) {
        table_.wglMakeCurrent(nullptr, nullptr);
    }

    initialized_ = true;
    return true;
}

void WglBridge::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_)
        return;

    if (dummy_hdc_ && dummy_hglrc_) {
        table_.wglMakeCurrent(nullptr, nullptr);
        table_.wglDeleteContext(dummy_hglrc_);
        dummy_hglrc_ = nullptr;
    }
    if (dummy_hwnd_ && dummy_hdc_) {
        ReleaseDC(dummy_hwnd_, dummy_hdc_);
        dummy_hdc_ = nullptr;
    }
    if (dummy_hwnd_) {
        DestroyWindow(dummy_hwnd_);
        dummy_hwnd_ = nullptr;
    }

    if (opengl_module_) {
        FreeLibrary(opengl_module_);
        opengl_module_ = nullptr;
    }

    table_ = {};
    initialized_ = false;
}

bool WglBridge::SetupWindowPixelFormat(HDC hdc) noexcept
{
    if (!hdc)
        return false;

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cRedBits = 8;
    pfd.cGreenBits = 8;
    pfd.cBlueBits = 8;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (format == 0)
        return false;

    return SetPixelFormat(hdc, format, &pfd) != FALSE;
}

HDC WglBridge::CreateOffscreenDc(int32_t width, int32_t height, HBITMAP *out_bitmap,
                                 void **out_bits) noexcept
{
    // Create a hidden HWND to ensure full GPU hardware acceleration and valid WGL context creation
    HWND hwnd = CreateWindowA("PortageWglDummyClass", "Offscreen", WS_POPUP, 0, 0,
                              (width > 0 ? width : 64), (height > 0 ? height : 64), nullptr,
                              nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!hwnd)
        return nullptr;

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        DestroyWindow(hwnd);
        return nullptr;
    }

    if (!SetupWindowPixelFormat(hdc)) {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return nullptr;
    }

    if (out_bitmap)
        *out_bitmap = reinterpret_cast<HBITMAP>(hwnd);
    if (out_bits)
        *out_bits = nullptr;
    return hdc;
}

void WglBridge::DestroyOffscreenDc(HDC hdc, HBITMAP bitmap) noexcept
{
    if (bitmap) {
        HWND hwnd = reinterpret_cast<HWND>(bitmap);
        if (hdc)
            ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
    }
}

HGLRC WglBridge::CreateContext(HDC hdc, int gles_version) noexcept
{
    (void)gles_version;
    if (!table_.wglCreateContext || !hdc)
        return nullptr;
    return table_.wglCreateContext(hdc);
}

bool WglBridge::MakeCurrent(HDC hdc, HGLRC hglrc) noexcept
{
    if (!table_.wglMakeCurrent)
        return false;
    return table_.wglMakeCurrent(hdc, hglrc) != FALSE;
}

bool WglBridge::DeleteContext(HGLRC hglrc) noexcept
{
    if (!table_.wglDeleteContext || !hglrc)
        return false;
    return table_.wglDeleteContext(hglrc) != FALSE;
}

bool WglBridge::SwapBuffers(HDC hdc) noexcept
{
    if (!hdc)
        return false;
    return ::SwapBuffers(hdc) != FALSE;
}

PROC WglBridge::GetProcAddress(const char *name) noexcept
{
    if (!name)
        return nullptr;
    return LoadWglProc<PROC>(table_.wglGetProcAddress, opengl_module_, name);
}

} // namespace portage::gles