#pragma once

#include "gles/gles_types.hpp"
#include <functional>
#include <mutex>
#include <string_view>
#include <windows.h>

namespace portage::gles {

// Function pointer signatures for dynamically loaded OpenGL / WGL functions
using PFN_wglCreateContext = HGLRC(WINAPI *)(HDC);
using PFN_wglDeleteContext = BOOL(WINAPI *)(HGLRC);
using PFN_wglMakeCurrent = BOOL(WINAPI *)(HDC, HGLRC);
using PFN_wglGetProcAddress = PROC(WINAPI *)(LPCSTR);
using PFN_wglGetCurrentContext = HGLRC(WINAPI *)(void);
using PFN_wglGetCurrentDC = HDC(WINAPI *)(void);

// Shaders & Programs
using PFN_glCreateShader = GLuint(APIENTRY *)(GLenum type);
using PFN_glShaderSource = void(APIENTRY *)(GLuint shader, GLsizei count,
                                            const GLchar *const *string, const GLint *length);
using PFN_glCompileShader = void(APIENTRY *)(GLuint shader);
using PFN_glGetShaderiv = void(APIENTRY *)(GLuint shader, GLenum pname, GLint *params);
using PFN_glGetShaderInfoLog = void(APIENTRY *)(GLuint shader, GLsizei bufSize, GLsizei *length,
                                                GLchar *infoLog);
using PFN_glDeleteShader = void(APIENTRY *)(GLuint shader);
using PFN_glCreateProgram = GLuint(APIENTRY *)(void);
using PFN_glAttachShader = void(APIENTRY *)(GLuint program, GLuint shader);
using PFN_glLinkProgram = void(APIENTRY *)(GLuint program);
using PFN_glGetProgramiv = void(APIENTRY *)(GLuint program, GLenum pname, GLint *params);
using PFN_glGetProgramInfoLog = void(APIENTRY *)(GLuint program, GLsizei bufSize, GLsizei *length,
                                                 GLchar *infoLog);
using PFN_glUseProgram = void(APIENTRY *)(GLuint program);
using PFN_glDeleteProgram = void(APIENTRY *)(GLuint program);

// Attributes & Uniforms
using PFN_glGetAttribLocation = GLint(APIENTRY *)(GLuint program, const GLchar *name);
using PFN_glEnableVertexAttribArray = void(APIENTRY *)(GLuint index);
using PFN_glDisableVertexAttribArray = void(APIENTRY *)(GLuint index);
using PFN_glVertexAttribPointer = void(APIENTRY *)(GLuint index, GLint size, GLenum type,
                                                   GLboolean normalized, GLsizei stride,
                                                   const void *pointer);
using PFN_glGetUniformLocation = GLint(APIENTRY *)(GLuint program, const GLchar *name);
using PFN_glUniform1i = void(APIENTRY *)(GLint location, GLint v0);
using PFN_glUniform1f = void(APIENTRY *)(GLint location, GLfloat v0);
using PFN_glUniform4f = void(APIENTRY *)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                         GLfloat v3);
using PFN_glUniformMatrix4fv = void(APIENTRY *)(GLint location, GLsizei count, GLboolean transpose,
                                                const GLfloat *value);

// Buffers & VAOs
using PFN_glGenBuffers = void(APIENTRY *)(GLsizei n, GLuint *buffers);
using PFN_glBindBuffer = void(APIENTRY *)(GLenum target, GLuint buffer);
using PFN_glBufferData = void(APIENTRY *)(GLenum target, GLsizeiptr size, const void *data,
                                          GLenum usage);
using PFN_glBufferSubData = void(APIENTRY *)(GLenum target, GLintptr offset, GLsizeiptr size,
                                             const void *data);
using PFN_glDeleteBuffers = void(APIENTRY *)(GLsizei n, const GLuint *buffers);
using PFN_glGenVertexArrays = void(APIENTRY *)(GLsizei n, GLuint *arrays);
using PFN_glBindVertexArray = void(APIENTRY *)(GLuint array);
using PFN_glDeleteVertexArrays = void(APIENTRY *)(GLsizei n, const GLuint *arrays);

