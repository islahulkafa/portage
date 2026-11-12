#include "jni_env.hpp"
#include "audio/audio_manager.hpp"
#include "camera/camera_manager.hpp"
#include "clipboard/clipboard_manager.hpp"
#include "input/haptic_manager.hpp"
#include "location/location_manager.hpp"
#include "notification/notification_manager.hpp"
#include "power/power_manager.hpp"
#include "security/keystore_manager.hpp"
#include "ndk/image_decoder.hpp"
#include "ndk/multinetwork.hpp"
#include "ndk/performance_hint.hpp"
#include "ndk/thermal.hpp"
#include "package_manager.hpp"
#include "resource_table.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>

namespace portage::package {

namespace {

jobject DecodeBitmapFromEncodedBytes(std::span<const uint8_t> encoded, jobject options_obj)
{
    if (encoded.empty()) {
        return nullptr;
    }

    auto &jni = JniEnvironment::Instance();
    ndk::DecodedImageHeader hdr{};
    if (ndk::WicImageCodec::InspectImage(encoded, hdr) != ANDROID_IMAGE_DECODER_SUCCESS) {
        return nullptr;
    }

    bool just_bounds = false;
    int32_t sample_size = 1;
    int32_t pref_format = ANDROID_BITMAP_FORMAT_RGBA_8888;
    std::shared_ptr<JniSyntheticObject> opts;
    if (options_obj) {
        opts = jni.LookupObject(options_obj);
        if (opts) {
            auto b_it = opts->bool_fields.find("inJustDecodeBounds");
            just_bounds = (b_it != opts->bool_fields.end() && b_it->second != 0);
            auto s_it = opts->int_fields.find("inSampleSize");
            if (s_it != opts->int_fields.end() && s_it->second > 1) {
                sample_size = s_it->second;
            }
            auto c_it = opts->object_fields.find("inPreferredConfig");
            if (c_it != opts->object_fields.end() && c_it->second) {
                auto cfg = jni.LookupObject(c_it->second);
                if (cfg && cfg->int_fields.count("format")) {
                    pref_format = cfg->int_fields["format"];
                }
            }
        }
    }

    uint32_t out_w = std::max(1U, hdr.width / static_cast<uint32_t>(sample_size));
    uint32_t out_h = std::max(1U, hdr.height / static_cast<uint32_t>(sample_size));

    if (opts) {
        opts->int_fields["outWidth"] = static_cast<int32_t>(out_w);
        opts->int_fields["outHeight"] = static_cast<int32_t>(out_h);
        opts->object_fields["outMimeType"] = jni.CreateString(hdr.mime_type);
        opts->string_fields["outMimeType"] = hdr.mime_type;
    }

    if (just_bounds) {
        return nullptr;
    }

    ndk::ImageDecodeOptions decode_opts{};
    if (sample_size > 1) {
        decode_opts.target_width = static_cast<int32_t>(out_w);
        decode_opts.target_height = static_cast<int32_t>(out_h);
    }
    decode_opts.android_format = pref_format;

    std::vector<uint8_t> pixels;
    uint32_t stride = 0;
    if (ndk::WicImageCodec::DecodeImageFrame(encoded, 0, decode_opts, pixels, stride, hdr) !=
        ANDROID_IMAGE_DECODER_SUCCESS) {
        return nullptr;
    }
    return jni.CreateBitmapFromPixels(out_w, out_h, pref_format, stride, std::move(pixels));
}

bool CommitSharedPrefsEditor(JniEnvironment &env, JniSyntheticObject &editor_syn)
{
    const std::string &prefs_name = editor_syn.string_fields.count("prefs_name")
                                        ? editor_syn.string_fields["prefs_name"]
                                        : "default";
    auto &store = env.GetSharedPrefsMap(prefs_name);
    if (editor_syn.bool_fields["clear"] != 0) {
        store.clear();
    }
    for (const auto &[field_key, val] : editor_syn.int_fields) {
        if (field_key.starts_with("rm:")) {
            store.erase(field_key.substr(3));
        } else if (field_key.starts_with("put_i:")) {
            JniSharedPrefValue entry{};
            entry.kind = JniSharedPrefValue::Kind::Int;
            entry.int_val = val;
            store[field_key.substr(6)] = entry;
        }
    }
    for (const auto &[field_key, val] : editor_syn.string_fields) {
        if (field_key.starts_with("put_s:")) {
            JniSharedPrefValue entry{};
            entry.kind = JniSharedPrefValue::Kind::String;
            entry.str_val = val;
            store[field_key.substr(6)] = entry;
        }
    }
    for (const auto &[field_key, val] : editor_syn.bool_fields) {
        if (field_key.starts_with("put_b:")) {
            JniSharedPrefValue entry{};
            entry.kind = JniSharedPrefValue::Kind::Bool;
            entry.bool_val = (val != 0);
            store[field_key.substr(6)] = entry;
        }
    }
    for (const auto &[field_key, val] : editor_syn.float_fields) {
        if (field_key.starts_with("put_f:")) {
            JniSharedPrefValue entry{};
            entry.kind = JniSharedPrefValue::Kind::Float;
            entry.float_val = val;
            store[field_key.substr(6)] = entry;
        }
    }
    env.SaveSharedPrefs(prefs_name);
    return true;
}

} // namespace

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

static const std::unordered_map<std::string, std::string> &GetSuperclassTable()
{
    static const std::unordered_map<std::string, std::string> kSuperclasses = {
        {"android/app/NativeActivity", "android/app/Activity"},
        {"android/app/Activity", "android/content/Context"},
        {"android/content/ContextThemeWrapper", "android/content/ContextWrapper"},
        {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
        {"android/content/ContextWrapper", "android/content/Context"},
        {"android/content/Context", "java/lang/Object"},
        {"android/view/View", "java/lang/Object"},
        {"android/view/Window", "java/lang/Object"},
        {"android/view/Display", "java/lang/Object"},
        {"android/view/WindowManager", "java/lang/Object"},
        {"android/os/Bundle", "java/lang/Object"},
        {"android/os/Vibrator", "java/lang/Object"},
        {"android/graphics/Point", "java/lang/Object"},
        {"android/content/pm/PackageManager", "java/lang/Object"},
        {"android/content/pm/PackageInfo", "java/lang/Object"},
        {"android/content/pm/ApplicationInfo", "java/lang/Object"},
        {"java/lang/String", "java/lang/Object"},
        {"java/lang/Class", "java/lang/Object"},
        {"java/lang/reflect/Method", "java/lang/Object"},
        {"java/lang/reflect/Field", "java/lang/Object"},
    };
    return kSuperclasses;
}

static __attribute__((sysv_abi)) jclass Env_FindClass(JNIEnv *, const char *name)
{
    if (!name) {
        return nullptr;
    }
    return const_cast<char *>(name);
}

static __attribute__((sysv_abi)) jmethodID Env_FromReflectedMethod(JNIEnv *, jobject method)
{
    if (!method) {
        return nullptr;
    }
    auto syn = JniEnvironment::Instance().LookupObject(method);
    if (!syn || syn->type != JniObjectType::Method) {
        return nullptr;
    }
    if (syn->method_id) {
        return syn->method_id;
    }
    return reinterpret_cast<jmethodID>(const_cast<char *>(syn->member_name.c_str()));
}

static __attribute__((sysv_abi)) jfieldID Env_FromReflectedField(JNIEnv *, jobject field)
{
    if (!field) {
        return nullptr;
    }
    auto syn = JniEnvironment::Instance().LookupObject(field);
    if (!syn || syn->type != JniObjectType::Field) {
        return nullptr;
    }
    if (syn->field_id) {
        return syn->field_id;
    }
    return reinterpret_cast<jfieldID>(const_cast<char *>(syn->member_name.c_str()));
}

static __attribute__((sysv_abi)) jobject Env_ToReflectedMethod(JNIEnv *, jclass clazz,
                                                               jmethodID methodID,
                                                               jboolean isStatic)
{
    if (!methodID) {
        return nullptr;
    }
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Method;
    obj->class_name = "java/lang/reflect/Method";
    obj->target_class = clazz ? reinterpret_cast<const char *>(clazz) : "";
    obj->member_name = reinterpret_cast<const char *>(methodID);
    obj->string_fields["name"] = obj->member_name;
    obj->is_static = (isStatic == JNI_TRUE);
    obj->method_id = methodID;
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jclass Env_GetSuperclass(JNIEnv *, jclass clazz)
{
    if (!clazz) {
        return nullptr;
    }
    std::string cname = reinterpret_cast<const char *>(clazz);
    if (cname == "java/lang/Object") {
        return nullptr;
    }
    const auto &table = GetSuperclassTable();
    auto it = table.find(cname);
    if (it != table.end()) {
        return const_cast<char *>(it->second.c_str());
    }
    return nullptr;
}

static __attribute__((sysv_abi)) jboolean Env_IsAssignableFrom(JNIEnv *, jclass sub, jclass sup)
{
    if (!sub || !sup) {
        return JNI_FALSE;
    }
    std::string cur = reinterpret_cast<const char *>(sub);
    std::string target = reinterpret_cast<const char *>(sup);
    if (cur == target || target == "java/lang/Object") {
        return JNI_TRUE;
    }
    const auto &table = GetSuperclassTable();
    while (true) {
        auto it = table.find(cur);
        if (it == table.end()) {
            return JNI_FALSE;
        }
        if (it->second == target) {
            return JNI_TRUE;
        }
        cur = it->second;
    }
}

static __attribute__((sysv_abi)) jobject Env_ToReflectedField(JNIEnv *, jclass clazz,
                                                              jfieldID fieldID, jboolean isStatic)
{
    if (!fieldID) {
        return nullptr;
    }
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Field;
    obj->class_name = "java/lang/reflect/Field";
    obj->target_class = clazz ? reinterpret_cast<const char *>(clazz) : "";
    obj->member_name = reinterpret_cast<const char *>(fieldID);
    obj->string_fields["name"] = obj->member_name;
    obj->is_static = (isStatic == JNI_TRUE);
    obj->field_id = fieldID;
    return JniEnvironment::Instance().RegisterObject(obj);
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

static void InitSyntheticObjectState(const std::shared_ptr<JniSyntheticObject> &obj)
{
    if (!obj) {
        return;
    }
    if (obj->class_name == "android/media/AudioRecord") {
        obj->int_fields["getState"] = 1;
        obj->int_fields["getRecordingState"] = 1;
    } else if (obj->class_name == "android/graphics/BitmapFactory$Options") {
        obj->int_fields["inSampleSize"] = 1;
        obj->int_fields["outWidth"] = 0;
        obj->int_fields["outHeight"] = 0;
        obj->bool_fields["inJustDecodeBounds"] = JNI_FALSE;
    } else if (obj->class_name == "android/util/DisplayMetrics") {
        obj->type = JniObjectType::DisplayMetrics;
        auto &env = JniEnvironment::Instance();
        obj->int_fields["widthPixels"] = env.GetDisplayWidth();
        obj->int_fields["heightPixels"] = env.GetDisplayHeight();
        obj->int_fields["densityDpi"] = env.GetDisplayDensityDpi();
        obj->float_fields["density"] = env.GetDisplayDensity();
        obj->float_fields["scaledDensity"] = env.GetDisplayDensity();
    } else if (obj->class_name == "android/os/Bundle") {
        obj->type = JniObjectType::Bundle;
    } else if (obj->class_name == "android/os/Vibrator") {
        obj->type = JniObjectType::Vibrator;
    } else if (obj->class_name == "android/graphics/Point") {
        obj->type = JniObjectType::Point;
        obj->int_fields["x"] = 0;
        obj->int_fields["y"] = 0;
    } else if (obj->class_name == "android/content/pm/PackageInfo") {
        obj->type = JniObjectType::PackageInfo;
    } else if (obj->class_name == "android/content/pm/ApplicationInfo") {
        obj->type = JniObjectType::ApplicationInfo;
    } else if (obj->class_name == "android/view/View") {
        obj->type = JniObjectType::View;
        obj->int_fields["systemUiVisibility"] = 0;
    }
}

static __attribute__((sysv_abi)) jobject Env_AllocObject(JNIEnv *, jclass clazz)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    InitSyntheticObjectState(obj);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObject(JNIEnv *, jclass clazz, jmethodID, ...)
{
    uintptr_t arg0 = 0;
    uintptr_t arg1 = 0;
    uintptr_t arg2 = 0;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
    __asm__ volatile("movq %%r8, %0" : "=r"(arg1));
    __asm__ volatile("movq %%r9, %0" : "=r"(arg2));
#endif
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    InitSyntheticObjectState(obj);

    if (obj->class_name == "android/app/NotificationChannel") {
        auto &env = JniEnvironment::Instance();
        jstring id_str = reinterpret_cast<jstring>(arg0);
        jstring name_str = reinterpret_cast<jstring>(arg1);
        const char *id = env.GetStringChars(id_str);
        const char *name = env.GetStringChars(name_str);
        obj->string_fields["id"] = id ? id : "default";
        obj->string_fields["name"] = name ? name : "Default";
        obj->int_fields["importance"] = static_cast<int32_t>(arg2);
    } else if (obj->class_name == "android/app/Notification$Builder" ||
               obj->class_name == "android/app/Notification.Builder") {
        auto &env = JniEnvironment::Instance();
        jstring ch_str = reinterpret_cast<jstring>(arg1);
        const char *ch = env.GetStringChars(ch_str);
        obj->string_fields["channel_id"] = ch ? ch : "default";
    } else if (obj->class_name == "android/graphics/Point") {
        obj->type = JniObjectType::Point;
        obj->int_fields["x"] = static_cast<int32_t>(arg0);
        obj->int_fields["y"] = static_cast<int32_t>(arg1);
    }

    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObjectV(JNIEnv *, jclass clazz, jmethodID, va_list)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    InitSyntheticObjectState(obj);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jobject Env_NewObjectA(JNIEnv *, jclass clazz, jmethodID,
                                                        const void *args)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->class_name = clazz ? reinterpret_cast<const char *>(clazz) : "java/lang/Object";
    InitSyntheticObjectState(obj);
    if (obj->class_name == "android/graphics/Point" && args) {
        const auto *jv = static_cast<const uint64_t *>(args);
        obj->type = JniObjectType::Point;
        obj->int_fields["x"] = static_cast<int32_t>(jv[0]);
        obj->int_fields["y"] = static_cast<int32_t>(jv[1]);
    }
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

static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethod(JNIEnv *, jclass clazz,
                                                                    jmethodID methodID,
                                                                    uintptr_t arg0, uintptr_t arg1,
                                                                    uintptr_t arg2, uintptr_t arg3);
static __attribute__((sysv_abi)) jobject Env_GetObjectField(JNIEnv *, jobject obj,
                                                            jfieldID fieldID);
static __attribute__((sysv_abi)) jobject Env_GetStaticObjectField(JNIEnv *, jclass clazz,
                                                                  jfieldID fieldID);
static __attribute__((sysv_abi)) void Env_SetObjectField(JNIEnv *, jobject obj, jfieldID fieldID,
                                                         jobject val);

static jobject DispatchObjectMethod(JNIEnv *jni_env, jobject obj, jmethodID methodID, uintptr_t arg0,
                                    uintptr_t arg1, uintptr_t arg2)
{
    (void)arg2;
    if (!methodID) {
        return nullptr;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    auto &env = JniEnvironment::Instance();

    if (std::strcmp(mname, "invoke") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->type == JniObjectType::Method) {
            jobject target_obj = reinterpret_cast<jobject>(arg0);
            jmethodID target_mid =
                syn->method_id
                    ? syn->method_id
                    : reinterpret_cast<jmethodID>(const_cast<char *>(syn->member_name.c_str()));
            uintptr_t a0 = 0;
            uintptr_t a1 = 0;
            uintptr_t a2 = 0;
            uintptr_t a3 = 0;
            if (arg1 != 0) {
                auto arr_syn = env.LookupObject(reinterpret_cast<jobject>(arg1));
                if (arr_syn && arr_syn->type == JniObjectType::ObjectArray) {
                    if (arr_syn->object_data.size() > 0) {
                        a0 = reinterpret_cast<uintptr_t>(arr_syn->object_data[0]);
                    }
                    if (arr_syn->object_data.size() > 1) {
                        a1 = reinterpret_cast<uintptr_t>(arr_syn->object_data[1]);
                    }
                    if (arr_syn->object_data.size() > 2) {
                        a2 = reinterpret_cast<uintptr_t>(arr_syn->object_data[2]);
                    }
                    if (arr_syn->object_data.size() > 3) {
                        a3 = reinterpret_cast<uintptr_t>(arr_syn->object_data[3]);
                    }
                }
            }
            if (syn->is_static) {
                return Env_CallStaticObjectMethod(
                    jni_env, const_cast<char *>(syn->target_class.c_str()), target_mid, a0, a1, a2,
                    a3);
            }
            return DispatchObjectMethod(jni_env, target_obj, target_mid, a0, a1, a2);
        }
    }
    if (std::strcmp(mname, "get") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->type == JniObjectType::Field) {
            jfieldID target_fid =
                syn->field_id
                    ? syn->field_id
                    : reinterpret_cast<jfieldID>(const_cast<char *>(syn->member_name.c_str()));
            if (syn->is_static) {
                return Env_GetStaticObjectField(
                    jni_env, const_cast<char *>(syn->target_class.c_str()), target_fid);
            }
            return Env_GetObjectField(jni_env, reinterpret_cast<jobject>(arg0), target_fid);
        }
    }

    if (std::strcmp(mname, "getPackageName") == 0) {
        return env.CreateString(env.GetPackageName());
    }
    if (std::strcmp(mname, "getFilesDir") == 0) {
        return env.CreateString(env.GetFilesDir());
    }
    if (std::strcmp(mname, "getAbsolutePath") == 0 || std::strcmp(mname, "getPath") == 0) {
        if (obj) {
            auto synth = env.LookupObject(obj);
            if (synth) {
                auto it = synth->string_fields.find("path");
                if (it != synth->string_fields.end()) {
                    return env.CreateString(it->second);
                }
            }
        }
        return env.CreateString(env.GetFilesDir());
    }
    if (std::strcmp(mname, "getObbDir") == 0) {
        auto obj_ret = std::make_shared<JniSyntheticObject>();
        obj_ret->class_name = "java/io/File";
        std::string pkg = env.GetPackageName();
        if (pkg.empty()) {
            const auto *active_pkg = PackageManager::Instance().GetActivePackage();
            if (active_pkg) {
                pkg = active_pkg->manifest.package_name;
            }
        }
        obj_ret->string_fields["path"] = "/storage/emulated/0/Android/obb/" + pkg;
        return env.RegisterObject(obj_ret);
    }
    if (std::strcmp(mname, "getObbDirs") == 0) {
        auto file_obj = std::make_shared<JniSyntheticObject>();
        file_obj->class_name = "java/io/File";
        std::string pkg = env.GetPackageName();
        if (pkg.empty()) {
            const auto *active_pkg = PackageManager::Instance().GetActivePackage();
            if (active_pkg) {
                pkg = active_pkg->manifest.package_name;
            }
        }
        file_obj->string_fields["path"] = "/storage/emulated/0/Android/obb/" + pkg;
        jobject file_job = env.RegisterObject(file_obj);

        auto arr_obj = std::make_shared<JniSyntheticObject>();
        arr_obj->type = JniObjectType::ObjectArray;
        arr_obj->class_name = "java/io/File";
        arr_obj->object_data.push_back(file_job);
        return env.RegisterObject(arr_obj);
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
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn && syn->class_name == "android/content/SharedPreferences") {
                const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                const auto &prefs_name = syn->string_fields.count("prefs_name")
                                             ? syn->string_fields["prefs_name"]
                                             : "default";
                auto &store = env.GetSharedPrefsMap(prefs_name);
                if (key) {
                    auto it = store.find(key);
                    if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::String) {
                        return env.CreateString(it->second.str_val);
                    }
                }
                return reinterpret_cast<jstring>(arg1);
            }
            if (syn &&
                (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
                const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                if (key) {
                    auto it = syn->bundle_strings.find(key);
                    if (it != syn->bundle_strings.end()) {
                        return env.CreateString(it->second);
                    }
                }
                return nullptr;
            }
        }
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
    if (std::strcmp(mname, "getSharedPreferences") == 0) {
        const char *name = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        auto sp = std::make_shared<JniSyntheticObject>();
        sp->class_name = "android/content/SharedPreferences";
        sp->string_fields["prefs_name"] = name ? name : "default";
        (void)env.GetSharedPrefsMap(sp->string_fields["prefs_name"]);
        return env.RegisterObject(sp);
    }
    if (std::strcmp(mname, "edit") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        auto ed = std::make_shared<JniSyntheticObject>();
        ed->class_name = "android/content/SharedPreferences$Editor";
        ed->string_fields["prefs_name"] = (syn && syn->string_fields.count("prefs_name"))
                                              ? syn->string_fields["prefs_name"]
                                              : "default";
        return env.RegisterObject(ed);
    }
    if (std::strcmp(mname, "putString") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        const char *val = env.GetStringChars(reinterpret_cast<jstring>(arg1));
        if (syn && key) {
            syn->string_fields[std::string("put_s:") + key] = val ? val : "";
        }
        return obj;
    }
    if (std::strcmp(mname, "putInt") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key) {
            syn->int_fields[std::string("put_i:") + key] = static_cast<int32_t>(arg1);
        }
        return obj;
    }
    if (std::strcmp(mname, "putBoolean") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key) {
            syn->bool_fields[std::string("put_b:") + key] = (arg1 != 0) ? 1 : 0;
        }
        return obj;
    }
    if (std::strcmp(mname, "putFloat") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        double dval = 0.0;
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("movq %%xmm0, %0" : "=r"(dval));
#endif
        if (syn && key) {
            syn->float_fields[std::string("put_f:") + key] = static_cast<float>(dval);
        }
        return obj;
    }
    if (std::strcmp(mname, "remove") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key) {
            syn->int_fields[std::string("rm:") + key] = 1;
        }
        return obj;
    }
    if (std::strcmp(mname, "clear") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            syn->bool_fields["clear"] = 1;
        }
        return obj;
    }
    if (std::strcmp(mname, "getDefaultDisplay") == 0) {
        auto disp = std::make_shared<JniSyntheticObject>();
        disp->class_name = "android/view/Display";
        return env.RegisterObject(disp);
    }
    if (std::strcmp(mname, "getPackageManager") == 0) {
        auto pm = std::make_shared<JniSyntheticObject>();
        pm->class_name = "android/content/pm/PackageManager";
        return env.RegisterObject(pm);
    }
    if (std::strcmp(mname, "getPackageInfo") == 0) {
        const char *req_pkg = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        const auto *active_pkg = PackageManager::Instance().GetActivePackage();
        std::string fallback_pkg =
            env.GetPackageName().empty() ? "com.portage.app" : env.GetPackageName();
        std::string pkg_name = (req_pkg && req_pkg[0] != '\0')
                                   ? req_pkg
                                   : (active_pkg ? active_pkg->manifest.package_name : fallback_pkg);
        std::string ver_name = (active_pkg && !active_pkg->manifest.version_name.empty())
                                   ? active_pkg->manifest.version_name
                                   : "1.0.0";
        int32_t ver_code = active_pkg ? active_pkg->manifest.version_code : 1;

        auto app_info = std::make_shared<JniSyntheticObject>();
        app_info->type = JniObjectType::ApplicationInfo;
        app_info->class_name = "android/content/pm/ApplicationInfo";
        app_info->object_fields["packageName"] = env.CreateString(pkg_name);
        app_info->object_fields["dataDir"] = env.CreateString(env.GetFilesDir());
        app_info->object_fields["nativeLibraryDir"] =
            env.CreateString(active_pkg ? active_pkg->lib_dir.string() : "/data/app/lib/x86_64");
        app_info->object_fields["sourceDir"] =
            env.CreateString(active_pkg ? active_pkg->apk_path.string() : "/data/app/base.apk");
        app_info->int_fields["targetSdkVersion"] =
            active_pkg ? active_pkg->manifest.target_sdk_version : 33;

        auto pi = std::make_shared<JniSyntheticObject>();
        pi->type = JniObjectType::PackageInfo;
        pi->class_name = "android/content/pm/PackageInfo";
        pi->object_fields["packageName"] = env.CreateString(pkg_name);
        pi->object_fields["versionName"] = env.CreateString(ver_name);
        pi->int_fields["versionCode"] = ver_code;
        pi->object_fields["applicationInfo"] = env.RegisterObject(app_info);
        return env.RegisterObject(pi);
    }
    if (std::strcmp(mname, "getApplicationInfo") == 0) {
        const char *req_pkg = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        const auto *active_pkg = PackageManager::Instance().GetActivePackage();
        std::string fallback_pkg =
            env.GetPackageName().empty() ? "com.portage.app" : env.GetPackageName();
        std::string pkg_name = (req_pkg && req_pkg[0] != '\0')
                                   ? req_pkg
                                   : (active_pkg ? active_pkg->manifest.package_name : fallback_pkg);
        auto app_info = std::make_shared<JniSyntheticObject>();
        app_info->type = JniObjectType::ApplicationInfo;
        app_info->class_name = "android/content/pm/ApplicationInfo";
        app_info->object_fields["packageName"] = env.CreateString(pkg_name);
        app_info->object_fields["dataDir"] = env.CreateString(env.GetFilesDir());
        app_info->object_fields["nativeLibraryDir"] =
            env.CreateString(active_pkg ? active_pkg->lib_dir.string() : "/data/app/lib/x86_64");
        app_info->object_fields["sourceDir"] =
            env.CreateString(active_pkg ? active_pkg->apk_path.string() : "/data/app/base.apk");
        app_info->int_fields["targetSdkVersion"] =
            active_pkg ? active_pkg->manifest.target_sdk_version : 33;
        return env.RegisterObject(app_info);
    }
    if (std::strcmp(mname, "getStringExtra") == 0 && obj) {
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        auto syn = env.LookupObject(obj);
        if (syn && key && syn->string_fields.count(key)) {
            return env.CreateString(syn->string_fields[key]);
        }
        return nullptr;
    }
    if (std::strcmp(mname, "getAction") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("action")) {
            return env.CreateString(syn->string_fields["action"]);
        }
        return env.CreateString("android.intent.action.MAIN");
    }
    if (std::strcmp(mname, "loadClass") == 0 || std::strcmp(mname, "findClass") == 0) {
        jstring class_name_str = reinterpret_cast<jstring>(arg0);
        const char *cname = env.GetStringChars(class_name_str);
        if (cname) {
            return const_cast<char *>(cname);
        }
        return const_cast<char *>("java/lang/Object");
    }
    if (std::strcmp(mname, "requestReviewFlow") == 0 ||
        std::strcmp(mname, "launchReviewFlow") == 0 ||
        std::strcmp(mname, "getAppUpdateInfo") == 0 ||
        std::strcmp(mname, "startUpdateFlowForResult") == 0 ||
        std::strcmp(mname, "makeGooglePlayServicesAvailable") == 0) {
        auto task = std::make_shared<JniSyntheticObject>();
        task->class_name = "com/google/android/gms/tasks/Task";
        task->int_fields["isSuccessful"] = 1;
        task->int_fields["isComplete"] = 1;
        return env.RegisterObject(task);
    }
    if (std::strcmp(mname, "getSystemService") == 0) {
        jstring name_str = reinterpret_cast<jstring>(arg0);
        const char *svc = env.GetStringChars(name_str);
        if (svc && std::strcmp(svc, "connectivity") == 0) {
            auto cm = std::make_shared<JniSyntheticObject>();
            cm->class_name = "android/net/ConnectivityManager";
            return env.RegisterObject(cm);
        }
        if (svc && std::strcmp(svc, "power") == 0) {
            auto pm = std::make_shared<JniSyntheticObject>();
            pm->class_name = "android/os/PowerManager";
            return env.RegisterObject(pm);
        }
        if (svc && std::strcmp(svc, "performance_hint") == 0) {
            auto phm = std::make_shared<JniSyntheticObject>();
            phm->class_name = "android/os/PerformanceHintManager";
            return env.RegisterObject(phm);
        }
        if (svc && std::strcmp(svc, "camera") == 0) {
            auto cm = std::make_shared<JniSyntheticObject>();
            cm->class_name = "android/hardware/camera2/CameraManager";
            return env.RegisterObject(cm);
        }
        if (svc && std::strcmp(svc, "location") == 0) {
            auto lm = std::make_shared<JniSyntheticObject>();
            lm->class_name = "android/location/LocationManager";
            return env.RegisterObject(lm);
        }
        if (svc && std::strcmp(svc, "clipboard") == 0) {
            auto cm = std::make_shared<JniSyntheticObject>();
            cm->class_name = "android/content/ClipboardManager";
            return env.RegisterObject(cm);
        }
        if (svc && (std::strcmp(svc, "batterymanager") == 0 || std::strcmp(svc, "battery") == 0)) {
            auto bm = std::make_shared<JniSyntheticObject>();
            bm->class_name = "android/os/BatteryManager";
            return env.RegisterObject(bm);
        }
        if (svc && std::strcmp(svc, "notification") == 0) {
            auto nm = std::make_shared<JniSyntheticObject>();
            nm->class_name = "android/app/NotificationManager";
            return env.RegisterObject(nm);
        }
        if (svc && std::strcmp(svc, "window") == 0) {
            auto wm = std::make_shared<JniSyntheticObject>();
            wm->class_name = "android/view/WindowManager";
            return env.RegisterObject(wm);
        }
        if (svc && std::strcmp(svc, "vibrator") == 0) {
            auto vib = std::make_shared<JniSyntheticObject>();
            vib->type = JniObjectType::Vibrator;
            vib->class_name = "android/os/Vibrator";
            return env.RegisterObject(vib);
        }
    }
    if (std::strcmp(mname, "getPrimaryClip") == 0) {
        auto clip = clipboard::ClipboardManager::Instance().GetPrimaryClip();
        if (clip.items.empty()) {
            return nullptr;
        }
        auto clip_obj = std::make_shared<JniSyntheticObject>();
        clip_obj->class_name = "android/content/ClipData";
        clip_obj->string_fields["label"] = clip.label;
        clip_obj->int_fields["item_count"] = static_cast<int32_t>(clip.items.size());

        auto desc_obj = std::make_shared<JniSyntheticObject>();
        desc_obj->class_name = "android/content/ClipDescription";
        desc_obj->string_fields["label"] = clip.label;
        desc_obj->int_fields["mime_count"] = static_cast<int32_t>(clip.mime_types.size());
        for (size_t i = 0; i < clip.mime_types.size(); ++i) {
            desc_obj->string_fields["mime_" + std::to_string(i)] = clip.mime_types[i];
        }
        clip_obj->object_fields["description"] = env.RegisterObject(desc_obj);

        for (size_t i = 0; i < clip.items.size(); ++i) {
            auto item_obj = std::make_shared<JniSyntheticObject>();
            item_obj->class_name = "android/content/ClipData$Item";
            item_obj->string_fields["text"] = clip.items[i].text;
            item_obj->string_fields["htmlText"] = clip.items[i].html_text;
            item_obj->string_fields["uri"] = clip.items[i].uri;
            clip_obj->object_fields["item_" + std::to_string(i)] = env.RegisterObject(item_obj);
        }
        return env.RegisterObject(clip_obj);
    }
    if (std::strcmp(mname, "getItemAt") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            int idx = static_cast<int>(arg0);
            std::string key = "item_" + std::to_string(idx);
            if (syn->object_fields.count(key)) {
                return syn->object_fields[key];
            }
        }
        return nullptr;
    }
    if (std::strcmp(mname, "getDescription") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->object_fields.count("description")) {
            return syn->object_fields["description"];
        }
        return nullptr;
    }
    if (std::strcmp(mname, "getText") == 0) {
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn) {
                if (syn->class_name == "android/content/ClipData$Item" &&
                    syn->string_fields.count("text")) {
                    return env.CreateString(syn->string_fields["text"]);
                }
                if (syn->class_name == "android/content/ClipboardManager") {
                    return env.CreateString(clipboard::ClipboardManager::Instance().GetText());
                }
            }
        }
        return env.CreateString(clipboard::ClipboardManager::Instance().GetText());
    }
    if (std::strcmp(mname, "getHtmlText") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("htmlText")) {
            return env.CreateString(syn->string_fields["htmlText"]);
        }
        return env.CreateString("");
    }
    if (std::strcmp(mname, "getUri") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("uri")) {
            return env.CreateString(syn->string_fields["uri"]);
        }
        return env.CreateString("");
    }
    if (std::strcmp(mname, "getLabel") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("label")) {
            return env.CreateString(syn->string_fields["label"]);
        }
        return env.CreateString("");
    }
    if (std::strcmp(mname, "getMimeType") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            int idx = static_cast<int>(arg0);
            std::string key = "mime_" + std::to_string(idx);
            if (syn->string_fields.count(key)) {
                return env.CreateString(syn->string_fields[key]);
            }
        }
        return env.CreateString("text/plain");
    }
    if (std::strcmp(mname, "getLastKnownLocation") == 0) {
        jstring prov_str = reinterpret_cast<jstring>(arg0);
        const char *prov = env.GetStringChars(prov_str);
        std::string provider = prov ? prov : "gps";
        auto fix = location::LocationManager::Instance().GetLastKnownLocation(provider);

        auto loc_obj = std::make_shared<JniSyntheticObject>();
        loc_obj->class_name = "android/location/Location";
        loc_obj->string_fields["provider"] = fix.provider;
        loc_obj->double_fields["latitude"] = fix.latitude;
        loc_obj->double_fields["longitude"] = fix.longitude;
        loc_obj->double_fields["altitude"] = fix.altitude;
        loc_obj->float_fields["accuracy"] = fix.accuracy;
        loc_obj->float_fields["speed"] = fix.speed;
        loc_obj->float_fields["bearing"] = fix.bearing;
        loc_obj->long_fields["time"] = fix.time_ms;
        loc_obj->long_fields["elapsedRealtimeNanos"] = fix.elapsed_realtime_ns;
        loc_obj->bool_fields["hasAltitude"] = fix.has_altitude ? 1 : 0;
        loc_obj->bool_fields["hasSpeed"] = fix.has_speed ? 1 : 0;
        loc_obj->bool_fields["hasBearing"] = fix.has_bearing ? 1 : 0;
        loc_obj->bool_fields["hasAccuracy"] = fix.has_accuracy ? 1 : 0;
        return env.RegisterObject(loc_obj);
    }
    if (std::strcmp(mname, "getAllProviders") == 0 || std::strcmp(mname, "getProviders") == 0) {
        auto prov_list = std::make_shared<JniSyntheticObject>();
        prov_list->type = JniObjectType::ObjectArray;
        prov_list->class_name = "java/util/List";
        auto provs = location::LocationManager::Instance().GetAllProviders();
        for (const auto &p : provs) {
            prov_list->object_data.push_back(env.CreateString(p));
        }
        return env.RegisterObject(prov_list);
    }
    if (std::strcmp(mname, "getProvider") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("provider")) {
            return env.CreateString(syn->string_fields["provider"]);
        }
        return env.CreateString("gps");
    }
    if (std::strcmp(mname, "getCameraIdList") == 0) {
        auto cam_ids = camera::CameraManager::Instance().GetCameraIds();
        auto arr_obj = std::make_shared<JniSyntheticObject>();
        arr_obj->type = JniObjectType::ObjectArray;
        arr_obj->class_name = "java/lang/String";
        for (const auto &cid : cam_ids) {
            arr_obj->object_data.push_back(env.CreateString(cid));
        }
        return env.RegisterObject(arr_obj);
    }
    if (std::strcmp(mname, "getCameraCharacteristics") == 0) {
        jstring cid_str = reinterpret_cast<jstring>(arg0);
        const char *cid = env.GetStringChars(cid_str);
        std::string target_id = cid ? cid : "0";
        auto chars = std::make_shared<JniSyntheticObject>();
        chars->class_name = "android/hardware/camera2/CameraCharacteristics";
        const auto *info = camera::CameraManager::Instance().GetDeviceInfo(target_id);
        if (info) {
            chars->int_fields["facing"] = info->facing;
            chars->int_fields["orientation"] = info->orientation;
            chars->int_fields["width"] = info->width;
            chars->int_fields["height"] = info->height;
        } else {
            chars->int_fields["facing"] = 1;
            chars->int_fields["orientation"] = 90;
            chars->int_fields["width"] = 1920;
            chars->int_fields["height"] = 1080;
        }
        return env.RegisterObject(chars);
    }
    if (std::strcmp(mname, "get") == 0 && obj) {
        auto syn_obj = env.LookupObject(obj);
        if (syn_obj && syn_obj->class_name == "android/hardware/camera2/CameraCharacteristics") {
            auto int_val = std::make_shared<JniSyntheticObject>();
            int_val->class_name = "java/lang/Integer";
            int_val->int_fields["value"] = syn_obj->int_fields["facing"];
            return env.RegisterObject(int_val);
        }
    }
    if (std::strcmp(mname, "getKey") == 0) {
        jstring alias_str = reinterpret_cast<jstring>(arg0);
        const char *alias = env.GetStringChars(alias_str);
        auto key_obj = std::make_shared<JniSyntheticObject>();
        key_obj->class_name = "javax/crypto/SecretKey";
        key_obj->string_fields["alias"] = alias ? alias : "";
        return env.RegisterObject(key_obj);
    }
    if (std::strcmp(mname, "generateKey") == 0) {
        std::string alias = "default_aes_key";
        if (obj) {
            auto syn_obj = env.LookupObject(obj);
            if (syn_obj && syn_obj->string_fields.count("alias")) {
                alias = syn_obj->string_fields["alias"];
            }
        }
        security::KeyStoreManager::Instance().GenerateKey(
            alias, security::KeyAlgorithm::Aes, 256,
            security::PurposeEncrypt | security::PurposeDecrypt, security::BlockMode::Gcm,
            security::Padding::None);
        auto key_obj = std::make_shared<JniSyntheticObject>();
        key_obj->class_name = "javax/crypto/SecretKey";
        key_obj->string_fields["alias"] = alias;
        return env.RegisterObject(key_obj);
    }
    if (std::strcmp(mname, "generateKeyPair") == 0) {
        std::string alias = "default_rsa_key";
        std::string alg = "RSA";
        if (obj) {
            auto syn_obj = env.LookupObject(obj);
            if (syn_obj) {
                if (syn_obj->string_fields.count("alias"))
                    alias = syn_obj->string_fields["alias"];
                if (syn_obj->string_fields.count("algorithm"))
                    alg = syn_obj->string_fields["algorithm"];
            }
        }
        if (alg == "EC") {
            security::KeyStoreManager::Instance().GenerateKeyPair(
                alias, security::KeyAlgorithm::Ec, 256,
                security::PurposeSign | security::PurposeVerify, security::Digest::Sha256,
                security::Padding::None);
        } else {
            security::KeyStoreManager::Instance().GenerateKeyPair(
                alias, security::KeyAlgorithm::Rsa, 2048,
                security::PurposeSign | security::PurposeVerify, security::Digest::Sha256,
                security::Padding::Pkcs1);
        }
        auto kp_obj = std::make_shared<JniSyntheticObject>();
        kp_obj->class_name = "java/security/KeyPair";
        kp_obj->string_fields["alias"] = alias;
        return env.RegisterObject(kp_obj);
    }
    if (std::strcmp(mname, "getPrivate") == 0) {
        auto priv_obj = std::make_shared<JniSyntheticObject>();
        priv_obj->class_name = "java/security/PrivateKey";
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn && syn->string_fields.count("alias")) {
                priv_obj->string_fields["alias"] = syn->string_fields["alias"];
            }
        }
        return env.RegisterObject(priv_obj);
    }
    if (std::strcmp(mname, "getPublic") == 0) {
        auto pub_obj = std::make_shared<JniSyntheticObject>();
        pub_obj->class_name = "java/security/PublicKey";
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn && syn->string_fields.count("alias")) {
                pub_obj->string_fields["alias"] = syn->string_fields["alias"];
            }
        }
        return env.RegisterObject(pub_obj);
    }
    if (std::strcmp(mname, "getIV") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->binary_fields.count("iv")) {
            const auto &iv_vec = syn->binary_fields["iv"];
            jbyteArray arr = env.GetJNIEnv()->functions->NewByteArray(
                env.GetJNIEnv(), static_cast<jsize>(iv_vec.size()));
            auto arr_obj = env.LookupObject(arr);
            if (arr_obj) {
                arr_obj->byte_data.assign(iv_vec.begin(), iv_vec.end());
            }
            return arr;
        }
        return nullptr;
    }
    if (std::strcmp(mname, "doFinal") == 0) {
        jbyteArray input_arr = reinterpret_cast<jbyteArray>(arg0);
        std::vector<uint8_t> input_bytes;
        if (input_arr) {
            auto syn_arr = env.LookupObject(input_arr);
            if (syn_arr && syn_arr->type == JniObjectType::ByteArray) {
                input_bytes.assign(syn_arr->byte_data.begin(), syn_arr->byte_data.end());
            }
        }
        std::string alias = "default_aes_key";
        int opmode = 1;
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn) {
                if (syn->string_fields.count("alias"))
                    alias = syn->string_fields["alias"];
                if (syn->int_fields.count("opmode"))
                    opmode = syn->int_fields["opmode"];
            }
        }
        if (opmode == 2) {
            std::vector<uint8_t> iv, tag;
            if (obj) {
                auto syn = env.LookupObject(obj);
                if (syn) {
                    if (syn->binary_fields.count("iv"))
                        iv = syn->binary_fields["iv"];
                    if (syn->binary_fields.count("tag"))
                        tag = syn->binary_fields["tag"];
                }
            }
            auto res = security::KeyStoreManager::Instance().Decrypt(alias, input_bytes, iv, tag);
            jbyteArray arr = env.GetJNIEnv()->functions->NewByteArray(
                env.GetJNIEnv(), static_cast<jsize>(res.data.size()));
            auto arr_obj = env.LookupObject(arr);
            if (arr_obj) {
                arr_obj->byte_data.assign(res.data.begin(), res.data.end());
            }
            return arr;
        } else {
            auto res = security::KeyStoreManager::Instance().Encrypt(alias, input_bytes);
            if (obj) {
                auto syn = env.LookupObject(obj);
                if (syn) {
                    syn->binary_fields["iv"] = res.iv;
                    syn->binary_fields["tag"] = res.tag;
                }
            }
            jbyteArray arr = env.GetJNIEnv()->functions->NewByteArray(
                env.GetJNIEnv(), static_cast<jsize>(res.data.size()));
            auto arr_obj = env.LookupObject(arr);
            if (arr_obj) {
                arr_obj->byte_data.assign(res.data.begin(), res.data.end());
            }
            return arr;
        }
    }
    if (std::strcmp(mname, "sign") == 0) {
        std::string alias = "default_rsa_key";
        std::vector<uint8_t> data;
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn) {
                if (syn->string_fields.count("alias"))
                    alias = syn->string_fields["alias"];
                if (syn->binary_fields.count("data")) {
                    data = syn->binary_fields["data"];
                    syn->binary_fields["data"].clear();
                }
            }
        }
        std::vector<uint8_t> sig;
        security::KeyStoreManager::Instance().Sign(alias, data, sig);
        jbyteArray arr = env.GetJNIEnv()->functions->NewByteArray(env.GetJNIEnv(),
                                                                  static_cast<jsize>(sig.size()));
        auto arr_obj = env.LookupObject(arr);
        if (arr_obj) {
            arr_obj->byte_data.assign(sig.begin(), sig.end());
        }
        return arr;
    }
    if (std::strcmp(mname, "createHintSession") == 0) {
        auto sess = std::make_shared<JniSyntheticObject>();
        sess->class_name = "android/os/PerformanceHintManager$Session";
        return env.RegisterObject(sess);
    }
    if (std::strcmp(mname, "getActiveNetwork") == 0) {
        auto net = std::make_shared<JniSyntheticObject>();
        net->class_name = "android/net/Network";
        net_handle_t h = ndk::NET_HANDLE_DEFAULT;
        ndk::MultinetworkManager::Instance().GetProcNetwork(&h);
        net->long_fields["networkHandle"] = static_cast<int64_t>(h);
        return env.RegisterObject(net);
    }
    if (std::strcmp(mname, "getNetworkCapabilities") == 0) {
        auto caps = std::make_shared<JniSyntheticObject>();
        caps->class_name = "android/net/NetworkCapabilities";
        caps->int_fields["downstreamBandwidth"] = 100000;
        caps->int_fields["upstreamBandwidth"] = 50000;
        return env.RegisterObject(caps);
    }
    if (std::strcmp(mname, "getLinkProperties") == 0) {
        auto lp = std::make_shared<JniSyntheticObject>();
        lp->class_name = "android/net/LinkProperties";
        lp->string_fields["interfaceName"] = "portage0";
        return env.RegisterObject(lp);
    }
    if (std::strcmp(mname, "getActiveNetworkInfo") == 0) {
        auto ni = std::make_shared<JniSyntheticObject>();
        ni->class_name = "android/net/NetworkInfo";
        ni->int_fields["type"] = 1; // TYPE_WIFI
        ni->string_fields["typeName"] = "WIFI";
        return env.RegisterObject(ni);
    }
    if (std::strcmp(mname, "getInterfaceName") == 0) {
        if (obj) {
            auto syn = env.LookupObject(obj);
            if (syn && syn->string_fields.contains("interfaceName")) {
                return env.CreateString(syn->string_fields["interfaceName"]);
            }
        }
        return env.CreateString("portage0");
    }
    if (std::strcmp(mname, "getTypeName") == 0) {
        return env.CreateString("WIFI");
    }
    if (std::strcmp(mname, "newWakeLock") == 0) {
        int32_t flags = static_cast<int32_t>(arg0);
        jstring tag_str = reinterpret_cast<jstring>(arg1);
        const char *tag = env.GetStringChars(tag_str);
        uint32_t id =
            power::PowerManager::Instance().CreateWakeLock(flags, tag ? tag : "synthetic_lock");
        auto wl = std::make_shared<JniSyntheticObject>();
        wl->class_name = "android/os/PowerManager$WakeLock";
        wl->int_fields["wakelock_id"] = static_cast<int32_t>(id);
        wl->int_fields["flags"] = flags;
        wl->string_fields["tag"] = tag ? tag : "";
        return env.RegisterObject(wl);
    }
    if (std::strcmp(mname, "registerReceiver") == 0) {
        auto intent = std::make_shared<JniSyntheticObject>();
        intent->class_name = "android/content/Intent";
        intent->string_fields["action"] = "android.intent.action.BATTERY_CHANGED";
        auto state = power::PowerManager::Instance().GetBatteryState();
        intent->int_fields["level"] = state.capacity_percent;
        intent->int_fields["scale"] = 100;
        intent->int_fields["status"] = state.status;
        intent->int_fields["health"] = state.health;
        intent->int_fields["plugged"] = state.plugged;
        intent->int_fields["voltage"] = state.voltage_mv;
        intent->int_fields["temperature"] = state.temperature_deci_c;
        intent->int_fields["present"] = state.is_present ? 1 : 0;
        return env.RegisterObject(intent);
    }
    if (std::strcmp(mname, "getNotificationChannel") == 0) {
        jstring id_str = reinterpret_cast<jstring>(arg0);
        const char *id = env.GetStringChars(id_str);
        auto opt =
            notification::NotificationManager::Instance().GetNotificationChannel(id ? id : "");
        if (!opt) {
            return nullptr;
        }
        auto ch_obj = std::make_shared<JniSyntheticObject>();
        ch_obj->class_name = "android/app/NotificationChannel";
        ch_obj->string_fields["id"] = opt->id;
        ch_obj->string_fields["name"] = opt->name;
        ch_obj->string_fields["description"] = opt->description;
        ch_obj->int_fields["importance"] = opt->importance;
        return env.RegisterObject(ch_obj);
    }
    if (std::strcmp(mname, "build") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            auto notif = std::make_shared<JniSyntheticObject>();
            notif->class_name = "android/app/Notification";
            notif->string_fields["title"] = syn->string_fields["title"];
            notif->string_fields["text"] = syn->string_fields["text"];
            notif->string_fields["channel_id"] = syn->string_fields.count("channel_id")
                                                     ? syn->string_fields["channel_id"]
                                                     : "default";
            notif->int_fields["small_icon"] = syn->int_fields["small_icon"];
            notif->int_fields["flags"] = syn->int_fields["flags"];
            notif->int_fields["progress_max"] = syn->int_fields["progress_max"];
            notif->int_fields["progress_current"] = syn->int_fields["progress_current"];
            notif->bool_fields["progress_indeterminate"] =
                syn->bool_fields["progress_indeterminate"];
            return env.RegisterObject(notif);
        }
    }
    if (std::strcmp(mname, "setContentTitle") == 0 && obj) {
        jstring str = reinterpret_cast<jstring>(arg0);
        const char *c = env.GetStringChars(str);
        auto syn = env.LookupObject(obj);
        if (syn && c)
            syn->string_fields["title"] = c;
        return obj;
    }
    if (std::strcmp(mname, "setContentText") == 0 && obj) {
        jstring str = reinterpret_cast<jstring>(arg0);
        const char *c = env.GetStringChars(str);
        auto syn = env.LookupObject(obj);
        if (syn && c)
            syn->string_fields["text"] = c;
        return obj;
    }
    if (std::strcmp(mname, "setSmallIcon") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn)
            syn->int_fields["small_icon"] = static_cast<int32_t>(arg0);
        return obj;
    }
    if (std::strcmp(mname, "setProgress") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            syn->int_fields["progress_max"] = static_cast<int32_t>(arg0);
            syn->int_fields["progress_current"] = static_cast<int32_t>(arg1);
            uintptr_t arg2 = 0;
#if defined(__x86_64__) || defined(_M_X64)
            __asm__ volatile("movq %%r9, %0" : "=r"(arg2));
#endif
            syn->bool_fields["progress_indeterminate"] = (arg2 != 0);
        }
        return obj;
    }
    if (std::strcmp(mname, "setOngoing") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            if (arg0)
                syn->int_fields["flags"] |= notification::FLAG_ONGOING_EVENT;
            else
                syn->int_fields["flags"] &= ~notification::FLAG_ONGOING_EVENT;
        }
        return obj;
    }
    if (std::strcmp(mname, "setAutoCancel") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn) {
            if (arg0)
                syn->int_fields["flags"] |= notification::FLAG_AUTO_CANCEL;
            else
                syn->int_fields["flags"] &= ~notification::FLAG_AUTO_CANCEL;
        }
        return obj;
    }
    if (std::strcmp(mname, "getId") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("id")) {
            return env.CreateString(syn->string_fields["id"]);
        }
        return env.CreateString("default");
    }
    if (std::strcmp(mname, "getName") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("name")) {
            return env.CreateString(syn->string_fields["name"]);
        }
        return env.CreateString("Default");
    }
    if (std::strcmp(mname, "getDescription") == 0 && obj) {
        auto syn = env.LookupObject(obj);
        if (syn && syn->string_fields.count("description")) {
            return env.CreateString(syn->string_fields["description"]);
        }
    }

    return nullptr;
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethod(JNIEnv *jni_env, jobject obj,
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
    return DispatchObjectMethod(jni_env, obj, methodID, arg0, arg1, arg2);
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethodV(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, va_list)
{
    return DispatchObjectMethod(env, obj, methodID, 0, 0, 0);
}

static __attribute__((sysv_abi)) jobject Env_CallObjectMethodA(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, const void *args)
{
    const auto *jv = static_cast<const uint64_t *>(args);
    return DispatchObjectMethod(env, obj, methodID, jv ? jv[0] : 0, jv ? jv[1] : 0,
                                jv ? jv[2] : 0);
}

static __attribute__((sysv_abi)) jboolean Env_CallBooleanMethod(JNIEnv *, jobject obj,
                                                                jmethodID methodID, ...)
{
    if (!methodID) {
        return JNI_TRUE;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    uintptr_t arg0 = 0;
    uintptr_t arg1 = 0;
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
    __asm__ volatile("movq %%r8, %0" : "=r"(arg1));
#endif
    if (std::strcmp(mname, "isInteractive") == 0) {
        return power::PowerManager::Instance().IsInteractive() ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "isPowerSaveMode") == 0) {
        return power::PowerManager::Instance().IsPowerSaveMode() ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "isDeviceIdleMode") == 0) {
        return power::PowerManager::Instance().IsDeviceIdleMode() ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "isHeld") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count("wakelock_id")) {
                uint32_t id = static_cast<uint32_t>(syn->int_fields["wakelock_id"]);
                return power::PowerManager::Instance().IsWakeLockHeld(id) ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_FALSE;
    }
    if (std::strcmp(mname, "areNotificationsEnabled") == 0) {
        return notification::NotificationManager::Instance().AreNotificationsEnabled() ? JNI_TRUE
                                                                                       : JNI_FALSE;
    }
    if (std::strcmp(mname, "isCharging") == 0) {
        return power::PowerManager::Instance().IsCharging() ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "getBooleanExtra") == 0) {
        jstring name_str = reinterpret_cast<jstring>(arg0);
        jboolean def_val = static_cast<jboolean>(arg1);
        const char *key = JniEnvironment::Instance().GetStringChars(name_str);
        if (obj && key) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                if (syn->int_fields.count(key)) {
                    return syn->int_fields[key] != 0 ? JNI_TRUE : JNI_FALSE;
                }
                if (syn->bool_fields.count(key)) {
                    return syn->bool_fields[key] ? JNI_TRUE : JNI_FALSE;
                }
            }
        }
        return def_val;
    }
    if (std::strcmp(mname, "containsAlias") == 0) {
        jstring alias_str = reinterpret_cast<jstring>(arg0);
        const char *alias = JniEnvironment::Instance().GetStringChars(alias_str);
        return security::KeyStoreManager::Instance().ContainsAlias(alias ? alias : "") ? JNI_TRUE
                                                                                       : JNI_FALSE;
    }
    if (std::strcmp(mname, "isProviderEnabled") == 0) {
        jstring prov_str = reinterpret_cast<jstring>(arg0);
        const char *prov = JniEnvironment::Instance().GetStringChars(prov_str);
        return location::LocationManager::Instance().IsProviderEnabled(prov ? prov : "gps")
                   ? JNI_TRUE
                   : JNI_FALSE;
    }
    if (std::strcmp(mname, "hasAltitude") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->bool_fields.count("hasAltitude")) {
                return syn->bool_fields["hasAltitude"] ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasSpeed") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->bool_fields.count("hasSpeed")) {
                return syn->bool_fields["hasSpeed"] ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasBearing") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->bool_fields.count("hasBearing")) {
                return syn->bool_fields["hasBearing"] ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasAccuracy") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->bool_fields.count("hasAccuracy")) {
                return syn->bool_fields["hasAccuracy"] ? JNI_TRUE : JNI_FALSE;
            }
        }
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasPrimaryClip") == 0) {
        return clipboard::ClipboardManager::Instance().HasPrimaryClip() ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "hasMimeType") == 0) {
        jstring mime_str = reinterpret_cast<jstring>(arg0);
        const char *mime = JniEnvironment::Instance().GetStringChars(mime_str);
        if (obj && mime) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                int count = syn->int_fields.count("mime_count") ? syn->int_fields["mime_count"] : 0;
                for (int i = 0; i < count; ++i) {
                    std::string key = "mime_" + std::to_string(i);
                    if (syn->string_fields.count(key) && syn->string_fields[key] == mime) {
                        return JNI_TRUE;
                    }
                }
            }
        }
        return JNI_FALSE;
    }
    if (std::strcmp(mname, "verify") == 0) {
        jbyteArray sig_arr = reinterpret_cast<jbyteArray>(arg0);
        std::vector<uint8_t> sig_bytes;
        if (sig_arr) {
            auto syn_arr = JniEnvironment::Instance().LookupObject(sig_arr);
            if (syn_arr && syn_arr->type == JniObjectType::ByteArray) {
                sig_bytes.assign(syn_arr->byte_data.begin(), syn_arr->byte_data.end());
            }
        }
        std::string alias = "default_rsa_key";
        std::vector<uint8_t> data;
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                if (syn->string_fields.count("alias"))
                    alias = syn->string_fields["alias"];
                if (syn->binary_fields.count("data")) {
                    data = syn->binary_fields["data"];
                    syn->binary_fields["data"].clear();
                }
            }
        }
        auto status = security::KeyStoreManager::Instance().Verify(alias, data, sig_bytes);
        return (status == security::KeyStoreStatus::Success) ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "commit") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->class_name == "android/content/SharedPreferences$Editor") {
            return CommitSharedPrefsEditor(JniEnvironment::Instance(), *syn) ? JNI_TRUE : JNI_FALSE;
        }
    }
    if (std::strcmp(mname, "contains") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && syn->class_name == "android/content/SharedPreferences" && key) {
            const auto &pname = syn->string_fields.count("prefs_name")
                                    ? syn->string_fields["prefs_name"]
                                    : "default";
            return env.GetSharedPrefsMap(pname).contains(key) ? JNI_TRUE : JNI_FALSE;
        }
    }
    if (std::strcmp(mname, "containsKey") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key &&
            (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
            bool has = syn->bundle_strings.contains(key) || syn->bundle_ints.contains(key) ||
                       syn->bundle_bools.contains(key);
            return has ? JNI_TRUE : JNI_FALSE;
        }
        return JNI_FALSE;
    }
    if (std::strcmp(mname, "hasVibrator") == 0) {
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "getBoolean") == 0) {
        if (obj) {
            auto &env = JniEnvironment::Instance();
            auto syn = env.LookupObject(obj);
            if (syn &&
                (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
                const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                if (key) {
                    auto it = syn->bundle_bools.find(key);
                    if (it != syn->bundle_bools.end()) {
                        return it->second ? JNI_TRUE : JNI_FALSE;
                    }
                }
                return (arg1 != 0) ? JNI_TRUE : JNI_FALSE;
            }
            if (syn && syn->class_name == "android/content/SharedPreferences") {
                const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                const auto &pname = syn->string_fields.count("prefs_name")
                                        ? syn->string_fields["prefs_name"]
                                        : "default";
                const auto &store = env.GetSharedPrefsMap(pname);
                if (key) {
                    auto it = store.find(key);
                    if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Bool) {
                        return it->second.bool_val ? JNI_TRUE : JNI_FALSE;
                    }
                }
                return (arg1 != 0) ? JNI_TRUE : JNI_FALSE;
            }
        }
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
    if (std::strcmp(mname, "isSuccessful") == 0 || std::strcmp(mname, "isComplete") == 0) {
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasCapability") == 0) {
        return JNI_TRUE;
    }
    if (std::strcmp(mname, "hasTransport") == 0) {
        int transport = static_cast<int>(arg0);
        return (transport == 1 || transport == 3) ? JNI_TRUE : JNI_FALSE;
    }
    if (std::strcmp(mname, "isConnected") == 0 ||
        std::strcmp(mname, "isConnectedOrConnecting") == 0) {
        return JNI_TRUE;
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
            if (std::strcmp(mname, "getInt") == 0 && obj) {
                auto &env = JniEnvironment::Instance();
                auto syn = env.LookupObject(obj);
                if (syn && syn->class_name == "android/content/SharedPreferences") {
                    const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                    const auto &pname = syn->string_fields.count("prefs_name")
                                            ? syn->string_fields["prefs_name"]
                                            : "default";
                    const auto &store = env.GetSharedPrefsMap(pname);
                    if (key) {
                        auto it = store.find(key);
                        if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Int) {
                            return it->second.int_val;
                        }
                    }
                    return static_cast<jint>(arg1);
                }
            }
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
    if (std::strcmp(mname, "getInt") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        if (syn && (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
            const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
            if (key) {
                auto it = syn->bundle_ints.find(key);
                if (it != syn->bundle_ints.end()) {
                    return it->second;
                }
            }
            return static_cast<jint>(arg1);
        }
        if (syn && syn->class_name == "android/content/SharedPreferences") {
            const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
            const auto &pname = syn->string_fields.count("prefs_name")
                                    ? syn->string_fields["prefs_name"]
                                    : "default";
            const auto &store = env.GetSharedPrefsMap(pname);
            if (key) {
                auto it = store.find(key);
                if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Int) {
                    return it->second.int_val;
                }
            }
            return static_cast<jint>(arg1);
        }
    }
    if (std::strcmp(mname, "getWidth") == 0) {
        return JniEnvironment::Instance().GetDisplayWidth();
    }
    if (std::strcmp(mname, "getIntProperty") == 0) {
        int32_t prop_id = static_cast<int32_t>(arg0);
        return power::PowerManager::Instance().GetBatteryProperty(prop_id);
    }
    if (std::strcmp(mname, "getImportance") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->int_fields.count("importance")) {
            return syn->int_fields["importance"];
        }
        return notification::IMPORTANCE_DEFAULT;
    }
    if (std::strcmp(mname, "getDuration") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->int_fields.count("duration")) {
            return syn->int_fields["duration"];
        }
        return 0;
    }
    if (std::strcmp(mname, "getIntExtra") == 0) {
        jstring name_str = reinterpret_cast<jstring>(arg0);
        int32_t def_val = static_cast<int32_t>(arg1);
        const char *key = JniEnvironment::Instance().GetStringChars(name_str);
        if (obj && key) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count(key)) {
                return syn->int_fields[key];
            }
        }
        return def_val;
    }
    if (std::strcmp(mname, "getHeight") == 0) {
        return JniEnvironment::Instance().GetDisplayHeight();
    }
    if (std::strcmp(mname, "getRotation") == 0) {
        return 0;
    }
    if (std::strcmp(mname, "getSystemUiVisibility") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count("systemUiVisibility")) {
                return syn->int_fields["systemUiVisibility"];
            }
        }
        return 0;
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
    if (std::strcmp(mname, "isGooglePlayServicesAvailable") == 0) {
        return 0; // ConnectionResult.SUCCESS
    }
    if (std::strcmp(mname, "intValue") == 0 && obj) {
        auto syn_obj = JniEnvironment::Instance().LookupObject(obj);
        if (syn_obj) {
            auto it = syn_obj->int_fields.find("value");
            if (it != syn_obj->int_fields.end()) {
                return it->second;
            }
        }
    }
    if (std::strcmp(mname, "getItemCount") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->int_fields.count("item_count")) {
            return syn->int_fields["item_count"];
        }
        return 0;
    }
    if (std::strcmp(mname, "getMimeTypeCount") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->int_fields.count("mime_count")) {
            return syn->int_fields["mime_count"];
        }
        return 0;
    }
    if (std::strcmp(mname, "getType") == 0) {
        return 1; // TYPE_WIFI
    }
    if (std::strcmp(mname, "getLinkDownstreamBandwidthKbps") == 0) {
        return 100000;
    }
    if (std::strcmp(mname, "getLinkUpstreamBandwidthKbps") == 0) {
        return 50000;
    }
    if (std::strcmp(mname, "getCurrentThermalStatus") == 0) {
        return static_cast<jint>(
            portage::ndk::ThermalManager::Instance().GetCurrentThermalStatus());
    }
    if (std::strcmp(mname, "reportActualWorkDuration") == 0 ||
        std::strcmp(mname, "updateTargetWorkDuration") == 0) {
        return 0;
    }
    if (std::strcmp(mname, "getState") == 0) {
        if (obj) {
            auto synth = JniEnvironment::Instance().LookupObject(obj);
            if (synth) {
                auto it = synth->int_fields.find("getState");
                if (it != synth->int_fields.end()) {
                    return it->second;
                }
            }
        }
        return 1;
    }
    if (std::strcmp(mname, "getRecordingState") == 0) {
        if (obj) {
            auto synth = JniEnvironment::Instance().LookupObject(obj);
            if (synth) {
                auto it = synth->int_fields.find("getRecordingState");
                if (it != synth->int_fields.end()) {
                    return it->second;
                }
            }
        }
        return audio::AudioManager::Instance().GetCaptureStats().is_running ? 3 : 1;
    }
    if (std::strcmp(mname, "getSampleRate") == 0) {
        return static_cast<jint>(audio::AudioManager::Instance().GetCaptureStats().sample_rate);
    }
    if (std::strcmp(mname, "getChannelCount") == 0) {
        return static_cast<jint>(audio::AudioManager::Instance().GetCaptureStats().channels);
    }
    if (std::strcmp(mname, "getAudioFormat") == 0) {
        return 2;
    }
    if (std::strcmp(mname, "read") == 0) {
        jobject arr_obj = reinterpret_cast<jobject>(arg0);
        size_t offset = static_cast<size_t>(arg1);
        size_t size = static_cast<size_t>(arg2);
        if (size == 0) {
            return 0;
        }

        auto synth = JniEnvironment::Instance().LookupObject(arr_obj);
        if (synth) {
            if (synth->type == JniObjectType::ShortArray &&
                synth->short_data.size() >= offset + size) {
                int64_t bytes_read = audio::AudioManager::Instance().ReadPcmBytes(
                    reinterpret_cast<uint8_t *>(synth->short_data.data() + offset), size * 2);
                return static_cast<jint>(bytes_read / 2);
            }
            if (!synth->byte_data.empty() && synth->byte_data.size() >= offset + size) {
                int64_t bytes_read = audio::AudioManager::Instance().ReadPcmBytes(
                    synth->byte_data.data() + offset, size);
                return static_cast<jint>(bytes_read);
            }
            return 0;
        }
        if (arr_obj) {
            int64_t bytes_read = audio::AudioManager::Instance().ReadPcmBytes(
                reinterpret_cast<uint8_t *>(arr_obj) + offset, size);
            return static_cast<jint>(bytes_read);
        }
        return 0;
    }
    if (std::strcmp(mname, "getFloat") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        if (syn && syn->class_name == "android/content/SharedPreferences") {
            uintptr_t a0 = 0;
            double def_val = 0.0;
#if defined(__x86_64__) || defined(_M_X64)
            __asm__ volatile("movq %%rcx, %0" : "=r"(a0));
            __asm__ volatile("movq %%xmm0, %0" : "=r"(def_val));
#endif
            const char *key = env.GetStringChars(reinterpret_cast<jstring>(a0));
            const auto &pname = syn->string_fields.count("prefs_name")
                                    ? syn->string_fields["prefs_name"]
                                    : "default";
            const auto &store = env.GetSharedPrefsMap(pname);
            if (key) {
                auto it = store.find(key);
                if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Float) {
                    return it->second.float_val;
                }
            }
            return static_cast<jfloat>(def_val);
        }
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

