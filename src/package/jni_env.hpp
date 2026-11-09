#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

constexpr int JNI_FALSE = 0;
constexpr int JNI_TRUE = 1;

constexpr int JNI_COMMIT = 1;
constexpr int JNI_ABORT = 2;

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
using jobjectArray = jobject;
using jbooleanArray = jobject;
using jbyteArray = jobject;
using jcharArray = jobject;
using jshortArray = jobject;
using jintArray = jobject;
using jlongArray = jobject;
using jfloatArray = jobject;
using jdoubleArray = jobject;
using jthrowable = jobject;
using jweak = jobject;
using jmethodID = void *;
using jfieldID = void *;

struct JNINativeMethod {
    const char *name;
    const char *signature;
    void *fnPtr;
};

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

    // Call<Type>Method
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

    // CallNonvirtual<Type>Method (27 pointers)
    void *nonvirtual_methods[27];

    // Field accessors
    jfieldID(__attribute__((sysv_abi)) * GetFieldID)(JNIEnv *, jclass, const char *, const char *);
    jobject(__attribute__((sysv_abi)) * GetObjectField)(JNIEnv *, jobject, jfieldID);
    jboolean(__attribute__((sysv_abi)) * GetBooleanField)(JNIEnv *, jobject, jfieldID);
    jbyte(__attribute__((sysv_abi)) * GetByteField)(JNIEnv *, jobject, jfieldID);
    jchar(__attribute__((sysv_abi)) * GetCharField)(JNIEnv *, jobject, jfieldID);
    jshort(__attribute__((sysv_abi)) * GetShortField)(JNIEnv *, jobject, jfieldID);
    jint(__attribute__((sysv_abi)) * GetIntField)(JNIEnv *, jobject, jfieldID);
    jlong(__attribute__((sysv_abi)) * GetLongField)(JNIEnv *, jobject, jfieldID);
    jfloat(__attribute__((sysv_abi)) * GetFloatField)(JNIEnv *, jobject, jfieldID);
    jdouble(__attribute__((sysv_abi)) * GetDoubleField)(JNIEnv *, jobject, jfieldID);
    void(__attribute__((sysv_abi)) * SetObjectField)(JNIEnv *, jobject, jfieldID, jobject);
    void(__attribute__((sysv_abi)) * SetBooleanField)(JNIEnv *, jobject, jfieldID, jboolean);
    void(__attribute__((sysv_abi)) * SetByteField)(JNIEnv *, jobject, jfieldID, jbyte);
    void(__attribute__((sysv_abi)) * SetCharField)(JNIEnv *, jobject, jfieldID, jchar);
    void(__attribute__((sysv_abi)) * SetShortField)(JNIEnv *, jobject, jfieldID, jshort);
    void(__attribute__((sysv_abi)) * SetIntField)(JNIEnv *, jobject, jfieldID, jint);
    void(__attribute__((sysv_abi)) * SetLongField)(JNIEnv *, jobject, jfieldID, jlong);
    void(__attribute__((sysv_abi)) * SetFloatField)(JNIEnv *, jobject, jfieldID, jfloat);
    void(__attribute__((sysv_abi)) * SetDoubleField)(JNIEnv *, jobject, jfieldID, jdouble);

    // Static methods
    jmethodID(__attribute__((sysv_abi)) * GetStaticMethodID)(JNIEnv *, jclass, const char *,
                                                             const char *);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethod)(JNIEnv *, jclass, jmethodID, ...);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethodV)(JNIEnv *, jclass, jmethodID,
                                                                 va_list);
    jobject(__attribute__((sysv_abi)) * CallStaticObjectMethodA)(JNIEnv *, jclass, jmethodID,
                                                                 const void *);
    jboolean(__attribute__((sysv_abi)) * CallStaticBooleanMethod)(JNIEnv *, jclass, jmethodID, ...);
    jboolean(__attribute__((sysv_abi)) * CallStaticBooleanMethodV)(JNIEnv *, jclass, jmethodID,
                                                                   va_list);
    jboolean(__attribute__((sysv_abi)) * CallStaticBooleanMethodA)(JNIEnv *, jclass, jmethodID,
                                                                   const void *);
    jbyte(__attribute__((sysv_abi)) * CallStaticByteMethod)(JNIEnv *, jclass, jmethodID, ...);
    jbyte(__attribute__((sysv_abi)) * CallStaticByteMethodV)(JNIEnv *, jclass, jmethodID, va_list);
    jbyte(__attribute__((sysv_abi)) * CallStaticByteMethodA)(JNIEnv *, jclass, jmethodID,
                                                             const void *);
    jchar(__attribute__((sysv_abi)) * CallStaticCharMethod)(JNIEnv *, jclass, jmethodID, ...);
    jchar(__attribute__((sysv_abi)) * CallStaticCharMethodV)(JNIEnv *, jclass, jmethodID, va_list);
    jchar(__attribute__((sysv_abi)) * CallStaticCharMethodA)(JNIEnv *, jclass, jmethodID,
                                                             const void *);
    jshort(__attribute__((sysv_abi)) * CallStaticShortMethod)(JNIEnv *, jclass, jmethodID, ...);
    jshort(__attribute__((sysv_abi)) * CallStaticShortMethodV)(JNIEnv *, jclass, jmethodID,
                                                               va_list);
    jshort(__attribute__((sysv_abi)) * CallStaticShortMethodA)(JNIEnv *, jclass, jmethodID,
                                                               const void *);
    jint(__attribute__((sysv_abi)) * CallStaticIntMethod)(JNIEnv *, jclass, jmethodID, ...);
    jint(__attribute__((sysv_abi)) * CallStaticIntMethodV)(JNIEnv *, jclass, jmethodID, va_list);
    jint(__attribute__((sysv_abi)) * CallStaticIntMethodA)(JNIEnv *, jclass, jmethodID,
                                                           const void *);
    jlong(__attribute__((sysv_abi)) * CallStaticLongMethod)(JNIEnv *, jclass, jmethodID, ...);
    jlong(__attribute__((sysv_abi)) * CallStaticLongMethodV)(JNIEnv *, jclass, jmethodID, va_list);
    jlong(__attribute__((sysv_abi)) * CallStaticLongMethodA)(JNIEnv *, jclass, jmethodID,
                                                             const void *);
    jfloat(__attribute__((sysv_abi)) * CallStaticFloatMethod)(JNIEnv *, jclass, jmethodID, ...);
    jfloat(__attribute__((sysv_abi)) * CallStaticFloatMethodV)(JNIEnv *, jclass, jmethodID,
                                                               va_list);
    jfloat(__attribute__((sysv_abi)) * CallStaticFloatMethodA)(JNIEnv *, jclass, jmethodID,
                                                               const void *);
    jdouble(__attribute__((sysv_abi)) * CallStaticDoubleMethod)(JNIEnv *, jclass, jmethodID, ...);
    jdouble(__attribute__((sysv_abi)) * CallStaticDoubleMethodV)(JNIEnv *, jclass, jmethodID,
                                                                 va_list);
    jdouble(__attribute__((sysv_abi)) * CallStaticDoubleMethodA)(JNIEnv *, jclass, jmethodID,
                                                                 const void *);
    void(__attribute__((sysv_abi)) * CallStaticVoidMethod)(JNIEnv *, jclass, jmethodID, ...);
    void(__attribute__((sysv_abi)) * CallStaticVoidMethodV)(JNIEnv *, jclass, jmethodID, va_list);
    void(__attribute__((sysv_abi)) * CallStaticVoidMethodA)(JNIEnv *, jclass, jmethodID,
                                                            const void *);

    // Static fields
    jfieldID(__attribute__((sysv_abi)) * GetStaticFieldID)(JNIEnv *, jclass, const char *,
                                                           const char *);
    jobject(__attribute__((sysv_abi)) * GetStaticObjectField)(JNIEnv *, jclass, jfieldID);
    jboolean(__attribute__((sysv_abi)) * GetStaticBooleanField)(JNIEnv *, jclass, jfieldID);
    jbyte(__attribute__((sysv_abi)) * GetStaticByteField)(JNIEnv *, jclass, jfieldID);
    jchar(__attribute__((sysv_abi)) * GetStaticCharField)(JNIEnv *, jclass, jfieldID);
    jshort(__attribute__((sysv_abi)) * GetStaticShortField)(JNIEnv *, jclass, jfieldID);
    jint(__attribute__((sysv_abi)) * GetStaticIntField)(JNIEnv *, jclass, jfieldID);
    jlong(__attribute__((sysv_abi)) * GetStaticLongField)(JNIEnv *, jclass, jfieldID);
    jfloat(__attribute__((sysv_abi)) * GetStaticFloatField)(JNIEnv *, jclass, jfieldID);
    jdouble(__attribute__((sysv_abi)) * GetStaticDoubleField)(JNIEnv *, jclass, jfieldID);
    void(__attribute__((sysv_abi)) * SetStaticObjectField)(JNIEnv *, jclass, jfieldID, jobject);
    void(__attribute__((sysv_abi)) * SetStaticBooleanField)(JNIEnv *, jclass, jfieldID, jboolean);
    void(__attribute__((sysv_abi)) * SetStaticByteField)(JNIEnv *, jclass, jfieldID, jbyte);
    void(__attribute__((sysv_abi)) * SetStaticCharField)(JNIEnv *, jclass, jfieldID, jchar);
    void(__attribute__((sysv_abi)) * SetStaticShortField)(JNIEnv *, jclass, jfieldID, jshort);
    void(__attribute__((sysv_abi)) * SetStaticIntField)(JNIEnv *, jclass, jfieldID, jint);
    void(__attribute__((sysv_abi)) * SetStaticLongField)(JNIEnv *, jclass, jfieldID, jlong);
    void(__attribute__((sysv_abi)) * SetStaticFloatField)(JNIEnv *, jclass, jfieldID, jfloat);
    void(__attribute__((sysv_abi)) * SetStaticDoubleField)(JNIEnv *, jclass, jfieldID, jdouble);

    // Strings
    jstring(__attribute__((sysv_abi)) * NewString)(JNIEnv *, const jchar *, jsize);
    jsize(__attribute__((sysv_abi)) * GetStringLength)(JNIEnv *, jstring);
    const jchar *(__attribute__((sysv_abi)) * GetStringChars)(JNIEnv *, jstring, jboolean *);
    void(__attribute__((sysv_abi)) * ReleaseStringChars)(JNIEnv *, jstring, const jchar *);
    jstring(__attribute__((sysv_abi)) * NewStringUTF)(JNIEnv *, const char *);
    jsize(__attribute__((sysv_abi)) * GetStringUTFLength)(JNIEnv *, jstring);
    const char *(__attribute__((sysv_abi)) * GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
    void(__attribute__((sysv_abi)) * ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);

    // Arrays
    jsize(__attribute__((sysv_abi)) * GetArrayLength)(JNIEnv *, jarray);
    jobjectArray(__attribute__((sysv_abi)) * NewObjectArray)(JNIEnv *, jsize, jclass, jobject);
    jobject(__attribute__((sysv_abi)) * GetObjectArrayElement)(JNIEnv *, jobjectArray, jsize);
    void(__attribute__((sysv_abi)) * SetObjectArrayElement)(JNIEnv *, jobjectArray, jsize, jobject);
    jbooleanArray(__attribute__((sysv_abi)) * NewBooleanArray)(JNIEnv *, jsize);
    jbyteArray(__attribute__((sysv_abi)) * NewByteArray)(JNIEnv *, jsize);
    jcharArray(__attribute__((sysv_abi)) * NewCharArray)(JNIEnv *, jsize);
    jshortArray(__attribute__((sysv_abi)) * NewShortArray)(JNIEnv *, jsize);
    jintArray(__attribute__((sysv_abi)) * NewIntArray)(JNIEnv *, jsize);
    jlongArray(__attribute__((sysv_abi)) * NewLongArray)(JNIEnv *, jsize);
    jfloatArray(__attribute__((sysv_abi)) * NewFloatArray)(JNIEnv *, jsize);
    jdoubleArray(__attribute__((sysv_abi)) * NewDoubleArray)(JNIEnv *, jsize);

    jboolean *(__attribute__((sysv_abi)) * GetBooleanArrayElements)(JNIEnv *, jbooleanArray,
                                                                    jboolean *);
    jbyte *(__attribute__((sysv_abi)) * GetByteArrayElements)(JNIEnv *, jbyteArray, jboolean *);
    jchar *(__attribute__((sysv_abi)) * GetCharArrayElements)(JNIEnv *, jcharArray, jboolean *);
    jshort *(__attribute__((sysv_abi)) * GetShortArrayElements)(JNIEnv *, jshortArray, jboolean *);
    jint *(__attribute__((sysv_abi)) * GetIntArrayElements)(JNIEnv *, jintArray, jboolean *);
    jlong *(__attribute__((sysv_abi)) * GetLongArrayElements)(JNIEnv *, jlongArray, jboolean *);
    jfloat *(__attribute__((sysv_abi)) * GetFloatArrayElements)(JNIEnv *, jfloatArray, jboolean *);
    jdouble *(__attribute__((sysv_abi)) * GetDoubleArrayElements)(JNIEnv *, jdoubleArray,
                                                                  jboolean *);

    void(__attribute__((sysv_abi)) * ReleaseBooleanArrayElements)(JNIEnv *, jbooleanArray,
                                                                  jboolean *, jint);
    void(__attribute__((sysv_abi)) * ReleaseByteArrayElements)(JNIEnv *, jbyteArray, jbyte *, jint);
    void(__attribute__((sysv_abi)) * ReleaseCharArrayElements)(JNIEnv *, jcharArray, jchar *, jint);
    void(__attribute__((sysv_abi)) * ReleaseShortArrayElements)(JNIEnv *, jshortArray, jshort *,
                                                                jint);
    void(__attribute__((sysv_abi)) * ReleaseIntArrayElements)(JNIEnv *, jintArray, jint *, jint);
    void(__attribute__((sysv_abi)) * ReleaseLongArrayElements)(JNIEnv *, jlongArray, jlong *, jint);
    void(__attribute__((sysv_abi)) * ReleaseFloatArrayElements)(JNIEnv *, jfloatArray, jfloat *,
                                                                jint);
    void(__attribute__((sysv_abi)) * ReleaseDoubleArrayElements)(JNIEnv *, jdoubleArray, jdouble *,
                                                                 jint);

    void(__attribute__((sysv_abi)) * GetBooleanArrayRegion)(JNIEnv *, jbooleanArray, jsize, jsize,
                                                            jboolean *);
    void(__attribute__((sysv_abi)) * GetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize,
                                                         jbyte *);
    void(__attribute__((sysv_abi)) * GetCharArrayRegion)(JNIEnv *, jcharArray, jsize, jsize,
                                                         jchar *);
    void(__attribute__((sysv_abi)) * GetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize,
                                                          jshort *);
    void(__attribute__((sysv_abi)) * GetIntArrayRegion)(JNIEnv *, jintArray, jsize, jsize, jint *);
    void(__attribute__((sysv_abi)) * GetLongArrayRegion)(JNIEnv *, jlongArray, jsize, jsize,
                                                         jlong *);
    void(__attribute__((sysv_abi)) * GetFloatArrayRegion)(JNIEnv *, jfloatArray, jsize, jsize,
                                                          jfloat *);
    void(__attribute__((sysv_abi)) * GetDoubleArrayRegion)(JNIEnv *, jdoubleArray, jsize, jsize,
                                                           jdouble *);

    void(__attribute__((sysv_abi)) * SetBooleanArrayRegion)(JNIEnv *, jbooleanArray, jsize, jsize,
                                                            const jboolean *);
    void(__attribute__((sysv_abi)) * SetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize,
                                                         const jbyte *);
    void(__attribute__((sysv_abi)) * SetCharArrayRegion)(JNIEnv *, jcharArray, jsize, jsize,
                                                         const jchar *);
    void(__attribute__((sysv_abi)) * SetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize,
                                                          const jshort *);
    void(__attribute__((sysv_abi)) * SetIntArrayRegion)(JNIEnv *, jintArray, jsize, jsize,
                                                        const jint *);
    void(__attribute__((sysv_abi)) * SetLongArrayRegion)(JNIEnv *, jlongArray, jsize, jsize,
                                                         const jlong *);
    void(__attribute__((sysv_abi)) * SetFloatArrayRegion)(JNIEnv *, jfloatArray, jsize, jsize,
                                                          const jfloat *);
    void(__attribute__((sysv_abi)) * SetDoubleArrayRegion)(JNIEnv *, jdoubleArray, jsize, jsize,
                                                           const jdouble *);

    // Registration & monitors
    jint(__attribute__((sysv_abi)) *
         RegisterNatives)(JNIEnv *, jclass, const JNINativeMethod *methods, jint nMethods);
    jint(__attribute__((sysv_abi)) * UnregisterNatives)(JNIEnv *, jclass);
    jint(__attribute__((sysv_abi)) * MonitorEnter)(JNIEnv *, jobject);
    jint(__attribute__((sysv_abi)) * MonitorExit)(JNIEnv *, jobject);
    jint(__attribute__((sysv_abi)) * GetJavaVM)(JNIEnv *, JavaVM **);

    void(__attribute__((sysv_abi)) * GetStringRegion)(JNIEnv *, jstring, jsize, jsize, jchar *);
    void(__attribute__((sysv_abi)) * GetStringUTFRegion)(JNIEnv *, jstring, jsize, jsize, char *);
    void *(__attribute__((sysv_abi)) * GetPrimitiveArrayCritical)(JNIEnv *, jarray, jboolean *);
    void(__attribute__((sysv_abi)) * ReleasePrimitiveArrayCritical)(JNIEnv *, jarray, void *, jint);
    const jchar *(__attribute__((sysv_abi)) * GetStringCritical)(JNIEnv *, jstring, jboolean *);
    void(__attribute__((sysv_abi)) * ReleaseStringCritical)(JNIEnv *, jstring, const jchar *);
    jweak(__attribute__((sysv_abi)) * NewWeakGlobalRef)(JNIEnv *, jobject);
    void(__attribute__((sysv_abi)) * DeleteWeakGlobalRef)(JNIEnv *, jweak);
    jboolean(__attribute__((sysv_abi)) * ExceptionCheck)(JNIEnv *);

    // Direct byte buffers (NIO)
    jobject(__attribute__((sysv_abi)) * NewDirectByteBuffer)(JNIEnv *, void *address,
                                                             jlong capacity);
    void *(__attribute__((sysv_abi)) * GetDirectBufferAddress)(JNIEnv *, jobject);
    jlong(__attribute__((sysv_abi)) * GetDirectBufferCapacity)(JNIEnv *, jobject);

    jint(__attribute__((sysv_abi)) * GetObjectRefType)(JNIEnv *, jobject);
};

