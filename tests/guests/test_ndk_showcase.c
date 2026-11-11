#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AAssetManager;
struct AAsset;
struct AImageDecoder;
struct AImageDecoderHeaderInfo;
struct AImageDecoderFrameInfo;
struct ANativeWindow;
struct AInputQueue;

struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct AndroidBitmapInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t format;
    uint32_t flags;
};

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
typedef jobject jbyteArray;
typedef void *jmethodID;
typedef void *jfieldID;

struct JNIInvokeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint (*DestroyJavaVM)(void *);
    jint (*AttachCurrentThread)(void *, void **, void *);
    jint (*DetachCurrentThread)(void *);
    jint (*GetEnv)(void *, void **, jint);
    jint (*AttachCurrentThreadAsDaemon)(void *, void **, void *);
};

typedef const struct JNIInvokeInterface *JavaVM;

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
    jint (*Throw)(JNIEnv *, void *);
    jint (*ThrowNew)(JNIEnv *, jclass, const char *);
    void *(*ExceptionOccurred)(JNIEnv *);
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
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);

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
};

struct ANativeActivityCallbacks {
    void (*onStart)(void *);
    void (*onResume)(void *);
    void *(*onSaveInstanceState)(void *, size_t *);
    void (*onPause)(void *);
    void (*onStop)(void *);
    void (*onDestroy)(void *);
    void (*onWindowFocusChanged)(void *, int);
    void (*onNativeWindowCreated)(void *, struct ANativeWindow *);
    void (*onNativeWindowResized)(void *, struct ANativeWindow *);
    void (*onNativeWindowRedrawNeeded)(void *, struct ANativeWindow *);
    void (*onNativeWindowDestroyed)(void *, struct ANativeWindow *);
    void (*onInputQueueCreated)(void *, struct AInputQueue *);
    void (*onInputQueueDestroyed)(void *, struct AInputQueue *);
    void (*onContentRectChanged)(void *, const void *);
    void (*onConfigurationChanged)(void *);
    void (*onLowMemory)(void *);
};

struct ANativeActivity {
    struct ANativeActivityCallbacks *callbacks;
    JavaVM *vm;
    JNIEnv *env;
    jobject clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int32_t sdkVersion;
    void *instance;
    struct AAssetManager *assetManager;
    const char *obbPath;
};

// NDK & Bionic imports (resolved dynamically from Portage virtual libraries)
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern void abort(void);
extern int strcmp(const char *a, const char *b);
extern void *malloc(size_t size);
extern void free(void *ptr);

extern int pthread_mutex_lock(void *mutex);
extern int pthread_mutex_unlock(void *mutex);
extern int pthread_create(uintptr_t *thread_out, const void *attr, void *(*start_fn)(void *),
                          void *arg);
extern int pthread_join(uintptr_t thread, void **retval);

extern float sinf(float x);
extern float sqrtf(float x);

extern int compress2(uint8_t *dest, uint64_t *destLen, const uint8_t *source, uint64_t sourceLen,
                     int level);
extern int uncompress(uint8_t *dest, uint64_t *destLen, const uint8_t *source, uint64_t sourceLen);
extern uint64_t crc32(uint64_t crc, const uint8_t *buf, uint32_t len);

extern struct AAsset *AAssetManager_open(struct AAssetManager *mgr, const char *filename, int mode);
extern void AAsset_close(struct AAsset *asset);

extern int AndroidBitmap_getInfo(JNIEnv *env, jobject jbitmap, struct AndroidBitmapInfo *info);
extern int AndroidBitmap_lockPixels(JNIEnv *env, jobject jbitmap, void **addrPtr);
extern int AndroidBitmap_unlockPixels(JNIEnv *env, jobject jbitmap);

extern int AImageDecoder_createFromAAsset(struct AAsset *asset, struct AImageDecoder **outDecoder);
extern void AImageDecoder_delete(struct AImageDecoder *decoder);
extern const struct AImageDecoderHeaderInfo *
AImageDecoder_getHeaderInfo(const struct AImageDecoder *decoder);
extern int32_t AImageDecoderHeaderInfo_getWidth(const struct AImageDecoderHeaderInfo *info);
extern int32_t AImageDecoderHeaderInfo_getHeight(const struct AImageDecoderHeaderInfo *info);
extern const char *AImageDecoderHeaderInfo_getMimeType(const struct AImageDecoderHeaderInfo *info);
extern int AImageDecoder_setTargetSize(struct AImageDecoder *decoder, int32_t width,
                                       int32_t height);
