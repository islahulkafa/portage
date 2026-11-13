#include "package/dex_vm.hpp"
#include "package/java_runtime.hpp"
#include "package/package_manager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace portage::package {

namespace {

struct MethodHandlePayload {
    DexMethodHandleType handle_type{DexMethodHandleType::InvokeStatic};
    std::string class_desc{};
    std::string member_name{};
    std::string member_sig{};
    std::string return_type{"V"};
    std::vector<std::string> param_types{};
    std::string field_type{};
};

struct MethodTypePayload {
    std::string descriptor{};
    std::string return_type{"V"};
    std::vector<std::string> param_types{};
};

struct LambdaProxyPayload {
    MethodHandlePayload impl{};
    std::string interface_slash{};
    std::vector<std::string> extra_interfaces{};
    std::string sam_name{};
    std::string sam_sig{};
    std::vector<uint64_t> captured_args{};
};

std::string SlashToDot(std::string_view s)
{
    std::string out(s);
    for (char &ch : out) {
        if (ch == '/') {
            ch = '.';
        }
    }
    return out;
}

std::string DotToSlash(std::string_view s)
{
    std::string out(s);
    for (char &ch : out) {
        if (ch == '.') {
            ch = '/';
        }
    }
    return out;
}

std::vector<std::string> ParseParamDescriptors(std::string_view sig)
{
    std::vector<std::string> out;
    auto open_p = sig.find('(');
    auto close_p = sig.find(')');
    if (open_p == std::string_view::npos || close_p == std::string_view::npos ||
        close_p <= open_p) {
        return out;
    }
    std::string_view params = sig.substr(open_p + 1, close_p - open_p - 1);
    for (size_t i = 0; i < params.size();) {
        size_t start = i;
        while (i < params.size() && params[i] == '[') {
            ++i;
        }
        if (i < params.size() && params[i] == 'L') {
            size_t semi = params.find(';', i);
            i = (semi == std::string_view::npos) ? params.size() : (semi + 1);
        } else if (i < params.size()) {
            ++i;
        }
        if (i > start) {
            out.emplace_back(params.substr(start, i - start));
        }
    }
    return out;
}

std::string ParseReturnDescriptor(std::string_view sig)
{
    auto close_p = sig.find(')');
    if (close_p == std::string_view::npos || close_p + 1 >= sig.size()) {
        return "V";
    }
    return std::string(sig.substr(close_p + 1));
}

bool IsPrimitiveDescriptor(std::string_view d)
{
    return d.size() == 1 &&
           (d[0] == 'V' || d[0] == 'Z' || d[0] == 'B' || d[0] == 'S' || d[0] == 'C' ||
            d[0] == 'I' || d[0] == 'J' || d[0] == 'F' || d[0] == 'D');
}

jobject BoxValue(uint64_t raw, std::string_view desc)
{
    if (desc.empty() || desc == "V") {
        return nullptr;
    }
    if (desc.front() == 'L' || desc.front() == '[') {
        return reinterpret_cast<jobject>(raw);
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    switch (desc.front()) {
    case 'Z':
        syn->class_name = "java/lang/Boolean";
        syn->bool_fields["value"] = (raw != 0) ? 1 : 0;
        syn->int_fields["value"] = (raw != 0) ? 1 : 0;
        break;
    case 'B':
        syn->class_name = "java/lang/Byte";
        syn->int_fields["value"] = static_cast<int8_t>(raw);
        break;
    case 'S':
        syn->class_name = "java/lang/Short";
        syn->int_fields["value"] = static_cast<int16_t>(raw);
        break;
    case 'C':
        syn->class_name = "java/lang/Character";
        syn->int_fields["value"] = static_cast<uint16_t>(raw);
        break;
    case 'I':
        syn->class_name = "java/lang/Integer";
        syn->int_fields["value"] = static_cast<int32_t>(raw);
        break;
    case 'J':
        syn->class_name = "java/lang/Long";
        syn->long_fields["value"] = static_cast<int64_t>(raw);
        break;
    case 'F': {
        syn->class_name = "java/lang/Float";
        uint32_t bits = static_cast<uint32_t>(raw);
        float f = 0.0f;
        std::memcpy(&f, &bits, sizeof(f));
        syn->float_fields["value"] = f;
        break;
    }
    case 'D': {
        syn->class_name = "java/lang/Double";
        double d = 0.0;
        std::memcpy(&d, &raw, sizeof(d));
        syn->double_fields["value"] = d;
        break;
    }
    default:
        return nullptr;
    }
    return JniEnvironment::Instance().RegisterObject(syn);
}

uint64_t UnboxOrPassArg(jobject obj, std::string_view expected_desc)
{
    if (expected_desc.empty() || expected_desc.front() == 'L' || expected_desc.front() == '[') {
        return reinterpret_cast<uintptr_t>(obj);
    }
    if (!obj) {
        return 0;
    }
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return reinterpret_cast<uintptr_t>(obj);
    }
    if (expected_desc == "J") {
        if (auto it = syn->long_fields.find("value"); it != syn->long_fields.end()) {
            return static_cast<uint64_t>(it->second);
        }
        if (auto it = syn->int_fields.find("value"); it != syn->int_fields.end()) {
            return static_cast<uint64_t>(static_cast<int64_t>(it->second));
        }
    }
    if (expected_desc == "F") {
        if (auto it = syn->float_fields.find("value"); it != syn->float_fields.end()) {
            uint32_t bits = 0;
            std::memcpy(&bits, &it->second, sizeof(bits));
            return bits;
        }
    }
    if (expected_desc == "D") {
        if (auto it = syn->double_fields.find("value"); it != syn->double_fields.end()) {
            uint64_t bits = 0;
            std::memcpy(&bits, &it->second, sizeof(bits));
            return bits;
        }
    }
    if (expected_desc == "Z") {
        if (auto it = syn->bool_fields.find("value"); it != syn->bool_fields.end()) {
            return it->second ? 1 : 0;
        }
    }
    if (auto it = syn->int_fields.find("value"); it != syn->int_fields.end()) {
        return static_cast<uint64_t>(static_cast<uint32_t>(it->second));
    }
    return 0;
}

std::vector<uint64_t> PackRawParamsForDex(const std::vector<std::string> &param_types,
                                          std::span<const uint64_t> args)
{
    std::vector<uint64_t> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i < param_types.size() && (param_types[i] == "J" || param_types[i] == "D")) {
            out.push_back(args[i] & 0xFFFFFFFFULL);
            out.push_back((args[i] >> 32) & 0xFFFFFFFFULL);
        } else {
            out.push_back(args[i]);
        }
    }
    return out;
}

