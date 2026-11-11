#include <stdbool.h>
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
#define LOOPER_ID_SENSOR 3

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
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4

#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_A 29
#define AKEYCODE_D 32
#define AKEYCODE_S 47
#define AKEYCODE_W 51

struct ALooper;
struct AInputQueue;
struct AInputEvent;
struct ANativeWindow;
struct AAssetManager;
struct AAsset;
struct AConfiguration;

struct ANativeActivity;

typedef int32_t jint;
typedef int64_t jlong;
typedef int8_t jbyte;
typedef uint8_t jboolean;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef float jfloat;
typedef double jdouble;
typedef int32_t jsize;

typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jobject jobjectArray;
typedef jobject jbooleanArray;
typedef jobject jbyteArray;
typedef jobject jcharArray;
typedef jobject jshortArray;
typedef jobject jintArray;
typedef jobject jlongArray;
typedef jobject jfloatArray;
typedef jobject jdoubleArray;
typedef jobject jthrowable;
typedef jobject jweak;
typedef void *jmethodID;
typedef void *jfieldID;

struct JNINativeMethod {
    const char *name;
    const char *signature;
    void *fnPtr;
};

struct JavaVM;
struct JNINativeInterface;
typedef const struct JNINativeInterface *JNIEnv;

struct JNINativeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    void *reserved3;

    jint (*GetVersion)(JNIEnv *);
    jclass (*DefineClass)(JNIEnv *, const char *, jobject, const jbyte *, jsize);
    jclass (*FindClass)(JNIEnv *, const char *name);
    jmethodID (*FromReflectedMethod)(JNIEnv *, jobject);
    jfieldID (*FromReflectedField)(JNIEnv *, jobject);
    jobject (*ToReflectedMethod)(JNIEnv *, jclass, jmethodID, jboolean);
    jclass (*GetSuperclass)(JNIEnv *, jclass);
    jboolean (*IsAssignableFrom)(JNIEnv *, jclass, jclass);
    jobject (*ToReflectedField)(JNIEnv *, jclass, jfieldID, jboolean);
    jint (*Throw)(JNIEnv *, jthrowable);
    jint (*ThrowNew)(JNIEnv *, jclass, const char *);
    jthrowable (*ExceptionOccurred)(JNIEnv *);
    void (*ExceptionDescribe)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
    void (*FatalError)(JNIEnv *, const char *);
    jint (*PushLocalFrame)(JNIEnv *, jint);
    jobject (*PopLocalFrame)(JNIEnv *, jobject);
    jobject (*NewGlobalRef)(JNIEnv *, jobject);
    void (*DeleteGlobalRef)(JNIEnv *, jobject);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
    jboolean (*IsSameObject)(JNIEnv *, jobject, jobject);
    jobject (*NewLocalRef)(JNIEnv *, jobject);
    jint (*EnsureLocalCapacity)(JNIEnv *, jint);
    jobject (*AllocObject)(JNIEnv *, jclass);
    jobject (*NewObject)(JNIEnv *, jclass, jmethodID, ...);
    jobject (*NewObjectV)(JNIEnv *, jclass, jmethodID, void *);
    jobject (*NewObjectA)(JNIEnv *, jclass, jmethodID, const void *);
    jclass (*GetObjectClass)(JNIEnv *, jobject);
    jboolean (*IsInstanceOf)(JNIEnv *, jobject, jclass);
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *name, const char *sig);

    jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    jobject (*CallObjectMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jobject (*CallObjectMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jboolean (*CallBooleanMethod)(JNIEnv *, jobject, jmethodID, ...);
    jboolean (*CallBooleanMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jboolean (*CallBooleanMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jbyte (*CallByteMethod)(JNIEnv *, jobject, jmethodID, ...);
    jbyte (*CallByteMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jbyte (*CallByteMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jchar (*CallCharMethod)(JNIEnv *, jobject, jmethodID, ...);
    jchar (*CallCharMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jchar (*CallCharMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jshort (*CallShortMethod)(JNIEnv *, jobject, jmethodID, ...);
    jshort (*CallShortMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jshort (*CallShortMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jint (*CallIntMethod)(JNIEnv *, jobject, jmethodID, ...);
    jint (*CallIntMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jint (*CallIntMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jlong (*CallLongMethod)(JNIEnv *, jobject, jmethodID, ...);
    jlong (*CallLongMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jlong (*CallLongMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jfloat (*CallFloatMethod)(JNIEnv *, jobject, jmethodID, ...);
    jfloat (*CallFloatMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jfloat (*CallFloatMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jdouble (*CallDoubleMethod)(JNIEnv *, jobject, jmethodID, ...);
    jdouble (*CallDoubleMethodV)(JNIEnv *, jobject, jmethodID, void *);
    jdouble (*CallDoubleMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    void (*CallVoidMethod)(JNIEnv *, jobject, jmethodID, ...);
    void (*CallVoidMethodV)(JNIEnv *, jobject, jmethodID, void *);
    void (*CallVoidMethodA)(JNIEnv *, jobject, jmethodID, const void *);

    void *nonvirtual_methods[27];

    jfieldID (*GetFieldID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*GetObjectField)(JNIEnv *, jobject, jfieldID);
    jboolean (*GetBooleanField)(JNIEnv *, jobject, jfieldID);
    jbyte (*GetByteField)(JNIEnv *, jobject, jfieldID);
    jchar (*GetCharField)(JNIEnv *, jobject, jfieldID);
    jshort (*GetShortField)(JNIEnv *, jobject, jfieldID);
    jint (*GetIntField)(JNIEnv *, jobject, jfieldID);
    jlong (*GetLongField)(JNIEnv *, jobject, jfieldID);
    jfloat (*GetFloatField)(JNIEnv *, jobject, jfieldID);
    jdouble (*GetDoubleField)(JNIEnv *, jobject, jfieldID);
    void (*SetObjectField)(JNIEnv *, jobject, jfieldID, jobject);
    void (*SetBooleanField)(JNIEnv *, jobject, jfieldID, jboolean);
    void (*SetByteField)(JNIEnv *, jobject, jfieldID, jbyte);
    void (*SetCharField)(JNIEnv *, jobject, jfieldID, jchar);
    void (*SetShortField)(JNIEnv *, jobject, jfieldID, jshort);
    void (*SetIntField)(JNIEnv *, jobject, jfieldID, jint);
    void (*SetLongField)(JNIEnv *, jobject, jfieldID, jlong);
    void (*SetFloatField)(JNIEnv *, jobject, jfieldID, jfloat);
    void (*SetDoubleField)(JNIEnv *, jobject, jfieldID, jdouble);

    jmethodID (*GetStaticMethodID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*CallStaticObjectMethod)(JNIEnv *, jclass, jmethodID, ...);
    jobject (*CallStaticObjectMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jobject (*CallStaticObjectMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jboolean (*CallStaticBooleanMethod)(JNIEnv *, jclass, jmethodID, ...);
    jboolean (*CallStaticBooleanMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jboolean (*CallStaticBooleanMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jbyte (*CallStaticByteMethod)(JNIEnv *, jclass, jmethodID, ...);
    jbyte (*CallStaticByteMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jbyte (*CallStaticByteMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jchar (*CallStaticCharMethod)(JNIEnv *, jclass, jmethodID, ...);
    jchar (*CallStaticCharMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jchar (*CallStaticCharMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jshort (*CallStaticShortMethod)(JNIEnv *, jclass, jmethodID, ...);
    jshort (*CallStaticShortMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jshort (*CallStaticShortMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jint (*CallStaticIntMethod)(JNIEnv *, jclass, jmethodID, ...);
    jint (*CallStaticIntMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jint (*CallStaticIntMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jlong (*CallStaticLongMethod)(JNIEnv *, jclass, jmethodID, ...);
    jlong (*CallStaticLongMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jlong (*CallStaticLongMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jfloat (*CallStaticFloatMethod)(JNIEnv *, jclass, jmethodID, ...);
    jfloat (*CallStaticFloatMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jfloat (*CallStaticFloatMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    jdouble (*CallStaticDoubleMethod)(JNIEnv *, jclass, jmethodID, ...);
    jdouble (*CallStaticDoubleMethodV)(JNIEnv *, jclass, jmethodID, void *);
    jdouble (*CallStaticDoubleMethodA)(JNIEnv *, jclass, jmethodID, const void *);
    void (*CallStaticVoidMethod)(JNIEnv *, jclass, jmethodID, ...);
    void (*CallStaticVoidMethodV)(JNIEnv *, jclass, jmethodID, void *);
    void (*CallStaticVoidMethodA)(JNIEnv *, jclass, jmethodID, const void *);

    jfieldID (*GetStaticFieldID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*GetStaticObjectField)(JNIEnv *, jclass, jfieldID);
    jboolean (*GetStaticBooleanField)(JNIEnv *, jclass, jfieldID);
    jbyte (*GetStaticByteField)(JNIEnv *, jclass, jfieldID);
    jchar (*GetStaticCharField)(JNIEnv *, jclass, jfieldID);
    jshort (*GetStaticShortField)(JNIEnv *, jclass, jfieldID);
    jint (*GetStaticIntField)(JNIEnv *, jclass, jfieldID);
    jlong (*GetStaticLongField)(JNIEnv *, jclass, jfieldID);
    jfloat (*GetStaticFloatField)(JNIEnv *, jclass, jfieldID);
    jdouble (*GetStaticDoubleField)(JNIEnv *, jclass, jfieldID);
    void (*SetStaticObjectField)(JNIEnv *, jclass, jfieldID, jobject);
    void (*SetStaticBooleanField)(JNIEnv *, jclass, jfieldID, jboolean);
    void (*SetStaticByteField)(JNIEnv *, jclass, jfieldID, jbyte);
    void (*SetStaticCharField)(JNIEnv *, jclass, jfieldID, jchar);
    void (*SetStaticShortField)(JNIEnv *, jclass, jfieldID, jshort);
    void (*SetStaticIntField)(JNIEnv *, jclass, jfieldID, jint);
    void (*SetStaticLongField)(JNIEnv *, jclass, jfieldID, jlong);
    void (*SetStaticFloatField)(JNIEnv *, jclass, jfieldID, jfloat);
    void (*SetStaticDoubleField)(JNIEnv *, jclass, jfieldID, jdouble);

    jstring (*NewString)(JNIEnv *, const jchar *, jsize);
    jsize (*GetStringLength)(JNIEnv *, jstring);
    const jchar *(*GetStringChars)(JNIEnv *, jstring, jboolean *);
    void (*ReleaseStringChars)(JNIEnv *, jstring, const jchar *);
    jstring (*NewStringUTF)(JNIEnv *, const char *);
    jsize (*GetStringUTFLength)(JNIEnv *, jstring);
    const char *(*GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
    void (*ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);

    jsize (*GetArrayLength)(JNIEnv *, jarray);
    jobjectArray (*NewObjectArray)(JNIEnv *, jsize, jclass, jobject);
    jobject (*GetObjectArrayElement)(JNIEnv *, jobjectArray, jsize);
    void (*SetObjectArrayElement)(JNIEnv *, jobjectArray, jsize, jobject);
    jbooleanArray (*NewBooleanArray)(JNIEnv *, jsize);
    jbyteArray (*NewByteArray)(JNIEnv *, jsize);
    jcharArray (*NewCharArray)(JNIEnv *, jsize);
    jshortArray (*NewShortArray)(JNIEnv *, jsize);
    jintArray (*NewIntArray)(JNIEnv *, jsize);
    jlongArray (*NewLongArray)(JNIEnv *, jsize);
    jfloatArray (*NewFloatArray)(JNIEnv *, jsize);
    jdoubleArray (*NewDoubleArray)(JNIEnv *, jsize);

    jboolean *(*GetBooleanArrayElements)(JNIEnv *, jbooleanArray, jboolean *);
    jbyte *(*GetByteArrayElements)(JNIEnv *, jbyteArray, jboolean *);
    jchar *(*GetCharArrayElements)(JNIEnv *, jcharArray, jboolean *);
    jshort *(*GetShortArrayElements)(JNIEnv *, jshortArray, jboolean *);
    jint *(*GetIntArrayElements)(JNIEnv *, jintArray, jboolean *);
    jlong *(*GetLongArrayElements)(JNIEnv *, jlongArray, jboolean *);
    jfloat *(*GetFloatArrayElements)(JNIEnv *, jfloatArray, jboolean *);
    jdouble *(*GetDoubleArrayElements)(JNIEnv *, jdoubleArray, jboolean *);

    void (*ReleaseBooleanArrayElements)(JNIEnv *, jbooleanArray, jboolean *, jint);
    void (*ReleaseByteArrayElements)(JNIEnv *, jbyteArray, jbyte *, jint);
    void (*ReleaseCharArrayElements)(JNIEnv *, jcharArray, jchar *, jint);
    void (*ReleaseShortArrayElements)(JNIEnv *, jshortArray, jshort *, jint);
    void (*ReleaseIntArrayElements)(JNIEnv *, jintArray, jint *, jint);
    void (*ReleaseLongArrayElements)(JNIEnv *, jlongArray, jlong *, jint);
    void (*ReleaseFloatArrayElements)(JNIEnv *, jfloatArray, jfloat *, jint);
    void (*ReleaseDoubleArrayElements)(JNIEnv *, jdoubleArray, jdouble *, jint);

    void (*GetBooleanArrayRegion)(JNIEnv *, jbooleanArray, jsize, jsize, jboolean *);
    void (*GetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize, jbyte *);
    void (*GetCharArrayRegion)(JNIEnv *, jcharArray, jsize, jsize, jchar *);
    void (*GetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize, jshort *);
    void (*GetIntArrayRegion)(JNIEnv *, jintArray, jsize, jsize, jint *);
    void (*GetLongArrayRegion)(JNIEnv *, jlongArray, jsize, jsize, jlong *);
    void (*GetFloatArrayRegion)(JNIEnv *, jfloatArray, jsize, jsize, jfloat *);
    void (*GetDoubleArrayRegion)(JNIEnv *, jdoubleArray, jsize, jsize, jdouble *);

    void (*SetBooleanArrayRegion)(JNIEnv *, jbooleanArray, jsize, jsize, const jboolean *);
    void (*SetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize, const jbyte *);
    void (*SetCharArrayRegion)(JNIEnv *, jcharArray, jsize, jsize, const jchar *);
    void (*SetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize, const jshort *);
    void (*SetIntArrayRegion)(JNIEnv *, jintArray, jsize, jsize, const jint *);
    void (*SetLongArrayRegion)(JNIEnv *, jlongArray, jsize, jsize, const jlong *);
    void (*SetFloatArrayRegion)(JNIEnv *, jfloatArray, jsize, jsize, const jfloat *);
    void (*SetDoubleArrayRegion)(JNIEnv *, jdoubleArray, jsize, jsize, const jdouble *);

    jint (*RegisterNatives)(JNIEnv *, jclass, const struct JNINativeMethod *methods, jint nMethods);
    jint (*UnregisterNatives)(JNIEnv *, jclass);
    jint (*MonitorEnter)(JNIEnv *, jobject);
    jint (*MonitorExit)(JNIEnv *, jobject);
    jint (*GetJavaVM)(JNIEnv *, struct JavaVM **);

    void (*GetStringRegion)(JNIEnv *, jstring, jsize, jsize, jchar *);
    void (*GetStringUTFRegion)(JNIEnv *, jstring, jsize, jsize, char *);
    void *(*GetPrimitiveArrayCritical)(JNIEnv *, jarray, jboolean *);
    void (*ReleasePrimitiveArrayCritical)(JNIEnv *, jarray, void *, jint);
    const jchar *(*GetStringCritical)(JNIEnv *, jstring, jboolean *);
    void (*ReleaseStringCritical)(JNIEnv *, jstring, const jchar *);
    jweak (*NewWeakGlobalRef)(JNIEnv *, jobject);
    void (*DeleteWeakGlobalRef)(JNIEnv *, jweak);
    jboolean (*ExceptionCheck)(JNIEnv *);

    jobject (*NewDirectByteBuffer)(JNIEnv *, void *address, jlong capacity);
    void *(*GetDirectBufferAddress)(JNIEnv *, jobject);
    jlong (*GetDirectBufferCapacity)(JNIEnv *, jobject);

    jint (*GetObjectRefType)(JNIEnv *, jobject);
};

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

static inline int64_t sys_nanosleep(uint64_t nsec)
{
    struct {
        int64_t tv_sec;
        int64_t tv_nsec;
    } req = {0, (int64_t)nsec};
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(35), "D"(&req), "S"(NULL)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static inline void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
                             uint64_t offset)
{
    int64_t ret;
    register int r10 __asm__("r10") = flags;
    register int r8 __asm__("r8") = fd;
    register uint64_t r9 __asm__("r9") = offset;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(9), "D"(addr), "S"(length), "d"(prot), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return (void *)ret;
}

static inline int64_t sys_munmap(void *addr, size_t length)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(11), "D"(addr), "S"(length)
                     : "rcx", "r11", "memory");
    return ret;
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

static inline int my_strcmp(const char *s1, const char *s2)
{
    if (!s1 || !s2)
        return s1 == s2 ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline const char *my_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return 0;
    if (!*needle)
        return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n)
            return haystack;
    }
    return 0;
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
extern float AMotionEvent_getAxisValue(const struct AInputEvent *motion_event, int32_t axis,
                                       size_t pointer_index) __attribute__((sysv_abi));
extern size_t AMotionEvent_getPointerCount(const struct AInputEvent *motion_event)
    __attribute__((sysv_abi));
extern int32_t AKeyEvent_getKeyCode(const struct AInputEvent *key_event) __attribute__((sysv_abi));
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...)
    __attribute__((sysv_abi));
extern struct AConfiguration *AConfiguration_new(void) __attribute__((sysv_abi));
extern int32_t AConfiguration_getOrientation(struct AConfiguration *config)
    __attribute__((sysv_abi));
extern void AConfiguration_delete(struct AConfiguration *config) __attribute__((sysv_abi));

struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
};

struct AndroidBitmapInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t format;
    uint32_t flags;
};

extern int32_t ANativeWindow_lock(struct ANativeWindow *window,
                                  struct ANativeWindow_Buffer *outBuffer,
                                  struct ARect *inOutDirtyBounds) __attribute__((sysv_abi));
extern int32_t ANativeWindow_unlockAndPost(struct ANativeWindow *window) __attribute__((sysv_abi));

extern int AndroidBitmap_getInfo(JNIEnv *env, jobject jbitmap, struct AndroidBitmapInfo *info)
    __attribute__((sysv_abi));
extern int AndroidBitmap_lockPixels(JNIEnv *env, jobject jbitmap, void **addrPtr)
    __attribute__((sysv_abi));
extern int AndroidBitmap_unlockPixels(JNIEnv *env, jobject jbitmap) __attribute__((sysv_abi));

extern int32_t ASharedMemory_create(const char *name, size_t size) __attribute__((sysv_abi));
extern size_t ASharedMemory_getSize(int fd) __attribute__((sysv_abi));
extern int32_t ASharedMemory_setProt(int fd, int prot) __attribute__((sysv_abi));
extern int32_t ASharedMemory_dup(int fd) __attribute__((sysv_abi));

struct AHardwareBuffer;
typedef struct AHardwareBuffer AHardwareBuffer;

typedef struct AHardwareBuffer_Desc {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t format;
    uint64_t usage;
    uint32_t stride;
    uint32_t rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

typedef struct AHardwareBuffer_Plane {
    void *data;
    uint32_t pixelStride;
    uint32_t rowStride;
} AHardwareBuffer_Plane;

typedef struct AHardwareBuffer_Planes {
    uint32_t planeCount;
    AHardwareBuffer_Plane planes[4];
} AHardwareBuffer_Planes;

extern int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc, AHardwareBuffer **outBuffer)
    __attribute__((sysv_abi));
extern void AHardwareBuffer_acquire(AHardwareBuffer *buffer) __attribute__((sysv_abi));
extern void AHardwareBuffer_release(AHardwareBuffer *buffer) __attribute__((sysv_abi));
extern void AHardwareBuffer_describe(const AHardwareBuffer *buffer, AHardwareBuffer_Desc *outDesc)
    __attribute__((sysv_abi));
extern int AHardwareBuffer_lock(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                                const struct ARect *rect, void **outVirtualAddress)
    __attribute__((sysv_abi));
extern int AHardwareBuffer_unlock(AHardwareBuffer *buffer, int32_t *fence)
    __attribute__((sysv_abi));
extern int AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                                      const struct ARect *rect, AHardwareBuffer_Planes *outPlanes)
    __attribute__((sysv_abi));
extern int AHardwareBuffer_getId(const AHardwareBuffer *buffer, uint64_t *outId)
    __attribute__((sysv_abi));

struct AMediaFormat;
typedef struct AMediaFormat AMediaFormat;
struct AMediaCodec;
typedef struct AMediaCodec AMediaCodec;

typedef struct AMediaCodecBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;

extern AMediaFormat *AMediaFormat_new(void) __attribute__((sysv_abi));
extern int32_t AMediaFormat_delete(AMediaFormat *format) __attribute__((sysv_abi));
extern void AMediaFormat_setString(AMediaFormat *format, const char *name, const char *value)
    __attribute__((sysv_abi));
extern void AMediaFormat_setInt32(AMediaFormat *format, const char *name, int32_t value)
    __attribute__((sysv_abi));

extern AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type)
    __attribute__((sysv_abi));
extern int32_t AMediaCodec_delete(AMediaCodec *codec) __attribute__((sysv_abi));
extern int32_t AMediaCodec_configure(AMediaCodec *codec, const AMediaFormat *format,
                                     struct ANativeWindow *surface, void *crypto, uint32_t flags)
    __attribute__((sysv_abi));
extern int32_t AMediaCodec_start(AMediaCodec *codec) __attribute__((sysv_abi));
extern int32_t AMediaCodec_stop(AMediaCodec *codec) __attribute__((sysv_abi));
extern int64_t AMediaCodec_dequeueInputBuffer(AMediaCodec *codec, int64_t timeoutUs)
    __attribute__((sysv_abi));
extern uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size)
    __attribute__((sysv_abi));
extern int32_t AMediaCodec_queueInputBuffer(AMediaCodec *codec, size_t idx, int64_t offset,
                                            size_t size, uint64_t timeUs, uint32_t flags)
    __attribute__((sysv_abi));
extern int64_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info,
                                               int64_t timeoutUs) __attribute__((sysv_abi));
extern uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size)
    __attribute__((sysv_abi));
extern int32_t AMediaCodec_releaseOutputBuffer(AMediaCodec *codec, size_t idx, _Bool render)
    __attribute__((sysv_abi));

struct AIBinder;
typedef struct AIBinder AIBinder;
struct AIBinder_Class;
typedef struct AIBinder_Class AIBinder_Class;
struct AParcel;
typedef struct AParcel AParcel;

extern AIBinder_Class *AIBinder_Class_define(const char *interfaceDescriptor, void *onCreate,
                                             void *onDestroy, void *onTransact)
    __attribute__((sysv_abi));
extern AIBinder *AIBinder_new(const AIBinder_Class *clazz, void *args) __attribute__((sysv_abi));
extern void AIBinder_decStrong(AIBinder *binder) __attribute__((sysv_abi));
extern int32_t AIBinder_transact(AIBinder *binder, uint32_t code, const AParcel *in, AParcel *out,
                                 uint32_t flags) __attribute__((sysv_abi));
extern AParcel *AParcel_create(void) __attribute__((sysv_abi));
extern void AParcel_delete(AParcel *parcel) __attribute__((sysv_abi));
extern int32_t AParcel_writeInt32(AParcel *parcel, int32_t value) __attribute__((sysv_abi));
extern int32_t AParcel_writeInt64(AParcel *parcel, int64_t value) __attribute__((sysv_abi));
extern int32_t AParcel_writeString(AParcel *parcel, const char *string, int32_t length)
    __attribute__((sysv_abi));
extern int32_t AParcel_writeStrongBinder(AParcel *parcel, AIBinder *binder)
    __attribute__((sysv_abi));
extern int32_t AParcel_readInt32(const AParcel *parcel, int32_t *out) __attribute__((sysv_abi));
extern int32_t AServiceManager_addService(AIBinder *binder, const char *instance)
    __attribute__((sysv_abi));
extern AIBinder *AServiceManager_checkService(const char *instance) __attribute__((sysv_abi));
extern AIBinder *AServiceManager_getService(const char *instance) __attribute__((sysv_abi));

typedef int64_t net_handle_t;
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    uint32_t ai_addrlen;
    uint32_t __pad;
    void *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

extern int android_setsocknetwork(net_handle_t network, int fd) __attribute__((sysv_abi));
extern int android_setprocnetwork(net_handle_t network) __attribute__((sysv_abi));
extern int android_getprocnetwork(net_handle_t *network) __attribute__((sysv_abi));
extern int android_getaddrinfofornetwork(net_handle_t network, const char *node,
                                         const char *service, const struct addrinfo *hints,
                                         struct addrinfo **res) __attribute__((sysv_abi));
extern void android_freeaddrinfofornetwork(struct addrinfo *ai) __attribute__((sysv_abi));
extern int android_res_nquery(net_handle_t network, const char *dname, int ns_class, int ns_type,
                              uint32_t flags) __attribute__((sysv_abi));
extern int android_res_nresult(int fd, int *rcode) __attribute__((sysv_abi));
extern void android_res_cancel(int nsend_fd) __attribute__((sysv_abi));

enum AThermalStatus;
struct AThermalManager;
typedef void (*AThermal_StatusCallback)(void *data, int status);

extern struct AThermalManager *AThermalManager_acquireManager(void) __attribute__((sysv_abi));
extern void AThermalManager_releaseManager(struct AThermalManager *manager)
    __attribute__((sysv_abi));
extern int AThermalManager_getCurrentThermalStatus(struct AThermalManager *manager)
    __attribute__((sysv_abi));
extern float AThermalManager_getThermalHeadroom(struct AThermalManager *manager,
                                                int forecastSeconds) __attribute__((sysv_abi));

struct APerformanceHintManager;
struct APerformanceHintSession;

extern struct APerformanceHintManager *APerformanceHintManager_create(void)
    __attribute__((sysv_abi));
extern struct APerformanceHintSession *APerformanceHintManager_createSession(
    struct APerformanceHintManager *manager, const int32_t *threadIds, size_t size,
    int64_t initialTargetWorkDurationNanos) __attribute__((sysv_abi));
extern int APerformanceHintSession_reportActualWorkDuration(struct APerformanceHintSession *session,
                                                            int64_t actualDurationNanos)
    __attribute__((sysv_abi));
extern void APerformanceHintSession_close(struct APerformanceHintSession *session)
    __attribute__((sysv_abi));

static volatile int g_engine_licensing_ok = 0;

static int32_t __attribute__((sysv_abi))
engine_license_listener_transact(AIBinder *binder, uint32_t code, const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)out;
    if (code == 1 && in) {
        int32_t resp_code = -1;
        if (AParcel_readInt32(in, &resp_code) == 0 && resp_code == 0) {
            g_engine_licensing_ok = 1;
            return 0;
        }
    }
    return -1;
}

static int32_t __attribute__((sysv_abi)) engine_binder_transact(AIBinder *binder, uint32_t code,
                                                                const AParcel *in, AParcel *out)
{
    (void)binder;
    if (code == 100) {
        int32_t val = 0;
        if (AParcel_readInt32(in, &val) == 0 && val == (int32_t)0xCAFEBABE) {
            AParcel_writeInt32(out, (int32_t)0xDEADBEEF);
            return 0;
        }
    }
    return -1;
}

struct AAsset;
struct AAssetManager;
struct AAssetDir;

extern struct AAssetManager *AAssetManager_fromJava(void *env, void *assetManager)
    __attribute__((sysv_abi));
extern struct AAsset *AAssetManager_open(struct AAssetManager *mgr, const char *filename, int mode)
    __attribute__((sysv_abi));
extern struct AAssetDir *AAssetManager_openDir(struct AAssetManager *mgr, const char *dirName)
    __attribute__((sysv_abi));
extern int AAsset_read(struct AAsset *asset, void *buf, size_t count) __attribute__((sysv_abi));
extern int64_t AAsset_getLength(struct AAsset *asset) __attribute__((sysv_abi));
extern void AAsset_close(struct AAsset *asset) __attribute__((sysv_abi));
extern const char *AAssetDir_getNextFileName(struct AAssetDir *assetDir) __attribute__((sysv_abi));
extern void AAssetDir_close(struct AAssetDir *assetDir) __attribute__((sysv_abi));

struct AChoreographer;
extern struct AChoreographer *AChoreographer_getInstance(void) __attribute__((sysv_abi));
extern void AChoreographer_postFrameCallback64(struct AChoreographer *choreographer,
                                               void (*callback)(int64_t frameTimeNanos, void *data),
                                               void *data) __attribute__((sysv_abi));
extern void AChoreographer_registerRefreshRateCallback(struct AChoreographer *choreographer,
                                                       void (*callback)(int64_t vsyncPeriodNanos,
                                                                        void *data),
                                                       void *data) __attribute__((sysv_abi));
extern void AChoreographer_unregisterRefreshRateCallback(struct AChoreographer *choreographer,
                                                         void (*callback)(int64_t vsyncPeriodNanos,
                                                                          void *data),
                                                         void *data) __attribute__((sysv_abi));

struct ASensorManager;
struct ASensorEventQueue;
struct ASensor;
struct ASensorVector {
    union {
        float v[3];
        struct {
            float x;
            float y;
            float z;
        };
    };
    int8_t status;
    uint8_t reserved[3];
};

struct ASensorEvent {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union {
        float data[16];
        struct ASensorVector vector;
        struct ASensorVector acceleration;
    };
    uint32_t flags;
    int32_t reserved1[3];
};

extern struct ASensorManager *ASensorManager_getInstanceForPackage(const char *packageName)
    __attribute__((sysv_abi));
extern const struct ASensor *ASensorManager_getDefaultSensor(struct ASensorManager *manager,
                                                             int type) __attribute__((sysv_abi));
extern struct ASensorEventQueue *ASensorManager_createEventQueue(struct ASensorManager *manager,
                                                                 struct ALooper *looper, int ident,
                                                                 void *callback, void *data)
    __attribute__((sysv_abi));
extern int ASensorManager_destroyEventQueue(struct ASensorManager *manager,
                                            struct ASensorEventQueue *queue)
    __attribute__((sysv_abi));
extern int ASensorEventQueue_registerSensor(struct ASensorEventQueue *queue,
                                            const struct ASensor *sensor, int32_t samplingPeriodUs,
                                            int64_t maxBatchReportLatencyUs)
    __attribute__((sysv_abi));
extern int ASensorEventQueue_disableSensor(struct ASensorEventQueue *queue,
                                           const struct ASensor *sensor) __attribute__((sysv_abi));
extern int64_t ASensorEventQueue_getEvents(struct ASensorEventQueue *queue,
                                           struct ASensorEvent *events, size_t count)
    __attribute__((sysv_abi));

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
extern uint32_t glCreateProgram(void) __attribute__((sysv_abi));
extern void glAttachShader(uint32_t program, uint32_t shader) __attribute__((sysv_abi));
extern void glLinkProgram(uint32_t program) __attribute__((sysv_abi));
extern void glUseProgram(uint32_t program) __attribute__((sysv_abi));
extern void glDeleteShader(uint32_t shader) __attribute__((sysv_abi));
extern void glDeleteProgram(uint32_t program) __attribute__((sysv_abi));
extern int32_t glGetAttribLocation(uint32_t program, const char *name) __attribute__((sysv_abi));
extern int32_t glGetUniformLocation(uint32_t program, const char *name) __attribute__((sysv_abi));
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
extern void glUniform1f(int32_t location, float v0) __attribute__((sysv_abi));
extern void glUniform4f(int32_t location, float v0, float v1, float v2, float v3)
    __attribute__((sysv_abi));

// AAudio functions
extern int32_t AAudio_createStreamBuilder(void **builder) __attribute__((sysv_abi));
extern void AAudioStreamBuilder_setSampleRate(void *builder, int32_t sampleRate)
    __attribute__((sysv_abi));
extern void AAudioStreamBuilder_setChannelCount(void *builder, int32_t channelCount)
    __attribute__((sysv_abi));
extern void AAudioStreamBuilder_setFormat(void *builder, int32_t format) __attribute__((sysv_abi));
extern void AAudioStreamBuilder_setDataCallback(void *builder, void *callback, void *userData)
    __attribute__((sysv_abi));
extern int32_t AAudioStreamBuilder_openStream(void *builder, void **stream)
    __attribute__((sysv_abi));
extern int32_t AAudioStreamBuilder_delete(void *builder) __attribute__((sysv_abi));
extern int32_t AAudioStream_requestStart(void *stream) __attribute__((sysv_abi));
extern int32_t AAudioStream_requestStop(void *stream) __attribute__((sysv_abi));
extern int32_t AAudioStream_close(void *stream) __attribute__((sysv_abi));
extern void AAudioStreamBuilder_setDirection(void *builder, int32_t direction)
    __attribute__((sysv_abi));
extern int32_t AAudioStream_read(void *stream, void *buffer, int32_t numFrames,
                                 int64_t timeoutNanoseconds) __attribute__((sysv_abi));

// Swappy & Native Window frame pacing functions
extern uint64_t SwappyVk_initAndGetRefreshCycleDuration(void *physicalDevice, void *device,
                                                        const void *pAllocator)
    __attribute__((sysv_abi));
extern void SwappyGL_init(void *env, void *activity) __attribute__((sysv_abi));
extern void SwappyGL_setWindow(void *window) __attribute__((sysv_abi));
extern bool SwappyGL_swap(void *display, void *surface) __attribute__((sysv_abi));
extern void SwappyGL_destroy(void) __attribute__((sysv_abi));
extern int32_t ANativeWindow_getLastQueueDuration(void *window, int64_t *outDuration)
    __attribute__((sysv_abi));

// Camera2 & ImageReader NDK functions
typedef struct ACameraIdList_guest {
    int numCameras;
    const char **cameraIds;
} ACameraIdList_guest;

extern void *ACameraManager_create(void) __attribute__((sysv_abi));
extern void ACameraManager_delete(void *manager) __attribute__((sysv_abi));
extern int32_t ACameraManager_getCameraIdList(void *manager, ACameraIdList_guest **cameraIdList)
    __attribute__((sysv_abi));
extern void ACameraManager_deleteCameraIdList(ACameraIdList_guest *cameraIdList)
    __attribute__((sysv_abi));
extern int32_t ACameraManager_openCamera(void *manager, const char *cameraId, void *callback,
                                         void **device) __attribute__((sysv_abi));
extern int32_t ACameraDevice_close(void *device) __attribute__((sysv_abi));
extern int32_t ACameraDevice_createCaptureRequest(const void *device, int templateId,
                                                  void **request) __attribute__((sysv_abi));
extern int32_t ACameraDevice_createCaptureSession(void *device, const void *outputs,
                                                  const void *callbacks, void **session)
    __attribute__((sysv_abi));
extern void ACaptureRequest_free(void *request) __attribute__((sysv_abi));
extern int32_t ACaptureRequest_addTarget(void *request, const void *target)
    __attribute__((sysv_abi));
extern int32_t ACaptureRequest_removeTarget(void *request, const void *target)
    __attribute__((sysv_abi));
extern int32_t ACameraCaptureSession_close(void *session) __attribute__((sysv_abi));
extern int32_t ACameraCaptureSession_capture(void *session, void *callbacks, int numRequests,
                                             void **requests, int *captureSequenceId)
    __attribute__((sysv_abi));
extern int32_t ACameraOutputTarget_create(void *window, void **output) __attribute__((sysv_abi));
extern void ACameraOutputTarget_free(void *output) __attribute__((sysv_abi));
extern int32_t ACaptureSessionOutput_create(void *window, void **output) __attribute__((sysv_abi));
extern void ACaptureSessionOutput_free(void *output) __attribute__((sysv_abi));
extern int32_t ACaptureSessionOutputContainer_create(void **container) __attribute__((sysv_abi));
extern void ACaptureSessionOutputContainer_free(void *container) __attribute__((sysv_abi));
extern int32_t ACaptureSessionOutputContainer_add(void *container, const void *output)
    __attribute__((sysv_abi));
extern int32_t ACaptureSessionOutputContainer_remove(void *container, const void *output)
    __attribute__((sysv_abi));
extern int32_t AImageReader_new(int32_t width, int32_t height, int32_t format, int32_t maxImages,
                                void **reader) __attribute__((sysv_abi));
extern void AImageReader_delete(void *reader) __attribute__((sysv_abi));
extern int32_t AImageReader_getWindow(void *reader, void **window) __attribute__((sysv_abi));
extern int32_t AImageReader_acquireNextImage(void *reader, void **image) __attribute__((sysv_abi));
extern void AImage_delete(void *image) __attribute__((sysv_abi));
extern int32_t AImage_getWidth(const void *image, int32_t *width) __attribute__((sysv_abi));
extern int32_t AImage_getHeight(const void *image, int32_t *height) __attribute__((sysv_abi));
extern int32_t AImage_getNumberOfPlanes(const void *image, int32_t *numPlanes)
    __attribute__((sysv_abi));
extern int32_t AImage_getPlaneData(const void *image, int32_t planeIdx, uint8_t **data,
                                   int *dataLength) __attribute__((sysv_abi));

// KeyStore NDK functions
extern void *AKeyStore_create(void) __attribute__((sysv_abi));
extern void AKeyStore_delete(void *store) __attribute__((sysv_abi));
extern int32_t AKeyStore_generateAesKey(void *store, const char *alias, int32_t keyBits,
                                        int32_t blockMode, int32_t padding)
    __attribute__((sysv_abi));
extern int32_t AKeyStore_encrypt(void *store, const char *alias, const uint8_t *plaintext,
                                 size_t plainLen, uint8_t **outCiphertext, size_t *outCipherLen,
                                 uint8_t **outIv, size_t *outIvLen, uint8_t **outTag,
                                 size_t *outTagLen) __attribute__((sysv_abi));
extern int32_t AKeyStore_decrypt(void *store, const char *alias, const uint8_t *ciphertext,
                                 size_t cipherLen, const uint8_t *iv, size_t ivLen,
                                 const uint8_t *tag, size_t tagLen, uint8_t **outPlaintext,
                                 size_t *outPlainLen) __attribute__((sysv_abi));
extern void AKeyStore_freeBuffer(void *buf) __attribute__((sysv_abi));

// Location NDK functions
extern void *ALocationManager_create(void) __attribute__((sysv_abi));
extern void ALocationManager_destroy(void *mgr) __attribute__((sysv_abi));
extern int ALocationManager_isProviderEnabled(void *mgr, const char *provider)
    __attribute__((sysv_abi));
extern void *ALocationManager_getLastKnownLocation(void *mgr, const char *provider)
    __attribute__((sysv_abi));
extern double ALocation_getLatitude(const void *loc) __attribute__((sysv_abi));
extern double ALocation_getLongitude(const void *loc) __attribute__((sysv_abi));
extern void ALocation_destroy(void *loc) __attribute__((sysv_abi));

// Clipboard NDK functions
extern void *AClipboard_create(void) __attribute__((sysv_abi));
extern void AClipboard_destroy(void *clip) __attribute__((sysv_abi));
extern int AClipboard_setText(void *clip, const char *text) __attribute__((sysv_abi));
extern int AClipboard_getText(void *clip, char *buffer, size_t max_len) __attribute__((sysv_abi));
extern int AClipboard_hasText(void *clip) __attribute__((sysv_abi));

// Power & Battery NDK functions
extern void *APowerManager_create(void) __attribute__((sysv_abi));
extern void APowerManager_destroy(void *mgr) __attribute__((sysv_abi));
extern int32_t APowerManager_isInteractive(void *mgr) __attribute__((sysv_abi));
extern void *AWakeLock_create(int32_t level_and_flags, const char *tag) __attribute__((sysv_abi));
extern int32_t AWakeLock_acquire(void *lock, int64_t timeout_ms) __attribute__((sysv_abi));
extern int32_t AWakeLock_release(void *lock) __attribute__((sysv_abi));
extern int32_t AWakeLock_isHeld(void *lock) __attribute__((sysv_abi));
extern void AWakeLock_destroy(void *lock) __attribute__((sysv_abi));
extern void *ABatteryManager_create(void) __attribute__((sysv_abi));
extern void ABatteryManager_destroy(void *mgr) __attribute__((sysv_abi));
extern int32_t ABatteryManager_getCapacity(void *mgr) __attribute__((sysv_abi));

// Notification & Toast NDK functions
extern void *ANotificationManager_create(void) __attribute__((sysv_abi));
extern void ANotificationManager_destroy(void *mgr) __attribute__((sysv_abi));
extern void *ANotificationChannel_create(const char *id, const char *name, int32_t importance)
    __attribute__((sysv_abi));
extern void ANotificationChannel_destroy(void *channel) __attribute__((sysv_abi));
extern int32_t ANotificationManager_createNotificationChannel(void *mgr, void *channel)
    __attribute__((sysv_abi));
extern void *ANotification_create(const char *channel_id) __attribute__((sysv_abi));
extern void ANotification_destroy(void *notification) __attribute__((sysv_abi));
extern void ANotification_setTitle(void *notification, const char *title) __attribute__((sysv_abi));
extern void ANotification_setText(void *notification, const char *text) __attribute__((sysv_abi));
extern int32_t ANotificationManager_notify(void *mgr, const char *tag, int32_t id,
                                           void *notification) __attribute__((sysv_abi));
extern int32_t ANotificationManager_cancel(void *mgr, const char *tag, int32_t id)
    __attribute__((sysv_abi));
extern void AToast_show(const char *text, int32_t duration) __attribute__((sysv_abi));

// Math functions (from DT_NEEDED libgame_math.so)
extern int math_add(int a, int b) __attribute__((sysv_abi));
extern int math_get_init_status(void) __attribute__((sysv_abi));

// libdl functions
#define RTLD_NOW 2
#define RTLD_DEFAULT ((void *)0)

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

extern void *dlopen(const char *filename, int flags) __attribute__((sysv_abi));
extern void *dlsym(void *handle, const char *symbol) __attribute__((sysv_abi));
extern int dlclose(void *handle) __attribute__((sysv_abi));
extern char *dlerror(void) __attribute__((sysv_abi));
extern int dladdr(const void *addr, Dl_info *info) __attribute__((sysv_abi));

// StorageManager functions
struct AStorageManager;
typedef void (*AStorageManager_obbCallbackFunc)(const char *filename, const int32_t state,
                                                void *data);

extern struct AStorageManager *AStorageManager_new(void) __attribute__((sysv_abi));
extern void AStorageManager_delete(struct AStorageManager *mgr) __attribute__((sysv_abi));
extern void AStorageManager_mountObb(struct AStorageManager *mgr, const char *filename,
                                     const char *key, AStorageManager_obbCallbackFunc cb,
                                     void *data) __attribute__((sysv_abi));
extern void AStorageManager_unmountObb(struct AStorageManager *mgr, const char *filename,
                                       const int force, AStorageManager_obbCallbackFunc cb,
                                       void *data) __attribute__((sysv_abi));
extern int AStorageManager_isObbMounted(struct AStorageManager *mgr, const char *filename)
    __attribute__((sysv_abi));
extern const char *AStorageManager_getMountedObbPath(struct AStorageManager *mgr,
                                                     const char *filename)
    __attribute__((sysv_abi));

static volatile int g_engine_obb_state = 0;
static void __attribute__((sysv_abi)) engine_obb_callback(const char *filename, const int32_t state,
                                                          void *data)
{
    (void)filename;
    (void)data;
    g_engine_obb_state = state;
}

static volatile uint32_t g_audio_phase1 = 0;
static volatile uint32_t g_audio_phase2 = 0;
static volatile uint32_t g_audio_chime_phase = 0;
static volatile int32_t g_audio_chime_envelope = 0;

static inline int16_t synth_wave(uint32_t phase)
{
    uint16_t p = (uint16_t)(phase >> 16);
    if (p < 32768) {
        return (int16_t)(p - 16384);
    } else {
        return (int16_t)(49152 - p);
    }
}

static int32_t __attribute__((sysv_abi)) engine_audio_callback(void *stream, void *userData,
                                                               void *audioData, int32_t numFrames)
{
    (void)stream;
    (void)userData;
    int16_t *out = (int16_t *)audioData;
    const uint32_t step1 = (220ULL * 4294967296ULL) / 48000;
    const uint32_t step2 = (330ULL * 4294967296ULL) / 48000;
    const uint32_t chime_step = (880ULL * 4294967296ULL) / 48000;

    for (int32_t i = 0; i < numFrames; ++i) {
        g_audio_phase1 += step1;
        g_audio_phase2 += step2;
        int16_t amb = (synth_wave(g_audio_phase1) / 12) + (synth_wave(g_audio_phase2) / 12);

        int16_t chime = 0;
        int32_t env = g_audio_chime_envelope;
        if (env > 0) {
            g_audio_chime_phase += chime_step;
            chime = (int16_t)((synth_wave(g_audio_chime_phase) * env) / 8000);
            g_audio_chime_envelope = env - 1;
        }

        int16_t sample = amb + chime;
        out[2 * i] = sample;
        out[2 * i + 1] = sample;
    }
    return 0; // AAUDIO_CALLBACK_RESULT_CONTINUE
}

static const char *kEngineVertexShader = "attribute vec2 a_pos;\n"
                                         "uniform vec4 u_offset;\n"
                                         "uniform float u_angle;\n"
                                         "void main() {\n"
                                         "    float c = cos(u_angle);\n"
                                         "    float s = sin(u_angle);\n"
                                         "    mat2 rot = mat2(c, -s, s, c);\n"
                                         "    vec2 p = rot * a_pos + u_offset.xy;\n"
                                         "    gl_Position = vec4(p, 0.0, 1.0);\n"
                                         "}\n";

static const char *kEngineFragmentShader = "uniform vec4 u_color;\n"
                                           "void main() {\n"
                                           "    gl_FragColor = u_color;\n"
                                           "}\n";

static const float kQuadVertices[] = {-0.4f, -0.4f, 0.4f, -0.4f, -0.4f, 0.4f,
                                      -0.4f, 0.4f,  0.4f, -0.4f, 0.4f,  0.4f};

static const float kColors[4][3] = {
    {0.2f, 0.8f, 0.4f},  // Emerald Green
    {0.9f, 0.3f, 0.2f},  // Crimson Red
    {0.2f, 0.6f, 0.9f},  // Sky Blue
    {0.95f, 0.75f, 0.1f} // Amber Yellow
};

struct EngineState {
    struct ANativeActivity *activity;
    struct ALooper *looper;
    struct AInputQueue *inputQueue;
    struct ANativeWindow *window;
    int msgpipe[2];
    int child_tid;
    volatile int running;
    char internalDataPath[128];
};

static struct EngineState g_engine;
static uint8_t g_thread_stack[65536] __attribute__((aligned(16)));

static int init_gles(void **out_dpy, void **out_surface, void **out_ctx, uint32_t *out_prog,
                     uint32_t *out_vbo, int32_t *out_pos, int32_t *out_offset, int32_t *out_angle,
                     int32_t *out_color, struct ANativeWindow *win)
{
    void *dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!dpy)
        return 0;
    int32_t major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor))
        return 0;

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
    if (!eglChooseConfig(dpy, attribs, &config, 1, &num_configs) || num_configs < 1)
        return 0;

    void *surface = eglCreateWindowSurface(dpy, config, win, (const int32_t *)0);
    if (!surface)
        return 0;

    const int32_t ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    void *context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (!context)
        return 0;

    if (!eglMakeCurrent(dpy, surface, surface, context))
        return 0;

    uint32_t vs = glCreateShader(GL_VERTEX_SHADER);
    const char *vs_src = kEngineVertexShader;
    glShaderSource(vs, 1, &vs_src, (const int32_t *)0);
    glCompileShader(vs);

    uint32_t fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char *fs_src = kEngineFragmentShader;
    glShaderSource(fs, 1, &fs_src, (const int32_t *)0);
    glCompileShader(fs);

    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    uint32_t vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);

    *out_dpy = dpy;
    *out_surface = surface;
    *out_ctx = context;
    *out_prog = prog;
    *out_vbo = vbo;
    *out_pos = glGetAttribLocation(prog, "a_pos");
    *out_offset = glGetUniformLocation(prog, "u_offset");
    *out_angle = glGetUniformLocation(prog, "u_angle");
    *out_color = glGetUniformLocation(prog, "u_color");
    return 1;
}

static void cleanup_gles(void *dpy, void *surface, void *ctx, uint32_t prog, uint32_t vbo)
{
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
}

static void dummy_guest_native(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
}

struct ChoreographerState {
    struct AChoreographer *choreographer;
    int64_t last_frame_ns;
    int64_t vsync_period_ns;
    int frame_count;
    int refresh_ok;
    int marker_written;
    float angle;
};

static void __attribute__((sysv_abi)) engine_on_refresh_rate(int64_t vsyncPeriodNanos, void *data)
{
    struct ChoreographerState *st = (struct ChoreographerState *)data;
    if (!st)
        return;
    if (vsyncPeriodNanos >= 1000000LL && vsyncPeriodNanos <= 100000000LL) {
        st->vsync_period_ns = vsyncPeriodNanos;
        st->refresh_ok = 1;
    }
}

static void __attribute__((sysv_abi)) engine_on_frame64(int64_t frameTimeNanos, void *data)
{
    struct ChoreographerState *st = (struct ChoreographerState *)data;
    if (!st)
        return;

    if (frameTimeNanos > st->last_frame_ns) {
        if (st->last_frame_ns > 0) {
            int64_t dt_ns = frameTimeNanos - st->last_frame_ns;
            if (dt_ns > 0 && dt_ns < 200000000LL) {
                st->angle += (float)dt_ns * 1.8e-9f;
            } else {
                st->angle += 0.03f;
            }
        } else {
            st->angle += 0.03f;
        }
        st->last_frame_ns = frameTimeNanos;
        st->frame_count++;
    }

    if (st->frame_count >= 2 && st->refresh_ok && !st->marker_written) {
        st->marker_written = 1;
        write_marker(g_engine.internalDataPath, "engine_choreographer_ready.txt",
                     "CHOREOGRAPHER_OK");
    }

    if (g_engine.running && st->choreographer) {
        AChoreographer_postFrameCallback64(st->choreographer, engine_on_frame64, st);
    }
}

static void __attribute__((noinline)) engine_worker_func(void)
{
    int bmp_ok = 0;
    struct ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    g_engine.looper = looper;

    ALooper_addFd(looper, g_engine.msgpipe[0], LOOPER_ID_CMD, ALOOPER_EVENT_INPUT, NULL, NULL);

    __android_log_print(ANDROID_LOG_INFO, "PortageEngine",
                        "Worker thread started and ALooper prepared");
    write_marker(g_engine.internalDataPath, "engine_worker_ready.txt", "READY");

    int m_res = math_add(20, 22);
    int m_init = math_get_init_status();
    if (m_res == 42 && m_init == 12345) {
        write_marker(g_engine.internalDataPath, "engine_math_ready.txt", "MATH_OK");
    }

    void *plugin_handle = dlopen("libgame_plugin.so", RTLD_NOW);
    if (plugin_handle) {
        int (*plugin_compute)(int) = (int (*)(int))dlsym(plugin_handle, "plugin_compute");
        int (*plugin_get_init_status)(void) =
            (int (*)(void))dlsym(plugin_handle, "plugin_get_init_status");
        if (plugin_compute && plugin_get_init_status) {
            int comp_val = plugin_compute(4);
            int pinit_val = plugin_get_init_status();
            if (comp_val == 45 && pinit_val == 777) {
                Dl_info dlinf;
                if (dladdr((void *)plugin_compute, &dlinf) && dlinf.dli_sname != NULL) {
                    write_marker(g_engine.internalDataPath, "engine_plugin_ready.txt", "PLUGIN_OK");
                }
            }
        }
        dlclose(plugin_handle);
    }

    int shm_fd = ASharedMemory_create("engine_shm", 4096);
    if (shm_fd >= 0) {
        size_t shm_sz = ASharedMemory_getSize(shm_fd);
        if (shm_sz == 4096) {
            void *shm_ptr =
                sys_mmap(NULL, 4096, 3 /* PROT_READ|PROT_WRITE */, 1 /* MAP_SHARED */, shm_fd, 0);
            if (shm_ptr && shm_ptr != (void *)-1) {
                char *cptr = (char *)shm_ptr;
                cptr[0] = 'A';
                cptr[1] = 'S';
                cptr[2] = 'H';
                cptr[3] = 'M';
                cptr[4] = 'E';
                cptr[5] = 'M';
                cptr[6] = '\0';

                int dup_fd = ASharedMemory_dup(shm_fd);
                if (dup_fd >= 0) {
                    void *dup_ptr =
                        sys_mmap(NULL, 4096, 1 /* PROT_READ */, 1 /* MAP_SHARED */, dup_fd, 0);
                    if (dup_ptr && dup_ptr != (void *)-1) {
                        char *dcptr = (char *)dup_ptr;
                        if (dcptr[0] == 'A' && dcptr[5] == 'M') {
                            ASharedMemory_setProt(dup_fd, 1 /* PROT_READ */);
                            write_marker(g_engine.internalDataPath, "engine_ashmem_ready.txt",
                                         "ASHMEM_OK");
                        }
                        sys_munmap(dup_ptr, 4096);
                    }
                    sys_close(dup_fd);
                }
                sys_munmap(shm_ptr, 4096);
            }
        }
        sys_close(shm_fd);
    }

    AHardwareBuffer_Desc hb_desc;
    hb_desc.width = 64;
    hb_desc.height = 64;
    hb_desc.layers = 1;
    hb_desc.format = 1;           // R8G8B8A8
    hb_desc.usage = 3 | (3 << 4); // CPU_READ_OFTEN | CPU_WRITE_OFTEN
    hb_desc.stride = 0;
    hb_desc.rfu0 = 0;
    hb_desc.rfu1 = 0;

    AHardwareBuffer *hb = NULL;
    if (AHardwareBuffer_allocate(&hb_desc, &hb) == 0 && hb) {
        AHardwareBuffer_Desc desc_out;
        AHardwareBuffer_describe(hb, &desc_out);
        void *vaddr = NULL;
        if (AHardwareBuffer_lock(hb, 3 << 4 /* CPU_WRITE_OFTEN */, -1, NULL, &vaddr) == 0 &&
            vaddr) {
            uint32_t *pix = (uint32_t *)vaddr;
            pix[0] = 0xAABBCCDD;
            AHardwareBuffer_unlock(hb, NULL);

            AHardwareBuffer_Planes hb_planes;
            if (AHardwareBuffer_lockPlanes(hb, 3 /* CPU_READ_OFTEN */, -1, NULL, &hb_planes) == 0) {
                if (hb_planes.planeCount >= 1 && hb_planes.planes[0].data != NULL) {
                    uint32_t *rpix = (uint32_t *)hb_planes.planes[0].data;
                    uint64_t hb_id = 0;
                    if (rpix[0] == 0xAABBCCDD && AHardwareBuffer_getId(hb, &hb_id) == 0 &&
                        hb_id > 0) {
                        write_marker(g_engine.internalDataPath, "engine_hardware_buffer_ready.txt",
                                     "HARDWARE_BUFFER_OK");
                    }
                }
                AHardwareBuffer_unlock(hb, NULL);
            }
        }
        AHardwareBuffer_release(hb);
    }

    AMediaCodec *media_codec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
    if (media_codec) {
        AMediaFormat *media_fmt = AMediaFormat_new();
        if (media_fmt) {
            AMediaFormat_setString(media_fmt, "mime", "audio/mp4a-latm");
            AMediaFormat_setInt32(media_fmt, "sample-rate", 44100);
            AMediaFormat_setInt32(media_fmt, "channel-count", 2);

            if (AMediaCodec_configure(media_codec, media_fmt, NULL, NULL, 0) == 0 &&
                AMediaCodec_start(media_codec) == 0) {
                int64_t in_buf_idx = AMediaCodec_dequeueInputBuffer(media_codec, 1000);
                if (in_buf_idx >= 0) {
                    size_t in_sz = 0;
                    uint8_t *in_buf =
                        AMediaCodec_getInputBuffer(media_codec, (size_t)in_buf_idx, &in_sz);
                    if (in_buf && in_sz >= 64) {
                        for (size_t mi = 0; mi < 64; ++mi) {
                            in_buf[mi] = (uint8_t)(mi + 1);
                        }
                        if (AMediaCodec_queueInputBuffer(media_codec, (size_t)in_buf_idx, 0, 64,
                                                         1000, 0) == 0) {
                            AMediaCodecBufferInfo out_inf;
                            int64_t out_buf_idx =
                                AMediaCodec_dequeueOutputBuffer(media_codec, &out_inf, 1000);
                            if (out_buf_idx >= 0) {
                                size_t out_sz = 0;
                                uint8_t *out_buf = AMediaCodec_getOutputBuffer(
                                    media_codec, (size_t)out_buf_idx, &out_sz);
                                if (out_buf && out_sz >= 64 && out_buf[0] == 1 &&
                                    out_buf[63] == 64) {
                                    write_marker(g_engine.internalDataPath,
                                                 "engine_media_ready.txt", "MEDIA_OK");
                                }
                                AMediaCodec_releaseOutputBuffer(media_codec, (size_t)out_buf_idx,
                                                                0);
                            }
                        }
                    }
                }
                AMediaCodec_stop(media_codec);
            }
            AMediaFormat_delete(media_fmt);
        }
        AMediaCodec_delete(media_codec);
    }

    AIBinder_Class *engine_binder_class = AIBinder_Class_define(
        "com.portage.IEngineService", NULL, NULL, (void *)engine_binder_transact);
    if (engine_binder_class) {
        AIBinder *engine_service = AIBinder_new(engine_binder_class, NULL);
        if (engine_service) {
            if (AServiceManager_addService(engine_service, "engine_service") == 0) {
                AIBinder *svc = AServiceManager_checkService("engine_service");
                if (svc) {
                    AParcel *bin_in = AParcel_create();
                    AParcel *bin_out = AParcel_create();
                    if (bin_in && bin_out) {
                        AParcel_writeInt32(bin_in, (int32_t)0xCAFEBABE);
                        if (AIBinder_transact(svc, 100, bin_in, bin_out, 0) == 0) {
                            int32_t reply_val = 0;
                            if (AParcel_readInt32(bin_out, &reply_val) == 0 &&
                                reply_val == (int32_t)0xDEADBEEF) {
                                write_marker(g_engine.internalDataPath, "engine_binder_ready.txt",
                                             "BINDER_OK");
                            }
                        }
                        AParcel_delete(bin_in);
                        AParcel_delete(bin_out);
                    }
                    AIBinder_decStrong(svc);
                }
            }
            AIBinder_decStrong(engine_service);
        }
    }

    AIBinder *vib_svc = AServiceManager_getService("vibrator");
    if (vib_svc) {
        AParcel *vib_in = AParcel_create();
        AParcel *vib_out = AParcel_create();
        if (vib_in && vib_out) {
            AParcel_writeInt32(vib_in, 100);
            if (AIBinder_transact(vib_svc, 1, vib_in, vib_out, 0) == 0) {
                int32_t ex = -1;
                if (AParcel_readInt32(vib_out, &ex) == 0 && ex == 0) {
                    write_marker(g_engine.internalDataPath, "engine_gamepad_ready.txt",
                                 "GAMEPAD_OK");
                }
            }
            AParcel_delete(vib_in);
            AParcel_delete(vib_out);
        }
        AIBinder_decStrong(vib_svc);
    }

    AIBinder *lic_svc =
        AServiceManager_getService("com.android.vending.licensing.ILicensingService");
    if (lic_svc) {
        AIBinder_Class *lic_class =
            AIBinder_Class_define("com.android.vending.licensing.ILicenseResultListener", NULL,
                                  NULL, (void *)engine_license_listener_transact);
        if (lic_class) {
            AIBinder *lic_listener = AIBinder_new(lic_class, NULL);
            if (lic_listener) {
                AParcel *lic_in = AParcel_create();
                AParcel *lic_out = AParcel_create();
                if (lic_in && lic_out) {
                    AParcel_writeInt64(lic_in, 123456789LL);
                    AParcel_writeString(lic_in, "com.portage.gameengine", -1);
                    AParcel_writeStrongBinder(lic_in, lic_listener);
                    if (AIBinder_transact(lic_svc, 1, lic_in, lic_out, 0) == 0) {
                        for (int i = 0; i < 200 && !g_engine_licensing_ok; ++i) {
                            sys_nanosleep(2000000);
                        }
                        if (g_engine_licensing_ok) {
                            write_marker(g_engine.internalDataPath, "engine_licensing_ready.txt",
                                         "LICENSING_OK");
                        }
                    }
                    AParcel_delete(lic_in);
                    AParcel_delete(lic_out);
                }
                AIBinder_decStrong(lic_listener);
            }
        }
        AIBinder_decStrong(lic_svc);
    }

    AIBinder *bill_svc =
        AServiceManager_getService("com.android.vending.billing.IInAppBillingService");
    if (bill_svc) {
        AParcel *bill_in = AParcel_create();
        AParcel *bill_out = AParcel_create();
        if (bill_in && bill_out) {
            AParcel_writeInt32(bill_in, 3);
            AParcel_writeString(bill_in, "com.portage.gameengine", -1);
            AParcel_writeString(bill_in, "inapp", -1);
            if (AIBinder_transact(bill_svc, 1 /* isBillingSupported */, bill_in, bill_out, 0) ==
                0) {
                int32_t ex = -1;
                int32_t res = -1;
                if (AParcel_readInt32(bill_out, &ex) == 0 && ex == 0 &&
                    AParcel_readInt32(bill_out, &res) == 0 && res == 0) {
                    write_marker(g_engine.internalDataPath, "engine_billing_ready.txt",
                                 "BILLING_OK");
                }
            }
            AParcel_delete(bill_in);
            AParcel_delete(bill_out);
        }
        AIBinder_decStrong(bill_svc);
    }

    if (g_engine.activity && g_engine.activity->env && g_engine.activity->clazz) {
        JNIEnv *env = (JNIEnv *)g_engine.activity->env;
        jobject activity_obj = g_engine.activity->clazz;

        jclass ver_cls = (*env)->FindClass(env, "android/os/Build$VERSION");
        jfieldID sdk_fid = ver_cls ? (*env)->GetStaticFieldID(env, ver_cls, "SDK_INT", "I") : NULL;
        jint sdk_val = sdk_fid ? (*env)->GetStaticIntField(env, ver_cls, sdk_fid) : 0;

        jclass build_cls = (*env)->FindClass(env, "android/os/Build");
        jfieldID model_fid =
            build_cls ? (*env)->GetStaticFieldID(env, build_cls, "MODEL", "Ljava/lang/String;")
                      : NULL;
        jstring model_str =
            model_fid ? (*env)->GetStaticObjectField(env, build_cls, model_fid) : NULL;
        const char *model_chars =
            model_str ? (*env)->GetStringUTFChars(env, model_str, NULL) : NULL;
        int model_ok = (model_chars && my_strlen(model_chars) > 0);
        if (model_chars) {
            (*env)->ReleaseStringUTFChars(env, model_str, model_chars);
        }

        jclass act_cls = (*env)->GetObjectClass(env, activity_obj);
        jmethodID get_res_mid = act_cls ? (*env)->GetMethodID(env, act_cls, "getResources",
                                                              "()Landroid/content/res/Resources;")
                                        : NULL;
        jobject res_obj =
            get_res_mid ? (*env)->CallObjectMethod(env, activity_obj, get_res_mid) : NULL;
        jclass res_cls = res_obj ? (*env)->GetObjectClass(env, res_obj) : NULL;
        jmethodID get_dm_mid = res_cls ? (*env)->GetMethodID(env, res_cls, "getDisplayMetrics",
                                                             "()Landroid/util/DisplayMetrics;")
                                       : NULL;
        jobject dm_obj = get_dm_mid ? (*env)->CallObjectMethod(env, res_obj, get_dm_mid) : NULL;
        jclass dm_cls = dm_obj ? (*env)->GetObjectClass(env, dm_obj) : NULL;
        jfieldID w_fid = dm_cls ? (*env)->GetFieldID(env, dm_cls, "widthPixels", "I") : NULL;
        jfieldID h_fid = dm_cls ? (*env)->GetFieldID(env, dm_cls, "heightPixels", "I") : NULL;
        jint w_val = w_fid ? (*env)->GetIntField(env, dm_obj, w_fid) : 0;
        jint h_val = h_fid ? (*env)->GetIntField(env, dm_obj, h_fid) : 0;

        char buf[32] = "DirectBufferTest";
        jobject byte_buf = (*env)->NewDirectByteBuffer(env, buf, 32);
        void *buf_addr = byte_buf ? (*env)->GetDirectBufferAddress(env, byte_buf) : NULL;
        jlong buf_cap = byte_buf ? (*env)->GetDirectBufferCapacity(env, byte_buf) : 0;
        int buf_ok = (buf_addr == buf && buf_cap == 32);

        struct JNINativeMethod nm = {"dummyNative", "()V", (void *)dummy_guest_native};
        int reg_ok = ((*env)->RegisterNatives(env, act_cls, &nm, 1) == 0);

        if (sdk_val == 33 && model_ok && w_val == 1280 && h_val == 720 && buf_ok && reg_ok) {
            write_marker(g_engine.internalDataPath, "engine_jni_ready.txt", "JNI_OK");
        }

        jclass bmp_cls = (*env)->FindClass(env, "android/graphics/Bitmap");
        jclass cfg_cls = (*env)->FindClass(env, "android/graphics/Bitmap$Config");
        if (bmp_cls && cfg_cls) {
            jfieldID argb_fid = (*env)->GetStaticFieldID(env, cfg_cls, "ARGB_8888",
                                                         "Landroid/graphics/Bitmap$Config;");
            jobject argb_obj =
                argb_fid ? (*env)->GetStaticObjectField(env, cfg_cls, argb_fid) : NULL;
            jmethodID create_mid = (*env)->GetStaticMethodID(
                env, bmp_cls, "createBitmap",
                "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
            jobject test_bmp =
                (create_mid && argb_obj)
                    ? (*env)->CallStaticObjectMethod(env, bmp_cls, create_mid, 64, 64, argb_obj)
                    : NULL;
            if (test_bmp) {
                struct AndroidBitmapInfo b_info;
                if (AndroidBitmap_getInfo(env, test_bmp, &b_info) == 0 && b_info.width == 64 &&
                    b_info.height == 64 && b_info.format == 1) {
                    void *pixels = NULL;
                    if (AndroidBitmap_lockPixels(env, test_bmp, &pixels) == 0 && pixels) {
                        ((uint32_t *)pixels)[0] = 0xFFAABBCC;
                        if (AndroidBitmap_unlockPixels(env, test_bmp) == 0) {
                            bmp_ok = 1;
                        }
                    }
                }
            }
        }

        jmethodID get_str_mid =
            res_cls ? (*env)->GetMethodID(env, res_cls, "getString", "(I)Ljava/lang/String;")
                    : NULL;
        jmethodID get_int_mid =
            res_cls ? (*env)->GetMethodID(env, res_cls, "getInteger", "(I)I") : NULL;
        jmethodID get_bool_mid =
            res_cls ? (*env)->GetMethodID(env, res_cls, "getBoolean", "(I)Z") : NULL;
        jmethodID get_id_mid =
            res_cls
                ? (*env)->GetMethodID(env, res_cls, "getIdentifier",
                                      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I")
                : NULL;

        jstring s_title =
            get_str_mid ? (*env)->CallObjectMethod(env, res_obj, get_str_mid, 0x7f010000) : NULL;
        const char *title_chars = s_title ? (*env)->GetStringUTFChars(env, s_title, NULL) : NULL;
        int title_ok = (title_chars && my_strcmp(title_chars, "Portage Game Engine") == 0);
        if (title_chars) {
            (*env)->ReleaseStringUTFChars(env, s_title, title_chars);
        }

        jstring s_welcome =
            get_str_mid ? (*env)->CallObjectMethod(env, res_obj, get_str_mid, 0x7f010001) : NULL;
        const char *welcome_chars =
            s_welcome ? (*env)->GetStringUTFChars(env, s_welcome, NULL) : NULL;
        int welcome_ok = (welcome_chars && my_strcmp(welcome_chars, "Welcome to Portage!") == 0);
        if (welcome_chars) {
            (*env)->ReleaseStringUTFChars(env, s_welcome, welcome_chars);
        }

        jint diff_val =
            get_int_mid ? (*env)->CallIntMethod(env, res_obj, get_int_mid, 0x7f020000) : 0;
        int diff_ok = (diff_val == 42);

        jboolean snd_val =
            get_bool_mid ? (*env)->CallBooleanMethod(env, res_obj, get_bool_mid, 0x7f030000) : 0;
        int snd_ok = (snd_val != 0);

        jstring id_name = (*env)->NewStringUTF(env, "app_name");
        jstring id_type = (*env)->NewStringUTF(env, "string");
        jstring id_pkg = (*env)->NewStringUTF(env, "com.portage.gameengine");
        jint res_ident =
            get_id_mid ? (*env)->CallIntMethod(env, res_obj, get_id_mid, id_name, id_type, id_pkg)
                       : 0;
        int ident_ok = (res_ident == 0x7f010000);

        jmethodID get_assets_mid = act_cls
                                       ? (*env)->GetMethodID(env, act_cls, "getAssets",
                                                             "()Landroid/content/res/AssetManager;")
                                       : NULL;
        jobject assets_obj =
            get_assets_mid ? (*env)->CallObjectMethod(env, activity_obj, get_assets_mid) : NULL;
        struct AAssetManager *asset_mgr = AAssetManager_fromJava(env, assets_obj);
        if (!asset_mgr && g_engine.activity) {
            asset_mgr = (struct AAssetManager *)g_engine.activity->assetManager;
        }

        int asset_ok = 0;
        int dir_ok = 0;
        if (asset_mgr) {
            struct AAsset *ast = AAssetManager_open(asset_mgr, "shaders/triangle.vert", 1);
            if (ast) {
                char abuf[128];
                int nread = AAsset_read(ast, abuf, sizeof(abuf) - 1);
                if (nread > 0) {
                    abuf[nread] = '\0';
                    if (my_strstr(abuf, "gl_Position") != NULL) {
                        asset_ok = 1;
                    }
                }
                AAsset_close(ast);
            }

            struct AAssetDir *adir = AAssetManager_openDir(asset_mgr, "shaders");
            if (adir) {
                const char *entry_name = AAssetDir_getNextFileName(adir);
                if (entry_name && my_strstr(entry_name, "triangle.vert") != NULL) {
                    dir_ok = 1;
                }
                AAssetDir_close(adir);
            }
        }

        if (title_ok && welcome_ok && diff_ok && snd_ok && ident_ok && asset_ok && dir_ok) {
            write_marker(g_engine.internalDataPath, "engine_resources_ready.txt", "RESOURCES_OK");
        }
    }

    struct AStorageManager *smgr = AStorageManager_new();
    if (smgr) {
        const char *obb_path = "/storage/emulated/0/Android/obb/com.portage.gameengine/"
                               "main.1.com.portage.gameengine.obb";
        g_engine_obb_state = 0;
        AStorageManager_mountObb(smgr, obb_path, NULL, engine_obb_callback, NULL);
        for (int i = 0; i < 200 && g_engine_obb_state == 0; ++i) {
            sys_nanosleep(2000000);
        }
        if (g_engine_obb_state == 1 && AStorageManager_isObbMounted(smgr, obb_path)) {
            const char *mounted_dir = AStorageManager_getMountedObbPath(smgr, obb_path);
            if (mounted_dir) {
                char asset_file[256];
                asset_file[0] = '\0';
                my_strcat(asset_file, mounted_dir);
                my_strcat(asset_file, "/obb_asset.txt");
                int64_t fd = sys_openat(-100, asset_file, 0 /* O_RDONLY */, 0);
                if (fd >= 0) {
                    char buf[64];
                    int64_t r = sys_read((int)fd, buf, sizeof(buf) - 1);
                    sys_close((int)fd);
                    if (r > 0) {
                        buf[r] = '\0';
                        if (my_strstr(buf, "OBB_CONTENT_OK") != NULL) {
                            write_marker(g_engine.internalDataPath, "engine_storage_ready.txt",
                                         "STORAGE_OK");
                        }
                    }
                }
            }
        }
        AStorageManager_delete(smgr);
    }

    net_handle_t engine_net = 0;
    if (android_getprocnetwork(&engine_net) == 0 && engine_net != 0) {
        struct addrinfo hints;
        hints.ai_flags = 0;
        hints.ai_family = 2;
        hints.ai_socktype = 1;
        hints.ai_protocol = 0;
        hints.ai_addrlen = 0;
        hints.__pad = 0;
        hints.ai_addr = NULL;
        hints.ai_canonname = NULL;
        hints.ai_next = NULL;

        struct addrinfo *res = NULL;
        if (android_getaddrinfofornetwork(engine_net, "127.0.0.1", "8080", &hints, &res) == 0 &&
            res != NULL) {
            int qfd = android_res_nquery(engine_net, "localhost", 1, 1, 0);
            if (qfd >= 0) {
                int rcode = -1;
                int rlen = android_res_nresult(qfd, &rcode);
                if (rlen > 0 && rcode == 0) {
                    write_marker(g_engine.internalDataPath, "engine_multinetwork_ready.txt",
                                 "MULTINETWORK_OK");
                }
                android_res_cancel(qfd);
            }
            android_freeaddrinfofornetwork(res);
        }
    }

    struct AThermalManager *tm = AThermalManager_acquireManager();
    if (tm) {
        int t_status = AThermalManager_getCurrentThermalStatus(tm);
        float t_headroom = AThermalManager_getThermalHeadroom(tm, 10);
        if (t_status >= 0 && t_headroom > 0.0f) {
            struct APerformanceHintManager *phm = APerformanceHintManager_create();
            if (phm) {
                int32_t tid = 1234;
                struct APerformanceHintSession *phs =
                    APerformanceHintManager_createSession(phm, &tid, 1, 16666666LL);
                if (phs) {
                    if (APerformanceHintSession_reportActualWorkDuration(phs, 15000000LL) == 0) {
                        write_marker(g_engine.internalDataPath, "engine_adpf_ready.txt", "ADPF_OK");
                    }
                    APerformanceHintSession_close(phs);
                }
            }
        }
        AThermalManager_releaseManager(tm);
    }

    void *audio_builder = NULL;
    void *audio_stream = NULL;
    if (AAudio_createStreamBuilder(&audio_builder) == 0 && audio_builder) {
        AAudioStreamBuilder_setSampleRate(audio_builder, 48000);
        AAudioStreamBuilder_setChannelCount(audio_builder, 2);
        AAudioStreamBuilder_setFormat(audio_builder, 1);
        AAudioStreamBuilder_setDataCallback(audio_builder, (void *)engine_audio_callback, NULL);
        if (AAudioStreamBuilder_openStream(audio_builder, &audio_stream) == 0 && audio_stream) {
            write_marker(g_engine.internalDataPath, "engine_audio_ready.txt", "READY");
            AAudioStream_requestStart(audio_stream);
            write_marker(g_engine.internalDataPath, "engine_audio_playing.txt", "PLAYING");
        }
        AAudioStreamBuilder_delete(audio_builder);
    }

    void *in_builder = NULL;
    void *in_stream = NULL;
    if (AAudio_createStreamBuilder(&in_builder) == 0 && in_builder) {
        AAudioStreamBuilder_setDirection(in_builder, 1);
        AAudioStreamBuilder_setSampleRate(in_builder, 48000);
        AAudioStreamBuilder_setChannelCount(in_builder, 1);
        AAudioStreamBuilder_setFormat(in_builder, 1);
        if (AAudioStreamBuilder_openStream(in_builder, &in_stream) == 0 && in_stream) {
            AAudioStream_requestStart(in_stream);
            int16_t in_buf[128];
            int32_t read_frames = AAudioStream_read(in_stream, in_buf, 128, 50000000LL);
            if (read_frames > 0) {
                write_marker(g_engine.internalDataPath, "engine_audio_record_ready.txt",
                             "AUDIO_RECORD_OK");
            }
            AAudioStream_requestStop(in_stream);
            AAudioStream_close(in_stream);
        }
        AAudioStreamBuilder_delete(in_builder);
    }

    uint64_t refresh_cycle = SwappyVk_initAndGetRefreshCycleDuration(NULL, NULL, NULL);
    if (refresh_cycle > 0) {
        SwappyGL_init(NULL, NULL);
        if (g_engine.window) {
            SwappyGL_setWindow((void *)g_engine.window);
            SwappyGL_swap(NULL, NULL);
            int64_t q_dur = 0;
            if (ANativeWindow_getLastQueueDuration((void *)g_engine.window, &q_dur) == 0) {
                write_marker(g_engine.internalDataPath, "engine_swappy_ready.txt", "SWAPPY_OK");
            }
        } else {
            write_marker(g_engine.internalDataPath, "engine_swappy_ready.txt", "SWAPPY_OK");
        }
        SwappyGL_destroy();
    }

    void *cam_mgr = ACameraManager_create();
    if (cam_mgr) {
        ACameraIdList_guest *cam_ids = NULL;
        if (ACameraManager_getCameraIdList(cam_mgr, &cam_ids) == 0 && cam_ids &&
            cam_ids->numCameras > 0) {
            void *img_reader = NULL;
            if (AImageReader_new(640, 480, 1, 2, &img_reader) == 0 && img_reader) {
                void *img_win = NULL;
                AImageReader_getWindow(img_reader, &img_win);
                if (img_win) {
                    void *cam_dev = NULL;
                    if (ACameraManager_openCamera(cam_mgr, cam_ids->cameraIds[0], NULL, &cam_dev) ==
                            0 &&
                        cam_dev) {
                        void *sess_cont = NULL;
                        ACaptureSessionOutputContainer_create(&sess_cont);
                        void *sess_out = NULL;
                        ACaptureSessionOutput_create(img_win, &sess_out);
                        ACaptureSessionOutputContainer_add(sess_cont, sess_out);

                        void *cam_sess = NULL;
                        if (ACameraDevice_createCaptureSession(cam_dev, sess_cont, NULL,
                                                               &cam_sess) == 0 &&
                            cam_sess) {
                            void *cap_req = NULL;
                            ACameraDevice_createCaptureRequest(cam_dev, 1, &cap_req);
                            void *out_target = NULL;
                            ACameraOutputTarget_create(img_win, &out_target);
                            ACaptureRequest_addTarget(cap_req, out_target);

                            int seq = 0;
                            ACameraCaptureSession_capture(cam_sess, NULL, 1, &cap_req, &seq);

                            void *cap_img = NULL;
                            if (AImageReader_acquireNextImage(img_reader, &cap_img) == 0 &&
                                cap_img) {
                                int32_t w = 0;
                                int32_t h = 0;
                                AImage_getWidth(cap_img, &w);
                                AImage_getHeight(cap_img, &h);
                                if (w == 640 && h == 480) {
                                    write_marker(g_engine.internalDataPath,
                                                 "engine_camera_ready.txt", "CAMERA_OK");
                                }
                                AImage_delete(cap_img);
                            }

                            ACaptureRequest_removeTarget(cap_req, out_target);
                            ACameraOutputTarget_free(out_target);
                            ACaptureRequest_free(cap_req);
                            ACameraCaptureSession_close(cam_sess);
                        }
                        ACaptureSessionOutputContainer_remove(sess_cont, sess_out);
                        ACaptureSessionOutput_free(sess_out);
                        ACaptureSessionOutputContainer_free(sess_cont);
                        ACameraDevice_close(cam_dev);
                    }
                }
                AImageReader_delete(img_reader);
            }
            ACameraManager_deleteCameraIdList(cam_ids);
        }
        ACameraManager_delete(cam_mgr);
    }

    void *keystore = AKeyStore_create();
    if (keystore) {
        if (AKeyStore_generateAesKey(keystore, "game_save_key", 256, 2, 1) == 0) {
            const char *secret = "Encrypted Save Game Data";
            uint8_t *ciphertext = NULL;
            size_t cipher_len = 0;
            uint8_t *iv = NULL;
            size_t iv_len = 0;
            uint8_t *tag = NULL;
            size_t tag_len = 0;
            if (AKeyStore_encrypt(keystore, "game_save_key", (const uint8_t *)secret, 24,
                                  &ciphertext, &cipher_len, &iv, &iv_len, &tag, &tag_len) == 0 &&
                ciphertext && cipher_len > 0) {
                uint8_t *plain = NULL;
                size_t plain_len = 0;
                if (AKeyStore_decrypt(keystore, "game_save_key", ciphertext, cipher_len, iv, iv_len,
                                      tag, tag_len, &plain, &plain_len) == 0 &&
                    plain && plain_len == 24) {
                    write_marker(g_engine.internalDataPath, "engine_keystore_ready.txt",
                                 "KEYSTORE_OK");
                    AKeyStore_freeBuffer(plain);
                }
                AKeyStore_freeBuffer(ciphertext);
                if (iv)
                    AKeyStore_freeBuffer(iv);
                if (tag)
                    AKeyStore_freeBuffer(tag);
            }
        }
        AKeyStore_delete(keystore);
    }

    void *loc_mgr = ALocationManager_create();
    if (loc_mgr) {
        if (ALocationManager_isProviderEnabled(loc_mgr, "gps")) {
            void *loc = ALocationManager_getLastKnownLocation(loc_mgr, "gps");
            if (loc) {
                double lat = ALocation_getLatitude(loc);
                double lon = ALocation_getLongitude(loc);
                if (lat != 0.0 && lon != 0.0) {
                    write_marker(g_engine.internalDataPath, "engine_location_ready.txt",
                                 "LOCATION_OK");
                }
                ALocation_destroy(loc);
            }
        }
        ALocationManager_destroy(loc_mgr);
    }

    void *clipboard = AClipboard_create();
    if (clipboard) {
        if (AClipboard_setText(clipboard, "Portage Clipboard Verification") == 0) {
            char clip_buf[64];
            int len = AClipboard_getText(clipboard, clip_buf, sizeof(clip_buf));
            if (len > 0 && my_strcmp(clip_buf, "Portage Clipboard Verification") == 0) {
                write_marker(g_engine.internalDataPath, "engine_clipboard_ready.txt",
                             "CLIPBOARD_OK");
            }
        }
        AClipboard_destroy(clipboard);
    }

    void *power_mgr = APowerManager_create();
    if (power_mgr) {
        if (APowerManager_isInteractive(power_mgr)) {
            void *lock = AWakeLock_create(1, "engine_cpu_lock");
            if (lock) {
                if (AWakeLock_acquire(lock, -1) == 0 && AWakeLock_isHeld(lock)) {
                    AWakeLock_release(lock);
                    void *bat_mgr = ABatteryManager_create();
                    if (bat_mgr) {
                        int32_t cap = ABatteryManager_getCapacity(bat_mgr);
                        if (cap >= 0) {
                            write_marker(g_engine.internalDataPath, "engine_power_ready.txt",
                                         "POWER_OK");
                        }
                        ABatteryManager_destroy(bat_mgr);
                    }
                }
                AWakeLock_destroy(lock);
            }
        }
        APowerManager_destroy(power_mgr);
    }

    void *notif_mgr = ANotificationManager_create();
    if (notif_mgr) {
        void *ch = ANotificationChannel_create("engine_channel", "Engine Alerts", 3);
        if (ch) {
            if (ANotificationManager_createNotificationChannel(notif_mgr, ch) == 0) {
                void *n = ANotification_create("engine_channel");
                if (n) {
                    ANotification_setTitle(n, "Engine Notification");
                    ANotification_setText(n, "Engine Subsystems Initialized");
                    if (ANotificationManager_notify(notif_mgr, "engine", 77, n) == 0) {
                        ANotificationManager_cancel(notif_mgr, "engine", 77);
                        AToast_show("Engine Ready", 0);
                        write_marker(g_engine.internalDataPath, "engine_notification_ready.txt",
                                     "NOTIFICATION_OK");
                    }
                    ANotification_destroy(n);
                }
            }
            ANotificationChannel_destroy(ch);
        }
        ANotificationManager_destroy(notif_mgr);
    }

    void *dpy = NULL;
    void *surface = NULL;
    void *context = NULL;
    uint32_t prog = 0;
    uint32_t vbo = 0;
    int32_t pos_loc = -1;
    int32_t offset_loc = -1;
    int32_t angle_loc = -1;
    int32_t color_loc = -1;
    int gles_ready = 0;

    float offset_x = 0.0f;
    float offset_y = 0.0f;
    int color_idx = 0;

    struct ChoreographerState choreo_state;
    choreo_state.choreographer = AChoreographer_getInstance();
    choreo_state.last_frame_ns = 0;
    choreo_state.vsync_period_ns = 0;
    choreo_state.frame_count = 0;
    choreo_state.refresh_ok = 0;
    choreo_state.marker_written = 0;
    choreo_state.angle = 0.0f;

    if (choreo_state.choreographer) {
        AChoreographer_registerRefreshRateCallback(choreo_state.choreographer,
                                                   engine_on_refresh_rate, &choreo_state);
        AChoreographer_postFrameCallback64(choreo_state.choreographer, engine_on_frame64,
                                           &choreo_state);
    }

    struct ASensorManager *sensor_mgr =
        ASensorManager_getInstanceForPackage("com.portage.gameengine");
    struct ASensorEventQueue *sensor_queue = NULL;
    const struct ASensor *accel_sensor = NULL;
    int sensor_marker_written = 0;

    if (sensor_mgr) {
        sensor_queue =
            ASensorManager_createEventQueue(sensor_mgr, looper, LOOPER_ID_SENSOR, NULL, NULL);
        accel_sensor =
            ASensorManager_getDefaultSensor(sensor_mgr, 1 /* ASENSOR_TYPE_ACCELEROMETER */);
        if (sensor_queue && accel_sensor) {
            ASensorEventQueue_registerSensor(sensor_queue, accel_sensor, 16667, 0);
        }
    }

    while (g_engine.running) {
        int outFd = 0;
        int outEvents = 0;
        void *outData = NULL;
        int ident = ALooper_pollOnce(16, &outFd, &outEvents, &outData);

        if (ident == LOOPER_ID_CMD) {
            uint8_t cmd = 0;
            if (sys_read(g_engine.msgpipe[0], &cmd, 1) > 0) {
                __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "Worker received cmd=%d",
                                    cmd);
                if (cmd == APP_CMD_INIT_WINDOW) {
                    write_marker(g_engine.internalDataPath, "engine_window_init.txt", "OK");
                    if (g_engine.window) {
                        struct ANativeWindow_Buffer win_buf;
                        if (ANativeWindow_lock(g_engine.window, &win_buf, NULL) == 0) {
                            if (win_buf.bits && win_buf.width > 0 && win_buf.height > 0) {
                                uint32_t *p = (uint32_t *)win_buf.bits;
                                for (int y = 0; y < 10; ++y) {
                                    for (int x = 0; x < 10; ++x) {
                                        p[y * win_buf.stride + x] = 0xFF00FF00;
                                    }
                                }
                            }
                            if (ANativeWindow_unlockAndPost(g_engine.window) == 0 && bmp_ok) {
                                write_marker(g_engine.internalDataPath, "engine_surface_ready.txt",
                                             "SURFACE_OK");
                            }
                        }
                    }
                    if (!gles_ready && g_engine.window) {
                        gles_ready =
                            init_gles(&dpy, &surface, &context, &prog, &vbo, &pos_loc, &offset_loc,
                                      &angle_loc, &color_loc, g_engine.window);
                    }
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
                    color_idx = (color_idx + 1) % 4;
                    g_audio_chime_envelope = 4800;
                    write_marker(g_engine.internalDataPath, "engine_motion_received.txt", "OK");
                } else if (type == 1) { // Key
                    int32_t kc = AKeyEvent_getKeyCode(evt);
                    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "Worker key code=%d",
                                        kc);
                    if (kc == AKEYCODE_DPAD_LEFT || kc == AKEYCODE_A) {
                        offset_x -= 0.05f;
                    } else if (kc == AKEYCODE_DPAD_RIGHT || kc == AKEYCODE_D) {
                        offset_x += 0.05f;
                    } else if (kc == AKEYCODE_DPAD_UP || kc == AKEYCODE_W) {
                        offset_y += 0.05f;
                    } else if (kc == AKEYCODE_DPAD_DOWN || kc == AKEYCODE_S) {
                        offset_y -= 0.05f;
                    }
                    g_audio_chime_envelope = 4800;
                    write_marker(g_engine.internalDataPath, "engine_key_received.txt", "OK");
                }
                AInputQueue_finishEvent(g_engine.inputQueue, evt, 1);
            }
        } else if (ident == LOOPER_ID_SENSOR && sensor_queue) {
            struct ASensorEvent s_events[8];
            int64_t n_events = ASensorEventQueue_getEvents(sensor_queue, s_events, 8);
            if (n_events > 0 && !sensor_marker_written) {
                for (int64_t i = 0; i < n_events; ++i) {
                    if (s_events[i].type == 1 /* ACCEL */) {
                        float z = s_events[i].acceleration.z;
                        if (z > 9.0f && z < 10.5f) {
                            sensor_marker_written = 1;
                            write_marker(g_engine.internalDataPath, "engine_sensor_ready.txt",
                                         "SENSOR_OK");
                            break;
                        }
                    }
                }
            }
        }

        if (gles_ready && surface && dpy) {
            glViewport(0, 0, 1280, 720);
            glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(prog);
            glUniform1f(angle_loc, choreo_state.angle);
            glUniform4f(offset_loc, offset_x, offset_y, 0.0f, 0.0f);
            glUniform4f(color_loc, kColors[color_idx][0], kColors[color_idx][1],
                        kColors[color_idx][2], 1.0f);

            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray((uint32_t)pos_loc);
            glVertexAttribPointer((uint32_t)pos_loc, 2, GL_FLOAT, 0, 2 * sizeof(float), (void *)0);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            eglSwapBuffers(dpy, surface);
        }
    }

    if (choreo_state.choreographer) {
        AChoreographer_unregisterRefreshRateCallback(choreo_state.choreographer,
                                                     engine_on_refresh_rate, &choreo_state);
    }

    if (sensor_queue && accel_sensor) {
        ASensorEventQueue_disableSensor(sensor_queue, accel_sensor);
    }
    if (sensor_mgr && sensor_queue) {
        ASensorManager_destroyEventQueue(sensor_mgr, sensor_queue);
    }

    if (gles_ready) {
        cleanup_gles(dpy, surface, context, prog, vbo);
    }

    if (audio_stream) {
        AAudioStream_requestStop(audio_stream);
        AAudioStream_close(audio_stream);
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
    g_engine.window = window;
    __android_log_print(ANDROID_LOG_INFO, "PortageEngine", "onNativeWindowCreated");
    uint8_t cmd = APP_CMD_INIT_WINDOW;
    sys_write(g_engine.msgpipe[1], &cmd, 1);
}

static void __attribute__((sysv_abi))
engine_onNativeWindowDestroyed(struct ANativeActivity *activity, struct ANativeWindow *window)
{
    (void)activity;
    (void)window;
    g_engine.window = NULL;
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
