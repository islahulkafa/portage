#include "package/dex_vm.hpp"

#include "elf/link_map.hpp"
#include "ndk/android_log.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/java_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace portage::package {

namespace {

inline int32_t SignExtend4(uint32_t v) noexcept
{
    v &= 0xFu;
    return (v & 0x8u) ? static_cast<int32_t>(v | 0xFFFFFFF0u) : static_cast<int32_t>(v);
}

inline uint64_t ReadWide(std::span<const uint64_t> regs, size_t idx) noexcept
{
    uint64_t lo = regs[idx] & 0xFFFFFFFFULL;
    uint64_t hi = regs[idx + 1] & 0xFFFFFFFFULL;
    return lo | (hi << 32);
}

inline void WriteWide(std::span<uint64_t> regs, size_t idx, uint64_t val) noexcept
{
    regs[idx] = val & 0xFFFFFFFFULL;
    regs[idx + 1] = (val >> 32) & 0xFFFFFFFFULL;
}

inline float BitsToFloat(uint64_t raw) noexcept
{
    uint32_t bits = static_cast<uint32_t>(raw);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(float));
    return f;
}

inline uint64_t FloatToBits(float f) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(float));
    return static_cast<uint64_t>(bits);
}

inline double BitsToDouble(uint64_t raw) noexcept
{
    double d = 0.0;
    std::memcpy(&d, &raw, sizeof(double));
    return d;
}

inline uint64_t DoubleToBits(double d) noexcept
{
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof(double));
    return bits;
}

std::string MakeStaticFieldKey(std::string_view class_desc, std::string_view field_name)
{
    std::string key(class_desc);
    key.push_back(':');
    key.append(field_name);
    return key;
}

void AppendMangledIdent(std::string &out, std::string_view s)
{
    for (char ch : s) {
        if (ch == '/' || ch == '.') {
            out.push_back('_');
        } else if (ch == '_') {
            out.append("_1");
        } else if (ch == ';') {
            out.append("_2");
        } else if (ch == '[') {
            out.append("_3");
        } else {
            out.push_back(ch);
        }
    }
}

uint64_t DispatchSysvNativeU64(void *fn, JNIEnv *env, void *obj_or_cls,
                               std::span<const uint64_t> args)
{
    using Fn0 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *);
    using Fn1 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t);
    using Fn2 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t, uint64_t);
    using Fn3 =
        __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t, uint64_t, uint64_t);
    using Fn4 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t, uint64_t,
                                                       uint64_t, uint64_t);
    using Fn5 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t, uint64_t,
                                                       uint64_t, uint64_t, uint64_t);
    using Fn6 = __attribute__((sysv_abi)) uint64_t (*)(JNIEnv *, void *, uint64_t, uint64_t,
                                                       uint64_t, uint64_t, uint64_t, uint64_t);
    switch (args.size()) {
    case 0:
        return reinterpret_cast<Fn0>(fn)(env, obj_or_cls);
    case 1:
        return reinterpret_cast<Fn1>(fn)(env, obj_or_cls, args[0]);
    case 2:
        return reinterpret_cast<Fn2>(fn)(env, obj_or_cls, args[0], args[1]);
    case 3:
        return reinterpret_cast<Fn3>(fn)(env, obj_or_cls, args[0], args[1], args[2]);
    case 4:
        return reinterpret_cast<Fn4>(fn)(env, obj_or_cls, args[0], args[1], args[2], args[3]);
    case 5:
        return reinterpret_cast<Fn5>(fn)(env, obj_or_cls, args[0], args[1], args[2], args[3],
                                         args[4]);
    default:
        return reinterpret_cast<Fn6>(fn)(env, obj_or_cls, args[0], args[1], args[2], args[3],
                                         args[4], args[5]);
    }
}

float DispatchSysvNativeFloat(void *fn, JNIEnv *env, void *obj_or_cls,
                              std::span<const uint64_t> args)
{
    using Fn0 = __attribute__((sysv_abi)) float (*)(JNIEnv *, void *);
    using Fn1 = __attribute__((sysv_abi)) float (*)(JNIEnv *, void *, uint64_t);
    using Fn2 = __attribute__((sysv_abi)) float (*)(JNIEnv *, void *, uint64_t, uint64_t);
    using Fn3 = __attribute__((sysv_abi)) float (*)(JNIEnv *, void *, uint64_t, uint64_t, uint64_t);
    using Fn4 = __attribute__((sysv_abi)) float (*)(JNIEnv *, void *, uint64_t, uint64_t, uint64_t,
                                                    uint64_t);
    switch (args.size()) {
    case 0:
        return reinterpret_cast<Fn0>(fn)(env, obj_or_cls);
    case 1:
        return reinterpret_cast<Fn1>(fn)(env, obj_or_cls, args[0]);
    case 2:
        return reinterpret_cast<Fn2>(fn)(env, obj_or_cls, args[0], args[1]);
    case 3:
        return reinterpret_cast<Fn3>(fn)(env, obj_or_cls, args[0], args[1], args[2]);
    default:
        return reinterpret_cast<Fn4>(fn)(env, obj_or_cls, args[0], args[1], args[2], args[3]);
    }
}

double DispatchSysvNativeDouble(void *fn, JNIEnv *env, void *obj_or_cls,
                                std::span<const uint64_t> args)
{
    using Fn0 = __attribute__((sysv_abi)) double (*)(JNIEnv *, void *);
    using Fn1 = __attribute__((sysv_abi)) double (*)(JNIEnv *, void *, uint64_t);
    using Fn2 = __attribute__((sysv_abi)) double (*)(JNIEnv *, void *, uint64_t, uint64_t);
    using Fn3 =
        __attribute__((sysv_abi)) double (*)(JNIEnv *, void *, uint64_t, uint64_t, uint64_t);
    using Fn4 = __attribute__((sysv_abi)) double (*)(JNIEnv *, void *, uint64_t, uint64_t, uint64_t,
                                                     uint64_t);
    switch (args.size()) {
    case 0:
        return reinterpret_cast<Fn0>(fn)(env, obj_or_cls);
    case 1:
        return reinterpret_cast<Fn1>(fn)(env, obj_or_cls, args[0]);
    case 2:
        return reinterpret_cast<Fn2>(fn)(env, obj_or_cls, args[0], args[1]);
    case 3:
        return reinterpret_cast<Fn3>(fn)(env, obj_or_cls, args[0], args[1], args[2]);
    default:
        return reinterpret_cast<Fn4>(fn)(env, obj_or_cls, args[0], args[1], args[2], args[3]);
    }
}

std::vector<uint64_t> CollectRegisterArgs(const DexMethodRef &ref, std::span<const uint64_t> regs,
                                          bool is_static, std::span<const uint32_t> indices)
{
    std::vector<uint64_t> packed;
    size_t pos = 0;
    if (!is_static && pos < indices.size()) {
        packed.push_back(regs[indices[pos++]]);
    }
    for (const auto &pt : ref.param_types) {
        if (pos >= indices.size()) {
            break;
        }
        if (pt == "J" || pt == "D") {
            if (pos + 1 < indices.size()) {
                uint64_t lo = regs[indices[pos]] & 0xFFFFFFFFULL;
                uint64_t hi = regs[indices[pos + 1]] & 0xFFFFFFFFULL;
                packed.push_back(lo);
                packed.push_back(hi);
                pos += 2;
            }
        } else {
            packed.push_back(regs[indices[pos++]]);
        }
    }
    return packed;
}

} // namespace

DexVm &DexVm::Instance() noexcept
{
    static DexVm instance;
    return instance;
}

bool DexVm::LoadDex(std::span<const uint8_t> data)
{
    auto dex = DexFile::Parse(data);
    if (!dex) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    dex_files_.push_back(std::move(dex));
    return true;
}

void DexVm::Reset()
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        dex_files_.clear();
        initialized_classes_.clear();
        initializing_classes_.clear();
        static_fields_.clear();
        jni_onload_done_.clear();
        main_looper_obj_ = nullptr;
        main_handler_obj_ = nullptr;
        runtime_obj_ = nullptr;
        main_looper_quit_ = false;
        next_looper_seq_ = 1;
        looper_queue_.clear();
    }
    JavaRuntime::Instance().Reset();
}

const DexClassDefResolved *DexVm::FindClassUnlocked(std::string_view name,
                                                    const DexFile **out_dex) const
{
    for (const auto &dex : dex_files_) {
        if (const auto *cls = dex->FindClass(name)) {
            if (out_dex) {
                *out_dex = dex.get();
            }
            return cls;
        }
    }
    return nullptr;
}

bool DexVm::HasClass(std::string_view class_name) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return FindClassUnlocked(class_name) != nullptr;
}

const DexClassDefResolved *DexVm::FindClassDef(std::string_view class_name) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return FindClassUnlocked(class_name);
}

bool DexVm::IsSubclassUnlocked(std::string_view sub_desc, std::string_view super_desc) const
{
    std::string cur = DexFile::NormalizeDescriptor(sub_desc);
    std::string target = DexFile::NormalizeDescriptor(super_desc);
    if (target == "Ljava/lang/Object;" || target == "Ljava/io/Serializable;") {
        return true;
    }
    auto &rt = JavaRuntime::Instance();
    for (int depth = 0; depth < 32 && !cur.empty(); ++depth) {
        if (cur == target || rt.IsAssignableFrom(cur, target)) {
            return true;
        }
        const auto *cls = FindClassUnlocked(cur);
        if (!cls) {
            break;
        }
        for (const auto &iface : cls->interfaces) {
            if (iface == target || rt.IsAssignableFrom(iface, target)) {
                return true;
            }
        }
        cur = cls->superclass_descriptor;
    }
    return false;
}

bool DexVm::IsInstanceOf(jobject obj, std::string_view target_class) const
{
    if (!obj) {
        return false;
    }
    std::string norm_target = DexFile::NormalizeDescriptor(target_class);
    auto &jni = JniEnvironment::Instance();
    if (jni.GetStringChars(obj) != nullptr) {
        return JavaRuntime::Instance().IsAssignableFrom("Ljava/lang/String;", norm_target);
    }
    auto syn = jni.LookupObject(obj);
    if (!syn) {
        return false;
    }
    if (syn->class_name == "java/lang/invoke/LambdaProxy") {
        std::string target_slash = DexFile::DescriptorToSlashName(norm_target);
        if (target_slash == "java/lang/Object" ||
            JavaRuntime::Instance().IsLambdaProxyInstanceOf(obj, target_slash)) {
            return true;
        }
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return IsSubclassUnlocked(syn->class_name, norm_target);
}

std::optional<DexVm::ResolvedMethod> DexVm::ResolveMethodUnlocked(std::string_view class_name,
                                                                  std::string_view method_name,
                                                                  std::string_view signature,
                                                                  bool search_super) const
{
    std::string cur = DexFile::NormalizeDescriptor(class_name);
    int max_depth = search_super ? 32 : 1;
    for (int depth = 0; depth < max_depth && !cur.empty(); ++depth) {
        const DexFile *owner = nullptr;
        const auto *cls = FindClassUnlocked(cur, &owner);
        if (!cls || !owner) {
            break;
        }
        if (const auto *m = owner->FindMethod(cur, method_name, signature)) {
            return ResolvedMethod{owner, m};
        }
        cur = cls->superclass_descriptor;
    }
    return std::nullopt;
}

std::optional<DexVm::ResolvedMethod> DexVm::ResolveMethod(std::string_view class_name,
                                                          std::string_view method_name,
                                                          std::string_view signature,
                                                          bool search_super) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ResolveMethodUnlocked(class_name, method_name, signature, search_super);
}

bool DexVm::HasField(std::string_view class_name, std::string_view field_name) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string cur = DexFile::NormalizeDescriptor(class_name);
    for (int depth = 0; depth < 32 && !cur.empty(); ++depth) {
        const auto *cls = FindClassUnlocked(cur);
        if (!cls) {
            break;
        }
        for (const auto &f : cls->static_fields) {
            if (f.name == field_name) {
                return true;
            }
        }
        for (const auto &f : cls->instance_fields) {
            if (f.name == field_name) {
                return true;
            }
        }
        cur = cls->superclass_descriptor;
    }
    return false;
}

void DexVm::EnsureClassInitialized(std::string_view class_descriptor, JNIEnv *env)
{
    std::string desc = DexFile::NormalizeDescriptor(class_descriptor);
    const DexFile *owner = nullptr;
    const DexMethodDef *clinit = nullptr;
    std::string super_desc;
    const auto self_tid = std::this_thread::get_id();
    {
        std::unique_lock<std::recursive_mutex> lock(mutex_);
        if (initialized_classes_.count(desc)) {
            return;
        }
        while (true) {
            auto it = initializing_classes_.find(desc);
            if (it == initializing_classes_.end()) {
                break;
            }
            if (it->second == self_tid) {
                return;
            }
            init_cv_.wait(lock);
        }
        if (initialized_classes_.count(desc)) {
            return;
        }
        const auto *cls = FindClassUnlocked(desc, &owner);
        if (!cls || !owner) {
            return;
        }
        initializing_classes_[desc] = self_tid;
        super_desc = cls->superclass_descriptor;
        clinit = owner->FindMethod(desc, "<clinit>", "()V");
        auto &jni = JniEnvironment::Instance();
        size_t sv_count = std::min(cls->static_fields.size(), cls->static_values.size());
        for (size_t i = 0; i < sv_count; ++i) {
            const auto &sf = cls->static_fields[i];
            const auto &sv = cls->static_values[i];
            uint64_t val = 0;
            switch (sv.kind) {
            case DexEncodedValue::Kind::Boolean:
            case DexEncodedValue::Kind::Int:
            case DexEncodedValue::Kind::Long:
            case DexEncodedValue::Kind::Float:
            case DexEncodedValue::Kind::Double:
                val = sv.raw;
                break;
            case DexEncodedValue::Kind::String:
                val = reinterpret_cast<uintptr_t>(jni.CreateString(sv.text));
                break;
            case DexEncodedValue::Kind::Type:
                val = reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(
                    DexFile::DescriptorToSlashName(sv.text)));
                break;
            default:
                break;
            }
            static_fields_[MakeStaticFieldKey(desc, sf.name)] = val;
        }
    }
    if (!super_desc.empty()) {
        EnsureClassInitialized(super_desc, env);
    }
    if (clinit && owner) {
        ExecuteMethod(owner, clinit, {}, env);
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        initializing_classes_.erase(desc);
        initialized_classes_.insert(desc);
    }
    init_cv_.notify_all();
}