DexValue InvokeMethodHandleImpl(DexVm &vm, const MethodHandlePayload &mh,
                                std::span<const uint64_t> logical_args, JNIEnv *env,
                                jobject &out_exception)
{
    auto &jni = JniEnvironment::Instance();
    std::string cls_slash = DexFile::DescriptorToSlashName(mh.class_desc);
    std::string cls_desc = DexFile::NormalizeDescriptor(mh.class_desc);

    switch (mh.handle_type) {
    case DexMethodHandleType::StaticGet: {
        uint64_t val = vm.GetStaticField(cls_desc, mh.member_name);
        char k = mh.field_type.empty() ? 'I' : mh.field_type.front();
        return {val, k};
    }
    case DexMethodHandleType::StaticPut: {
        if (!logical_args.empty()) {
            vm.SetStaticField(cls_desc, mh.member_name, logical_args[0]);
        }
        return {0, 'V'};
    }
    case DexMethodHandleType::InstanceGet: {
        if (logical_args.empty()) {
            return {0, 'V'};
        }
        uint64_t val = 0;
        (void)vm.GetInstanceField(reinterpret_cast<jobject>(logical_args[0]), mh.member_name,
                                  mh.field_type, val);
        char k = mh.field_type.empty() ? 'I' : mh.field_type.front();
        return {val, k};
    }
    case DexMethodHandleType::InstancePut: {
        if (logical_args.size() >= 2) {
            vm.SetInstanceField(reinterpret_cast<jobject>(logical_args[0]), mh.member_name,
                                mh.field_type, logical_args[1]);
        }
        return {0, 'V'};
    }
    case DexMethodHandleType::InvokeConstructor: {
        vm.EnsureClassInitialized(cls_desc, env);
        auto ctor = vm.ResolveMethod(cls_desc, "<init>", mh.member_sig, false);
        auto packed = PackRawParamsForDex(mh.param_types, logical_args);
        jobject obj = vm.NewObject(cls_desc, ctor ? ctor->method : nullptr, packed, env);
        if (jthrowable pending = jni.GetPendingException()) {
            out_exception = pending;
            jni.ClearPendingException();
        }
        return {reinterpret_cast<uintptr_t>(obj), 'L'};
    }
    case DexMethodHandleType::InvokeStatic: {
        vm.EnsureClassInitialized(cls_desc, env);
        if (auto resolved = vm.ResolveMethod(cls_desc, mh.member_name, mh.member_sig, true)) {
            auto packed = PackRawParamsForDex(mh.param_types, logical_args);
            DexValue res = vm.ExecuteMethod(resolved->dex, resolved->method, packed, env);
            if (jthrowable pending = jni.GetPendingException()) {
                out_exception = pending;
                jni.ClearPendingException();
            }
            return res;
        }
        DexMethodRef ref{};
        ref.class_descriptor = cls_desc;
        ref.name = mh.member_name;
        ref.descriptor = mh.member_sig;
        ref.param_types = mh.param_types;
        ref.return_type = mh.return_type;
        auto packed = PackRawParamsForDex(mh.param_types, logical_args);
        bool handled = false;
        DexValue res = JavaRuntime::Instance().Invoke(vm, cls_slash, ref, packed, true, env,
                                                      out_exception, handled);
        return res;
    }
    case DexMethodHandleType::InvokeInstance:
    case DexMethodHandleType::InvokeDirect:
    case DexMethodHandleType::InvokeInterface: {
        if (logical_args.empty()) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "Null receiver in MethodHandle invoke");
            return {0, 'V'};
        }
        jobject recv = reinterpret_cast<jobject>(logical_args[0]);
        if (!recv) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "Null receiver in MethodHandle invoke");
            return {0, 'V'};
        }
        auto packed_extra = PackRawParamsForDex(mh.param_types, logical_args.subspan(1));
        if (mh.handle_type == DexMethodHandleType::InvokeDirect) {
            if (auto resolved = vm.ResolveMethod(cls_desc, mh.member_name, mh.member_sig, false)) {
                std::vector<uint64_t> full;
                full.push_back(reinterpret_cast<uintptr_t>(recv));
                full.insert(full.end(), packed_extra.begin(), packed_extra.end());
                DexValue res = vm.ExecuteMethod(resolved->dex, resolved->method, full, env);
                if (jthrowable pending = jni.GetPendingException()) {
                    out_exception = pending;
                    jni.ClearPendingException();
                }
                return res;
            }
        }
        return JavaRuntime::CallVirtual(vm, recv, mh.member_name, mh.member_sig, packed_extra, env,
                                        out_exception);
    }
    }
    return {0, 'V'};
}

MethodHandlePayload DecodeMethodHandleFromDex(const DexFile *dex, const DexMethodHandleDef &mh)
{
    MethodHandlePayload out{};
    out.handle_type = mh.type;
    if (!dex) {
        return out;
    }
    if (mh.type == DexMethodHandleType::StaticPut || mh.type == DexMethodHandleType::StaticGet ||
        mh.type == DexMethodHandleType::InstancePut ||
        mh.type == DexMethodHandleType::InstanceGet) {
        if (const auto *f = dex->GetFieldRef(mh.field_or_method_id)) {
            out.class_desc = f->class_descriptor;
            out.member_name = f->name;
            out.field_type = f->type_descriptor;
        }
    } else {
        if (const auto *m = dex->GetMethodRef(mh.field_or_method_id)) {
            out.class_desc = m->class_descriptor;
            out.member_name = m->name;
            out.member_sig = m->descriptor;
            out.return_type = m->return_type;
            out.param_types = m->param_types;
        }
    }
    return out;
}

