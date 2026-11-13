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
using PFN_wglCreateContextAttribsARB = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
using PFN_wglShareLists = BOOL(WINAPI *)(HGLRC, HGLRC);
using PFN_wglGetExtensionsStringARB = const char *(WINAPI *)(HDC);

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

// Desktop double-precision fallbacks
using PFN_glClearDepth = void(APIENTRY *)(GLclampd depth);
using PFN_glDepthRange = void(APIENTRY *)(GLclampd near_val, GLclampd far_val);

// Pipeline State
using PFN_glDepthFunc = void(APIENTRY *)(GLenum func);
using PFN_glDepthMask = void(APIENTRY *)(GLboolean flag);
using PFN_glClearDepthf = void(APIENTRY *)(GLclampf depth);
using PFN_glDepthRangef = void(APIENTRY *)(GLclampf near_val, GLclampf far_val);
using PFN_glColorMask = void(APIENTRY *)(GLboolean red, GLboolean green, GLboolean blue,
                                         GLboolean alpha);
using PFN_glBlendFunc = void(APIENTRY *)(GLenum sfactor, GLenum dfactor);
using PFN_glBlendFuncSeparate = void(APIENTRY *)(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                                                 GLenum dstAlpha);
using PFN_glBlendEquation = void(APIENTRY *)(GLenum mode);
using PFN_glBlendEquationSeparate = void(APIENTRY *)(GLenum modeRGB, GLenum modeAlpha);
using PFN_glBlendColor = void(APIENTRY *)(GLclampf red, GLclampf green, GLclampf blue,
                                          GLclampf alpha);
using PFN_glCullFace = void(APIENTRY *)(GLenum mode);
using PFN_glFrontFace = void(APIENTRY *)(GLenum mode);
using PFN_glScissor = void(APIENTRY *)(GLint x, GLint y, GLsizei width, GLsizei height);
using PFN_glStencilFunc = void(APIENTRY *)(GLenum func, GLint ref, GLuint mask);
using PFN_glStencilFuncSeparate = void(APIENTRY *)(GLenum face, GLenum func, GLint ref,
                                                   GLuint mask);
using PFN_glStencilOp = void(APIENTRY *)(GLenum fail, GLenum zfail, GLenum zpass);
using PFN_glStencilOpSeparate = void(APIENTRY *)(GLenum face, GLenum fail, GLenum zfail,
                                                 GLenum zpass);
using PFN_glStencilMask = void(APIENTRY *)(GLuint mask);
using PFN_glStencilMaskSeparate = void(APIENTRY *)(GLenum face, GLuint mask);
using PFN_glLineWidth = void(APIENTRY *)(GLfloat width);
using PFN_glPolygonOffset = void(APIENTRY *)(GLfloat factor, GLfloat units);
using PFN_glSampleCoverage = void(APIENTRY *)(GLclampf value, GLboolean invert);
using PFN_glFlush = void(APIENTRY *)(void);
using PFN_glFinish = void(APIENTRY *)(void);
using PFN_glHint = void(APIENTRY *)(GLenum target, GLenum mode);

// Renderbuffers & Framebuffers
using PFN_glGenRenderbuffers = void(APIENTRY *)(GLsizei n, GLuint *renderbuffers);
using PFN_glBindRenderbuffer = void(APIENTRY *)(GLenum target, GLuint renderbuffer);
using PFN_glDeleteRenderbuffers = void(APIENTRY *)(GLsizei n, const GLuint *renderbuffers);
using PFN_glIsRenderbuffer = GLboolean(APIENTRY *)(GLuint renderbuffer);
using PFN_glRenderbufferStorage = void(APIENTRY *)(GLenum target, GLenum internalformat,
                                                   GLsizei width, GLsizei height);
using PFN_glRenderbufferStorageMultisample = void(APIENTRY *)(GLenum target, GLsizei samples,
                                                              GLenum internalformat, GLsizei width,
                                                              GLsizei height);
using PFN_glFramebufferRenderbuffer = void(APIENTRY *)(GLenum target, GLenum attachment,
                                                       GLenum renderbuffertarget,
                                                       GLuint renderbuffer);
