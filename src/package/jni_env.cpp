#include "jni_env.hpp"

#include <cstdarg>
#include <cstring>
#include <iostream>

namespace portage::package {

static __attribute__((sysv_abi)) jint Vm_DestroyJavaVM(JavaVM *vm)
{
    (void)vm;
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Vm_AttachCurrentThread(JavaVM *vm, void **penv, void *args)
{
    (void)vm;
    (void)args;
    if (penv)
        *penv = JniEnvironment::Instance().GetJNIEnv();
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Vm_DetachCurrentThread(JavaVM *vm)
{
    (void)vm;
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Vm_GetEnv(JavaVM *vm, void **penv, jint version)
{
    (void)vm;
    (void)version;
    if (penv)
        *penv = JniEnvironment::Instance().GetJNIEnv();
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Vm_AttachCurrentThreadAsDaemon(JavaVM *vm, void **penv,
                                                                     void *args)
{
    return Vm_AttachCurrentThread(vm, penv, args);
}

static __attribute__((sysv_abi)) jint Env_GetVersion(JNIEnv *) { return JNI_VERSION_1_6; }
static __attribute__((sysv_abi)) jclass Env_GetObjectClass(JNIEnv *, jobject obj)
{
    if (!obj)
        return nullptr;
    return reinterpret_cast<jclass>(obj);
}
static __attribute__((sysv_abi)) jclass Env_FindClass(JNIEnv *, const char *name)
{
    if (!name)
        return nullptr;
    return const_cast<char *>(name);
}
static __attribute__((sysv_abi)) jmethodID Env_GetMethodID(JNIEnv *, jclass, const char *name,
                                                           const char *)
{
    if (!name)
        return nullptr;
    return const_cast<char *>(name);
}
static __attribute__((sysv_abi)) jmethodID Env_GetStaticMethodID(JNIEnv *, jclass, const char *name,
                                                                 const char *)
{
    if (!name)
        return nullptr;
    return const_cast<char *>(name);
}
static __attribute__((sysv_abi)) jfieldID Env_GetFieldID(JNIEnv *, jclass, const char *name,
                                                         const char *)
{
    if (!name)
        return nullptr;
    return const_cast<char *>(name);
}
static __attribute__((sysv_abi)) jobject Env_CallObjectMethod(JNIEnv *, jobject, jmethodID methodID,
                                                              ...)
{
    if (!methodID)
        return nullptr;
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "getPackageName") == 0) {
        return JniEnvironment::Instance().CreateString(JniEnvironment::Instance().GetPackageName());
    }
    if (std::strcmp(mname, "getFilesDir") == 0 || std::strcmp(mname, "getAbsolutePath") == 0) {
        return JniEnvironment::Instance().CreateString(JniEnvironment::Instance().GetFilesDir());
    }
    if (std::strcmp(mname, "getCacheDir") == 0) {
        return JniEnvironment::Instance().CreateString(JniEnvironment::Instance().GetCacheDir());
    }
    return nullptr;
}
static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethod(JNIEnv *, jclass,
                                                                    jmethodID methodID, ...)
{
    if (!methodID)
        return nullptr;
    return nullptr;
}
static __attribute__((sysv_abi)) void Env_CallVoidMethod(JNIEnv *, jobject, jmethodID, ...) {}
static __attribute__((sysv_abi)) jboolean Env_CallBooleanMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 1;
}
static __attribute__((sysv_abi)) jint Env_CallIntMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jstring Env_NewStringUTF(JNIEnv *, const char *bytes)
{
    if (!bytes)
        return nullptr;
    return JniEnvironment::Instance().CreateString(bytes);
}
static __attribute__((sysv_abi)) jsize Env_GetStringUTFLength(JNIEnv *, jstring str)
{
    if (!str)
        return 0;
    const char *s = JniEnvironment::Instance().GetStringChars(str);
    return s ? static_cast<jsize>(std::strlen(s)) : 0;
}
static __attribute__((sysv_abi)) const char *Env_GetStringUTFChars(JNIEnv *, jstring str,
                                                                   jboolean *isCopy)
{
    if (isCopy)
        *isCopy = 0;
    return JniEnvironment::Instance().GetStringChars(str);
}
static __attribute__((sysv_abi)) void Env_ReleaseStringUTFChars(JNIEnv *, jstring, const char *) {}

static __attribute__((sysv_abi)) jobject Env_NewGlobalRef(JNIEnv *, jobject obj) { return obj; }
static __attribute__((sysv_abi)) void Env_DeleteGlobalRef(JNIEnv *, jobject) {}
static __attribute__((sysv_abi)) void Env_DeleteLocalRef(JNIEnv *, jobject) {}
static __attribute__((sysv_abi)) jobject Env_NewLocalRef(JNIEnv *, jobject obj) { return obj; }
static __attribute__((sysv_abi)) jboolean Env_IsSameObject(JNIEnv *, jobject o1, jobject o2)
{
    return o1 == o2;
}

static __attribute__((sysv_abi)) void Env_ExceptionClear(JNIEnv *) {}
static __attribute__((sysv_abi)) jboolean Env_ExceptionCheck(JNIEnv *) { return 0; }
static __attribute__((sysv_abi)) jthrowable Env_ExceptionOccurred(JNIEnv *) { return nullptr; }
static __attribute__((sysv_abi)) void Env_ExceptionDescribe(JNIEnv *) {}
static __attribute__((sysv_abi)) jint Env_Throw(JNIEnv *, jthrowable) { return JNI_OK; }

static __attribute__((sysv_abi)) jint Env_GetJavaVM(JNIEnv *, JavaVM **vm)
{
    if (vm)
        *vm = JniEnvironment::Instance().GetJavaVM();
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Env_RegisterNatives(JNIEnv *, jclass, const void *, jint)
{
    return JNI_OK;
}
static __attribute__((sysv_abi)) jint Env_UnregisterNatives(JNIEnv *, jclass) { return JNI_OK; }

JniEnvironment &JniEnvironment::Instance() noexcept
{
    static JniEnvironment instance;
    return instance;
}

JniEnvironment::JniEnvironment() { SetupInterfaceTables(); }

void JniEnvironment::Initialize(std::string_view package_name, std::string_view files_dir,
                                std::string_view cache_dir)
{
    package_name_ = std::string(package_name);
    files_dir_ = std::string(files_dir);
    cache_dir_ = std::string(cache_dir);
}

void JniEnvironment::Reset() { string_table_.clear(); }

void JniEnvironment::SetupInterfaceTables()
{
    vm_functions_ = std::make_unique<JNIInvokeInterface>();
    std::memset(vm_functions_.get(), 0, sizeof(JNIInvokeInterface));
    vm_functions_->DestroyJavaVM = Vm_DestroyJavaVM;
    vm_functions_->AttachCurrentThread = Vm_AttachCurrentThread;
    vm_functions_->DetachCurrentThread = Vm_DetachCurrentThread;
    vm_functions_->GetEnv = Vm_GetEnv;
    vm_functions_->AttachCurrentThreadAsDaemon = Vm_AttachCurrentThreadAsDaemon;
    vm_.functions = vm_functions_.get();

    env_functions_ = std::make_unique<JNINativeInterface>();
    std::memset(env_functions_.get(), 0, sizeof(JNINativeInterface));
    env_functions_->GetVersion = Env_GetVersion;
    env_functions_->GetObjectClass = Env_GetObjectClass;
    env_functions_->FindClass = Env_FindClass;
    env_functions_->GetMethodID = Env_GetMethodID;
    env_functions_->GetStaticMethodID = Env_GetStaticMethodID;
    env_functions_->GetFieldID = Env_GetFieldID;
    env_functions_->CallObjectMethod = Env_CallObjectMethod;
    env_functions_->CallStaticObjectMethod = Env_CallStaticObjectMethod;
    env_functions_->CallVoidMethod = Env_CallVoidMethod;
    env_functions_->CallBooleanMethod = Env_CallBooleanMethod;
    env_functions_->CallIntMethod = Env_CallIntMethod;
    env_functions_->NewStringUTF = Env_NewStringUTF;
    env_functions_->GetStringUTFLength = Env_GetStringUTFLength;
    env_functions_->GetStringUTFChars = Env_GetStringUTFChars;
    env_functions_->ReleaseStringUTFChars = Env_ReleaseStringUTFChars;
    env_functions_->NewGlobalRef = Env_NewGlobalRef;
    env_functions_->DeleteGlobalRef = Env_DeleteGlobalRef;
    env_functions_->DeleteLocalRef = Env_DeleteLocalRef;
    env_functions_->NewLocalRef = Env_NewLocalRef;
    env_functions_->IsSameObject = Env_IsSameObject;
    env_functions_->ExceptionClear = Env_ExceptionClear;
    env_functions_->ExceptionCheck = Env_ExceptionCheck;
    env_functions_->ExceptionOccurred = Env_ExceptionOccurred;
    env_functions_->ExceptionDescribe = Env_ExceptionDescribe;
    env_functions_->Throw = Env_Throw;
    env_functions_->GetJavaVM = Env_GetJavaVM;
    env_functions_->RegisterNatives = Env_RegisterNatives;
    env_functions_->UnregisterNatives = Env_UnregisterNatives;
    env_.functions = env_functions_.get();
}

jstring JniEnvironment::CreateString(std::string_view str)
{
    string_table_.emplace_back(str);
    return const_cast<char *>(string_table_.back().c_str());
}

const char *JniEnvironment::GetStringChars(jstring str) const
{
    return reinterpret_cast<const char *>(str);
}

} // namespace portage::package
