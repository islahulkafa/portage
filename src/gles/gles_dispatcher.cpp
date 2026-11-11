#include "gles/gles_dispatcher.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/wgl_bridge.hpp"
#include "ndk/hardware_buffer.hpp"
#include <atomic>
#include <cctype>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace portage::gles {

static std::string SanitizeGlesShaderSource(std::string_view src)
{
    std::string out(src);

    // Replace precision qualifiers with spaces
    size_t pos = 0;
    while ((pos = out.find("precision", pos)) != std::string::npos) {
        bool before_ok =
            (pos == 0 || isspace(static_cast<unsigned char>(out[pos - 1])) || out[pos - 1] == ';');
        bool after_ok = (pos + 9 < out.size() && isspace(static_cast<unsigned char>(out[pos + 9])));
        if (before_ok && after_ok) {
            size_t semi = out.find(';', pos);
            if (semi != std::string::npos) {
                for (size_t i = pos; i <= semi; ++i) {
                    if (out[i] != '\n' && out[i] != '\r') {
                        out[i] = ' ';
                    }
                }
                pos = semi + 1;
                continue;
            }
        }
        pos += 9;
    }

    // Prepend compatibility defines for lowp, mediump, highp if not already present
    std::string prefix = "#ifndef GL_ES\n"
                         "#define lowp\n"
                         "#define mediump\n"
                         "#define highp\n"
                         "#endif\n";

    size_t ver_pos = out.find("#version");
    if (ver_pos != std::string::npos) {
        size_t eol = out.find('\n', ver_pos);
        if (eol != std::string::npos) {
            out.insert(eol + 1, prefix);
            return out;
        }
    }

    out.insert(0, prefix);
    return out;
}

