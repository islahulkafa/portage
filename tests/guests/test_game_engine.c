#include <stddef.h>
#include <stdint.h>

#define O_WRONLY 00000001
#define O_CREAT 00000100
#define O_TRUNC 00001000

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_CHILD_CLEARTID 0x00200000

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#define ALOOPER_PREPARE_ALLOW_NON_CALLBACKS 1
#define ALOOPER_EVENT_INPUT 1

#define ANDROID_LOG_INFO 4

#define APP_CMD_INIT_WINDOW 1
#define APP_CMD_GAINED_FOCUS 6
#define APP_CMD_DESTROY 15

#define LOOPER_ID_CMD 1
#define LOOPER_ID_INPUT 2

struct ALooper;
struct AInputQueue;
struct AInputEvent;
struct ANativeWindow;
struct AAssetManager;
struct AAsset;
struct AConfiguration;

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
                                                          struct AInputQueue *queue);
    void(__attribute__((sysv_abi)) * onInputQueueDestroyed)(struct ANativeActivity *activity,
                                                            struct AInputQueue *queue);
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
    struct AAssetManager *assetManager;
    const char *obbPath;
};

// Syscalls
static inline int64_t sys_read(int fd, void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(0), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

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

static inline int64_t sys_pipe2(int *pipefd, int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(293), "D"(pipefd), "S"(flags)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_futex(int *uaddr, int op, int val, const void *timeout)
{
    int64_t ret;
    register const void *r10 __asm__("r10") = timeout;
    register void *r8 __asm__("r8") = NULL;
    register int r9 __asm__("r9") = 0;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(202), "D"(uaddr), "S"(op), "d"(val), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static inline int64_t clone_thread(void (*fn)(void), void *stack_top, int *ctid)
{
    uint64_t flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_CHILD_CLEARTID;
    int64_t ret;
    register int *r10 __asm__("r10") = ctid;
    register unsigned long r8 __asm__("r8") = 0;
    __asm__ volatile("subq $16, %%rsi\n\t"
                     "movq %5, 8(%%rsi)\n\t"
                     "movq $56, %%rax\n\t"
                     "syscall\n\t"
                     "testq %%rax, %%rax\n\t"
                     "jnz 1f\n\t"
                     "movq 8(%%rsp), %%rax\n\t"
                     "addq $16, %%rsp\n\t"
                     "xorq %%rbp, %%rbp\n\t"
                     "callq *%%rax\n\t"
                     "movl $60, %%eax\n\t"
                     "xorl %%edi, %%edi\n\t"
                     "syscall\n\t"
                     "1:\n\t"
                     : "=a"(ret)
                     : "D"(flags), "S"(stack_top), "d"(NULL), "r"(r10), "r"(fn), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

// Helpers
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

static inline void my_memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) {
        *d++ = *s++;
        __asm__ volatile("" : "+r"(d), "+r"(s));
    }
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

// Declarations of NDK functions resolved dynamically
extern struct ALooper *ALooper_prepare(int opts) __attribute__((sysv_abi));
extern struct ALooper *ALooper_forThread(void) __attribute__((sysv_abi));
extern int ALooper_addFd(struct ALooper *looper, int fd, int ident, int events, void *callback,
                         void *data) __attribute__((sysv_abi));
extern int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData)
    __attribute__((sysv_abi));
extern void AInputQueue_attachLooper(struct AInputQueue *queue, struct ALooper *looper, int ident,
                                     void *callback, void *data) __attribute__((sysv_abi));
extern void AInputQueue_detachLooper(struct AInputQueue *queue) __attribute__((sysv_abi));
extern int AInputQueue_getEvent(struct AInputQueue *queue, struct AInputEvent **outEvent)
    __attribute__((sysv_abi));
extern void AInputQueue_finishEvent(struct AInputQueue *queue, struct AInputEvent *event,
                                    int handled) __attribute__((sysv_abi));
extern int32_t AInputEvent_getType(const struct AInputEvent *event) __attribute__((sysv_abi));
extern int32_t AMotionEvent_getAction(const struct AInputEvent *motion_event)
    __attribute__((sysv_abi));
extern int32_t AKeyEvent_getKeyCode(const struct AInputEvent *key_event) __attribute__((sysv_abi));
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...)
    __attribute__((sysv_abi));
extern struct AConfiguration *AConfiguration_new(void) __attribute__((sysv_abi));
extern int32_t AConfiguration_getOrientation(struct AConfiguration *config)
    __attribute__((sysv_abi));
extern void AConfiguration_delete(struct AConfiguration *config) __attribute__((sysv_abi));

struct EngineState {
    struct ANativeActivity *activity;
    struct ALooper *looper;
    struct AInputQueue *inputQueue;
    int msgpipe[2];
    int child_tid;
    volatile int running;
    char internalDataPath[128];
};

static struct EngineState g_engine;
static uint8_t g_thread_stack[65536] __attribute__((aligned(16)));

static void __attribute__((noinline)) engine_worker_func(void)
{
    struct ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    g_engine.looper = looper;

    ALooper_addFd(looper, g_engine.msgpipe[0], LOOPER_ID_CMD, ALOOPER_EVENT_INPUT, NULL, NULL);

    __android_log_print(ANDROID_LOG_INFO, "PortageEngine",
                        "Worker thread started and ALooper prepared");
    write_marker(g_engine.internalDataPath, "engine_worker_ready.txt", "READY");

    while (g_engine.running) {
        int outFd = 0;
        int outEvents = 0;
        void *outData = NULL;
        int ident = ALooper_pollOnce(50, &outFd, &outEvents, &outData);

        if (ident == LOOPER_ID_CMD) {
            uint8_t cmd = 0;
            if (sys_read(g_engine.msgpipe[0], &cmd, 1) > 0) {
                __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "Worker received cmd=%d",
                                    cmd);
                if (cmd == APP_CMD_INIT_WINDOW) {
                    write_marker(g_engine.internalDataPath, "engine_window_init.txt", "OK");
                } else if (cmd == APP_CMD_GAINED_FOCUS) {
                    write_marker(g_engine.internalDataPath, "engine_focus_gained.txt", "OK");
                } else if (cmd == APP_CMD_DESTROY) {
                    write_marker(g_engine.internalDataPath, "engine_destroyed.txt", "OK");
                    g_engine.running = 0;
                    break;
                }
            }
        } else if (ident == LOOPER_ID_INPUT && g_engine.inputQueue) {
            struct AInputEvent *evt = NULL;
            while (AInputQueue_getEvent(g_engine.inputQueue, &evt) >= 0) {
                int32_t type = AInputEvent_getType(evt);
                __android_log_print(ANDROID_LOG_INFO, "PortageEngine",
                                    "Worker received input type=%d", type);
                if (type == 2) { // Motion
                    int32_t act = AMotionEvent_getAction(evt);
                    __android_log_print(ANDROID_LOG_INFO, "PortageEngine",
                                        "Worker motion action=%d", act);
                    write_marker(g_engine.internalDataPath, "engine_motion_received.txt", "OK");
                } else if (type == 1) { // Key
                    int32_t kc = AKeyEvent_getKeyCode(evt);
                    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "Worker key code=%d",
                                        kc);
                    write_marker(g_engine.internalDataPath, "engine_key_received.txt", "OK");
                }
                AInputQueue_finishEvent(g_engine.inputQueue, evt, 1);
            }
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "Worker thread terminating");
    sys_exit(0);
}

// Activity callbacks
static void __attribute__((sysv_abi)) engine_onStart(struct ANativeActivity *activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onStart");
}

static void __attribute__((sysv_abi)) engine_onResume(struct ANativeActivity *activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onResume");
}

static void __attribute__((sysv_abi)) engine_onPause(struct ANativeActivity *activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onPause");
}

static void __attribute__((sysv_abi)) engine_onStop(struct ANativeActivity *activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onStop");
}

static void __attribute__((sysv_abi)) engine_onNativeWindowCreated(struct ANativeActivity *activity,
                                                                   struct ANativeWindow *window)
{
    (void)activity;
    (void)window;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onNativeWindowCreated");
    uint8_t cmd = APP_CMD_INIT_WINDOW;
    sys_write(g_engine.msgpipe[1], &cmd, 1);
}

static void __attribute__((sysv_abi))
engine_onNativeWindowDestroyed(struct ANativeActivity *activity, struct ANativeWindow *window)
{
    (void)activity;
    (void)window;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onNativeWindowDestroyed");
}

static void __attribute__((sysv_abi)) engine_onWindowFocusChanged(struct ANativeActivity *activity,
                                                                  int hasFocus)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onWindowFocusChanged hasFocus=%d",
                        hasFocus);
    if (hasFocus) {
        uint8_t cmd = APP_CMD_GAINED_FOCUS;
        sys_write(g_engine.msgpipe[1], &cmd, 1);
    }
}

