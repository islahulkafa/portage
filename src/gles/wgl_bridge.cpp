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
    table_.wglShareLists = LoadProc<PFN_wglShareLists>(opengl_module_, "wglShareLists");

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
    table_.glPixelStorei = LoadProc<PFN_glPixelStorei>(opengl_module_, "glPixelStorei");

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

    table_.wglSwapIntervalEXT =
        reinterpret_cast<PFN_wglSwapIntervalEXT>(load_fn("wglSwapIntervalEXT"));

    // Sync Fences
    table_.glFenceSync = reinterpret_cast<PFN_glFenceSync>(load_fn("glFenceSync"));
    table_.glIsSync = reinterpret_cast<PFN_glIsSync>(load_fn("glIsSync"));
    table_.glDeleteSync = reinterpret_cast<PFN_glDeleteSync>(load_fn("glDeleteSync"));
    table_.glClientWaitSync = reinterpret_cast<PFN_glClientWaitSync>(load_fn("glClientWaitSync"));
    table_.glWaitSync = reinterpret_cast<PFN_glWaitSync>(load_fn("glWaitSync"));
    table_.glGetSynciv = reinterpret_cast<PFN_glGetSynciv>(load_fn("glGetSynciv"));

    // UBOs
    table_.glBindBufferBase = reinterpret_cast<PFN_glBindBufferBase>(load_fn("glBindBufferBase"));
    table_.glBindBufferRange =
        reinterpret_cast<PFN_glBindBufferRange>(load_fn("glBindBufferRange"));
    table_.glGetUniformBlockIndex =
        reinterpret_cast<PFN_glGetUniformBlockIndex>(load_fn("glGetUniformBlockIndex"));
    table_.glUniformBlockBinding =
        reinterpret_cast<PFN_glUniformBlockBinding>(load_fn("glUniformBlockBinding"));

    // Instancing
    table_.glDrawArraysInstanced =
        reinterpret_cast<PFN_glDrawArraysInstanced>(load_fn("glDrawArraysInstanced"));
    table_.glDrawElementsInstanced =
        reinterpret_cast<PFN_glDrawElementsInstanced>(load_fn("glDrawElementsInstanced"));
    table_.glVertexAttribDivisor =
        reinterpret_cast<PFN_glVertexAttribDivisor>(load_fn("glVertexAttribDivisor"));

    // Transform Feedback
    table_.glGenTransformFeedbacks =
        reinterpret_cast<PFN_glGenTransformFeedbacks>(load_fn("glGenTransformFeedbacks"));
    table_.glBindTransformFeedback =
        reinterpret_cast<PFN_glBindTransformFeedback>(load_fn("glBindTransformFeedback"));
    table_.glBeginTransformFeedback =
        reinterpret_cast<PFN_glBeginTransformFeedback>(load_fn("glBeginTransformFeedback"));
    table_.glEndTransformFeedback =
        reinterpret_cast<PFN_glEndTransformFeedback>(load_fn("glEndTransformFeedback"));
    table_.glTransformFeedbackVaryings =
        reinterpret_cast<PFN_glTransformFeedbackVaryings>(load_fn("glTransformFeedbackVaryings"));
    table_.glDeleteTransformFeedbacks =
        reinterpret_cast<PFN_glDeleteTransformFeedbacks>(load_fn("glDeleteTransformFeedbacks"));

    // Sampler Objects
    table_.glGenSamplers = reinterpret_cast<PFN_glGenSamplers>(load_fn("glGenSamplers"));
    table_.glBindSampler = reinterpret_cast<PFN_glBindSampler>(load_fn("glBindSampler"));
    table_.glSamplerParameteri =
        reinterpret_cast<PFN_glSamplerParameteri>(load_fn("glSamplerParameteri"));
    table_.glDeleteSamplers = reinterpret_cast<PFN_glDeleteSamplers>(load_fn("glDeleteSamplers"));

    // Buffer Mapping
    table_.glMapBufferRange = reinterpret_cast<PFN_glMapBufferRange>(load_fn("glMapBufferRange"));
    table_.glUnmapBuffer = reinterpret_cast<PFN_glUnmapBuffer>(load_fn("glUnmapBuffer"));
    table_.glFlushMappedBufferRange =
        reinterpret_cast<PFN_glFlushMappedBufferRange>(load_fn("glFlushMappedBufferRange"));

    // Texture Storage & 3D Textures
    table_.glTexStorage2D = reinterpret_cast<PFN_glTexStorage2D>(load_fn("glTexStorage2D"));
    table_.glTexStorage3D = reinterpret_cast<PFN_glTexStorage3D>(load_fn("glTexStorage3D"));
    table_.glTexImage3D = reinterpret_cast<PFN_glTexImage3D>(load_fn("glTexImage3D"));
    table_.glTexSubImage3D = reinterpret_cast<PFN_glTexSubImage3D>(load_fn("glTexSubImage3D"));

    // Compute & Barriers
    table_.glDispatchCompute =
        reinterpret_cast<PFN_glDispatchCompute>(load_fn("glDispatchCompute"));
    table_.glMemoryBarrier = reinterpret_cast<PFN_glMemoryBarrier>(load_fn("glMemoryBarrier"));

    // WGL ARB & Desktop Depth Fallbacks
    table_.wglCreateContextAttribsARB = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
        load_fn("wglCreateContextAttribsARB"));
    table_.wglGetExtensionsStringARB = reinterpret_cast<PFN_wglGetExtensionsStringARB>(
        load_fn("wglGetExtensionsStringARB"));
    table_.glClearDepth = reinterpret_cast<PFN_glClearDepth>(load_fn("glClearDepth"));
    table_.glDepthRange = reinterpret_cast<PFN_glDepthRange>(load_fn("glDepthRange"));

    // Pipeline State
    table_.glDepthFunc = reinterpret_cast<PFN_glDepthFunc>(load_fn("glDepthFunc"));
    table_.glDepthMask = reinterpret_cast<PFN_glDepthMask>(load_fn("glDepthMask"));
    table_.glClearDepthf = reinterpret_cast<PFN_glClearDepthf>(load_fn("glClearDepthf"));
    table_.glDepthRangef = reinterpret_cast<PFN_glDepthRangef>(load_fn("glDepthRangef"));
    table_.glColorMask = reinterpret_cast<PFN_glColorMask>(load_fn("glColorMask"));
    table_.glBlendFunc = reinterpret_cast<PFN_glBlendFunc>(load_fn("glBlendFunc"));
    table_.glBlendFuncSeparate =
        reinterpret_cast<PFN_glBlendFuncSeparate>(load_fn("glBlendFuncSeparate"));
    table_.glBlendEquation = reinterpret_cast<PFN_glBlendEquation>(load_fn("glBlendEquation"));
    table_.glBlendEquationSeparate =
        reinterpret_cast<PFN_glBlendEquationSeparate>(load_fn("glBlendEquationSeparate"));
    table_.glBlendColor = reinterpret_cast<PFN_glBlendColor>(load_fn("glBlendColor"));
    table_.glCullFace = reinterpret_cast<PFN_glCullFace>(load_fn("glCullFace"));
    table_.glFrontFace = reinterpret_cast<PFN_glFrontFace>(load_fn("glFrontFace"));
    table_.glScissor = reinterpret_cast<PFN_glScissor>(load_fn("glScissor"));
    table_.glStencilFunc = reinterpret_cast<PFN_glStencilFunc>(load_fn("glStencilFunc"));
    table_.glStencilFuncSeparate =
        reinterpret_cast<PFN_glStencilFuncSeparate>(load_fn("glStencilFuncSeparate"));
    table_.glStencilOp = reinterpret_cast<PFN_glStencilOp>(load_fn("glStencilOp"));
    table_.glStencilOpSeparate =
        reinterpret_cast<PFN_glStencilOpSeparate>(load_fn("glStencilOpSeparate"));
    table_.glStencilMask = reinterpret_cast<PFN_glStencilMask>(load_fn("glStencilMask"));
    table_.glStencilMaskSeparate =
        reinterpret_cast<PFN_glStencilMaskSeparate>(load_fn("glStencilMaskSeparate"));
    table_.glLineWidth = reinterpret_cast<PFN_glLineWidth>(load_fn("glLineWidth"));
    table_.glPolygonOffset = reinterpret_cast<PFN_glPolygonOffset>(load_fn("glPolygonOffset"));
    table_.glSampleCoverage = reinterpret_cast<PFN_glSampleCoverage>(load_fn("glSampleCoverage"));
    table_.glFlush = reinterpret_cast<PFN_glFlush>(load_fn("glFlush"));
    table_.glFinish = reinterpret_cast<PFN_glFinish>(load_fn("glFinish"));
    table_.glHint = reinterpret_cast<PFN_glHint>(load_fn("glHint"));

    // Renderbuffers & Framebuffers
    table_.glGenRenderbuffers =
        reinterpret_cast<PFN_glGenRenderbuffers>(load_fn("glGenRenderbuffers"));
    table_.glBindRenderbuffer =
        reinterpret_cast<PFN_glBindRenderbuffer>(load_fn("glBindRenderbuffer"));
    table_.glDeleteRenderbuffers =
        reinterpret_cast<PFN_glDeleteRenderbuffers>(load_fn("glDeleteRenderbuffers"));
    table_.glIsRenderbuffer = reinterpret_cast<PFN_glIsRenderbuffer>(load_fn("glIsRenderbuffer"));
    table_.glRenderbufferStorage =
        reinterpret_cast<PFN_glRenderbufferStorage>(load_fn("glRenderbufferStorage"));
    table_.glRenderbufferStorageMultisample =
        reinterpret_cast<PFN_glRenderbufferStorageMultisample>(
            load_fn("glRenderbufferStorageMultisample"));
    table_.glFramebufferRenderbuffer =
        reinterpret_cast<PFN_glFramebufferRenderbuffer>(load_fn("glFramebufferRenderbuffer"));
    table_.glBlitFramebuffer =
        reinterpret_cast<PFN_glBlitFramebuffer>(load_fn("glBlitFramebuffer"));
    table_.glDrawBuffers = reinterpret_cast<PFN_glDrawBuffers>(load_fn("glDrawBuffers"));
    table_.glReadBuffer = reinterpret_cast<PFN_glReadBuffer>(load_fn("glReadBuffer"));
    table_.glInvalidateFramebuffer =
        reinterpret_cast<PFN_glInvalidateFramebuffer>(load_fn("glInvalidateFramebuffer"));
    table_.glFramebufferTextureLayer =
        reinterpret_cast<PFN_glFramebufferTextureLayer>(load_fn("glFramebufferTextureLayer"));

    // Textures & Mipmaps
    table_.glTexSubImage2D = reinterpret_cast<PFN_glTexSubImage2D>(load_fn("glTexSubImage2D"));
    table_.glCopyTexImage2D = reinterpret_cast<PFN_glCopyTexImage2D>(load_fn("glCopyTexImage2D"));
    table_.glCopyTexSubImage2D =
        reinterpret_cast<PFN_glCopyTexSubImage2D>(load_fn("glCopyTexSubImage2D"));
    table_.glCompressedTexImage2D =
        reinterpret_cast<PFN_glCompressedTexImage2D>(load_fn("glCompressedTexImage2D"));
    table_.glCompressedTexSubImage2D =
        reinterpret_cast<PFN_glCompressedTexSubImage2D>(load_fn("glCompressedTexSubImage2D"));
    table_.glGenerateMipmap = reinterpret_cast<PFN_glGenerateMipmap>(load_fn("glGenerateMipmap"));
    table_.glTexParameterf = reinterpret_cast<PFN_glTexParameterf>(load_fn("glTexParameterf"));
    table_.glTexParameterfv = reinterpret_cast<PFN_glTexParameterfv>(load_fn("glTexParameterfv"));
    table_.glTexParameteriv = reinterpret_cast<PFN_glTexParameteriv>(load_fn("glTexParameteriv"));

    // Uniforms, Attributes & Program
    table_.glUniform2i = reinterpret_cast<PFN_glUniform2i>(load_fn("glUniform2i"));
    table_.glUniform3i = reinterpret_cast<PFN_glUniform3i>(load_fn("glUniform3i"));
    table_.glUniform4i = reinterpret_cast<PFN_glUniform4i>(load_fn("glUniform4i"));
    table_.glUniform1iv = reinterpret_cast<PFN_glUniform1iv>(load_fn("glUniform1iv"));
    table_.glUniform2iv = reinterpret_cast<PFN_glUniform2iv>(load_fn("glUniform2iv"));
    table_.glUniform3iv = reinterpret_cast<PFN_glUniform3iv>(load_fn("glUniform3iv"));
    table_.glUniform4iv = reinterpret_cast<PFN_glUniform4iv>(load_fn("glUniform4iv"));
    table_.glUniform2f = reinterpret_cast<PFN_glUniform2f>(load_fn("glUniform2f"));
    table_.glUniform3f = reinterpret_cast<PFN_glUniform3f>(load_fn("glUniform3f"));
    table_.glUniform1fv = reinterpret_cast<PFN_glUniform1fv>(load_fn("glUniform1fv"));
    table_.glUniform2fv = reinterpret_cast<PFN_glUniform2fv>(load_fn("glUniform2fv"));
    table_.glUniform3fv = reinterpret_cast<PFN_glUniform3fv>(load_fn("glUniform3fv"));
    table_.glUniform4fv = reinterpret_cast<PFN_glUniform4fv>(load_fn("glUniform4fv"));
    table_.glUniform1uiv = reinterpret_cast<PFN_glUniform1uiv>(load_fn("glUniform1uiv"));
    table_.glUniform2uiv = reinterpret_cast<PFN_glUniform2uiv>(load_fn("glUniform2uiv"));
    table_.glUniform3uiv = reinterpret_cast<PFN_glUniform3uiv>(load_fn("glUniform3uiv"));
    table_.glUniform4uiv = reinterpret_cast<PFN_glUniform4uiv>(load_fn("glUniform4uiv"));
    table_.glUniformMatrix2fv =
        reinterpret_cast<PFN_glUniformMatrix2fv>(load_fn("glUniformMatrix2fv"));
    table_.glUniformMatrix3fv =
        reinterpret_cast<PFN_glUniformMatrix3fv>(load_fn("glUniformMatrix3fv"));
    table_.glVertexAttribIPointer =
        reinterpret_cast<PFN_glVertexAttribIPointer>(load_fn("glVertexAttribIPointer"));
    table_.glVertexAttrib1f = reinterpret_cast<PFN_glVertexAttrib1f>(load_fn("glVertexAttrib1f"));
    table_.glVertexAttrib2f = reinterpret_cast<PFN_glVertexAttrib2f>(load_fn("glVertexAttrib2f"));
    table_.glVertexAttrib3f = reinterpret_cast<PFN_glVertexAttrib3f>(load_fn("glVertexAttrib3f"));
    table_.glVertexAttrib4f = reinterpret_cast<PFN_glVertexAttrib4f>(load_fn("glVertexAttrib4f"));
    table_.glBindAttribLocation =
        reinterpret_cast<PFN_glBindAttribLocation>(load_fn("glBindAttribLocation"));
    table_.glGetActiveUniform =
        reinterpret_cast<PFN_glGetActiveUniform>(load_fn("glGetActiveUniform"));
    table_.glGetActiveAttrib =
        reinterpret_cast<PFN_glGetActiveAttrib>(load_fn("glGetActiveAttrib"));
    table_.glDetachShader = reinterpret_cast<PFN_glDetachShader>(load_fn("glDetachShader"));
    table_.glValidateProgram =
        reinterpret_cast<PFN_glValidateProgram>(load_fn("glValidateProgram"));
    table_.glIsShader = reinterpret_cast<PFN_glIsShader>(load_fn("glIsShader"));
    table_.glIsProgram = reinterpret_cast<PFN_glIsProgram>(load_fn("glIsProgram"));
    table_.glIsBuffer = reinterpret_cast<PFN_glIsBuffer>(load_fn("glIsBuffer"));
    table_.glIsTexture = reinterpret_cast<PFN_glIsTexture>(load_fn("glIsTexture"));
    table_.glIsFramebuffer = reinterpret_cast<PFN_glIsFramebuffer>(load_fn("glIsFramebuffer"));
    table_.glIsVertexArray = reinterpret_cast<PFN_glIsVertexArray>(load_fn("glIsVertexArray"));
    table_.glIsSampler = reinterpret_cast<PFN_glIsSampler>(load_fn("glIsSampler"));

    // Clear Buffers
    table_.glClearBufferfv = reinterpret_cast<PFN_glClearBufferfv>(load_fn("glClearBufferfv"));
    table_.glClearBufferiv = reinterpret_cast<PFN_glClearBufferiv>(load_fn("glClearBufferiv"));
    table_.glClearBufferuiv = reinterpret_cast<PFN_glClearBufferuiv>(load_fn("glClearBufferuiv"));
    table_.glClearBufferfi = reinterpret_cast<PFN_glClearBufferfi>(load_fn("glClearBufferfi"));

    // State & Queries
    table_.glGetIntegerv = reinterpret_cast<PFN_glGetIntegerv>(load_fn("glGetIntegerv"));
    table_.glGetInteger64v = reinterpret_cast<PFN_glGetInteger64v>(load_fn("glGetInteger64v"));
    table_.glGetFloatv = reinterpret_cast<PFN_glGetFloatv>(load_fn("glGetFloatv"));
    table_.glGetBooleanv = reinterpret_cast<PFN_glGetBooleanv>(load_fn("glGetBooleanv"));
    table_.glGetStringi = reinterpret_cast<PFN_glGetStringi>(load_fn("glGetStringi"));
    table_.glGetTexParameteriv =
        reinterpret_cast<PFN_glGetTexParameteriv>(load_fn("glGetTexParameteriv"));
    table_.glGetTexParameterfv =
        reinterpret_cast<PFN_glGetTexParameterfv>(load_fn("glGetTexParameterfv"));
    table_.glGetFramebufferAttachmentParameteriv =
        reinterpret_cast<PFN_glGetFramebufferAttachmentParameteriv>(
            load_fn("glGetFramebufferAttachmentParameteriv"));
    table_.glGetRenderbufferParameteriv =
        reinterpret_cast<PFN_glGetRenderbufferParameteriv>(load_fn("glGetRenderbufferParameteriv"));
    table_.glGetBufferParameteriv =
        reinterpret_cast<PFN_glGetBufferParameteriv>(load_fn("glGetBufferParameteriv"));
    table_.glGetVertexAttribiv =
        reinterpret_cast<PFN_glGetVertexAttribiv>(load_fn("glGetVertexAttribiv"));
    table_.glIsEnabled = reinterpret_cast<PFN_glIsEnabled>(load_fn("glIsEnabled"));

    // Query ES compatibility support while dummy context is active
    if (table_.glGetString) {
        const char *exts = reinterpret_cast<const char *>(table_.glGetString(GL_EXTENSIONS));
        if (exts) {
            std::string_view sv(exts);
            table_.has_es2_compat = (sv.find("GL_ARB_ES2_compatibility") != std::string_view::npos);
            table_.has_es3_compat = (sv.find("GL_ARB_ES3_compatibility") != std::string_view::npos);
        }
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

HGLRC WglBridge::CreateContext(HDC hdc, int gles_version, HGLRC share_context) noexcept
{
    if (!hdc)
        return nullptr;

    if (table_.wglCreateContextAttribsARB) {
        int major = (gles_version >= 3) ? 3 : 2;
        int minor = (gles_version >= 3) ? 2 : 0;
        int attribs[] = {
            0x2091, major,      // WGL_CONTEXT_MAJOR_VERSION_ARB
            0x2092, minor,      // WGL_CONTEXT_MINOR_VERSION_ARB
            0x9126, 0x00000002, // WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
            0
        };
        HGLRC ctx = table_.wglCreateContextAttribsARB(hdc, share_context, attribs);
        if (ctx)
            return ctx;
    }

    if (!table_.wglCreateContext)
        return nullptr;

    HGLRC ctx = table_.wglCreateContext(hdc);
    if (ctx && share_context && table_.wglShareLists) {
        table_.wglShareLists(share_context, ctx);
    }
    return ctx;
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