void InitializeGlesDispatcher() noexcept
{
    auto &egl = EglDispatcher::Instance();

// Register all GLES symbols with eglGetProcAddress
#define REG_PROC(name) egl.RegisterProc(#name, reinterpret_cast<void *>(&name))

    // Shaders & Programs
    REG_PROC(glCreateShader);
    REG_PROC(glShaderSource);
    REG_PROC(glCompileShader);
    REG_PROC(glGetShaderiv);
    REG_PROC(glGetShaderInfoLog);
    REG_PROC(glDeleteShader);
    REG_PROC(glCreateProgram);
    REG_PROC(glAttachShader);
    REG_PROC(glLinkProgram);
    REG_PROC(glGetProgramiv);
    REG_PROC(glGetProgramInfoLog);
    REG_PROC(glUseProgram);
    REG_PROC(glDeleteProgram);

    // Attributes & Uniforms
    REG_PROC(glGetAttribLocation);
    REG_PROC(glEnableVertexAttribArray);
    REG_PROC(glDisableVertexAttribArray);
    REG_PROC(glVertexAttribPointer);
    REG_PROC(glGetUniformLocation);
    REG_PROC(glUniform1i);
    REG_PROC(glUniform1f);
    REG_PROC(glUniform4f);
    REG_PROC(glUniformMatrix4fv);

    // Buffers & VAOs
    REG_PROC(glGenBuffers);
    REG_PROC(glBindBuffer);
    REG_PROC(glBufferData);
    REG_PROC(glBufferSubData);
    REG_PROC(glDeleteBuffers);
    REG_PROC(glGenVertexArrays);
    REG_PROC(glBindVertexArray);
    REG_PROC(glDeleteVertexArrays);

    // Textures & Framebuffers
    REG_PROC(glActiveTexture);
    REG_PROC(glGenTextures);
    REG_PROC(glBindTexture);
    REG_PROC(glDeleteTextures);
    REG_PROC(glTexImage2D);
    REG_PROC(glTexParameteri);
    REG_PROC(glGenFramebuffers);
    REG_PROC(glBindFramebuffer);
    REG_PROC(glFramebufferTexture2D);
    REG_PROC(glCheckFramebufferStatus);
    REG_PROC(glDeleteFramebuffers);

    // Core GL
    REG_PROC(glClear);
    REG_PROC(glClearColor);
    REG_PROC(glViewport);
    REG_PROC(glEnable);
    REG_PROC(glDisable);
    REG_PROC(glDrawArrays);
    REG_PROC(glDrawElements);
    REG_PROC(glGetError);
    REG_PROC(glGetString);
    REG_PROC(glReadPixels);

    // OES Image Texturing
    REG_PROC(glEGLImageTargetTexture2DOES);

    // Sync Fences
    REG_PROC(glFenceSync);
    REG_PROC(glIsSync);
    REG_PROC(glDeleteSync);
    REG_PROC(glClientWaitSync);
    REG_PROC(glWaitSync);
    REG_PROC(glGetSynciv);

    // Uniform Buffer Objects
    REG_PROC(glBindBufferBase);
    REG_PROC(glBindBufferRange);
    REG_PROC(glGetUniformBlockIndex);
    REG_PROC(glUniformBlockBinding);

    // Instanced Rendering
    REG_PROC(glDrawArraysInstanced);
    REG_PROC(glDrawElementsInstanced);
    REG_PROC(glVertexAttribDivisor);

    // Transform Feedback
    REG_PROC(glGenTransformFeedbacks);
    REG_PROC(glBindTransformFeedback);
    REG_PROC(glBeginTransformFeedback);
    REG_PROC(glEndTransformFeedback);
    REG_PROC(glTransformFeedbackVaryings);
    REG_PROC(glDeleteTransformFeedbacks);

    // Sampler Objects
    REG_PROC(glGenSamplers);
    REG_PROC(glBindSampler);
    REG_PROC(glSamplerParameteri);
    REG_PROC(glDeleteSamplers);

    // Buffer Mapping & Pixel Store
    REG_PROC(glMapBufferRange);
    REG_PROC(glUnmapBuffer);
    REG_PROC(glFlushMappedBufferRange);
    REG_PROC(glPixelStorei);

    // Texture Storage & 3D Textures
    REG_PROC(glTexStorage2D);
    REG_PROC(glTexStorage3D);
    REG_PROC(glTexImage3D);
    REG_PROC(glTexSubImage3D);

    // Compute & Memory Barriers
    REG_PROC(glDispatchCompute);
    REG_PROC(glMemoryBarrier);

#undef REG_PROC
}

namespace {
std::mutex &FallbackSyncMutex()
{
    static std::mutex m;
    return m;
}
std::unordered_set<GLsync> &FallbackSyncSet()
{
    static std::unordered_set<GLsync> s;
    return s;
}
std::atomic<uintptr_t> &FallbackSyncCounter()
{
    static std::atomic<uintptr_t> c{0x10000};
    return c;
}
std::atomic<GLuint> &FallbackIdCounter()
{
    static std::atomic<GLuint> c{10000};
    return c;
}
} // namespace

} // namespace portage::gles

// SysV ABI C Function Definitions
extern "C" {

__attribute__((sysv_abi)) GLuint glCreateShader(GLenum type)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glCreateShader)
        return tbl.glCreateShader(type);
    return 0;
}

__attribute__((sysv_abi)) void glShaderSource(GLuint shader, GLsizei count,
                                              const GLchar *const *string, const GLint *length)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (!tbl.glShaderSource || !string || count <= 0)
        return;

    std::vector<std::string> sanitized;
    std::vector<const GLchar *> sanitized_ptrs;
    std::vector<GLint> sanitized_lens;
    sanitized.reserve(count);
    sanitized_ptrs.reserve(count);
    sanitized_lens.reserve(count);

    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i])
            continue;
        size_t len =
            (length && length[i] >= 0) ? static_cast<size_t>(length[i]) : strlen(string[i]);
        std::string_view sv(string[i], len);
        sanitized.push_back(portage::gles::SanitizeGlesShaderSource(sv));
        sanitized_ptrs.push_back(sanitized.back().c_str());
        sanitized_lens.push_back(static_cast<GLint>(sanitized.back().size()));
    }

    tbl.glShaderSource(shader, static_cast<GLsizei>(sanitized_ptrs.size()), sanitized_ptrs.data(),
                       sanitized_lens.data());
}