struct JavaVM {
    const JNIInvokeInterface *functions;
};

struct JNIEnv {
    const JNINativeInterface *functions;
};

enum class JniObjectType {
    Generic,
    Class,
    String,
    DirectBuffer,
    ByteArray,
    IntArray,
    FloatArray,
    ObjectArray,
    Context,
    Activity,
    Resources,
    DisplayMetrics,
    Window,
    View,
    ClassLoader,
    AssetManager,
    Bitmap,
    BitmapConfig,
};

struct JniSyntheticObject {
    JniObjectType type{JniObjectType::Generic};
    std::string class_name;
    std::unordered_map<std::string, std::string> string_fields;
    std::unordered_map<std::string, int32_t> int_fields;
    std::unordered_map<std::string, int64_t> long_fields;
    std::unordered_map<std::string, float> float_fields;
    std::unordered_map<std::string, double> double_fields;
    std::unordered_map<std::string, uint8_t> bool_fields;
    std::unordered_map<std::string, jobject> object_fields;
    std::unordered_map<std::string, std::vector<uint8_t>> binary_fields;

    void *buffer_address{nullptr};
    jlong buffer_capacity{0};
    void *native_ptr{nullptr};

    std::vector<int8_t> byte_data;
    std::vector<int32_t> int_data;
    std::vector<float> float_data;
    std::vector<jobject> object_data;