static __attribute__((sysv_abi)) jlong Env_CallLongMethod(JNIEnv *, jobject obj, jmethodID methodID,
                                                          ...)
{
    if (!methodID) {
        return 0;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "getNetworkHandle") == 0) {
        net_handle_t h = ndk::NET_HANDLE_DEFAULT;
        ndk::MultinetworkManager::Instance().GetProcNetwork(&h);
        return static_cast<jlong>(h);
    }
    if (std::strcmp(mname, "getFloat") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        if (syn && syn->class_name == "android/content/SharedPreferences") {
            uintptr_t a0 = 0;
            double def_val = 0.0;
#if defined(__x86_64__) || defined(_M_X64)
            __asm__ volatile("movq %%rcx, %0" : "=r"(a0));
            __asm__ volatile("movq %%xmm0, %0" : "=r"(def_val));
#endif
            const char *key = env.GetStringChars(reinterpret_cast<jstring>(a0));
            const auto &pname = syn->string_fields.count("prefs_name")
                                    ? syn->string_fields["prefs_name"]
                                    : "default";
            const auto &store = env.GetSharedPrefsMap(pname);
            if (key) {
                auto it = store.find(key);
                if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Float) {
                    return it->second.float_val;
                }
            }
            return static_cast<jfloat>(def_val);
        }
    }
    if (obj) {
        auto synth = JniEnvironment::Instance().LookupObject(obj);
        if (synth) {
            if (std::strcmp(mname, "getTime") == 0 && synth->long_fields.count("time")) {
                return synth->long_fields["time"];
            }
            if (std::strcmp(mname, "getElapsedRealtimeNanos") == 0 &&
                synth->long_fields.count("elapsedRealtimeNanos")) {
                return synth->long_fields["elapsedRealtimeNanos"];
            }
            auto it = synth->long_fields.find(mname);
            if (it != synth->long_fields.end()) {
                return it->second;
            }
        }
    }
    return 0;
}