jobject MakeByteArrayInputStream(std::vector<uint8_t> bytes)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/io/ByteArrayInputStream";
    syn->byte_data.reserve(bytes.size());
    for (uint8_t b : bytes) {
        syn->byte_data.push_back(static_cast<int8_t>(b));
    }
    syn->int_fields["pos"] = 0;
    syn->int_fields["mark"] = 0;
    syn->int_fields["count"] = static_cast<int32_t>(bytes.size());
    return JniEnvironment::Instance().RegisterObject(syn);
}

std::string FormatModifierString(uint32_t flags)
{
    std::vector<std::string_view> parts;
    if (flags & ACC_PUBLIC) {
        parts.push_back("public");
    }
    if (flags & ACC_PROTECTED) {
        parts.push_back("protected");
    }
    if (flags & ACC_PRIVATE) {
        parts.push_back("private");
    }
    if (flags & ACC_ABSTRACT) {
        parts.push_back("abstract");
    }
    if (flags & ACC_STATIC) {
        parts.push_back("static");
    }
    if (flags & ACC_FINAL) {
        parts.push_back("final");
    }
    if (flags & ACC_SYNCHRONIZED) {
        parts.push_back("synchronized");
    }
    if (flags & ACC_NATIVE) {
        parts.push_back("native");
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out.push_back(' ');
        }
        out.append(parts[i]);
    }
    return out;
}

jobject CreateReflectedMethodObj(std::string_view class_slash, std::string_view name,
                                 std::string_view sig, uint32_t flags, bool is_ctor)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Method;
    obj->class_name = is_ctor ? "java/lang/reflect/Constructor" : "java/lang/reflect/Method";
    obj->target_class = std::string(class_slash);
    obj->member_name = std::string(name);
    obj->signature = std::string(sig);
    obj->is_static = (flags & ACC_STATIC) != 0;
    obj->int_fields["modifiers"] = static_cast<int32_t>(flags);
    obj->string_fields["name"] = std::string(name);
    return JniEnvironment::Instance().RegisterObject(obj);
}

jobject CreateReflectedFieldObj(std::string_view class_slash, std::string_view name,
                                std::string_view type_desc, uint32_t flags)
{
    auto obj = std::make_shared<JniSyntheticObject>();
    obj->type = JniObjectType::Field;
    obj->class_name = "java/lang/reflect/Field";
    obj->target_class = std::string(class_slash);
    obj->member_name = std::string(name);
    obj->signature = std::string(type_desc);
    obj->is_static = (flags & ACC_STATIC) != 0;
    obj->int_fields["modifiers"] = static_cast<int32_t>(flags);
    obj->string_fields["name"] = std::string(name);
    return JniEnvironment::Instance().RegisterObject(obj);
}

jobject BuildObjectArray(std::string_view elem_class, const std::vector<jobject> &items)
{
    auto arr = std::make_shared<JniSyntheticObject>();
    arr->type = JniObjectType::ObjectArray;
    arr->class_name = std::string(elem_class);
    arr->object_data = items;
    return JniEnvironment::Instance().RegisterObject(arr);
}

bool MatchClassArrayArg(jobject cls_arr_obj, const std::vector<std::string> &param_types)
{
    if (!cls_arr_obj) {
        return param_types.empty();
    }
    auto syn = JniEnvironment::Instance().LookupObject(cls_arr_obj);
    if (!syn || syn->type != JniObjectType::ObjectArray) {
        return param_types.empty();
    }
    if (syn->object_data.size() != param_types.size()) {
        return false;
    }
    for (size_t i = 0; i < param_types.size(); ++i) {
        std::string given = JavaRuntime::ExtractClassSlash(syn->object_data[i]);
        std::string expected = DexFile::DescriptorToSlashName(param_types[i]);
        if (given != expected) {
            return false;
        }
    }
    return true;
}