// Textures & Framebuffers
using PFN_glActiveTexture = void(APIENTRY *)(GLenum texture);
using PFN_glGenFramebuffers = void(APIENTRY *)(GLsizei n, GLuint *framebuffers);
using PFN_glBindFramebuffer = void(APIENTRY *)(GLenum target, GLuint framebuffer);
using PFN_glFramebufferTexture2D = void(APIENTRY *)(GLenum target, GLenum attachment,
                                                    GLenum textarget, GLuint texture, GLint level);
using PFN_glCheckFramebufferStatus = GLenum(APIENTRY *)(GLenum target);
using PFN_glDeleteFramebuffers = void(APIENTRY *)(GLsizei n, const GLuint *framebuffers);

// Core GL
using PFN_glClear = void(APIENTRY *)(GLbitfield mask);
using PFN_glClearColor = void(APIENTRY *)(GLclampf red, GLclampf green, GLclampf blue,
                                          GLclampf alpha);
using PFN_glViewport = void(APIENTRY *)(GLint x, GLint y, GLsizei width, GLsizei height);
using PFN_glEnable = void(APIENTRY *)(GLenum cap);
using PFN_glDisable = void(APIENTRY *)(GLenum cap);
using PFN_glDrawArrays = void(APIENTRY *)(GLenum mode, GLint first, GLsizei count);
using PFN_glDrawElements = void(APIENTRY *)(GLenum mode, GLsizei count, GLenum type,
                                            const void *indices);
using PFN_glGetError = GLenum(APIENTRY *)(void);
using PFN_glGetString = const GLubyte *(APIENTRY *)(GLenum name);
using PFN_glReadPixels = void(APIENTRY *)(GLint x, GLint y, GLsizei width, GLsizei height,
                                          GLenum format, GLenum type, void *pixels);
using PFN_glGenTextures = void(APIENTRY *)(GLsizei n, GLuint *textures);
using PFN_glBindTexture = void(APIENTRY *)(GLenum target, GLuint texture);
using PFN_glDeleteTextures = void(APIENTRY *)(GLsizei n, const GLuint *textures);
using PFN_glTexImage2D = void(APIENTRY *)(GLenum target, GLint level, GLint internalformat,
                                          GLsizei width, GLsizei height, GLint border,
                                          GLenum format, GLenum type, const void *pixels);
using PFN_glTexParameteri = void(APIENTRY *)(GLenum target, GLenum pname, GLint param);
using PFN_wglSwapIntervalEXT = BOOL(APIENTRY *)(int interval);

// Sync Fences (GL 3.2 / ARB_sync)
using PFN_glFenceSync = GLsync(APIENTRY *)(GLenum condition, GLbitfield flags);
using PFN_glIsSync = GLboolean(APIENTRY *)(GLsync sync);
using PFN_glDeleteSync = void(APIENTRY *)(GLsync sync);
using PFN_glClientWaitSync = GLenum(APIENTRY *)(GLsync sync, GLbitfield flags, GLuint64 timeout);
using PFN_glWaitSync = void(APIENTRY *)(GLsync sync, GLbitfield flags, GLuint64 timeout);
using PFN_glGetSynciv = void(APIENTRY *)(GLsync sync, GLenum pname, GLsizei bufSize,
                                         GLsizei *length, GLint *values);

// Uniform Buffer Objects (GL 3.1 / ARB_uniform_buffer_object)
using PFN_glBindBufferBase = void(APIENTRY *)(GLenum target, GLuint index, GLuint buffer);
using PFN_glBindBufferRange = void(APIENTRY *)(GLenum target, GLuint index, GLuint buffer,
                                               GLintptr offset, GLsizeiptr size);
using PFN_glGetUniformBlockIndex = GLuint(APIENTRY *)(GLuint program, const char *uniformBlockName);
using PFN_glUniformBlockBinding = void(APIENTRY *)(GLuint program, GLuint uniformBlockIndex,
                                                   GLuint uniformBlockBinding);

