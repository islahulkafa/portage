#include "gles/gles_dispatcher.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/wgl_bridge.hpp"
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace portage::gles {

static std::string SanitizeGlesShaderSource(std::string_view src)
{
    std::string out(src);

    // 1. Remove "precision <qualifier> <type>;" by replacing with spaces
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

    // 2. Prepend compatibility defines for lowp, mediump, highp if not already present
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

#undef REG_PROC
}

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
        return reinterpret_cast<const GLubyte *>("OpenGL ES 3.0 (Portage Desktop OpenGL)");
    if (name == GL_SHADING_LANGUAGE_VERSION)
        return reinterpret_cast<const GLubyte *>("OpenGL ES GLSL ES 3.00");
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

} // extern "C"