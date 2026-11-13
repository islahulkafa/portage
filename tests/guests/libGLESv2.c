#include <stddef.h>
#include <stdint.h>

#define EXPORT __attribute__((visibility("default"))) __attribute__((sysv_abi))

EXPORT uint32_t glCreateShader(uint32_t type)
{
    (void)type;
    return 0;
}
EXPORT void glShaderSource(uint32_t shader, int32_t count, const char *const *string,
                           const int32_t *length)
{
    (void)shader;
    (void)count;
    (void)string;
    (void)length;
}
EXPORT void glCompileShader(uint32_t shader) { (void)shader; }
EXPORT void glGetShaderiv(uint32_t shader, uint32_t pname, int32_t *params)
{
    (void)shader;
    (void)pname;
    (void)params;
}
EXPORT void glGetShaderInfoLog(uint32_t shader, int32_t bufSize, int32_t *length, char *infoLog)
{
    (void)shader;
    (void)bufSize;
    (void)length;
    (void)infoLog;
}
EXPORT void glDeleteShader(uint32_t shader) { (void)shader; }

EXPORT uint32_t glCreateProgram(void) { return 0; }
EXPORT void glAttachShader(uint32_t program, uint32_t shader)
{
    (void)program;
    (void)shader;
}
EXPORT void glLinkProgram(uint32_t program) { (void)program; }
EXPORT void glGetProgramiv(uint32_t program, uint32_t pname, int32_t *params)
{
    (void)program;
    (void)pname;
    (void)params;
}
EXPORT void glGetProgramInfoLog(uint32_t program, int32_t bufSize, int32_t *length, char *infoLog)
{
    (void)program;
    (void)bufSize;
    (void)length;
    (void)infoLog;
}
EXPORT void glUseProgram(uint32_t program) { (void)program; }
EXPORT void glDeleteProgram(uint32_t program) { (void)program; }

EXPORT int32_t glGetAttribLocation(uint32_t program, const char *name)
{
    (void)program;
    (void)name;
    return -1;
}
EXPORT void glEnableVertexAttribArray(uint32_t index) { (void)index; }
EXPORT void glDisableVertexAttribArray(uint32_t index) { (void)index; }
EXPORT void glVertexAttribPointer(uint32_t index, int32_t size, uint32_t type,
                                  unsigned char normalized, int32_t stride, const void *pointer)
{
    (void)index;
    (void)size;
    (void)type;
    (void)normalized;
    (void)stride;
    (void)pointer;
}
EXPORT int32_t glGetUniformLocation(uint32_t program, const char *name)
{
    (void)program;
    (void)name;
    return -1;
}
EXPORT void glUniform1i(int32_t location, int32_t v0)
{
    (void)location;
    (void)v0;
}
EXPORT void glUniform1f(int32_t location, float v0)
{
    (void)location;
    (void)v0;
}
EXPORT void glUniform4f(int32_t location, float v0, float v1, float v2, float v3)
{
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;
}
EXPORT void glUniformMatrix4fv(int32_t location, int32_t count, unsigned char transpose,
                               const float *value)
{
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
}

EXPORT void glGenBuffers(int32_t n, uint32_t *buffers)
{
    (void)n;
    (void)buffers;
}
EXPORT void glBindBuffer(uint32_t target, uint32_t buffer)
{
    (void)target;
    (void)buffer;
}
EXPORT void glBufferData(uint32_t target, ptrdiff_t size, const void *data, uint32_t usage)
{
    (void)target;
    (void)size;
    (void)data;
    (void)usage;
}
EXPORT void glBufferSubData(uint32_t target, ptrdiff_t offset, ptrdiff_t size, const void *data)
{
    (void)target;
    (void)offset;
    (void)size;
    (void)data;
}
EXPORT void glDeleteBuffers(int32_t n, const uint32_t *buffers)
{
    (void)n;
    (void)buffers;
}

EXPORT void glGenVertexArrays(int32_t n, uint32_t *arrays)
{
    (void)n;
    (void)arrays;
}
EXPORT void glBindVertexArray(uint32_t array) { (void)array; }
EXPORT void glDeleteVertexArrays(int32_t n, const uint32_t *arrays)
{
    (void)n;
    (void)arrays;
}

