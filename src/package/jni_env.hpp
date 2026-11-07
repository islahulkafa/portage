#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace portage::package {

constexpr int JNI_OK = 0;
constexpr int JNI_ERR = -1;
constexpr int JNI_EDETACHED = -2;
constexpr int JNI_EVERSION = -3;

constexpr int JNI_VERSION_1_1 = 0x00010001;
constexpr int JNI_VERSION_1_2 = 0x00010002;
constexpr int JNI_VERSION_1_4 = 0x00010004;
constexpr int JNI_VERSION_1_6 = 0x00010006;

using jint = int32_t;
using jlong = int64_t;
using jbyte = int8_t;
using jboolean = uint8_t;
using jchar = uint16_t;
using jshort = int16_t;
using jfloat = float;
using jdouble = double;
using jsize = jint;

using jobject = void *;
using jclass = jobject;
using jstring = jobject;
using jarray = jobject;
using jthrowable = jobject;
using jweak = jobject;
using jmethodID = void *;
using jfieldID = void *;

struct JavaVM;
struct JNIEnv;

struct JNIInvokeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint(__attribute__((sysv_abi)) * DestroyJavaVM)(JavaVM *vm);
    jint(__attribute__((sysv_abi)) * AttachCurrentThread)(JavaVM *vm, void **penv, void *args);
    jint(__attribute__((sysv_abi)) * DetachCurrentThread)(JavaVM *vm);
    jint(__attribute__((sysv_abi)) * GetEnv)(JavaVM *vm, void **penv, jint version);
    jint(__attribute__((sysv_abi)) * AttachCurrentThreadAsDaemon)(JavaVM *vm, void **penv,
                                                                  void *args);
};