__attribute__((sysv_abi)) void glCompileShader(GLuint shader)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glCompileShader)
        tbl.glCompileShader(shader);
}

__attribute__((sysv_abi)) void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetShaderiv)
        tbl.glGetShaderiv(shader, pname, params);
}

__attribute__((sysv_abi)) void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length,
                                                  GLchar *infoLog)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetShaderInfoLog)
        tbl.glGetShaderInfoLog(shader, bufSize, length, infoLog);
}

__attribute__((sysv_abi)) void glDeleteShader(GLuint shader)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteShader)
        tbl.glDeleteShader(shader);
}

__attribute__((sysv_abi)) GLuint glCreateProgram(void)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glCreateProgram)
        return tbl.glCreateProgram();
    return 0;
}

__attribute__((sysv_abi)) void glAttachShader(GLuint program, GLuint shader)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glAttachShader)
        tbl.glAttachShader(program, shader);
}

__attribute__((sysv_abi)) void glLinkProgram(GLuint program)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glLinkProgram)
        tbl.glLinkProgram(program);
}

__attribute__((sysv_abi)) void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetProgramiv)
        tbl.glGetProgramiv(program, pname, params);
}

__attribute__((sysv_abi)) void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length,
                                                   GLchar *infoLog)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetProgramInfoLog)
        tbl.glGetProgramInfoLog(program, bufSize, length, infoLog);
}

__attribute__((sysv_abi)) void glUseProgram(GLuint program)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUseProgram)
        tbl.glUseProgram(program);
}

__attribute__((sysv_abi)) void glDeleteProgram(GLuint program)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteProgram)
        tbl.glDeleteProgram(program);
}

__attribute__((sysv_abi)) GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetAttribLocation)
        return tbl.glGetAttribLocation(program, name);
    return -1;
}

__attribute__((sysv_abi)) void glEnableVertexAttribArray(GLuint index)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glEnableVertexAttribArray)
        tbl.glEnableVertexAttribArray(index);
}

__attribute__((sysv_abi)) void glDisableVertexAttribArray(GLuint index)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDisableVertexAttribArray)
        tbl.glDisableVertexAttribArray(index);
}

__attribute__((sysv_abi)) void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                                     GLboolean normalized, GLsizei stride,
                                                     const void *pointer)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glVertexAttribPointer)
        tbl.glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

__attribute__((sysv_abi)) GLint glGetUniformLocation(GLuint program, const GLchar *name)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetUniformLocation)
        return tbl.glGetUniformLocation(program, name);
    return -1;
}

__attribute__((sysv_abi)) void glUniform1i(GLint location, GLint v0)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUniform1i)
        tbl.glUniform1i(location, v0);
}

__attribute__((sysv_abi)) void glUniform1f(GLint location, GLfloat v0)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUniform1f)
        tbl.glUniform1f(location, v0);
}

__attribute__((sysv_abi)) void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                           GLfloat v3)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUniform4f)
        tbl.glUniform4f(location, v0, v1, v2, v3);
}

__attribute__((sysv_abi)) void glUniformMatrix4fv(GLint location, GLsizei count,
                                                  GLboolean transpose, const GLfloat *value)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUniformMatrix4fv)
        tbl.glUniformMatrix4fv(location, count, transpose, value);
}

__attribute__((sysv_abi)) void glGenBuffers(GLsizei n, GLuint *buffers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenBuffers)
        tbl.glGenBuffers(n, buffers);
}

