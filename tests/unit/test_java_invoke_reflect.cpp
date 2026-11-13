#include "java_runtime_dex_builder.hpp"
#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "package/java_runtime.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace portage::package {
namespace {

using test_util::AppendU16;
using test_util::AppendU32;
using test_util::Emit21c;
using test_util::Emit21s;
using test_util::Emit22c;
using test_util::Emit23x;
using test_util::Emit35c;
using test_util::Emit45cc;
using test_util::Encode10x;
using test_util::Encode11n;
using test_util::Encode11x;
using test_util::Encode12x;
using test_util::RuntimeDexBuilder;

class JavaInvokeReflectTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        JniEnvironment::Instance().Reset();
        DexVm::Instance().Reset();
        JavaRuntime::Instance().Reset();
    }

    void TearDown() override
    {
        PackageManager::Instance().Reset();
        JavaRuntime::Instance().Reset();
        DexVm::Instance().Reset();
        JniEnvironment::Instance().Reset();
    }

    static std::string ReadJavaString(jobject str_obj)
    {
        if (!str_obj) {
            return "";
        }
        if (const char *s =
                JniEnvironment::Instance().GetStringChars(reinterpret_cast<jstring>(str_obj))) {
            return std::string(s);
        }
        auto syn = JniEnvironment::Instance().LookupObject(str_obj);
        return syn ? syn->string_fields["value"] : "";
    }

    static int32_t CallStaticInt(const DexFile &dex, std::string_view cls, std::string_view name,
                                 std::string_view sig, std::span<const uint64_t> args = {})
    {
        const DexMethodDef *m = dex.FindMethod(cls, name, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return -1;
        }
        DexValue ret = DexVm::Instance().ExecuteMethod(&dex, m, args);
        return static_cast<int32_t>(ret.raw);
    }

    static jobject CallStaticObj(const DexFile &dex, std::string_view cls, std::string_view name,
                                 std::string_view sig, std::span<const uint64_t> args = {})
    {
        const DexMethodDef *m = dex.FindMethod(cls, name, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return nullptr;
        }
        DexValue ret = DexVm::Instance().ExecuteMethod(&dex, m, args);
        return reinterpret_cast<jobject>(static_cast<uintptr_t>(ret.raw));
    }

    static std::vector<uint8_t> BuildMinimalManifest(std::string_view pkg_name)
    {
        std::vector<std::string> strings = {"manifest", "package", std::string(pkg_name),
                                            "application"};
        std::vector<uint8_t> sp_strings;
        std::vector<uint32_t> offsets;
        for (const auto &s : strings) {
            offsets.push_back(static_cast<uint32_t>(sp_strings.size()));
            AppendU16(sp_strings, static_cast<uint16_t>(s.size()));
            for (char c : s) {
                AppendU16(sp_strings, static_cast<uint16_t>(static_cast<uint8_t>(c)));
            }
            AppendU16(sp_strings, 0);
        }
        test_util::Align4(sp_strings);
        uint32_t sp_header_size = 28;
        uint32_t offsets_bytes = static_cast<uint32_t>(offsets.size() * 4);
        uint32_t sp_total_size =
            sp_header_size + offsets_bytes + static_cast<uint32_t>(sp_strings.size());
        std::vector<uint8_t> body;
        AppendU16(body, 0x0001);
        AppendU16(body, 0x001C);
        AppendU32(body, sp_total_size);
        AppendU32(body, static_cast<uint32_t>(strings.size()));
        AppendU32(body, 0);
        AppendU32(body, 0);
        AppendU32(body, sp_header_size + offsets_bytes);
        AppendU32(body, 0);
        for (uint32_t off : offsets) {
            AppendU32(body, off);
        }
        body.insert(body.end(), sp_strings.begin(), sp_strings.end());
        AppendU16(body, 0x0102);
        AppendU16(body, 0x0010);
        AppendU32(body, 0x0038);
        AppendU32(body, 1);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0);
        AppendU16(body, 0x0014);
        AppendU16(body, 0x0014);
        AppendU16(body, 1);
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 1);
        AppendU32(body, 2);
        AppendU32(body, 2);
        AppendU16(body, 0x0008);
        AppendU16(body, 0x0300);
        AppendU32(body, 2);
        AppendU16(body, 0x0102);
        AppendU16(body, 0x0010);
        AppendU32(body, 0x0024);
        AppendU32(body, 2);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 3);
        AppendU16(body, 0x0014);
        AppendU16(body, 0x0014);
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0x0103);
        AppendU16(body, 0x0010);
        AppendU32(body, 0x0018);
        AppendU32(body, 2);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 3);
        AppendU16(body, 0x0103);
        AppendU16(body, 0x0010);
        AppendU32(body, 0x0018);
        AppendU32(body, 3);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0);
        std::vector<uint8_t> doc;
        AppendU16(doc, 0x0003);
        AppendU16(doc, 0x0008);
        AppendU32(doc, static_cast<uint32_t>(8 + body.size()));
        doc.insert(doc.end(), body.begin(), body.end());
        return doc;
    }

    static std::vector<uint8_t>
    BuildMinimalZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &entries)
    {
        std::vector<uint8_t> out;
        struct CdEntry {
            std::string name;
            uint32_t crc;
            uint32_t size;
            uint32_t lfh_off;
        };
        std::vector<CdEntry> cd;
        for (const auto &[name, data] : entries) {
            uint32_t crc = 0;
            if (!data.empty()) {
                crc = crc32(0L, Z_NULL, 0);
                crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
            }
            uint32_t lfh_off = static_cast<uint32_t>(out.size());
            AppendU32(out, 0x04034B50u);
            AppendU16(out, 20);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU32(out, crc);
            AppendU32(out, static_cast<uint32_t>(data.size()));
            AppendU32(out, static_cast<uint32_t>(data.size()));
            AppendU16(out, static_cast<uint16_t>(name.size()));
            AppendU16(out, 0);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            cd.push_back({name, crc, static_cast<uint32_t>(data.size()), lfh_off});
        }
        uint32_t cd_off = static_cast<uint32_t>(out.size());
        for (const auto &e : cd) {
            AppendU32(out, 0x02014B50u);
            AppendU16(out, 20);
            AppendU16(out, 20);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU32(out, e.crc);
            AppendU32(out, e.size);
            AppendU32(out, e.size);
            AppendU16(out, static_cast<uint16_t>(e.name.size()));
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU16(out, 0);
            AppendU32(out, 0);
            AppendU32(out, e.lfh_off);
            out.insert(out.end(), e.name.begin(), e.name.end());
        }
        uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_off;
        AppendU32(out, 0x06054B50u);
        AppendU16(out, 0);
        AppendU16(out, 0);
        AppendU16(out, static_cast<uint16_t>(cd.size()));
        AppendU16(out, static_cast<uint16_t>(cd.size()));
        AppendU32(out, cd_size);
        AppendU32(out, cd_off);
        AppendU16(out, 0);
        return out;
    }
};

