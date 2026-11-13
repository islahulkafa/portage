#include "java_runtime_dex_builder.hpp"
#include "package/dex_vm.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>

namespace portage::package {
namespace {

using namespace test_util;

class JavaStringTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        JniEnvironment::Instance().Initialize("com.test.strings", ".", ".");
        DexVm::Instance().Reset();
    }

    static jobject CallStaticObj(const DexFile &dex, const std::string &cls,
                                 const std::string &method, const std::string &sig,
                                 std::vector<uint64_t> args = {})
    {
        const auto *m = dex.FindMethod(cls, method, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return nullptr;
        }
        DexValue v = DexVm::Instance().ExecuteMethod(&dex, m, args);
        return reinterpret_cast<jobject>(v.raw);
    }

    static int32_t CallStaticInt(const DexFile &dex, const std::string &cls,
                                 const std::string &method, const std::string &sig,
                                 std::vector<uint64_t> args = {})
    {
        const auto *m = dex.FindMethod(cls, method, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return -1;
        }
        DexValue v = DexVm::Instance().ExecuteMethod(&dex, m, args);
        return static_cast<int32_t>(v.raw);
    }

    static std::string ReadJavaString(jobject obj)
    {
        const char *s = JniEnvironment::Instance().GetStringChars(reinterpret_cast<jstring>(obj));
        return s ? std::string(s) : std::string();
    }
};

TEST_F(JavaStringTest, Utf16LengthCharAtAndSurrogates)
{
    RuntimeDexBuilder db;
    uint32_t s_emoji = db.AddString("\xF0\x9F\x98\x80");
    uint32_t s_cafe = db.AddString("caf\xC3\xA9");
    uint32_t m_len = db.AddMethod("Ljava/lang/String;", "length", "I", {});
    uint32_t m_charAt = db.AddMethod("Ljava/lang/String;", "charAt", "C", {"I"});
    uint32_t m_codePointAt = db.AddMethod("Ljava/lang/String;", "codePointAt", "I", {"I"});
    uint32_t m_substr2 =
        db.AddMethod("Ljava/lang/String;", "substring", "Ljava/lang/String;", {"I", "I"});

    auto &cls = db.AddClass("LTestStr;");
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = cls.class_idx;
    ms.method_idx = db.AddMethod("LTestStr;", "run", "I", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 5;
    ms.ins_size = 0;

    Emit21c(ms.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_emoji));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_len), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));

    Emit21s(ms.insns, OP_CONST_16, 2, 0);
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_charAt), {0, 2});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));

    Emit21s(ms.insns, OP_CONST_16, 2, 1);
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_charAt), {0, 2});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 4));

    // Return len*100000 + ... wait: check codePointAt(0)
    Emit21s(ms.insns, OP_CONST_16, 2, 0);
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_codePointAt), {0, 2});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));

    // Validate charAt(0) and charAt(1) by returning len == 2 && c0 == 0xD83D && c1 == 0xDE00 &&
    // cp == 0x1F600 ? 1 : 0 via arithmetic XOR sum? Easier: separate helper methods!
    ms.insns.push_back(Encode11x(OP_RETURN, 0));

    RuntimeDexBuilder::MethodSpec m_cafe_len;
    m_cafe_len.class_idx = cls.class_idx;
    m_cafe_len.method_idx = db.AddMethod("LTestStr;", "cafeLen", "I", {});
    m_cafe_len.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_cafe_len.registers_size = 2;
    Emit21c(m_cafe_len.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_cafe));
    Emit35c(m_cafe_len.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_len), {0});
    m_cafe_len.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));
    m_cafe_len.insns.push_back(Encode11x(OP_RETURN, 1));

    RuntimeDexBuilder::MethodSpec m_cafe_sub;
    m_cafe_sub.class_idx = cls.class_idx;
    m_cafe_sub.method_idx = db.AddMethod("LTestStr;", "cafeSub", "Ljava/lang/String;", {});
    m_cafe_sub.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_cafe_sub.registers_size = 3;
    Emit21c(m_cafe_sub.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_cafe));
    Emit21s(m_cafe_sub.insns, OP_CONST_16, 1, 2);
    Emit21s(m_cafe_sub.insns, OP_CONST_16, 2, 4);
    Emit35c(m_cafe_sub.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_substr2), {0, 1, 2});
    m_cafe_sub.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    m_cafe_sub.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));

    RuntimeDexBuilder::MethodSpec m_emoji_len;
    m_emoji_len.class_idx = cls.class_idx;
    m_emoji_len.method_idx = db.AddMethod("LTestStr;", "emojiLen", "I", {});
    m_emoji_len.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_emoji_len.registers_size = 2;
    Emit21c(m_emoji_len.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_emoji));
    Emit35c(m_emoji_len.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_len), {0});
    m_emoji_len.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));
    m_emoji_len.insns.push_back(Encode11x(OP_RETURN, 1));

    RuntimeDexBuilder::MethodSpec m_emoji_hi;
    m_emoji_hi.class_idx = cls.class_idx;
    m_emoji_hi.method_idx = db.AddMethod("LTestStr;", "emojiHi", "I", {});
    m_emoji_hi.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_emoji_hi.registers_size = 2;
    Emit21c(m_emoji_hi.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_emoji));
    Emit21s(m_emoji_hi.insns, OP_CONST_16, 1, 0);
    Emit35c(m_emoji_hi.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_charAt), {0, 1});
    m_emoji_hi.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));
    m_emoji_hi.insns.push_back(Encode11x(OP_RETURN, 1));

    RuntimeDexBuilder::MethodSpec m_emoji_lo;
    m_emoji_lo.class_idx = cls.class_idx;
    m_emoji_lo.method_idx = db.AddMethod("LTestStr;", "emojiLo", "I", {});
    m_emoji_lo.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_emoji_lo.registers_size = 2;
    Emit21c(m_emoji_lo.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_emoji));
    Emit21s(m_emoji_lo.insns, OP_CONST_16, 1, 1);
    Emit35c(m_emoji_lo.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_charAt), {0, 1});
    m_emoji_lo.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));
    m_emoji_lo.insns.push_back(Encode11x(OP_RETURN, 1));

    cls.direct_methods.push_back(std::move(ms));
    cls.direct_methods.push_back(std::move(m_cafe_len));
    cls.direct_methods.push_back(std::move(m_cafe_sub));
    cls.direct_methods.push_back(std::move(m_emoji_len));
    cls.direct_methods.push_back(std::move(m_emoji_hi));
    cls.direct_methods.push_back(std::move(m_emoji_lo));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LTestStr;", "run", "()I"), 0x1F600);
    EXPECT_EQ(CallStaticInt(*dex, "LTestStr;", "emojiLen", "()I"), 2);
    EXPECT_EQ(CallStaticInt(*dex, "LTestStr;", "emojiHi", "()I"), 0xD83D);
    EXPECT_EQ(CallStaticInt(*dex, "LTestStr;", "emojiLo", "()I"), 0xDE00);
    EXPECT_EQ(CallStaticInt(*dex, "LTestStr;", "cafeLen", "()I"), 4);
    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestStr;", "cafeSub", "()Ljava/lang/String;")),
              "f\xC3\xA9");
}