__attribute__((sysv_abi)) void glBindBuffer(GLenum target, GLuint buffer)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindBuffer)
        tbl.glBindBuffer(target, buffer);
}

__attribute__((sysv_abi)) void glBufferData(GLenum target, GLsizeiptr size, const void *data,
                                            GLenum usage)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBufferData)
        tbl.glBufferData(target, size, data, usage);
}

__attribute__((sysv_abi)) void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                               const void *data)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBufferSubData)
        tbl.glBufferSubData(target, offset, size, data);
}

__attribute__((sysv_abi)) void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteBuffers)
        tbl.glDeleteBuffers(n, buffers);
}

__attribute__((sysv_abi)) void glGenVertexArrays(GLsizei n, GLuint *arrays)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenVertexArrays)
        tbl.glGenVertexArrays(n, arrays);
}

__attribute__((sysv_abi)) void glBindVertexArray(GLuint array)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindVertexArray)
        tbl.glBindVertexArray(array);
}

__attribute__((sysv_abi)) void glDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteVertexArrays)
        tbl.glDeleteVertexArrays(n, arrays);
}

__attribute__((sysv_abi)) void glActiveTexture(GLenum texture)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glActiveTexture)
        tbl.glActiveTexture(texture);
}

__attribute__((sysv_abi)) void glGenTextures(GLsizei n, GLuint *textures)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenTextures)
        tbl.glGenTextures(n, textures);
}

__attribute__((sysv_abi)) void glBindTexture(GLenum target, GLuint texture)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindTexture)
        tbl.glBindTexture(target, texture);
}

__attribute__((sysv_abi)) void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteTextures)
        tbl.glDeleteTextures(n, textures);
}

__attribute__((sysv_abi)) void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                            GLsizei width, GLsizei height, GLint border,
                                            GLenum format, GLenum type, const void *pixels)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexImage2D)
        tbl.glTexImage2D(target, level, internalformat, width, height, border, format, type,
                         pixels);
}

__attribute__((sysv_abi)) void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexParameteri)
        tbl.glTexParameteri(target, pname, param);
}

__attribute__((sysv_abi)) void glGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenFramebuffers)
        tbl.glGenFramebuffers(n, framebuffers);
}

__attribute__((sysv_abi)) void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindFramebuffer)
        tbl.glBindFramebuffer(target, framebuffer);
}

__attribute__((sysv_abi)) void glFramebufferTexture2D(GLenum target, GLenum attachment,
                                                      GLenum textarget, GLuint texture, GLint level)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glFramebufferTexture2D)
        tbl.glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

__attribute__((sysv_abi)) GLenum glCheckFramebufferStatus(GLenum target)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glCheckFramebufferStatus)
        return tbl.glCheckFramebufferStatus(target);
    return GL_FRAMEBUFFER_COMPLETE;
}

__attribute__((sysv_abi)) void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteFramebuffers)
        tbl.glDeleteFramebuffers(n, framebuffers);
}

__attribute__((sysv_abi)) void glClear(GLbitfield mask)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glClear)
        tbl.glClear(mask);
}

__attribute__((sysv_abi)) void glClearColor(GLclampf red, GLclampf green, GLclampf blue,
                                            GLclampf alpha)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glClearColor)
        tbl.glClearColor(red, green, blue, alpha);
}

__attribute__((sysv_abi)) void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glViewport)
        tbl.glViewport(x, y, width, height);
}

__attribute__((sysv_abi)) void glEnable(GLenum cap)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glEnable)
        tbl.glEnable(cap);
}

__attribute__((sysv_abi)) void glDisable(GLenum cap)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDisable)
        tbl.glDisable(cap);
}

__attribute__((sysv_abi)) void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDrawArrays)
        tbl.glDrawArrays(mode, first, count);
}

__attribute__((sysv_abi)) void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                              const void *indices)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDrawElements)
        tbl.glDrawElements(mode, count, type, indices);
}