EXPORT void glActiveTexture(uint32_t texture) { (void)texture; }
EXPORT void glGenTextures(int32_t n, uint32_t *textures)
{
    (void)n;
    (void)textures;
}
EXPORT void glBindTexture(uint32_t target, uint32_t texture)
{
    (void)target;
    (void)texture;
}
EXPORT void glDeleteTextures(int32_t n, const uint32_t *textures)
{
    (void)n;
    (void)textures;
}
EXPORT void glTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width,
                         int32_t height, int32_t border, uint32_t format, uint32_t type,
                         const void *pixels)
{
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
}
EXPORT void glTexParameteri(uint32_t target, uint32_t pname, int32_t param)
{
    (void)target;
    (void)pname;
    (void)param;
}

EXPORT void glGenFramebuffers(int32_t n, uint32_t *framebuffers)
{
    (void)n;
    (void)framebuffers;
}
EXPORT void glBindFramebuffer(uint32_t target, uint32_t framebuffer)
{
    (void)target;
    (void)framebuffer;
}
EXPORT void glFramebufferTexture2D(uint32_t target, uint32_t attachment, uint32_t textarget,
                                   uint32_t texture, int32_t level)
{
    (void)target;
    (void)attachment;
    (void)textarget;
    (void)texture;
    (void)level;
}
EXPORT uint32_t glCheckFramebufferStatus(uint32_t target)
{
    (void)target;
    return 0x8CD5;
}
EXPORT void glDeleteFramebuffers(int32_t n, const uint32_t *framebuffers)
{
    (void)n;
    (void)framebuffers;
}

EXPORT void glClear(uint32_t mask) { (void)mask; }
EXPORT void glClearColor(float red, float green, float blue, float alpha)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}
EXPORT void glViewport(int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}
EXPORT void glEnable(uint32_t cap) { (void)cap; }
EXPORT void glDisable(uint32_t cap) { (void)cap; }
EXPORT void glDrawArrays(uint32_t mode, int32_t first, int32_t count)
{
    (void)mode;
    (void)first;
    (void)count;
}
EXPORT void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void *indices)
{
    (void)mode;
    (void)count;
    (void)type;
    (void)indices;
}
EXPORT uint32_t glGetError(void) { return 0; }
EXPORT const unsigned char *glGetString(uint32_t name)
{
    (void)name;
    return (const unsigned char *)"";
}
EXPORT void glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                         uint32_t type, void *pixels)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)format;
    (void)type;
    (void)pixels;
}

EXPORT void glEGLImageTargetTexture2DOES(uint32_t target, void *image)
{
    (void)target;
    (void)image;
}

EXPORT void *glFenceSync(uint32_t condition, uint32_t flags)
{
    (void)condition;
    (void)flags;
    return (void *)0;
}
EXPORT unsigned char glIsSync(void *sync)
{
    (void)sync;
    return 0;
}
EXPORT void glDeleteSync(void *sync) { (void)sync; }
EXPORT uint32_t glClientWaitSync(void *sync, uint32_t flags, uint64_t timeout)
{
    (void)sync;
    (void)flags;
    (void)timeout;
    return 0x911A;
}
EXPORT void glWaitSync(void *sync, uint32_t flags, uint64_t timeout)
{
    (void)sync;
    (void)flags;
    (void)timeout;
}
EXPORT void glGetSynciv(void *sync, uint32_t pname, int32_t bufSize, int32_t *length,
                        int32_t *values)
{
    (void)sync;
    (void)pname;
    (void)bufSize;
    (void)length;
    (void)values;
}

EXPORT void glBindBufferBase(uint32_t target, uint32_t index, uint32_t buffer)
{
    (void)target;
    (void)index;
    (void)buffer;
}
EXPORT void glBindBufferRange(uint32_t target, uint32_t index, uint32_t buffer, intptr_t offset,
                              intptr_t size)
{
    (void)target;
    (void)index;
    (void)buffer;
    (void)offset;
    (void)size;
}
EXPORT uint32_t glGetUniformBlockIndex(uint32_t program, const char *uniformBlockName)
{
    (void)program;
    (void)uniformBlockName;
    return 0xFFFFFFFFu;
}
EXPORT void glUniformBlockBinding(uint32_t program, uint32_t uniformBlockIndex,
                                  uint32_t uniformBlockBinding)
{
    (void)program;
    (void)uniformBlockIndex;
    (void)uniformBlockBinding;
}