TEST_F(JavaStringTest, SearchHashCompareEqualsAndStartsEnds)
{
    RuntimeDexBuilder db;
    uint32_t s_hay = db.AddString("bananas");
    uint32_t s_na = db.AddString("na");
    uint32_t s_ba = db.AddString("ba");
    uint32_t s_BAN = db.AddString("BANANAS");
    uint32_t s_Aa = db.AddString("Aa");
    uint32_t s_BB = db.AddString("BB");

    uint32_t m_idxStrFrom =
        db.AddMethod("Ljava/lang/String;", "indexOf", "I", {"Ljava/lang/String;", "I"});
    uint32_t m_lastIdx =
        db.AddMethod("Ljava/lang/String;", "lastIndexOf", "I", {"Ljava/lang/String;"});
    uint32_t m_contains =
        db.AddMethod("Ljava/lang/String;", "contains", "Z", {"Ljava/lang/CharSequence;"});
    uint32_t m_starts2 =
        db.AddMethod("Ljava/lang/String;", "startsWith", "Z", {"Ljava/lang/String;", "I"});
    uint32_t m_ends = db.AddMethod("Ljava/lang/String;", "endsWith", "Z", {"Ljava/lang/String;"});
    uint32_t m_eqIc =
        db.AddMethod("Ljava/lang/String;", "equalsIgnoreCase", "Z", {"Ljava/lang/String;"});
    uint32_t m_cmp = db.AddMethod("Ljava/lang/String;", "compareTo", "I", {"Ljava/lang/String;"});
    uint32_t m_hash = db.AddMethod("Ljava/lang/String;", "hashCode", "I", {});

    auto &cls = db.AddClass("LTestSearch;");

    auto add_int_fn = [&](const std::string &name, auto emit_body) {
        RuntimeDexBuilder::MethodSpec ms;
        ms.class_idx = cls.class_idx;
        ms.method_idx = db.AddMethod("LTestSearch;", name, "I", {});
        ms.access_flags = ACC_PUBLIC | ACC_STATIC;
        ms.registers_size = 4;
        emit_body(ms.insns);
        ms.insns.push_back(Encode11x(OP_RETURN, 0));
        cls.direct_methods.push_back(std::move(ms));
    };

    add_int_fn("idxFrom", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_na));
        Emit21s(ins, OP_CONST_16, 2, 3);
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_idxStrFrom), {0, 1, 2});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("lastIdx", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_na));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_lastIdx), {0, 1});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("hasNa", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_na));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_contains), {0, 1});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("startsNaAt2", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_na));
        Emit21s(ins, OP_CONST_16, 2, 2);
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_starts2), {0, 1, 2});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("endsBa", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_ba));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ends), {0, 1});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("eqIc", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hay));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_BAN));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_eqIc), {0, 1});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("cmpAaBB", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_Aa));
        Emit21c(ins, OP_CONST_STRING, 1, static_cast<uint16_t>(s_BB));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_cmp), {0, 1});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("hashAa", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_Aa));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_hash), {0});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });
    add_int_fn("hashBB", [&](std::vector<uint16_t> &ins) {
        Emit21c(ins, OP_CONST_STRING, 0, static_cast<uint16_t>(s_BB));
        Emit35c(ins, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_hash), {0});
        ins.push_back(Encode11x(OP_MOVE_RESULT, 0));
    });

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "idxFrom", "()I"), 4);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "lastIdx", "()I"), 4);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "hasNa", "()I"), 1);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "startsNaAt2", "()I"), 1);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "endsBa", "()I"), 0);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "eqIc", "()I"), 1);
    EXPECT_LT(CallStaticInt(*dex, "LTestSearch;", "cmpAaBB", "()I"), 0);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "hashAa", "()I"), 2112);
    EXPECT_EQ(CallStaticInt(*dex, "LTestSearch;", "hashBB", "()I"), 2112);
}