static __attribute__((sysv_abi)) jlong Env_CallLongMethodV(JNIEnv *env, jobject obj,
                                                           jmethodID methodID, va_list)
{
    return Env_CallLongMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jlong Env_CallLongMethodA(JNIEnv *env, jobject obj,
                                                           jmethodID methodID, const void *)
{
    return Env_CallLongMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethod(JNIEnv *, jobject obj,
                                                            jmethodID methodID, ...)
{
    if (!methodID) {
        return 0.0f;
    }
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "getThermalHeadroom") == 0) {
        uintptr_t arg0 = 0;
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("movq %%rcx, %0" : "=r"(arg0));
#endif
        return portage::ndk::ThermalManager::Instance().GetThermalHeadroom(static_cast<int>(arg0));
    }
    if (std::strcmp(mname, "getFloat") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        if (syn && syn->class_name == "android/content/SharedPreferences") {
            uintptr_t a0 = 0;
            double def_val = 0.0;
#if defined(__x86_64__) || defined(_M_X64)
            __asm__ volatile("movq %%rcx, %0" : "=r"(a0));
            __asm__ volatile("movq %%xmm0, %0" : "=r"(def_val));
#endif
            const char *key = env.GetStringChars(reinterpret_cast<jstring>(a0));
            const auto &pname = syn->string_fields.count("prefs_name")
                                    ? syn->string_fields["prefs_name"]
                                    : "default";
            const auto &store = env.GetSharedPrefsMap(pname);
            if (key) {
                auto it = store.find(key);
                if (it != store.end() && it->second.kind == JniSharedPrefValue::Kind::Float) {
                    return it->second.float_val;
                }
            }
            return static_cast<jfloat>(def_val);
        }
    }
    if (std::strcmp(mname, "getRefreshRate") == 0) {
        return 60.0f;
    }
    if (obj) {
        auto synth = JniEnvironment::Instance().LookupObject(obj);
        if (synth) {
            if (std::strcmp(mname, "getAccuracy") == 0 && synth->float_fields.count("accuracy")) {
                return synth->float_fields["accuracy"];
            }
            if (std::strcmp(mname, "getSpeed") == 0 && synth->float_fields.count("speed")) {
                return synth->float_fields["speed"];
            }
            if (std::strcmp(mname, "getBearing") == 0 && synth->float_fields.count("bearing")) {
                return synth->float_fields["bearing"];
            }
            auto it = synth->float_fields.find(mname);
            if (it != synth->float_fields.end()) {
                return it->second;
            }
        }
    }
    return 0.0f;
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethodV(JNIEnv *env, jobject obj,
                                                             jmethodID methodID, va_list)
{
    return Env_CallFloatMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jfloat Env_CallFloatMethodA(JNIEnv *env, jobject obj,
                                                             jmethodID methodID, const void *)
{
    return Env_CallFloatMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethod(JNIEnv *, jobject obj,
                                                              jmethodID methodID, ...)
{
    if (obj && methodID) {
        auto synth = JniEnvironment::Instance().LookupObject(obj);
        if (synth) {
            const char *mname = reinterpret_cast<const char *>(methodID);
            if (std::strcmp(mname, "getLatitude") == 0 && synth->double_fields.count("latitude")) {
                return synth->double_fields["latitude"];
            }
            if (std::strcmp(mname, "getLongitude") == 0 &&
                synth->double_fields.count("longitude")) {
                return synth->double_fields["longitude"];
            }
            if (std::strcmp(mname, "getAltitude") == 0 && synth->double_fields.count("altitude")) {
                return synth->double_fields["altitude"];
            }
            auto it = synth->double_fields.find(mname);
            if (it != synth->double_fields.end()) {
                return it->second;
            }
        }
    }
    return 0.0;
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethodV(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, va_list)
{
    return Env_CallDoubleMethod(env, obj, methodID);
}

static __attribute__((sysv_abi)) jdouble Env_CallDoubleMethodA(JNIEnv *env, jobject obj,
                                                               jmethodID methodID, const void *)
{
    return Env_CallDoubleMethod(env, obj, methodID);
}

static void DispatchVoidMethod(jobject obj, jmethodID methodID)
{
    if (methodID) {
        const char *mname = reinterpret_cast<const char *>(methodID);
        if (std::strcmp(mname, "startRecording") == 0) {
            audio::AudioManager::Instance().StartCapture();
            if (obj) {
                auto synth = JniEnvironment::Instance().LookupObject(obj);
                if (synth) {
                    synth->int_fields["getRecordingState"] = 3;
                }
            }
            return;
        }
        if (std::strcmp(mname, "stop") == 0) {
            audio::AudioManager::Instance().StopCapture();
            if (obj) {
                auto synth = JniEnvironment::Instance().LookupObject(obj);
                if (synth) {
                    synth->int_fields["getRecordingState"] = 1;
                }
            }
            return;
        }
        if (std::strcmp(mname, "release") == 0) {
            audio::AudioManager::Instance().CloseCaptureStream();
            if (obj) {
                auto synth = JniEnvironment::Instance().LookupObject(obj);
                if (synth) {
                    synth->int_fields["getState"] = 0;
                    synth->int_fields["getRecordingState"] = 1;
                }
            }
            return;
        }
    }
}

static __attribute__((sysv_abi)) void Env_CallVoidMethod(JNIEnv *, jobject obj, jmethodID methodID,
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
    if (!methodID)
        return;
    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "init") == 0 || std::strcmp(mname, "initialize") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                syn->int_fields["opmode"] = static_cast<int32_t>(arg0);
                jobject key_obj = reinterpret_cast<jobject>(arg1);
                if (key_obj) {
                    auto key_syn = JniEnvironment::Instance().LookupObject(key_obj);
                    if (key_syn && key_syn->string_fields.count("alias")) {
                        syn->string_fields["alias"] = key_syn->string_fields["alias"];
                    }
                }
            }
        }
        return;
    }
    if (std::strcmp(mname, "initSign") == 0 || std::strcmp(mname, "initVerify") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                syn->binary_fields["data"].clear();
                jobject key_obj = reinterpret_cast<jobject>(arg0);
                if (key_obj) {
                    auto key_syn = JniEnvironment::Instance().LookupObject(key_obj);
                    if (key_syn && key_syn->string_fields.count("alias")) {
                        syn->string_fields["alias"] = key_syn->string_fields["alias"];
                    }
                }
            }
        }
        return;
    }
    if (std::strcmp(mname, "update") == 0) {
        jbyteArray data_arr = reinterpret_cast<jbyteArray>(arg0);
        if (obj && data_arr) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            auto arr_syn = JniEnvironment::Instance().LookupObject(data_arr);
            if (syn && arr_syn && arr_syn->type == JniObjectType::ByteArray) {
                auto &buf = syn->binary_fields["data"];
                buf.insert(buf.end(), arr_syn->byte_data.begin(), arr_syn->byte_data.end());
            }
        }
        return;
    }
    if (std::strcmp(mname, "deleteEntry") == 0) {
        jstring alias_str = reinterpret_cast<jstring>(arg0);
        const char *alias = JniEnvironment::Instance().GetStringChars(alias_str);
        if (alias) {
            security::KeyStoreManager::Instance().DeleteKey(alias);
        }
        return;
    }
    if (std::strcmp(mname, "setPrimaryClip") == 0) {
        jobject clip_arg = reinterpret_cast<jobject>(arg0);
        if (clip_arg) {
            auto clip_syn = JniEnvironment::Instance().LookupObject(clip_arg);
            if (clip_syn) {
                clipboard::ClipData clip;
                clip.label = clip_syn->string_fields.count("label")
                                 ? clip_syn->string_fields["label"]
                                 : "text";
                int count = clip_syn->int_fields.count("item_count")
                                ? clip_syn->int_fields["item_count"]
                                : 0;
                for (int i = 0; i < count; ++i) {
                    std::string key = "item_" + std::to_string(i);
                    if (clip_syn->object_fields.count(key)) {
                        auto item_syn =
                            JniEnvironment::Instance().LookupObject(clip_syn->object_fields[key]);
                        if (item_syn) {
                            clipboard::ClipItem it;
                            if (item_syn->string_fields.count("text")) {
                                it.text = item_syn->string_fields["text"];
                            }
                            if (item_syn->string_fields.count("htmlText")) {
                                it.html_text = item_syn->string_fields["htmlText"];
                            }
                            if (item_syn->string_fields.count("uri")) {
                                it.uri = item_syn->string_fields["uri"];
                            }
                            clip.items.push_back(it);
                        }
                    }
                }
                if (clip_syn->object_fields.count("description")) {
                    auto desc_syn = JniEnvironment::Instance().LookupObject(
                        clip_syn->object_fields["description"]);
                    if (desc_syn) {
                        int mcount = desc_syn->int_fields.count("mime_count")
                                         ? desc_syn->int_fields["mime_count"]
                                         : 0;
                        for (int i = 0; i < mcount; ++i) {
                            std::string mkey = "mime_" + std::to_string(i);
                            if (desc_syn->string_fields.count(mkey)) {
                                clip.mime_types.push_back(desc_syn->string_fields[mkey]);
                            }
                        }
                    }
                }
                if (clip.mime_types.empty()) {
                    clip.mime_types.push_back("text/plain");
                }
                clipboard::ClipboardManager::Instance().SetPrimaryClip(clip);
            }
        }
        return;
    }
    if (std::strcmp(mname, "clearPrimaryClip") == 0) {
        clipboard::ClipboardManager::Instance().ClearPrimaryClip();
        return;
    }
    if (std::strcmp(mname, "setText") == 0) {
        jstring text_str = reinterpret_cast<jstring>(arg0);
        if (text_str) {
            const char *txt = JniEnvironment::Instance().GetStringChars(text_str);
            if (txt) {
                clipboard::ClipboardManager::Instance().SetText(txt);
            }
        }
        return;
    }
    if (std::strcmp(mname, "acquire") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count("wakelock_id")) {
                uint32_t id = static_cast<uint32_t>(syn->int_fields["wakelock_id"]);
                int64_t timeout_ms = static_cast<int64_t>(arg0);
                power::PowerManager::Instance().AcquireWakeLock(id,
                                                                timeout_ms > 0 ? timeout_ms : -1);
            }
        }
        return;
    }
    if (std::strcmp(mname, "release") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count("wakelock_id")) {
                uint32_t id = static_cast<uint32_t>(syn->int_fields["wakelock_id"]);
                power::PowerManager::Instance().ReleaseWakeLock(id);
                return;
            }
        }
    }
    if (std::strcmp(mname, "setReferenceCounted") == 0) {
        if (obj) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn && syn->int_fields.count("wakelock_id")) {
                uint32_t id = static_cast<uint32_t>(syn->int_fields["wakelock_id"]);
                power::PowerManager::Instance().SetWakeLockReferenceCounted(id, arg0 != 0);
            }
        }
        return;
    }
    if (std::strcmp(mname, "addAction") == 0) {
        jstring action_str = reinterpret_cast<jstring>(arg0);
        const char *act = JniEnvironment::Instance().GetStringChars(action_str);
        if (obj && act) {
            auto syn = JniEnvironment::Instance().LookupObject(obj);
            if (syn) {
                syn->string_fields["action"] = act;
            }
        }
        return;
    }
    if (std::strcmp(mname, "createNotificationChannel") == 0) {
        jobject ch_arg = reinterpret_cast<jobject>(arg0);
        if (ch_arg) {
            auto syn = JniEnvironment::Instance().LookupObject(ch_arg);
            if (syn) {
                notification::NotificationChannel ch;
                ch.id = syn->string_fields.count("id") ? syn->string_fields["id"] : "default";
                ch.name = syn->string_fields.count("name") ? syn->string_fields["name"] : "Default";
                ch.description = syn->string_fields.count("description")
                                     ? syn->string_fields["description"]
                                     : "";
                ch.importance = syn->int_fields.count("importance")
                                    ? syn->int_fields["importance"]
                                    : notification::IMPORTANCE_DEFAULT;
                notification::NotificationManager::Instance().CreateNotificationChannel(ch);
            }
        }
        return;
    }
    if (std::strcmp(mname, "deleteNotificationChannel") == 0) {
        jstring id_str = reinterpret_cast<jstring>(arg0);
        const char *id = JniEnvironment::Instance().GetStringChars(id_str);
        if (id) {
            notification::NotificationManager::Instance().DeleteNotificationChannel(id);
        }
        return;
    }
    if (std::strcmp(mname, "notify") == 0) {
        std::string tag{};
        int32_t id = 0;
        jobject notif_obj = nullptr;

        auto syn1 = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg1));
        if (syn1 && syn1->class_name == "android/app/Notification") {
            id = static_cast<int32_t>(arg0);
            notif_obj = reinterpret_cast<jobject>(arg1);
        } else {
            auto syn2 = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg2));
            if (syn2 && syn2->class_name == "android/app/Notification") {
                jstring tag_str = reinterpret_cast<jstring>(arg0);
                const char *t = JniEnvironment::Instance().GetStringChars(tag_str);
                if (t)
                    tag = t;
                id = static_cast<int32_t>(arg1);
                notif_obj = reinterpret_cast<jobject>(arg2);
            }
        }

        if (notif_obj) {
            auto syn = JniEnvironment::Instance().LookupObject(notif_obj);
            if (syn) {
                notification::NotificationRecord rec;
                rec.id = id;
                rec.tag = tag;
                rec.title = syn->string_fields["title"];
                rec.text = syn->string_fields["text"];
                rec.channel_id = syn->string_fields.count("channel_id")
                                     ? syn->string_fields["channel_id"]
                                     : "default";
                rec.small_icon = syn->int_fields["small_icon"];
                rec.flags = syn->int_fields["flags"];
                rec.progress_max = syn->int_fields["progress_max"];
                rec.progress_current = syn->int_fields["progress_current"];
                rec.progress_indeterminate = syn->bool_fields["progress_indeterminate"];
                notification::NotificationManager::Instance().PostNotification(tag, id, rec);
            }
        }
        return;
    }
    if (std::strcmp(mname, "cancel") == 0) {
        if (arg0 > 0x10000 && arg1 != 0) {
            jstring tag_str = reinterpret_cast<jstring>(arg0);
            const char *t = JniEnvironment::Instance().GetStringChars(tag_str);
            if (t) {
                int32_t id = static_cast<int32_t>(arg1);
                notification::NotificationManager::Instance().CancelNotification(t, id);
                return;
            }
        }
        int32_t id = static_cast<int32_t>(arg0);
        notification::NotificationManager::Instance().CancelNotification("", id);
        return;
    }
    if (std::strcmp(mname, "cancelAll") == 0) {
        notification::NotificationManager::Instance().CancelAllNotifications();
        return;
    }
    if (std::strcmp(mname, "apply") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->class_name == "android/content/SharedPreferences$Editor") {
            CommitSharedPrefsEditor(JniEnvironment::Instance(), *syn);
            return;
        }
    }
    if (std::strcmp(mname, "getMetrics") == 0 || std::strcmp(mname, "getRealMetrics") == 0) {
        jobject dm_arg = reinterpret_cast<jobject>(arg0);
        if (dm_arg) {
            auto dm = JniEnvironment::Instance().LookupObject(dm_arg);
            if (dm) {
                auto &env = JniEnvironment::Instance();
                dm->int_fields["widthPixels"] = env.GetDisplayWidth();
                dm->int_fields["heightPixels"] = env.GetDisplayHeight();
                dm->int_fields["densityDpi"] = env.GetDisplayDensityDpi();
                dm->float_fields["density"] = env.GetDisplayDensity();
                dm->float_fields["scaledDensity"] = env.GetDisplayDensity();
            }
        }
        return;
    }
    if (std::strcmp(mname, "getRealSize") == 0) {
        jobject pt_arg = reinterpret_cast<jobject>(arg0);
        if (pt_arg) {
            auto pt = JniEnvironment::Instance().LookupObject(pt_arg);
            if (pt) {
                auto &env = JniEnvironment::Instance();
                pt->int_fields["x"] = env.GetDisplayWidth();
                pt->int_fields["y"] = env.GetDisplayHeight();
            }
        }
        return;
    }
    if (std::strcmp(mname, "putString") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        const char *val = env.GetStringChars(reinterpret_cast<jstring>(arg1));
        if (syn && key &&
            (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
            syn->bundle_strings[key] = val ? val : "";
        }
        return;
    }
    if (std::strcmp(mname, "putInt") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key &&
            (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
            syn->bundle_ints[key] = static_cast<int32_t>(arg1);
        }
        return;
    }
    if (std::strcmp(mname, "putBoolean") == 0 && obj) {
        auto &env = JniEnvironment::Instance();
        auto syn = env.LookupObject(obj);
        const char *key = env.GetStringChars(reinterpret_cast<jstring>(arg0));
        if (syn && key &&
            (syn->type == JniObjectType::Bundle || syn->class_name == "android/os/Bundle")) {
            syn->bundle_bools[key] = (arg1 != 0);
        }
        return;
    }
    if (std::strcmp(mname, "vibrate") == 0) {
        input::HapticManager::Instance().Vibrate(0, 32000, 32000, static_cast<uint32_t>(arg0));
        return;
    }
    if (std::strcmp(mname, "setFlags") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            int32_t flags = static_cast<int32_t>(arg0);
            int32_t mask = static_cast<int32_t>(arg1);
            syn->int_fields["flags"] = (syn->int_fields["flags"] & ~mask) | (flags & mask);
        }
        return;
    }
    if (std::strcmp(mname, "addFlags") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->int_fields["flags"] |= static_cast<int32_t>(arg0);
        }
        return;
    }
    if (std::strcmp(mname, "clearFlags") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->int_fields["flags"] &= ~static_cast<int32_t>(arg0);
        }
        return;
    }
    if (std::strcmp(mname, "setStatusBarColor") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->int_fields["statusBarColor"] = static_cast<int32_t>(arg0);
        }
        return;
    }
    if (std::strcmp(mname, "setNavigationBarColor") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->int_fields["navigationBarColor"] = static_cast<int32_t>(arg0);
        }
        return;
    }
    if (std::strcmp(mname, "setSystemUiVisibility") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->int_fields["systemUiVisibility"] = static_cast<int32_t>(arg0);
        }
        return;
    }
    if (std::strcmp(mname, "setKeepScreenOn") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn) {
            syn->bool_fields["keepScreenOn"] = (arg0 != 0) ? 1 : 0;
        }
        return;
    }
    if (std::strcmp(mname, "set") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->type == JniObjectType::Field && !syn->is_static) {
            jfieldID target_fid =
                syn->field_id
                    ? syn->field_id
                    : reinterpret_cast<jfieldID>(const_cast<char *>(syn->member_name.c_str()));
            Env_SetObjectField(nullptr, reinterpret_cast<jobject>(arg0), target_fid,
                               reinterpret_cast<jobject>(arg1));
        }
        return;
    }
    if (std::strcmp(mname, "show") == 0 && obj) {
        auto syn = JniEnvironment::Instance().LookupObject(obj);
        if (syn && syn->class_name == "android/widget/Toast") {
            std::string txt = syn->string_fields["text"];
            int32_t dur = syn->int_fields["duration"];
            notification::NotificationManager::Instance().ShowToast(txt, dur);
            return;
        }
    }
    DispatchVoidMethod(obj, methodID);
}