TEST_F(JavaInvokeReflectTest, StaticValuesPreClinitConstants)
{
    RuntimeDexBuilder db;
    uint32_t f_num = db.AddField("LConstCls;", "NUM", "I");
    uint32_t f_flag = db.AddField("LConstCls;", "FLAG", "Z");
    uint32_t f_label = db.AddField("LConstCls;", "LABEL", "Ljava/lang/String;");
    uint32_t s_hello = db.AddString("portage-v039");

    auto &cls = db.AddClass("LConstCls;", "Ljava/lang/Object;");
    cls.static_fields.push_back({f_num, ACC_PUBLIC | ACC_STATIC | ACC_FINAL});
    cls.static_fields.push_back({f_flag, ACC_PUBLIC | ACC_STATIC | ACC_FINAL});
    cls.static_fields.push_back({f_label, ACC_PUBLIC | ACC_STATIC | ACC_FINAL});

    std::sort(cls.static_fields.begin(), cls.static_fields.end(),
              [](const auto &a, const auto &b) { return a.field_idx < b.field_idx; });
    for (const auto &sf : cls.static_fields) {
        if (sf.field_idx == f_num) {
            cls.static_values.push_back(
                {RuntimeDexBuilder::EncodedValueSpec::Kind::Int, static_cast<uint64_t>(4242)});
        } else if (sf.field_idx == f_flag) {
            cls.static_values.push_back({RuntimeDexBuilder::EncodedValueSpec::Kind::Boolean, 1});
        } else if (sf.field_idx == f_label) {
            cls.static_values.push_back(
                {RuntimeDexBuilder::EncodedValueSpec::Kind::String, s_hello});
        }
    }

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(static_cast<int32_t>(DexVm::Instance().GetStaticField("LConstCls;", "NUM")), 4242);
    EXPECT_EQ(DexVm::Instance().GetStaticField("LConstCls;", "FLAG"), 1ULL);
    EXPECT_EQ(ReadJavaString(reinterpret_cast<jobject>(
                  static_cast<uintptr_t>(DexVm::Instance().GetStaticField("LConstCls;", "LABEL")))),
              "portage-v039");
}