std::string MakeStringConcatRecipe(DexVm &vm, std::string_view recipe,
                                   const std::vector<DexEncodedValue> &const_args,
                                   std::span<const uint64_t> dyn_args,
                                   const std::vector<std::string> &dyn_types, JNIEnv *env,
                                   jobject &out_exception)
{
    std::string out;
    size_t dyn_idx = 0;
    size_t const_idx = 0;
    for (unsigned char ch : recipe) {
        if (ch == 0x01u) {
            if (dyn_idx < dyn_args.size()) {
                std::string ptype =
                    dyn_idx < dyn_types.size() ? dyn_types[dyn_idx] : "Ljava/lang/Object;";
                uint64_t v = dyn_args[dyn_idx];
                if (ptype == "I") {
                    out += std::to_string(static_cast<int32_t>(v));
                } else if (ptype == "J") {
                    out += std::to_string(static_cast<int64_t>(v));
                } else if (ptype == "Z") {
                    out += (v != 0) ? "true" : "false";
                } else if (ptype == "B") {
                    out += std::to_string(static_cast<int8_t>(v));
                } else if (ptype == "S") {
                    out += std::to_string(static_cast<int16_t>(v));
                } else if (ptype == "C") {
                    char16_t c16 = static_cast<char16_t>(v & 0xFFFFu);
                    out += java_rt::Utf16ToUtf8(std::u16string_view(&c16, 1));
                } else if (ptype == "F") {
                    uint32_t bits = static_cast<uint32_t>(v);
                    float f = 0.0f;
                    std::memcpy(&f, &bits, sizeof(f));
                    std::ostringstream oss;
                    oss << f;
                    out += oss.str();
                } else if (ptype == "D") {
                    double d = 0.0;
                    std::memcpy(&d, &v, sizeof(d));
                    std::ostringstream oss;
                    oss << d;
                    out += oss.str();
                } else {
                    out += JavaRuntime::ObjectToString(vm, reinterpret_cast<jobject>(v), env,
                                                       out_exception);
                }
            }
            ++dyn_idx;
        } else if (ch == 0x02u) {
            if (const_idx < const_args.size()) {
                const auto &cv = const_args[const_idx];
                if (cv.kind == DexEncodedValue::Kind::String) {
                    out += cv.text;
                } else if (cv.kind == DexEncodedValue::Kind::Int) {
                    out += std::to_string(static_cast<int32_t>(cv.raw));
                } else if (cv.kind == DexEncodedValue::Kind::Long) {
                    out += std::to_string(static_cast<int64_t>(cv.raw));
                } else if (cv.kind == DexEncodedValue::Kind::Boolean) {
                    out += (cv.raw != 0) ? "true" : "false";
                }
            }
            ++const_idx;
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::vector<uint64_t> UnpackArgsToLogical(const std::vector<std::string> &param_types,
                                          std::span<const uint64_t> raw_regs)
{
    std::vector<uint64_t> out;
    size_t r = 0;
    for (const auto &pt : param_types) {
        if (r >= raw_regs.size()) {
            break;
        }
        if ((pt == "J" || pt == "D") && r + 1 < raw_regs.size()) {
            uint64_t wide =
                (raw_regs[r] & 0xFFFFFFFFULL) | ((raw_regs[r + 1] & 0xFFFFFFFFULL) << 32);
            out.push_back(wide);
            r += 2;
        } else {
            out.push_back(raw_regs[r]);
            r += 1;
        }
    }
    return out;
}

DexValue HandleClass(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                     bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static && ref.name == "forName" && !arg_regs.empty()) {
        const char *name_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
        if (!name_c) {
            out_exception =
                JavaRuntime::CreateException("java/lang/ClassNotFoundException", "null");
            handled = true;
            return {0, 'L'};
        }
        std::string slash = DotToSlash(name_c);
        std::string desc = DexFile::NormalizeDescriptor(slash);
        if (!vm.HasClass(desc) && !JavaRuntime::Instance().HasHostClass(slash)) {
            out_exception =
                JavaRuntime::CreateException("java/lang/ClassNotFoundException", name_c);
            handled = true;
            return {0, 'L'};
        }
        bool init = true;
        if (arg_regs.size() >= 2 && ref.param_types.size() >= 2 && ref.param_types[1] == "Z") {
            init = (arg_regs[1] != 0);
        }
        if (init && vm.HasClass(desc)) {
            vm.EnsureClassInitialized(desc, env);
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(slash)), 'L'};
    }

    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }

    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    std::string cls_slash = JavaRuntime::ExtractClassSlash(self);
    if (cls_slash.empty()) {
        cls_slash = "java/lang/Object";
    }
    std::string cls_desc = DexFile::NormalizeDescriptor(cls_slash);

    if (ref.name == "getName") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(SlashToDot(cls_slash))), 'L'};
    }
    if (ref.name == "getSimpleName" || ref.name == "getCanonicalName") {
        std::string s = (ref.name == "getCanonicalName") ? SlashToDot(cls_slash) : cls_slash;
        if (ref.name == "getSimpleName") {
            auto pos = s.find_last_of("/$");
            if (pos != std::string::npos) {
                s = s.substr(pos + 1);
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(s)), 'L'};
    }
    if (ref.name == "toString") {
        const auto *cdef = vm.FindClassDef(cls_desc);
        bool is_iface = cdef && (cdef->access_flags & ACC_INTERFACE) != 0;
        std::string prefix = is_iface ? "interface " : "class ";
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(prefix + SlashToDot(cls_slash))), 'L'};
    }
    if (ref.name == "getSuperclass") {
        if (cls_slash == "java/lang/Object") {
            handled = true;
            return {0, 'L'};
        }
        std::string super_slash = "java/lang/Object";
        if (const auto *cdef = vm.FindClassDef(cls_desc);
            cdef && !cdef->superclass_descriptor.empty()) {
            super_slash = DexFile::DescriptorToSlashName(cdef->superclass_descriptor);
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(super_slash)),
                'L'};
    }
    if (ref.name == "getInterfaces") {
        std::vector<jobject> items;
        if (const auto *cdef = vm.FindClassDef(cls_desc)) {
            for (const auto &iface : cdef->interfaces) {
                items.push_back(
                    JavaRuntime::Instance().GetClassObject(DexFile::DescriptorToSlashName(iface)));
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(BuildObjectArray("java/lang/Class", items)), 'L'};
    }
    if (ref.name == "isInterface") {
        const auto *cdef = vm.FindClassDef(cls_desc);
        bool res = cdef && (cdef->access_flags & ACC_INTERFACE) != 0;
        handled = true;
        return {static_cast<uint64_t>(res ? 1 : 0), 'Z'};
    }
    if (ref.name == "isArray") {
        handled = true;
        return {static_cast<uint64_t>(!cls_slash.empty() && cls_slash.front() == '[' ? 1 : 0), 'Z'};
    }
    if (ref.name == "isEnum") {
        const auto *cdef = vm.FindClassDef(cls_desc);
        bool res = cdef && ((cdef->access_flags & ACC_ENUM) != 0 ||
                            cdef->superclass_descriptor == "Ljava/lang/Enum;");
        handled = true;
        return {static_cast<uint64_t>(res ? 1 : 0), 'Z'};
    }
    if (ref.name == "isPrimitive") {
        handled = true;
        return {static_cast<uint64_t>(IsPrimitiveDescriptor(cls_slash) ? 1 : 0), 'Z'};
    }
    if (ref.name == "getModifiers") {
        const auto *cdef = vm.FindClassDef(cls_desc);
        handled = true;
        return {static_cast<uint64_t>(cdef ? cdef->access_flags : ACC_PUBLIC), 'I'};
    }
    if (ref.name == "isInstance" && arg_regs.size() >= 2) {
        jobject obj = reinterpret_cast<jobject>(arg_regs[1]);
        handled = true;
        return {static_cast<uint64_t>(vm.IsInstanceOf(obj, cls_desc) ? 1 : 0), 'Z'};
    }
    if (ref.name == "cast" && arg_regs.size() >= 2) {
        jobject obj = reinterpret_cast<jobject>(arg_regs[1]);
        if (obj && !vm.IsInstanceOf(obj, cls_desc)) {
            out_exception = JavaRuntime::CreateException("java/lang/ClassCastException",
                                                         "Cannot cast object to " + cls_slash);
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(obj), 'L'};
    }
    if (ref.name == "isAssignableFrom" && arg_regs.size() >= 2) {
        std::string sub_slash =
            JavaRuntime::ExtractClassSlash(reinterpret_cast<jobject>(arg_regs[1]));
        bool ok = (sub_slash == cls_slash) ||
                  JavaRuntime::Instance().IsAssignableFrom(sub_slash, cls_slash);
        if (!ok && vm.HasClass(DexFile::NormalizeDescriptor(sub_slash))) {
            auto dummy = std::make_shared<JniSyntheticObject>();
            dummy->class_name = sub_slash;
            jobject d = jni.RegisterObject(dummy);
            ok = vm.IsInstanceOf(d, cls_desc);
        }
        handled = true;
        return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
    }
    if (ref.name == "newInstance") {
        vm.EnsureClassInitialized(cls_desc, env);
        auto ctor = vm.ResolveMethod(cls_desc, "<init>", "()V", false);
        jobject inst = vm.NewObject(cls_desc, ctor ? ctor->method : nullptr, {}, env);
        handled = true;
        return {reinterpret_cast<uintptr_t>(inst), 'L'};
    }
    if ((ref.name == "getDeclaredMethod" || ref.name == "getMethod") && arg_regs.size() >= 2) {
        const char *mname = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        jobject ptypes_arr =
            arg_regs.size() >= 3 ? reinterpret_cast<jobject>(arg_regs[2]) : nullptr;
        std::string cur = cls_desc;
        int max_depth = (ref.name == "getMethod") ? 16 : 1;
        for (int d = 0; d < max_depth && !cur.empty(); ++d) {
            if (const auto *cdef = vm.FindClassDef(cur)) {
                auto scan = [&](const std::vector<DexMethodDef> &ms) -> jobject {
                    for (const auto &m : ms) {
                        if (mname && m.name == mname &&
                            MatchClassArrayArg(ptypes_arr, m.param_types)) {
                            return CreateReflectedMethodObj(DexFile::DescriptorToSlashName(cur),
                                                            m.name, m.descriptor, m.access_flags,
                                                            false);
                        }
                    }
                    return nullptr;
                };
                if (jobject found = scan(cdef->virtual_methods)) {
                    handled = true;
                    return {reinterpret_cast<uintptr_t>(found), 'L'};
                }
                if (jobject found = scan(cdef->direct_methods)) {
                    handled = true;
                    return {reinterpret_cast<uintptr_t>(found), 'L'};
                }
                cur = cdef->superclass_descriptor;
            } else {
                break;
            }
        }
        out_exception =
            JavaRuntime::CreateException("java/lang/NoSuchMethodException", mname ? mname : "null");
        handled = true;
        return {0, 'L'};
    }
    if (ref.name == "getDeclaredMethods" || ref.name == "getMethods") {
        std::vector<jobject> items;
        if (const auto *cdef = vm.FindClassDef(cls_desc)) {
            for (const auto &m : cdef->virtual_methods) {
                items.push_back(CreateReflectedMethodObj(cls_slash, m.name, m.descriptor,
                                                         m.access_flags, false));
            }
            for (const auto &m : cdef->direct_methods) {
                if (m.name != "<init>" && m.name != "<clinit>") {
                    items.push_back(CreateReflectedMethodObj(cls_slash, m.name, m.descriptor,
                                                             m.access_flags, false));
                }
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(BuildObjectArray("java/lang/reflect/Method", items)),
                'L'};
    }
    if (ref.name == "getDeclaredConstructor" || ref.name == "getConstructor") {
        jobject ptypes_arr =
            arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        if (const auto *cdef = vm.FindClassDef(cls_desc)) {
            for (const auto &m : cdef->direct_methods) {
                if (m.name == "<init>" && MatchClassArrayArg(ptypes_arr, m.param_types)) {
                    handled = true;
                    return {reinterpret_cast<uintptr_t>(CreateReflectedMethodObj(
                                cls_slash, "<init>", m.descriptor, m.access_flags, true)),
                            'L'};
                }
            }
        }
        out_exception = JavaRuntime::CreateException("java/lang/NoSuchMethodException", "<init>");
        handled = true;
        return {0, 'L'};
    }
    if ((ref.name == "getDeclaredField" || ref.name == "getField") && arg_regs.size() >= 2) {
        const char *fname = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        std::string cur = cls_desc;
        int max_depth = (ref.name == "getField") ? 16 : 1;
        for (int d = 0; d < max_depth && !cur.empty(); ++d) {
            if (const auto *cdef = vm.FindClassDef(cur)) {
                auto scan_f = [&](const std::vector<DexFieldDef> &fs) -> jobject {
                    for (const auto &f : fs) {
                        if (fname && f.name == fname) {
                            return CreateReflectedFieldObj(DexFile::DescriptorToSlashName(cur),
                                                           f.name, f.type_descriptor,
                                                           f.access_flags);
                        }
                    }
                    return nullptr;
                };
                if (jobject found = scan_f(cdef->static_fields)) {
                    handled = true;
                    return {reinterpret_cast<uintptr_t>(found), 'L'};
                }
                if (jobject found = scan_f(cdef->instance_fields)) {
                    handled = true;
                    return {reinterpret_cast<uintptr_t>(found), 'L'};
                }
                cur = cdef->superclass_descriptor;
            } else {
                break;
            }
        }
        out_exception =
            JavaRuntime::CreateException("java/lang/NoSuchFieldException", fname ? fname : "null");
        handled = true;
        return {0, 'L'};
    }
    if (ref.name == "getDeclaredFields" || ref.name == "getFields") {
        std::vector<jobject> items;
        if (const auto *cdef = vm.FindClassDef(cls_desc)) {
            for (const auto &f : cdef->static_fields) {
                items.push_back(
                    CreateReflectedFieldObj(cls_slash, f.name, f.type_descriptor, f.access_flags));
            }
            for (const auto &f : cdef->instance_fields) {
                items.push_back(
                    CreateReflectedFieldObj(cls_slash, f.name, f.type_descriptor, f.access_flags));
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(BuildObjectArray("java/lang/reflect/Field", items)),
                'L'};
    }
    if (ref.name == "getEnumConstants") {
        if (auto resolved = vm.ResolveMethod(cls_desc, "values", "()[" + cls_desc, false)) {
            DexValue v = vm.ExecuteMethod(resolved->dex, resolved->method, {}, env);
            handled = true;
            return v;
        }
        handled = true;
        return {0, 'L'};
    }
    if (ref.name == "getClassLoader") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.GetClassLoaderObject()), 'L'};
    }
    if (ref.name == "getResourceAsStream" && arg_regs.size() >= 2) {
        const char *res_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        if (res_c) {
            std::string rel(res_c);
            const auto *pkg = PackageManager::Instance().GetActivePackage();
            if (pkg && pkg->archive) {
                std::vector<std::string> candidates;
                if (!rel.empty() && rel.front() == '/') {
                    std::string trimmed = rel.substr(1);
                    candidates.push_back(trimmed);
                    candidates.push_back("assets/" + trimmed);
                } else {
                    auto slash_pos = cls_slash.find_last_of('/');
                    if (slash_pos != std::string::npos) {
                        candidates.push_back(cls_slash.substr(0, slash_pos + 1) + rel);
                    }
                    candidates.push_back(rel);
                    candidates.push_back("assets/" + rel);
                }
                for (const auto &cand : candidates) {
                    if (auto data = pkg->archive->ReadEntry(cand)) {
                        handled = true;
                        return {
                            reinterpret_cast<uintptr_t>(MakeByteArrayInputStream(std::move(*data))),
                            'L'};
                    }
                }
            }
        }
        handled = true;
        return {0, 'L'};
    }
    return {0, 'V'};
}

