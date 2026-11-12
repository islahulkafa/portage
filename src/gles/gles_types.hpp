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
using GLdouble = double;
using GLclampd = double;
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
inline constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
inline constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum GL_COLOR_ATTACHMENT1 = 0x8CE1;
inline constexpr GLenum GL_COLOR_ATTACHMENT2 = 0x8CE2;
inline constexpr GLenum GL_COLOR_ATTACHMENT3 = 0x8CE3;
inline constexpr GLenum GL_COLOR_ATTACHMENT4 = 0x8CE4;
inline constexpr GLenum GL_COLOR_ATTACHMENT5 = 0x8CE5;
inline constexpr GLenum GL_COLOR_ATTACHMENT6 = 0x8CE6;
inline constexpr GLenum GL_COLOR_ATTACHMENT7 = 0x8CE7;
inline constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
inline constexpr GLenum GL_STENCIL_ATTACHMENT = 0x8D20;
inline constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
inline constexpr GLenum GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT = 0x8CD6;
inline constexpr GLenum GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT = 0x8CD7;
inline constexpr GLenum GL_FRAMEBUFFER_UNSUPPORTED = 0x8CDD;
inline constexpr GLenum GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE = 0x8D56;
inline constexpr GLenum GL_RENDERBUFFER_WIDTH = 0x8D42;
inline constexpr GLenum GL_RENDERBUFFER_HEIGHT = 0x8D43;
inline constexpr GLenum GL_RENDERBUFFER_INTERNAL_FORMAT = 0x8D44;
inline constexpr GLenum GL_DEPTH_COMPONENT16 = 0x81A5;
inline constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
inline constexpr GLenum GL_DEPTH_COMPONENT32F = 0x8CAC;
inline constexpr GLenum GL_DEPTH24_STENCIL8 = 0x88F0;
inline constexpr GLenum GL_STENCIL_INDEX8 = 0x8D48;
inline constexpr GLenum GL_NONE = 0;
inline constexpr GLenum GL_MAX_COLOR_ATTACHMENTS = 0x8CDF;
inline constexpr GLenum GL_MAX_DRAW_BUFFERS = 0x8824;
inline constexpr GLenum GL_MAX_RENDERBUFFER_SIZE = 0x84E8;
inline constexpr GLenum GL_MAX_SAMPLES = 0x8D57;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE = 0x8CD0;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME = 0x8CD1;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL = 0x8CD2;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE = 0x8CD3;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER = 0x8CD4;

// Capability enable flags
inline constexpr GLenum GL_CULL_FACE = 0x0B44;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_DITHER = 0x0BD0;
inline constexpr GLenum GL_STENCIL_TEST = 0x0B90;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
inline constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
inline constexpr GLenum GL_SAMPLE_COVERAGE = 0x80A0;
inline constexpr GLenum GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E;

// Depth & Stencil Comparison Functions
inline constexpr GLenum GL_NEVER = 0x0200;
inline constexpr GLenum GL_LESS = 0x0201;
inline constexpr GLenum GL_EQUAL = 0x0202;
inline constexpr GLenum GL_LEQUAL = 0x0203;
inline constexpr GLenum GL_GREATER = 0x0204;
inline constexpr GLenum GL_NOTEQUAL = 0x0205;
inline constexpr GLenum GL_GEQUAL = 0x0206;
inline constexpr GLenum GL_ALWAYS = 0x0207;

// Stencil Operations
inline constexpr GLenum GL_KEEP = 0x1E00;
inline constexpr GLenum GL_REPLACE = 0x1E01;
inline constexpr GLenum GL_INCR = 0x1E02;
inline constexpr GLenum GL_DECR = 0x1E03;
inline constexpr GLenum GL_INVERT = 0x150A;
inline constexpr GLenum GL_INCR_WRAP = 0x8507;
inline constexpr GLenum GL_DECR_WRAP = 0x8508;

// Face culling & winding
inline constexpr GLenum GL_FRONT = 0x0404;
inline constexpr GLenum GL_BACK = 0x0405;
inline constexpr GLenum GL_FRONT_AND_BACK = 0x0408;
inline constexpr GLenum GL_CW = 0x0900;
inline constexpr GLenum GL_CCW = 0x0901;

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
inline constexpr GLenum GL_CONSTANT_COLOR = 0x8001;
inline constexpr GLenum GL_ONE_MINUS_CONSTANT_COLOR = 0x8002;
inline constexpr GLenum GL_CONSTANT_ALPHA = 0x8003;
inline constexpr GLenum GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004;
inline constexpr GLenum GL_FUNC_ADD = 0x8006;
inline constexpr GLenum GL_FUNC_SUBTRACT = 0x800A;
inline constexpr GLenum GL_FUNC_REVERSE_SUBTRACT = 0x800B;
inline constexpr GLenum GL_MIN = 0x8007;
inline constexpr GLenum GL_MAX = 0x8008;

// Hints
inline constexpr GLenum GL_FASTEST = 0x1101;
inline constexpr GLenum GL_NICEST = 0x1102;
inline constexpr GLenum GL_DONT_CARE = 0x1100;
inline constexpr GLenum GL_GENERATE_MIPMAP_HINT = 0x8192;

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

