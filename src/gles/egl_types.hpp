#pragma once

#include <cstdint>

// Khronos EGL Types
using EGLBoolean = unsigned int;
using EGLint = int32_t;
using EGLDisplay = void *;
using EGLConfig = void *;
using EGLSurface = void *;
using EGLContext = void *;
using EGLNativeDisplayType = void *;
using EGLNativeWindowType = void *;
using EGLNativePixmapType = void *;
using EGLenum = unsigned int;
using EGLClientBuffer = void *;
using EGLAttrib = intptr_t;

// Boolean values
inline constexpr EGLBoolean EGL_FALSE = 0;
inline constexpr EGLBoolean EGL_TRUE = 1;

// Special constants
inline constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
inline constexpr EGLContext EGL_NO_CONTEXT = nullptr;
inline constexpr EGLSurface EGL_NO_SURFACE = nullptr;
inline constexpr EGLNativeDisplayType EGL_DEFAULT_DISPLAY = nullptr;
inline constexpr EGLint EGL_DONT_CARE = -1;

// Errors
inline constexpr EGLint EGL_SUCCESS = 0x3000;
inline constexpr EGLint EGL_NOT_INITIALIZED = 0x3001;
inline constexpr EGLint EGL_BAD_ACCESS = 0x3002;
inline constexpr EGLint EGL_BAD_ALLOC = 0x3003;
inline constexpr EGLint EGL_BAD_ATTRIBUTE = 0x3004;
inline constexpr EGLint EGL_BAD_CONFIG = 0x3005;
inline constexpr EGLint EGL_BAD_CONTEXT = 0x3006;
inline constexpr EGLint EGL_BAD_CURRENT_SURFACE = 0x3007;
inline constexpr EGLint EGL_BAD_DISPLAY = 0x3008;
inline constexpr EGLint EGL_BAD_MATCH = 0x3009;
inline constexpr EGLint EGL_BAD_NATIVE_PIXMAP = 0x300A;
inline constexpr EGLint EGL_BAD_NATIVE_WINDOW = 0x300B;
inline constexpr EGLint EGL_BAD_PARAMETER = 0x300C;
inline constexpr EGLint EGL_BAD_SURFACE = 0x300D;
inline constexpr EGLint EGL_CONTEXT_LOST = 0x300E;

// Config attributes
inline constexpr EGLint EGL_BUFFER_SIZE = 0x3020;
inline constexpr EGLint EGL_ALPHA_SIZE = 0x3021;
inline constexpr EGLint EGL_BLUE_SIZE = 0x3022;
inline constexpr EGLint EGL_GREEN_SIZE = 0x3023;
inline constexpr EGLint EGL_RED_SIZE = 0x3024;
inline constexpr EGLint EGL_DEPTH_SIZE = 0x3025;
inline constexpr EGLint EGL_STENCIL_SIZE = 0x3026;
inline constexpr EGLint EGL_CONFIG_CAVEAT = 0x3027;
inline constexpr EGLint EGL_CONFIG_ID = 0x3028;
inline constexpr EGLint EGL_LEVEL = 0x3029;
inline constexpr EGLint EGL_MAX_PBUFFER_HEIGHT = 0x302A;
inline constexpr EGLint EGL_MAX_PBUFFER_PIXELS = 0x302B;
inline constexpr EGLint EGL_MAX_PBUFFER_WIDTH = 0x302C;
inline constexpr EGLint EGL_NATIVE_RENDERABLE = 0x302D;
inline constexpr EGLint EGL_NATIVE_VISUAL_ID = 0x302E;
inline constexpr EGLint EGL_NATIVE_VISUAL_TYPE = 0x302F;
inline constexpr EGLint EGL_SAMPLES = 0x3031;
inline constexpr EGLint EGL_SAMPLE_BUFFERS = 0x3032;
inline constexpr EGLint EGL_SURFACE_TYPE = 0x3033;
inline constexpr EGLint EGL_TRANSPARENT_TYPE = 0x3034;
inline constexpr EGLint EGL_TRANSPARENT_BLUE_VALUE = 0x3035;
inline constexpr EGLint EGL_TRANSPARENT_GREEN_VALUE = 0x3036;
inline constexpr EGLint EGL_TRANSPARENT_RED_VALUE = 0x3037;
inline constexpr EGLint EGL_NONE = 0x3038;
inline constexpr EGLint EGL_BIND_TO_TEXTURE_RGB = 0x3039;
inline constexpr EGLint EGL_BIND_TO_TEXTURE_RGBA = 0x303A;
inline constexpr EGLint EGL_MIN_SWAP_INTERVAL = 0x303B;
inline constexpr EGLint EGL_MAX_SWAP_INTERVAL = 0x303C;
inline constexpr EGLint EGL_LUMINANCE_SIZE = 0x303D;
inline constexpr EGLint EGL_ALPHA_MASK_SIZE = 0x303E;
inline constexpr EGLint EGL_COLOR_BUFFER_TYPE = 0x303F;
inline constexpr EGLint EGL_RENDERABLE_TYPE = 0x3040;
inline constexpr EGLint EGL_MATCH_NATIVE_PIXMAP = 0x3041;
inline constexpr EGLint EGL_CONFORMANT = 0x3042;