EXPORT void glDrawArraysInstanced(uint32_t mode, int32_t first, int32_t count,
                                  int32_t instancecount)
{
    (void)mode;
    (void)first;
    (void)count;
    (void)instancecount;
}
EXPORT void glDrawElementsInstanced(uint32_t mode, int32_t count, uint32_t type,
                                    const void *indices, int32_t instancecount)
{
    (void)mode;
    (void)count;
    (void)type;
    (void)indices;
    (void)instancecount;
}
EXPORT void glVertexAttribDivisor(uint32_t index, uint32_t divisor)
{
    (void)index;
    (void)divisor;
}

EXPORT void glGenTransformFeedbacks(int32_t n, uint32_t *ids)
{
    (void)n;
    (void)ids;
}
EXPORT void glBindTransformFeedback(uint32_t target, uint32_t id)
{
    (void)target;
    (void)id;
}
EXPORT void glBeginTransformFeedback(uint32_t primitiveMode) { (void)primitiveMode; }
EXPORT void glEndTransformFeedback(void) {}
EXPORT void glTransformFeedbackVaryings(uint32_t program, int32_t count,
                                        const char *const *varyings, uint32_t bufferMode)
{
    (void)program;
    (void)count;
    (void)varyings;
    (void)bufferMode;
}
EXPORT void glDeleteTransformFeedbacks(int32_t n, const uint32_t *ids)
{
    (void)n;
    (void)ids;
}

EXPORT void glGenSamplers(int32_t count, uint32_t *samplers)
{
    (void)count;
    (void)samplers;
}
EXPORT void glBindSampler(uint32_t unit, uint32_t sampler)
{
    (void)unit;
    (void)sampler;
}
EXPORT void glSamplerParameteri(uint32_t sampler, uint32_t pname, int32_t param)
{
    (void)sampler;
    (void)pname;
    (void)param;
}
EXPORT void glDeleteSamplers(int32_t count, const uint32_t *samplers)
{
    (void)count;
    (void)samplers;
}

EXPORT void *glMapBufferRange(uint32_t target, intptr_t offset, intptr_t length, uint32_t access)
{
    (void)target;
    (void)offset;
    (void)length;
    (void)access;
    return (void *)0;
}
EXPORT unsigned char glUnmapBuffer(uint32_t target)
{
    (void)target;
    return 1;
}
EXPORT void glFlushMappedBufferRange(uint32_t target, intptr_t offset, intptr_t length)
{
    (void)target;
    (void)offset;
    (void)length;
}
EXPORT void glPixelStorei(uint32_t pname, int32_t param)
{
    (void)pname;
    (void)param;
}

EXPORT void glTexStorage2D(uint32_t target, int32_t levels, uint32_t internalformat, int32_t width,
                           int32_t height)
{
    (void)target;
    (void)levels;
    (void)internalformat;
    (void)width;
    (void)height;
}
EXPORT void glTexStorage3D(uint32_t target, int32_t levels, uint32_t internalformat, int32_t width,
                           int32_t height, int32_t depth)
{
    (void)target;
    (void)levels;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)depth;
}
EXPORT void glTexImage3D(uint32_t target, int32_t level, int32_t internalformat, int32_t width,
                         int32_t height, int32_t depth, int32_t border, uint32_t format,
                         uint32_t type, const void *pixels)
{
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)depth;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
}
EXPORT void glTexSubImage3D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
                            int32_t zoffset, int32_t width, int32_t height, int32_t depth,
                            uint32_t format, uint32_t type, const void *pixels)
{
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)width;
    (void)height;
    (void)depth;
    (void)format;
    (void)type;
    (void)pixels;
}

EXPORT void glDispatchCompute(uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z)
{
    (void)num_groups_x;
    (void)num_groups_y;
    (void)num_groups_z;
}
EXPORT void glMemoryBarrier(uint32_t barriers) { (void)barriers; }

