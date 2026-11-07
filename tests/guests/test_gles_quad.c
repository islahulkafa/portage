#include <stddef.h>
#include <stdint.h>

#define O_WRONLY 00000001
#define O_CREAT 00000100
#define O_TRUNC 00001000

#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NO_CONTEXT ((void *)0)
#define EGL_NO_SURFACE ((void *)0)
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_RGBA 0x1908

struct ANativeWindow;
struct ANativeActivity;

struct ANativeActivityCallbacks {
    void(__attribute__((sysv_abi)) * onStart)(struct ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onResume)(struct ANativeActivity *activity);
    void *(__attribute__((sysv_abi)) * onSaveInstanceState)(struct ANativeActivity *activity,
                                                            size_t *outSize);
    void(__attribute__((sysv_abi)) * onPause)(struct ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onStop)(struct ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onDestroy)(struct ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onWindowFocusChanged)(struct ANativeActivity *activity,
                                                           int hasFocus);
    void(__attribute__((sysv_abi)) * onNativeWindowCreated)(struct ANativeActivity *activity,
                                                            struct ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowResized)(struct ANativeActivity *activity,
                                                            struct ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowRedrawNeeded)(struct ANativeActivity *activity,
                                                                 struct ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowDestroyed)(struct ANativeActivity *activity,
                                                              struct ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onInputQueueCreated)(struct ANativeActivity *activity,
                                                          void *queue);
    void(__attribute__((sysv_abi)) * onInputQueueDestroyed)(struct ANativeActivity *activity,
                                                            void *queue);
    void(__attribute__((sysv_abi)) * onContentRectChanged)(struct ANativeActivity *activity,
                                                           const void *rect);
    void(__attribute__((sysv_abi)) * onConfigurationChanged)(struct ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onLowMemory)(struct ANativeActivity *activity);
};

struct ANativeActivity {
    struct ANativeActivityCallbacks *callbacks;
    void *vm;
    void *env;
    void *clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int32_t sdkVersion;
    void *instance;
    void *assetManager;
    const char *obbPath;
};

// Syscalls
static inline int64_t sys_write(int fd, const void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    register int r10 __asm__("r10") = mode;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline size_t my_strlen(const char *s)
{
    size_t len = 0;
    if (!s)
        return 0;
    while (*s) {
        len++;
        s++;
        __asm__ volatile("" : "+r"(s));
    }
    return len;
}

static inline void my_strcat(char *dst, const char *src)
{
    while (*dst)
        dst++;
    while (*src) {
        *dst++ = *src++;
        __asm__ volatile("" : "+r"(dst), "+r"(src));
    }
    *dst = '\0';
}

static void write_marker(const char *dir, const char *filename, const char *content)
{
    char path[256];
    path[0] = '\0';
    my_strcat(path, dir);
    my_strcat(path, "/");
    my_strcat(path, filename);

    int64_t fd = sys_openat(-100, path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        sys_write((int)fd, content, my_strlen(content));
        sys_close((int)fd);
    }
}

// EGL functions
extern void *eglGetDisplay(void *display_id) __attribute__((sysv_abi));
extern unsigned int eglInitialize(void *dpy, int32_t *major, int32_t *minor)
    __attribute__((sysv_abi));
extern unsigned int eglTerminate(void *dpy) __attribute__((sysv_abi));
extern unsigned int eglChooseConfig(void *dpy, const int32_t *attrib_list, void **configs,
                                    int32_t config_size, int32_t *num_config)
    __attribute__((sysv_abi));
extern void *eglCreateWindowSurface(void *dpy, void *config, void *win, const int32_t *attrib_list)
    __attribute__((sysv_abi));
extern unsigned int eglDestroySurface(void *dpy, void *surface) __attribute__((sysv_abi));
extern void *eglCreateContext(void *dpy, void *config, void *share_context,
                              const int32_t *attrib_list) __attribute__((sysv_abi));
extern unsigned int eglDestroyContext(void *dpy, void *ctx) __attribute__((sysv_abi));
extern unsigned int eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx)
    __attribute__((sysv_abi));
extern unsigned int eglSwapBuffers(void *dpy, void *surface) __attribute__((sysv_abi));

// GLES functions
extern uint32_t glCreateShader(uint32_t type) __attribute__((sysv_abi));
extern void glShaderSource(uint32_t shader, int32_t count, const char *const *string,
                           const int32_t *length) __attribute__((sysv_abi));
extern void glCompileShader(uint32_t shader) __attribute__((sysv_abi));
extern void glGetShaderiv(uint32_t shader, uint32_t pname, int32_t *params)
    __attribute__((sysv_abi));
extern void glDeleteShader(uint32_t shader) __attribute__((sysv_abi));
extern uint32_t glCreateProgram(void) __attribute__((sysv_abi));
extern void glAttachShader(uint32_t program, uint32_t shader) __attribute__((sysv_abi));
extern void glLinkProgram(uint32_t program) __attribute__((sysv_abi));
extern void glGetProgramiv(uint32_t program, uint32_t pname, int32_t *params)
    __attribute__((sysv_abi));