using PFN_glBlitFramebuffer = void(APIENTRY *)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                               GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                               GLbitfield mask, GLenum filter);
using PFN_glDrawBuffers = void(APIENTRY *)(GLsizei n, const GLenum *bufs);
using PFN_glReadBuffer = void(APIENTRY *)(GLenum mode);
using PFN_glInvalidateFramebuffer = void(APIENTRY *)(GLenum target, GLsizei numAttachments,
                                                     const GLenum *attachments);
using PFN_glFramebufferTextureLayer = void(APIENTRY *)(GLenum target, GLenum attachment,
                                                       GLuint texture, GLint level, GLint layer);

// Textures & Mipmaps
using PFN_glTexSubImage2D = void(APIENTRY *)(GLenum target, GLint level, GLint xoffset,
                                             GLint yoffset, GLsizei width, GLsizei height,
                                             GLenum format, GLenum type, const void *pixels);
using PFN_glCopyTexImage2D = void(APIENTRY *)(GLenum target, GLint level, GLenum internalformat,
                                              GLint x, GLint y, GLsizei width, GLsizei height,
                                              GLint border);
using PFN_glCopyTexSubImage2D = void(APIENTRY *)(GLenum target, GLint level, GLint xoffset,
                                                 GLint yoffset, GLint x, GLint y, GLsizei width,
                                                 GLsizei height);
using PFN_glCompressedTexImage2D = void(APIENTRY *)(GLenum target, GLint level,
                                                    GLenum internalformat, GLsizei width,
                                                    GLsizei height, GLint border, GLsizei imageSize,
                                                    const void *data);
using PFN_glCompressedTexSubImage2D = void(APIENTRY *)(GLenum target, GLint level, GLint xoffset,
                                                       GLint yoffset, GLsizei width, GLsizei height,
                                                       GLenum format, GLsizei imageSize,
                                                       const void *data);
using PFN_glGenerateMipmap = void(APIENTRY *)(GLenum target);
using PFN_glTexParameterf = void(APIENTRY *)(GLenum target, GLenum pname, GLfloat param);
using PFN_glTexParameterfv = void(APIENTRY *)(GLenum target, GLenum pname, const GLfloat *params);
using PFN_glTexParameteriv = void(APIENTRY *)(GLenum target, GLenum pname, const GLint *params);

// Uniforms, Attributes & Program
using PFN_glUniform2i = void(APIENTRY *)(GLint location, GLint v0, GLint v1);
using PFN_glUniform3i = void(APIENTRY *)(GLint location, GLint v0, GLint v1, GLint v2);
using PFN_glUniform4i = void(APIENTRY *)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
using PFN_glUniform1iv = void(APIENTRY *)(GLint location, GLsizei count, const GLint *value);
using PFN_glUniform2iv = void(APIENTRY *)(GLint location, GLsizei count, const GLint *value);
using PFN_glUniform3iv = void(APIENTRY *)(GLint location, GLsizei count, const GLint *value);
using PFN_glUniform4iv = void(APIENTRY *)(GLint location, GLsizei count, const GLint *value);
using PFN_glUniform2f = void(APIENTRY *)(GLint location, GLfloat v0, GLfloat v1);
using PFN_glUniform3f = void(APIENTRY *)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
using PFN_glUniform1fv = void(APIENTRY *)(GLint location, GLsizei count, const GLfloat *value);
using PFN_glUniform2fv = void(APIENTRY *)(GLint location, GLsizei count, const GLfloat *value);
using PFN_glUniform3fv = void(APIENTRY *)(GLint location, GLsizei count, const GLfloat *value);
using PFN_glUniform4fv = void(APIENTRY *)(GLint location, GLsizei count, const GLfloat *value);
using PFN_glUniform1uiv = void(APIENTRY *)(GLint location, GLsizei count, const GLuint *value);
using PFN_glUniform2uiv = void(APIENTRY *)(GLint location, GLsizei count, const GLuint *value);
using PFN_glUniform3uiv = void(APIENTRY *)(GLint location, GLsizei count, const GLuint *value);
using PFN_glUniform4uiv = void(APIENTRY *)(GLint location, GLsizei count, const GLuint *value);
using PFN_glUniformMatrix2fv = void(APIENTRY *)(GLint location, GLsizei count, GLboolean transpose,
                                                const GLfloat *value);