std::string DexVm::MangleJniName(std::string_view class_slash, std::string_view method_name,
                                 std::string_view signature)
{
    std::string out = "Java_";
    AppendMangledIdent(out, class_slash);
    out.push_back('_');
    AppendMangledIdent(out, method_name);
    if (!signature.empty()) {
        auto close = signature.find(')');
        if (close != std::string_view::npos && close > 1) {
            out.append("__");
            AppendMangledIdent(out, signature.substr(1, close - 1));
        }
    }
    return out;
}

std::vector<uint64_t> DexVm::PackJniArgs(const std::vector<std::string> &param_types,
                                         std::span<const uint64_t> input_args)
{
    std::vector<uint64_t> out;
    for (size_t i = 0; i < param_types.size() && i < input_args.size(); ++i) {
        if (param_types[i] == "J" || param_types[i] == "D") {
            out.push_back(input_args[i] & 0xFFFFFFFFULL);
            out.push_back((input_args[i] >> 32) & 0xFFFFFFFFULL);
        } else {
            out.push_back(input_args[i]);
        }
    }
    return out;
}

uint64_t DexVm::GetStaticField(std::string_view class_name, std::string_view field_name)
{
    std::string desc = DexFile::NormalizeDescriptor(class_name);
    EnsureClassInitialized(desc);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = static_fields_.find(MakeStaticFieldKey(desc, field_name));
        if (it != static_fields_.end()) {
            return it->second;
        }
    }
    uint64_t host_val = 0;
    if (JavaRuntime::Instance().TryGetStaticField(desc, field_name, host_val)) {
        return host_val;
    }
    return 0;
}

void DexVm::SetStaticField(std::string_view class_name, std::string_view field_name, uint64_t val)
{
    std::string desc = DexFile::NormalizeDescriptor(class_name);
    EnsureClassInitialized(desc);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    static_fields_[MakeStaticFieldKey(desc, field_name)] = val;
}

bool DexVm::GetInstanceField(jobject obj, std::string_view field_name, std::string_view type_desc,
                             uint64_t &out_val) const
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> field_lock(syn->field_mutex);
    std::string key(field_name);
    if (type_desc == "J" || type_desc == "D") {
        if (type_desc == "D") {
            auto dit = syn->double_fields.find(key);
            if (dit != syn->double_fields.end()) {
                out_val = DoubleToBits(dit->second);
                return true;
            }
        }
        auto lit = syn->long_fields.find(key);
        out_val = (lit != syn->long_fields.end()) ? static_cast<uint64_t>(lit->second) : 0;
        return true;
    }
    if (type_desc.size() > 0 && (type_desc.front() == 'L' || type_desc.front() == '[')) {
        auto oit = syn->object_fields.find(key);
        out_val = (oit != syn->object_fields.end()) ? reinterpret_cast<uint64_t>(oit->second) : 0;
        return true;
    }
    if (type_desc == "F") {
        auto fit = syn->float_fields.find(key);
        out_val = (fit != syn->float_fields.end()) ? FloatToBits(fit->second) : 0;
        return true;
    }
    if (type_desc == "Z") {
        auto bit = syn->bool_fields.find(key);
        out_val = (bit != syn->bool_fields.end()) ? static_cast<uint64_t>(bit->second) : 0;
        return true;
    }
    auto iit = syn->int_fields.find(key);
    if (iit != syn->int_fields.end()) {
        out_val = static_cast<uint64_t>(static_cast<uint32_t>(iit->second));
    } else {
        auto oit = syn->object_fields.find(key);
        if (oit != syn->object_fields.end()) {
            out_val = reinterpret_cast<uint64_t>(oit->second);
        } else {
            out_val = 0;
        }
    }
    return true;
}

bool DexVm::SetInstanceField(jobject obj, std::string_view field_name, std::string_view type_desc,
                             uint64_t val)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> field_lock(syn->field_mutex);
    std::string key(field_name);
    if (type_desc == "J") {
        syn->long_fields[key] = static_cast<int64_t>(val);
    } else if (type_desc == "D") {
        syn->double_fields[key] = BitsToDouble(val);
        syn->long_fields[key] = static_cast<int64_t>(val);
    } else if (!type_desc.empty() && (type_desc.front() == 'L' || type_desc.front() == '[')) {
        syn->object_fields[key] = reinterpret_cast<jobject>(static_cast<uintptr_t>(val));
    } else if (type_desc == "F") {
        syn->float_fields[key] = BitsToFloat(val);
    } else if (type_desc == "Z") {
        syn->bool_fields[key] = static_cast<uint8_t>(val != 0 ? 1 : 0);
    } else {
        syn->int_fields[key] = static_cast<int32_t>(val);
    }
    return true;
}

jobject DexVm::NewObject(std::string_view class_name, const DexMethodDef *ctor,
                         std::span<const uint64_t> ctor_args, JNIEnv *env)
{
    std::string desc = DexFile::NormalizeDescriptor(class_name);
    EnsureClassInitialized(desc, env);
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = DexFile::DescriptorToSlashName(desc);
    jobject obj = JniEnvironment::Instance().RegisterObject(syn);

    if (ctor) {
        const DexFile *owner = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            (void)FindClassUnlocked(desc, &owner);
        }
        if (owner) {
            std::vector<uint64_t> full_args;
            full_args.push_back(reinterpret_cast<uintptr_t>(obj));
            auto packed = PackJniArgs(ctor->param_types, ctor_args);
            full_args.insert(full_args.end(), packed.begin(), packed.end());
            ExecuteMethod(owner, ctor, full_args, env);
        }
    }
    return obj;
}

jobject DexVm::AllocateArray(std::string_view array_desc, size_t length)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = std::string(array_desc);
    char elem = (array_desc.size() > 1) ? array_desc[1] : 'I';
    switch (elem) {
    case 'Z':
        syn->type = JniObjectType::BooleanArray;
        syn->bool_data.assign(length, 0);
        break;
    case 'B':
        syn->type = JniObjectType::ByteArray;
        syn->byte_data.assign(length, 0);
        break;
    case 'C':
        syn->type = JniObjectType::CharArray;
        syn->char_data.assign(length, 0);
        break;
    case 'S':
        syn->type = JniObjectType::ShortArray;
        syn->short_data.assign(length, 0);
        break;
    case 'I':
        syn->type = JniObjectType::IntArray;
        syn->int_data.assign(length, 0);
        break;
    case 'J':
        syn->type = JniObjectType::LongArray;
        syn->long_data.assign(length, 0);
        break;
    case 'F':
        syn->type = JniObjectType::FloatArray;
        syn->float_data.assign(length, 0.0f);
        break;
    case 'D':
        syn->type = JniObjectType::DoubleArray;
        syn->double_data.assign(length, 0.0);
        break;
    default:
        syn->type = JniObjectType::ObjectArray;
        syn->object_data.assign(length, nullptr);
        break;
    }
    return JniEnvironment::Instance().RegisterObject(syn);
}

bool DexVm::LoadNativeLibrary(std::string_view lib_name, JNIEnv *, std::string *out_error)
{
    if (lib_name.empty()) {
        if (out_error) {
            *out_error = "empty library name";
        }
        return false;
    }

    std::string mapped(lib_name);
    if (mapped.find('/') == std::string::npos && !mapped.ends_with(".so")) {
        if (!mapped.starts_with("lib")) {
            mapped = "lib" + mapped;
        }
        mapped += ".so";
    }

    void *handle = elf::LinkMap::Instance().DlOpen(mapped.c_str(), elf::RTLD_NOW);
    if (!handle) {
        if (out_error) {
            const char *err = elf::LinkMap::Instance().DlError();
            *out_error = err ? err : ("dlopen failed for " + mapped);
        }
        return false;
    }

    bool should_call_onload = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!jni_onload_done_.contains(handle)) {
            jni_onload_done_.insert(handle);
            should_call_onload = true;
        }
    }

    if (should_call_onload) {
        void *sym = elf::LinkMap::Instance().DlSym(handle, "JNI_OnLoad");
        if (sym) {
            using JniOnLoadFn = jint(__attribute__((sysv_abi)) *)(JavaVM *, void *);
            auto fn = reinterpret_cast<JniOnLoadFn>(sym);
            (void)fn(JniEnvironment::Instance().GetJavaVM(), nullptr);
        }
    }
    return true;
}

jobject DexVm::GetMainLooperObject()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!main_looper_obj_) {
        auto syn = std::make_shared<JniSyntheticObject>();
        syn->type = JniObjectType::Generic;
        syn->class_name = "android/os/Looper";
        main_looper_obj_ = JniEnvironment::Instance().RegisterObject(syn);
    }
    return main_looper_obj_;
}

jobject DexVm::GetMainHandlerObject()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!main_handler_obj_) {
        auto syn = std::make_shared<JniSyntheticObject>();
        syn->type = JniObjectType::Generic;
        syn->class_name = "android/os/Handler";
        syn->object_fields["looper"] = GetMainLooperObject();
        main_handler_obj_ = JniEnvironment::Instance().RegisterObject(syn);
    }
    return main_handler_obj_;
}

void DexVm::PostToMainLooper(jobject target_handler, jobject runnable, jobject message,
                             uint64_t delay_ms)
{
    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
    int32_t what = 0;
    if (message) {
        if (auto syn = JniEnvironment::Instance().LookupObject(message)) {
            auto it = syn->int_fields.find("what");
            if (it != syn->int_fields.end()) {
                what = it->second;
            }
        }
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    looper_queue_.push_back(
        {now_ms + delay_ms, target_handler, runnable, message, what, next_looper_seq_++});
}

size_t DexVm::DrainMainLooper(size_t max_iterations, JNIEnv *env)
{
    if (!env) {
        env = JniEnvironment::Instance().GetJNIEnv();
    }
    size_t executed = 0;
    while (executed < max_iterations) {
        LooperTask task{};
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (main_looper_quit_ || looper_queue_.empty()) {
                break;
            }
            auto best = std::min_element(looper_queue_.begin(), looper_queue_.end(),
                                         [](const LooperTask &a, const LooperTask &b) {
                                             if (a.execute_at_ms != b.execute_at_ms) {
                                                 return a.execute_at_ms < b.execute_at_ms;
                                             }
                                             return a.seq < b.seq;
                                         });
            task = *best;
            looper_queue_.erase(best);
        }

        if (task.runnable) {
            auto syn = JniEnvironment::Instance().LookupObject(task.runnable);
            if (syn && !syn->class_name.empty()) {
                auto resolved = ResolveMethod(syn->class_name, "run", "()V", true);
                if (resolved.has_value()) {
                    std::array<uint64_t, 1> args = {reinterpret_cast<uintptr_t>(task.runnable)};
                    (void)ExecuteMethod(resolved->dex, resolved->method, args, env);
                }
            }
        } else if (task.target && task.message) {
            auto syn = JniEnvironment::Instance().LookupObject(task.target);
            if (syn && !syn->class_name.empty()) {
                auto resolved = ResolveMethod(syn->class_name, "handleMessage",
                                              "(Landroid/os/Message;)V", true);
                if (resolved.has_value()) {
                    std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(task.target),
                                                    reinterpret_cast<uintptr_t>(task.message)};
                    (void)ExecuteMethod(resolved->dex, resolved->method, args, env);
                }
            }
        }
        ++executed;
    }
    return executed;
}

void DexVm::QuitMainLooper()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    main_looper_quit_ = true;
}

bool DexVm::IsMainLooperQuit() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return main_looper_quit_;
}