static void __attribute__((sysv_abi)) engine_onInputQueueCreated(struct ANativeActivity *activity,
                                                                 struct AInputQueue *queue)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onInputQueueCreated");
    g_engine.inputQueue = queue;
    if (g_engine.looper) {
        AInputQueue_attachLooper(queue, g_engine.looper, LOOPER_ID_INPUT, NULL, NULL);
    }
}

static void __attribute__((sysv_abi)) engine_onInputQueueDestroyed(struct ANativeActivity *activity,
                                                                   struct AInputQueue *queue)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onInputQueueDestroyed");
    if (g_engine.inputQueue == queue) {
        AInputQueue_detachLooper(queue);
        g_engine.inputQueue = NULL;
    }
}

static void __attribute__((sysv_abi)) engine_onDestroy(struct ANativeActivity *activity)
{
    (void)activity;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onDestroy");
    uint8_t cmd = APP_CMD_DESTROY;
    sys_write(g_engine.msgpipe[1], &cmd, 1);

    // Wait for worker thread to exit
    while (g_engine.child_tid != 0) {
        sys_futex(&g_engine.child_tid, FUTEX_WAIT, g_engine.child_tid, NULL);
    }

    sys_close(g_engine.msgpipe[0]);
    sys_close(g_engine.msgpipe[1]);
}