extern size_t AImageDecoder_getMinimumStride(const struct AImageDecoder *decoder);
extern int AImageDecoder_decodeImage(struct AImageDecoder *decoder, void *pixels, size_t stride,
                                     size_t size);
extern bool AImageDecoder_isAnimated(const struct AImageDecoder *decoder);
extern int AImageDecoder_advanceFrame(struct AImageDecoder *decoder);
extern int AImageDecoder_rewind(struct AImageDecoder *decoder);

extern void glGenTextures(int32_t n, uint32_t *textures);
extern void glBindTexture(uint32_t target, uint32_t texture);
extern void glTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width,
                         int32_t height, int32_t border, uint32_t format, uint32_t type,
                         const void *pixels);
extern void glDeleteTextures(int32_t n, const uint32_t *textures);

static void Fail(const char *msg)
{
    __android_log_print(6, "ndk-showcase", "%s", msg);
    abort();
}

struct ThreadJob {
    uint8_t mutex[40];
    float sum;
};

static void *MathWorker(void *arg)
{
    struct ThreadJob *job = (struct ThreadJob *)arg;
    float s = sinf(1.5707963f) + sqrtf(9.0f);
    pthread_mutex_lock(job->mutex);
    job->sum += s;
    pthread_mutex_unlock(job->mutex);
    return (void *)0x42;
}

__attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env = (JNIEnv *)0;
    if (!vm || (*vm)->GetEnv(vm, (void **)&env, 0x00010006) != 0 || !env) {
        Fail("GetEnv failed");
    }

    jclass ver_cls = (*env)->FindClass(env, "android/os/Build$VERSION");
    jfieldID fid_sdk = (*env)->GetStaticFieldID(env, ver_cls, "SDK_INT", "I");
    if ((*env)->GetStaticIntField(env, ver_cls, fid_sdk) < 30) {
        Fail("SDK_INT unexpected");
    }

    jclass bf_cls = (*env)->FindClass(env, "android/graphics/BitmapFactory");
    jmethodID mid_dec_res =
        (*env)->GetStaticMethodID(env, bf_cls, "decodeResource",
                                  "(Landroid/content/res/Resources;I)Landroid/graphics/Bitmap;");
    jobject bmp = (*env)->CallStaticObjectMethod(env, bf_cls, mid_dec_res, (jobject)0, 0x7f010001);
    if (!bmp) {
        Fail("decodeResource failed");
    }

    struct AndroidBitmapInfo binfo;
    if (AndroidBitmap_getInfo(env, bmp, &binfo) != 0 || binfo.width != 4 || binfo.height != 4) {
        Fail("AndroidBitmap_getInfo mismatch");
    }
    void *px = (void *)0;
    if (AndroidBitmap_lockPixels(env, bmp, &px) != 0 || !px) {
        Fail("AndroidBitmap_lockPixels failed");
    }
    AndroidBitmap_unlockPixels(env, bmp);

    return 0x00010006;
}