DexValue HandleClassLoader(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                           bool is_static, JNIEnv *, jobject &, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static && ref.name == "getSystemClassLoader") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.GetClassLoaderObject()), 'L'};
    }
    if (!is_static && ref.name == "getResourceAsStream" && arg_regs.size() >= 2) {
        const char *res_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        const auto *pkg = PackageManager::Instance().GetActivePackage();
        if (res_c && pkg && pkg->archive) {
            std::string rel(res_c);
            if (!rel.empty() && rel.front() == '/') {
                rel.erase(0, 1);
            }
            if (auto data = pkg->archive->ReadEntry(rel)) {
                handled = true;
                return {reinterpret_cast<uintptr_t>(MakeByteArrayInputStream(std::move(*data))),
                        'L'};
            }
            if (auto data = pkg->archive->ReadEntry("assets/" + rel)) {
                handled = true;
                return {reinterpret_cast<uintptr_t>(MakeByteArrayInputStream(std::move(*data))),
                        'L'};
            }
        }
        handled = true;
        return {0, 'L'};
    }
    return {0, 'V'};
}

DexValue HandleReflectMember(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (is_static) {
        if (ref.name == "toString" && !arg_regs.empty()) {
            handled = true;
            return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(
                        FormatModifierString(static_cast<uint32_t>(arg_regs[0])))),
                    'L'};
        }
        if (ref.name == "isPublic" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>((arg_regs[0] & ACC_PUBLIC) != 0 ? 1 : 0), 'Z'};
        }
        if (ref.name == "isPrivate" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>((arg_regs[0] & ACC_PRIVATE) != 0 ? 1 : 0), 'Z'};
        }
        if (ref.name == "isProtected" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>((arg_regs[0] & ACC_PROTECTED) != 0 ? 1 : 0), 'Z'};
        }
        if (ref.name == "isStatic" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>((arg_regs[0] & ACC_STATIC) != 0 ? 1 : 0), 'Z'};
        }
        if (ref.name == "isFinal" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>((arg_regs[0] & ACC_FINAL) != 0 ? 1 : 0), 'Z'};
        }
        return {0, 'V'};
    }
    if (arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "setAccessible") {
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getName") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(syn->member_name)), 'L'};
    }
    if (ref.name == "getDeclaringClass") {
        handled = true;
        return {
            reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(syn->target_class)),
            'L'};
    }
    if (ref.name == "getModifiers") {
        auto it = syn->int_fields.find("modifiers");
        handled = true;
        return {static_cast<uint64_t>(it != syn->int_fields.end() ? it->second : ACC_PUBLIC), 'I'};
    }

    if (syn->type == JniObjectType::Method) {
        if ((ref.name == "invoke" && arg_regs.size() >= 2) ||
            (ref.name == "newInstance" && arg_regs.size() >= 1)) {
            bool is_ctor = (syn->class_name == "java/lang/reflect/Constructor");
            jobject recv = is_ctor ? nullptr : reinterpret_cast<jobject>(arg_regs[1]);
            jobject args_arr_obj =
                is_ctor ? (arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr)
                        : (arg_regs.size() >= 3 ? reinterpret_cast<jobject>(arg_regs[2]) : nullptr);
            std::vector<std::string> ptypes = ParseParamDescriptors(syn->signature);
            std::string rtype = ParseReturnDescriptor(syn->signature);
            std::vector<uint64_t> logical;
            if (!is_ctor && !syn->is_static) {
                logical.push_back(reinterpret_cast<uintptr_t>(recv));
            }
            if (args_arr_obj) {
                if (auto arr = jni.LookupObject(args_arr_obj);
                    arr && arr->type == JniObjectType::ObjectArray) {
                    for (size_t i = 0; i < arr->object_data.size() && i < ptypes.size(); ++i) {
                        logical.push_back(UnboxOrPassArg(arr->object_data[i], ptypes[i]));
                    }
                }
            }
            MethodHandlePayload mh{};
            mh.handle_type = is_ctor ? DexMethodHandleType::InvokeConstructor
                                     : (syn->is_static ? DexMethodHandleType::InvokeStatic
                                                       : DexMethodHandleType::InvokeInstance);
            mh.class_desc = DexFile::NormalizeDescriptor(syn->target_class);
            mh.member_name = syn->member_name;
            mh.member_sig = syn->signature;
            mh.param_types = std::move(ptypes);
            mh.return_type = rtype;
            DexValue res = InvokeMethodHandleImpl(vm, mh, logical, env, out_exception);
            handled = true;
            if (out_exception || is_ctor) {
                return res;
            }
            return {reinterpret_cast<uintptr_t>(BoxValue(res.raw, rtype)), 'L'};
        }
    }

    if (syn->type == JniObjectType::Field) {
        std::string cls_desc = DexFile::NormalizeDescriptor(syn->target_class);
        if ((ref.name == "get" || ref.name == "getInt" || ref.name == "getLong" ||
             ref.name == "getBoolean") &&
            arg_regs.size() >= 2) {
            jobject obj = reinterpret_cast<jobject>(arg_regs[1]);
            uint64_t val = 0;
            if (syn->is_static || !obj) {
                val = vm.GetStaticField(cls_desc, syn->member_name);
            } else {
                (void)vm.GetInstanceField(obj, syn->member_name, syn->signature, val);
            }
            handled = true;
            if (ref.name == "get") {
                return {reinterpret_cast<uintptr_t>(BoxValue(val, syn->signature)), 'L'};
            }
            char k = syn->signature.empty() ? 'I' : syn->signature.front();
            return {val, k};
        }
        if ((ref.name == "set" || ref.name == "setInt" || ref.name == "setLong" ||
             ref.name == "setBoolean") &&
            arg_regs.size() >= 3) {
            jobject obj = reinterpret_cast<jobject>(arg_regs[1]);
            uint64_t val = arg_regs[2];
            if (ref.name == "set") {
                val = UnboxOrPassArg(reinterpret_cast<jobject>(arg_regs[2]), syn->signature);
            }
            if (syn->is_static || !obj) {
                vm.SetStaticField(cls_desc, syn->member_name, val);
            } else {
                vm.SetInstanceField(obj, syn->member_name, syn->signature, val);
            }
            handled = true;
            return {0, 'V'};
        }
    }
    return {0, 'V'};
}

