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

// Image / OES External Texturing
__attribute__((sysv_abi)) void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);

// Sync Fences (GLES 3.0)
__attribute__((sysv_abi)) GLsync glFenceSync(GLenum condition, GLbitfield flags);
__attribute__((sysv_abi)) GLboolean glIsSync(GLsync sync);
__attribute__((sysv_abi)) void glDeleteSync(GLsync sync);
__attribute__((sysv_abi)) GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
__attribute__((sysv_abi)) void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
__attribute__((sysv_abi)) void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize,
                                           GLsizei *length, GLint *values);

// Uniform Buffer Objects (GLES 3.0)
__attribute__((sysv_abi)) void glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
__attribute__((sysv_abi)) void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                                 GLintptr offset, GLsizeiptr size);
__attribute__((sysv_abi)) GLuint glGetUniformBlockIndex(GLuint program,
                                                        const GLchar *uniformBlockName);
__attribute__((sysv_abi)) void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                                                     GLuint uniformBlockBinding);

// Instanced Rendering (GLES 3.0)
__attribute__((sysv_abi)) void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                                     GLsizei instancecount);
__attribute__((sysv_abi)) void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                                       const void *indices, GLsizei instancecount);
__attribute__((sysv_abi)) void glVertexAttribDivisor(GLuint index, GLuint divisor);

// Transform Feedback (GLES 3.0)
__attribute__((sysv_abi)) void glGenTransformFeedbacks(GLsizei n, GLuint *ids);
__attribute__((sysv_abi)) void glBindTransformFeedback(GLenum target, GLuint id);
__attribute__((sysv_abi)) void glBeginTransformFeedback(GLenum primitiveMode);
__attribute__((sysv_abi)) void glEndTransformFeedback(void);
__attribute__((sysv_abi)) void glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                                           const GLchar *const *varyings,
                                                           GLenum bufferMode);
__attribute__((sysv_abi)) void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids);

// Sampler Objects (GLES 3.0)
__attribute__((sysv_abi)) void glGenSamplers(GLsizei count, GLuint *samplers);
__attribute__((sysv_abi)) void glBindSampler(GLuint unit, GLuint sampler);
__attribute__((sysv_abi)) void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param);
__attribute__((sysv_abi)) void glDeleteSamplers(GLsizei count, const GLuint *samplers);

// Buffer Mapping & Pixel Store (GLES 3.0)
__attribute__((sysv_abi)) void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                                 GLbitfield access);
__attribute__((sysv_abi)) GLboolean glUnmapBuffer(GLenum target);
__attribute__((sysv_abi)) void glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                                        GLsizeiptr length);
__attribute__((sysv_abi)) void glPixelStorei(GLenum pname, GLint param);

// Texture Storage & 3D Textures (GLES 3.0)
__attribute__((sysv_abi)) void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                                              GLsizei width, GLsizei height);
__attribute__((sysv_abi)) void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                                              GLsizei width, GLsizei height, GLsizei depth);
__attribute__((sysv_abi)) void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                                            GLsizei width, GLsizei height, GLsizei depth,
                                            GLint border, GLenum format, GLenum type,
                                            const void *pixels);
__attribute__((sysv_abi)) void glTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                               GLint yoffset, GLint zoffset, GLsizei width,
                                               GLsizei height, GLsizei depth, GLenum format,
                                               GLenum type, const void *pixels);

// Compute Shaders & Memory Barriers (GLES 3.1 / 3.2)
__attribute__((sysv_abi)) void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y,
                                                 GLuint num_groups_z);
__attribute__((sysv_abi)) void glMemoryBarrier(GLbitfield barriers);

// Pipeline State
__attribute__((sysv_abi)) void glDepthFunc(GLenum func);
__attribute__((sysv_abi)) void glDepthMask(GLboolean flag);
__attribute__((sysv_abi)) void glClearDepthf(GLclampf depth);
__attribute__((sysv_abi)) void glDepthRangef(GLclampf near_val, GLclampf far_val);
__attribute__((sysv_abi)) void glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                                           GLboolean alpha);
__attribute__((sysv_abi)) void glBlendFunc(GLenum sfactor, GLenum dfactor);
__attribute__((sysv_abi)) void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                                                   GLenum dstAlpha);
__attribute__((sysv_abi)) void glBlendEquation(GLenum mode);
__attribute__((sysv_abi)) void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
__attribute__((sysv_abi)) void glBlendColor(GLclampf red, GLclampf green, GLclampf blue,
                                            GLclampf alpha);
