#include "jni_env.hpp"
#include "package_manager.hpp"
#include "resource_table.hpp"

#include <algorithm>
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
    if (penv) {
        *penv = JniEnvironment::Instance().GetJNIEnv();
    }
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
    if (penv) {
        *penv = JniEnvironment::Instance().GetJNIEnv();
    }
    return JNI_OK;
}

static __attribute__((sysv_abi)) jint Vm_AttachCurrentThreadAsDaemon(JavaVM *vm, void **penv,
                                                                     void *args)
{
    return Vm_AttachCurrentThread(vm, penv, args);
}

static __attribute__((sysv_abi)) jint Env_GetVersion(JNIEnv *) { return JNI_VERSION_1_6; }

static __attribute__((sysv_abi)) jclass Env_DefineClass(JNIEnv *, const char *name, jobject,
                                                        const jbyte *, jsize)
{
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jclass Env_FindClass(JNIEnv *, const char *name)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jmethodID Env_FromReflectedMethod(JNIEnv *, jobject)
{
    return nullptr;
}

static __attribute__((sysv_abi)) jfieldID Env_FromReflectedField(JNIEnv *, jobject)
{
    return nullptr;
}

static __attribute__((sysv_abi)) jobject Env_ToReflectedMethod(JNIEnv *, jclass, jmethodID,
                                                               jboolean)
{
    return nullptr;
}

static __attribute__((sysv_abi)) jclass Env_GetSuperclass(JNIEnv *, jclass) { return nullptr; }

static __attribute__((sysv_abi)) jboolean Env_IsAssignableFrom(JNIEnv *, jclass, jclass)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jobject Env_ToReflectedField(JNIEnv *, jclass, jfieldID, jboolean)
{
    return nullptr;
}

static __attribute__((sysv_abi)) jint Env_Throw(JNIEnv *, jthrowable obj)
{
    JniEnvironment::Instance().SetPendingException(obj);
    return JNI_OK;
}

static __attribute__((sysv_abi)) jint Env_ThrowNew(JNIEnv *, jclass clazz, const char *msg)
{
    auto ex = std::make_shared<JniSyntheticObject>();
    ex->type = JniObjectType::Generic;
    ex->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Throwable";
    if (msg) {
        ex->string_fields["detailMessage"] = msg;
    }
    jobject ex_handle = JniEnvironment::Instance().RegisterObject(ex);
    JniEnvironment::Instance().SetPendingException(ex_handle);
    return JNI_OK;
}

static __attribute__((sysv_abi)) jthrowable Env_ExceptionOccurred(JNIEnv *)
{
    return JniEnvironment::Instance().GetPendingException();
}

static __attribute__((sysv_abi)) void Env_ExceptionDescribe(JNIEnv *) {}

static __attribute__((sysv_abi)) void Env_ExceptionClear(JNIEnv *)
{
    JniEnvironment::Instance().ClearPendingException();
}

static __attribute__((sysv_abi)) void Env_FatalError(JNIEnv *, const char *) {}

static __attribute__((sysv_abi)) jint Env_PushLocalFrame(JNIEnv *, jint) { return JNI_OK; }

static __attribute__((sysv_abi)) jobject Env_PopLocalFrame(JNIEnv *, jobject result)
{
    return result;
}

static __attribute__((sysv_abi)) jobject Env_NewGlobalRef(JNIEnv *, jobject obj) { return obj; }

static __attribute__((sysv_abi)) void Env_DeleteGlobalRef(JNIEnv *, jobject) {}

static __attribute__((sysv_abi)) void Env_DeleteLocalRef(JNIEnv *, jobject) {}

static __attribute__((sysv_abi)) jboolean Env_IsSameObject(JNIEnv *, jobject o1, jobject o2)
{
    return o1 == o2 ? JNI_TRUE : JNI_FALSE;
}

static __attribute__((sysv_abi)) jobject Env_NewLocalRef(JNIEnv *, jobject obj) { return obj; }

static __attribute__((sysv_abi)) jint Env_EnsureLocalCapacity(JNIEnv *, jint) { return JNI_OK; }

static __attribute__((sysv_abi)) jobject Env_AllocObject(JNIEnv *, jclass clazz)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObject(JNIEnv *, jclass clazz, jmethodID, ...)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObjectV(JNIEnv *, jclass clazz, jmethodID, va_list)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObjectA(JNIEnv *, jclass clazz, jmethodID,
                                                        const void *)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jclass Env_GetObjectClass(JNIEnv *, jobject obj)
{
    if (!obj) {
        return nullptr;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (synth && !synth->class_name.empty()) {
        return const_cast<char *>(synth->class_name.c_str());
    }
    return reinterpret_cast<jclass>(obj);
}

static __attribute__((sysv_abi)) jboolean Env_IsInstanceOf(JNIEnv *, jobject, jclass)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jmethodID Env_GetMethodID(JNIEnv *, jclass, const char *name,
                                                           const char *)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jmethodID Env_GetStaticMethodID(JNIEnv *, jclass, const char *name,
                                                                 const char *)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jfieldID Env_GetFieldID(JNIEnv *, jclass, const char *name,
                                                         const char *)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jfieldID Env_GetStaticFieldID(JNIEnv *, jclass, const char *name,
                                                               const char *)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethod(JNIEnv *, jobject obj,
                                                              jmethodID methodID, ...)
{
    uintptr_t arg0 = 0;
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));

    if (!methodID) {
        return nullptr;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    auto &env = JniEnvironment::Instance();

    if (std::strcmp(mname, "getPackageName") == 0) {
        return env.CreateString(env.GetPackageName());
    }
    if (std::strcmp(mname, "getFilesDir") == 0 || std::strcmp(mname, "getAbsolutePath") == 0) {
        return env.CreateString(env.GetFilesDir());
    }
    if (std::strcmp(mname, "getCacheDir") == 0) {
        return env.CreateString(env.GetCacheDir());
    }
    if (std::strcmp(mname, "getExternalFilesDir") == 0) {
        return env.CreateString(env.GetFilesDir());
    }
    if (std::strcmp(mname, "getResources") == 0) {
        return env.GetResourcesObject();
    }
    if (std::strcmp(mname, "getDisplayMetrics") == 0) {
        return env.GetDisplayMetricsObject();
    }
    if (std::strcmp(mname, "getWindow") == 0) {
        return env.GetWindowObject();
    }
    if (std::strcmp(mname, "getDecorView") == 0) {
        return env.GetDecorViewObject();
    }
    if (std::strcmp(mname, "getClassLoader") == 0) {
        return env.GetClassLoaderObject();
    }
    if (std::strcmp(mname, "getAssets") == 0) {
        auto obj = std::make_shared<JniSyntheticObject>();
        obj->type = JniObjectType::AssetManager;
        obj->class_name = "android/content/res/AssetManager";
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (pkg && pkg->asset_manager) {
            obj->native_ptr = pkg->asset_manager;
        }
        return env.RegisterObject(obj);
    }
    if (std::strcmp(mname, "getConfig") == 0) {
        auto syn_obj = env.LookupObject(obj);
        auto cfg = std::make_shared<JniSyntheticObject>();
        cfg->type = JniObjectType::BitmapConfig;
        cfg->class_name = "android/graphics/Bitmap$Config";
        int32_t fmt =
            (syn_obj && syn_obj->type == JniObjectType::Bitmap) ? syn_obj->bitmap_format : 1;
        cfg->int_fields["format"] = fmt;
        cfg->string_fields["name"] = (fmt == 4) ? "RGB_565" : "ARGB_8888";
        return env.RegisterObject(cfg);
    }
    if (std::strcmp(mname, "getString") == 0) {
        uint32_t res_id = static_cast<uint32_t>(arg0);
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (pkg && pkg->resource_table) {
            auto str = pkg->resource_table->GetString(res_id);
            if (str.has_value()) {
                return env.CreateString(*str);
            }
        }
        return env.CreateString("");
    }
    if (std::strcmp(mname, "loadClass") == 0 || std::strcmp(mname, "findClass") == 0) {
        jstring class_name_str = reinterpret_cast<jstring>(arg0);
        const char *cname = env.GetStringChars(class_name_str);
        if (cname) {
            return const_cast<char *>(cname);
        }
        return const_cast<char *>("java/lang/Object");
    }

    return nullptr;
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethodV(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, va_list)
{
    return Env_CallObjectMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethodA(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, const void *)
{
    return Env_CallObjectMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jboolean Env_CallBooleanMethod(JNIEnv *, jobject,
                                                                jmethodID methodID, ...)
{
    if (!methodID) {
        return JNI_TRUE;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    uintptr_t arg0 = 0;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
#endif
    if (std::strcmp(mname, "getBoolean") == 0) {
        uint32_t res_id = static_cast<uint32_t>(arg0);
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (pkg && pkg->resource_table) {
            auto b = pkg->resource_table->GetBoolean(res_id);
            if (b.has_value()) {
                return *b ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jboolean Env_CallBooleanMethodV(JNIEnv *, jobject, jmethodID,
                                                                 va_list)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jboolean Env_CallBooleanMethodA(JNIEnv *, jobject, jmethodID,
                                                                 const void *)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jbyte Env_CallByteMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jbyte Env_CallByteMethodV(JNIEnv *, jobject, jmethodID, va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jbyte Env_CallByteMethodA(JNIEnv *, jobject, jmethodID,
                                                           const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallCharMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallCharMethodV(JNIEnv *, jobject, jmethodID, va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallCharMethodA(JNIEnv *, jobject, jmethodID,
                                                           const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallShortMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallShortMethodV(JNIEnv *, jobject, jmethodID, va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallShortMethodA(JNIEnv *, jobject, jmethodID,
                                                             const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallIntMethod(JNIEnv *, jobject obj, jmethodID methodID,
                                                        ...)
{
    uintptr_t arg0 = 0;
    uintptr_t arg1 = 0;
    uintptr_t arg2 = 0;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
    __asm__ volatile("movq %%r8, %0" : "=r"(arg1));
    __asm__ volatile("movq %%r9, %0" : "=r"(arg2));
#endif
    if (!methodID) {
        return 0;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (obj) {
        auto syn_obj = JniEnvironment::Instance().LookupObject(obj);
        if (syn_obj && syn_obj->type == JniObjectType::Bitmap) {
            if (std::strcmp(mname, "getWidth") == 0) {
                return static_cast<jint>(syn_obj->bitmap_width);
            }
            if (std::strcmp(mname, "getHeight") == 0) {
                return static_cast<jint>(syn_obj->bitmap_height);
            }
            if (std::strcmp(mname, "getByteCount") == 0 ||
                std::strcmp(mname, "getAllocationByteCount") == 0) {
                return static_cast<jint>(syn_obj->bitmap_stride * syn_obj->bitmap_height);
            }
        }
    }
    if (std::strcmp(mname, "getWidth") == 0) {
        return JniEnvironment::Instance().GetDisplayWidth();
    }
    if (std::strcmp(mname, "getHeight") == 0) {
        return JniEnvironment::Instance().GetDisplayHeight();
    }
    if (std::strcmp(mname, "getInteger") == 0) {
        uint32_t res_id = static_cast<uint32_t>(arg0);
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (pkg && pkg->resource_table) {
            auto val = pkg->resource_table->GetInteger(res_id);
            if (val.has_value()) {
                return *val;
            }
        }
        return 0;
    }
    if (std::strcmp(mname, "getIdentifier") == 0) {
        auto &env = JniEnvironment::Instance();
        const char *name_str = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        const char *type_str = env.GetStringChars(reinterpret_cast<jstring>(arg1));
        const char *pkg_str = env.GetStringChars(reinterpret_cast<jstring>(arg2));
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (pkg && pkg->resource_table && name_str) {
            return static_cast<jint>(pkg->resource_table->GetIdentifier(
                name_str, type_str ? type_str : "", pkg_str ? pkg_str : ""));
        }
        return 0;
    }
    if (obj) {
        auto synth = JniEnvironment::Instance().LookupObject(obj);
        if (synth) {
            auto it = synth->int_fields.find(mname);
            if (it != synth->int_fields.end()) {
                return it->second;
            }
        }
    }
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallIntMethodV(JNIEnv *env, jobject obj,
                                                         jmethodID methodID, va_list)
{
    return Env_CallIntMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jint Env_CallIntMethodA(JNIEnv *env, jobject obj,
                                                         jmethodID methodID, const void *)
{
    return Env_CallIntMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jlong Env_CallLongMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallLongMethodV(JNIEnv *, jobject, jmethodID, va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallLongMethodA(JNIEnv *, jobject, jmethodID,
                                                           const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethodV(JNIEnv *, jobject, jmethodID, va_list)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethodA(JNIEnv *, jobject, jmethodID,
                                                             const void *)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethod(JNIEnv *, jobject, jmethodID, ...)
{
    return 0.0;
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethodV(JNIEnv *, jobject, jmethodID,
                                                               va_list)
{
    return 0.0;
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethodA(JNIEnv *, jobject, jmethodID,
                                                               const void *)
{
    return 0.0;
}

static __attribute__((sysv_abi)) void Env_CallVoidMethod(JNIEnv *, jobject, jmethodID, ...) {}

static __attribute__((sysv_abi)) void Env_CallVoidMethodV(JNIEnv *, jobject, jmethodID, va_list) {}

static __attribute__((sysv_abi)) void Env_CallVoidMethodA(JNIEnv *, jobject, jmethodID,
                                                          const void *)
{
}

// Static method calls
static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethod(JNIEnv *, jclass,
                                                                    jmethodID methodID, ...)
{
    uintptr_t arg0 = 0;
    uintptr_t arg1 = 0;
    uintptr_t arg2 = 0;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
    __asm__ volatile("movq %%r8, %0" : "=r"(arg1));
    __asm__ volatile("movq %%r9, %0" : "=r"(arg2));
#endif

    if (!methodID) {
        return nullptr;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "createBitmap") == 0) {
        uint32_t w = static_cast<uint32_t>(arg0 > 0 ? arg0 : 64);
        uint32_t h = static_cast<uint32_t>(arg1 > 0 ? arg1 : 64);
        int32_t fmt = 1;
        if (arg2 != 0) {
            auto cfg_obj = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg2));
            if (cfg_obj && cfg_obj->int_fields.find("format") != cfg_obj->int_fields.end()) {
                fmt = cfg_obj->int_fields["format"];
            }
        }
        return JniEnvironment::Instance().CreateBitmap(w, h, fmt);
    }
    return nullptr;
}

static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethodV(JNIEnv *env, jclass clazz,
                                                                     jmethodID methodID, va_list)
{
    return Env_CallStaticObjectMethod(env, clazz, methodID);
}

static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethodA(JNIEnv *env, jclass clazz,
                                                                     jmethodID methodID,
                                                                     const void *)
{
    return Env_CallStaticObjectMethod(env, clazz, methodID);
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethod(JNIEnv *, jclass, jmethodID,
                                                                      ...)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethodV(JNIEnv *, jclass, jmethodID,
                                                                       va_list)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethodA(JNIEnv *, jclass, jmethodID,
                                                                       const void *)
{
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jbyte Env_CallStaticByteMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jbyte Env_CallStaticByteMethodV(JNIEnv *, jclass, jmethodID,
                                                                 va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jbyte Env_CallStaticByteMethodA(JNIEnv *, jclass, jmethodID,
                                                                 const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallStaticCharMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallStaticCharMethodV(JNIEnv *, jclass, jmethodID,
                                                                 va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_CallStaticCharMethodA(JNIEnv *, jclass, jmethodID,
                                                                 const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallStaticShortMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallStaticShortMethodV(JNIEnv *, jclass, jmethodID,
                                                                   va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_CallStaticShortMethodA(JNIEnv *, jclass, jmethodID,
                                                                   const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethodV(JNIEnv *, jclass, jmethodID, va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethodA(JNIEnv *, jclass, jmethodID,
                                                               const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallStaticLongMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallStaticLongMethodV(JNIEnv *, jclass, jmethodID,
                                                                 va_list)
{
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallStaticLongMethodA(JNIEnv *, jclass, jmethodID,
                                                                 const void *)
{
    return 0;
}

static __attribute__((sysv_abi)) jfloat Env_CallStaticFloatMethod(JNIEnv *, jclass, jmethodID, ...)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jfloat Env_CallStaticFloatMethodV(JNIEnv *, jclass, jmethodID,
                                                                   va_list)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jfloat Env_CallStaticFloatMethodA(JNIEnv *, jclass, jmethodID,
                                                                   const void *)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jdouble Env_CallStaticDoubleMethod(JNIEnv *, jclass, jmethodID,
                                                                    ...)
{
    return 0.0;
}

static __attribute__((sysv_abi)) jdouble Env_CallStaticDoubleMethodV(JNIEnv *, jclass, jmethodID,
                                                                     va_list)
{
    return 0.0;
}

static __attribute__((sysv_abi)) jdouble Env_CallStaticDoubleMethodA(JNIEnv *, jclass, jmethodID,
                                                                     const void *)
{
    return 0.0;
}

static __attribute__((sysv_abi)) void Env_CallStaticVoidMethod(JNIEnv *, jclass, jmethodID, ...) {}

static __attribute__((sysv_abi)) void Env_CallStaticVoidMethodV(JNIEnv *, jclass, jmethodID,
                                                                va_list)
{
}

static __attribute__((sysv_abi)) void Env_CallStaticVoidMethodA(JNIEnv *, jclass, jmethodID,
                                                                const void *)
{
}

// Instance Field Operations
static __attribute__((sysv_abi)) jobject Env_GetObjectField(JNIEnv *, jobject obj, jfieldID fieldID)
{
    if (!obj || !fieldID) {
        return nullptr;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (!synth) {
        return nullptr;
    }
    const char *fname = reinterpret_cast<const char *>(fieldID);
    auto it = synth->object_fields.find(fname);
    return (it != synth->object_fields.end()) ? it->second : nullptr;
}

static __attribute__((sysv_abi)) jboolean Env_GetBooleanField(JNIEnv *, jobject obj,
                                                              jfieldID fieldID)
{
    if (!obj || !fieldID) {
        return JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (!synth) {
        return JNI_FALSE;
    }
    const char *fname = reinterpret_cast<const char *>(fieldID);
    auto it = synth->bool_fields.find(fname);
    return (it != synth->bool_fields.end()) ? it->second : JNI_FALSE;
}

static __attribute__((sysv_abi)) jbyte Env_GetByteField(JNIEnv *, jobject, jfieldID) { return 0; }

static __attribute__((sysv_abi)) jchar Env_GetCharField(JNIEnv *, jobject, jfieldID) { return 0; }

static __attribute__((sysv_abi)) jshort Env_GetShortField(JNIEnv *, jobject, jfieldID) { return 0; }

static __attribute__((sysv_abi)) jint Env_GetIntField(JNIEnv *, jobject obj, jfieldID fieldID)
{
    if (!obj || !fieldID) {
        return 0;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (!synth) {
        return 0;
    }
    const char *fname = reinterpret_cast<const char *>(fieldID);
    auto it = synth->int_fields.find(fname);
    return (it != synth->int_fields.end()) ? it->second : 0;
}

static __attribute__((sysv_abi)) jlong Env_GetLongField(JNIEnv *, jobject, jfieldID) { return 0; }

static __attribute__((sysv_abi)) jfloat Env_GetFloatField(JNIEnv *, jobject obj, jfieldID fieldID)
{
    if (!obj || !fieldID) {
        return 0.0f;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (!synth) {
        return 0.0f;
    }
    const char *fname = reinterpret_cast<const char *>(fieldID);
    auto it = synth->float_fields.find(fname);
    return (it != synth->float_fields.end()) ? it->second : 0.0f;
}

static __attribute__((sysv_abi)) jdouble Env_GetDoubleField(JNIEnv *, jobject, jfieldID)
{
    return 0.0;
}

static __attribute__((sysv_abi)) void Env_SetObjectField(JNIEnv *, jobject obj, jfieldID fieldID,
                                                         jobject val)
{
    if (!obj || !fieldID) {
        return;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (synth) {
        synth->object_fields[reinterpret_cast<const char *>(fieldID)] = val;
    }
}

static __attribute__((sysv_abi)) void Env_SetBooleanField(JNIEnv *, jobject obj, jfieldID fieldID,
                                                          jboolean val)
{
    if (!obj || !fieldID) {
        return;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (synth) {
        synth->bool_fields[reinterpret_cast<const char *>(fieldID)] = val;
    }
}

static __attribute__((sysv_abi)) void Env_SetByteField(JNIEnv *, jobject, jfieldID, jbyte) {}

static __attribute__((sysv_abi)) void Env_SetCharField(JNIEnv *, jobject, jfieldID, jchar) {}

static __attribute__((sysv_abi)) void Env_SetShortField(JNIEnv *, jobject, jfieldID, jshort) {}

static __attribute__((sysv_abi)) void Env_SetIntField(JNIEnv *, jobject obj, jfieldID fieldID,
                                                      jint val)
{
    if (!obj || !fieldID) {
        return;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (synth) {
        synth->int_fields[reinterpret_cast<const char *>(fieldID)] = val;
    }
}

static __attribute__((sysv_abi)) void Env_SetLongField(JNIEnv *, jobject, jfieldID, jlong) {}

static __attribute__((sysv_abi)) void Env_SetFloatField(JNIEnv *, jobject obj, jfieldID fieldID,
                                                        jfloat val)
{
    if (!obj || !fieldID) {
        return;
    }
    auto synth = JniEnvironment::Instance().LookupObject(obj);
    if (synth) {
        synth->float_fields[reinterpret_cast<const char *>(fieldID)] = val;
    }
}

static __attribute__((sysv_abi)) void Env_SetDoubleField(JNIEnv *, jobject, jfieldID, jdouble) {}

// Static Field Operations
static __attribute__((sysv_abi)) jobject Env_GetStaticObjectField(JNIEnv *, jclass clazz,
                                                                  jfieldID fieldID)
{
    if (!fieldID) {
        return nullptr;
    }
    const char *cname = clazz ? reinterpret_cast<const char *>(clazz) : "";
    const char *fname = reinterpret_cast<const char *>(fieldID);
    auto &env = JniEnvironment::Instance();

    if (std::strstr(cname, "Build") != nullptr) {
        if (std::strcmp(fname, "MODEL") == 0) {
            return env.CreateString("Portage Virtual Device");
        }
        if (std::strcmp(fname, "MANUFACTURER") == 0) {
            return env.CreateString("Google");
        }
        if (std::strcmp(fname, "BRAND") == 0) {
            return env.CreateString("generic");
        }
        if (std::strcmp(fname, "DEVICE") == 0) {
            return env.CreateString("portage_x86_64");
        }
        if (std::strcmp(fname, "PRODUCT") == 0) {
            return env.CreateString("portage");
        }
        if (std::strcmp(fname, "HARDWARE") == 0) {
            return env.CreateString("portage");
        }
        if (std::strcmp(fname, "FINGERPRINT") == 0) {
            return env.CreateString("generic/portage/x86_64:13/TQ3A.230901.001/1:userdebug/"
                                    "test-keys");
        }
    }

    if (std::strstr(cname, "VERSION") != nullptr) {
        if (std::strcmp(fname, "RELEASE") == 0) {
            return env.CreateString("13");
        }
        if (std::strcmp(fname, "CODENAME") == 0) {
            return env.CreateString("REL");
        }
    }

    if (std::strstr(cname, "Bitmap$Config") != nullptr || std::strcmp(fname, "ARGB_8888") == 0 ||
        std::strcmp(fname, "RGB_565") == 0 || std::strcmp(fname, "ALPHA_8") == 0 ||
        std::strcmp(fname, "RGBA_F16") == 0) {
        auto cfg = std::make_shared<JniSyntheticObject>();
        cfg->type = JniObjectType::BitmapConfig;
        cfg->class_name = "android/graphics/Bitmap$Config";
        int32_t fmt = 1;
        if (std::strcmp(fname, "RGB_565") == 0) {
            fmt = 4;
        } else if (std::strcmp(fname, "ALPHA_8") == 0) {
            fmt = 8;
        } else if (std::strcmp(fname, "RGBA_F16") == 0) {
            fmt = 9;
        }
        cfg->int_fields["format"] = fmt;
        cfg->string_fields["name"] = fname;
        return env.RegisterObject(cfg);
    }

    return nullptr;
}

static __attribute__((sysv_abi)) jboolean Env_GetStaticBooleanField(JNIEnv *, jclass, jfieldID)
{
    return JNI_FALSE;
}

static __attribute__((sysv_abi)) jbyte Env_GetStaticByteField(JNIEnv *, jclass, jfieldID)
{
    return 0;
}

static __attribute__((sysv_abi)) jchar Env_GetStaticCharField(JNIEnv *, jclass, jfieldID)
{
    return 0;
}

static __attribute__((sysv_abi)) jshort Env_GetStaticShortField(JNIEnv *, jclass, jfieldID)
{
    return 0;
}

static __attribute__((sysv_abi)) jint Env_GetStaticIntField(JNIEnv *, jclass clazz,
                                                            jfieldID fieldID)
{
    if (!fieldID) {
        return 0;
    }
    const char *cname = clazz ? reinterpret_cast<const char *>(clazz) : "";
    const char *fname = reinterpret_cast<const char *>(fieldID);

    if (std::strstr(cname, "VERSION") != nullptr || std::strstr(cname, "Build") != nullptr) {
        if (std::strcmp(fname, "SDK_INT") == 0) {
            return 33; // Android 13 (Tiramisu)
        }
    }
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_GetStaticLongField(JNIEnv *, jclass, jfieldID)
{
    return 0;
}

static __attribute__((sysv_abi)) jfloat Env_GetStaticFloatField(JNIEnv *, jclass, jfieldID)
{
    return 0.0f;
}

static __attribute__((sysv_abi)) jdouble Env_GetStaticDoubleField(JNIEnv *, jclass, jfieldID)
{
    return 0.0;
}

static __attribute__((sysv_abi)) void Env_SetStaticObjectField(JNIEnv *, jclass, jfieldID, jobject)
{
}

static __attribute__((sysv_abi)) void Env_SetStaticBooleanField(JNIEnv *, jclass, jfieldID,
                                                                jboolean)
{
}

static __attribute__((sysv_abi)) void Env_SetStaticByteField(JNIEnv *, jclass, jfieldID, jbyte) {}

static __attribute__((sysv_abi)) void Env_SetStaticCharField(JNIEnv *, jclass, jfieldID, jchar) {}

static __attribute__((sysv_abi)) void Env_SetStaticShortField(JNIEnv *, jclass, jfieldID, jshort) {}

static __attribute__((sysv_abi)) void Env_SetStaticIntField(JNIEnv *, jclass, jfieldID, jint) {}

static __attribute__((sysv_abi)) void Env_SetStaticLongField(JNIEnv *, jclass, jfieldID, jlong) {}

static __attribute__((sysv_abi)) void Env_SetStaticFloatField(JNIEnv *, jclass, jfieldID, jfloat) {}

static __attribute__((sysv_abi)) void Env_SetStaticDoubleField(JNIEnv *, jclass, jfieldID, jdouble)
{
}

// Strings
static __attribute__((sysv_abi)) jstring Env_NewString(JNIEnv *, const jchar *unicode, jsize len)
{
    if (!unicode || len <= 0) {
        return JniEnvironment::Instance().CreateString("");
    }
    std::string s;
    s.reserve(static_cast<size_t>(len));
    for (jsize i = 0; i < len; ++i) {
        s.push_back(static_cast<char>(unicode[i]));
    }
    return JniEnvironment::Instance().CreateString(s);
}

static __attribute__((sysv_abi)) jsize Env_GetStringLength(JNIEnv *, jstring str)
{
    if (!str) {
        return 0;
    }
    const char *s = JniEnvironment::Instance().GetStringChars(str);
    return s ? static_cast<jsize>(std::strlen(s)) : 0;
}

static __attribute__((sysv_abi)) const jchar *Env_GetStringChars(JNIEnv *, jstring str,
                                                                 jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    // Portage strings are UTF-8; cast for callers requiring pointer identity
    return reinterpret_cast<const jchar *>(JniEnvironment::Instance().GetStringChars(str));
}

static __attribute__((sysv_abi)) void Env_ReleaseStringChars(JNIEnv *, jstring, const jchar *) {}

static __attribute__((sysv_abi)) jstring Env_NewStringUTF(JNIEnv *, const char *bytes)
{
    if (!bytes) {
        return nullptr;
    }
    return JniEnvironment::Instance().CreateString(bytes);
}

static __attribute__((sysv_abi)) jsize Env_GetStringUTFLength(JNIEnv *, jstring str)
{
    if (!str) {
        return 0;
    }
    const char *s = JniEnvironment::Instance().GetStringChars(str);
    return s ? static_cast<jsize>(std::strlen(s)) : 0;
}

static __attribute__((sysv_abi)) const char *Env_GetStringUTFChars(JNIEnv *, jstring str,
                                                                   jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    return JniEnvironment::Instance().GetStringChars(str);
}

static __attribute__((sysv_abi)) void Env_ReleaseStringUTFChars(JNIEnv *, jstring, const char *) {}

// Array Operations
static __attribute__((sysv_abi)) jsize Env_GetArrayLength(JNIEnv *, jarray arr)
{
    if (!arr) {
        return 0;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (!synth) {
        return 0;
    }
    switch (synth->type) {
    case JniObjectType::ByteArray:
        return static_cast<jsize>(synth->byte_data.size());
    case JniObjectType::IntArray:
        return static_cast<jsize>(synth->int_data.size());
    case JniObjectType::FloatArray:
        return static_cast<jsize>(synth->float_data.size());
    case JniObjectType::ObjectArray:
        return static_cast<jsize>(synth->object_data.size());
    default:
        return 0;
    }
}

static __attribute__((sysv_abi)) jobjectArray Env_NewObjectArray(JNIEnv *, jsize len, jclass clazz,
                                                                 jobject init)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ObjectArray;
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    obj->object_data.resize(std::max<jsize>(0, len), init);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_GetObjectArrayElement(JNIEnv *, jobjectArray arr,
                                                                   jsize index)
{
    if (!arr) {
        return nullptr;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (!synth || index < 0 || static_cast<size_t>(index) >= synth->object_data.size()) {
        return nullptr;
    }
    return synth->object_data[static_cast<size_t>(index)];
}

static __attribute__((sysv_abi)) void Env_SetObjectArrayElement(JNIEnv *, jobjectArray arr,
                                                                jsize index, jobject val)
{
    if (!arr) {
        return;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && index >= 0 && static_cast<size_t>(index) < synth->object_data.size()) {
        synth->object_data[static_cast<size_t>(index)] = val;
    }
}

static __attribute__((sysv_abi)) jbooleanArray Env_NewBooleanArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jbyteArray Env_NewByteArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jcharArray Env_NewCharArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len * 2), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jshortArray Env_NewShortArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len * 2), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jintArray Env_NewIntArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::IntArray;
    obj->int_data.resize(std::max<jsize>(0, len), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jlongArray Env_NewLongArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len * 8), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jfloatArray Env_NewFloatArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::FloatArray;
    obj->float_data.resize(std::max<jsize>(0, len), 0.0f);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jdoubleArray Env_NewDoubleArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ByteArray;
    obj->byte_data.resize(std::max<jsize>(0, len * 8), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jboolean *Env_GetBooleanArrayElements(JNIEnv *, jbooleanArray arr,
                                                                       jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jboolean *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jbyte *Env_GetByteArrayElements(JNIEnv *, jbyteArray arr,
                                                                 jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jbyte *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jchar *Env_GetCharArrayElements(JNIEnv *, jcharArray arr,
                                                                 jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jchar *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jshort *Env_GetShortArrayElements(JNIEnv *, jshortArray arr,
                                                                   jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jshort *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jint *Env_GetIntArrayElements(JNIEnv *, jintArray arr,
                                                               jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? synth->int_data.data() : nullptr;
}

static __attribute__((sysv_abi)) jlong *Env_GetLongArrayElements(JNIEnv *, jlongArray arr,
                                                                 jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jlong *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jfloat *Env_GetFloatArrayElements(JNIEnv *, jfloatArray arr,
                                                                   jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? synth->float_data.data() : nullptr;
}

static __attribute__((sysv_abi)) jdouble *Env_GetDoubleArrayElements(JNIEnv *, jdoubleArray arr,
                                                                     jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jdouble *>(synth->byte_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) void Env_ReleaseBooleanArrayElements(JNIEnv *, jbooleanArray,
                                                                      jboolean *, jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseByteArrayElements(JNIEnv *, jbyteArray, jbyte *,
                                                                   jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseCharArrayElements(JNIEnv *, jcharArray, jchar *,
                                                                   jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseShortArrayElements(JNIEnv *, jshortArray, jshort *,
                                                                    jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseIntArrayElements(JNIEnv *, jintArray, jint *, jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseLongArrayElements(JNIEnv *, jlongArray, jlong *,
                                                                   jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseFloatArrayElements(JNIEnv *, jfloatArray, jfloat *,
                                                                    jint)
{
}

static __attribute__((sysv_abi)) void Env_ReleaseDoubleArrayElements(JNIEnv *, jdoubleArray,
                                                                     jdouble *, jint)
{
}

static __attribute__((sysv_abi)) void
Env_GetBooleanArrayRegion(JNIEnv *, jbooleanArray arr, jsize start, jsize len, jboolean *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start, len);
    }
}

static __attribute__((sysv_abi)) void Env_GetByteArrayRegion(JNIEnv *, jbyteArray arr, jsize start,
                                                             jsize len, jbyte *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start, len);
    }
}

static __attribute__((sysv_abi)) void Env_GetCharArrayRegion(JNIEnv *, jcharArray arr, jsize start,
                                                             jsize len, jchar *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 2) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start * 2, len * 2);
    }
}

static __attribute__((sysv_abi)) void Env_GetShortArrayRegion(JNIEnv *, jshortArray arr,
                                                              jsize start, jsize len, jshort *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 2) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start * 2, len * 2);
    }
}

static __attribute__((sysv_abi)) void Env_GetIntArrayRegion(JNIEnv *, jintArray arr, jsize start,
                                                            jsize len, jint *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->int_data.size()) {
        std::memcpy(buf, synth->int_data.data() + start, len * sizeof(jint));
    }
}

static __attribute__((sysv_abi)) void Env_GetLongArrayRegion(JNIEnv *, jlongArray arr, jsize start,
                                                             jsize len, jlong *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 8) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start * 8, len * 8);
    }
}

static __attribute__((sysv_abi)) void Env_GetFloatArrayRegion(JNIEnv *, jfloatArray arr,
                                                              jsize start, jsize len, jfloat *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->float_data.size()) {
        std::memcpy(buf, synth->float_data.data() + start, len * sizeof(jfloat));
    }
}

static __attribute__((sysv_abi)) void Env_GetDoubleArrayRegion(JNIEnv *, jdoubleArray arr,
                                                               jsize start, jsize len, jdouble *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 8) <= synth->byte_data.size()) {
        std::memcpy(buf, synth->byte_data.data() + start * 8, len * 8);
    }
}

static __attribute__((sysv_abi)) void
Env_SetBooleanArrayRegion(JNIEnv *, jbooleanArray arr, jsize start, jsize len, const jboolean *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start, buf, len);
    }
}

static __attribute__((sysv_abi)) void Env_SetByteArrayRegion(JNIEnv *, jbyteArray arr, jsize start,
                                                             jsize len, const jbyte *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start, buf, len);
    }
}

static __attribute__((sysv_abi)) void Env_SetCharArrayRegion(JNIEnv *, jcharArray arr, jsize start,
                                                             jsize len, const jchar *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 2) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start * 2, buf, len * 2);
    }
}

static __attribute__((sysv_abi)) void
Env_SetShortArrayRegion(JNIEnv *, jshortArray arr, jsize start, jsize len, const jshort *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 2) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start * 2, buf, len * 2);
    }
}

static __attribute__((sysv_abi)) void Env_SetIntArrayRegion(JNIEnv *, jintArray arr, jsize start,
                                                            jsize len, const jint *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->int_data.size()) {
        std::memcpy(synth->int_data.data() + start, buf, len * sizeof(jint));
    }
}

static __attribute__((sysv_abi)) void Env_SetLongArrayRegion(JNIEnv *, jlongArray arr, jsize start,
                                                             jsize len, const jlong *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 8) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start * 8, buf, len * 8);
    }
}

static __attribute__((sysv_abi)) void
Env_SetFloatArrayRegion(JNIEnv *, jfloatArray arr, jsize start, jsize len, const jfloat *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->float_data.size()) {
        std::memcpy(synth->float_data.data() + start, buf, len * sizeof(jfloat));
    }
}

static __attribute__((sysv_abi)) void
Env_SetDoubleArrayRegion(JNIEnv *, jdoubleArray arr, jsize start, jsize len, const jdouble *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>((start + len) * 8) <= synth->byte_data.size()) {
        std::memcpy(synth->byte_data.data() + start * 8, buf, len * 8);
    }
}

// Native registration & NIO
static __attribute__((sysv_abi)) jint Env_RegisterNatives(JNIEnv *, jclass clazz,
                                                          const JNINativeMethod *methods,
                                                          jint nMethods)
{
    if (!clazz || !methods || nMethods <= 0) {
        return JNI_OK;
    }
    const char *cname = reinterpret_cast<const char *>(clazz);
    auto &env = JniEnvironment::Instance();
    for (jint i = 0; i < nMethods; ++i) {
        if (methods[i].name && methods[i].fnPtr) {
            env.RegisterNativeMethod(cname, methods[i].name,
                                     methods[i].signature ? methods[i].signature : "",
                                     methods[i].fnPtr);
        }
    }
    return JNI_OK;
}

static __attribute__((sysv_abi)) jint Env_UnregisterNatives(JNIEnv *, jclass) { return JNI_OK; }

static __attribute__((sysv_abi)) jint Env_MonitorEnter(JNIEnv *, jobject) { return JNI_OK; }

static __attribute__((sysv_abi)) jint Env_MonitorExit(JNIEnv *, jobject) { return JNI_OK; }

static __attribute__((sysv_abi)) jint Env_GetJavaVM(JNIEnv *, JavaVM **vm)
{
    if (vm) {
        *vm = JniEnvironment::Instance().GetJavaVM();
    }
    return JNI_OK;
}

static __attribute__((sysv_abi)) void Env_GetStringRegion(JNIEnv *, jstring str, jsize start,
                                                          jsize len, jchar *buf)
{
    const char *s = JniEnvironment::Instance().GetStringChars(str);
    if (s && buf && start >= 0 && len > 0) {
        size_t slen = std::strlen(s);
        for (jsize i = 0; i < len && static_cast<size_t>(start + i) < slen; ++i) {
            buf[i] = static_cast<jchar>(s[start + i]);
        }
    }
}

static __attribute__((sysv_abi)) void Env_GetStringUTFRegion(JNIEnv *, jstring str, jsize start,
                                                             jsize len, char *buf)
{
    const char *s = JniEnvironment::Instance().GetStringChars(str);
    if (s && buf && start >= 0 && len > 0) {
        size_t slen = std::strlen(s);
        if (static_cast<size_t>(start) < slen) {
            size_t copy_len = std::min<size_t>(len, slen - start);
            std::memcpy(buf, s + start, copy_len);
            buf[copy_len] = '\0';
        }
    }
}

static __attribute__((sysv_abi)) void *Env_GetPrimitiveArrayCritical(JNIEnv *, jarray arr,
                                                                     jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (!synth) {
        return nullptr;
    }
    if (!synth->byte_data.empty()) {
        return synth->byte_data.data();
    }
    if (!synth->int_data.empty()) {
        return synth->int_data.data();
    }
    if (!synth->float_data.empty()) {
        return synth->float_data.data();
    }
    return synth->buffer_address;
}

static __attribute__((sysv_abi)) void Env_ReleasePrimitiveArrayCritical(JNIEnv *, jarray, void *,
                                                                        jint)
{
}

static __attribute__((sysv_abi)) const jchar *Env_GetStringCritical(JNIEnv *env, jstring str,
                                                                    jboolean *isCopy)
{
    return Env_GetStringChars(env, str, isCopy);
}

static __attribute__((sysv_abi)) void Env_ReleaseStringCritical(JNIEnv *, jstring, const jchar *) {}

static __attribute__((sysv_abi)) jweak Env_NewWeakGlobalRef(JNIEnv *, jobject obj) { return obj; }

static __attribute__((sysv_abi)) void Env_DeleteWeakGlobalRef(JNIEnv *, jweak) {}

static __attribute__((sysv_abi)) jboolean Env_ExceptionCheck(JNIEnv *)
{
    return JniEnvironment::Instance().GetPendingException() != nullptr ? JNI_TRUE : JNI_FALSE;
}

// Direct Byte Buffers (NIO)
static __attribute__((sysv_abi)) jobject Env_NewDirectByteBuffer(JNIEnv *, void *address,
                                                                 jlong capacity)
{
    return JniEnvironment::Instance().CreateDirectByteBuffer(address, capacity);
}

static __attribute__((sysv_abi)) void *Env_GetDirectBufferAddress(JNIEnv *, jobject buf)
{
    if (!buf) {
        return nullptr;
    }
    auto synth = JniEnvironment::Instance().LookupObject(buf);
    return (synth && synth->type == JniObjectType::DirectBuffer) ? synth->buffer_address : nullptr;
}

static __attribute__((sysv_abi)) jlong Env_GetDirectBufferCapacity(JNIEnv *, jobject buf)
{
    if (!buf) {
        return -1;
    }
    auto synth = JniEnvironment::Instance().LookupObject(buf);
    return (synth && synth->type == JniObjectType::DirectBuffer) ? synth->buffer_capacity : -1;
}

static __attribute__((sysv_abi)) jint Env_GetObjectRefType(JNIEnv *, jobject) { return 2; }

JniEnvironment &JniEnvironment::Instance() noexcept
{
    static JniEnvironment instance;
    return instance;
}

JniEnvironment::JniEnvironment()
{
    SetupInterfaceTables();
    SetupSyntheticHierarchy();
}

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
    env_functions_->DefineClass = Env_DefineClass;
    env_functions_->FindClass = Env_FindClass;
    env_functions_->FromReflectedMethod = Env_FromReflectedMethod;
    env_functions_->FromReflectedField = Env_FromReflectedField;
    env_functions_->ToReflectedMethod = Env_ToReflectedMethod;
    env_functions_->GetSuperclass = Env_GetSuperclass;
    env_functions_->IsAssignableFrom = Env_IsAssignableFrom;
    env_functions_->ToReflectedField = Env_ToReflectedField;
    env_functions_->Throw = Env_Throw;
    env_functions_->ThrowNew = Env_ThrowNew;
    env_functions_->ExceptionOccurred = Env_ExceptionOccurred;
    env_functions_->ExceptionDescribe = Env_ExceptionDescribe;
    env_functions_->ExceptionClear = Env_ExceptionClear;
    env_functions_->FatalError = Env_FatalError;
    env_functions_->PushLocalFrame = Env_PushLocalFrame;
    env_functions_->PopLocalFrame = Env_PopLocalFrame;
    env_functions_->NewGlobalRef = Env_NewGlobalRef;
    env_functions_->DeleteGlobalRef = Env_DeleteGlobalRef;
    env_functions_->DeleteLocalRef = Env_DeleteLocalRef;
    env_functions_->IsSameObject = Env_IsSameObject;
    env_functions_->NewLocalRef = Env_NewLocalRef;
    env_functions_->EnsureLocalCapacity = Env_EnsureLocalCapacity;
    env_functions_->AllocObject = Env_AllocObject;
    env_functions_->NewObject = Env_NewObject;
    env_functions_->NewObjectV = Env_NewObjectV;
    env_functions_->NewObjectA = Env_NewObjectA;
    env_functions_->GetObjectClass = Env_GetObjectClass;
    env_functions_->IsInstanceOf = Env_IsInstanceOf;
    env_functions_->GetMethodID = Env_GetMethodID;

    env_functions_->CallObjectMethod = Env_CallObjectMethod;
    env_functions_->CallObjectMethodV = Env_CallObjectMethodV;
    env_functions_->CallObjectMethodA = Env_CallObjectMethodA;
    env_functions_->CallBooleanMethod = Env_CallBooleanMethod;
    env_functions_->CallBooleanMethodV = Env_CallBooleanMethodV;
    env_functions_->CallBooleanMethodA = Env_CallBooleanMethodA;
    env_functions_->CallByteMethod = Env_CallByteMethod;
    env_functions_->CallByteMethodV = Env_CallByteMethodV;
    env_functions_->CallByteMethodA = Env_CallByteMethodA;
    env_functions_->CallCharMethod = Env_CallCharMethod;
    env_functions_->CallCharMethodV = Env_CallCharMethodV;
    env_functions_->CallCharMethodA = Env_CallCharMethodA;
    env_functions_->CallShortMethod = Env_CallShortMethod;
    env_functions_->CallShortMethodV = Env_CallShortMethodV;
    env_functions_->CallShortMethodA = Env_CallShortMethodA;
    env_functions_->CallIntMethod = Env_CallIntMethod;
    env_functions_->CallIntMethodV = Env_CallIntMethodV;
    env_functions_->CallIntMethodA = Env_CallIntMethodA;
    env_functions_->CallLongMethod = Env_CallLongMethod;
    env_functions_->CallLongMethodV = Env_CallLongMethodV;
    env_functions_->CallLongMethodA = Env_CallLongMethodA;
    env_functions_->CallFloatMethod = Env_CallFloatMethod;
    env_functions_->CallFloatMethodV = Env_CallFloatMethodV;
    env_functions_->CallFloatMethodA = Env_CallFloatMethodA;
    env_functions_->CallDoubleMethod = Env_CallDoubleMethod;
    env_functions_->CallDoubleMethodV = Env_CallDoubleMethodV;
    env_functions_->CallDoubleMethodA = Env_CallDoubleMethodA;
    env_functions_->CallVoidMethod = Env_CallVoidMethod;
    env_functions_->CallVoidMethodV = Env_CallVoidMethodV;
    env_functions_->CallVoidMethodA = Env_CallVoidMethodA;

    env_functions_->GetFieldID = Env_GetFieldID;
    env_functions_->GetObjectField = Env_GetObjectField;
    env_functions_->GetBooleanField = Env_GetBooleanField;
    env_functions_->GetByteField = Env_GetByteField;
    env_functions_->GetCharField = Env_GetCharField;
    env_functions_->GetShortField = Env_GetShortField;
    env_functions_->GetIntField = Env_GetIntField;
    env_functions_->GetLongField = Env_GetLongField;
    env_functions_->GetFloatField = Env_GetFloatField;
    env_functions_->GetDoubleField = Env_GetDoubleField;
    env_functions_->SetObjectField = Env_SetObjectField;
    env_functions_->SetBooleanField = Env_SetBooleanField;
    env_functions_->SetByteField = Env_SetByteField;
    env_functions_->SetCharField = Env_SetCharField;
    env_functions_->SetShortField = Env_SetShortField;
    env_functions_->SetIntField = Env_SetIntField;
    env_functions_->SetLongField = Env_SetLongField;
    env_functions_->SetFloatField = Env_SetFloatField;
    env_functions_->SetDoubleField = Env_SetDoubleField;

    env_functions_->GetStaticMethodID = Env_GetStaticMethodID;
    env_functions_->CallStaticObjectMethod = Env_CallStaticObjectMethod;
    env_functions_->CallStaticObjectMethodV = Env_CallStaticObjectMethodV;
    env_functions_->CallStaticObjectMethodA = Env_CallStaticObjectMethodA;
    env_functions_->CallStaticBooleanMethod = Env_CallStaticBooleanMethod;
    env_functions_->CallStaticBooleanMethodV = Env_CallStaticBooleanMethodV;
    env_functions_->CallStaticBooleanMethodA = Env_CallStaticBooleanMethodA;
    env_functions_->CallStaticByteMethod = Env_CallStaticByteMethod;
    env_functions_->CallStaticByteMethodV = Env_CallStaticByteMethodV;
    env_functions_->CallStaticByteMethodA = Env_CallStaticByteMethodA;
    env_functions_->CallStaticCharMethod = Env_CallStaticCharMethod;
    env_functions_->CallStaticCharMethodV = Env_CallStaticCharMethodV;
    env_functions_->CallStaticCharMethodA = Env_CallStaticCharMethodA;
    env_functions_->CallStaticShortMethod = Env_CallStaticShortMethod;
    env_functions_->CallStaticShortMethodV = Env_CallStaticShortMethodV;
    env_functions_->CallStaticShortMethodA = Env_CallStaticShortMethodA;
    env_functions_->CallStaticIntMethod = Env_CallStaticIntMethod;
    env_functions_->CallStaticIntMethodV = Env_CallStaticIntMethodV;
    env_functions_->CallStaticIntMethodA = Env_CallStaticIntMethodA;
    env_functions_->CallStaticLongMethod = Env_CallStaticLongMethod;
    env_functions_->CallStaticLongMethodV = Env_CallStaticLongMethodV;
    env_functions_->CallStaticLongMethodA = Env_CallStaticLongMethodA;
    env_functions_->CallStaticFloatMethod = Env_CallStaticFloatMethod;
    env_functions_->CallStaticFloatMethodV = Env_CallStaticFloatMethodV;
    env_functions_->CallStaticFloatMethodA = Env_CallStaticFloatMethodA;
    env_functions_->CallStaticDoubleMethod = Env_CallStaticDoubleMethod;
    env_functions_->CallStaticDoubleMethodV = Env_CallStaticDoubleMethodV;
    env_functions_->CallStaticDoubleMethodA = Env_CallStaticDoubleMethodA;
    env_functions_->CallStaticVoidMethod = Env_CallStaticVoidMethod;
    env_functions_->CallStaticVoidMethodV = Env_CallStaticVoidMethodV;
    env_functions_->CallStaticVoidMethodA = Env_CallStaticVoidMethodA;

    env_functions_->GetStaticFieldID = Env_GetStaticFieldID;
    env_functions_->GetStaticObjectField = Env_GetStaticObjectField;
    env_functions_->GetStaticBooleanField = Env_GetStaticBooleanField;
    env_functions_->GetStaticByteField = Env_GetStaticByteField;
    env_functions_->GetStaticCharField = Env_GetStaticCharField;
    env_functions_->GetStaticShortField = Env_GetStaticShortField;
    env_functions_->GetStaticIntField = Env_GetStaticIntField;
    env_functions_->GetStaticLongField = Env_GetStaticLongField;
    env_functions_->GetStaticFloatField = Env_GetStaticFloatField;
    env_functions_->GetStaticDoubleField = Env_GetStaticDoubleField;
    env_functions_->SetStaticObjectField = Env_SetStaticObjectField;
    env_functions_->SetStaticBooleanField = Env_SetStaticBooleanField;
    env_functions_->SetStaticByteField = Env_SetStaticByteField;
    env_functions_->SetStaticCharField = Env_SetStaticCharField;
    env_functions_->SetStaticShortField = Env_SetStaticShortField;
    env_functions_->SetStaticIntField = Env_SetStaticIntField;
    env_functions_->SetStaticLongField = Env_SetStaticLongField;
    env_functions_->SetStaticFloatField = Env_SetStaticFloatField;
    env_functions_->SetStaticDoubleField = Env_SetStaticDoubleField;

    env_functions_->NewString = Env_NewString;
    env_functions_->GetStringLength = Env_GetStringLength;
    env_functions_->GetStringChars = Env_GetStringChars;
    env_functions_->ReleaseStringChars = Env_ReleaseStringChars;
    env_functions_->NewStringUTF = Env_NewStringUTF;
    env_functions_->GetStringUTFLength = Env_GetStringUTFLength;
    env_functions_->GetStringUTFChars = Env_GetStringUTFChars;
    env_functions_->ReleaseStringUTFChars = Env_ReleaseStringUTFChars;

    env_functions_->GetArrayLength = Env_GetArrayLength;
    env_functions_->NewObjectArray = Env_NewObjectArray;
    env_functions_->GetObjectArrayElement = Env_GetObjectArrayElement;
    env_functions_->SetObjectArrayElement = Env_SetObjectArrayElement;
    env_functions_->NewBooleanArray = Env_NewBooleanArray;
    env_functions_->NewByteArray = Env_NewByteArray;
    env_functions_->NewCharArray = Env_NewCharArray;
    env_functions_->NewShortArray = Env_NewShortArray;
    env_functions_->NewIntArray = Env_NewIntArray;
    env_functions_->NewLongArray = Env_NewLongArray;
    env_functions_->NewFloatArray = Env_NewFloatArray;
    env_functions_->NewDoubleArray = Env_NewDoubleArray;

    env_functions_->GetBooleanArrayElements = Env_GetBooleanArrayElements;
    env_functions_->GetByteArrayElements = Env_GetByteArrayElements;
    env_functions_->GetCharArrayElements = Env_GetCharArrayElements;
    env_functions_->GetShortArrayElements = Env_GetShortArrayElements;
    env_functions_->GetIntArrayElements = Env_GetIntArrayElements;
    env_functions_->GetLongArrayElements = Env_GetLongArrayElements;
    env_functions_->GetFloatArrayElements = Env_GetFloatArrayElements;
    env_functions_->GetDoubleArrayElements = Env_GetDoubleArrayElements;

    env_functions_->ReleaseBooleanArrayElements = Env_ReleaseBooleanArrayElements;
    env_functions_->ReleaseByteArrayElements = Env_ReleaseByteArrayElements;
    env_functions_->ReleaseCharArrayElements = Env_ReleaseCharArrayElements;
    env_functions_->ReleaseShortArrayElements = Env_ReleaseShortArrayElements;
    env_functions_->ReleaseIntArrayElements = Env_ReleaseIntArrayElements;
    env_functions_->ReleaseLongArrayElements = Env_ReleaseLongArrayElements;
    env_functions_->ReleaseFloatArrayElements = Env_ReleaseFloatArrayElements;
    env_functions_->ReleaseDoubleArrayElements = Env_ReleaseDoubleArrayElements;

    env_functions_->GetBooleanArrayRegion = Env_GetBooleanArrayRegion;
    env_functions_->GetByteArrayRegion = Env_GetByteArrayRegion;
    env_functions_->GetCharArrayRegion = Env_GetCharArrayRegion;
    env_functions_->GetShortArrayRegion = Env_GetShortArrayRegion;
    env_functions_->GetIntArrayRegion = Env_GetIntArrayRegion;
    env_functions_->GetLongArrayRegion = Env_GetLongArrayRegion;
    env_functions_->GetFloatArrayRegion = Env_GetFloatArrayRegion;
    env_functions_->GetDoubleArrayRegion = Env_GetDoubleArrayRegion;

    env_functions_->SetBooleanArrayRegion = Env_SetBooleanArrayRegion;
    env_functions_->SetByteArrayRegion = Env_SetByteArrayRegion;
    env_functions_->SetCharArrayRegion = Env_SetCharArrayRegion;
    env_functions_->SetShortArrayRegion = Env_SetShortArrayRegion;
    env_functions_->SetIntArrayRegion = Env_SetIntArrayRegion;
    env_functions_->SetLongArrayRegion = Env_SetLongArrayRegion;
    env_functions_->SetFloatArrayRegion = Env_SetFloatArrayRegion;
    env_functions_->SetDoubleArrayRegion = Env_SetDoubleArrayRegion;

    env_functions_->RegisterNatives = Env_RegisterNatives;
    env_functions_->UnregisterNatives = Env_UnregisterNatives;
    env_functions_->MonitorEnter = Env_MonitorEnter;
    env_functions_->MonitorExit = Env_MonitorExit;
    env_functions_->GetJavaVM = Env_GetJavaVM;

    env_functions_->GetStringRegion = Env_GetStringRegion;
    env_functions_->GetStringUTFRegion = Env_GetStringUTFRegion;
    env_functions_->GetPrimitiveArrayCritical = Env_GetPrimitiveArrayCritical;
    env_functions_->ReleasePrimitiveArrayCritical = Env_ReleasePrimitiveArrayCritical;
    env_functions_->GetStringCritical = Env_GetStringCritical;
    env_functions_->ReleaseStringCritical = Env_ReleaseStringCritical;
    env_functions_->NewWeakGlobalRef = Env_NewWeakGlobalRef;
    env_functions_->DeleteWeakGlobalRef = Env_DeleteWeakGlobalRef;
    env_functions_->ExceptionCheck = Env_ExceptionCheck;

    env_functions_->NewDirectByteBuffer = Env_NewDirectByteBuffer;
    env_functions_->GetDirectBufferAddress = Env_GetDirectBufferAddress;
    env_functions_->GetDirectBufferCapacity = Env_GetDirectBufferCapacity;
    env_functions_->GetObjectRefType = Env_GetObjectRefType;

    env_.functions = env_functions_.get();
}

void JniEnvironment::SetupSyntheticHierarchy()
{
    // DisplayMetrics
    auto dm = std::make_shared<JniSyntheticObject>();
    dm->type = JniObjectType::DisplayMetrics;
    dm->class_name = "android/util/DisplayMetrics";
    dm->int_fields["widthPixels"] = display_width_;
    dm->int_fields["heightPixels"] = display_height_;
    dm->int_fields["densityDpi"] = display_density_dpi_;
    dm->float_fields["density"] = display_density_;
    dm->float_fields["scaledDensity"] = display_density_;
    dm->float_fields["xdpi"] = 320.0f;
    dm->float_fields["ydpi"] = 320.0f;
    display_metrics_obj_ = RegisterObject(dm);

    // Resources
    auto res = std::make_shared<JniSyntheticObject>();
    res->type = JniObjectType::Resources;
    res->class_name = "android/content/res/Resources";
    res->object_fields["displayMetrics"] = display_metrics_obj_;
    resources_obj_ = RegisterObject(res);

    // DecorView
    auto decor = std::make_shared<JniSyntheticObject>();
    decor->type = JniObjectType::View;
    decor->class_name = "android/view/View";
    decor_view_obj_ = RegisterObject(decor);

    // Window
    auto win = std::make_shared<JniSyntheticObject>();
    win->type = JniObjectType::Window;
    win->class_name = "android/view/Window";
    win->object_fields["decorView"] = decor_view_obj_;
    window_obj_ = RegisterObject(win);

    // ClassLoader
    auto cl = std::make_shared<JniSyntheticObject>();
    cl->type = JniObjectType::ClassLoader;
    cl->class_name = "java/lang/ClassLoader";
    class_loader_obj_ = RegisterObject(cl);

    // Context / Activity
    auto ctx = std::make_shared<JniSyntheticObject>();
    ctx->type = JniObjectType::Context;
    ctx->class_name = "android/app/Activity";
    ctx->string_fields["packageName"] = package_name_;
    ctx->object_fields["resources"] = resources_obj_;
    ctx->object_fields["window"] = window_obj_;
    ctx->object_fields["classLoader"] = class_loader_obj_;
    context_obj_ = RegisterObject(ctx);
}

void JniEnvironment::Initialize(std::string_view package_name, std::string_view files_dir,
                                std::string_view cache_dir)
{
    package_name_ = package_name;
    files_dir_ = files_dir;
    cache_dir_ = cache_dir;
    Reset();
}

void JniEnvironment::Reset()
{
    string_table_.clear();
    object_table_.clear();
    objects_by_handle_.clear();
    registered_natives_.clear();
    pending_exception_ = nullptr;
    next_handle_ = 0x1000;
    SetupSyntheticHierarchy();
}

jstring JniEnvironment::CreateString(std::string_view str)
{
    for (size_t i = 0; i < string_table_.size(); ++i) {
        if (string_table_[i] == str) {
            return reinterpret_cast<jstring>(static_cast<uintptr_t>(i + 1));
        }
    }
    string_table_.emplace_back(str);
    return reinterpret_cast<jstring>(static_cast<uintptr_t>(string_table_.size()));
}

const char *JniEnvironment::GetStringChars(jstring str) const
{
    auto idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(str));
    if (idx == 0 || idx > string_table_.size()) {
        return nullptr;
    }
    return string_table_[idx - 1].c_str();
}

jobject JniEnvironment::RegisterObject(std::shared_ptr<JniSyntheticObject> obj)
{
    if (!obj) {
        return nullptr;
    }
    auto handle = reinterpret_cast<jobject>(next_handle_++);
    object_table_.push_back(obj);
    objects_by_handle_[handle] = obj;
    return handle;
}

std::shared_ptr<JniSyntheticObject> JniEnvironment::LookupObject(jobject obj) const
{
    if (!obj) {
        return nullptr;
    }
    auto it = objects_by_handle_.find(obj);
    return (it != objects_by_handle_.end()) ? it->second : nullptr;
}

jobject JniEnvironment::CreateDirectByteBuffer(void *address, jlong capacity)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::DirectBuffer;
    obj->class_name = "java/nio/DirectByteBuffer";
    obj->buffer_address = address;
    obj->buffer_capacity = capacity;
    return RegisterObject(obj);
}

jobject JniEnvironment::CreateBitmap(uint32_t width, uint32_t height, int32_t format)
{
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        width = 64;
        height = 64;
    }
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Bitmap;
    obj->class_name = "android/graphics/Bitmap";
    obj->bitmap_width = width;
    obj->bitmap_height = height;
    obj->bitmap_format = (format > 0) ? format : 1; // 1 = RGBA_8888

    int32_t bpp = (obj->bitmap_format == 4 /* RGB_565 */) ? 2 : 4;
    obj->bitmap_stride = width * bpp;
    size_t total_bytes = static_cast<size_t>(obj->bitmap_stride) * height;
    obj->bitmap_pixels.resize(total_bytes, 0);
    obj->buffer_address = obj->bitmap_pixels.data();
    obj->buffer_capacity = static_cast<jlong>(total_bytes);
    obj->bitmap_locked = false;

    return RegisterObject(obj);
}

void JniEnvironment::RegisterNativeMethod(std::string_view className, std::string_view methodName,
                                          std::string_view sig, void *fnPtr)
{
    std::string key = std::string(className) + "/" + std::string(methodName) + std::string(sig);
    registered_natives_[key] = fnPtr;
}

void *JniEnvironment::LookupNativeMethod(std::string_view className, std::string_view methodName,
                                         std::string_view sig) const
{
    std::string key = std::string(className) + "/" + std::string(methodName) + std::string(sig);
    auto it = registered_natives_.find(key);
    return (it != registered_natives_.end()) ? it->second : nullptr;
}

void JniEnvironment::SetDisplayMetrics(int32_t width, int32_t height, float density,
                                       int32_t densityDpi)
{
    display_width_ = width;
    display_height_ = height;
    display_density_ = density;
    display_density_dpi_ = densityDpi;

    auto dm = LookupObject(display_metrics_obj_);
    if (dm) {
        dm->int_fields["widthPixels"] = width;
        dm->int_fields["heightPixels"] = height;
        dm->int_fields["densityDpi"] = densityDpi;
        dm->float_fields["density"] = density;
        dm->float_fields["scaledDensity"] = density;
    }
}

} // namespace portage::package