DexValue HandleEnum(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                    bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static && ref.name == "valueOf" && arg_regs.size() >= 2) {
        std::string enum_slash =
            JavaRuntime::ExtractClassSlash(reinterpret_cast<jobject>(arg_regs[0]));
        const char *name_c = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        if (name_c && !enum_slash.empty()) {
            std::string cls_desc = DexFile::NormalizeDescriptor(enum_slash);
            vm.EnsureClassInitialized(cls_desc, env);
            if (auto resolved = vm.ResolveMethod(cls_desc, "values", "()[" + cls_desc, false)) {
                DexValue arr_v = vm.ExecuteMethod(resolved->dex, resolved->method, {}, env);
                if (auto arr = jni.LookupObject(reinterpret_cast<jobject>(arr_v.raw));
                    arr && arr->type == JniObjectType::ObjectArray) {
                    for (jobject item : arr->object_data) {
                        if (auto isyn = jni.LookupObject(item)) {
                            if (isyn->string_fields["$enum_name"] == name_c) {
                                handled = true;
                                return {reinterpret_cast<uintptr_t>(item), 'L'};
                            }
                        }
                    }
                }
            }
            uint64_t fval = vm.GetStaticField(cls_desc, name_c);
            if (fval != 0) {
                handled = true;
                return {fval, 'L'};
            }
        }
        out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                     name_c ? name_c : "null");
        handled = true;
        return {0, 'L'};
    }
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }
    if (ref.name == "<init>" && arg_regs.size() >= 3) {
        if (const char *nm = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]))) {
            syn->string_fields["$enum_name"] = nm;
        }
        syn->int_fields["$enum_ordinal"] = static_cast<int32_t>(arg_regs[2]);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "name" || ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(syn->string_fields["$enum_name"])),
                'L'};
    }
    if (ref.name == "ordinal") {
        handled = true;
        return {static_cast<uint64_t>(syn->int_fields["$enum_ordinal"]), 'I'};
    }
    if (ref.name == "getDeclaringClass") {
        handled = true;
        return {
            reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject(syn->class_name)),
            'L'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        auto other = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        int32_t a = syn->int_fields["$enum_ordinal"];
        int32_t b = other ? other->int_fields["$enum_ordinal"] : 0;
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(a - b)), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        return {static_cast<uint64_t>(self == reinterpret_cast<jobject>(arg_regs[1]) ? 1 : 0), 'Z'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self))),
                'I'};
    }
    return {0, 'V'};
}

