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