__attribute__((sysv_abi)) GLenum glGetError(void)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetError)
        return tbl.glGetError();
    return GL_NO_ERROR;
}

__attribute__((sysv_abi)) const GLubyte *glGetString(GLenum name)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (name == GL_VERSION)
        return reinterpret_cast<const GLubyte *>("OpenGL ES 3.2 (Portage Desktop OpenGL)");
    if (name == GL_SHADING_LANGUAGE_VERSION)
        return reinterpret_cast<const GLubyte *>("OpenGL ES GLSL ES 3.20");
    if (tbl.glGetString)
        return tbl.glGetString(name);
    return reinterpret_cast<const GLubyte *>("");
}

__attribute__((sysv_abi)) void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                            GLenum format, GLenum type, void *pixels)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glReadPixels)
        tbl.glReadPixels(x, y, width, height, format, type, pixels);
}

__attribute__((sysv_abi)) void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    const auto *img = portage::gles::EglDispatcher::Instance().FindImage(image);
    AHardwareBuffer *ahb = img ? img->ahb : nullptr;
    if (!ahb) {
        auto *candidate = reinterpret_cast<AHardwareBuffer *>(image);
        if (portage::ndk::HardwareBufferManager::Instance().IsValid(candidate)) {
            ahb = candidate;
        }
    }
    if (!ahb) {
        return;
    }

    void *pixels = ahb->mapped_address;
    bool locked_here = false;
    if (!pixels) {
        if (AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &pixels) ==
            AHARDWAREBUFFER_STATUS_OK) {
            locked_here = true;
        }
    }

    if (pixels && tbl.glTexImage2D) {
        GLenum gl_format = GL_RGBA;
        GLint gl_internal = GL_RGBA8;
        GLenum gl_type = GL_UNSIGNED_BYTE;
        if (ahb->desc.format == AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM) {
            gl_format = GL_RGB;
            gl_internal = GL_RGB8;
        } else if (ahb->desc.format == AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM) {
            gl_format = GL_RGB;
            gl_internal = GL_RGB;
            gl_type = GL_UNSIGNED_SHORT_5_6_5;
        }
        if (tbl.glPixelStorei) {
            tbl.glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(ahb->desc.stride));
        }
        tbl.glTexImage2D(target, 0, gl_internal, static_cast<GLsizei>(ahb->desc.width),
                         static_cast<GLsizei>(ahb->desc.height), 0, gl_format, gl_type, pixels);
        if (tbl.glPixelStorei) {
            tbl.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }

    if (locked_here) {
        AHardwareBuffer_unlock(ahb, nullptr);
    }
}

__attribute__((sysv_abi)) GLsync glFenceSync(GLenum condition, GLbitfield flags)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glFenceSync && tbl.wglGetCurrentContext && tbl.wglGetCurrentContext() != nullptr) {
        if (GLsync s = tbl.glFenceSync(condition, flags)) {
            return s;
        }
    }
    GLsync fallback = reinterpret_cast<GLsync>(portage::gles::FallbackSyncCounter().fetch_add(1));
    std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
    portage::gles::FallbackSyncSet().insert(fallback);
    return fallback;
}

__attribute__((sysv_abi)) GLboolean glIsSync(GLsync sync)
{
    if (!sync) {
        return GL_FALSE;
    }
    {
        std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
        if (portage::gles::FallbackSyncSet().contains(sync)) {
            return GL_TRUE;
        }
    }
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glIsSync) {
        return tbl.glIsSync(sync);
    }
    return GL_FALSE;
}

__attribute__((sysv_abi)) void glDeleteSync(GLsync sync)
{
    if (!sync) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
        if (portage::gles::FallbackSyncSet().erase(sync) > 0) {
            return;
        }
    }
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteSync) {
        tbl.glDeleteSync(sync);
    }
}