// Instanced Drawing (GL 3.1 / 3.3)
using PFN_glDrawArraysInstanced = void(APIENTRY *)(GLenum mode, GLint first, GLsizei count,
                                                   GLsizei instancecount);
using PFN_glDrawElementsInstanced = void(APIENTRY *)(GLenum mode, GLsizei count, GLenum type,
                                                     const void *indices, GLsizei instancecount);
using PFN_glVertexAttribDivisor = void(APIENTRY *)(GLuint index, GLuint divisor);

// Transform Feedback (GL 3.0 / 4.0)
using PFN_glGenTransformFeedbacks = void(APIENTRY *)(GLsizei n, GLuint *ids);
using PFN_glBindTransformFeedback = void(APIENTRY *)(GLenum target, GLuint id);
using PFN_glBeginTransformFeedback = void(APIENTRY *)(GLenum primitiveMode);
using PFN_glEndTransformFeedback = void(APIENTRY *)(void);
using PFN_glTransformFeedbackVaryings = void(APIENTRY *)(GLuint program, GLsizei count,
                                                         const char *const *varyings,
                                                         GLenum bufferMode);
using PFN_glDeleteTransformFeedbacks = void(APIENTRY *)(GLsizei n, const GLuint *ids);

// Sampler Objects (GL 3.3 / ARB_sampler_objects)
using PFN_glGenSamplers = void(APIENTRY *)(GLsizei count, GLuint *samplers);
using PFN_glBindSampler = void(APIENTRY *)(GLuint unit, GLuint sampler);
using PFN_glSamplerParameteri = void(APIENTRY *)(GLuint sampler, GLenum pname, GLint param);
using PFN_glDeleteSamplers = void(APIENTRY *)(GLsizei count, const GLuint *samplers);

// Buffer Mapping & Pixel Store
using PFN_glMapBufferRange = void *(APIENTRY *)(GLenum target, GLintptr offset, GLsizeiptr length,
                                                GLbitfield access);
using PFN_glUnmapBuffer = GLboolean(APIENTRY *)(GLenum target);
using PFN_glFlushMappedBufferRange = void(APIENTRY *)(GLenum target, GLintptr offset,
                                                      GLsizeiptr length);
using PFN_glPixelStorei = void(APIENTRY *)(GLenum pname, GLint param);

// Texture Storage & 3D Textures
using PFN_glTexStorage2D = void(APIENTRY *)(GLenum target, GLsizei levels, GLenum internalformat,
                                            GLsizei width, GLsizei height);
using PFN_glTexStorage3D = void(APIENTRY *)(GLenum target, GLsizei levels, GLenum internalformat,
                                            GLsizei width, GLsizei height, GLsizei depth);
using PFN_glTexImage3D = void(APIENTRY *)(GLenum target, GLint level, GLint internalformat,
                                          GLsizei width, GLsizei height, GLsizei depth,
                                          GLint border, GLenum format, GLenum type,
                                          const void *pixels);
using PFN_glTexSubImage3D = void(APIENTRY *)(GLenum target, GLint level, GLint xoffset,
                                             GLint yoffset, GLint zoffset, GLsizei width,
                                             GLsizei height, GLsizei depth, GLenum format,
                                             GLenum type, const void *pixels);

// Compute Shaders & Memory Barriers (GL 4.3 / 4.2)
using PFN_glDispatchCompute = void(APIENTRY *)(GLuint num_groups_x, GLuint num_groups_y,
                                               GLuint num_groups_z);
using PFN_glMemoryBarrier = void(APIENTRY *)(GLbitfield barriers);

struct WglDispatchTable {
    // WGL
    PFN_wglCreateContext wglCreateContext{nullptr};
    PFN_wglDeleteContext wglDeleteContext{nullptr};
    PFN_wglMakeCurrent wglMakeCurrent{nullptr};
    PFN_wglGetProcAddress wglGetProcAddress{nullptr};
    PFN_wglGetCurrentContext wglGetCurrentContext{nullptr};
    PFN_wglGetCurrentDC wglGetCurrentDC{nullptr};