__attribute__((visibility("default"))) void
ANativeActivity_onCreate(struct ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;
    if (!activity || !activity->assetManager || !activity->env) {
        Fail("Invalid ANativeActivity");
    }
    JNIEnv *env = activity->env;

    // Decode deflate-compressed PNG via AImageDecoder and AAssetManager
    struct AAsset *png_asset = AAssetManager_open(activity->assetManager, "test.png", 3);
    if (!png_asset) {
        Fail("Failed to open assets/test.png");
    }
    struct AImageDecoder *png_dec = (struct AImageDecoder *)0;
    if (AImageDecoder_createFromAAsset(png_asset, &png_dec) != 0 || !png_dec) {
        Fail("AImageDecoder_createFromAAsset(test.png) failed");
    }
    const struct AImageDecoderHeaderInfo *hdr = AImageDecoder_getHeaderInfo(png_dec);
    if (AImageDecoderHeaderInfo_getWidth(hdr) != 4 || AImageDecoderHeaderInfo_getHeight(hdr) != 4 ||
        strcmp(AImageDecoderHeaderInfo_getMimeType(hdr), "image/png") != 0) {
        Fail("PNG header mismatch");
    }
    AImageDecoder_setTargetSize(png_dec, 2, 2);
    size_t stride = AImageDecoder_getMinimumStride(png_dec);
    void *scaled_px = malloc(stride * 2);
    if (AImageDecoder_decodeImage(png_dec, scaled_px, stride, stride * 2) != 0) {
        Fail("AImageDecoder_decodeImage failed");
    }
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(0x0DE1, tex);
    glTexImage2D(0x0DE1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, scaled_px);
    glDeleteTextures(1, &tex);
    free(scaled_px);
    AImageDecoder_delete(png_dec);
    AAsset_close(png_asset);

    // Decode animated GIF frames
    struct AAsset *gif_asset = AAssetManager_open(activity->assetManager, "anim.gif", 3);
    if (!gif_asset) {
        Fail("Failed to open assets/anim.gif");
    }
    struct AImageDecoder *gif_dec = (struct AImageDecoder *)0;
    if (AImageDecoder_createFromAAsset(gif_asset, &gif_dec) != 0 ||
        !AImageDecoder_isAnimated(gif_dec)) {
        Fail("Animated GIF check failed");
    }
    uint8_t gif_buf[16];
    if (AImageDecoder_decodeImage(gif_dec, gif_buf, 8, sizeof(gif_buf)) != 0 || gif_buf[0] < 200) {
        Fail("GIF frame 0 pixel check failed");
    }
    if (AImageDecoder_advanceFrame(gif_dec) != 0 ||
        AImageDecoder_decodeImage(gif_dec, gif_buf, 8, sizeof(gif_buf)) != 0 || gif_buf[2] < 200) {
        Fail("GIF frame 1 pixel check failed");
    }
    AImageDecoder_rewind(gif_dec);
    AImageDecoder_delete(gif_dec);
    AAsset_close(gif_asset);

    // SharedPreferences roundtrip via JNI
    jclass act_cls = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID m_sp =
        (*env)->GetMethodID(env, act_cls, "getSharedPreferences",
                            "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    jobject sp = (*env)->CallObjectMethod(env, activity->clazz, m_sp,
                                          (*env)->NewStringUTF(env, "showcase_prefs"), 0);
    jclass sp_cls = (*env)->GetObjectClass(env, sp);
    jmethodID m_edit =
        (*env)->GetMethodID(env, sp_cls, "edit", "()Landroid/content/SharedPreferences$Editor;");
    jobject ed = (*env)->CallObjectMethod(env, sp, m_edit);
    jclass ed_cls = (*env)->GetObjectClass(env, ed);
    jmethodID m_puti = (*env)->GetMethodID(
        env, ed_cls, "putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;");
    jmethodID m_commit = (*env)->GetMethodID(env, ed_cls, "commit", "()Z");
    (*env)->CallObjectMethod(env, ed, m_puti, (*env)->NewStringUTF(env, "runs"), 7);
    (*env)->CallBooleanMethod(env, ed, m_commit);
    jmethodID m_geti = (*env)->GetMethodID(env, sp_cls, "getInt", "(Ljava/lang/String;I)I");
    if ((*env)->CallIntMethod(env, sp, m_geti, (*env)->NewStringUTF(env, "runs"), 0) != 7) {
        Fail("SharedPreferences check failed");
    }

    // zlib LP64 compress2/uncompress and crc32 verification
    const char *msg = "Portage NDK compatibility showcase zlib test string repeated!";
    size_t msg_len = 59;
    uint8_t comp[128];
    uint64_t comp_len = sizeof(comp);
    if (compress2(comp, &comp_len, (const uint8_t *)msg, msg_len, 6) != 0) {
        Fail("zlib compress2 failed");
    }
    uint8_t decomp[128];
    uint64_t decomp_len = sizeof(decomp);
    if (uncompress(decomp, &decomp_len, comp, comp_len) != 0 || decomp_len != msg_len ||
        crc32(0, decomp, (uint32_t)decomp_len) !=
            crc32(0, (const uint8_t *)msg, (uint32_t)msg_len)) {
        Fail("zlib uncompress/crc32 mismatch");
    }

    // Multi-threaded worker using pthread_create and libm
    struct ThreadJob job = {{0}, 0.0f};
    uintptr_t th = 0;
    void *ret = (void *)0;
    if (pthread_create(&th, (const void *)0, MathWorker, &job) != 0 ||
        pthread_join(th, &ret) != 0 || ret != (void *)0x42 || job.sum < 3.9f || job.sum > 4.1f) {
        Fail("pthread_create + libm failed");
    }

    __android_log_print(4, "ndk-showcase", "[ndk-showcase] ALL CHECKS PASSED\n");
}