DexValue HandleMethodHandles(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (is_static && ref.name == "lookup") {
        auto syn = std::make_shared<JniSyntheticObject>();
        syn->type = JniObjectType::Generic;
        syn->class_name = "java/lang/invoke/MethodHandles$Lookup";
        handled = true;
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().RegisterObject(syn)), 'L'};
    }
    if (!is_static && !arg_regs.empty()) {
        auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
        if (syn && syn->class_name == "java/lang/invoke/MethodHandle") {
            if (ref.name == "invoke" || ref.name == "invokeExact") {
                auto payload = std::static_pointer_cast<MethodHandlePayload>(syn->runtime_payload);
                if (payload) {
                    handled = true;
                    return InvokeMethodHandleImpl(vm, *payload, arg_regs.subspan(1), env,
                                                  out_exception);
                }
            }
        }
    }
    return {0, 'V'};
}

} // namespace

jobject JavaRuntime::CreateMethodHandleObject(const DexFile *dex, const DexMethodHandleDef &mh)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/lang/invoke/MethodHandle";
    syn->runtime_payload =
        std::make_shared<MethodHandlePayload>(DecodeMethodHandleFromDex(dex, mh));
    return JniEnvironment::Instance().RegisterObject(syn);
}

jobject JavaRuntime::CreateMethodTypeObject(const DexProtoRef &proto)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/lang/invoke/MethodType";
    auto payload = std::make_shared<MethodTypePayload>();
    payload->descriptor = proto.descriptor;
    payload->return_type = proto.return_type;
    payload->param_types = proto.param_types;
    syn->runtime_payload = std::move(payload);
    return JniEnvironment::Instance().RegisterObject(syn);
}