static __attribute__((sysv_abi)) void Env_CallVoidMethodV(JNIEnv *, jobject obj, jmethodID methodID,
                                                          va_list)
{
    DispatchVoidMethod(obj, methodID);
}

static __attribute__((sysv_abi)) void Env_CallVoidMethodA(JNIEnv *, jobject obj, jmethodID methodID,
                                                          const void *)
{
    DispatchVoidMethod(obj, methodID);
}

// Static method calls
static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethod(JNIEnv *, jclass clazz,
                                                                    jmethodID methodID,
                                                                    uintptr_t arg0, uintptr_t arg1,
                                                                    uintptr_t arg2, uintptr_t arg3)
{
    if (!methodID) {
        return nullptr;
    }

    const char *mname = reinterpret_cast<const char *>(methodID);
    if (std::strcmp(mname, "decodeByteArray") == 0) {
        auto arr_syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg0));
        if (!arr_syn || arr_syn->byte_data.empty()) {
            return nullptr;
        }
        int32_t offset = static_cast<int32_t>(arg1);
        int32_t length = static_cast<int32_t>(arg2);
        if (offset < 0 || length <= 0 ||
            static_cast<size_t>(offset) + static_cast<size_t>(length) > arr_syn->byte_data.size()) {
            return nullptr;
        }
        const auto *ptr = reinterpret_cast<const uint8_t *>(arr_syn->byte_data.data() + offset);
        jobject opts_obj = reinterpret_cast<jobject>(arg3);
        return DecodeBitmapFromEncodedBytes(
            std::span<const uint8_t>(ptr, static_cast<size_t>(length)), opts_obj);
    }
    if (std::strcmp(mname, "decodeResource") == 0) {
        uint32_t res_id = static_cast<uint32_t>(arg1);
        jobject opts_obj = reinterpret_cast<jobject>(arg2);
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (!pkg || !pkg->archive || !pkg->resource_table) {
            return nullptr;
        }
        auto res_path = pkg->resource_table->GetString(res_id);
        if (!res_path.has_value() || res_path->empty()) {
            return nullptr;
        }
        auto entry_bytes = pkg->archive->ReadEntry(*res_path);
        if (!entry_bytes.has_value()) {
            return nullptr;
        }
        return DecodeBitmapFromEncodedBytes(*entry_bytes, opts_obj);
    }
    if (std::strcmp(mname, "decodeFile") == 0) {
        const char *path_c =
            JniEnvironment::Instance().GetStringChars(reinterpret_cast<jstring>(arg0));
        jobject opts_obj = reinterpret_cast<jobject>(arg1);
        if (!path_c) {
            return nullptr;
        }
        std::filesystem::path target_path(path_c);
        if (auto *vfs = syscall::SyscallDispatcher::GetVfs()) {
            auto resolved = vfs->Resolve(path_c);
            if (!resolved.host_path.empty()) {
                target_path = resolved.host_path;
            }
        }
        std::ifstream ifs(target_path, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            return nullptr;
        }
        auto sz = ifs.tellg();
        if (sz <= 0) {
            return nullptr;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(sz));
        ifs.seekg(0, std::ios::beg);
        ifs.read(reinterpret_cast<char *>(bytes.data()), sz);
        return DecodeBitmapFromEncodedBytes(bytes, opts_obj);
    }
    if (std::strcmp(mname, "decodeStream") == 0) {
        auto stream_syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg0));
        jobject opts_obj = reinterpret_cast<jobject>(arg2);
        if (!stream_syn) {
            return nullptr;
        }
        if (auto it = stream_syn->binary_fields.find("bytes");
            it != stream_syn->binary_fields.end()) {
            return DecodeBitmapFromEncodedBytes(it->second, opts_obj);
        }
        if (!stream_syn->byte_data.empty()) {
            return DecodeBitmapFromEncodedBytes(
                std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t *>(stream_syn->byte_data.data()),
                    stream_syn->byte_data.size()),
                opts_obj);
        }
        return nullptr;
    }
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
    if (std::strcmp(mname, "newPlainText") == 0) {
        auto &env = JniEnvironment::Instance();
        jstring label_str = reinterpret_cast<jstring>(arg0);
        jstring text_str = reinterpret_cast<jstring>(arg1);
        const char *label = label_str ? env.GetStringChars(label_str) : "text";
        const char *text = text_str ? env.GetStringChars(text_str) : "";

        auto clip_obj = std::make_shared<JniSyntheticObject>();
        clip_obj->class_name = "android/content/ClipData";
        clip_obj->string_fields["label"] = label ? label : "text";
        clip_obj->int_fields["item_count"] = 1;

        auto desc_obj = std::make_shared<JniSyntheticObject>();
        desc_obj->class_name = "android/content/ClipDescription";
        desc_obj->string_fields["label"] = label ? label : "text";
        desc_obj->int_fields["mime_count"] = 1;
        desc_obj->string_fields["mime_0"] = "text/plain";
        clip_obj->object_fields["description"] = env.RegisterObject(desc_obj);

        auto item_obj = std::make_shared<JniSyntheticObject>();
        item_obj->class_name = "android/content/ClipData$Item";
        item_obj->string_fields["text"] = text ? text : "";
        clip_obj->object_fields["item_0"] = env.RegisterObject(item_obj);

        return env.RegisterObject(clip_obj);
    }
    if (std::strcmp(mname, "newHtmlText") == 0) {
        auto &env = JniEnvironment::Instance();
        jstring label_str = reinterpret_cast<jstring>(arg0);
        jstring text_str = reinterpret_cast<jstring>(arg1);
        jstring html_str = reinterpret_cast<jstring>(arg2);
        const char *label = label_str ? env.GetStringChars(label_str) : "html";
        const char *text = text_str ? env.GetStringChars(text_str) : "";
        const char *html = html_str ? env.GetStringChars(html_str) : "";

        auto clip_obj = std::make_shared<JniSyntheticObject>();
        clip_obj->class_name = "android/content/ClipData";
        clip_obj->string_fields["label"] = label ? label : "html";
        clip_obj->int_fields["item_count"] = 1;

        auto desc_obj = std::make_shared<JniSyntheticObject>();
        desc_obj->class_name = "android/content/ClipDescription";
        desc_obj->string_fields["label"] = label ? label : "html";
        desc_obj->int_fields["mime_count"] = 2;
        desc_obj->string_fields["mime_0"] = "text/html";
        desc_obj->string_fields["mime_1"] = "text/plain";
        clip_obj->object_fields["description"] = env.RegisterObject(desc_obj);

        auto item_obj = std::make_shared<JniSyntheticObject>();
        item_obj->class_name = "android/content/ClipData$Item";
        item_obj->string_fields["text"] = text ? text : "";
        item_obj->string_fields["htmlText"] = html ? html : "";
        clip_obj->object_fields["item_0"] = env.RegisterObject(item_obj);

        return env.RegisterObject(clip_obj);
    }
    if (std::strcmp(mname, "newUri") == 0) {
        auto &env = JniEnvironment::Instance();
        jstring label_str = reinterpret_cast<jstring>(arg1);
        jobject uri_obj = reinterpret_cast<jobject>(arg2);
        const char *label = label_str ? env.GetStringChars(label_str) : "uri";
        std::string uri_str = "content://media/external/1";
        if (uri_obj) {
            auto uri_syn = env.LookupObject(uri_obj);
            if (uri_syn && uri_syn->string_fields.count("uri")) {
                uri_str = uri_syn->string_fields["uri"];
            }
        }

        auto clip_obj = std::make_shared<JniSyntheticObject>();
        clip_obj->class_name = "android/content/ClipData";
        clip_obj->string_fields["label"] = label ? label : "uri";
        clip_obj->int_fields["item_count"] = 1;

        auto desc_obj = std::make_shared<JniSyntheticObject>();
        desc_obj->class_name = "android/content/ClipDescription";
        desc_obj->string_fields["label"] = label ? label : "uri";
        desc_obj->int_fields["mime_count"] = 1;
        desc_obj->string_fields["mime_0"] = "text/uri-list";
        clip_obj->object_fields["description"] = env.RegisterObject(desc_obj);

        auto item_obj = std::make_shared<JniSyntheticObject>();
        item_obj->class_name = "android/content/ClipData$Item";
        item_obj->string_fields["uri"] = uri_str;
        item_obj->string_fields["text"] = uri_str;
        clip_obj->object_fields["item_0"] = env.RegisterObject(item_obj);

        return env.RegisterObject(clip_obj);
    }
    if (std::strcmp(mname, "getExternalStorageDirectory") == 0) {
        auto obj_ret = std::make_shared<JniSyntheticObject>();
        obj_ret->class_name = "java/io/File";
        obj_ret->string_fields["path"] = "/storage/emulated/0";
        return JniEnvironment::Instance().RegisterObject(obj_ret);
    }
    if (std::strcmp(mname, "getDataDirectory") == 0) {
        auto obj_ret = std::make_shared<JniSyntheticObject>();
        obj_ret->class_name = "java/io/File";
        obj_ret->string_fields["path"] = "/data";
        return JniEnvironment::Instance().RegisterObject(obj_ret);
    }
    if (std::strcmp(mname, "getExternalStorageState") == 0) {
        return JniEnvironment::Instance().CreateString("mounted");
    }
    if (std::strcmp(mname, "getInstance") == 0) {
        const char *cname = clazz ? reinterpret_cast<const char *>(clazz) : "";
        auto &env = JniEnvironment::Instance();
        if (std::strstr(cname, "KeyStore") != nullptr) {
            auto obj_ret = std::make_shared<JniSyntheticObject>();
            obj_ret->class_name = "java/security/KeyStore";
            return env.RegisterObject(obj_ret);
        }
        if (std::strstr(cname, "KeyGenerator") != nullptr) {
            auto obj_ret = std::make_shared<JniSyntheticObject>();
            obj_ret->class_name = "javax/crypto/KeyGenerator";
            if (arg0 != 0) {
                const char *alg = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                obj_ret->string_fields["algorithm"] = alg ? alg : "AES";
            }
            return env.RegisterObject(obj_ret);
        }
        if (std::strstr(cname, "KeyPairGenerator") != nullptr) {
            auto obj_ret = std::make_shared<JniSyntheticObject>();
            obj_ret->class_name = "java/security/KeyPairGenerator";
            if (arg0 != 0) {
                const char *alg = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                obj_ret->string_fields["algorithm"] = alg ? alg : "RSA";
            }
            return env.RegisterObject(obj_ret);
        }
        if (std::strstr(cname, "Cipher") != nullptr) {
            auto obj_ret = std::make_shared<JniSyntheticObject>();
            obj_ret->class_name = "javax/crypto/Cipher";
            if (arg0 != 0) {
                const char *transf = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                obj_ret->string_fields["transformation"] = transf ? transf : "AES/GCM/NoPadding";
            }
            return env.RegisterObject(obj_ret);
        }
        if (std::strstr(cname, "Signature") != nullptr) {
            auto obj_ret = std::make_shared<JniSyntheticObject>();
            obj_ret->class_name = "java/security/Signature";
            if (arg0 != 0) {
                const char *alg = env.GetStringChars(reinterpret_cast<jstring>(arg0));
                obj_ret->string_fields["algorithm"] = alg ? alg : "SHA256withRSA";
            }
            return env.RegisterObject(obj_ret);
        }

        auto obj_ret = std::make_shared<JniSyntheticObject>();
        obj_ret->class_name = "com/google/android/gms/common/GoogleApiAvailability";
        return JniEnvironment::Instance().RegisterObject(obj_ret);
    }
    if (std::strcmp(mname, "create") == 0) {
        auto obj_ret = std::make_shared<JniSyntheticObject>();
        obj_ret->class_name = "com/google/android/play/core/review/ReviewManager";
        return JniEnvironment::Instance().RegisterObject(obj_ret);
    }
    if (std::strcmp(mname, "makeText") == 0) {
        auto &env = JniEnvironment::Instance();
        jstring text_str = reinterpret_cast<jstring>(arg1);
        const char *txt = text_str ? env.GetStringChars(text_str) : "";
        auto toast = std::make_shared<JniSyntheticObject>();
        toast->class_name = "android/widget/Toast";
        toast->string_fields["text"] = txt ? txt : "";
        toast->int_fields["duration"] = static_cast<int32_t>(arg2);
        return env.RegisterObject(toast);
    }
    return nullptr;
}