TEST_F(JavaStringTest, TrimReplaceSplitJoinAndFormat)
{
    RuntimeDexBuilder db;
    uint32_t s_raw = db.AddString("  a,b,c  ");
    uint32_t s_comma = db.AddString(",");
    uint32_t s_bar = db.AddString("|");
    uint32_t s_dash = db.AddString("-");
    uint32_t s_fmt = db.AddString("%s:%04d:%x:%X:%.2f:%c:%b%%");
    uint32_t s_hi = db.AddString("hi");

    uint32_t t_obj_arr = db.AddType("[Ljava/lang/Object;");
    uint32_t m_trim = db.AddMethod("Ljava/lang/String;", "trim", "Ljava/lang/String;", {});
    uint32_t m_rep = db.AddMethod("Ljava/lang/String;", "replace", "Ljava/lang/String;",
                                  {"Ljava/lang/CharSequence;", "Ljava/lang/CharSequence;"});
    uint32_t m_split =
        db.AddMethod("Ljava/lang/String;", "split", "[Ljava/lang/String;", {"Ljava/lang/String;"});
    uint32_t m_join = db.AddMethod("Ljava/lang/String;", "join", "Ljava/lang/String;",
                                   {"Ljava/lang/CharSequence;", "[Ljava/lang/CharSequence;"});
    uint32_t m_format = db.AddMethod("Ljava/lang/String;", "format", "Ljava/lang/String;",
                                     {"Ljava/lang/String;", "[Ljava/lang/Object;"});
    uint32_t m_valInt =
        db.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    uint32_t m_valDbl = db.AddMethod("Ljava/lang/Double;", "valueOf", "Ljava/lang/Double;", {"D"});
    uint32_t m_valChr =
        db.AddMethod("Ljava/lang/Character;", "valueOf", "Ljava/lang/Character;", {"C"});
    uint32_t m_valBool =
        db.AddMethod("Ljava/lang/Boolean;", "valueOf", "Ljava/lang/Boolean;", {"Z"});

    auto &cls = db.AddClass("LTestFmt;");

    RuntimeDexBuilder::MethodSpec m_rep_fn;
    m_rep_fn.class_idx = cls.class_idx;
    m_rep_fn.method_idx = db.AddMethod("LTestFmt;", "trimSplitJoin", "Ljava/lang/String;", {});
    m_rep_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_rep_fn.registers_size = 5;
    Emit21c(m_rep_fn.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_raw));
    Emit35c(m_rep_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_trim), {0});
    m_rep_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit21c(m_rep_fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_comma));
    Emit21c(m_rep_fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_bar));
    Emit35c(m_rep_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rep), {0, 1, 2});
    m_rep_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit35c(m_rep_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_split), {0, 2});
    m_rep_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    Emit21c(m_rep_fn.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_dash));
    Emit35c(m_rep_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_join), {4, 3});
    m_rep_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    m_rep_fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(m_rep_fn));

    RuntimeDexBuilder::MethodSpec m_fmt_fn;
    m_fmt_fn.class_idx = cls.class_idx;
    m_fmt_fn.method_idx = db.AddMethod("LTestFmt;", "fmt", "Ljava/lang/String;", {});
    m_fmt_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_fmt_fn.registers_size = 7;
    // args = new Object[7]
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 7);
    Emit22c(m_fmt_fn.insns, OP_NEW_ARRAY, 0, 1, static_cast<uint16_t>(t_obj_arr));
    // [0] = "hi"
    Emit21c(m_fmt_fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_hi));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 0);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [1] = Integer.valueOf(42)
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 3, 42);
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valInt), {3});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 1);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [2] = Integer.valueOf(255)
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 3, 255);
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valInt), {3});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 2);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [3] = Integer.valueOf(171) (0xAB)
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 3, 171);
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valInt), {3});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 3);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [4] = Double.valueOf(3.25)
    uint64_t d_bits = 0x400A000000000000ULL; // 3.25
    Emit51l(m_fmt_fn.insns, OP_CONST_WIDE, 4, d_bits);
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valDbl), {4, 5});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 4);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [5] = Character.valueOf('Z')
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 3, 'Z');
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valChr), {3});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 5);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);
    // [6] = Boolean.valueOf(true)
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 3, 1);
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_valBool), {3});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_fmt_fn.insns, OP_CONST_16, 1, 6);
    Emit23x(m_fmt_fn.insns, OP_APUT_OBJECT, 2, 0, 1);

    Emit21c(m_fmt_fn.insns, OP_CONST_STRING, 6, static_cast<uint16_t>(s_fmt));
    Emit35c(m_fmt_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_format), {6, 0});
    m_fmt_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    m_fmt_fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(m_fmt_fn));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(
        ReadJavaString(CallStaticObj(*dex, "LTestFmt;", "trimSplitJoin", "()Ljava/lang/String;")),
        "a-b-c");
    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestFmt;", "fmt", "()Ljava/lang/String;")),
              "hi:0042:ff:AB:3.25:Z:true%");
}