DexValue JavaRuntime::InvokeCustomCallSite(DexVm &vm, const DexFile *dex, uint32_t call_site_idx,
                                           std::span<const uint64_t> arg_regs, JNIEnv *env,
                                           jobject &out_exception)
{
    if (!dex) {
        return {0, 'V'};
    }
    const DexCallSiteDef *cs = dex->GetCallSite(call_site_idx);
    if (!cs) {
        return {0, 'V'};
    }
    const DexMethodHandleDef *bmh_def = dex->GetMethodHandle(cs->bootstrap_method_handle_idx);
    const DexProtoRef *cs_proto = dex->GetProtoRef(cs->method_type_proto_idx);
    if (!bmh_def || !cs_proto) {
        return {0, 'V'};
    }

    MethodHandlePayload bmh = DecodeMethodHandleFromDex(dex, *bmh_def);
    std::string bsm_cls = DexFile::DescriptorToSlashName(bmh.class_desc);
    std::vector<uint64_t> logical_args = UnpackArgsToLogical(cs_proto->param_types, arg_regs);

    if (bsm_cls == "java/lang/invoke/LambdaMetafactory") {
        auto proxy = std::make_shared<JniSyntheticObject>();
        proxy->type = JniObjectType::Generic;
        proxy->class_name = "java/lang/invoke/LambdaProxy";
        auto lp = std::make_shared<LambdaProxyPayload>();
        lp->interface_slash = DexFile::DescriptorToSlashName(cs_proto->return_type);
        lp->sam_name = cs->method_name;
        lp->captured_args = std::move(logical_args);
        if (cs->extra_args.size() >= 2) {
            if (const auto *sam_proto = dex->GetProtoRef(cs->extra_args[0].index)) {
                lp->sam_sig = sam_proto->descriptor;
            }
            if (const auto *impl_mh = dex->GetMethodHandle(cs->extra_args[1].index)) {
                lp->impl = DecodeMethodHandleFromDex(dex, *impl_mh);
            }
        }
        proxy->runtime_payload = std::move(lp);
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().RegisterObject(proxy)), 'L'};
    }

    if (bsm_cls == "java/lang/invoke/StringConcatFactory") {
        std::string recipe;
        std::vector<DexEncodedValue> const_args;
        if (bmh.member_name == "makeConcatWithConstants" && !cs->extra_args.empty()) {
            recipe = cs->extra_args[0].text;
            for (size_t i = 1; i < cs->extra_args.size(); ++i) {
                const_args.push_back(cs->extra_args[i]);
            }
        } else {
            recipe.assign(logical_args.size(), '\x01');
        }
        std::string str = MakeStringConcatRecipe(vm, recipe, const_args, logical_args,
                                                 cs_proto->param_types, env, out_exception);
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(str)), 'L'};
    }

    return {0, 'V'};
}

DexValue JavaRuntime::InvokePolymorphic(DexVm &vm, jobject mh_obj, const DexProtoRef &call_proto,
                                        std::span<const uint64_t> arg_regs, JNIEnv *env,
                                        jobject &out_exception)
{
    auto syn = JniEnvironment::Instance().LookupObject(mh_obj);
    if (!syn || syn->class_name != "java/lang/invoke/MethodHandle" || !syn->runtime_payload) {
        out_exception = CreateException("java/lang/NullPointerException", "Null MethodHandle");
        return {0, 'V'};
    }
    auto mh = std::static_pointer_cast<MethodHandlePayload>(syn->runtime_payload);
    std::vector<uint64_t> logical = UnpackArgsToLogical(call_proto.param_types, arg_regs);
    return InvokeMethodHandleImpl(vm, *mh, logical, env, out_exception);
}

DexValue JavaRuntime::TryInvokeLambdaProxy(DexVm &vm, jobject proxy_obj, const DexMethodRef &ref,
                                           std::span<const uint64_t> arg_regs, JNIEnv *env,
                                           jobject &out_exception, bool &handled)
{
    handled = false;
    auto syn = JniEnvironment::Instance().LookupObject(proxy_obj);
    if (!syn || syn->class_name != "java/lang/invoke/LambdaProxy" || !syn->runtime_payload) {
        return {0, 'V'};
    }
    auto lp = std::static_pointer_cast<LambdaProxyPayload>(syn->runtime_payload);
    if (ref.name != lp->sam_name) {
        return {0, 'V'};
    }
    std::vector<uint64_t> combined = lp->captured_args;
    if (arg_regs.size() > 1) {
        std::vector<std::string> sam_ptypes =
            !ref.param_types.empty() ? ref.param_types : ParseParamDescriptors(lp->sam_sig);
        std::vector<uint64_t> call_logical = UnpackArgsToLogical(sam_ptypes, arg_regs.subspan(1));
        combined.insert(combined.end(), call_logical.begin(), call_logical.end());
    }
    handled = true;
    return InvokeMethodHandleImpl(vm, lp->impl, combined, env, out_exception);
}

bool JavaRuntime::IsLambdaProxyInstanceOf(jobject obj, std::string_view target_slash) const
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn || syn->class_name != "java/lang/invoke/LambdaProxy" || !syn->runtime_payload) {
        return false;
    }
    auto lp = std::static_pointer_cast<LambdaProxyPayload>(syn->runtime_payload);
    if (lp->interface_slash == target_slash ||
        IsAssignableFrom(lp->interface_slash, target_slash)) {
        return true;
    }
    for (const auto &extra : lp->extra_interfaces) {
        if (extra == target_slash || IsAssignableFrom(extra, target_slash)) {
            return true;
        }
    }
    return false;
}

void RegisterReflectIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/lang/Class", HandleClass);
    rt.RegisterClass("java/lang/ClassLoader", HandleClassLoader);
    rt.RegisterClass("java/lang/reflect/Method", HandleReflectMember);
    rt.RegisterClass("java/lang/reflect/Constructor", HandleReflectMember);
    rt.RegisterClass("java/lang/reflect/Field", HandleReflectMember);
    rt.RegisterClass("java/lang/reflect/AccessibleObject", HandleReflectMember);
    rt.RegisterClass("java/lang/reflect/Modifier", HandleReflectMember);
    rt.RegisterClass("java/lang/Enum", HandleEnum,
                     {"java/lang/Comparable", "java/io/Serializable"});
    rt.RegisterClass("java/lang/invoke/MethodHandles", HandleMethodHandles);
    rt.RegisterClass("java/lang/invoke/MethodHandles$Lookup", HandleMethodHandles);
    rt.RegisterClass("java/lang/invoke/MethodHandle", HandleMethodHandles);

    rt.RegisterStaticField("java/lang/Boolean", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("Z"));
    });
    rt.RegisterStaticField("java/lang/Byte", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("B"));
    });
    rt.RegisterStaticField("java/lang/Short", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("S"));
    });
    rt.RegisterStaticField("java/lang/Character", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("C"));
    });
    rt.RegisterStaticField("java/lang/Integer", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("I"));
    });
    rt.RegisterStaticField("java/lang/Long", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("J"));
    });
    rt.RegisterStaticField("java/lang/Float", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("F"));
    });
    rt.RegisterStaticField("java/lang/Double", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("D"));
    });
    rt.RegisterStaticField("java/lang/Void", "TYPE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JavaRuntime::Instance().GetClassObject("V"));
    });
}

} // namespace portage::package