    uint32_t bitmap_width{0};
    uint32_t bitmap_height{0};
    uint32_t bitmap_stride{0};
    int32_t bitmap_format{1}; // ANDROID_BITMAP_FORMAT_RGBA_8888
    uint32_t bitmap_flags{0};
    std::vector<uint8_t> bitmap_pixels{};
    bool bitmap_locked{false};
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

    jobject RegisterObject(std::shared_ptr<JniSyntheticObject> obj);
    std::shared_ptr<JniSyntheticObject> LookupObject(jobject obj) const;

    jobject CreateDirectByteBuffer(void *address, jlong capacity);
    jobject CreateBitmap(uint32_t width, uint32_t height, int32_t format = 1);

    void RegisterNativeMethod(std::string_view className, std::string_view methodName,
                              std::string_view sig, void *fnPtr);
    void *LookupNativeMethod(std::string_view className, std::string_view methodName,
                             std::string_view sig) const;

    void SetDisplayMetrics(int32_t width, int32_t height, float density = 2.0f,
                           int32_t densityDpi = 320);

    [[nodiscard]] const std::string &GetPackageName() const noexcept { return package_name_; }
    [[nodiscard]] const std::string &GetFilesDir() const noexcept { return files_dir_; }
    [[nodiscard]] const std::string &GetCacheDir() const noexcept { return cache_dir_; }