TEST_F(JavaInvokeReflectTest, ConstClassIdentityAndReflectionInvocation)
{
    RuntimeDexBuilder db;
    uint32_t t_cls = db.AddType("LReflTarget;");
    uint32_t t_obj = db.AddType("Ljava/lang/Object;");
    uint32_t t_class = db.AddType("Ljava/lang/Class;");
    uint32_t t_method = db.AddType("Ljava/lang/reflect/Method;");
    uint32_t t_field = db.AddType("Ljava/lang/reflect/Field;");
    uint32_t t_ctor = db.AddType("Ljava/lang/reflect/Constructor;");
    uint32_t t_int_obj = db.AddType("Ljava/lang/Integer;");
    uint32_t t_obj_arr = db.AddType("[Ljava/lang/Object;");
    uint32_t t_cls_arr = db.AddType("[Ljava/lang/Class;");

    uint32_t f_val = db.AddField("LReflTarget;", "val", "I");
    uint32_t m_init = db.AddMethod("LReflTarget;", "<init>", "V", {"I"});
    uint32_t m_add = db.AddMethod("LReflTarget;", "add", "I", {"I"});
    uint32_t m_get_class = db.AddMethod("Ljava/lang/Object;", "getClass", "Ljava/lang/Class;", {});
    uint32_t m_for_name =
        db.AddMethod("Ljava/lang/Class;", "forName", "Ljava/lang/Class;", {"Ljava/lang/String;"});
    uint32_t m_get_decl_ctor =
        db.AddMethod("Ljava/lang/Class;", "getDeclaredConstructor",
                     "Ljava/lang/reflect/Constructor;", {"[Ljava/lang/Class;"});
    uint32_t m_new_inst = db.AddMethod("Ljava/lang/reflect/Constructor;", "newInstance",
                                       "Ljava/lang/Object;", {"[Ljava/lang/Object;"});
    uint32_t m_get_decl_method =
        db.AddMethod("Ljava/lang/Class;", "getDeclaredMethod", "Ljava/lang/reflect/Method;",
                     {"Ljava/lang/String;", "[Ljava/lang/Class;"});
    uint32_t m_invoke = db.AddMethod("Ljava/lang/reflect/Method;", "invoke", "Ljava/lang/Object;",
                                     {"Ljava/lang/Object;", "[Ljava/lang/Object;"});
    uint32_t m_get_decl_field = db.AddMethod("Ljava/lang/Class;", "getDeclaredField",
                                             "Ljava/lang/reflect/Field;", {"Ljava/lang/String;"});
    uint32_t m_field_set_int =
        db.AddMethod("Ljava/lang/reflect/Field;", "setInt", "V", {"Ljava/lang/Object;", "I"});
    uint32_t m_field_get_int =
        db.AddMethod("Ljava/lang/reflect/Field;", "getInt", "I", {"Ljava/lang/Object;"});
    uint32_t m_int_valof =
        db.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    uint32_t m_int_val = db.AddMethod("Ljava/lang/Integer;", "intValue", "I", {});

    uint32_t s_cls_name = db.AddString("ReflTarget");
    uint32_t s_add = db.AddString("add");
    uint32_t s_val = db.AddString("val");
    uint32_t f_int_type = db.AddField("Ljava/lang/Integer;", "TYPE", "Ljava/lang/Class;");

    auto &cls = db.AddClass("LReflTarget;", "Ljava/lang/Object;");
    cls.instance_fields.push_back({f_val, ACC_PUBLIC});

    RuntimeDexBuilder::MethodSpec ctor_fn;
    ctor_fn.class_idx = cls.class_idx;
    ctor_fn.method_idx = m_init;
    ctor_fn.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
    ctor_fn.registers_size = 2;
    ctor_fn.ins_size = 2;
    ctor_fn.insns.push_back(Encode11n(OP_CONST_4, 0, 0));
    ctor_fn.insns.push_back(0);
    // v0=this, v1=arg0 -> iput v1, v0, f_val
    ctor_fn.insns.clear();
    Emit22c(ctor_fn.insns, OP_IPUT, 1, 0, static_cast<uint16_t>(f_val));
    ctor_fn.insns.push_back(Encode10x(OP_RETURN_VOID));
    cls.direct_methods.push_back(std::move(ctor_fn));

    RuntimeDexBuilder::MethodSpec add_fn;
    add_fn.class_idx = cls.class_idx;
    add_fn.method_idx = m_add;
    add_fn.access_flags = ACC_PUBLIC;
    add_fn.registers_size = 3;
    add_fn.ins_size = 2;
    // v1=this, v2=delta -> iget v0, v1, f_val; add-int v0, v0, v2; return v0
    Emit22c(add_fn.insns, OP_IGET, 0, 1, static_cast<uint16_t>(f_val));
    Emit23x(add_fn.insns, OP_ADD_INT, 0, 0, 2);
    add_fn.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.virtual_methods.push_back(std::move(add_fn));

    RuntimeDexBuilder::MethodSpec test_fn;
    test_fn.class_idx = cls.class_idx;
    test_fn.method_idx = db.AddMethod("LReflTarget;", "runTest", "I", {});
    test_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    test_fn.registers_size = 8;
    // v0 = const-class LReflTarget;
    Emit21c(test_fn.insns, OP_CONST_CLASS, 0, static_cast<uint16_t>(t_cls));
    // v1 = Class.forName("ReflTarget")
    Emit21c(test_fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_cls_name));
    Emit35c(test_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_for_name), {2});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    // if (v0 != v1) return -10
    uint32_t br_idx = static_cast<uint32_t>(test_fn.insns.size());
    test_fn.insns.push_back(0);
    test_fn.insns.push_back(0);
    // v2 = new Class[]{Integer.TYPE}
    test_fn.insns.push_back(Encode11n(OP_CONST_4, 3, 1));
    Emit22c(test_fn.insns, OP_NEW_ARRAY, 2, 3, static_cast<uint16_t>(t_cls_arr));
    Emit21c(test_fn.insns, OP_SGET_OBJECT, 4, static_cast<uint16_t>(f_int_type));
    test_fn.insns.push_back(Encode11n(OP_CONST_4, 5, 0));
    Emit23x(test_fn.insns, OP_APUT_OBJECT, 4, 2, 5);
    // v3 = v0.getDeclaredConstructor(v2)
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_decl_ctor), {0, 2});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    // v4 = new Object[]{Integer.valueOf(10)}
    test_fn.insns.push_back(Encode11n(OP_CONST_4, 5, 1));
    Emit22c(test_fn.insns, OP_NEW_ARRAY, 4, 5, static_cast<uint16_t>(t_obj_arr));
    Emit21s(test_fn.insns, OP_CONST_16, 6, 10);
    Emit35c(test_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valof), {6});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 6));
    test_fn.insns.push_back(Encode11n(OP_CONST_4, 5, 0));
    Emit23x(test_fn.insns, OP_APUT_OBJECT, 6, 4, 5);
    // v3 = ctor.newInstance(v4) -> ReflTarget(10)
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_new_inst), {3, 4});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    // Check instance.getClass() == v0
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_class), {3});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 5));
    uint32_t br2_idx = static_cast<uint32_t>(test_fn.insns.size());
    test_fn.insns.push_back(0);
    test_fn.insns.push_back(0);
    // field = v0.getDeclaredField("val"); field.setInt(v3, 25)
    Emit21c(test_fn.insns, OP_CONST_STRING, 5, static_cast<uint16_t>(s_val));
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_decl_field), {0, 5});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 5));
    Emit21s(test_fn.insns, OP_CONST_16, 6, 25);
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_field_set_int), {5, 3, 6});
    // method = v0.getDeclaredMethod("add", v2)
    Emit21c(test_fn.insns, OP_CONST_STRING, 6, static_cast<uint16_t>(s_add));
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_decl_method), {0, 6, 2});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 6));
    // res = (Integer)method.invoke(v3, v4) -> 25 + 10 = 35
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_invoke), {6, 3, 4});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 6));
    Emit21c(test_fn.insns, OP_CHECK_CAST, 6, static_cast<uint16_t>(t_int_obj));
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_int_val), {6});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 7));
    // Also read field.getInt(v3) -> 25; return 35 + 25 = 60
    Emit35c(test_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_field_get_int), {5, 3});
    test_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 6));
    Emit23x(test_fn.insns, OP_ADD_INT, 7, 7, 6);
    test_fn.insns.push_back(Encode11x(OP_RETURN, 7));

    uint32_t fail_off = static_cast<uint32_t>(test_fn.insns.size());
    Emit21s(test_fn.insns, OP_CONST_16, 7, -10);
    test_fn.insns.push_back(Encode11x(OP_RETURN, 7));

    test_fn.insns[br_idx] = static_cast<uint16_t>(OP_IF_NE | (0 << 8) | (1 << 12));
    test_fn.insns[br_idx + 1] = static_cast<uint16_t>(static_cast<int16_t>(fail_off - br_idx));
    test_fn.insns[br2_idx] = static_cast<uint16_t>(OP_IF_NE | (0 << 8) | (5 << 12));
    test_fn.insns[br2_idx + 1] = static_cast<uint16_t>(static_cast<int16_t>(fail_off - br2_idx));

    cls.direct_methods.push_back(std::move(test_fn));
    (void)t_obj;
    (void)t_class;
    (void)t_method;
    (void)t_field;
    (void)t_ctor;

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LReflTarget;", "runTest", "()I"), 60);
}

