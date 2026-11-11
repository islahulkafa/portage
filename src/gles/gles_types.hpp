#pragma once

#include <cstddef>
#include <cstdint>

// Khronos OpenGL ES Types
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLshort = short;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLuint = unsigned int;
using GLfloat = float;
using GLclampf = float;
using GLvoid = void;
using GLchar = char;
using GLintptr = ptrdiff_t;
using GLsizeiptr = ptrdiff_t;
using GLuint64 = uint64_t;
using GLint64 = int64_t;
using GLsync = struct __GLsync *;
using GLeglImageOES = void *;

// Constants
inline constexpr GLboolean GL_FALSE = 0;
inline constexpr GLboolean GL_TRUE = 1;

// Clear buffer bits
inline constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr GLbitfield GL_STENCIL_BUFFER_BIT = 0x00000400;
inline constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000;

// Primitives
inline constexpr GLenum GL_POINTS = 0x0000;
inline constexpr GLenum GL_LINES = 0x0001;
inline constexpr GLenum GL_LINE_LOOP = 0x0002;
inline constexpr GLenum GL_LINE_STRIP = 0x0003;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;
inline constexpr GLenum GL_TRIANGLE_FAN = 0x0006;

// Data types
inline constexpr GLenum GL_BYTE = 0x1400;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_SHORT = 0x1402;
inline constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum GL_INT = 0x1404;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_FIXED = 0x140C;
inline constexpr GLenum GL_UNSIGNED_SHORT_5_6_5 = 0x8363;

// Shaders
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_MAX_VERTEX_ATTRIBS = 0x8869;
inline constexpr GLenum GL_MAX_VERTEX_UNIFORM_VECTORS = 0x8DFB;
inline constexpr GLenum GL_MAX_VARYING_VECTORS = 0x8DFC;
inline constexpr GLenum GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
inline constexpr GLenum GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
inline constexpr GLenum GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872;
inline constexpr GLenum GL_MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD;
inline constexpr GLenum GL_SHADER_TYPE = 0x8B4F;
inline constexpr GLenum GL_DELETE_STATUS = 0x8B80;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_VALIDATE_STATUS = 0x8B83;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum GL_ATTACHED_SHADERS = 0x8B85;
inline constexpr GLenum GL_ACTIVE_UNIFORMS = 0x8B86;
inline constexpr GLenum GL_ACTIVE_UNIFORM_MAX_LENGTH = 0x8B87;
inline constexpr GLenum GL_SHADER_SOURCE_LENGTH = 0x8B88;
inline constexpr GLenum GL_ACTIVE_ATTRIBUTES = 0x8B89;
inline constexpr GLenum GL_ACTIVE_ATTRIBUTE_MAX_LENGTH = 0x8B8A;
inline constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
inline constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D;

// Buffers
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
inline constexpr GLenum GL_STREAM_DRAW = 0x88E0;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_BUFFER_SIZE = 0x8764;
inline constexpr GLenum GL_BUFFER_USAGE = 0x8765;

// Textures
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_LINEAR = 0x2601;
inline constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
inline constexpr GLenum GL_LINEAR_MIPMAP_NEAREST = 0x2701;
inline constexpr GLenum GL_NEAREST_MIPMAP_LINEAR = 0x2702;
inline constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
inline constexpr GLenum GL_REPEAT = 0x2901;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_MIRRORED_REPEAT = 0x8370;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;

// Pixel formats
inline constexpr GLenum GL_ALPHA = 0x1906;
inline constexpr GLenum GL_RGB = 0x1907;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_LUMINANCE = 0x1909;
inline constexpr GLenum GL_LUMINANCE_ALPHA = 0x190A;

// Framebuffers & Renderbuffers
inline constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
inline constexpr GLenum GL_RENDERBUFFER = 0x8D41;
inline constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
inline constexpr GLenum GL_STENCIL_ATTACHMENT = 0x8D20;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;

// Capability enable flags
inline constexpr GLenum GL_CULL_FACE = 0x0B44;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_DITHER = 0x0BD0;
inline constexpr GLenum GL_STENCIL_TEST = 0x0B90;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_SCISSOR_TEST = 0x0C11;

// Blending
inline constexpr GLenum GL_ZERO = 0;
inline constexpr GLenum GL_ONE = 1;
inline constexpr GLenum GL_SRC_COLOR = 0x0300;
inline constexpr GLenum GL_ONE_MINUS_SRC_COLOR = 0x0301;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_DST_ALPHA = 0x0304;
inline constexpr GLenum GL_ONE_MINUS_DST_ALPHA = 0x0305;
inline constexpr GLenum GL_DST_COLOR = 0x0306;
inline constexpr GLenum GL_ONE_MINUS_DST_COLOR = 0x0307;
inline constexpr GLenum GL_SRC_ALPHA_SATURATE = 0x0308;