DexValue DexVm::InvokeIntrinsic(const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                                bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    handled = false;
    auto &jni = JniEnvironment::Instance();
    std::string cls = DexFile::DescriptorToSlashName(ref.class_descriptor);

    DexValue host_res = JavaRuntime::Instance().Invoke(*this, cls, ref, arg_regs, is_static, env,
                                                       out_exception, handled);
    if (handled) {
        return host_res;
    }

    if (cls == "java/lang/Object") {
        if (ref.name == "<init>") {
            handled = true;
            return {0, 'V'};
        }
        if (ref.name == "hashCode" && !is_static && !arg_regs.empty()) {
            handled = true;
            int32_t h = JavaRuntime::ComputeObjectHashCode(
                *this, reinterpret_cast<jobject>(arg_regs[0]), env, out_exception);
            return {static_cast<uint64_t>(static_cast<uint32_t>(h)), 'I'};
        }
        if (ref.name == "equals" && !is_static && arg_regs.size() >= 2) {
            handled = true;
            bool eq = JavaRuntime::ComputeObjectEquals(
                *this, reinterpret_cast<jobject>(arg_regs[0]),
                reinterpret_cast<jobject>(arg_regs[1]), env, out_exception);
            return {static_cast<uint64_t>(eq ? 1 : 0), 'Z'};
        }
        if (ref.name == "toString" && !is_static && !arg_regs.empty()) {
            handled = true;
            std::string str = JavaRuntime::ObjectToString(
                *this, reinterpret_cast<jobject>(arg_regs[0]), env, out_exception);
            jstring s = jni.CreateString(str);
            return {reinterpret_cast<uintptr_t>(s), 'L'};
        }
        if (ref.name == "getClass" && !is_static && !arg_regs.empty()) {
            jobject obj = reinterpret_cast<jobject>(arg_regs[0]);
            std::string cls_name = "java/lang/Object";
            if (jni.GetStringChars(obj) != nullptr) {
                cls_name = "java/lang/String";
            } else if (auto syn = jni.LookupObject(obj); syn && !syn->class_name.empty()) {
                cls_name = syn->class_name;
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(cls_name)),
                    'L'};
        }
    }

    if (cls == "java/lang/Math" && is_static) {
        if (ref.name == "abs" && !arg_regs.empty() && !ref.param_types.empty()) {
            if (ref.param_types[0] == "I") {
                int32_t v = static_cast<int32_t>(arg_regs[0]);
                handled = true;
                return {static_cast<uint64_t>(static_cast<uint32_t>(v < 0 ? -v : v)), 'I'};
            }
            if (ref.param_types[0] == "F") {
                handled = true;
                return {FloatToBits(std::fabs(BitsToFloat(arg_regs[0]))), 'F'};
            }
        }
        if (ref.name == "min" && arg_regs.size() >= 2 && ref.param_types.size() >= 2 &&
            ref.param_types[0] == "I") {
            int32_t a = static_cast<int32_t>(arg_regs[0]);
            int32_t b = static_cast<int32_t>(arg_regs[1]);
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(a < b ? a : b)), 'I'};
        }
        if (ref.name == "max" && arg_regs.size() >= 2 && ref.param_types.size() >= 2 &&
            ref.param_types[0] == "I") {
            int32_t a = static_cast<int32_t>(arg_regs[0]);
            int32_t b = static_cast<int32_t>(arg_regs[1]);
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(a > b ? a : b)), 'I'};
        }
        if (ref.name == "sqrt" && arg_regs.size() >= 2 && ref.param_types.size() >= 1 &&
            ref.param_types[0] == "D") {
            uint64_t bits = (arg_regs[0] & 0xFFFFFFFFULL) | ((arg_regs[1] & 0xFFFFFFFFULL) << 32);
            handled = true;
            return {DoubleToBits(std::sqrt(BitsToDouble(bits))), 'D'};
        }
    }

    if (cls == "java/lang/System" && is_static) {
        if (ref.name == "currentTimeMillis") {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            uint64_t ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
            handled = true;
            return {ms, 'J'};
        }
        if (ref.name == "nanoTime") {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            uint64_t ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
            handled = true;
            return {ns, 'J'};
        }
        if ((ref.name == "loadLibrary" || ref.name == "load") && !arg_regs.empty()) {
            const char *lib_str = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
            std::string err;
            if (!lib_str || !LoadNativeLibrary(lib_str, env, &err)) {
                auto ex = std::make_shared<JniSyntheticObject>();
                ex->type = JniObjectType::Generic;
                ex->class_name = "java/lang/UnsatisfiedLinkError";
                ex->string_fields["detailMessage"] =
                    err.empty() ? std::string("dlopen failed: ") + (lib_str ? lib_str : "null")
                                : err;
                out_exception = jni.RegisterObject(ex);
            }
            handled = true;
            return {0, 'V'};
        }
        if (ref.name == "arraycopy" && arg_regs.size() >= 5) {
            auto src = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            int32_t src_pos = static_cast<int32_t>(arg_regs[1]);
            auto dst = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[2]));
            int32_t dst_pos = static_cast<int32_t>(arg_regs[3]);
            int32_t count = static_cast<int32_t>(arg_regs[4]);
            if (src && dst && src_pos >= 0 && dst_pos >= 0 && count > 0 && src->type == dst->type) {
                auto copy_vec = [&](auto &svec, auto &dvec) {
                    if (static_cast<size_t>(src_pos + count) <= svec.size() &&
                        static_cast<size_t>(dst_pos + count) <= dvec.size()) {
                        std::copy_n(svec.begin() + src_pos, count, dvec.begin() + dst_pos);
                    }
                };
                switch (src->type) {
                case JniObjectType::BooleanArray:
                    copy_vec(src->bool_data, dst->bool_data);
                    break;
                case JniObjectType::ByteArray:
                    copy_vec(src->byte_data, dst->byte_data);
                    break;
                case JniObjectType::CharArray:
                    copy_vec(src->char_data, dst->char_data);
                    break;
                case JniObjectType::ShortArray:
                    copy_vec(src->short_data, dst->short_data);
                    break;
                case JniObjectType::IntArray:
                    copy_vec(src->int_data, dst->int_data);
                    break;
                case JniObjectType::LongArray:
                    copy_vec(src->long_data, dst->long_data);
                    break;
                case JniObjectType::FloatArray:
                    copy_vec(src->float_data, dst->float_data);
                    break;
                case JniObjectType::DoubleArray:
                    copy_vec(src->double_data, dst->double_data);
                    break;
                case JniObjectType::ObjectArray:
                    copy_vec(src->object_data, dst->object_data);
                    break;
                default:
                    break;
                }
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "java/lang/Runtime") {
        if (ref.name == "getRuntime" && is_static) {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (!runtime_obj_) {
                auto syn = std::make_shared<JniSyntheticObject>();
                syn->type = JniObjectType::Generic;
                syn->class_name = "java/lang/Runtime";
                runtime_obj_ = jni.RegisterObject(syn);
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(runtime_obj_), 'L'};
        }
        if ((ref.name == "loadLibrary" || ref.name == "load") && !is_static &&
            arg_regs.size() >= 2) {
            const char *lib_str = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string err;
            if (!lib_str || !LoadNativeLibrary(lib_str, env, &err)) {
                auto ex = std::make_shared<JniSyntheticObject>();
                ex->type = JniObjectType::Generic;
                ex->class_name = "java/lang/UnsatisfiedLinkError";
                ex->string_fields["detailMessage"] =
                    err.empty() ? std::string("dlopen failed: ") + (lib_str ? lib_str : "null")
                                : err;
                out_exception = jni.RegisterObject(ex);
            }
            handled = true;
            return {0, 'V'};
        }
        if (ref.name == "loadLibrary0" && !is_static && arg_regs.size() >= 3) {
            const char *lib_str = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]));
            std::string err;
            if (!lib_str || !LoadNativeLibrary(lib_str, env, &err)) {
                auto ex = std::make_shared<JniSyntheticObject>();
                ex->type = JniObjectType::Generic;
                ex->class_name = "java/lang/UnsatisfiedLinkError";
                ex->string_fields["detailMessage"] = err;
                out_exception = jni.RegisterObject(ex);
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "java/lang/Exception" || cls == "java/lang/RuntimeException" ||
        cls == "java/lang/Throwable" || cls == "java/lang/IllegalStateException" ||
        cls == "java/lang/IllegalArgumentException" || cls == "java/lang/UnsatisfiedLinkError") {
        if (ref.name == "<init>" && !is_static && !arg_regs.empty()) {
            auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            if (syn && arg_regs.size() >= 2 && !ref.param_types.empty() &&
                ref.param_types[0] == "Ljava/lang/String;") {
                const char *msg = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
                syn->string_fields["detailMessage"] = msg ? msg : "";
            }
            handled = true;
            return {0, 'V'};
        }
        if (ref.name == "getMessage" && !is_static && !arg_regs.empty()) {
            auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            jstring s = jni.CreateString(syn ? syn->string_fields["detailMessage"] : "");
            handled = true;
            return {reinterpret_cast<uintptr_t>(s), 'L'};
        }
    }

    if (cls == "android/util/Log" && is_static && arg_regs.size() >= 2) {
        const char *tag = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
        const char *msg = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        int prio = 4;
        if (ref.name == "v") {
            prio = 2;
        } else if (ref.name == "d") {
            prio = 3;
        } else if (ref.name == "w") {
            prio = 5;
        } else if (ref.name == "e") {
            prio = 6;
        }
        ndk::LogcatBuffer::Instance().Append(prio, tag ? tag : "DexVm", msg ? msg : "");
        handled = true;
        return {0, 'I'};
    }

    if (cls == "android/os/Looper") {
        if (is_static && (ref.name == "prepare" || ref.name == "prepareMainLooper")) {
            (void)GetMainLooperObject();
            handled = true;
            return {0, 'V'};
        }
        if (is_static && (ref.name == "myLooper" || ref.name == "getMainLooper")) {
            handled = true;
            return {reinterpret_cast<uintptr_t>(GetMainLooperObject()), 'L'};
        }
        if (is_static && ref.name == "loop") {
            DrainMainLooper(1024, env);
            handled = true;
            return {0, 'V'};
        }
        if (!is_static && (ref.name == "quit" || ref.name == "quitSafely")) {
            QuitMainLooper();
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/os/Message") {
        if (is_static && ref.name == "obtain") {
            auto msg = std::make_shared<JniSyntheticObject>();
            msg->type = JniObjectType::Generic;
            msg->class_name = "android/os/Message";
            if (!arg_regs.empty() && !ref.param_types.empty() &&
                ref.param_types[0] == "Landroid/os/Handler;") {
                msg->object_fields["target"] = reinterpret_cast<jobject>(arg_regs[0]);
                if (arg_regs.size() >= 2 && ref.param_types.size() >= 2 &&
                    ref.param_types[1] == "I") {
                    msg->int_fields["what"] = static_cast<int32_t>(arg_regs[1]);
                }
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(msg)), 'L'};
        }
        if (!is_static && ref.name == "<init>" && !arg_regs.empty()) {
            handled = true;
            return {0, 'V'};
        }
        if (!is_static && ref.name == "sendToTarget" && !arg_regs.empty()) {
            jobject msg_obj = reinterpret_cast<jobject>(arg_regs[0]);
            auto syn = jni.LookupObject(msg_obj);
            jobject target = syn ? syn->object_fields["target"] : nullptr;
            if (target) {
                PostToMainLooper(target, nullptr, msg_obj, 0);
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/os/Handler") {
        if (!is_static && ref.name == "<init>" && !arg_regs.empty()) {
            auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            if (syn) {
                syn->object_fields["looper"] = (arg_regs.size() >= 2 && !ref.param_types.empty() &&
                                                ref.param_types[0] == "Landroid/os/Looper;")
                                                   ? reinterpret_cast<jobject>(arg_regs[1])
                                                   : GetMainLooperObject();
            }
            handled = true;
            return {0, 'V'};
        }
        if (!is_static && ref.name == "post" && arg_regs.size() >= 2) {
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]),
                             reinterpret_cast<jobject>(arg_regs[1]), nullptr, 0);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "postDelayed" && arg_regs.size() >= 4) {
            uint64_t delay_ms =
                (arg_regs[2] & 0xFFFFFFFFULL) | ((arg_regs[3] & 0xFFFFFFFFULL) << 32);
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]),
                             reinterpret_cast<jobject>(arg_regs[1]), nullptr, delay_ms);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "sendMessage" && arg_regs.size() >= 2) {
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]), nullptr,
                             reinterpret_cast<jobject>(arg_regs[1]), 0);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "sendMessageDelayed" && arg_regs.size() >= 4) {
            uint64_t delay_ms =
                (arg_regs[2] & 0xFFFFFFFFULL) | ((arg_regs[3] & 0xFFFFFFFFULL) << 32);
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]), nullptr,
                             reinterpret_cast<jobject>(arg_regs[1]), delay_ms);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "sendEmptyMessage" && arg_regs.size() >= 2) {
            auto msg = std::make_shared<JniSyntheticObject>();
            msg->type = JniObjectType::Generic;
            msg->class_name = "android/os/Message";
            msg->int_fields["what"] = static_cast<int32_t>(arg_regs[1]);
            jobject msg_obj = jni.RegisterObject(msg);
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]), nullptr, msg_obj, 0);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "sendEmptyMessageDelayed" && arg_regs.size() >= 4) {
            auto msg = std::make_shared<JniSyntheticObject>();
            msg->type = JniObjectType::Generic;
            msg->class_name = "android/os/Message";
            msg->int_fields["what"] = static_cast<int32_t>(arg_regs[1]);
            uint64_t delay_ms =
                (arg_regs[2] & 0xFFFFFFFFULL) | ((arg_regs[3] & 0xFFFFFFFFULL) << 32);
            jobject msg_obj = jni.RegisterObject(msg);
            PostToMainLooper(reinterpret_cast<jobject>(arg_regs[0]), nullptr, msg_obj, delay_ms);
            handled = true;
            return {1, 'Z'};
        }
        if (!is_static && ref.name == "obtainMessage" && !arg_regs.empty()) {
            auto msg = std::make_shared<JniSyntheticObject>();
            msg->type = JniObjectType::Generic;
            msg->class_name = "android/os/Message";
            msg->object_fields["target"] = reinterpret_cast<jobject>(arg_regs[0]);
            if (arg_regs.size() >= 2 && !ref.param_types.empty() && ref.param_types[0] == "I") {
                msg->int_fields["what"] = static_cast<int32_t>(arg_regs[1]);
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(msg)), 'L'};
        }
        if (!is_static && ref.name == "removeCallbacks" && arg_regs.size() >= 2) {
            jobject target = reinterpret_cast<jobject>(arg_regs[0]);
            jobject runnable = reinterpret_cast<jobject>(arg_regs[1]);
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            std::erase_if(looper_queue_, [&](const LooperTask &t) {
                return t.target == target && t.runnable == runnable;
            });
            handled = true;
            return {0, 'V'};
        }
        if (!is_static && ref.name == "removeMessages" && arg_regs.size() >= 2) {
            jobject target = reinterpret_cast<jobject>(arg_regs[0]);
            int32_t what = static_cast<int32_t>(arg_regs[1]);
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            std::erase_if(looper_queue_, [&](const LooperTask &t) {
                return t.target == target && t.message != nullptr && t.what == what;
            });
            handled = true;
            return {0, 'V'};
        }
        if (!is_static && ref.name == "handleMessage") {
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/os/Bundle") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (ref.name == "<init>") {
            handled = true;
            return {0, 'V'};
        }
        if (syn && arg_regs.size() >= 3) {
            const char *key_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string key = key_c ? key_c : "";
            if (ref.name == "putString") {
                const char *val_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]));
                syn->string_fields[key] = val_c ? val_c : "";
                handled = true;
                return {0, 'V'};
            }
            if (ref.name == "getString") {
                auto it = syn->string_fields.find(key);
                jobject res = (it != syn->string_fields.end())
                                  ? jni.CreateString(it->second)
                                  : reinterpret_cast<jobject>(arg_regs[2]);
                handled = true;
                return {reinterpret_cast<uintptr_t>(res), 'L'};
            }
            if (ref.name == "putInt") {
                syn->int_fields[key] = static_cast<int32_t>(arg_regs[2]);
                handled = true;
                return {0, 'V'};
            }
            if (ref.name == "getInt") {
                auto it = syn->int_fields.find(key);
                int32_t val =
                    (it != syn->int_fields.end()) ? it->second : static_cast<int32_t>(arg_regs[2]);
                handled = true;
                return {static_cast<uint64_t>(static_cast<uint32_t>(val)), 'I'};
            }
            if (ref.name == "putBoolean") {
                syn->bool_fields[key] = arg_regs[2] ? 1 : 0;
                handled = true;
                return {0, 'V'};
            }
            if (ref.name == "getBoolean") {
                auto it = syn->bool_fields.find(key);
                uint64_t val =
                    (it != syn->bool_fields.end()) ? (it->second ? 1 : 0) : (arg_regs[2] ? 1 : 0);
                handled = true;
                return {val, 'Z'};
            }
        }
        if (syn && arg_regs.size() >= 2) {
            const char *key_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string key = key_c ? key_c : "";
            if (ref.name == "getString") {
                auto it = syn->string_fields.find(key);
                jobject res =
                    (it != syn->string_fields.end()) ? jni.CreateString(it->second) : nullptr;
                handled = true;
                return {reinterpret_cast<uintptr_t>(res), 'L'};
            }
            if (ref.name == "containsKey") {
                bool found = syn->string_fields.contains(key) || syn->int_fields.contains(key) ||
                             syn->bool_fields.contains(key);
                handled = true;
                return {static_cast<uint64_t>(found ? 1 : 0), 'Z'};
            }
        }
    }

    if (cls == "android/content/Intent") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (syn && ref.name == "<init>") {
            auto bundle = std::make_shared<JniSyntheticObject>();
            bundle->type = JniObjectType::Bundle;
            bundle->class_name = "android/os/Bundle";
            syn->object_fields["extras"] = jni.RegisterObject(bundle);
            if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
                ref.param_types[0] == "Ljava/lang/String;") {
                const char *act = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
                syn->string_fields["action"] = act ? act : "";
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "getAction") {
            auto it = syn->string_fields.find("action");
            jstring s = (it != syn->string_fields.end() && !it->second.empty())
                            ? jni.CreateString(it->second)
                            : nullptr;
            handled = true;
            return {reinterpret_cast<uintptr_t>(s), 'L'};
        }
        if (syn && ref.name == "setAction" && arg_regs.size() >= 2) {
            const char *act = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            syn->string_fields["action"] = act ? act : "";
            handled = true;
            return {arg_regs[0], 'L'};
        }
        if (syn && ref.name == "getExtras") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(syn->object_fields["extras"]), 'L'};
        }
        if (syn && arg_regs.size() >= 3) {
            auto extras = jni.LookupObject(syn->object_fields["extras"]);
            const char *key_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string key = key_c ? key_c : "";
            if (extras && ref.name == "putExtra") {
                if (ref.param_types.size() >= 2 && ref.param_types[1] == "Ljava/lang/String;") {
                    const char *val_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]));
                    extras->string_fields[key] = val_c ? val_c : "";
                } else if (ref.param_types.size() >= 2 && ref.param_types[1] == "I") {
                    extras->int_fields[key] = static_cast<int32_t>(arg_regs[2]);
                } else if (ref.param_types.size() >= 2 && ref.param_types[1] == "Z") {
                    extras->bool_fields[key] = arg_regs[2] ? 1 : 0;
                }
                handled = true;
                return {arg_regs[0], 'L'};
            }
            if (extras && ref.name == "getIntExtra") {
                auto it = extras->int_fields.find(key);
                int32_t val = (it != extras->int_fields.end()) ? it->second
                                                               : static_cast<int32_t>(arg_regs[2]);
                handled = true;
                return {static_cast<uint64_t>(static_cast<uint32_t>(val)), 'I'};
            }
            if (extras && ref.name == "getBooleanExtra") {
                auto it = extras->bool_fields.find(key);
                uint64_t val = (it != extras->bool_fields.end()) ? (it->second ? 1 : 0)
                                                                 : (arg_regs[2] ? 1 : 0);
                handled = true;
                return {val, 'Z'};
            }
        }
        if (syn && ref.name == "getStringExtra" && arg_regs.size() >= 2) {
            auto extras = jni.LookupObject(syn->object_fields["extras"]);
            const char *key_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string key = key_c ? key_c : "";
            jstring out = nullptr;
            if (extras) {
                auto it = extras->string_fields.find(key);
                if (it != extras->string_fields.end()) {
                    out = jni.CreateString(it->second);
                }
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(out), 'L'};
        }
    }

    if (cls == "android/graphics/Paint") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (ref.name == "<init>") {
            if (syn) {
                syn->int_fields["color"] = static_cast<int32_t>(0xFFFFFFFFu);
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "setColor" && arg_regs.size() >= 2) {
            syn->int_fields["color"] = static_cast<int32_t>(arg_regs[1]);
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "getColor") {
            auto it = syn->int_fields.find("color");
            int32_t color =
                (it != syn->int_fields.end()) ? it->second : static_cast<int32_t>(0xFFFFFFFFu);
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(color)), 'I'};
        }
    }

    if (cls == "android/graphics/Rect") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (ref.name == "<init>") {
            if (syn && arg_regs.size() >= 5) {
                syn->int_fields["left"] = static_cast<int32_t>(arg_regs[1]);
                syn->int_fields["top"] = static_cast<int32_t>(arg_regs[2]);
                syn->int_fields["right"] = static_cast<int32_t>(arg_regs[3]);
                syn->int_fields["bottom"] = static_cast<int32_t>(arg_regs[4]);
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/graphics/Canvas") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (syn && ref.name == "getWidth") {
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(syn->int_fields["width"])), 'I'};
        }
        if (syn && ref.name == "getHeight") {
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(syn->int_fields["height"])), 'I'};
        }
        if (syn && ref.name == "drawColor" && arg_regs.size() >= 2) {
            uint32_t argb = static_cast<uint32_t>(arg_regs[1]);
            uint32_t rgba = ((argb & 0x00FF0000u) >> 16) | (argb & 0x0000FF00u) |
                            ((argb & 0x000000FFu) << 16) | (argb & 0xFF000000u);
            auto &px = syn->binary_fields["pixels"];
            for (size_t i = 0; i + 4 <= px.size(); i += 4) {
                std::memcpy(px.data() + i, &rgba, sizeof(uint32_t));
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "drawRect") {
            int32_t w = syn->int_fields["width"];
            int32_t h = syn->int_fields["height"];
            int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            jobject paint_obj = nullptr;
            if (arg_regs.size() >= 6 && !ref.param_types.empty() && ref.param_types[0] == "F") {
                x0 = static_cast<int32_t>(BitsToFloat(arg_regs[1]));
                y0 = static_cast<int32_t>(BitsToFloat(arg_regs[2]));
                x1 = static_cast<int32_t>(BitsToFloat(arg_regs[3]));
                y1 = static_cast<int32_t>(BitsToFloat(arg_regs[4]));
                paint_obj = reinterpret_cast<jobject>(arg_regs[5]);
            } else if (arg_regs.size() >= 3 && !ref.param_types.empty() &&
                       ref.param_types[0] == "Landroid/graphics/Rect;") {
                auto rect = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
                if (rect) {
                    x0 = rect->int_fields["left"];
                    y0 = rect->int_fields["top"];
                    x1 = rect->int_fields["right"];
                    y1 = rect->int_fields["bottom"];
                }
                paint_obj = reinterpret_cast<jobject>(arg_regs[2]);
            }
            uint32_t argb = 0xFFFFFFFFu;
            if (auto paint = jni.LookupObject(paint_obj)) {
                auto it = paint->int_fields.find("color");
                if (it != paint->int_fields.end()) {
                    argb = static_cast<uint32_t>(it->second);
                }
            }
            uint32_t rgba = ((argb & 0x00FF0000u) >> 16) | (argb & 0x0000FF00u) |
                            ((argb & 0x000000FFu) << 16) | (argb & 0xFF000000u);
            x0 = std::max<int32_t>(0, x0);
            y0 = std::max<int32_t>(0, y0);
            x1 = std::min<int32_t>(w, x1);
            y1 = std::min<int32_t>(h, y1);
            auto &px = syn->binary_fields["pixels"];
            for (int32_t y = y0; y < y1; ++y) {
                for (int32_t x = x0; x < x1; ++x) {
                    size_t off =
                        (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) *
                        4;
                    if (off + 4 <= px.size()) {
                        std::memcpy(px.data() + off, &rgba, sizeof(uint32_t));
                    }
                }
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/view/Surface") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (syn && ref.name == "<init>") {
            if (!syn->native_ptr) {
                syn->native_ptr = jni.GetActiveNativeWindow();
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "isValid") {
            handled = true;
            return {static_cast<uint64_t>(syn->native_ptr ? 1 : 0), 'Z'};
        }
        if (syn && ref.name == "release") {
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/view/SurfaceView" || cls == "android/opengl/GLSurfaceView") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (syn && ref.name == "<init>") {
            void *win = jni.GetActiveNativeWindow();
            auto surf = std::make_shared<JniSyntheticObject>();
            surf->type = JniObjectType::Generic;
            surf->class_name = "android/view/Surface";
            surf->native_ptr = win;
            jobject surf_obj = jni.RegisterObject(surf);

            auto holder = std::make_shared<JniSyntheticObject>();
            holder->type = JniObjectType::Generic;
            holder->class_name = "android/view/SurfaceHolder";
            holder->native_ptr = win;
            holder->object_fields["surface"] = surf_obj;
            jobject holder_obj = jni.RegisterObject(holder);

            syn->native_ptr = win;
            syn->object_fields["holder"] = holder_obj;
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "getHolder") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(syn->object_fields["holder"]), 'L'};
        }
        if (syn && ref.name == "setRenderer" && arg_regs.size() >= 2) {
            syn->object_fields["renderer"] = reinterpret_cast<jobject>(arg_regs[1]);
            handled = true;
            return {0, 'V'};
        }
        if (syn && (ref.name == "setEGLContextClientVersion" || ref.name == "onResume" ||
                    ref.name == "onPause" || ref.name == "requestRender")) {
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/view/SurfaceHolder") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (syn && ref.name == "addCallback" && arg_regs.size() >= 2) {
            jobject cb = reinterpret_cast<jobject>(arg_regs[1]);
            if (cb && std::find(syn->object_data.begin(), syn->object_data.end(), cb) ==
                          syn->object_data.end()) {
                syn->object_data.push_back(cb);
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "removeCallback" && arg_regs.size() >= 2) {
            jobject cb = reinterpret_cast<jobject>(arg_regs[1]);
            std::erase(syn->object_data, cb);
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "getSurface") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(syn->object_fields["surface"]), 'L'};
        }
        if (syn && ref.name == "lockCanvas") {
            auto *win = reinterpret_cast<ANativeWindow *>(
                syn->native_ptr ? syn->native_ptr : jni.GetActiveNativeWindow());
            int32_t w = win && win->width > 0 ? win->width : jni.GetDisplayWidth();
            int32_t h = win && win->height > 0 ? win->height : jni.GetDisplayHeight();
            auto canvas = std::make_shared<JniSyntheticObject>();
            canvas->type = JniObjectType::Generic;
            canvas->class_name = "android/graphics/Canvas";
            canvas->native_ptr = win;
            canvas->int_fields["width"] = w;
            canvas->int_fields["height"] = h;
            canvas->binary_fields["pixels"].assign(
                static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(canvas)), 'L'};
        }
        if (syn && ref.name == "unlockCanvasAndPost" && arg_regs.size() >= 2) {
            auto canvas = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            auto *win = reinterpret_cast<ANativeWindow *>(
                canvas && canvas->native_ptr
                    ? canvas->native_ptr
                    : (syn->native_ptr ? syn->native_ptr : jni.GetActiveNativeWindow()));
            if (canvas && win) {
                ANativeWindow_Buffer buf{};
                if (ANativeWindow_lock(win, &buf, nullptr) == 0 && buf.bits) {
                    int32_t cw = canvas->int_fields["width"];
                    int32_t ch = canvas->int_fields["height"];
                    int32_t copy_w = std::min(cw, buf.width);
                    int32_t copy_h = std::min(ch, buf.height);
                    const auto &src = canvas->binary_fields["pixels"];
                    auto *dst = static_cast<uint8_t *>(buf.bits);
                    for (int32_t y = 0; y < copy_h; ++y) {
                        std::memcpy(
                            dst + static_cast<size_t>(y) * static_cast<size_t>(buf.stride) * 4,
                            src.data() + static_cast<size_t>(y) * static_cast<size_t>(cw) * 4,
                            static_cast<size_t>(copy_w) * 4);
                    }
                    ANativeWindow_unlockAndPost(win);
                }
            }
            handled = true;
            return {0, 'V'};
        }
    }

    if (cls == "android/app/Activity" || cls == "android/app/NativeActivity" ||
        cls == "android/app/Application" || cls == "android/content/ContextWrapper" ||
        cls == "android/content/Context") {
        auto syn = (!is_static && !arg_regs.empty())
                       ? jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]))
                       : nullptr;
        if (ref.name == "<init>" || ref.name == "onCreate" || ref.name == "onStart" ||
            ref.name == "onResume" || ref.name == "onPause" || ref.name == "onStop" ||
            ref.name == "onDestroy" || ref.name == "attachBaseContext") {
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "setContentView" && arg_regs.size() >= 2) {
            if (!ref.param_types.empty() && ref.param_types[0] == "Landroid/view/View;") {
                syn->object_fields["contentView"] = reinterpret_cast<jobject>(arg_regs[1]);
            } else {
                syn->int_fields["contentViewId"] = static_cast<int32_t>(arg_regs[1]);
            }
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "findViewById") {
            auto it = syn->object_fields.find("contentView");
            jobject v = (it != syn->object_fields.end()) ? it->second : jni.GetDecorViewObject();
            handled = true;
            return {reinterpret_cast<uintptr_t>(v), 'L'};
        }
        if (ref.name == "getWindow") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.GetWindowObject()), 'L'};
        }
        if (ref.name == "getIntent") {
            jobject intent = (syn && syn->object_fields.contains("intent"))
                                 ? syn->object_fields["intent"]
                                 : jni.GetIntentObject();
            handled = true;
            return {reinterpret_cast<uintptr_t>(intent), 'L'};
        }
        if (syn && ref.name == "setIntent" && arg_regs.size() >= 2) {
            syn->object_fields["intent"] = reinterpret_cast<jobject>(arg_regs[1]);
            handled = true;
            return {0, 'V'};
        }
        if (ref.name == "getApplication" || ref.name == "getApplicationContext") {
            jobject app = jni.GetApplicationObject();
            if (!app) {
                app = jni.GetContextObject();
            }
            handled = true;
            return {reinterpret_cast<uintptr_t>(app), 'L'};
        }
        if (ref.name == "getPackageName") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.CreateString(jni.GetPackageName())), 'L'};
        }
        if (ref.name == "getAssets") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.GetContextObject()), 'L'};
        }
        if (ref.name == "getResources") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.GetResourcesObject()), 'L'};
        }
        if (ref.name == "runOnUiThread" && arg_regs.size() >= 2) {
            PostToMainLooper(GetMainHandlerObject(), reinterpret_cast<jobject>(arg_regs[1]),
                             nullptr, 0);
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "finish") {
            syn->bool_fields["finishing"] = 1;
            QuitMainLooper();
            handled = true;
            return {0, 'V'};
        }
        if (syn && ref.name == "isFinishing") {
            handled = true;
            return {static_cast<uint64_t>(syn->bool_fields["finishing"] ? 1 : 0), 'Z'};
        }
    }

    return {0, 'V'};
}

