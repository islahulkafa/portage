#include <stdint.h>

typedef void *JNIEnv;
typedef void *JavaVM;
typedef void *jobject;
typedef int32_t jint;

struct ANativeWindow;

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern struct ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface);
extern int32_t ANativeWindow_getWidth(struct ANativeWindow *window);
extern int32_t ANativeWindow_getHeight(struct ANativeWindow *window);
extern void ANativeWindow_release(struct ANativeWindow *window);

__attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved;
    __android_log_print(4, "JavaHybrid", "[javahybrid] JNI_OnLoad invoked");
    return 0x00010006;
}

__attribute__((visibility("default"))) jint
Java_com_portage_hybrid_MainActivity_nativeOnSurfaceReady(JNIEnv *env, jobject thiz,
                                                          jobject surface)
{
    (void)thiz;
    struct ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    if (!win) {
        __android_log_print(6, "JavaHybrid",
                            "[javahybrid] ANativeWindow_fromSurface returned null");
        return -1;
    }
    int32_t w = ANativeWindow_getWidth(win);
    int32_t h = ANativeWindow_getHeight(win);
    ANativeWindow_release(win);
    __android_log_print(4, "JavaHybrid", "[javahybrid] surface verified %dx%d", w, h);
    return w * 10000 + h;
}