TEST_F(JavaStringTest, StringBuilderInsertDeleteReverseAndValueOf)
{
    RuntimeDexBuilder db;
    uint32_t t_sb = db.AddType("Ljava/lang/StringBuilder;");
    uint32_t s_abc = db.AddString("abc");
    uint32_t s_xy = db.AddString("XY");

    uint32_t m_sb_init = db.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", {});
    uint32_t m_app_str = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                      "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});
    uint32_t m_app_dbl =
        db.AddMethod("Ljava/lang/StringBuilder;", "append", "Ljava/lang/StringBuilder;", {"D"});
    uint32_t m_insert = db.AddMethod("Ljava/lang/StringBuilder;", "insert",
                                     "Ljava/lang/StringBuilder;", {"I", "Ljava/lang/String;"});
    uint32_t m_delete = db.AddMethod("Ljava/lang/StringBuilder;", "delete",
                                     "Ljava/lang/StringBuilder;", {"I", "I"});
    uint32_t m_rev =
        db.AddMethod("Ljava/lang/StringBuilder;", "reverse", "Ljava/lang/StringBuilder;", {});
    uint32_t m_toStr =
        db.AddMethod("Ljava/lang/StringBuilder;", "toString", "Ljava/lang/String;", {});

    auto &cls = db.AddClass("LTestSb;");
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = cls.class_idx;
    ms.method_idx = db.AddMethod("LTestSb;", "build", "Ljava/lang/String;", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 6;

    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_sb));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_sb_init), {0});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_abc));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_app_str), {0, 1});
    // insert(1, "XY") -> "aXYbc"
    Emit21s(ms.insns, OP_CONST_16, 2, 1);
    Emit21c(ms.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_xy));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_insert), {0, 2, 3});
    // delete(2, 4) -> "aXc"
    Emit21s(ms.insns, OP_CONST_16, 2, 2);
    Emit21s(ms.insns, OP_CONST_16, 3, 4);
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_delete), {0, 2, 3});
    // reverse() -> "cXa"
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rev), {0});
    // append(2.0) -> "cXa2.0"
    Emit51l(ms.insns, OP_CONST_WIDE, 4, 0x4000000000000000ULL);
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_app_dbl), {0, 4, 5});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_toStr), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    ms.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(ms));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestSb;", "build", "()Ljava/lang/String;")),
              "cXa2.0");
}

} // namespace
} // namespace portage::package