__attribute__((sysv_abi)) void glCullFace(GLenum mode);
__attribute__((sysv_abi)) void glFrontFace(GLenum mode);
__attribute__((sysv_abi)) void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
__attribute__((sysv_abi)) void glStencilFunc(GLenum func, GLint ref, GLuint mask);
__attribute__((sysv_abi)) void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                                     GLuint mask);
__attribute__((sysv_abi)) void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
__attribute__((sysv_abi)) void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail,
                                                   GLenum zpass);
__attribute__((sysv_abi)) void glStencilMask(GLuint mask);
__attribute__((sysv_abi)) void glStencilMaskSeparate(GLenum face, GLuint mask);
__attribute__((sysv_abi)) void glLineWidth(GLfloat width);
__attribute__((sysv_abi)) void glPolygonOffset(GLfloat factor, GLfloat units);
__attribute__((sysv_abi)) void glSampleCoverage(GLclampf value, GLboolean invert);
__attribute__((sysv_abi)) void glFlush(void);
__attribute__((sysv_abi)) void glFinish(void);
__attribute__((sysv_abi)) void glHint(GLenum target, GLenum mode);

// Renderbuffers & Framebuffers
__attribute__((sysv_abi)) void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
__attribute__((sysv_abi)) void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
__attribute__((sysv_abi)) void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
__attribute__((sysv_abi)) GLboolean glIsRenderbuffer(GLuint renderbuffer);
__attribute__((sysv_abi)) void glRenderbufferStorage(GLenum target, GLenum internalformat,
                                                     GLsizei width, GLsizei height);
__attribute__((sysv_abi)) void glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                                              GLenum internalformat, GLsizei width,
                                                              GLsizei height);
__attribute__((sysv_abi)) void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                                        GLenum renderbuffertarget,
                                                        GLuint renderbuffer);
__attribute__((sysv_abi)) void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                                 GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                                 GLbitfield mask, GLenum filter);
__attribute__((sysv_abi)) void glDrawBuffers(GLsizei n, const GLenum *bufs);
__attribute__((sysv_abi)) void glReadBuffer(GLenum mode);
__attribute__((sysv_abi)) void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                                                       const GLenum *attachments);
__attribute__((sysv_abi)) void glFramebufferTextureLayer(GLenum target, GLenum attachment,
                                                         GLuint texture, GLint level, GLint layer);

// Textures & Mipmaps
__attribute__((sysv_abi)) void glTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                               GLint yoffset, GLsizei width, GLsizei height,
                                               GLenum format, GLenum type, const void *pixels);
__attribute__((sysv_abi)) void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                                GLint x, GLint y, GLsizei width, GLsizei height,
                                                GLint border);
__attribute__((sysv_abi)) void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                                   GLint yoffset, GLint x, GLint y, GLsizei width,
                                                   GLsizei height);
__attribute__((sysv_abi)) void glCompressedTexImage2D(GLenum target, GLint level,
                                                      GLenum internalformat, GLsizei width,
                                                      GLsizei height, GLint border,
                                                      GLsizei imageSize, const void *data);
__attribute__((sysv_abi)) void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                                         GLint yoffset, GLsizei width,
                                                         GLsizei height, GLenum format,
                                                         GLsizei imageSize, const void *data);
__attribute__((sysv_abi)) void glGenerateMipmap(GLenum target);
__attribute__((sysv_abi)) void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
__attribute__((sysv_abi)) void glTexParameterfv(GLenum target, GLenum pname,
                                                const GLfloat *params);
__attribute__((sysv_abi)) void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);

