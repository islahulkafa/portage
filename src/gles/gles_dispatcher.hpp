#pragma once

#include "gles/gles_types.hpp"

namespace portage::gles {

// Initializes GLES dispatch and registers all GLES/EGL symbols with EglDispatcher
void InitializeGlesDispatcher() noexcept;

} // namespace portage::gles

extern "C" {
// Shaders & Programs
__attribute__((sysv_abi)) GLuint glCreateShader(GLenum type);
__attribute__((sysv_abi)) void glShaderSource(GLuint shader, GLsizei count,
                                              const GLchar *const *string, const GLint *length);
__attribute__((sysv_abi)) void glCompileShader(GLuint shader);
__attribute__((sysv_abi)) void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
__attribute__((sysv_abi)) void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length,
                                                  GLchar *infoLog);
__attribute__((sysv_abi)) void glDeleteShader(GLuint shader);

__attribute__((sysv_abi)) GLuint glCreateProgram(void);
__attribute__((sysv_abi)) void glAttachShader(GLuint program, GLuint shader);
__attribute__((sysv_abi)) void glLinkProgram(GLuint program);
__attribute__((sysv_abi)) void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
__attribute__((sysv_abi)) void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length,
                                                   GLchar *infoLog);
__attribute__((sysv_abi)) void glUseProgram(GLuint program);
__attribute__((sysv_abi)) void glDeleteProgram(GLuint program);

// Attributes & Uniforms
__attribute__((sysv_abi)) GLint glGetAttribLocation(GLuint program, const GLchar *name);
__attribute__((sysv_abi)) void glEnableVertexAttribArray(GLuint index);
__attribute__((sysv_abi)) void glDisableVertexAttribArray(GLuint index);
__attribute__((sysv_abi)) void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                                     GLboolean normalized, GLsizei stride,
                                                     const void *pointer);
__attribute__((sysv_abi)) GLint glGetUniformLocation(GLuint program, const GLchar *name);
__attribute__((sysv_abi)) void glUniform1i(GLint location, GLint v0);
__attribute__((sysv_abi)) void glUniform1f(GLint location, GLfloat v0);
__attribute__((sysv_abi)) void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                           GLfloat v3);
__attribute__((sysv_abi)) void glUniformMatrix4fv(GLint location, GLsizei count,
                                                  GLboolean transpose, const GLfloat *value);

// Buffers & VAOs
__attribute__((sysv_abi)) void glGenBuffers(GLsizei n, GLuint *buffers);
__attribute__((sysv_abi)) void glBindBuffer(GLenum target, GLuint buffer);
__attribute__((sysv_abi)) void glBufferData(GLenum target, GLsizeiptr size, const void *data,
                                            GLenum usage);
__attribute__((sysv_abi)) void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                               const void *data);
__attribute__((sysv_abi)) void glDeleteBuffers(GLsizei n, const GLuint *buffers);

__attribute__((sysv_abi)) void glGenVertexArrays(GLsizei n, GLuint *arrays);
__attribute__((sysv_abi)) void glBindVertexArray(GLuint array);
__attribute__((sysv_abi)) void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);

// Textures & Framebuffers
__attribute__((sysv_abi)) void glActiveTexture(GLenum texture);
__attribute__((sysv_abi)) void glGenTextures(GLsizei n, GLuint *textures);
__attribute__((sysv_abi)) void glBindTexture(GLenum target, GLuint texture);
__attribute__((sysv_abi)) void glDeleteTextures(GLsizei n, const GLuint *textures);
__attribute__((sysv_abi)) void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                            GLsizei width, GLsizei height, GLint border,
                                            GLenum format, GLenum type, const void *pixels);
__attribute__((sysv_abi)) void glTexParameteri(GLenum target, GLenum pname, GLint param);

__attribute__((sysv_abi)) void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
__attribute__((sysv_abi)) void glBindFramebuffer(GLenum target, GLuint framebuffer);
__attribute__((sysv_abi)) void glFramebufferTexture2D(GLenum target, GLenum attachment,
                                                      GLenum textarget, GLuint texture,
                                                      GLint level);
__attribute__((sysv_abi)) GLenum glCheckFramebufferStatus(GLenum target);
__attribute__((sysv_abi)) void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);

// Core GL
__attribute__((sysv_abi)) void glClear(GLbitfield mask);
__attribute__((sysv_abi)) void glClearColor(GLclampf red, GLclampf green, GLclampf blue,
                                            GLclampf alpha);
__attribute__((sysv_abi)) void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
__attribute__((sysv_abi)) void glEnable(GLenum cap);
__attribute__((sysv_abi)) void glDisable(GLenum cap);
__attribute__((sysv_abi)) void glDrawArrays(GLenum mode, GLint first, GLsizei count);
__attribute__((sysv_abi)) void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                              const void *indices);
__attribute__((sysv_abi)) GLenum glGetError(void);
__attribute__((sysv_abi)) const GLubyte *glGetString(GLenum name);
__attribute__((sysv_abi)) void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                            GLenum format, GLenum type, void *pixels);
}