TEST_F(JavaInvokeReflectTest, EnumOperationsAndGetResourceAsStreamFromApk)
{
    RuntimeDexBuilder db;
    uint32_t t_enum_cls = db.AddType("LMyColor;");
    uint32_t m_enum_init =
        db.AddMethod("Ljava/lang/Enum;", "<init>", "V", {"Ljava/lang/String;", "I"});
    uint32_t m_enum_name = db.AddMethod("Ljava/lang/Enum;", "name", "Ljava/lang/String;", {});
    uint32_t m_enum_ord = db.AddMethod("Ljava/lang/Enum;", "ordinal", "I", {});
    uint32_t m_get_res = db.AddMethod("Ljava/lang/Class;", "getResourceAsStream",
                                      "Ljava/io/InputStream;", {"Ljava/lang/String;"});
    uint32_t m_read = db.AddMethod("Ljava/io/InputStream;", "read", "I", {"[B"});
    uint32_t m_str_bytes = db.AddMethod("Ljava/lang/String;", "<init>", "V", {"[B", "I", "I"});
    uint32_t m_sb_init = db.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", {});
    uint32_t m_sb_app_str = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                         "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});
    uint32_t m_sb_app_int =
        db.AddMethod("Ljava/lang/StringBuilder;", "append", "Ljava/lang/StringBuilder;", {"I"});
    uint32_t m_sb_str =
        db.AddMethod("Ljava/lang/StringBuilder;", "toString", "Ljava/lang/String;", {});

    uint32_t t_sb = db.AddType("Ljava/lang/StringBuilder;");
    uint32_t t_str = db.AddType("Ljava/lang/String;");
    uint32_t t_barr = db.AddType("[B");
    uint32_t s_red = db.AddString("RED");
    uint32_t s_res_path = db.AddString("/assets/hello.txt");
    uint32_t s_colon = db.AddString(":");

    auto &cls = db.AddClass("LMyColor;", "Ljava/lang/Enum;");
    cls.access_flags = ACC_PUBLIC | ACC_FINAL | ACC_ENUM;

    RuntimeDexBuilder::MethodSpec fn;
    fn.class_idx = cls.class_idx;
    fn.method_idx = db.AddMethod("LMyColor;", "probe", "Ljava/lang/String;", {});
    fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    fn.registers_size = 7;

    Emit21c(fn.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_enum_cls));
    Emit21c(fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_red));
    Emit21s(fn.insns, OP_CONST_16, 2, 2);
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_enum_init), {0, 1, 2});

    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_enum_name), {0});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_enum_ord), {0});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 2));

    Emit21c(fn.insns, OP_CONST_CLASS, 3, static_cast<uint16_t>(t_enum_cls));
    Emit21c(fn.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_res_path));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_res), {3, 4});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    Emit21s(fn.insns, OP_CONST_16, 4, 32);
    Emit22c(fn.insns, OP_NEW_ARRAY, 5, 4, static_cast<uint16_t>(t_barr));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_read), {3, 5});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 4));
    Emit21c(fn.insns, OP_NEW_INSTANCE, 3, static_cast<uint16_t>(t_str));
    fn.insns.push_back(Encode11n(OP_CONST_4, 6, 0));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_str_bytes), {3, 5, 6, 4});

    Emit21c(fn.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_sb));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_sb_init), {0});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_app_str), {0, 1});
    Emit21c(fn.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_colon));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_app_str), {0, 4});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_app_int), {0, 2});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_app_str), {0, 4});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_app_str), {0, 3});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_str), {0});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(fn));

    auto dex_bytes = db.Build();
    const std::string payload = "from-apk-zip";
    std::vector<uint8_t> res_bytes(payload.begin(), payload.end());
    auto apk_bytes =
        BuildMinimalZip({{"AndroidManifest.xml", BuildMinimalManifest("com.portage.reflect")},
                         {"classes.dex", dex_bytes},
                         {"assets/hello.txt", res_bytes}});

    auto temp_dir = std::filesystem::temp_directory_path() / "portage_reflect_res_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    auto apk_path = temp_dir / "test.apk";
    {
        std::ofstream ofs(apk_path, std::ios::binary | std::ios::trunc);
        ofs.write(reinterpret_cast<const char *>(apk_bytes.data()),
                  static_cast<std::streamsize>(apk_bytes.size()));
    }

    ASSERT_TRUE(PackageManager::Instance().Initialize(temp_dir / "data"));
    auto pkg = PackageManager::Instance().InstallPackage(apk_path);
    ASSERT_TRUE(pkg.has_value());

    auto dex = DexFile::Parse(dex_bytes);
    ASSERT_NE(dex, nullptr);
    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LMyColor;", "probe", "()Ljava/lang/String;")),
              "RED:2:from-apk-zip");

    PackageManager::Instance().Reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}