using PFN_glUniformMatrix3fv = void(APIENTRY *)(GLint location, GLsizei count, GLboolean transpose,
                                                const GLfloat *value);
using PFN_glVertexAttribIPointer = void(APIENTRY *)(GLuint index, GLint size, GLenum type,
                                                    GLsizei stride, const void *pointer);
using PFN_glVertexAttrib1f = void(APIENTRY *)(GLuint index, GLfloat x);
using PFN_glVertexAttrib2f = void(APIENTRY *)(GLuint index, GLfloat x, GLfloat y);
using PFN_glVertexAttrib3f = void(APIENTRY *)(GLuint index, GLfloat x, GLfloat y, GLfloat z);
using PFN_glVertexAttrib4f = void(APIENTRY *)(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                              GLfloat w);
using PFN_glBindAttribLocation = void(APIENTRY *)(GLuint program, GLuint index, const GLchar *name);
using PFN_glGetActiveUniform = void(APIENTRY *)(GLuint program, GLuint index, GLsizei bufSize,
                                                GLsizei *length, GLint *size, GLenum *type,
                                                GLchar *name);
using PFN_glGetActiveAttrib = void(APIENTRY *)(GLuint program, GLuint index, GLsizei bufSize,
                                               GLsizei *length, GLint *size, GLenum *type,
                                               GLchar *name);
using PFN_glDetachShader = void(APIENTRY *)(GLuint program, GLuint shader);
using PFN_glValidateProgram = void(APIENTRY *)(GLuint program);
using PFN_glIsShader = GLboolean(APIENTRY *)(GLuint shader);
using PFN_glIsProgram = GLboolean(APIENTRY *)(GLuint program);
using PFN_glIsBuffer = GLboolean(APIENTRY *)(GLuint buffer);
using PFN_glIsTexture = GLboolean(APIENTRY *)(GLuint texture);
using PFN_glIsFramebuffer = GLboolean(APIENTRY *)(GLuint framebuffer);
using PFN_glIsVertexArray = GLboolean(APIENTRY *)(GLuint array);
using PFN_glIsSampler = GLboolean(APIENTRY *)(GLuint sampler);

// Clear Buffers (GLES 3.0)
using PFN_glClearBufferfv = void(APIENTRY *)(GLenum buffer, GLint drawbuffer, const GLfloat *value);
using PFN_glClearBufferiv = void(APIENTRY *)(GLenum buffer, GLint drawbuffer, const GLint *value);
using PFN_glClearBufferuiv = void(APIENTRY *)(GLenum buffer, GLint drawbuffer, const GLuint *value);
using PFN_glClearBufferfi = void(APIENTRY *)(GLenum buffer, GLint drawbuffer, GLfloat depth,
                                             GLint stencil);

// State & Queries
using PFN_glGetIntegerv = void(APIENTRY *)(GLenum pname, GLint *data);
using PFN_glGetInteger64v = void(APIENTRY *)(GLenum pname, GLint64 *data);
using PFN_glGetFloatv = void(APIENTRY *)(GLenum pname, GLfloat *data);
using PFN_glGetBooleanv = void(APIENTRY *)(GLenum pname, GLboolean *data);
using PFN_glGetStringi = const GLubyte *(APIENTRY *)(GLenum name, GLuint index);
using PFN_glGetTexParameteriv = void(APIENTRY *)(GLenum target, GLenum pname, GLint *params);
using PFN_glGetTexParameterfv = void(APIENTRY *)(GLenum target, GLenum pname, GLfloat *params);
using PFN_glGetFramebufferAttachmentParameteriv = void(APIENTRY *)(GLenum target, GLenum attachment,
                                                                   GLenum pname, GLint *params);
using PFN_glGetRenderbufferParameteriv = void(APIENTRY *)(GLenum target, GLenum pname,
                                                          GLint *params);
using PFN_glGetBufferParameteriv = void(APIENTRY *)(GLenum target, GLenum pname, GLint *params);
using PFN_glGetVertexAttribiv = void(APIENTRY *)(GLuint index, GLenum pname, GLint *params);
using PFN_glIsEnabled = GLboolean(APIENTRY *)(GLenum cap);