struct JNINativeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    void *reserved3;
    jint(__attribute__((sysv_abi)) * GetVersion)(JNIEnv *);
    jclass(__attribute__((sysv_abi)) * DefineClass)(JNIEnv *, const char *, jobject, const jbyte *,
                                                    jsize);
    jclass(__attribute__((sysv_abi)) * FindClass)(JNIEnv *, const char *name);
    jmethodID(__attribute__((sysv_abi)) * FromReflectedMethod)(JNIEnv *, jobject);
    jfieldID(__attribute__((sysv_abi)) * FromReflectedField)(JNIEnv *, jobject);
    jobject(__attribute__((sysv_abi)) * ToReflectedMethod)(JNIEnv *, jclass, jmethodID, jboolean);
    jclass(__attribute__((sysv_abi)) * GetSuperclass)(JNIEnv *, jclass);
    jboolean(__attribute__((sysv_abi)) * IsAssignableFrom)(JNIEnv *, jclass, jclass);
    jobject(__attribute__((sysv_abi)) * ToReflectedField)(JNIEnv *, jclass, jfieldID, jboolean);
    jint(__attribute__((sysv_abi)) * Throw)(JNIEnv *, jthrowable);
    jint(__attribute__((sysv_abi)) * ThrowNew)(JNIEnv *, jclass, const char *);
    jthrowable(__attribute__((sysv_abi)) * ExceptionOccurred)(JNIEnv *);
    void(__attribute__((sysv_abi)) * ExceptionDescribe)(JNIEnv *);
    void(__attribute__((sysv_abi)) * ExceptionClear)(JNIEnv *);
    void(__attribute__((sysv_abi)) * FatalError)(JNIEnv *, const char *);
    jint(__attribute__((sysv_abi)) * PushLocalFrame)(JNIEnv *, jint);
    jobject(__attribute__((sysv_abi)) * PopLocalFrame)(JNIEnv *, jobject);
    jobject(__attribute__((sysv_abi)) * NewGlobalRef)(JNIEnv *, jobject);
    void(__attribute__((sysv_abi)) * DeleteGlobalRef)(JNIEnv *, jobject);
    void(__attribute__((sysv_abi)) * DeleteLocalRef)(JNIEnv *, jobject);
    jboolean(__attribute__((sysv_abi)) * IsSameObject)(JNIEnv *, jobject, jobject);
    jobject(__attribute__((sysv_abi)) * NewLocalRef)(JNIEnv *, jobject);
    jint(__attribute__((sysv_abi)) * EnsureLocalCapacity)(JNIEnv *, jint);
    jobject(__attribute__((sysv_abi)) * AllocObject)(JNIEnv *, jclass);
    jobject(__attribute__((sysv_abi)) * NewObject)(JNIEnv *, jclass, jmethodID, ...);
    jobject(__attribute__((sysv_abi)) * NewObjectV)(JNIEnv *, jclass, jmethodID, va_list);
    jobject(__attribute__((sysv_abi)) * NewObjectA)(JNIEnv *, jclass, jmethodID, const void *);
    jclass(__attribute__((sysv_abi)) * GetObjectClass)(JNIEnv *, jobject);
    jboolean(__attribute__((sysv_abi)) * IsInstanceOf)(JNIEnv *, jobject, jclass);
    jmethodID(__attribute__((sysv_abi)) * GetMethodID)(JNIEnv *, jclass, const char *name,
                                                       const char *sig);
    jobject(__attribute__((sysv_abi)) * CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    jobject(__attribute__((sysv_abi)) * CallObjectMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jobject(__attribute__((sysv_abi)) * CallObjectMethodA)(JNIEnv *, jobject, jmethodID,
                                                           const void *);
    jboolean(__attribute__((sysv_abi)) * CallBooleanMethod)(JNIEnv *, jobject, jmethodID, ...);
    jboolean(__attribute__((sysv_abi)) * CallBooleanMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jboolean(__attribute__((sysv_abi)) * CallBooleanMethodA)(JNIEnv *, jobject, jmethodID,
                                                             const void *);
    jbyte(__attribute__((sysv_abi)) * CallByteMethod)(JNIEnv *, jobject, jmethodID, ...);
    jbyte(__attribute__((sysv_abi)) * CallByteMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jbyte(__attribute__((sysv_abi)) * CallByteMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jchar(__attribute__((sysv_abi)) * CallCharMethod)(JNIEnv *, jobject, jmethodID, ...);
    jchar(__attribute__((sysv_abi)) * CallCharMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jchar(__attribute__((sysv_abi)) * CallCharMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jshort(__attribute__((sysv_abi)) * CallShortMethod)(JNIEnv *, jobject, jmethodID, ...);
    jshort(__attribute__((sysv_abi)) * CallShortMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jshort(__attribute__((sysv_abi)) * CallShortMethodA)(JNIEnv *, jobject, jmethodID,
                                                         const void *);
    jint(__attribute__((sysv_abi)) * CallIntMethod)(JNIEnv *, jobject, jmethodID, ...);
    jint(__attribute__((sysv_abi)) * CallIntMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jint(__attribute__((sysv_abi)) * CallIntMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jlong(__attribute__((sysv_abi)) * CallLongMethod)(JNIEnv *, jobject, jmethodID, ...);
    jlong(__attribute__((sysv_abi)) * CallLongMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jlong(__attribute__((sysv_abi)) * CallLongMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    jfloat(__attribute__((sysv_abi)) * CallFloatMethod)(JNIEnv *, jobject, jmethodID, ...);
    jfloat(__attribute__((sysv_abi)) * CallFloatMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jfloat(__attribute__((sysv_abi)) * CallFloatMethodA)(JNIEnv *, jobject, jmethodID,
                                                         const void *);
    jdouble(__attribute__((sysv_abi)) * CallDoubleMethod)(JNIEnv *, jobject, jmethodID, ...);
    jdouble(__attribute__((sysv_abi)) * CallDoubleMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    jdouble(__attribute__((sysv_abi)) * CallDoubleMethodA)(JNIEnv *, jobject, jmethodID,
                                                           const void *);
    void(__attribute__((sysv_abi)) * CallVoidMethod)(JNIEnv *, jobject, jmethodID, ...);
    void(__attribute__((sysv_abi)) * CallVoidMethodV)(JNIEnv *, jobject, jmethodID, va_list);
    void(__attribute__((sysv_abi)) * CallVoidMethodA)(JNIEnv *, jobject, jmethodID, const void *);
    void *nonvirtual_methods[27];
    jfieldID(__attribute__((sysv_abi)) * GetFieldID)(JNIEnv *, jclass, const char *, const char *);
    void *field_get_set[18];
    jmethodID(__attribute__((sysv_abi)) * GetStaticMethodID)(JNIEnv *, jclass, const char *,
                                                             const char *);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethod)(JNIEnv *, jclass, jmethodID, ...);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethodV)(JNIEnv *, jclass, jmethodID,
                                                                 va_list);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethodA)(JNIEnv *, jclass, jmethodID,
                                                                 const void *);
    void *static_methods[26];
    void *static_fields[19];
    jstring(__attribute__((sysv_abi)) * NewString)(JNIEnv *, const jchar *, jsize);
    jsize(__attribute__((sysv_abi)) * GetStringLength)(JNIEnv *, jstring);
    const jchar *(__attribute__((sysv_abi)) * GetStringChars)(JNIEnv *, jstring, jboolean *);
    void(__attribute__((sysv_abi)) * ReleaseStringChars)(JNIEnv *, jstring, const jchar *);
    jstring(__attribute__((sysv_abi)) * NewStringUTF)(JNIEnv *, const char *);
    jsize(__attribute__((sysv_abi)) * GetStringUTFLength)(JNIEnv *, jstring);
    const char *(__attribute__((sysv_abi)) * GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
    void(__attribute__((sysv_abi)) * ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);
    jsize(__attribute__((sysv_abi)) * GetArrayLength)(JNIEnv *, jarray);
    void *array_methods[32];
    jint(__attribute__((sysv_abi)) * RegisterNatives)(JNIEnv *, jclass, const void *, jint);
    jint(__attribute__((sysv_abi)) * UnregisterNatives)(JNIEnv *, jclass);
    jint(__attribute__((sysv_abi)) * MonitorEnter)(JNIEnv *, jobject);
    jint(__attribute__((sysv_abi)) * MonitorExit)(JNIEnv *, jobject);
    jint(__attribute__((sysv_abi)) * GetJavaVM)(JNIEnv *, JavaVM **);
    void *other_methods[6];
    jboolean(__attribute__((sysv_abi)) * ExceptionCheck)(JNIEnv *);
    void *nio_methods[3];
};

struct JavaVM {
    const JNIInvokeInterface *functions;
};

struct JNIEnv {
    const JNINativeInterface *functions;
};

class JniEnvironment {
  public:
    static JniEnvironment &Instance() noexcept;

    void Initialize(std::string_view package_name, std::string_view files_dir,
                    std::string_view cache_dir);
    void Reset();

    [[nodiscard]] JavaVM *GetJavaVM() noexcept { return &vm_; }
    [[nodiscard]] JNIEnv *GetJNIEnv() noexcept { return &env_; }

    jstring CreateString(std::string_view str);
    const char *GetStringChars(jstring str) const;

    [[nodiscard]] const std::string &GetPackageName() const noexcept { return package_name_; }
    [[nodiscard]] const std::string &GetFilesDir() const noexcept { return files_dir_; }
    [[nodiscard]] const std::string &GetCacheDir() const noexcept { return cache_dir_; }

  private:
    JniEnvironment();
    ~JniEnvironment() = default;

    void SetupInterfaceTables();

    JavaVM vm_{};
    JNIEnv env_{};

    std::unique_ptr<JNIInvokeInterface> vm_functions_{};
    std::unique_ptr<JNINativeInterface> env_functions_{};

    std::string package_name_{"com.portage.app"};
    std::string files_dir_{"/data/data/com.portage.app/files"};
    std::string cache_dir_{"/data/data/com.portage.app/cache"};

    std::vector<std::string> string_table_{};
};

} // namespace portage::package