    [[nodiscard]] jobject GetContextObject() const noexcept { return context_obj_; }
    [[nodiscard]] jobject GetResourcesObject() const noexcept { return resources_obj_; }
    [[nodiscard]] jobject GetDisplayMetricsObject() const noexcept { return display_metrics_obj_; }
    [[nodiscard]] jobject GetWindowObject() const noexcept { return window_obj_; }
    [[nodiscard]] jobject GetDecorViewObject() const noexcept { return decor_view_obj_; }
    [[nodiscard]] jobject GetClassLoaderObject() const noexcept { return class_loader_obj_; }

    [[nodiscard]] int32_t GetDisplayWidth() const noexcept { return display_width_; }
    [[nodiscard]] int32_t GetDisplayHeight() const noexcept { return display_height_; }
    [[nodiscard]] float GetDisplayDensity() const noexcept { return display_density_; }
    [[nodiscard]] int32_t GetDisplayDensityDpi() const noexcept { return display_density_dpi_; }

    [[nodiscard]] jthrowable GetPendingException() const noexcept { return pending_exception_; }
    void SetPendingException(jthrowable ex) noexcept { pending_exception_ = ex; }
    void ClearPendingException() noexcept { pending_exception_ = nullptr; }

  private:
    JniEnvironment();
    ~JniEnvironment() = default;