// Pipeline State
EXPORT void glDepthFunc(uint32_t func) { (void)func; }
EXPORT void glDepthMask(uint8_t flag) { (void)flag; }
EXPORT void glClearDepthf(float depth) { (void)depth; }
EXPORT void glDepthRangef(float near_val, float far_val)
{
    (void)near_val;
    (void)far_val;
}
EXPORT void glColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}
EXPORT void glBlendFunc(uint32_t sfactor, uint32_t dfactor)
{
    (void)sfactor;
    (void)dfactor;
}
EXPORT void glBlendFuncSeparate(uint32_t srcRGB, uint32_t dstRGB, uint32_t srcAlpha,
                                uint32_t dstAlpha)
{
    (void)srcRGB;
    (void)dstRGB;
    (void)srcAlpha;
    (void)dstAlpha;
}
EXPORT void glBlendEquation(uint32_t mode) { (void)mode; }
EXPORT void glBlendEquationSeparate(uint32_t modeRGB, uint32_t modeAlpha)
{
    (void)modeRGB;
    (void)modeAlpha;
}
EXPORT void glBlendColor(float red, float green, float blue, float alpha)
{
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
}
EXPORT void glCullFace(uint32_t mode) { (void)mode; }
EXPORT void glFrontFace(uint32_t mode) { (void)mode; }
EXPORT void glScissor(int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}
EXPORT void glStencilFunc(uint32_t func, int32_t ref, uint32_t mask)
{
    (void)func;
    (void)ref;
    (void)mask;
}
EXPORT void glStencilFuncSeparate(uint32_t face, uint32_t func, int32_t ref, uint32_t mask)
{
    (void)face;
    (void)func;
    (void)ref;
    (void)mask;
}
EXPORT void glStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass)
{
    (void)fail;
    (void)zfail;
    (void)zpass;
}
EXPORT void glStencilOpSeparate(uint32_t face, uint32_t fail, uint32_t zfail, uint32_t zpass)
{
    (void)face;
    (void)fail;
    (void)zfail;
    (void)zpass;
}
EXPORT void glStencilMask(uint32_t mask) { (void)mask; }
EXPORT void glStencilMaskSeparate(uint32_t face, uint32_t mask)
{
    (void)face;
    (void)mask;
}
EXPORT void glLineWidth(float width) { (void)width; }
EXPORT void glPolygonOffset(float factor, float units)
{
    (void)factor;
    (void)units;
}
EXPORT void glSampleCoverage(float value, uint8_t invert)
{
    (void)value;
    (void)invert;
}
EXPORT void glFlush(void) {}
EXPORT void glFinish(void) {}
EXPORT void glHint(uint32_t target, uint32_t mode)
{
    (void)target;
    (void)mode;
}

// Renderbuffers & Framebuffers
EXPORT void glGenRenderbuffers(int32_t n, uint32_t *renderbuffers)
{
    (void)n;
    (void)renderbuffers;
}
EXPORT void glBindRenderbuffer(uint32_t target, uint32_t renderbuffer)
{
    (void)target;
    (void)renderbuffer;
}
EXPORT void glDeleteRenderbuffers(int32_t n, const uint32_t *renderbuffers)
{
    (void)n;
    (void)renderbuffers;
}
EXPORT uint8_t glIsRenderbuffer(uint32_t renderbuffer)
{
    (void)renderbuffer;
    return 0;
}
EXPORT void glRenderbufferStorage(uint32_t target, uint32_t internalformat, int32_t width,
                                  int32_t height)
{
    (void)target;
    (void)internalformat;
    (void)width;
    (void)height;
}
EXPORT void glRenderbufferStorageMultisample(uint32_t target, int32_t samples,
                                             uint32_t internalformat, int32_t width, int32_t height)
{
    (void)target;
    (void)samples;
    (void)internalformat;
    (void)width;
    (void)height;
}
EXPORT void glFramebufferRenderbuffer(uint32_t target, uint32_t attachment,
                                      uint32_t renderbuffertarget, uint32_t renderbuffer)
{
    (void)target;
    (void)attachment;
    (void)renderbuffertarget;
    (void)renderbuffer;
}
EXPORT void glBlitFramebuffer(int32_t srcX0, int32_t srcY0, int32_t srcX1, int32_t srcY1,
                              int32_t dstX0, int32_t dstY0, int32_t dstX1, int32_t dstY1,
                              uint32_t mask, uint32_t filter)
{
    (void)srcX0;
    (void)srcY0;
    (void)srcX1;
    (void)srcY1;
    (void)dstX0;
    (void)dstY0;
    (void)dstX1;
    (void)dstY1;
    (void)mask;
    (void)filter;
}
EXPORT void glDrawBuffers(int32_t n, const uint32_t *bufs)
{
    (void)n;
    (void)bufs;
}
EXPORT void glReadBuffer(uint32_t mode) { (void)mode; }
EXPORT void glInvalidateFramebuffer(uint32_t target, int32_t numAttachments,
                                    const uint32_t *attachments)
{
    (void)target;
    (void)numAttachments;
    (void)attachments;
}
EXPORT void glFramebufferTextureLayer(uint32_t target, uint32_t attachment, uint32_t texture,
                                      int32_t level, int32_t layer)
{
    (void)target;
    (void)attachment;
    (void)texture;
    (void)level;
    (void)layer;
}