DexValue DexVm::InvokeNativeMethod(const DexMethodDef &method, std::span<const uint64_t> raw_args,
                                   JNIEnv *env)
{
    if (!env) {
        env = JniEnvironment::Instance().GetJNIEnv();
    }
    std::string class_slash = DexFile::DescriptorToSlashName(method.class_descriptor);
    void *fn =
        JniEnvironment::Instance().LookupNativeMethod(class_slash, method.name, method.descriptor);
    if (!fn) {
        std::string short_sym = MangleJniName(class_slash, method.name);
        fn = elf::LinkMap::Instance().DlSym(nullptr, short_sym.c_str());
    }
    if (!fn) {
        std::string long_sym = MangleJniName(class_slash, method.name, method.descriptor);
        fn = elf::LinkMap::Instance().DlSym(nullptr, long_sym.c_str());
    }
    if (!fn) {
        return {0, 'V'};
    }

    bool is_static = (method.access_flags & ACC_STATIC) != 0;
    void *obj_or_cls = nullptr;
    size_t param_reg_idx = 0;
    if (is_static) {
        obj_or_cls = const_cast<char *>(class_slash.c_str());
    } else if (!raw_args.empty()) {
        obj_or_cls = reinterpret_cast<void *>(static_cast<uintptr_t>(raw_args[0]));
        param_reg_idx = 1;
    }

    std::vector<uint64_t> call_args;
    for (const auto &pt : method.param_types) {
        if (param_reg_idx >= raw_args.size()) {
            break;
        }
        if (pt == "J" || pt == "D") {
            if (param_reg_idx + 1 < raw_args.size()) {
                call_args.push_back(ReadWide(raw_args, param_reg_idx));
                param_reg_idx += 2;
            }
        } else {
            call_args.push_back(raw_args[param_reg_idx++]);
        }
    }

    char ret_kind = method.return_type.empty() ? 'V' : method.return_type.front();
    if (ret_kind == 'F') {
        float res = DispatchSysvNativeFloat(fn, env, obj_or_cls, call_args);
        return {FloatToBits(res), 'F'};
    }
    if (ret_kind == 'D') {
        double res = DispatchSysvNativeDouble(fn, env, obj_or_cls, call_args);
        return {DoubleToBits(res), 'D'};
    }
    uint64_t res = DispatchSysvNativeU64(fn, env, obj_or_cls, call_args);
    return {res, ret_kind};
}