TEST_F(JavaInvokeReflectTest, InvokeCustomLambdaMetafactoryAndStringConcatFactory)
{
    RuntimeDexBuilder db;
    db.SetDexVersion("039");

    uint32_t m_lmf = db.AddMethod(
        "Ljava/lang/invoke/LambdaMetafactory;", "metafactory", "Ljava/lang/invoke/CallSite;",
        {"Ljava/lang/invoke/MethodHandles$Lookup;", "Ljava/lang/String;",
         "Ljava/lang/invoke/MethodType;", "Ljava/lang/invoke/MethodType;",
         "Ljava/lang/invoke/MethodHandle;", "Ljava/lang/invoke/MethodType;"});
    uint32_t m_scf = db.AddMethod("Ljava/lang/invoke/StringConcatFactory;",
                                  "makeConcatWithConstants", "Ljava/lang/invoke/CallSite;",
                                  {"Ljava/lang/invoke/MethodHandles$Lookup;", "Ljava/lang/String;",
                                   "Ljava/lang/invoke/MethodType;", "Ljava/lang/String;",
                                   "[Ljava/lang/Object;"});
    uint32_t m_target = db.AddMethod("LD8Demo;", "lambdaImpl", "I", {"I", "I"});
    uint32_t m_apply =
        db.AddMethod("Ljava/util/function/IntUnaryOperator;", "applyAsInt", "I", {"I"});
    uint32_t m_sb_app = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                     "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});

    uint32_t p_sam = db.AddProto("I", {"I"});
    uint32_t p_factory = db.AddProto("Ljava/util/function/IntUnaryOperator;", {"I"});
    uint32_t p_concat = db.AddProto("Ljava/lang/String;", {"I", "Ljava/lang/String;"});

    uint32_t mh_lmf =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_lmf));
    uint32_t mh_scf =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_scf));
    uint32_t mh_impl =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_target));

    uint32_t s_apply = db.AddString("applyAsInt");
    uint32_t s_make = db.AddString("makeConcatWithConstants");
    uint32_t s_recipe = db.AddString("sum=\x01, tag=\x01");
    uint32_t s_ok = db.AddString("ok");

    uint32_t cs_lambda = db.AddCallSite({
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodHandle, mh_lmf},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::String, s_apply},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodType, p_factory},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodType, p_sam},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodHandle, mh_impl},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodType, p_sam},
    });

    uint32_t cs_concat = db.AddCallSite({
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodHandle, mh_scf},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::String, s_make},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::MethodType, p_concat},
        {RuntimeDexBuilder::EncodedValueSpec::Kind::String, s_recipe},
    });

    auto &cls = db.AddClass("LD8Demo;", "Ljava/lang/Object;");

    RuntimeDexBuilder::MethodSpec impl_fn;
    impl_fn.class_idx = cls.class_idx;
    impl_fn.method_idx = m_target;
    impl_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    impl_fn.registers_size = 3;
    impl_fn.ins_size = 2;
    // v1=captured, v2=arg -> return v1 * 2 + v2
    Emit23x(impl_fn.insns, OP_ADD_INT, 0, 1, 1);
    Emit23x(impl_fn.insns, OP_ADD_INT, 0, 0, 2);
    impl_fn.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.direct_methods.push_back(std::move(impl_fn));

    RuntimeDexBuilder::MethodSpec run_fn;
    run_fn.class_idx = cls.class_idx;
    run_fn.method_idx = db.AddMethod("LD8Demo;", "run", "Ljava/lang/String;", {});
    run_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    run_fn.registers_size = 4;
    // v0 = 10 (captured); v1 = invoke-custom cs_lambda(v0) -> IntUnaryOperator
    Emit21s(run_fn.insns, OP_CONST_16, 0, 10);
    Emit35c(run_fn.insns, OP_INVOKE_CUSTOM, static_cast<uint16_t>(cs_lambda), {0});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    // v2 = v1.applyAsInt(7) -> 10*2 + 7 = 27
    Emit21s(run_fn.insns, OP_CONST_16, 2, 7);
    Emit35c(run_fn.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_apply), {1, 2});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 2));
    // v3 = "ok"; v0 = invoke-custom cs_concat(v2, v3) -> "sum=27, tag=ok"
    Emit21c(run_fn.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_ok));
    Emit35c(run_fn.insns, OP_INVOKE_CUSTOM, static_cast<uint16_t>(cs_concat), {2, 3});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    run_fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(run_fn));
    (void)m_sb_app;

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    EXPECT_NE(dex->GetCallSite(0), nullptr);
    EXPECT_NE(dex->GetMethodHandle(0), nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LD8Demo;", "run", "()Ljava/lang/String;")),
              "sum=27, tag=ok");
}