// Textures & Mipmaps
EXPORT void glTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
                            int32_t width, int32_t height, uint32_t format, uint32_t type,
                            const void *pixels)
{
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)width;
    (void)height;
    (void)format;
    (void)type;
    (void)pixels;
}
EXPORT void glCopyTexImage2D(uint32_t target, int32_t level, uint32_t internalformat, int32_t x,
                             int32_t y, int32_t width, int32_t height, int32_t border)
{
    (void)target;
    (void)level;
    (void)internalformat;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)border;
}
EXPORT void glCopyTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
                                int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}
EXPORT void glCompressedTexImage2D(uint32_t target, int32_t level, uint32_t internalformat,
                                   int32_t width, int32_t height, int32_t border, int32_t imageSize,
                                   const void *data)
{
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)border;
    (void)imageSize;
    (void)data;
}
EXPORT void glCompressedTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset,
                                      int32_t yoffset, int32_t width, int32_t height,
                                      uint32_t format, int32_t imageSize, const void *data)
{
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)width;
    (void)height;
    (void)format;
    (void)imageSize;
    (void)data;
}
EXPORT void glGenerateMipmap(uint32_t target) { (void)target; }
EXPORT void glTexParameterf(uint32_t target, uint32_t pname, float param)
{
    (void)target;
    (void)pname;
    (void)param;
}
EXPORT void glTexParameterfv(uint32_t target, uint32_t pname, const float *params)
{
    (void)target;
    (void)pname;
    (void)params;
}
EXPORT void glTexParameteriv(uint32_t target, uint32_t pname, const int32_t *params)
{
    (void)target;
    (void)pname;
    (void)params;
}