__attribute__((sysv_abi)) GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    if (!sync) {
        return GL_WAIT_FAILED;
    }
    {
        std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
        if (portage::gles::FallbackSyncSet().contains(sync)) {
            return GL_ALREADY_SIGNALED;
        }
    }
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glClientWaitSync) {
        return tbl.glClientWaitSync(sync, flags, timeout);
    }
    return GL_ALREADY_SIGNALED;
}

__attribute__((sysv_abi)) void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    if (!sync) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
        if (portage::gles::FallbackSyncSet().contains(sync)) {
            return;
        }
    }
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glWaitSync) {
        tbl.glWaitSync(sync, flags, timeout);
    }
}

__attribute__((sysv_abi)) void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize,
                                           GLsizei *length, GLint *values)
{
    if (!sync || !values || bufSize <= 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(portage::gles::FallbackSyncMutex());
        if (portage::gles::FallbackSyncSet().contains(sync)) {
            if (pname == GL_SYNC_STATUS) {
                *values = static_cast<GLint>(GL_SIGNALED);
            } else if (pname == GL_OBJECT_TYPE) {
                *values = static_cast<GLint>(GL_SYNC_FENCE);
            } else if (pname == GL_SYNC_CONDITION) {
                *values = static_cast<GLint>(GL_SYNC_GPU_COMMANDS_COMPLETE);
            } else {
                *values = 0;
            }
            if (length) {
                *length = 1;
            }
            return;
        }
    }
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetSynciv) {
        tbl.glGetSynciv(sync, pname, bufSize, length, values);
    }
}

__attribute__((sysv_abi)) void glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindBufferBase) {
        tbl.glBindBufferBase(target, index, buffer);
    }
}

__attribute__((sysv_abi)) void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                                 GLintptr offset, GLsizeiptr size)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindBufferRange) {
        tbl.glBindBufferRange(target, index, buffer, offset, size);
    }
}

__attribute__((sysv_abi)) GLuint glGetUniformBlockIndex(GLuint program,
                                                        const GLchar *uniformBlockName)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGetUniformBlockIndex) {
        return tbl.glGetUniformBlockIndex(program, uniformBlockName);
    }
    return GL_INVALID_INDEX;
}

__attribute__((sysv_abi)) void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                                                     GLuint uniformBlockBinding)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUniformBlockBinding) {
        tbl.glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding);
    }
}

__attribute__((sysv_abi)) void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                                     GLsizei instancecount)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDrawArraysInstanced) {
        tbl.glDrawArraysInstanced(mode, first, count, instancecount);
    }
}

__attribute__((sysv_abi)) void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                                       const void *indices, GLsizei instancecount)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDrawElementsInstanced) {
        tbl.glDrawElementsInstanced(mode, count, type, indices, instancecount);
    }
}

__attribute__((sysv_abi)) void glVertexAttribDivisor(GLuint index, GLuint divisor)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glVertexAttribDivisor) {
        tbl.glVertexAttribDivisor(index, divisor);
    }
}

__attribute__((sysv_abi)) void glGenTransformFeedbacks(GLsizei n, GLuint *ids)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenTransformFeedbacks && tbl.wglGetCurrentContext &&
        tbl.wglGetCurrentContext() != nullptr) {
        tbl.glGenTransformFeedbacks(n, ids);
        return;
    }
    if (ids && n > 0) {
        for (GLsizei i = 0; i < n; ++i) {
            ids[i] = portage::gles::FallbackIdCounter().fetch_add(1);
        }
    }
}

__attribute__((sysv_abi)) void glBindTransformFeedback(GLenum target, GLuint id)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindTransformFeedback) {
        tbl.glBindTransformFeedback(target, id);
    }
}

__attribute__((sysv_abi)) void glBeginTransformFeedback(GLenum primitiveMode)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBeginTransformFeedback) {
        tbl.glBeginTransformFeedback(primitiveMode);
    }
}