TEST_F(JavaInvokeReflectTest, ConstMethodHandleTypeAndInvokePolymorphic)
{
    RuntimeDexBuilder db;
    db.SetDexVersion("039");

    uint32_t m_mul = db.AddMethod("LPolyDemo;", "mulAdd", "I", {"I", "I"});
    uint32_t m_mh_invoke = db.AddMethod("Ljava/lang/invoke/MethodHandle;", "invoke",
                                        "Ljava/lang/Object;", {"[Ljava/lang/Object;"});
    uint32_t m_mt_ret =
        db.AddMethod("Ljava/lang/invoke/MethodType;", "returnType", "Ljava/lang/Class;", {});
    uint32_t m_cls_name = db.AddMethod("Ljava/lang/Class;", "getName", "Ljava/lang/String;", {});

    uint32_t p_call = db.AddProto("I", {"I", "I"});
    uint32_t mh_mul =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_mul));

    auto &cls = db.AddClass("LPolyDemo;", "Ljava/lang/Object;");

    RuntimeDexBuilder::MethodSpec mul_fn;
    mul_fn.class_idx = cls.class_idx;
    mul_fn.method_idx = m_mul;
    mul_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    mul_fn.registers_size = 3;
    mul_fn.ins_size = 2;
    Emit23x(mul_fn.insns, OP_MUL_INT, 0, 1, 2);
    Emit21s(mul_fn.insns, OP_CONST_16, 1, 5);
    Emit23x(mul_fn.insns, OP_ADD_INT, 0, 0, 1);
    mul_fn.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.direct_methods.push_back(std::move(mul_fn));

    RuntimeDexBuilder::MethodSpec run_fn;
    run_fn.class_idx = cls.class_idx;
    run_fn.method_idx = db.AddMethod("LPolyDemo;", "run", "I", {});
    run_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    run_fn.registers_size = 4;
    // v0 = const-method-handle mh_mul
    Emit21c(run_fn.insns, OP_CONST_METHOD_HANDLE, 0, static_cast<uint16_t>(mh_mul));
    // v1 = const-method-type p_call (verify it doesn't crash & returns non-null)
    Emit21c(run_fn.insns, OP_CONST_METHOD_TYPE, 1, static_cast<uint16_t>(p_call));
    Emit35c(run_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_mt_ret), {1});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(run_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_cls_name), {1});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    // v2 = 6, v3 = 7 -> invoke-polymorphic {v0, v2, v3}, m_mh_invoke, p_call -> 6*7 + 5 = 47
    Emit21s(run_fn.insns, OP_CONST_16, 2, 6);
    Emit21s(run_fn.insns, OP_CONST_16, 3, 7);
    Emit45cc(run_fn.insns, OP_INVOKE_POLYMORPHIC, static_cast<uint16_t>(m_mh_invoke),
             static_cast<uint16_t>(p_call), {0, 2, 3});
    run_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));
    run_fn.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.direct_methods.push_back(std::move(run_fn));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LPolyDemo;", "run", "()I"), 47);
}

} // namespace
} // namespace portage::package