    void SetupInterfaceTables();
    void SetupSyntheticHierarchy();

    JavaVM vm_{};
    JNIEnv env_{};

    std::unique_ptr<JNIInvokeInterface> vm_functions_{};
    std::unique_ptr<JNINativeInterface> env_functions_{};

    std::string package_name_{"com.portage.app"};
    std::string files_dir_{"/data/data/com.portage.app/files"};
    std::string cache_dir_{"/data/data/com.portage.app/cache"};

    int32_t display_width_{1280};
    int32_t display_height_{720};
    float display_density_{2.0f};
    int32_t display_density_dpi_{320};

    std::vector<std::string> string_table_{};
    std::vector<std::shared_ptr<JniSyntheticObject>> object_table_{};
    std::unordered_map<jobject, std::shared_ptr<JniSyntheticObject>> objects_by_handle_{};
    std::unordered_map<std::string, void *> registered_natives_{};

    uintptr_t next_handle_{0x1000};

    jobject context_obj_{nullptr};
    jobject resources_obj_{nullptr};
    jobject display_metrics_obj_{nullptr};
    jobject window_obj_{nullptr};
    jobject decor_view_obj_{nullptr};
    jobject class_loader_obj_{nullptr};
    jthrowable pending_exception_{nullptr};
};

} // namespace portage::package