// Surface types
inline constexpr EGLint EGL_WINDOW_BIT = 0x0004;
inline constexpr EGLint EGL_PBUFFER_BIT = 0x0001;
inline constexpr EGLint EGL_PIXMAP_BIT = 0x0002;

// Renderable types
inline constexpr EGLint EGL_OPENGL_ES_BIT = 0x0001;
inline constexpr EGLint EGL_OPENVG_BIT = 0x0002;
inline constexpr EGLint EGL_OPENGL_ES2_BIT = 0x0004;
inline constexpr EGLint EGL_OPENGL_BIT = 0x0008;
inline constexpr EGLint EGL_OPENGL_ES3_BIT = 0x00000040;

// Context attributes
inline constexpr EGLint EGL_CONTEXT_CLIENT_VERSION = 0x3098;

// Surface attributes
inline constexpr EGLint EGL_HEIGHT = 0x3056;
inline constexpr EGLint EGL_WIDTH = 0x3057;
inline constexpr EGLint EGL_DRAW = 0x3059;
inline constexpr EGLint EGL_READ = 0x305A;

// Query strings
inline constexpr EGLint EGL_VENDOR = 0x3053;
inline constexpr EGLint EGL_VERSION = 0x3054;
inline constexpr EGLint EGL_EXTENSIONS = 0x3055;
inline constexpr EGLint EGL_CLIENT_APIS = 0x308D;

// Android EGL Extensions
inline constexpr EGLenum EGL_NATIVE_BUFFER_ANDROID = 0x3140;
inline constexpr EGLenum EGL_GL_TEXTURE_2D_KHR = 0x30B1;
inline constexpr EGLenum EGL_IMAGE_PRESERVED_KHR = 0x30D2;
using EGLImageKHR = void *;
inline constexpr EGLImageKHR EGL_NO_IMAGE_KHR = nullptr;

// EGL 1.5 & Sync Fence Extensions
using EGLSync = void *;
using EGLSyncKHR = void *;
using EGLTime = uint64_t;
using EGLTimeKHR = uint64_t;
inline constexpr EGLSync EGL_NO_SYNC = nullptr;
inline constexpr EGLSyncKHR EGL_NO_SYNC_KHR = nullptr;
inline constexpr EGLTime EGL_FOREVER = 0xFFFFFFFFFFFFFFFFULL;
inline constexpr EGLTimeKHR EGL_FOREVER_KHR = 0xFFFFFFFFFFFFFFFFULL;
inline constexpr EGLenum EGL_SYNC_FENCE = 0x30F9;
inline constexpr EGLenum EGL_SYNC_FENCE_KHR = 0x30F9;
inline constexpr EGLint EGL_SYNC_TYPE = 0x30F7;
inline constexpr EGLint EGL_SYNC_STATUS = 0x30F1;
inline constexpr EGLint EGL_SYNC_CONDITION = 0x30F8;
inline constexpr EGLint EGL_SYNC_TYPE_KHR = 0x30F7;
inline constexpr EGLint EGL_SYNC_STATUS_KHR = 0x30F1;
inline constexpr EGLint EGL_SYNC_CONDITION_KHR = 0x30F8;
inline constexpr EGLint EGL_SIGNALED = 0x30F2;
inline constexpr EGLint EGL_UNSIGNALED = 0x30F3;
inline constexpr EGLint EGL_SIGNALED_KHR = 0x30F2;
inline constexpr EGLint EGL_UNSIGNALED_KHR = 0x30F3;
inline constexpr EGLint EGL_SYNC_FLUSH_COMMANDS_BIT = 0x0001;
inline constexpr EGLint EGL_SYNC_FLUSH_COMMANDS_BIT_KHR = 0x0001;
inline constexpr EGLint EGL_TIMEOUT_EXPIRED = 0x30F5;
inline constexpr EGLint EGL_CONDITION_SATISFIED = 0x30F6;
inline constexpr EGLint EGL_TIMEOUT_EXPIRED_KHR = 0x30F5;
inline constexpr EGLint EGL_CONDITION_SATISFIED_KHR = 0x30F6;
inline constexpr EGLint EGL_SYNC_PRIOR_COMMANDS_COMPLETE = 0x30F0;

inline constexpr EGLenum EGL_SYNC_NATIVE_FENCE_ANDROID = 0x3144;
inline constexpr EGLint EGL_SYNC_NATIVE_FENCE_FD_ANDROID = 0x3145;
inline constexpr EGLint EGL_SYNC_NATIVE_FENCE_SIGNALED_ANDROID = 0x3146;
inline constexpr EGLint EGL_NO_NATIVE_FENCE_FD_ANDROID = -1;