struct WglDispatchTable {
    // WGL
    PFN_wglCreateContext wglCreateContext{nullptr};
    PFN_wglDeleteContext wglDeleteContext{nullptr};
    PFN_wglMakeCurrent wglMakeCurrent{nullptr};
    PFN_wglGetProcAddress wglGetProcAddress{nullptr};
    PFN_wglGetCurrentContext wglGetCurrentContext{nullptr};
    PFN_wglGetCurrentDC wglGetCurrentDC{nullptr};
    PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB{nullptr};
    PFN_wglShareLists wglShareLists{nullptr};
    PFN_wglGetExtensionsStringARB wglGetExtensionsStringARB{nullptr};
    PFN_glClearDepth glClearDepth{nullptr};
    PFN_glDepthRange glDepthRange{nullptr};
    bool has_es2_compat{false};
    bool has_es3_compat{false};

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

    // Pipeline State
    PFN_glDepthFunc glDepthFunc{nullptr};
    PFN_glDepthMask glDepthMask{nullptr};
    PFN_glClearDepthf glClearDepthf{nullptr};
    PFN_glDepthRangef glDepthRangef{nullptr};
    PFN_glColorMask glColorMask{nullptr};
    PFN_glBlendFunc glBlendFunc{nullptr};
    PFN_glBlendFuncSeparate glBlendFuncSeparate{nullptr};
    PFN_glBlendEquation glBlendEquation{nullptr};
    PFN_glBlendEquationSeparate glBlendEquationSeparate{nullptr};
    PFN_glBlendColor glBlendColor{nullptr};
    PFN_glCullFace glCullFace{nullptr};
    PFN_glFrontFace glFrontFace{nullptr};
    PFN_glScissor glScissor{nullptr};
    PFN_glStencilFunc glStencilFunc{nullptr};
    PFN_glStencilFuncSeparate glStencilFuncSeparate{nullptr};
    PFN_glStencilOp glStencilOp{nullptr};
    PFN_glStencilOpSeparate glStencilOpSeparate{nullptr};
    PFN_glStencilMask glStencilMask{nullptr};
    PFN_glStencilMaskSeparate glStencilMaskSeparate{nullptr};
    PFN_glLineWidth glLineWidth{nullptr};
    PFN_glPolygonOffset glPolygonOffset{nullptr};
    PFN_glSampleCoverage glSampleCoverage{nullptr};
    PFN_glFlush glFlush{nullptr};
    PFN_glFinish glFinish{nullptr};
    PFN_glHint glHint{nullptr};

    // Renderbuffers & Framebuffers
    PFN_glGenRenderbuffers glGenRenderbuffers{nullptr};
    PFN_glBindRenderbuffer glBindRenderbuffer{nullptr};
    PFN_glDeleteRenderbuffers glDeleteRenderbuffers{nullptr};
    PFN_glIsRenderbuffer glIsRenderbuffer{nullptr};
    PFN_glRenderbufferStorage glRenderbufferStorage{nullptr};
    PFN_glRenderbufferStorageMultisample glRenderbufferStorageMultisample{nullptr};
    PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer{nullptr};
    PFN_glBlitFramebuffer glBlitFramebuffer{nullptr};
    PFN_glDrawBuffers glDrawBuffers{nullptr};
    PFN_glReadBuffer glReadBuffer{nullptr};
    PFN_glInvalidateFramebuffer glInvalidateFramebuffer{nullptr};
    PFN_glFramebufferTextureLayer glFramebufferTextureLayer{nullptr};

    // Textures & Mipmaps
    PFN_glTexSubImage2D glTexSubImage2D{nullptr};
    PFN_glCopyTexImage2D glCopyTexImage2D{nullptr};
    PFN_glCopyTexSubImage2D glCopyTexSubImage2D{nullptr};
    PFN_glCompressedTexImage2D glCompressedTexImage2D{nullptr};
    PFN_glCompressedTexSubImage2D glCompressedTexSubImage2D{nullptr};
    PFN_glGenerateMipmap glGenerateMipmap{nullptr};
    PFN_glTexParameterf glTexParameterf{nullptr};
    PFN_glTexParameterfv glTexParameterfv{nullptr};
    PFN_glTexParameteriv glTexParameteriv{nullptr};