// Uniforms, Attributes & Program
EXPORT void glUniform2i(int32_t location, int32_t v0, int32_t v1)
{
    (void)location;
    (void)v0;
    (void)v1;
}
EXPORT void glUniform3i(int32_t location, int32_t v0, int32_t v1, int32_t v2)
{
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
}
EXPORT void glUniform4i(int32_t location, int32_t v0, int32_t v1, int32_t v2, int32_t v3)
{
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;
}
EXPORT void glUniform1iv(int32_t location, int32_t count, const int32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform2iv(int32_t location, int32_t count, const int32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform3iv(int32_t location, int32_t count, const int32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform4iv(int32_t location, int32_t count, const int32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform2f(int32_t location, float v0, float v1)
{
    (void)location;
    (void)v0;
    (void)v1;
}
EXPORT void glUniform3f(int32_t location, float v0, float v1, float v2)
{
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
}
EXPORT void glUniform1fv(int32_t location, int32_t count, const float *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform2fv(int32_t location, int32_t count, const float *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform3fv(int32_t location, int32_t count, const float *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform4fv(int32_t location, int32_t count, const float *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform1uiv(int32_t location, int32_t count, const uint32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform2uiv(int32_t location, int32_t count, const uint32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform3uiv(int32_t location, int32_t count, const uint32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniform4uiv(int32_t location, int32_t count, const uint32_t *value)
{
    (void)location;
    (void)count;
    (void)value;
}
EXPORT void glUniformMatrix2fv(int32_t location, int32_t count, uint8_t transpose,
                               const float *value)
{
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
}
EXPORT void glUniformMatrix3fv(int32_t location, int32_t count, uint8_t transpose,
                               const float *value)
{
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
}
EXPORT void glVertexAttribIPointer(uint32_t index, int32_t size, uint32_t type, int32_t stride,
                                   const void *pointer)
{
    (void)index;
    (void)size;
    (void)type;
    (void)stride;
    (void)pointer;
}
EXPORT void glVertexAttrib1f(uint32_t index, float x)
{
    (void)index;
    (void)x;
}
EXPORT void glVertexAttrib2f(uint32_t index, float x, float y)
{
    (void)index;
    (void)x;
    (void)y;
}
EXPORT void glVertexAttrib3f(uint32_t index, float x, float y, float z)
{
    (void)index;
    (void)x;
    (void)y;
    (void)z;
}
EXPORT void glVertexAttrib4f(uint32_t index, float x, float y, float z, float w)
{
    (void)index;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
}
EXPORT void glBindAttribLocation(uint32_t program, uint32_t index, const char *name)
{
    (void)program;
    (void)index;
    (void)name;
}
EXPORT void glGetActiveUniform(uint32_t program, uint32_t index, int32_t bufSize, int32_t *length,
                               int32_t *size, uint32_t *type, char *name)
{
    (void)program;
    (void)index;
    (void)bufSize;
    (void)length;
    (void)size;
    (void)type;
    (void)name;
}
EXPORT void glGetActiveAttrib(uint32_t program, uint32_t index, int32_t bufSize, int32_t *length,
                              int32_t *size, uint32_t *type, char *name)
{
    (void)program;
    (void)index;
    (void)bufSize;
    (void)length;
    (void)size;
    (void)type;
    (void)name;
}
EXPORT void glDetachShader(uint32_t program, uint32_t shader)
{
    (void)program;
    (void)shader;
}
EXPORT void glValidateProgram(uint32_t program) { (void)program; }
EXPORT uint8_t glIsShader(uint32_t shader)
{
    (void)shader;
    return 0;
}
EXPORT uint8_t glIsProgram(uint32_t program)
{
    (void)program;
    return 0;
}
EXPORT uint8_t glIsBuffer(uint32_t buffer)
{
    (void)buffer;
    return 0;
}
EXPORT uint8_t glIsTexture(uint32_t texture)
{
    (void)texture;
    return 0;
}
EXPORT uint8_t glIsFramebuffer(uint32_t framebuffer)
{
    (void)framebuffer;
    return 0;
}
EXPORT uint8_t glIsVertexArray(uint32_t array)
{
    (void)array;
    return 0;
}
EXPORT uint8_t glIsSampler(uint32_t sampler)
{
    (void)sampler;
    return 0;
}

// Clear Buffers
EXPORT void glClearBufferfv(uint32_t buffer, int32_t drawbuffer, const float *value)
{
    (void)buffer;
    (void)drawbuffer;
    (void)value;
}
EXPORT void glClearBufferiv(uint32_t buffer, int32_t drawbuffer, const int32_t *value)
{
    (void)buffer;
    (void)drawbuffer;
    (void)value;
}
EXPORT void glClearBufferuiv(uint32_t buffer, int32_t drawbuffer, const uint32_t *value)
{
    (void)buffer;
    (void)drawbuffer;
    (void)value;
}
EXPORT void glClearBufferfi(uint32_t buffer, int32_t drawbuffer, float depth, int32_t stencil)
{
    (void)buffer;
    (void)drawbuffer;
    (void)depth;
    (void)stencil;
}

// State & Queries
EXPORT void glGetIntegerv(uint32_t pname, int32_t *data)
{
    (void)pname;
    (void)data;
}
EXPORT void glGetInteger64v(uint32_t pname, int64_t *data)
{
    (void)pname;
    (void)data;
}
EXPORT void glGetFloatv(uint32_t pname, float *data)
{
    (void)pname;
    (void)data;
}
EXPORT void glGetBooleanv(uint32_t pname, uint8_t *data)
{
    (void)pname;
    (void)data;
}
EXPORT const uint8_t *glGetStringi(uint32_t name, uint32_t index)
{
    (void)name;
    (void)index;
    return (const uint8_t *)"";
}
EXPORT void glGetTexParameteriv(uint32_t target, uint32_t pname, int32_t *params)
{
    (void)target;
    (void)pname;
    (void)params;
}
EXPORT void glGetTexParameterfv(uint32_t target, uint32_t pname, float *params)
{
    (void)target;
    (void)pname;
    (void)params;
}
EXPORT void glGetFramebufferAttachmentParameteriv(uint32_t target, uint32_t attachment,
                                                  uint32_t pname, int32_t *params)
{
    (void)target;
    (void)attachment;
    (void)pname;
    (void)params;
}
EXPORT void glGetRenderbufferParameteriv(uint32_t target, uint32_t pname, int32_t *params)
{
    (void)target;
    (void)pname;
    (void)params;
}
EXPORT void glGetBufferParameteriv(uint32_t target, uint32_t pname, int32_t *params)
{
    (void)target;
    (void)pname;
    (void)params;
}
EXPORT void glGetVertexAttribiv(uint32_t index, uint32_t pname, int32_t *params)
{
    (void)index;
    (void)pname;
    (void)params;
}
EXPORT uint8_t glIsEnabled(uint32_t cap)
{
    (void)cap;
    return 0;
}