// Errors
inline constexpr GLenum GL_NO_ERROR = 0;
inline constexpr GLenum GL_INVALID_ENUM = 0x0500;
inline constexpr GLenum GL_INVALID_VALUE = 0x0501;
inline constexpr GLenum GL_INVALID_OPERATION = 0x0502;
inline constexpr GLenum GL_OUT_OF_MEMORY = 0x0505;
inline constexpr GLenum GL_INVALID_FRAMEBUFFER_OPERATION = 0x0506;

// String queries
inline constexpr GLenum GL_VENDOR = 0x1F00;
inline constexpr GLenum GL_RENDERER = 0x1F01;
inline constexpr GLenum GL_VERSION = 0x1F02;
inline constexpr GLenum GL_EXTENSIONS = 0x1F03;
inline constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;

// Uniform Buffer Objects (GLES 3.0)
inline constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11;
inline constexpr GLenum GL_UNIFORM_BUFFER_BINDING = 0x8A28;
inline constexpr GLenum GL_MAX_UNIFORM_BUFFER_BINDINGS = 0x8A2F;
inline constexpr GLuint GL_INVALID_INDEX = 0xFFFFFFFFu;

// Transform Feedback (GLES 3.0)
inline constexpr GLenum GL_TRANSFORM_FEEDBACK = 0x8E22;
inline constexpr GLenum GL_TRANSFORM_FEEDBACK_BUFFER = 0x8C8E;
inline constexpr GLenum GL_INTERLEAVED_ATTRIBS = 0x8C8C;
inline constexpr GLenum GL_SEPARATE_ATTRIBS = 0x8C8D;

// Samplers (GLES 3.0)
inline constexpr GLenum GL_SAMPLER_BINDING = 0x8919;

// Buffer Mapping (GLES 3.0)
inline constexpr GLbitfield GL_MAP_READ_BIT = 0x0001;
inline constexpr GLbitfield GL_MAP_WRITE_BIT = 0x0002;
inline constexpr GLbitfield GL_MAP_INVALIDATE_RANGE_BIT = 0x0004;
inline constexpr GLbitfield GL_MAP_INVALIDATE_BUFFER_BIT = 0x0008;
inline constexpr GLbitfield GL_MAP_FLUSH_EXPLICIT_BIT = 0x0010;
inline constexpr GLbitfield GL_MAP_UNSYNCHRONIZED_BIT = 0x0020;

// Sync Objects & Fences (GLES 3.0)
inline constexpr GLenum GL_SYNC_FENCE = 0x9116;
inline constexpr GLenum GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
inline constexpr GLenum GL_UNSIGNALED = 0x9118;
inline constexpr GLenum GL_SIGNALED = 0x9119;
inline constexpr GLenum GL_ALREADY_SIGNALED = 0x911A;
inline constexpr GLenum GL_TIMEOUT_EXPIRED = 0x911B;
inline constexpr GLenum GL_CONDITION_SATISFIED = 0x911C;
inline constexpr GLenum GL_WAIT_FAILED = 0x911D;
inline constexpr GLenum GL_SYNC_STATUS = 0x9114;
inline constexpr GLenum GL_OBJECT_TYPE = 0x9112;
inline constexpr GLenum GL_SYNC_CONDITION = 0x9113;
inline constexpr GLenum GL_SYNC_FLAGS = 0x9115;
inline constexpr GLbitfield GL_SYNC_FLUSH_COMMANDS_BIT = 0x00000001;
inline constexpr GLuint64 GL_TIMEOUT_IGNORED = 0xFFFFFFFFFFFFFFFFULL;

// Textures 3D & Sized Internal Formats (GLES 3.0)
inline constexpr GLenum GL_TEXTURE_3D = 0x806F;
inline constexpr GLenum GL_TEXTURE_2D_ARRAY = 0x8C1A;
inline constexpr GLenum GL_TEXTURE_WRAP_R = 0x8072;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_RGB8 = 0x8051;
inline constexpr GLenum GL_RGBA16F = 0x881A;
inline constexpr GLenum GL_RGBA32F = 0x8814;

// Pixel Buffer Objects & Store (GLES 3.0)
inline constexpr GLenum GL_PIXEL_PACK_BUFFER = 0x88EB;
inline constexpr GLenum GL_PIXEL_UNPACK_BUFFER = 0x88EC;
inline constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;

// Compute Shaders & Memory Barriers (GLES 3.1)
inline constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
inline constexpr GLenum GL_MAX_COMPUTE_WORK_GROUP_COUNT = 0x91BE;
inline constexpr GLbitfield GL_ALL_BARRIER_BITS = 0xFFFFFFFFu;
inline constexpr GLbitfield GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000u;
inline constexpr GLbitfield GL_TEXTURE_FETCH_BARRIER_BIT = 0x00000008u;