// Uniforms, Attributes & Program
__attribute__((sysv_abi)) void glUniform2i(GLint location, GLint v0, GLint v1);
__attribute__((sysv_abi)) void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
__attribute__((sysv_abi)) void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
__attribute__((sysv_abi)) void glUniform1iv(GLint location, GLsizei count, const GLint *value);
__attribute__((sysv_abi)) void glUniform2iv(GLint location, GLsizei count, const GLint *value);
__attribute__((sysv_abi)) void glUniform3iv(GLint location, GLsizei count, const GLint *value);
__attribute__((sysv_abi)) void glUniform4iv(GLint location, GLsizei count, const GLint *value);
__attribute__((sysv_abi)) void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
__attribute__((sysv_abi)) void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
__attribute__((sysv_abi)) void glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
__attribute__((sysv_abi)) void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
__attribute__((sysv_abi)) void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
__attribute__((sysv_abi)) void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
__attribute__((sysv_abi)) void glUniform1uiv(GLint location, GLsizei count, const GLuint *value);
__attribute__((sysv_abi)) void glUniform2uiv(GLint location, GLsizei count, const GLuint *value);
__attribute__((sysv_abi)) void glUniform3uiv(GLint location, GLsizei count, const GLuint *value);
__attribute__((sysv_abi)) void glUniform4uiv(GLint location, GLsizei count, const GLuint *value);
__attribute__((sysv_abi)) void glUniformMatrix2fv(GLint location, GLsizei count,
                                                  GLboolean transpose, const GLfloat *value);
__attribute__((sysv_abi)) void glUniformMatrix3fv(GLint location, GLsizei count,
                                                  GLboolean transpose, const GLfloat *value);
__attribute__((sysv_abi)) void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                                                      GLsizei stride, const void *pointer);
__attribute__((sysv_abi)) void glVertexAttrib1f(GLuint index, GLfloat x);
__attribute__((sysv_abi)) void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
__attribute__((sysv_abi)) void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
__attribute__((sysv_abi)) void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                                GLfloat w);
__attribute__((sysv_abi)) void glBindAttribLocation(GLuint program, GLuint index,
                                                    const GLchar *name);
__attribute__((sysv_abi)) void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                                                 GLsizei *length, GLint *size, GLenum *type,
                                                 GLchar *name);
__attribute__((sysv_abi)) void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                                                GLsizei *length, GLint *size, GLenum *type,
                                                GLchar *name);
__attribute__((sysv_abi)) void glDetachShader(GLuint program, GLuint shader);
__attribute__((sysv_abi)) void glValidateProgram(GLuint program);
__attribute__((sysv_abi)) GLboolean glIsShader(GLuint shader);
__attribute__((sysv_abi)) GLboolean glIsProgram(GLuint program);
__attribute__((sysv_abi)) GLboolean glIsBuffer(GLuint buffer);
__attribute__((sysv_abi)) GLboolean glIsTexture(GLuint texture);
__attribute__((sysv_abi)) GLboolean glIsFramebuffer(GLuint framebuffer);
__attribute__((sysv_abi)) GLboolean glIsVertexArray(GLuint array);
__attribute__((sysv_abi)) GLboolean glIsSampler(GLuint sampler);

// Clear Buffers (GLES 3.0)
__attribute__((sysv_abi)) void glClearBufferfv(GLenum buffer, GLint drawbuffer,
                                              const GLfloat *value);
__attribute__((sysv_abi)) void glClearBufferiv(GLenum buffer, GLint drawbuffer,
                                              const GLint *value);
__attribute__((sysv_abi)) void glClearBufferuiv(GLenum buffer, GLint drawbuffer,
                                               const GLuint *value);
__attribute__((sysv_abi)) void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                                              GLint stencil);

// State & Queries
__attribute__((sysv_abi)) void glGetIntegerv(GLenum pname, GLint *data);
__attribute__((sysv_abi)) void glGetInteger64v(GLenum pname, GLint64 *data);
__attribute__((sysv_abi)) void glGetFloatv(GLenum pname, GLfloat *data);
__attribute__((sysv_abi)) void glGetBooleanv(GLenum pname, GLboolean *data);
__attribute__((sysv_abi)) const GLubyte *glGetStringi(GLenum name, GLuint index);
__attribute__((sysv_abi)) void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
__attribute__((sysv_abi)) void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
__attribute__((sysv_abi)) void glGetFramebufferAttachmentParameteriv(GLenum target,
                                                                     GLenum attachment,
                                                                     GLenum pname, GLint *params);
__attribute__((sysv_abi)) void glGetRenderbufferParameteriv(GLenum target, GLenum pname,
                                                            GLint *params);
__attribute__((sysv_abi)) void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
__attribute__((sysv_abi)) void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
__attribute__((sysv_abi)) GLboolean glIsEnabled(GLenum cap);
}