__attribute__((visibility("default"))) void
ANativeActivity_onCreate(struct ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;

    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "ANativeActivity_onCreate started");

    // Test AConfiguration
    struct AConfiguration *config = AConfiguration_new();
    if (config) {
        int32_t orient = AConfiguration_getOrientation(config);
        __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "AConfiguration orientation=%d",
                            orient);
        AConfiguration_delete(config);
    }

    g_engine.activity = activity;
    g_engine.looper = NULL;
    g_engine.inputQueue = NULL;
    g_engine.running = 1;
    g_engine.child_tid = 9999;
    g_engine.internalDataPath[0] = '\0';
    if (activity->internalDataPath) {
        my_memcpy(g_engine.internalDataPath, activity->internalDataPath,
                  my_strlen(activity->internalDataPath) + 1);
    }

    // Create command pipe
    int pfd[2] = {-1, -1};
    if (sys_pipe2(pfd, 0) == 0) {
        g_engine.msgpipe[0] = pfd[0];
        g_engine.msgpipe[1] = pfd[1];
    }

    // Set callbacks
    activity->callbacks->onStart = engine_onStart;
    activity->callbacks->onResume = engine_onResume;
    activity->callbacks->onPause = engine_onPause;
    activity->callbacks->onStop = engine_onStop;
    activity->callbacks->onDestroy = engine_onDestroy;
    activity->callbacks->onWindowFocusChanged = engine_onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = engine_onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = engine_onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = engine_onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = engine_onInputQueueDestroyed;

    // Spawn worker thread
    void *stack_top = g_thread_stack + sizeof(g_thread_stack);
    int64_t ret = clone_thread(engine_worker_func, stack_top, &g_engine.child_tid);
    (void)ret;

    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "ANativeActivity_onCreate finished");
}