__attribute__((sysv_abi)) void glEndTransformFeedback(void)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glEndTransformFeedback) {
        tbl.glEndTransformFeedback();
    }
}

__attribute__((sysv_abi)) void glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                                           const GLchar *const *varyings,
                                                           GLenum bufferMode)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTransformFeedbackVaryings) {
        tbl.glTransformFeedbackVaryings(program, count, varyings, bufferMode);
    }
}

__attribute__((sysv_abi)) void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteTransformFeedbacks) {
        tbl.glDeleteTransformFeedbacks(n, ids);
    }
}

__attribute__((sysv_abi)) void glGenSamplers(GLsizei count, GLuint *samplers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glGenSamplers && tbl.wglGetCurrentContext && tbl.wglGetCurrentContext() != nullptr) {
        tbl.glGenSamplers(count, samplers);
        return;
    }
    if (samplers && count > 0) {
        for (GLsizei i = 0; i < count; ++i) {
            samplers[i] = portage::gles::FallbackIdCounter().fetch_add(1);
        }
    }
}

__attribute__((sysv_abi)) void glBindSampler(GLuint unit, GLuint sampler)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glBindSampler) {
        tbl.glBindSampler(unit, sampler);
    }
}

__attribute__((sysv_abi)) void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glSamplerParameteri) {
        tbl.glSamplerParameteri(sampler, pname, param);
    }
}

__attribute__((sysv_abi)) void glDeleteSamplers(GLsizei count, const GLuint *samplers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDeleteSamplers) {
        tbl.glDeleteSamplers(count, samplers);
    }
}

__attribute__((sysv_abi)) void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                                 GLbitfield access)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glMapBufferRange) {
        return tbl.glMapBufferRange(target, offset, length, access);
    }
    return nullptr;
}

__attribute__((sysv_abi)) GLboolean glUnmapBuffer(GLenum target)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glUnmapBuffer) {
        return tbl.glUnmapBuffer(target);
    }
    return GL_TRUE;
}

__attribute__((sysv_abi)) void glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                                        GLsizeiptr length)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glFlushMappedBufferRange) {
        tbl.glFlushMappedBufferRange(target, offset, length);
    }
}

__attribute__((sysv_abi)) void glPixelStorei(GLenum pname, GLint param)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glPixelStorei) {
        tbl.glPixelStorei(pname, param);
    }
}

__attribute__((sysv_abi)) void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                                              GLsizei width, GLsizei height)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexStorage2D) {
        tbl.glTexStorage2D(target, levels, internalformat, width, height);
    }
}

__attribute__((sysv_abi)) void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                                              GLsizei width, GLsizei height, GLsizei depth)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexStorage3D) {
        tbl.glTexStorage3D(target, levels, internalformat, width, height, depth);
    }
}

__attribute__((sysv_abi)) void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                                            GLsizei width, GLsizei height, GLsizei depth,
                                            GLint border, GLenum format, GLenum type,
                                            const void *pixels)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexImage3D) {
        tbl.glTexImage3D(target, level, internalformat, width, height, depth, border, format, type,
                         pixels);
    }
}

__attribute__((sysv_abi)) void glTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                               GLint yoffset, GLint zoffset, GLsizei width,
                                               GLsizei height, GLsizei depth, GLenum format,
                                               GLenum type, const void *pixels)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glTexSubImage3D) {
        tbl.glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format,
                            type, pixels);
    }
}

__attribute__((sysv_abi)) void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y,
                                                 GLuint num_groups_z)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glDispatchCompute) {
        tbl.glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
    }
}

__attribute__((sysv_abi)) void glMemoryBarrier(GLbitfield barriers)
{
    const auto &tbl = portage::gles::WglBridge::Instance().GetDispatchTable();
    if (tbl.glMemoryBarrier) {
        tbl.glMemoryBarrier(barriers);
    }
}

} // extern "C"