    // Shaders & Programs
    PFN_glCreateShader glCreateShader{nullptr};
    PFN_glShaderSource glShaderSource{nullptr};
    PFN_glCompileShader glCompileShader{nullptr};
    PFN_glGetShaderiv glGetShaderiv{nullptr};
    PFN_glGetShaderInfoLog glGetShaderInfoLog{nullptr};
    PFN_glDeleteShader glDeleteShader{nullptr};
    PFN_glCreateProgram glCreateProgram{nullptr};
    PFN_glAttachShader glAttachShader{nullptr};
    PFN_glLinkProgram glLinkProgram{nullptr};
    PFN_glGetProgramiv glGetProgramiv{nullptr};
    PFN_glGetProgramInfoLog glGetProgramInfoLog{nullptr};
    PFN_glUseProgram glUseProgram{nullptr};
    PFN_glDeleteProgram glDeleteProgram{nullptr};

    // Attributes & Uniforms
    PFN_glGetAttribLocation glGetAttribLocation{nullptr};
    PFN_glEnableVertexAttribArray glEnableVertexAttribArray{nullptr};
    PFN_glDisableVertexAttribArray glDisableVertexAttribArray{nullptr};
    PFN_glVertexAttribPointer glVertexAttribPointer{nullptr};
    PFN_glGetUniformLocation glGetUniformLocation{nullptr};
    PFN_glUniform1i glUniform1i{nullptr};
    PFN_glUniform1f glUniform1f{nullptr};
    PFN_glUniform4f glUniform4f{nullptr};
    PFN_glUniformMatrix4fv glUniformMatrix4fv{nullptr};

    // Buffers & VAOs
    PFN_glGenBuffers glGenBuffers{nullptr};
    PFN_glBindBuffer glBindBuffer{nullptr};
    PFN_glBufferData glBufferData{nullptr};
    PFN_glBufferSubData glBufferSubData{nullptr};
    PFN_glDeleteBuffers glDeleteBuffers{nullptr};
    PFN_glGenVertexArrays glGenVertexArrays{nullptr};
    PFN_glBindVertexArray glBindVertexArray{nullptr};
    PFN_glDeleteVertexArrays glDeleteVertexArrays{nullptr};

    // Textures & Framebuffers
    PFN_glActiveTexture glActiveTexture{nullptr};
    PFN_glGenFramebuffers glGenFramebuffers{nullptr};
    PFN_glBindFramebuffer glBindFramebuffer{nullptr};
    PFN_glFramebufferTexture2D glFramebufferTexture2D{nullptr};
    PFN_glCheckFramebufferStatus glCheckFramebufferStatus{nullptr};
    PFN_glDeleteFramebuffers glDeleteFramebuffers{nullptr};

    // Core GL
    PFN_glClear glClear{nullptr};
    PFN_glClearColor glClearColor{nullptr};
    PFN_glViewport glViewport{nullptr};
    PFN_glEnable glEnable{nullptr};
    PFN_glDisable glDisable{nullptr};
    PFN_glDrawArrays glDrawArrays{nullptr};
    PFN_glDrawElements glDrawElements{nullptr};
    PFN_glGetError glGetError{nullptr};
    PFN_glGetString glGetString{nullptr};
    PFN_glReadPixels glReadPixels{nullptr};
    PFN_glGenTextures glGenTextures{nullptr};
    PFN_glBindTexture glBindTexture{nullptr};
    PFN_glDeleteTextures glDeleteTextures{nullptr};
    PFN_glTexImage2D glTexImage2D{nullptr};
    PFN_glTexParameteri glTexParameteri{nullptr};
    PFN_wglSwapIntervalEXT wglSwapIntervalEXT{nullptr};