extern void glUseProgram(uint32_t program) __attribute__((sysv_abi));
extern void glDeleteProgram(uint32_t program) __attribute__((sysv_abi));
extern int32_t glGetAttribLocation(uint32_t program, const char *name) __attribute__((sysv_abi));
extern void glEnableVertexAttribArray(uint32_t index) __attribute__((sysv_abi));
extern void glVertexAttribPointer(uint32_t index, int32_t size, uint32_t type,
                                  unsigned char normalized, int32_t stride, const void *pointer)
    __attribute__((sysv_abi));
extern void glGenBuffers(int32_t n, uint32_t *buffers) __attribute__((sysv_abi));
extern void glBindBuffer(uint32_t target, uint32_t buffer) __attribute__((sysv_abi));
extern void glBufferData(uint32_t target, ptrdiff_t size, const void *data, uint32_t usage)
    __attribute__((sysv_abi));
extern void glDeleteBuffers(int32_t n, const uint32_t *buffers) __attribute__((sysv_abi));
extern void glClear(uint32_t mask) __attribute__((sysv_abi));
extern void glClearColor(float red, float green, float blue, float alpha) __attribute__((sysv_abi));
extern void glViewport(int32_t x, int32_t y, int32_t width, int32_t height)
    __attribute__((sysv_abi));
extern void glDrawArrays(uint32_t mode, int32_t first, int32_t count) __attribute__((sysv_abi));
extern void glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                         uint32_t type, void *pixels) __attribute__((sysv_abi));

static const char *kVertexShaderSource = "attribute vec2 a_pos;\n"
                                         "void main() {\n"
                                         "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
                                         "}\n";

static const char *kFragmentShaderSource =
    "void main() {\n"
    "    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n" // Solid green
    "}\n";

static const float kQuadVertices[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f,
                                      -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  0.5f};

static char g_internal_dir[128];

static void __attribute__((sysv_abi)) engine_onNativeWindowCreated(struct ANativeActivity *activity,
                                                                   struct ANativeWindow *window)
{
    (void)activity;

    void *dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!dpy)
        return;

    int32_t major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor))
        return;

    const int32_t attribs[] = {EGL_RENDERABLE_TYPE,
                               EGL_OPENGL_ES2_BIT,
                               EGL_SURFACE_TYPE,
                               EGL_WINDOW_BIT,
                               EGL_RED_SIZE,
                               8,
                               EGL_GREEN_SIZE,
                               8,
                               EGL_BLUE_SIZE,
                               8,
                               EGL_ALPHA_SIZE,
                               8,
                               EGL_NONE};

    void *config = (void *)0;
    int32_t num_configs = 0;
    if (!eglChooseConfig(dpy, attribs, &config, 1, &num_configs) || num_configs < 1) {
        eglTerminate(dpy);
        return;
    }

    void *surface = eglCreateWindowSurface(dpy, config, window, (void *)0);
    if (!surface) {
        eglTerminate(dpy);
        return;
    }

    const int32_t ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    void *context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (!context) {
        eglDestroySurface(dpy, surface);
        eglTerminate(dpy);
        return;
    }

    if (!eglMakeCurrent(dpy, surface, surface, context)) {
        eglDestroyContext(dpy, context);
        eglDestroySurface(dpy, surface);
        eglTerminate(dpy);
        return;
    }

    // Compile vertex shader
    uint32_t vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVertexShaderSource, (void *)0);
    glCompileShader(vs);
    int32_t compiled = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);

    // Compile fragment shader
    uint32_t fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFragmentShaderSource, (void *)0);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &compiled);

    // Link program
    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int32_t linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    glUseProgram(prog);

    // Upload geometry VBO
    uint32_t vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    int32_t pos_loc = glGetAttribLocation(prog, "a_pos");
    glEnableVertexAttribArray((uint32_t)pos_loc);
    glVertexAttribPointer((uint32_t)pos_loc, 2, GL_FLOAT, 0, 2 * sizeof(float), (void *)0);

    // Render viewport
    int width = 800;
    int height = 600;
    glViewport(0, 0, width, height);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Read back center pixel
    uint8_t pixel[4] = {0, 0, 0, 0};
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    // Verify green component is dominant: R=0, G=255 (or >200), B=0
    if (pixel[1] >= 200 && pixel[0] < 50 && pixel[2] < 50) {
        write_marker(g_internal_dir, "gles_render_success.txt", "SUCCESS");
    }

    eglSwapBuffers(dpy, surface);

    // Clean up
    glDeleteBuffers(1, &vbo);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(prog);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
}

__attribute__((visibility("default"))) void
ANativeActivity_onCreate(struct ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;

    g_internal_dir[0] = '\0';
    if (activity && activity->internalDataPath) {
        my_strcat(g_internal_dir, activity->internalDataPath);
    }

    if (activity && activity->callbacks) {
        activity->callbacks->onNativeWindowCreated = engine_onNativeWindowCreated;
    }
}