    // Uniforms, Attributes & Program
    PFN_glUniform2i glUniform2i{nullptr};
    PFN_glUniform3i glUniform3i{nullptr};
    PFN_glUniform4i glUniform4i{nullptr};
    PFN_glUniform1iv glUniform1iv{nullptr};
    PFN_glUniform2iv glUniform2iv{nullptr};
    PFN_glUniform3iv glUniform3iv{nullptr};
    PFN_glUniform4iv glUniform4iv{nullptr};
    PFN_glUniform2f glUniform2f{nullptr};
    PFN_glUniform3f glUniform3f{nullptr};
    PFN_glUniform1fv glUniform1fv{nullptr};
    PFN_glUniform2fv glUniform2fv{nullptr};
    PFN_glUniform3fv glUniform3fv{nullptr};
    PFN_glUniform4fv glUniform4fv{nullptr};
    PFN_glUniform1uiv glUniform1uiv{nullptr};
    PFN_glUniform2uiv glUniform2uiv{nullptr};
    PFN_glUniform3uiv glUniform3uiv{nullptr};
    PFN_glUniform4uiv glUniform4uiv{nullptr};
    PFN_glUniformMatrix2fv glUniformMatrix2fv{nullptr};
    PFN_glUniformMatrix3fv glUniformMatrix3fv{nullptr};
    PFN_glVertexAttribIPointer glVertexAttribIPointer{nullptr};
    PFN_glVertexAttrib1f glVertexAttrib1f{nullptr};
    PFN_glVertexAttrib2f glVertexAttrib2f{nullptr};
    PFN_glVertexAttrib3f glVertexAttrib3f{nullptr};
    PFN_glVertexAttrib4f glVertexAttrib4f{nullptr};
    PFN_glBindAttribLocation glBindAttribLocation{nullptr};
    PFN_glGetActiveUniform glGetActiveUniform{nullptr};
    PFN_glGetActiveAttrib glGetActiveAttrib{nullptr};
    PFN_glDetachShader glDetachShader{nullptr};
    PFN_glValidateProgram glValidateProgram{nullptr};
    PFN_glIsShader glIsShader{nullptr};
    PFN_glIsProgram glIsProgram{nullptr};
    PFN_glIsBuffer glIsBuffer{nullptr};
    PFN_glIsTexture glIsTexture{nullptr};
    PFN_glIsFramebuffer glIsFramebuffer{nullptr};
    PFN_glIsVertexArray glIsVertexArray{nullptr};
    PFN_glIsSampler glIsSampler{nullptr};

    // Clear Buffers
    PFN_glClearBufferfv glClearBufferfv{nullptr};
    PFN_glClearBufferiv glClearBufferiv{nullptr};
    PFN_glClearBufferuiv glClearBufferuiv{nullptr};
    PFN_glClearBufferfi glClearBufferfi{nullptr};

    // State & Queries
    PFN_glGetIntegerv glGetIntegerv{nullptr};
    PFN_glGetInteger64v glGetInteger64v{nullptr};
    PFN_glGetFloatv glGetFloatv{nullptr};
    PFN_glGetBooleanv glGetBooleanv{nullptr};
    PFN_glGetStringi glGetStringi{nullptr};
    PFN_glGetTexParameteriv glGetTexParameteriv{nullptr};
    PFN_glGetTexParameterfv glGetTexParameterfv{nullptr};
    PFN_glGetFramebufferAttachmentParameteriv glGetFramebufferAttachmentParameteriv{nullptr};
    PFN_glGetRenderbufferParameteriv glGetRenderbufferParameteriv{nullptr};
    PFN_glGetBufferParameteriv glGetBufferParameteriv{nullptr};
    PFN_glGetVertexAttribiv glGetVertexAttribiv{nullptr};
    PFN_glIsEnabled glIsEnabled{nullptr};
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

    HGLRC CreateContext(HDC hdc, int gles_version, HGLRC share_context = nullptr) noexcept;
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