DexValue DexVm::InvokeTarget(const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, bool is_super, bool is_direct,
                             std::string_view caller_class, JNIEnv *env, jobject &out_exception)
{
    std::string target_class = ref.class_descriptor;
    if (is_super) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (const auto *cur_cls = FindClassUnlocked(caller_class)) {
            if (!cur_cls->superclass_descriptor.empty()) {
                target_class = cur_cls->superclass_descriptor;
            }
        }
    } else if (!is_static && !is_direct && !arg_regs.empty()) {
        jobject recv = reinterpret_cast<jobject>(arg_regs[0]);
        auto &jni = JniEnvironment::Instance();
        if (jni.GetStringChars(recv) != nullptr) {
            target_class = "Ljava/lang/String;";
        } else {
            auto syn = jni.LookupObject(recv);
            if (syn && !syn->class_name.empty()) {
                std::string inst_desc = DexFile::NormalizeDescriptor(syn->class_name);
                if (HasClass(inst_desc) || JavaRuntime::Instance().HasHostClass(syn->class_name)) {
                    target_class = std::move(inst_desc);
                }
            }
        }
    }

    if (!is_static && !is_direct && !arg_regs.empty()) {
        jobject recv = reinterpret_cast<jobject>(arg_regs[0]);
        auto syn = JniEnvironment::Instance().LookupObject(recv);
        if (syn && syn->class_name == "java/lang/invoke/LambdaProxy") {
            bool lambda_handled = false;
            DexValue lres = JavaRuntime::Instance().TryInvokeLambdaProxy(
                *this, recv, ref, arg_regs, env, out_exception, lambda_handled);
            if (lambda_handled) {
                return lres;
            }
        }
    }

    if (is_static) {
        EnsureClassInitialized(target_class, env);
    }

    if (!is_direct) {
        std::string cur = target_class;
        for (int depth = 0; depth < 32 && !cur.empty(); ++depth) {
            const DexFile *found_dex = nullptr;
            const DexMethodDef *found_method = nullptr;
            std::string super_desc;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (const auto *cls = FindClassUnlocked(cur, &found_dex)) {
                    if (found_dex) {
                        found_method = found_dex->FindMethod(cur, ref.name, ref.descriptor);
                    }
                    super_desc = cls->superclass_descriptor;
                }
            }
            if (found_method && found_dex) {
                DexValue result = ExecuteMethod(found_dex, found_method, arg_regs, env);
                if (jthrowable pending = JniEnvironment::Instance().GetPendingException()) {
                    out_exception = pending;
                    JniEnvironment::Instance().ClearPendingException();
                }
                return result;
            }
            DexMethodRef step_ref = ref;
            step_ref.class_descriptor = cur;
            bool step_handled = false;
            DexValue sv =
                InvokeIntrinsic(step_ref, arg_regs, is_static, env, out_exception, step_handled);
            if (step_handled) {
                return sv;
            }
            cur = std::move(super_desc);
        }
    }

    DexMethodRef direct_ref = ref;
    if (is_super) {
        direct_ref.class_descriptor = target_class;
    }
    bool handled = false;
    DexValue intrinsic_val =
        InvokeIntrinsic(direct_ref, arg_regs, is_static, env, out_exception, handled);
    if (handled) {
        return intrinsic_val;
    }

    auto resolved = ResolveMethod(target_class, ref.name, ref.descriptor, true);
    if (!resolved.has_value() || !resolved->dex || !resolved->method) {
        std::string cur = target_class;
        for (int depth = 0; depth < 16; ++depth) {
            std::string super_desc;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (const auto *cls = FindClassUnlocked(cur)) {
                    super_desc = cls->superclass_descriptor;
                }
            }
            if (super_desc.empty()) {
                break;
            }
            DexMethodRef super_ref = ref;
            super_ref.class_descriptor = super_desc;
            bool super_handled = false;
            DexValue sv =
                InvokeIntrinsic(super_ref, arg_regs, is_static, env, out_exception, super_handled);
            if (super_handled) {
                return sv;
            }
            cur = std::move(super_desc);
        }
        return {0, ref.return_type.empty() ? 'V' : ref.return_type.front()};
    }

    DexValue result = ExecuteMethod(resolved->dex, resolved->method, arg_regs, env);
    if (jthrowable pending = JniEnvironment::Instance().GetPendingException()) {
        out_exception = pending;
        JniEnvironment::Instance().ClearPendingException();
    }
    return result;
}