    // Sync Fences
    PFN_glFenceSync glFenceSync{nullptr};
    PFN_glIsSync glIsSync{nullptr};
    PFN_glDeleteSync glDeleteSync{nullptr};
    PFN_glClientWaitSync glClientWaitSync{nullptr};
    PFN_glWaitSync glWaitSync{nullptr};
    PFN_glGetSynciv glGetSynciv{nullptr};

    // UBOs
    PFN_glBindBufferBase glBindBufferBase{nullptr};
    PFN_glBindBufferRange glBindBufferRange{nullptr};
    PFN_glGetUniformBlockIndex glGetUniformBlockIndex{nullptr};
    PFN_glUniformBlockBinding glUniformBlockBinding{nullptr};

    // Instancing
    PFN_glDrawArraysInstanced glDrawArraysInstanced{nullptr};
    PFN_glDrawElementsInstanced glDrawElementsInstanced{nullptr};
    PFN_glVertexAttribDivisor glVertexAttribDivisor{nullptr};

    // Transform Feedback
    PFN_glGenTransformFeedbacks glGenTransformFeedbacks{nullptr};
    PFN_glBindTransformFeedback glBindTransformFeedback{nullptr};
    PFN_glBeginTransformFeedback glBeginTransformFeedback{nullptr};
    PFN_glEndTransformFeedback glEndTransformFeedback{nullptr};
    PFN_glTransformFeedbackVaryings glTransformFeedbackVaryings{nullptr};
    PFN_glDeleteTransformFeedbacks glDeleteTransformFeedbacks{nullptr};

    // Sampler Objects
    PFN_glGenSamplers glGenSamplers{nullptr};
    PFN_glBindSampler glBindSampler{nullptr};
    PFN_glSamplerParameteri glSamplerParameteri{nullptr};
    PFN_glDeleteSamplers glDeleteSamplers{nullptr};

    // Buffer Mapping & Pixel Store
    PFN_glMapBufferRange glMapBufferRange{nullptr};
    PFN_glUnmapBuffer glUnmapBuffer{nullptr};
    PFN_glFlushMappedBufferRange glFlushMappedBufferRange{nullptr};
    PFN_glPixelStorei glPixelStorei{nullptr};

    // Texture Storage & 3D Textures
    PFN_glTexStorage2D glTexStorage2D{nullptr};
    PFN_glTexStorage3D glTexStorage3D{nullptr};
    PFN_glTexImage3D glTexImage3D{nullptr};
    PFN_glTexSubImage3D glTexSubImage3D{nullptr};

    // Compute & Barriers
    PFN_glDispatchCompute glDispatchCompute{nullptr};
    PFN_glMemoryBarrier glMemoryBarrier{nullptr};
};

class WglBridge {
  public:
    static WglBridge &Instance() noexcept;

    bool Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
    [[nodiscard]] const WglDispatchTable &GetDispatchTable() const noexcept { return table_; }

    // Surface creation helpers
    bool SetupWindowPixelFormat(HDC hdc) noexcept;
    HDC CreateOffscreenDc(int32_t width, int32_t height, HBITMAP *out_bitmap,
                          void **out_bits) noexcept;
    void DestroyOffscreenDc(HDC hdc, HBITMAP bitmap) noexcept;

    HGLRC CreateContext(HDC hdc, int gles_version) noexcept;
    bool MakeCurrent(HDC hdc, HGLRC hglrc) noexcept;
    bool DeleteContext(HGLRC hglrc) noexcept;
    bool SwapBuffers(HDC hdc) noexcept;

    PROC GetProcAddress(const char *name) noexcept;

  private:
    WglBridge() = default;
    ~WglBridge() { Shutdown(); }

    WglBridge(const WglBridge &) = delete;
    WglBridge &operator=(const WglBridge &) = delete;

    std::mutex mutex_{};
    bool initialized_{false};
    HMODULE opengl_module_{nullptr};
    WglDispatchTable table_{};

    HWND dummy_hwnd_{nullptr};
    HDC dummy_hdc_{nullptr};
    HGLRC dummy_hglrc_{nullptr};
};

} // namespace portage::gles