// Buffer Clear & Query Targets (GLES 2.0 / 3.0)
inline constexpr GLenum GL_COLOR = 0x1800;
inline constexpr GLenum GL_DEPTH = 0x1801;
inline constexpr GLenum GL_STENCIL = 0x1802;
inline constexpr GLenum GL_DEPTH_STENCIL = 0x84F9;
inline constexpr GLenum GL_MAJOR_VERSION = 0x821B;
inline constexpr GLenum GL_MINOR_VERSION = 0x821C;
inline constexpr GLenum GL_MAX_TEXTURE_SIZE = 0x0D33;
inline constexpr GLenum GL_MAX_VIEWPORT_DIMS = 0x0D3A;
inline constexpr GLenum GL_VIEWPORT = 0x0BA2;
inline constexpr GLenum GL_SCISSOR_BOX = 0x0C10;
inline constexpr GLenum GL_DEPTH_WRITEMASK = 0x0B72;
inline constexpr GLenum GL_COLOR_WRITEMASK = 0x0C23;
inline constexpr GLenum GL_BLEND_SRC_RGB = 0x80C9;
inline constexpr GLenum GL_BLEND_DST_RGB = 0x80C8;
inline constexpr GLenum GL_BLEND_SRC_ALPHA = 0x80CB;
inline constexpr GLenum GL_BLEND_DST_ALPHA = 0x80CA;
inline constexpr GLenum GL_BLEND_EQUATION_RGB = 0x8009;
inline constexpr GLenum GL_BLEND_EQUATION_ALPHA = 0x883D;
inline constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;
inline constexpr GLenum GL_LINE_WIDTH = 0x0B21;
inline constexpr GLenum GL_CULL_FACE_MODE = 0x0B45;
inline constexpr GLenum GL_FRONT_FACE = 0x0B46;
inline constexpr GLenum GL_DEPTH_FUNC = 0x0B74;
inline constexpr GLenum GL_DEPTH_CLEAR_VALUE = 0x0B73;
inline constexpr GLenum GL_DEPTH_RANGE = 0x0B70;
inline constexpr GLenum GL_STENCIL_FUNC = 0x0B92;
inline constexpr GLenum GL_STENCIL_VALUE_MASK = 0x0B93;
inline constexpr GLenum GL_STENCIL_REF = 0x0B97;
inline constexpr GLenum GL_STENCIL_FAIL = 0x0B94;
inline constexpr GLenum GL_STENCIL_PASS_DEPTH_FAIL = 0x0B95;
inline constexpr GLenum GL_STENCIL_PASS_DEPTH_PASS = 0x0B96;
inline constexpr GLenum GL_STENCIL_WRITEMASK = 0x0B98;
inline constexpr GLenum GL_STENCIL_CLEAR_VALUE = 0x0B91;
inline constexpr GLenum GL_COLOR_CLEAR_VALUE = 0x0C22;
inline constexpr GLenum GL_FRAMEBUFFER_BINDING = 0x8CA6;
inline constexpr GLenum GL_RENDERBUFFER_BINDING = 0x8CA7;
inline constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x85B5;

// Texture Parameters & Compression (GLES 3.0)
inline constexpr GLenum GL_TEXTURE_BASE_LEVEL = 0x813C;
inline constexpr GLenum GL_TEXTURE_MAX_LEVEL = 0x813D;
inline constexpr GLenum GL_TEXTURE_MIN_LOD = 0x813A;
inline constexpr GLenum GL_TEXTURE_MAX_LOD = 0x813B;
inline constexpr GLenum GL_TEXTURE_COMPARE_MODE = 0x884C;
inline constexpr GLenum GL_TEXTURE_COMPARE_FUNC = 0x884D;
inline constexpr GLenum GL_COMPARE_REF_TO_TEXTURE = 0x884E;
inline constexpr GLenum GL_COMPRESSED_RGB8_ETC2 = 0x9274;
inline constexpr GLenum GL_COMPRESSED_RGBA8_ETC2_EAC = 0x9278;
inline constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE;

// Shader & Program Types (GLES 2.0 / 3.0)
inline constexpr GLenum GL_FLOAT_VEC2 = 0x8B50;
inline constexpr GLenum GL_FLOAT_VEC3 = 0x8B51;
inline constexpr GLenum GL_FLOAT_VEC4 = 0x8B52;
inline constexpr GLenum GL_INT_VEC2 = 0x8B53;
inline constexpr GLenum GL_INT_VEC3 = 0x8B54;
inline constexpr GLenum GL_INT_VEC4 = 0x8B55;
inline constexpr GLenum GL_BOOL = 0x8B56;
inline constexpr GLenum GL_BOOL_VEC2 = 0x8B57;
inline constexpr GLenum GL_BOOL_VEC3 = 0x8B58;
inline constexpr GLenum GL_BOOL_VEC4 = 0x8B59;
inline constexpr GLenum GL_FLOAT_MAT2 = 0x8B5A;
inline constexpr GLenum GL_FLOAT_MAT3 = 0x8B5B;
inline constexpr GLenum GL_FLOAT_MAT4 = 0x8B5C;
inline constexpr GLenum GL_SAMPLER_2D = 0x8B5E;
inline constexpr GLenum GL_SAMPLER_CUBE = 0x8B60;