DexValue DexVm::ExecuteMethod(const DexFile *dex, const DexMethodDef *method,
                              std::span<const uint64_t> raw_args, JNIEnv *env)
{
    if (!method) {
        return {0, 'V'};
    }
    if ((method->access_flags & ACC_NATIVE) != 0) {
        return InvokeNativeMethod(*method, raw_args, env);
    }
    if (!dex || method->insns.empty()) {
        return {0, method->return_type.empty() ? 'V' : method->return_type.front()};
    }

    std::vector<uint64_t> regs(method->registers_size, 0);
    if (method->registers_size >= method->ins_size) {
        size_t first_in = method->registers_size - method->ins_size;
        size_t copy_n = std::min(raw_args.size(), static_cast<size_t>(method->ins_size));
        for (size_t i = 0; i < copy_n; ++i) {
            regs[first_in + i] = raw_args[i];
        }
    }

    const auto &insns = method->insns;
    const size_t code_len = insns.size();
    uint64_t last_result = 0;
    jobject last_exception = nullptr;
    size_t pc = 0;

    auto dispatch_exception = [&](jobject ex_obj, size_t fault_pc) -> bool {
        if (!ex_obj) {
            return false;
        }
        for (const auto &t : method->tries) {
            if (fault_pc >= t.start_addr && fault_pc < t.end_addr) {
                for (const auto &h : t.handlers) {
                    if (IsInstanceOf(ex_obj, h.exception_descriptor)) {
                        last_exception = ex_obj;
                        pc = h.addr;
                        return true;
                    }
                }
                if (t.catch_all_addr != DEX_NO_INDEX) {
                    last_exception = ex_obj;
                    pc = t.catch_all_addr;
                    return true;
                }
            }
        }
        JniEnvironment::Instance().SetPendingException(ex_obj);
        return false;
    };

    char ret_kind = method->return_type.empty() ? 'V' : method->return_type.front();

    struct ScopedMethodMonitor {
        jobject obj{nullptr};
        explicit ScopedMethodMonitor(jobject target) : obj(target)
        {
            if (obj) {
                JavaRuntime::Instance().MonitorEnter(obj);
            }
        }
        ~ScopedMethodMonitor()
        {
            if (obj) {
                JavaRuntime::Instance().MonitorExit(obj);
            }
        }
    };

    jobject sync_target = nullptr;
    if ((method->access_flags & (ACC_SYNCHRONIZED | ACC_DECLARED_SYNCHRONIZED)) != 0) {
        if ((method->access_flags & ACC_STATIC) != 0) {
            std::string slash = DexFile::DescriptorToSlashName(method->class_descriptor);
            sync_target =
                reinterpret_cast<jobject>(JniEnvironment::Instance().InternClass(slash.c_str()));
        } else if (method->registers_size >= method->ins_size && !raw_args.empty()) {
            sync_target =
                reinterpret_cast<jobject>(regs[method->registers_size - method->ins_size]);
        }
    }
    ScopedMethodMonitor method_monitor(sync_target);

    while (pc < code_len) {
        uint16_t insn = insns[pc];
        uint8_t op = static_cast<uint8_t>(insn & 0xFFu);

        switch (op) {
        case OP_NOP:
            pc += 1;
            break;

        case OP_MOVE:
        case OP_MOVE_OBJECT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = regs[b];
            pc += 1;
            break;
        }
        case OP_MOVE_FROM16:
        case OP_MOVE_OBJECT_FROM16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t bbbb = insns[pc + 1];
            regs[aa] = regs[bbbb];
            pc += 2;
            break;
        }
        case OP_MOVE_16:
        case OP_MOVE_OBJECT_16: {
            uint16_t aaaa = insns[pc + 1];
            uint16_t bbbb = insns[pc + 2];
            regs[aaaa] = regs[bbbb];
            pc += 3;
            break;
        }
        case OP_MOVE_WIDE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a, ReadWide(regs, b));
            pc += 1;
            break;
        }
        case OP_MOVE_WIDE_FROM16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t bbbb = insns[pc + 1];
            WriteWide(regs, aa, ReadWide(regs, bbbb));
            pc += 2;
            break;
        }
        case OP_MOVE_WIDE_16: {
            uint16_t aaaa = insns[pc + 1];
            uint16_t bbbb = insns[pc + 2];
            WriteWide(regs, aaaa, ReadWide(regs, bbbb));
            pc += 3;
            break;
        }

        case OP_MOVE_RESULT:
        case OP_MOVE_RESULT_OBJECT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            regs[aa] = last_result;
            pc += 1;
            break;
        }
        case OP_MOVE_RESULT_WIDE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            WriteWide(regs, aa, last_result);
            pc += 1;
            break;
        }
        case OP_MOVE_EXCEPTION: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            regs[aa] = reinterpret_cast<uintptr_t>(last_exception);
            last_exception = nullptr;
            pc += 1;
            break;
        }

        case OP_RETURN_VOID:
            return {0, 'V'};
        case OP_RETURN:
        case OP_RETURN_OBJECT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            return {regs[aa], ret_kind};
        }
        case OP_RETURN_WIDE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            return {ReadWide(regs, aa), ret_kind};
        }

        case OP_CONST_4: {
            uint8_t a = (insn >> 8) & 0xFu;
            int32_t val = SignExtend4((insn >> 12) & 0xFu);
            regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(val));
            pc += 1;
            break;
        }
        case OP_CONST_16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int32_t val = static_cast<int16_t>(insns[pc + 1]);
            regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(val));
            pc += 2;
            break;
        }
        case OP_CONST: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint32_t val = insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16);
            regs[aa] = val;
            pc += 3;
            break;
        }
        case OP_CONST_HIGH16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint32_t val = static_cast<uint32_t>(insns[pc + 1]) << 16;
            regs[aa] = val;
            pc += 2;
            break;
        }
        case OP_CONST_WIDE_16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int64_t val = static_cast<int16_t>(insns[pc + 1]);
            WriteWide(regs, aa, static_cast<uint64_t>(val));
            pc += 2;
            break;
        }
        case OP_CONST_WIDE_32: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int64_t val =
                static_cast<int32_t>(insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16));
            WriteWide(regs, aa, static_cast<uint64_t>(val));
            pc += 3;
            break;
        }
        case OP_CONST_WIDE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint64_t val = static_cast<uint64_t>(insns[pc + 1]) |
                           (static_cast<uint64_t>(insns[pc + 2]) << 16) |
                           (static_cast<uint64_t>(insns[pc + 3]) << 32) |
                           (static_cast<uint64_t>(insns[pc + 4]) << 48);
            WriteWide(regs, aa, val);
            pc += 5;
            break;
        }
        case OP_CONST_WIDE_HIGH16: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint64_t val = static_cast<uint64_t>(insns[pc + 1]) << 48;
            WriteWide(regs, aa, val);
            pc += 2;
            break;
        }
        case OP_CONST_STRING: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t idx = insns[pc + 1];
            jstring s = JniEnvironment::Instance().CreateString(dex->GetString(idx));
            regs[aa] = reinterpret_cast<uintptr_t>(s);
            pc += 2;
            break;
        }
        case OP_CONST_STRING_JUMBO: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint32_t idx = insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16);
            jstring s = JniEnvironment::Instance().CreateString(dex->GetString(idx));
            regs[aa] = reinterpret_cast<uintptr_t>(s);
            pc += 3;
            break;
        }
        case OP_CONST_CLASS: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t idx = insns[pc + 1];
            std::string cls_slash = DexFile::DescriptorToSlashName(dex->GetType(idx));
            jobject cls_obj = JavaRuntime::Instance().GetClassObject(cls_slash);
            regs[aa] = reinterpret_cast<uintptr_t>(cls_obj);
            pc += 2;
            break;
        }
        case OP_CONST_METHOD_HANDLE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t idx = insns[pc + 1];
            jobject mh_obj = nullptr;
            if (const auto *mh = dex->GetMethodHandle(idx)) {
                mh_obj = JavaRuntime::Instance().CreateMethodHandleObject(dex, *mh);
            }
            regs[aa] = reinterpret_cast<uintptr_t>(mh_obj);
            pc += 2;
            break;
        }
        case OP_CONST_METHOD_TYPE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t idx = insns[pc + 1];
            jobject mt_obj = nullptr;
            if (const auto *proto = dex->GetProtoRef(idx)) {
                mt_obj = JavaRuntime::Instance().CreateMethodTypeObject(*proto);
            }
            regs[aa] = reinterpret_cast<uintptr_t>(mt_obj);
            pc += 2;
            break;
        }

        case OP_MONITOR_ENTER: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            jobject obj = reinterpret_cast<jobject>(regs[aa]);
            if (!obj) {
                if (!dispatch_exception(
                        JavaRuntime::Instance().CreateException("java/lang/NullPointerException",
                                                                "Attempt to lock null object"),
                        pc)) {
                    return {0, ret_kind};
                }
                break;
            }
            JavaRuntime::Instance().MonitorEnter(obj);
            pc += 1;
            break;
        }
        case OP_MONITOR_EXIT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            jobject obj = reinterpret_cast<jobject>(regs[aa]);
            JavaRuntime::Instance().MonitorExit(obj);
            pc += 1;
            break;
        }

        case OP_CHECK_CAST: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t tidx = insns[pc + 1];
            jobject obj = reinterpret_cast<jobject>(regs[aa]);
            if (obj && !IsInstanceOf(obj, dex->GetType(tidx))) {
                auto ex = std::make_shared<JniSyntheticObject>();
                ex->class_name = "java/lang/ClassCastException";
                ex->string_fields["detailMessage"] = "ClassCastException";
                jobject ex_obj = JniEnvironment::Instance().RegisterObject(ex);
                if (!dispatch_exception(ex_obj, pc)) {
                    return {0, ret_kind};
                }
                break;
            }
            pc += 2;
            break;
        }
        case OP_INSTANCE_OF: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            uint16_t tidx = insns[pc + 1];
            jobject obj = reinterpret_cast<jobject>(regs[b]);
            regs[a] = IsInstanceOf(obj, dex->GetType(tidx)) ? 1 : 0;
            pc += 2;
            break;
        }

        case OP_ARRAY_LENGTH: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(regs[b]));
            size_t len = 0;
            if (syn) {
                switch (syn->type) {
                case JniObjectType::BooleanArray:
                    len = syn->bool_data.size();
                    break;
                case JniObjectType::ByteArray:
                    len = syn->byte_data.size();
                    break;
                case JniObjectType::CharArray:
                    len = syn->char_data.size();
                    break;
                case JniObjectType::ShortArray:
                    len = syn->short_data.size();
                    break;
                case JniObjectType::IntArray:
                    len = syn->int_data.size();
                    break;
                case JniObjectType::LongArray:
                    len = syn->long_data.size();
                    break;
                case JniObjectType::FloatArray:
                    len = syn->float_data.size();
                    break;
                case JniObjectType::DoubleArray:
                    len = syn->double_data.size();
                    break;
                case JniObjectType::ObjectArray:
                    len = syn->object_data.size();
                    break;
                default:
                    break;
                }
            }
            regs[a] = static_cast<uint64_t>(len);
            pc += 1;
            break;
        }

        case OP_NEW_INSTANCE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t tidx = insns[pc + 1];
            std::string_view type_desc = dex->GetType(tidx);
            EnsureClassInitialized(type_desc, env);
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->type = JniObjectType::Generic;
            syn->class_name = DexFile::DescriptorToSlashName(type_desc);
            regs[aa] = reinterpret_cast<uintptr_t>(JniEnvironment::Instance().RegisterObject(syn));
            pc += 2;
            break;
        }

        case OP_NEW_ARRAY: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            uint16_t tidx = insns[pc + 1];
            int32_t len = static_cast<int32_t>(regs[b]);
            regs[a] = reinterpret_cast<uintptr_t>(
                AllocateArray(dex->GetType(tidx), len > 0 ? static_cast<size_t>(len) : 0));
            pc += 2;
            break;
        }

        case OP_FILLED_NEW_ARRAY:
        case OP_FILLED_NEW_ARRAY_RANGE: {
            uint16_t tidx = insns[pc + 1];
            std::string_view desc = dex->GetType(tidx);
            std::vector<uint32_t> arg_idx;
            if (op == OP_FILLED_NEW_ARRAY) {
                uint8_t count = (insn >> 12) & 0xFu;
                uint16_t word2 = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    uint8_t r = (i < 4) ? ((word2 >> (i * 4)) & 0xFu) : ((insn >> 8) & 0xFu);
                    arg_idx.push_back(r);
                }
            } else {
                uint8_t count = (insn >> 8) & 0xFFu;
                uint16_t start = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    arg_idx.push_back(start + i);
                }
            }
            jobject arr = AllocateArray(desc, arg_idx.size());
            auto syn = JniEnvironment::Instance().LookupObject(arr);
            if (syn) {
                for (size_t i = 0; i < arg_idx.size(); ++i) {
                    uint64_t val = regs[arg_idx[i]];
                    if (syn->type == JniObjectType::IntArray) {
                        syn->int_data[i] = static_cast<int32_t>(val);
                    } else if (syn->type == JniObjectType::ObjectArray) {
                        syn->object_data[i] = reinterpret_cast<jobject>(val);
                    }
                }
            }
            last_result = reinterpret_cast<uintptr_t>(arr);
            pc += 3;
            break;
        }

        case OP_FILL_ARRAY_DATA: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int32_t off =
                static_cast<int32_t>(insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16));
            size_t tbl_pc = pc + static_cast<ptrdiff_t>(off);
            auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(regs[aa]));
            if (syn && tbl_pc + 4 <= code_len && insns[tbl_pc] == 0x0300u) {
                uint16_t elem_w = insns[tbl_pc + 1];
                uint32_t count =
                    insns[tbl_pc + 2] | (static_cast<uint32_t>(insns[tbl_pc + 3]) << 16);
                const uint8_t *raw = reinterpret_cast<const uint8_t *>(&insns[tbl_pc + 4]);
                for (uint32_t i = 0; i < count; ++i) {
                    if (elem_w == 4 && i < syn->int_data.size()) {
                        std::memcpy(&syn->int_data[i], raw + i * 4, 4);
                    } else if (elem_w == 8 && i < syn->long_data.size()) {
                        std::memcpy(&syn->long_data[i], raw + i * 8, 8);
                    } else if (elem_w == 1 && i < syn->byte_data.size()) {
                        syn->byte_data[i] = static_cast<int8_t>(raw[i]);
                    }
                }
            }
            pc += 3;
            break;
        }

        case OP_THROW: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            jobject ex_obj = reinterpret_cast<jobject>(regs[aa]);
            if (!ex_obj) {
                auto npe = std::make_shared<JniSyntheticObject>();
                npe->class_name = "java/lang/NullPointerException";
                ex_obj = JniEnvironment::Instance().RegisterObject(npe);
            }
            if (!dispatch_exception(ex_obj, pc)) {
                return {0, ret_kind};
            }
            break;
        }

        case OP_GOTO: {
            int8_t off = static_cast<int8_t>((insn >> 8) & 0xFFu);
            pc = static_cast<size_t>(static_cast<ptrdiff_t>(pc) + off);
            break;
        }
        case OP_GOTO_16: {
            int16_t off = static_cast<int16_t>(insns[pc + 1]);
            pc = static_cast<size_t>(static_cast<ptrdiff_t>(pc) + off);
            break;
        }
        case OP_GOTO_32: {
            int32_t off =
                static_cast<int32_t>(insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16));
            pc = static_cast<size_t>(static_cast<ptrdiff_t>(pc) + off);
            break;
        }

        case OP_PACKED_SWITCH: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int32_t off =
                static_cast<int32_t>(insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16));
            size_t tbl = pc + static_cast<ptrdiff_t>(off);
            int32_t key = static_cast<int32_t>(regs[aa]);
            bool jumped = false;
            if (tbl + 4 <= code_len && insns[tbl] == 0x0100u) {
                uint16_t sz = insns[tbl + 1];
                int32_t first_key = static_cast<int32_t>(
                    insns[tbl + 2] | (static_cast<uint32_t>(insns[tbl + 3]) << 16));
                int32_t diff = key - first_key;
                if (diff >= 0 && static_cast<uint32_t>(diff) < sz) {
                    size_t entry = tbl + 4 + static_cast<size_t>(diff) * 2;
                    if (entry + 2 <= code_len) {
                        int32_t target = static_cast<int32_t>(
                            insns[entry] | (static_cast<uint32_t>(insns[entry + 1]) << 16));
                        pc = static_cast<size_t>(static_cast<ptrdiff_t>(pc) + target);
                        jumped = true;
                    }
                }
            }
            if (!jumped) {
                pc += 3;
            }
            break;
        }

        case OP_SPARSE_SWITCH: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int32_t off =
                static_cast<int32_t>(insns[pc + 1] | (static_cast<uint32_t>(insns[pc + 2]) << 16));
            size_t tbl = pc + static_cast<ptrdiff_t>(off);
            int32_t key = static_cast<int32_t>(regs[aa]);
            bool jumped = false;
            if (tbl + 2 <= code_len && insns[tbl] == 0x0200u) {
                uint16_t sz = insns[tbl + 1];
                for (uint16_t i = 0; i < sz; ++i) {
                    size_t k_off = tbl + 2 + i * 2;
                    size_t t_off = tbl + 2 + sz * 2 + i * 2;
                    if (t_off + 2 <= code_len) {
                        int32_t k = static_cast<int32_t>(
                            insns[k_off] | (static_cast<uint32_t>(insns[k_off + 1]) << 16));
                        if (k == key) {
                            int32_t target = static_cast<int32_t>(
                                insns[t_off] | (static_cast<uint32_t>(insns[t_off + 1]) << 16));
                            pc = static_cast<size_t>(static_cast<ptrdiff_t>(pc) + target);
                            jumped = true;
                            break;
                        }
                    }
                }
            }
            if (!jumped) {
                pc += 3;
            }
            break;
        }

        case OP_CMPL_FLOAT:
        case OP_CMPG_FLOAT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            float b = BitsToFloat(regs[insns[pc + 1] & 0xFFu]);
            float c = BitsToFloat(regs[(insns[pc + 1] >> 8) & 0xFFu]);
            int32_t res = (std::isnan(b) || std::isnan(c)) ? (op == OP_CMPG_FLOAT ? 1 : -1)
                          : (b < c)                        ? -1
                          : (b > c)                        ? 1
                                                           : 0;
            regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(res));
            pc += 2;
            break;
        }
        case OP_CMPL_DOUBLE:
        case OP_CMPG_DOUBLE: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            double b = BitsToDouble(ReadWide(regs, insns[pc + 1] & 0xFFu));
            double c = BitsToDouble(ReadWide(regs, (insns[pc + 1] >> 8) & 0xFFu));
            int32_t res = (std::isnan(b) || std::isnan(c)) ? (op == OP_CMPG_DOUBLE ? 1 : -1)
                          : (b < c)                        ? -1
                          : (b > c)                        ? 1
                                                           : 0;
            regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(res));
            pc += 2;
            break;
        }
        case OP_CMP_LONG: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int64_t b = static_cast<int64_t>(ReadWide(regs, insns[pc + 1] & 0xFFu));
            int64_t c = static_cast<int64_t>(ReadWide(regs, (insns[pc + 1] >> 8) & 0xFFu));
            int32_t res = (b < c) ? -1 : (b > c) ? 1 : 0;
            regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(res));
            pc += 2;
            break;
        }

        case OP_IF_EQ:
        case OP_IF_NE:
        case OP_IF_LT:
        case OP_IF_GE:
        case OP_IF_GT:
        case OP_IF_LE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            int16_t off = static_cast<int16_t>(insns[pc + 1]);
            int32_t va = static_cast<int32_t>(regs[a]);
            int32_t vb = static_cast<int32_t>(regs[b]);
            bool take = false;
            if (op == OP_IF_EQ) {
                take = (regs[a] == regs[b]);
            } else if (op == OP_IF_NE) {
                take = (regs[a] != regs[b]);
            } else if (op == OP_IF_LT) {
                take = (va < vb);
            } else if (op == OP_IF_GE) {
                take = (va >= vb);
            } else if (op == OP_IF_GT) {
                take = (va > vb);
            } else {
                take = (va <= vb);
            }
            pc = take ? static_cast<size_t>(static_cast<ptrdiff_t>(pc) + off) : (pc + 2);
            break;
        }

        case OP_IF_EQZ:
        case OP_IF_NEZ:
        case OP_IF_LTZ:
        case OP_IF_GEZ:
        case OP_IF_GTZ:
        case OP_IF_LEZ: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            int16_t off = static_cast<int16_t>(insns[pc + 1]);
            int32_t va = static_cast<int32_t>(regs[aa]);
            bool take = false;
            if (op == OP_IF_EQZ) {
                take = (regs[aa] == 0);
            } else if (op == OP_IF_NEZ) {
                take = (regs[aa] != 0);
            } else if (op == OP_IF_LTZ) {
                take = (va < 0);
            } else if (op == OP_IF_GEZ) {
                take = (va >= 0);
            } else if (op == OP_IF_GTZ) {
                take = (va > 0);
            } else {
                take = (va <= 0);
            }
            pc = take ? static_cast<size_t>(static_cast<ptrdiff_t>(pc) + off) : (pc + 2);
            break;
        }

        case OP_AGET:
        case OP_AGET_WIDE:
        case OP_AGET_OBJECT:
        case OP_AGET_BOOLEAN:
        case OP_AGET_BYTE:
        case OP_AGET_CHAR:
        case OP_AGET_SHORT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint8_t bb = insns[pc + 1] & 0xFFu;
            uint8_t cc = (insns[pc + 1] >> 8) & 0xFFu;
            auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(regs[bb]));
            size_t idx = static_cast<size_t>(static_cast<int32_t>(regs[cc]));
            if (syn) {
                if (op == OP_AGET_WIDE) {
                    uint64_t val = 0;
                    if (syn->type == JniObjectType::LongArray && idx < syn->long_data.size()) {
                        val = static_cast<uint64_t>(syn->long_data[idx]);
                    } else if (syn->type == JniObjectType::DoubleArray &&
                               idx < syn->double_data.size()) {
                        val = DoubleToBits(syn->double_data[idx]);
                    }
                    WriteWide(regs, aa, val);
                } else if (op == OP_AGET_OBJECT && idx < syn->object_data.size()) {
                    regs[aa] = reinterpret_cast<uintptr_t>(syn->object_data[idx]);
                } else if (op == OP_AGET_BOOLEAN && idx < syn->bool_data.size()) {
                    regs[aa] = syn->bool_data[idx];
                } else if (op == OP_AGET_BYTE && idx < syn->byte_data.size()) {
                    regs[aa] = static_cast<uint64_t>(static_cast<int32_t>(syn->byte_data[idx]));
                } else if (op == OP_AGET_CHAR && idx < syn->char_data.size()) {
                    regs[aa] = syn->char_data[idx];
                } else if (op == OP_AGET_SHORT && idx < syn->short_data.size()) {
                    regs[aa] = static_cast<uint64_t>(static_cast<int32_t>(syn->short_data[idx]));
                } else if (syn->type == JniObjectType::IntArray && idx < syn->int_data.size()) {
                    regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(syn->int_data[idx]));
                } else if (syn->type == JniObjectType::FloatArray && idx < syn->float_data.size()) {
                    regs[aa] = FloatToBits(syn->float_data[idx]);
                }
            }
            pc += 2;
            break;
        }

        case OP_APUT:
        case OP_APUT_WIDE:
        case OP_APUT_OBJECT:
        case OP_APUT_BOOLEAN:
        case OP_APUT_BYTE:
        case OP_APUT_CHAR:
        case OP_APUT_SHORT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint8_t bb = insns[pc + 1] & 0xFFu;
            uint8_t cc = (insns[pc + 1] >> 8) & 0xFFu;
            auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(regs[bb]));
            size_t idx = static_cast<size_t>(static_cast<int32_t>(regs[cc]));
            if (syn) {
                if (op == OP_APUT_WIDE) {
                    uint64_t val = ReadWide(regs, aa);
                    if (syn->type == JniObjectType::LongArray && idx < syn->long_data.size()) {
                        syn->long_data[idx] = static_cast<int64_t>(val);
                    } else if (syn->type == JniObjectType::DoubleArray &&
                               idx < syn->double_data.size()) {
                        syn->double_data[idx] = BitsToDouble(val);
                    }
                } else if (op == OP_APUT_OBJECT && idx < syn->object_data.size()) {
                    syn->object_data[idx] = reinterpret_cast<jobject>(regs[aa]);
                } else if (op == OP_APUT_BOOLEAN && idx < syn->bool_data.size()) {
                    syn->bool_data[idx] = static_cast<uint8_t>(regs[aa] != 0 ? 1 : 0);
                } else if (op == OP_APUT_BYTE && idx < syn->byte_data.size()) {
                    syn->byte_data[idx] = static_cast<int8_t>(regs[aa]);
                } else if (op == OP_APUT_CHAR && idx < syn->char_data.size()) {
                    syn->char_data[idx] = static_cast<uint16_t>(regs[aa]);
                } else if (op == OP_APUT_SHORT && idx < syn->short_data.size()) {
                    syn->short_data[idx] = static_cast<int16_t>(regs[aa]);
                } else if (syn->type == JniObjectType::IntArray && idx < syn->int_data.size()) {
                    syn->int_data[idx] = static_cast<int32_t>(regs[aa]);
                } else if (syn->type == JniObjectType::FloatArray && idx < syn->float_data.size()) {
                    syn->float_data[idx] = BitsToFloat(regs[aa]);
                }
            }
            pc += 2;
            break;
        }

        case OP_IGET:
        case OP_IGET_WIDE:
        case OP_IGET_OBJECT:
        case OP_IGET_BOOLEAN:
        case OP_IGET_BYTE:
        case OP_IGET_CHAR:
        case OP_IGET_SHORT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            uint16_t fidx = insns[pc + 1];
            if (const auto *f = dex->GetFieldRef(fidx)) {
                uint64_t val = 0;
                if (GetInstanceField(reinterpret_cast<jobject>(regs[b]), f->name,
                                     f->type_descriptor, val)) {
                    if (op == OP_IGET_WIDE) {
                        WriteWide(regs, a, val);
                    } else {
                        regs[a] = val;
                    }
                }
            }
            pc += 2;
            break;
        }

        case OP_IPUT:
        case OP_IPUT_WIDE:
        case OP_IPUT_OBJECT:
        case OP_IPUT_BOOLEAN:
        case OP_IPUT_BYTE:
        case OP_IPUT_CHAR:
        case OP_IPUT_SHORT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            uint16_t fidx = insns[pc + 1];
            if (const auto *f = dex->GetFieldRef(fidx)) {
                uint64_t val = (op == OP_IPUT_WIDE) ? ReadWide(regs, a) : regs[a];
                SetInstanceField(reinterpret_cast<jobject>(regs[b]), f->name, f->type_descriptor,
                                 val);
            }
            pc += 2;
            break;
        }

        case OP_SGET:
        case OP_SGET_WIDE:
        case OP_SGET_OBJECT:
        case OP_SGET_BOOLEAN:
        case OP_SGET_BYTE:
        case OP_SGET_CHAR:
        case OP_SGET_SHORT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t fidx = insns[pc + 1];
            if (const auto *f = dex->GetFieldRef(fidx)) {
                uint64_t val = GetStaticField(f->class_descriptor, f->name);
                if (op == OP_SGET_WIDE) {
                    WriteWide(regs, aa, val);
                } else {
                    regs[aa] = val;
                }
            }
            pc += 2;
            break;
        }

        case OP_SPUT:
        case OP_SPUT_WIDE:
        case OP_SPUT_OBJECT:
        case OP_SPUT_BOOLEAN:
        case OP_SPUT_BYTE:
        case OP_SPUT_CHAR:
        case OP_SPUT_SHORT: {
            uint8_t aa = (insn >> 8) & 0xFFu;
            uint16_t fidx = insns[pc + 1];
            if (const auto *f = dex->GetFieldRef(fidx)) {
                uint64_t val = (op == OP_SPUT_WIDE) ? ReadWide(regs, aa) : regs[aa];
                SetStaticField(f->class_descriptor, f->name, val);
            }
            pc += 2;
            break;
        }

        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_SUPER:
        case OP_INVOKE_DIRECT:
        case OP_INVOKE_STATIC:
        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_SUPER_RANGE:
        case OP_INVOKE_DIRECT_RANGE:
        case OP_INVOKE_STATIC_RANGE:
        case OP_INVOKE_INTERFACE_RANGE: {
            bool is_range = op >= OP_INVOKE_VIRTUAL_RANGE;
            bool is_static = (op == OP_INVOKE_STATIC || op == OP_INVOKE_STATIC_RANGE);
            bool is_super = (op == OP_INVOKE_SUPER || op == OP_INVOKE_SUPER_RANGE);
            bool is_direct = (op == OP_INVOKE_DIRECT || op == OP_INVOKE_DIRECT_RANGE);
            uint16_t midx = insns[pc + 1];
            std::vector<uint32_t> arg_indices;
            if (!is_range) {
                uint8_t count = (insn >> 12) & 0xFu;
                uint16_t word2 = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    uint8_t r = (i < 4) ? ((word2 >> (i * 4)) & 0xFu) : ((insn >> 8) & 0xFu);
                    arg_indices.push_back(r);
                }
            } else {
                uint8_t count = (insn >> 8) & 0xFFu;
                uint16_t start = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    arg_indices.push_back(start + i);
                }
            }
            if (const auto *mref = dex->GetMethodRef(midx)) {
                auto arg_regs = CollectRegisterArgs(*mref, regs, is_static, arg_indices);
                jobject ex = nullptr;
                DexValue res = InvokeTarget(*mref, arg_regs, is_static, is_super, is_direct,
                                            method->class_descriptor, env, ex);
                last_result = res.raw;
                if (ex) {
                    if (!dispatch_exception(ex, pc)) {
                        return {0, ret_kind};
                    }
                    break;
                }
            }
            pc += 3;
            break;
        }

        case OP_INVOKE_CUSTOM:
        case OP_INVOKE_CUSTOM_RANGE: {
            bool is_range = (op == OP_INVOKE_CUSTOM_RANGE);
            uint16_t cs_idx = insns[pc + 1];
            std::vector<uint32_t> arg_indices;
            if (!is_range) {
                uint8_t count = (insn >> 12) & 0xFu;
                uint16_t word2 = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    uint8_t r = (i < 4) ? ((word2 >> (i * 4)) & 0xFu) : ((insn >> 8) & 0xFu);
                    arg_indices.push_back(r);
                }
            } else {
                uint8_t count = (insn >> 8) & 0xFFu;
                uint16_t start = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    arg_indices.push_back(start + i);
                }
            }
            std::vector<uint64_t> arg_regs;
            if (const auto *cs = dex->GetCallSite(cs_idx)) {
                if (const auto *proto = dex->GetProtoRef(cs->method_type_proto_idx)) {
                    DexMethodRef synth_ref{};
                    synth_ref.param_types = proto->param_types;
                    synth_ref.return_type = proto->return_type;
                    arg_regs = CollectRegisterArgs(synth_ref, regs, true, arg_indices);
                }
            }
            if (arg_regs.empty() && !arg_indices.empty()) {
                for (uint32_t r : arg_indices) {
                    arg_regs.push_back(r < regs.size() ? regs[r] : 0);
                }
            }
            jobject ex = nullptr;
            DexValue res =
                JavaRuntime::Instance().InvokeCustomCallSite(*this, dex, cs_idx, arg_regs, env, ex);
            last_result = res.raw;
            if (ex) {
                if (!dispatch_exception(ex, pc)) {
                    return {0, ret_kind};
                }
                break;
            }
            pc += 3;
            break;
        }

        case OP_INVOKE_POLYMORPHIC:
        case OP_INVOKE_POLYMORPHIC_RANGE: {
            bool is_range = (op == OP_INVOKE_POLYMORPHIC_RANGE);
            uint16_t midx = insns[pc + 1];
            uint16_t proto_idx = insns[pc + 3];
            std::vector<uint32_t> arg_indices;
            if (!is_range) {
                uint8_t count = (insn >> 12) & 0xFu;
                uint16_t word2 = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    uint8_t r = (i < 4) ? ((word2 >> (i * 4)) & 0xFu) : ((insn >> 8) & 0xFu);
                    arg_indices.push_back(r);
                }
            } else {
                uint8_t count = (insn >> 8) & 0xFFu;
                uint16_t start = insns[pc + 2];
                for (uint8_t i = 0; i < count; ++i) {
                    arg_indices.push_back(start + i);
                }
            }
            const auto *proto = dex->GetProtoRef(proto_idx);
            const auto *mref = dex->GetMethodRef(midx);
            std::vector<uint64_t> arg_regs;
            if (proto) {
                DexMethodRef synth_ref{};
                synth_ref.param_types = proto->param_types;
                synth_ref.return_type = proto->return_type;
                arg_regs = CollectRegisterArgs(synth_ref, regs, false, arg_indices);
            } else if (mref) {
                arg_regs = CollectRegisterArgs(*mref, regs, false, arg_indices);
            }
            jobject ex = nullptr;
            DexValue res{0, 'V'};
            if (!arg_regs.empty() && proto) {
                res = JavaRuntime::Instance().InvokePolymorphic(
                    *this, reinterpret_cast<jobject>(arg_regs[0]), *proto,
                    std::span<const uint64_t>(arg_regs).subspan(1), env, ex);
            }
            last_result = res.raw;
            if (ex) {
                if (!dispatch_exception(ex, pc)) {
                    return {0, ret_kind};
                }
                break;
            }
            pc += 4;
            break;
        }

        case OP_NEG_INT:
        case OP_NOT_INT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            int32_t v = static_cast<int32_t>(regs[b]);
            regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(op == OP_NEG_INT ? -v : ~v));
            pc += 1;
            break;
        }
        case OP_NEG_LONG:
        case OP_NOT_LONG: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            int64_t v = static_cast<int64_t>(ReadWide(regs, b));
            WriteWide(regs, a, static_cast<uint64_t>(op == OP_NEG_LONG ? -v : ~v));
            pc += 1;
            break;
        }
        case OP_NEG_FLOAT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = FloatToBits(-BitsToFloat(regs[b]));
            pc += 1;
            break;
        }
        case OP_NEG_DOUBLE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a, DoubleToBits(-BitsToDouble(ReadWide(regs, b))));
            pc += 1;
            break;
        }

        case OP_INT_TO_LONG: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a,
                      static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(regs[b]))));
            pc += 1;
            break;
        }
        case OP_INT_TO_FLOAT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = FloatToBits(static_cast<float>(static_cast<int32_t>(regs[b])));
            pc += 1;
            break;
        }
        case OP_INT_TO_DOUBLE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a, DoubleToBits(static_cast<double>(static_cast<int32_t>(regs[b]))));
            pc += 1;
            break;
        }
        case OP_LONG_TO_INT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(ReadWide(regs, b)));
            pc += 1;
            break;
        }
        case OP_LONG_TO_FLOAT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = FloatToBits(static_cast<float>(static_cast<int64_t>(ReadWide(regs, b))));
            pc += 1;
            break;
        }
        case OP_LONG_TO_DOUBLE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a,
                      DoubleToBits(static_cast<double>(static_cast<int64_t>(ReadWide(regs, b)))));
            pc += 1;
            break;
        }
        case OP_FLOAT_TO_INT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint64_t>(
                static_cast<uint32_t>(static_cast<int32_t>(BitsToFloat(regs[b]))));
            pc += 1;
            break;
        }
        case OP_FLOAT_TO_LONG: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a, static_cast<uint64_t>(static_cast<int64_t>(BitsToFloat(regs[b]))));
            pc += 1;
            break;
        }
        case OP_FLOAT_TO_DOUBLE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a, DoubleToBits(static_cast<double>(BitsToFloat(regs[b]))));
            pc += 1;
            break;
        }
        case OP_DOUBLE_TO_INT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint64_t>(
                static_cast<uint32_t>(static_cast<int32_t>(BitsToDouble(ReadWide(regs, b)))));
            pc += 1;
            break;
        }
        case OP_DOUBLE_TO_LONG: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            WriteWide(regs, a,
                      static_cast<uint64_t>(static_cast<int64_t>(BitsToDouble(ReadWide(regs, b)))));
            pc += 1;
            break;
        }
        case OP_DOUBLE_TO_FLOAT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = FloatToBits(static_cast<float>(BitsToDouble(ReadWide(regs, b))));
            pc += 1;
            break;
        }
        case OP_INT_TO_BYTE: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int8_t>(regs[b])));
            pc += 1;
            break;
        }
        case OP_INT_TO_CHAR: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint16_t>(regs[b]);
            pc += 1;
            break;
        }
        case OP_INT_TO_SHORT: {
            uint8_t a = (insn >> 8) & 0xFu;
            uint8_t b = (insn >> 12) & 0xFu;
            regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int16_t>(regs[b])));
            pc += 1;
            break;
        }

        default: {
            if (op >= OP_ADD_INT && op <= OP_USHR_INT) {
                uint8_t aa = (insn >> 8) & 0xFFu;
                uint8_t bb = insns[pc + 1] & 0xFFu;
                uint8_t cc = (insns[pc + 1] >> 8) & 0xFFu;
                int32_t vb = static_cast<int32_t>(regs[bb]);
                int32_t vc = static_cast<int32_t>(regs[cc]);
                uint32_t uvb = static_cast<uint32_t>(regs[bb]);
                uint32_t uvc = static_cast<uint32_t>(regs[cc]) & 0x1Fu;
                int32_t res = 0;
                switch (op) {
                case OP_ADD_INT:
                    res = vb + vc;
                    break;
                case OP_SUB_INT:
                    res = vb - vc;
                    break;
                case OP_MUL_INT:
                    res = vb * vc;
                    break;
                case OP_DIV_INT:
                    res = (vc != 0) ? (vb / vc) : 0;
                    break;
                case OP_REM_INT:
                    res = (vc != 0) ? (vb % vc) : 0;
                    break;
                case OP_AND_INT:
                    res = vb & vc;
                    break;
                case OP_OR_INT:
                    res = vb | vc;
                    break;
                case OP_XOR_INT:
                    res = vb ^ vc;
                    break;
                case OP_SHL_INT:
                    res = static_cast<int32_t>(uvb << uvc);
                    break;
                case OP_SHR_INT:
                    res = vb >> uvc;
                    break;
                case OP_USHR_INT:
                    res = static_cast<int32_t>(uvb >> uvc);
                    break;
                default:
                    break;
                }
                regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(res));
                pc += 2;
            } else if (op >= OP_ADD_LONG && op <= OP_USHR_LONG) {
                uint8_t aa = (insn >> 8) & 0xFFu;
                uint8_t bb = insns[pc + 1] & 0xFFu;
                uint8_t cc = (insns[pc + 1] >> 8) & 0xFFu;
                int64_t vb = static_cast<int64_t>(ReadWide(regs, bb));
                int64_t vc = static_cast<int64_t>(ReadWide(regs, cc));
                uint64_t uvb = static_cast<uint64_t>(vb);
                uint32_t uvc = static_cast<uint32_t>(regs[cc]) & 0x3Fu;
                int64_t res = 0;
                switch (op) {
                case OP_ADD_LONG:
                    res = vb + vc;
                    break;
                case OP_SUB_LONG:
                    res = vb - vc;
                    break;
                case OP_MUL_LONG:
                    res = vb * vc;
                    break;
                case OP_DIV_LONG:
                    res = (vc != 0) ? (vb / vc) : 0;
                    break;
                case OP_REM_LONG:
                    res = (vc != 0) ? (vb % vc) : 0;
                    break;
                case OP_AND_LONG:
                    res = vb & vc;
                    break;
                case OP_OR_LONG:
                    res = vb | vc;
                    break;
                case OP_XOR_LONG:
                    res = vb ^ vc;
                    break;
                case OP_SHL_LONG:
                    res = static_cast<int64_t>(uvb << uvc);
                    break;
                case OP_SHR_LONG:
                    res = vb >> uvc;
                    break;
                case OP_USHR_LONG:
                    res = static_cast<int64_t>(uvb >> uvc);
                    break;
                default:
                    break;
                }
                WriteWide(regs, aa, static_cast<uint64_t>(res));
                pc += 2;
            } else if (op >= OP_ADD_FLOAT && op <= OP_REM_FLOAT) {
                uint8_t aa = (insn >> 8) & 0xFFu;
                float b = BitsToFloat(regs[insns[pc + 1] & 0xFFu]);
                float c = BitsToFloat(regs[(insns[pc + 1] >> 8) & 0xFFu]);
                float r = 0.0f;
                if (op == OP_ADD_FLOAT) {
                    r = b + c;
                } else if (op == OP_SUB_FLOAT) {
                    r = b - c;
                } else if (op == OP_MUL_FLOAT) {
                    r = b * c;
                } else if (op == OP_DIV_FLOAT) {
                    r = b / c;
                } else {
                    r = std::fmod(b, c);
                }
                regs[aa] = FloatToBits(r);
                pc += 2;
            } else if (op >= OP_ADD_DOUBLE && op <= OP_REM_DOUBLE) {
                uint8_t aa = (insn >> 8) & 0xFFu;
                double b = BitsToDouble(ReadWide(regs, insns[pc + 1] & 0xFFu));
                double c = BitsToDouble(ReadWide(regs, (insns[pc + 1] >> 8) & 0xFFu));
                double r = 0.0;
                if (op == OP_ADD_DOUBLE) {
                    r = b + c;
                } else if (op == OP_SUB_DOUBLE) {
                    r = b - c;
                } else if (op == OP_MUL_DOUBLE) {
                    r = b * c;
                } else if (op == OP_DIV_DOUBLE) {
                    r = b / c;
                } else {
                    r = std::fmod(b, c);
                }
                WriteWide(regs, aa, DoubleToBits(r));
                pc += 2;
            } else if (op >= OP_ADD_INT_2ADDR && op <= OP_USHR_INT_2ADDR) {
                uint8_t a = (insn >> 8) & 0xFu;
                uint8_t b = (insn >> 12) & 0xFu;
                int32_t va = static_cast<int32_t>(regs[a]);
                int32_t vb = static_cast<int32_t>(regs[b]);
                uint32_t uva = static_cast<uint32_t>(regs[a]);
                uint32_t uvb = static_cast<uint32_t>(regs[b]) & 0x1Fu;
                int32_t res = va;
                switch (op) {
                case OP_ADD_INT_2ADDR:
                    res = va + vb;
                    break;
                case OP_SUB_INT_2ADDR:
                    res = va - vb;
                    break;
                case OP_MUL_INT_2ADDR:
                    res = va * vb;
                    break;
                case OP_DIV_INT_2ADDR:
                    res = (vb != 0) ? (va / vb) : 0;
                    break;
                case OP_REM_INT_2ADDR:
                    res = (vb != 0) ? (va % vb) : 0;
                    break;
                case OP_AND_INT_2ADDR:
                    res = va & vb;
                    break;
                case OP_OR_INT_2ADDR:
                    res = va | vb;
                    break;
                case OP_XOR_INT_2ADDR:
                    res = va ^ vb;
                    break;
                case OP_SHL_INT_2ADDR:
                    res = static_cast<int32_t>(uva << uvb);
                    break;
                case OP_SHR_INT_2ADDR:
                    res = va >> uvb;
                    break;
                case OP_USHR_INT_2ADDR:
                    res = static_cast<int32_t>(uva >> uvb);
                    break;
                default:
                    break;
                }
                regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(res));
                pc += 1;
            } else if (op >= OP_ADD_LONG_2ADDR && op <= OP_USHR_LONG_2ADDR) {
                uint8_t a = (insn >> 8) & 0xFu;
                uint8_t b = (insn >> 12) & 0xFu;
                int64_t va = static_cast<int64_t>(ReadWide(regs, a));
                int64_t vb = static_cast<int64_t>(ReadWide(regs, b));
                uint32_t uvb = static_cast<uint32_t>(regs[b]) & 0x3Fu;
                int64_t res = va;
                switch (op) {
                case OP_ADD_LONG_2ADDR:
                    res = va + vb;
                    break;
                case OP_SUB_LONG_2ADDR:
                    res = va - vb;
                    break;
                case OP_MUL_LONG_2ADDR:
                    res = va * vb;
                    break;
                case OP_DIV_LONG_2ADDR:
                    res = (vb != 0) ? (va / vb) : 0;
                    break;
                case OP_REM_LONG_2ADDR:
                    res = (vb != 0) ? (va % vb) : 0;
                    break;
                case OP_AND_LONG_2ADDR:
                    res = va & vb;
                    break;
                case OP_OR_LONG_2ADDR:
                    res = va | vb;
                    break;
                case OP_XOR_LONG_2ADDR:
                    res = va ^ vb;
                    break;
                case OP_SHL_LONG_2ADDR:
                    res = static_cast<int64_t>(static_cast<uint64_t>(va) << uvb);
                    break;
                case OP_SHR_LONG_2ADDR:
                    res = va >> uvb;
                    break;
                case OP_USHR_LONG_2ADDR:
                    res = static_cast<int64_t>(static_cast<uint64_t>(va) >> uvb);
                    break;
                default:
                    break;
                }
                WriteWide(regs, a, static_cast<uint64_t>(res));
                pc += 1;
            } else if (op >= OP_ADD_FLOAT_2ADDR && op <= OP_REM_FLOAT_2ADDR) {
                uint8_t a = (insn >> 8) & 0xFu;
                uint8_t b = (insn >> 12) & 0xFu;
                float fa = BitsToFloat(regs[a]);
                float fb = BitsToFloat(regs[b]);
                float r = (op == OP_ADD_FLOAT_2ADDR)   ? (fa + fb)
                          : (op == OP_SUB_FLOAT_2ADDR) ? (fa - fb)
                          : (op == OP_MUL_FLOAT_2ADDR) ? (fa * fb)
                          : (op == OP_DIV_FLOAT_2ADDR) ? (fa / fb)
                                                       : std::fmod(fa, fb);
                regs[a] = FloatToBits(r);
                pc += 1;
            } else if (op >= OP_ADD_DOUBLE_2ADDR && op <= OP_REM_DOUBLE_2ADDR) {
                uint8_t a = (insn >> 8) & 0xFu;
                uint8_t b = (insn >> 12) & 0xFu;
                double da = BitsToDouble(ReadWide(regs, a));
                double db = BitsToDouble(ReadWide(regs, b));
                double r = (op == OP_ADD_DOUBLE_2ADDR)   ? (da + db)
                           : (op == OP_SUB_DOUBLE_2ADDR) ? (da - db)
                           : (op == OP_MUL_DOUBLE_2ADDR) ? (da * db)
                           : (op == OP_DIV_DOUBLE_2ADDR) ? (da / db)
                                                         : std::fmod(da, db);
                WriteWide(regs, a, DoubleToBits(r));
                pc += 1;
            } else if (op >= OP_ADD_INT_LIT16 && op <= OP_XOR_INT_LIT16) {
                uint8_t a = (insn >> 8) & 0xFu;
                uint8_t b = (insn >> 12) & 0xFu;
                int32_t vb = static_cast<int32_t>(regs[b]);
                int32_t imm = static_cast<int16_t>(insns[pc + 1]);
                int32_t res = 0;
                switch (op) {
                case OP_ADD_INT_LIT16:
                    res = vb + imm;
                    break;
                case OP_RSUB_INT:
                    res = imm - vb;
                    break;
                case OP_MUL_INT_LIT16:
                    res = vb * imm;
                    break;
                case OP_DIV_INT_LIT16:
                    res = (imm != 0) ? (vb / imm) : 0;
                    break;
                case OP_REM_INT_LIT16:
                    res = (imm != 0) ? (vb % imm) : 0;
                    break;
                case OP_AND_INT_LIT16:
                    res = vb & imm;
                    break;
                case OP_OR_INT_LIT16:
                    res = vb | imm;
                    break;
                case OP_XOR_INT_LIT16:
                    res = vb ^ imm;
                    break;
                default:
                    break;
                }
                regs[a] = static_cast<uint64_t>(static_cast<uint32_t>(res));
                pc += 2;
            } else if (op >= OP_ADD_INT_LIT8 && op <= OP_USHR_INT_LIT8) {
                uint8_t aa = (insn >> 8) & 0xFFu;
                uint8_t bb = insns[pc + 1] & 0xFFu;
                int8_t imm = static_cast<int8_t>((insns[pc + 1] >> 8) & 0xFFu);
                int32_t vb = static_cast<int32_t>(regs[bb]);
                uint32_t uvb = static_cast<uint32_t>(regs[bb]);
                uint32_t uimm = static_cast<uint32_t>(imm) & 0x1Fu;
                int32_t res = 0;
                switch (op) {
                case OP_ADD_INT_LIT8:
                    res = vb + imm;
                    break;
                case OP_RSUB_INT_LIT8:
                    res = imm - vb;
                    break;
                case OP_MUL_INT_LIT8:
                    res = vb * imm;
                    break;
                case OP_DIV_INT_LIT8:
                    res = (imm != 0) ? (vb / imm) : 0;
                    break;
                case OP_REM_INT_LIT8:
                    res = (imm != 0) ? (vb % imm) : 0;
                    break;
                case OP_AND_INT_LIT8:
                    res = vb & imm;
                    break;
                case OP_OR_INT_LIT8:
                    res = vb | imm;
                    break;
                case OP_XOR_INT_LIT8:
                    res = vb ^ imm;
                    break;
                case OP_SHL_INT_LIT8:
                    res = static_cast<int32_t>(uvb << uimm);
                    break;
                case OP_SHR_INT_LIT8:
                    res = vb >> uimm;
                    break;
                case OP_USHR_INT_LIT8:
                    res = static_cast<int32_t>(uvb >> uimm);
                    break;
                default:
                    break;
                }
                regs[aa] = static_cast<uint64_t>(static_cast<uint32_t>(res));
                pc += 2;
            } else {
                pc += 1;
            }
            break;
        }
        }
    }

    return {0, ret_kind};
}

} // namespace portage::package