static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethodV(JNIEnv *env, jclass clazz,
                                                                     jmethodID methodID, va_list)
{
    return Env_CallStaticObjectMethod(env, clazz, methodID, 0, 0, 0, 0);
}

static __attribute__((sysv_abi)) jobject Env_CallStaticObjectMethodA(JNIEnv *env, jclass clazz,
                                                                     jmethodID methodID,
                                                                     const void *args)
{
    const auto *jv = static_cast<const uint64_t *>(args);
    return Env_CallStaticObjectMethod(env, clazz, methodID, jv ? jv[0] : 0, jv ? jv[1] : 0,
                                      jv ? jv[2] : 0, jv ? jv[3] : 0);
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethod(JNIEnv *, jclass,
                                                                      jmethodID methodID, ...)
{
    if (methodID) {
        const char *mname = reinterpret_cast<const char *>(methodID);
        if (std::strcmp(mname, "isExternalStorageRemovable") == 0) {
            return JNI_FALSE;
        }
        if (std::strcmp(mname, "isExternalStorageEmulated") == 0) {
            return JNI_TRUE;
        }
    }
    return JNI_TRUE;
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethodV(JNIEnv *env, jclass clazz,
                                                                       jmethodID methodID, va_list)
{
    return Env_CallStaticBooleanMethod(env, clazz, methodID);
}

static __attribute__((sysv_abi)) jboolean Env_CallStaticBooleanMethodA(JNIEnv *env, jclass clazz,
                                                                       jmethodID methodID,
                                                                       const void *)
{
    return Env_CallStaticBooleanMethod(env, clazz, methodID);
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

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethod(JNIEnv *, jclass, jmethodID methodID,
                                                              ...)
{
    if (methodID) {
        const char *mname = reinterpret_cast<const char *>(methodID);
        if (std::strcmp(mname, "isGooglePlayServicesAvailable") == 0) {
            return 0; // ConnectionResult.SUCCESS
        }
        if (std::strcmp(mname, "getMinBufferSize") == 0) {
            return 4096;
        }
        if (std::strcmp(mname, "myPid") == 0) {
            return static_cast<jint>(GetCurrentProcessId());
        }
        if (std::strcmp(mname, "myTid") == 0) {
            return static_cast<jint>(thread::ThreadManager::Instance().GetCurrentTid());
        }
        if (std::strcmp(mname, "myUid") == 0) {
            return 10000;
        }
    }
    return 0;
}

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethodV(JNIEnv *env, jclass clazz,
                                                               jmethodID methodID, va_list)
{
    return Env_CallStaticIntMethod(env, clazz, methodID);
}

static __attribute__((sysv_abi)) jint Env_CallStaticIntMethodA(JNIEnv *env, jclass clazz,
                                                               jmethodID methodID, const void *)
{
    return Env_CallStaticIntMethod(env, clazz, methodID);
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
        if (std::strcmp(fname, "BRAND") == 0) {
            return env.CreateString("portage");
        }
        if (std::strcmp(fname, "DEVICE") == 0 || std::strcmp(fname, "BOARD") == 0) {
            return env.CreateString("portage_x86_64");
        }
        if (std::strcmp(fname, "DISPLAY") == 0) {
            return env.CreateString("Portage.13");
        }
        if (std::strcmp(fname, "HOST") == 0) {
            return env.CreateString("portage-win");
        }
        if (std::strcmp(fname, "USER") == 0) {
            return env.CreateString("android-build");
        }
        if (std::strcmp(fname, "TAGS") == 0) {
            return env.CreateString("release-keys");
        }
        if (std::strcmp(fname, "TYPE") == 0) {
            return env.CreateString("user");
        }
        if (std::strcmp(fname, "SUPPORTED_ABIS") == 0) {
            auto arr = std::make_shared<JniSyntheticObject>();
            arr->type = JniObjectType::ObjectArray;
            arr->class_name = "[Ljava/lang/String;";
            arr->object_data.push_back(env.CreateString("x86_64"));
            return env.RegisterObject(arr);
        }
    }

    if (std::strstr(cname, "VERSION") != nullptr) {
        if (std::strcmp(fname, "RELEASE") == 0) {
            return env.CreateString("13");
        }
        if (std::strcmp(fname, "CODENAME") == 0) {
            return env.CreateString("REL");
        }
        if (std::strcmp(fname, "INCREMENTAL") == 0) {
            return env.CreateString("1");
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
    case JniObjectType::BooleanArray:
        return static_cast<jsize>(synth->bool_data.size());
    case JniObjectType::ByteArray:
        return static_cast<jsize>(synth->byte_data.size());
    case JniObjectType::CharArray:
        return static_cast<jsize>(synth->char_data.size());
    case JniObjectType::ShortArray:
        return static_cast<jsize>(synth->short_data.size());
    case JniObjectType::IntArray:
        return static_cast<jsize>(synth->int_data.size());
    case JniObjectType::LongArray:
        return static_cast<jsize>(synth->long_data.size());
    case JniObjectType::FloatArray:
        return static_cast<jsize>(synth->float_data.size());
    case JniObjectType::DoubleArray:
        return static_cast<jsize>(synth->double_data.size());
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
    obj->type = JniObjectType::BooleanArray;
    obj->bool_data.resize(std::max<jsize>(0, len), 0);
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
    obj->type = JniObjectType::CharArray;
    obj->char_data.resize(std::max<jsize>(0, len), 0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jshortArray Env_NewShortArray(JNIEnv *, jsize len)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::ShortArray;
    obj->short_data.resize(std::max<jsize>(0, len), 0);
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
    obj->type = JniObjectType::LongArray;
    obj->long_data.resize(std::max<jsize>(0, len), 0);
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
    obj->type = JniObjectType::DoubleArray;
    obj->double_data.resize(std::max<jsize>(0, len), 0.0);
    return JniEnvironment::Instance().RegisterObject(obj);
}

static __attribute__((sysv_abi)) jboolean *Env_GetBooleanArrayElements(JNIEnv *, jbooleanArray arr,
                                                                       jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jboolean *>(synth->bool_data.data()) : nullptr;
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
    return synth ? reinterpret_cast<jchar *>(synth->char_data.data()) : nullptr;
}

static __attribute__((sysv_abi)) jshort *Env_GetShortArrayElements(JNIEnv *, jshortArray arr,
                                                                   jboolean *isCopy)
{
    if (isCopy) {
        *isCopy = JNI_FALSE;
    }
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    return synth ? reinterpret_cast<jshort *>(synth->short_data.data()) : nullptr;
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
    return synth ? reinterpret_cast<jlong *>(synth->long_data.data()) : nullptr;
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
    return synth ? synth->double_data.data() : nullptr;
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
        static_cast<size_t>(start + len) <= synth->bool_data.size()) {
        std::memcpy(buf, synth->bool_data.data() + start, static_cast<size_t>(len) * sizeof(jboolean));
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
        static_cast<size_t>(start + len) <= synth->char_data.size()) {
        std::memcpy(buf, synth->char_data.data() + start, static_cast<size_t>(len) * sizeof(jchar));
    }
}

static __attribute__((sysv_abi)) void Env_GetShortArrayRegion(JNIEnv *, jshortArray arr,
                                                              jsize start, jsize len, jshort *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->short_data.size()) {
        std::memcpy(buf, synth->short_data.data() + start, static_cast<size_t>(len) * sizeof(jshort));
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
        static_cast<size_t>(start + len) <= synth->long_data.size()) {
        std::memcpy(buf, synth->long_data.data() + start, static_cast<size_t>(len) * sizeof(jlong));
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
        static_cast<size_t>(start + len) <= synth->double_data.size()) {
        std::memcpy(buf, synth->double_data.data() + start, static_cast<size_t>(len) * sizeof(jdouble));
    }
}

static __attribute__((sysv_abi)) void
Env_SetBooleanArrayRegion(JNIEnv *, jbooleanArray arr, jsize start, jsize len, const jboolean *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->bool_data.size()) {
        std::memcpy(synth->bool_data.data() + start, buf, static_cast<size_t>(len) * sizeof(jboolean));
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
        static_cast<size_t>(start + len) <= synth->char_data.size()) {
        std::memcpy(synth->char_data.data() + start, buf, static_cast<size_t>(len) * sizeof(jchar));
    }
}

static __attribute__((sysv_abi)) void
Env_SetShortArrayRegion(JNIEnv *, jshortArray arr, jsize start, jsize len, const jshort *buf)
{
    auto synth = JniEnvironment::Instance().LookupObject(arr);
    if (synth && buf && start >= 0 && len > 0 &&
        static_cast<size_t>(start + len) <= synth->short_data.size()) {
        std::memcpy(synth->short_data.data() + start, buf, static_cast<size_t>(len) * sizeof(jshort));
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
        static_cast<size_t>(start + len) <= synth->long_data.size()) {
        std::memcpy(synth->long_data.data() + start, buf, static_cast<size_t>(len) * sizeof(jlong));
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
        static_cast<size_t>(start + len) <= synth->double_data.size()) {
        std::memcpy(synth->double_data.data() + start, buf, static_cast<size_t>(len) * sizeof(jdouble));
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
    switch (synth->type) {
    case JniObjectType::BooleanArray:
        return synth->bool_data.data();
    case JniObjectType::ByteArray:
        return synth->byte_data.data();
    case JniObjectType::CharArray:
        return synth->char_data.data();
    case JniObjectType::ShortArray:
        return synth->short_data.data();
    case JniObjectType::IntArray:
        return synth->int_data.data();
    case JniObjectType::LongArray:
        return synth->long_data.data();
    case JniObjectType::FloatArray:
        return synth->float_data.data();
    case JniObjectType::DoubleArray:
        return synth->double_data.data();
    default:
        return synth->buffer_address;
    }
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
    env_functions_->CallStaticObjectMethod =
        reinterpret_cast<decltype(env_functions_->CallStaticObjectMethod)>(
            reinterpret_cast<void *>(&Env_CallStaticObjectMethod));
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
    shared_prefs_.clear();
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

jobject JniEnvironment::CreateBitmapFromPixels(uint32_t width, uint32_t height, int32_t format,
                                               uint32_t stride, std::vector<uint8_t> pixels)
{
    if (width == 0 || height == 0 || stride == 0 ||
        pixels.size() < static_cast<size_t>(stride) * height) {
        return CreateBitmap(width, height, format);
    }
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Bitmap;
    obj->class_name = "android/graphics/Bitmap";
    obj->bitmap_width = width;
    obj->bitmap_height = height;
    obj->bitmap_format = (format > 0) ? format : ANDROID_BITMAP_FORMAT_RGBA_8888;
    obj->bitmap_stride = stride;
    obj->bitmap_pixels = std::move(pixels);
    obj->buffer_address = obj->bitmap_pixels.data();
    obj->buffer_capacity = static_cast<jlong>(obj->bitmap_pixels.size());
    obj->bitmap_locked = false;
    return RegisterObject(obj);
}

std::unordered_map<std::string, JniSharedPrefValue> &
JniEnvironment::GetSharedPrefsMap(std::string_view prefs_name)
{
    std::string name_str(prefs_name.empty() ? "default" : prefs_name);
    auto &map_ref = shared_prefs_[name_str];
    if (!map_ref.empty() || files_dir_.empty()) {
        return map_ref;
    }

    std::filesystem::path prefs_path =
        std::filesystem::path(files_dir_).parent_path() / "shared_prefs" / (name_str + ".kv");
    std::ifstream ifs(prefs_path);
    if (!ifs.is_open()) {
        return map_ref;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.size() < 3 || line[1] != '|') {
            continue;
        }
        auto sep = line.find('=', 2);
        if (sep == std::string::npos) {
            continue;
        }
        std::string key = line.substr(2, sep - 2);
        std::string val = line.substr(sep + 1);
        JniSharedPrefValue entry{};
        switch (line[0]) {
        case 'S':
            entry.kind = JniSharedPrefValue::Kind::String;
            entry.str_val = val;
            map_ref[key] = entry;
            break;
        case 'I':
            entry.kind = JniSharedPrefValue::Kind::Int;
            entry.int_val = std::stoi(val);
            map_ref[key] = entry;
            break;
        case 'F':
            entry.kind = JniSharedPrefValue::Kind::Float;
            entry.float_val = std::stof(val);
            map_ref[key] = entry;
            break;
        case 'B':
            entry.kind = JniSharedPrefValue::Kind::Bool;
            entry.bool_val = (val == "1");
            map_ref[key] = entry;
            break;
        default:
            break;
        }
    }
    return map_ref;
}

void JniEnvironment::SaveSharedPrefs(std::string_view prefs_name) const
{
    std::string name_str(prefs_name.empty() ? "default" : prefs_name);
    auto it = shared_prefs_.find(name_str);
    if (it == shared_prefs_.end() || files_dir_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::path(files_dir_).parent_path() / "shared_prefs";
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return;
    }
    std::ofstream ofs(dir / (name_str + ".kv"), std::ios::trunc);
    if (!ofs.is_open()) {
        return;
    }
    for (const auto &[key, val] : it->second) {
        switch (val.kind) {
        case JniSharedPrefValue::Kind::String:
            ofs << "S|" << key << "=" << val.str_val << "\n";
            break;
        case JniSharedPrefValue::Kind::Int:
            ofs << "I|" << key << "=" << val.int_val << "\n";
            break;
        case JniSharedPrefValue::Kind::Float:
            ofs << "F|" << key << "=" << val.float_val << "\n";
            break;
        case JniSharedPrefValue::Kind::Bool:
            ofs << "B|" << key << "=" << (val.bool_val ? "1" : "0") << "\n";
            break;
        }
    }
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
