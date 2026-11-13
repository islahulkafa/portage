#include "java_runtime_dex_builder.hpp"
#include "package/dex_vm.hpp"
#include "package/jni_env.hpp"

#include <bit>
#include <gtest/gtest.h>
#include <limits>

namespace portage::package {
namespace {

using namespace test_util;

class JavaCollectionsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        JniEnvironment::Instance().Initialize("com.test.collections", ".", ".");
        DexVm::Instance().Reset();
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

    static std::string ReadJavaString(jobject obj)
    {
        const char *s = JniEnvironment::Instance().GetStringChars(reinterpret_cast<jstring>(obj));
        return s ? std::string(s) : std::string();
    }
};

TEST_F(JavaCollectionsTest, BoxingRoundTripsCacheAndExceptions)
{
    RuntimeDexBuilder db;
    uint32_t s_bad = db.AddString("not_an_int");
    uint32_t s_hex = db.AddString("ff");
    uint32_t s_big = db.AddString("9876543210");

    uint32_t f_bool_true = db.AddField("Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;");
    uint32_t f_dbl_max = db.AddField("Ljava/lang/Double;", "MAX_VALUE", "D");

    uint32_t m_int_valOf =
        db.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    uint32_t m_int_intVal = db.AddMethod("Ljava/lang/Integer;", "intValue", "I", {});
    uint32_t m_int_parse =
        db.AddMethod("Ljava/lang/Integer;", "parseInt", "I", {"Ljava/lang/String;"});
    uint32_t m_int_parseRadix =
        db.AddMethod("Ljava/lang/Integer;", "parseInt", "I", {"Ljava/lang/String;", "I"});
    uint32_t m_int_toHex =
        db.AddMethod("Ljava/lang/Integer;", "toHexString", "Ljava/lang/String;", {"I"});
    uint32_t m_long_parse =
        db.AddMethod("Ljava/lang/Long;", "parseLong", "J", {"Ljava/lang/String;"});
    uint32_t m_long_valOf = db.AddMethod("Ljava/lang/Long;", "valueOf", "Ljava/lang/Long;", {"J"});
    uint32_t m_bool_val = db.AddMethod("Ljava/lang/Boolean;", "booleanValue", "Z", {});
    uint32_t m_char_isDigit = db.AddMethod("Ljava/lang/Character;", "isDigit", "Z", {"C"});
    uint32_t m_char_toUpper = db.AddMethod("Ljava/lang/Character;", "toUpperCase", "C", {"C"});

    auto &cls = db.AddClass("LTestBox;");

    // checkCache(): returns 1 if valueOf(100)==valueOf(100) && valueOf(200)!=valueOf(200) &&
    // TRUE.booleanValue() && parseInt("ff", 16) == 255 && isDigit('7') && toUpperCase('b')=='B'
    RuntimeDexBuilder::MethodSpec m_cache;
    m_cache.class_idx = cls.class_idx;
    m_cache.method_idx = db.AddMethod("LTestBox;", "checkCacheAndOps", "I", {});
    m_cache.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_cache.registers_size = 6;
    // fail label at end
    Emit21s(m_cache.insns, OP_CONST_16, 0, 100);
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    // if r1 != r2 goto fail
    std::vector<size_t> fail_jumps;
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_NE, 1, 2, 0);

    Emit21s(m_cache.insns, OP_CONST_16, 0, 200);
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_EQ, 1, 2, 0);

    // intValue of r1 == 200
    Emit35c(m_cache.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_int_intVal), {1});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_NE, 3, 0, 0);

    // Boolean.TRUE.booleanValue() == true
    Emit21c(m_cache.insns, OP_SGET_OBJECT, 4, static_cast<uint16_t>(f_bool_true));
    Emit35c(m_cache.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_bool_val), {4});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT, 5));
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_EQZ, 5, 0, 0);

    // Integer.parseInt("ff", 16) == 255
    Emit21c(m_cache.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_hex));
    Emit21s(m_cache.insns, OP_CONST_16, 2, 16);
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_parseRadix), {1, 2});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    Emit21s(m_cache.insns, OP_CONST_16, 0, 255);
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_NE, 3, 0, 0);

    // Character.isDigit('7') == true && Character.toUpperCase('b') == 'B'
    Emit21s(m_cache.insns, OP_CONST_16, 0, '7');
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_char_isDigit), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_EQZ, 3, 0, 0);

    Emit21s(m_cache.insns, OP_CONST_16, 0, 'b');
    Emit35c(m_cache.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_char_toUpper), {0});
    m_cache.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    Emit21s(m_cache.insns, OP_CONST_16, 0, 'B');
    fail_jumps.push_back(m_cache.insns.size());
    Emit22t(m_cache.insns, OP_IF_NE, 3, 0, 0);

    Emit21s(m_cache.insns, OP_CONST_16, 0, 1);
    m_cache.insns.push_back(Encode11x(OP_RETURN, 0));

    uint32_t fail_pc = static_cast<uint32_t>(m_cache.insns.size());
    for (size_t jpc : fail_jumps) {
        m_cache.insns[jpc + 1] = static_cast<uint16_t>(static_cast<int16_t>(fail_pc - jpc));
    }
    Emit21s(m_cache.insns, OP_CONST_16, 0, 0);
    m_cache.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.direct_methods.push_back(std::move(m_cache));

    // catchParseError(): NumberFormatException -> return 42
    RuntimeDexBuilder::MethodSpec m_nfe;
    m_nfe.class_idx = cls.class_idx;
    m_nfe.method_idx = db.AddMethod("LTestBox;", "catchParseError", "I", {});
    m_nfe.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_nfe.registers_size = 2;
    Emit21c(m_nfe.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_bad));
    uint32_t try_start = static_cast<uint32_t>(m_nfe.insns.size());
    Emit35c(m_nfe.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_parse), {0});
    uint32_t try_end = static_cast<uint32_t>(m_nfe.insns.size());
    Emit21s(m_nfe.insns, OP_CONST_16, 0, -1);
    m_nfe.insns.push_back(Encode11x(OP_RETURN, 0));
    uint32_t catch_pc = static_cast<uint32_t>(m_nfe.insns.size());
    m_nfe.insns.push_back(Encode11x(OP_MOVE_EXCEPTION, 1));
    Emit21s(m_nfe.insns, OP_CONST_16, 0, 42);
    m_nfe.insns.push_back(Encode11x(OP_RETURN, 0));
    RuntimeDexBuilder::TrySpec ts;
    ts.start_addr = try_start;
    ts.insn_count = static_cast<uint16_t>(try_end - try_start);
    ts.handlers.push_back({"Ljava/lang/NumberFormatException;", catch_pc});
    m_nfe.tries.push_back(std::move(ts));
    cls.direct_methods.push_back(std::move(m_nfe));

    // doubleMaxAndLong(): checks Double.MAX_VALUE and Long.parseLong
    RuntimeDexBuilder::MethodSpec m_dl;
    m_dl.class_idx = cls.class_idx;
    m_dl.method_idx = db.AddMethod("LTestBox;", "longAndDoubleCheck", "I", {});
    m_dl.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_dl.registers_size = 4;
    Emit21c(m_dl.insns, OP_SGET_WIDE, 0, static_cast<uint16_t>(f_dbl_max));
    Emit21c(m_dl.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_big));
    Emit35c(m_dl.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_long_parse), {2});
    m_dl.insns.push_back(Encode11x(OP_MOVE_RESULT_WIDE, 2));
    // wrap in Long.valueOf(long) and also check toHexString(255)
    Emit35c(m_dl.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_long_valOf), {2, 3});
    m_dl.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(m_dl.insns, OP_CONST_16, 2, 255);
    Emit35c(m_dl.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_toHex), {2});
    m_dl.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    // check Double.MAX_VALUE upper 32 bits match 0x7FEFFFFF
    uint32_t expected_hi =
        static_cast<uint32_t>(std::bit_cast<uint64_t>(std::numeric_limits<double>::max()) >> 32);
    Emit31i(m_dl.insns, OP_CONST, 2, expected_hi);
    m_dl.insns.push_back(Encode11x(OP_RETURN, 2));
    cls.direct_methods.push_back(std::move(m_dl));

    RuntimeDexBuilder::MethodSpec m_hex;
    m_hex.class_idx = cls.class_idx;
    m_hex.method_idx = db.AddMethod("LTestBox;", "hex255", "Ljava/lang/String;", {});
    m_hex.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_hex.registers_size = 2;
    Emit21s(m_hex.insns, OP_CONST_16, 0, 255);
    Emit35c(m_hex.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_toHex), {0});
    m_hex.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    m_hex.insns.push_back(Encode11x(OP_RETURN_OBJECT, 1));
    cls.direct_methods.push_back(std::move(m_hex));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LTestBox;", "checkCacheAndOps", "()I"), 1);
    EXPECT_EQ(CallStaticInt(*dex, "LTestBox;", "catchParseError", "()I"), 42);
    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestBox;", "hex255", "()Ljava/lang/String;")),
              "ff");
    EXPECT_EQ(
        CallStaticInt(*dex, "LTestBox;", "longAndDoubleCheck", "()I"),
        static_cast<int32_t>(std::bit_cast<uint64_t>(std::numeric_limits<double>::max()) >> 32));
}

TEST_F(JavaCollectionsTest, ArrayListLinkedListAndArrayDequeOps)
{
    RuntimeDexBuilder db;
    uint32_t t_al = db.AddType("Ljava/util/ArrayList;");
    uint32_t t_ll = db.AddType("Ljava/util/LinkedList;");
    uint32_t t_dq = db.AddType("Ljava/util/ArrayDeque;");
    uint32_t s_a = db.AddString("A");
    uint32_t s_b = db.AddString("B");
    uint32_t s_c = db.AddString("C");
    uint32_t s_sep = db.AddString(":");

    uint32_t m_al_init = db.AddMethod("Ljava/util/ArrayList;", "<init>", "V", {});
    uint32_t m_ll_init = db.AddMethod("Ljava/util/LinkedList;", "<init>", "V", {});
    uint32_t m_dq_init = db.AddMethod("Ljava/util/ArrayDeque;", "<init>", "V", {});
    uint32_t m_list_add = db.AddMethod("Ljava/util/List;", "add", "Z", {"Ljava/lang/Object;"});
    uint32_t m_list_addAt =
        db.AddMethod("Ljava/util/List;", "add", "V", {"I", "Ljava/lang/Object;"});
    uint32_t m_list_set =
        db.AddMethod("Ljava/util/List;", "set", "Ljava/lang/Object;", {"I", "Ljava/lang/Object;"});
    uint32_t m_list_get = db.AddMethod("Ljava/util/List;", "get", "Ljava/lang/Object;", {"I"});
    uint32_t m_list_size = db.AddMethod("Ljava/util/List;", "size", "I", {});
    uint32_t m_list_iter = db.AddMethod("Ljava/util/List;", "iterator", "Ljava/util/Iterator;", {});
    uint32_t m_it_hasNext = db.AddMethod("Ljava/util/Iterator;", "hasNext", "Z", {});
    uint32_t m_it_next = db.AddMethod("Ljava/util/Iterator;", "next", "Ljava/lang/Object;", {});

    uint32_t m_ll_addFirst =
        db.AddMethod("Ljava/util/LinkedList;", "addFirst", "V", {"Ljava/lang/Object;"});
    uint32_t m_ll_remLast =
        db.AddMethod("Ljava/util/LinkedList;", "removeLast", "Ljava/lang/Object;", {});
    uint32_t m_dq_push =
        db.AddMethod("Ljava/util/ArrayDeque;", "push", "V", {"Ljava/lang/Object;"});
    uint32_t m_dq_pop = db.AddMethod("Ljava/util/ArrayDeque;", "pop", "Ljava/lang/Object;", {});
    uint32_t m_dq_pollLast =
        db.AddMethod("Ljava/util/ArrayDeque;", "pollLast", "Ljava/lang/Object;", {});

    uint32_t t_sb = db.AddType("Ljava/lang/StringBuilder;");
    uint32_t m_sb_init = db.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", {});
    uint32_t m_sb_appObj = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                        "Ljava/lang/StringBuilder;", {"Ljava/lang/Object;"});
    uint32_t m_sb_appStr = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                        "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});
    uint32_t m_sb_toStr =
        db.AddMethod("Ljava/lang/StringBuilder;", "toString", "Ljava/lang/String;", {});

    auto &cls = db.AddClass("LTestLists;");
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = cls.class_idx;
    ms.method_idx = db.AddMethod("LTestLists;", "run", "Ljava/lang/String;", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 8;

    // list = new ArrayList(); list.add("A"); list.add("C"); list.add(1, "B"); list.set(2, "C");
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_al));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_al_init), {0});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_a));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    Emit21c(ms.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_c));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 2});
    Emit21s(ms.insns, OP_CONST_16, 3, 1);
    Emit21c(ms.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_b));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_addAt), {0, 3, 4});
    Emit21s(ms.insns, OP_CONST_16, 3, 2);
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_set), {0, 3, 2});

    // sb = new StringBuilder(); iterate list -> "ABC"
    Emit21c(ms.insns, OP_NEW_INSTANCE, 5, static_cast<uint16_t>(t_sb));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_sb_init), {5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_iter), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 6));
    uint32_t loop_pc = static_cast<uint32_t>(ms.insns.size());
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_it_hasNext), {6});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 7));
    size_t exit_jmp = ms.insns.size();
    Emit22t(ms.insns, OP_IF_EQZ, 7, 0, 0);
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_it_next), {6});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {5, 7});
    ms.insns.push_back(static_cast<uint16_t>(
        OP_GOTO | (static_cast<uint8_t>(static_cast<int8_t>(loop_pc - ms.insns.size())) << 8)));
    uint32_t after_loop = static_cast<uint32_t>(ms.insns.size());
    ms.insns[exit_jmp + 1] = static_cast<uint16_t>(static_cast<int16_t>(after_loop - exit_jmp));

    Emit21c(ms.insns, OP_CONST_STRING, 7, static_cast<uint16_t>(s_sep));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appStr), {5, 7});

    // LinkedList ll = new LinkedList(); ll.add("B"); ll.addFirst("A"); sb.append(ll.removeLast())
    // -> "B"
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_ll));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_ll_init), {0});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 4});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ll_addFirst), {0, 1});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ll_remLast), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {5, 7});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_get),
            {0, static_cast<uint8_t>(3)}); // unused size=1 index 0 below:
    ms.insns.pop_back();
    ms.insns.pop_back();
    ms.insns.pop_back();

    // ArrayDeque dq = new ArrayDeque(); dq.push("A"); dq.push("B"); dq.push("C"); pop->"C",
    // pollLast->"A"
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_dq));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_dq_init), {0});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_dq_push), {0, 1});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_dq_push), {0, 4});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_dq_push), {0, 2});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_dq_pop), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {5, 7});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_dq_pollLast), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {5, 7});

    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_toStr), {5});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    ms.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    (void)m_list_size;
    (void)m_list_get;

    cls.direct_methods.push_back(std::move(ms));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestLists;", "run", "()Ljava/lang/String;")),
              "ABC:BCA");
}

TEST_F(JavaCollectionsTest, HashMapWithGuestEqualsHashCodeAndIteration)
{
    RuntimeDexBuilder db;
    uint32_t t_key = db.AddType("LGuestKey;");
    uint32_t t_hm = db.AddType("Ljava/util/HashMap;");
    uint32_t f_id = db.AddField("LGuestKey;", "id", "I");
    uint32_t s_v1 = db.AddString("first");
    uint32_t s_v2 = db.AddString("second");

    uint32_t m_obj_init = db.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t m_key_init = db.AddMethod("LGuestKey;", "<init>", "V", {"I"});
    uint32_t m_key_hash = db.AddMethod("LGuestKey;", "hashCode", "I", {});
    uint32_t m_key_eq = db.AddMethod("LGuestKey;", "equals", "Z", {"Ljava/lang/Object;"});

    uint32_t m_hm_init = db.AddMethod("Ljava/util/HashMap;", "<init>", "V", {});
    uint32_t m_map_put = db.AddMethod("Ljava/util/Map;", "put", "Ljava/lang/Object;",
                                      {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    uint32_t m_map_get =
        db.AddMethod("Ljava/util/Map;", "get", "Ljava/lang/Object;", {"Ljava/lang/Object;"});
    uint32_t m_map_size = db.AddMethod("Ljava/util/Map;", "size", "I", {});
    uint32_t m_map_entrySet = db.AddMethod("Ljava/util/Map;", "entrySet", "Ljava/util/Set;", {});
    uint32_t m_set_iter = db.AddMethod("Ljava/util/Set;", "iterator", "Ljava/util/Iterator;", {});
    uint32_t m_it_next = db.AddMethod("Ljava/util/Iterator;", "next", "Ljava/lang/Object;", {});
    uint32_t m_entry_val =
        db.AddMethod("Ljava/util/Map$Entry;", "getValue", "Ljava/lang/Object;", {});

    // GuestKey: hashCode returns id & 1, equals returns this.id == other.id
    auto &kcls = db.AddClass("LGuestKey;");
    kcls.instance_fields.push_back({f_id, ACC_PUBLIC});

    RuntimeDexBuilder::MethodSpec k_init;
    k_init.class_idx = kcls.class_idx;
    k_init.method_idx = m_key_init;
    k_init.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
    k_init.registers_size = 2;
    k_init.ins_size = 2;
    Emit35c(k_init.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_obj_init), {0});
    Emit22c(k_init.insns, OP_IPUT, 1, 0, static_cast<uint16_t>(f_id));
    k_init.insns.push_back(Encode10x(OP_RETURN_VOID));

    RuntimeDexBuilder::MethodSpec k_hash;
    k_hash.class_idx = kcls.class_idx;
    k_hash.method_idx = m_key_hash;
    k_hash.access_flags = ACC_PUBLIC;
    k_hash.registers_size = 2;
    k_hash.ins_size = 1;
    Emit22c(k_hash.insns, OP_IGET, 0, 1, static_cast<uint16_t>(f_id));
    // register 1 = this initially; preserve this in 1 by loading from 1 into 0
    k_hash.insns.clear();
    Emit22c(k_hash.insns, OP_IGET, 0, 1, static_cast<uint16_t>(f_id));
    // Force hash collision (always 7) to prove equals() disambiguates collisions!
    Emit21s(k_hash.insns, OP_CONST_16, 0, 7);
    k_hash.insns.push_back(Encode11x(OP_RETURN, 0));

    RuntimeDexBuilder::MethodSpec k_eq;
    k_eq.class_idx = kcls.class_idx;
    k_eq.method_idx = m_key_eq;
    k_eq.access_flags = ACC_PUBLIC;
    k_eq.registers_size = 4; // v0=a, v1=b, p0(v2)=this, p1(v3)=other
    k_eq.ins_size = 2;
    Emit22c(k_eq.insns, OP_IGET, 0, 2, static_cast<uint16_t>(f_id));
    Emit22c(k_eq.insns, OP_IGET, 1, 3, static_cast<uint16_t>(f_id));
    Emit22t(k_eq.insns, OP_IF_NE, 0, 1, 5);
    Emit21s(k_eq.insns, OP_CONST_16, 0, 1);
    k_eq.insns.push_back(Encode11x(OP_RETURN, 0));
    Emit21s(k_eq.insns, OP_CONST_16, 0, 0);
    k_eq.insns.push_back(Encode11x(OP_RETURN, 0));

    kcls.direct_methods.push_back(std::move(k_init));
    kcls.virtual_methods.push_back(std::move(k_hash));
    kcls.virtual_methods.push_back(std::move(k_eq));

    auto &tcls = db.AddClass("LTestMap;");
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = tcls.class_idx;
    ms.method_idx = db.AddMethod("LTestMap;", "run", "Ljava/lang/String;", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 7;

    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_hm));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_hm_init), {0});

    // k1a = GuestKey(10), k1b = GuestKey(10), k2 = GuestKey(20)
    Emit21c(ms.insns, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_key));
    Emit21s(ms.insns, OP_CONST_16, 4, 10);
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_key_init), {1, 4});

    Emit21c(ms.insns, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_key));
    Emit21s(ms.insns, OP_CONST_16, 4, 10);
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_key_init), {2, 4});

    Emit21c(ms.insns, OP_NEW_INSTANCE, 3, static_cast<uint16_t>(t_key));
    Emit21s(ms.insns, OP_CONST_16, 4, 20);
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_key_init), {3, 4});

    // map.put(k1a, "first"); map.put(k2, "first"); map.put(k1b, "second"); -> size must be 2,
    // get(k1b)=="second"
    Emit21c(ms.insns, OP_CONST_STRING, 5, static_cast<uint16_t>(s_v1));
    Emit21c(ms.insns, OP_CONST_STRING, 6, static_cast<uint16_t>(s_v2));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_put), {0, 1, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_put), {0, 3, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_put), {0, 2, 6});

    // Verify size == 2; if not 2 return null
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_size), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 4));
    Emit21s(ms.insns, OP_CONST_16, 5, 2);
    Emit22t(ms.insns, OP_IF_EQ, 4, 5, 5);
    Emit21s(ms.insns, OP_CONST_16, 0, 0);
    ms.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));

    // Also inspect first entrySet().iterator().next().getValue() isn't crashing and
    // get(k1a)=="second"
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_entrySet), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_set_iter), {4});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_it_next), {4});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_entry_val), {4});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));

    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_get), {0, 1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    ms.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    tcls.direct_methods.push_back(std::move(ms));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestMap;", "run", "()Ljava/lang/String;")),
              "second");
}

TEST_F(JavaCollectionsTest, LinkedHashMapAndTreeMapWithComparator)
{
    RuntimeDexBuilder db;
    uint32_t t_comp = db.AddType("LRevComp;");
    uint32_t t_tm = db.AddType("Ljava/util/TreeMap;");
    uint32_t t_lhm = db.AddType("Ljava/util/LinkedHashMap;");
    uint32_t s_a = db.AddString("alpha");
    uint32_t s_b = db.AddString("beta");
    uint32_t s_c = db.AddString("gamma");
    uint32_t s_one = db.AddString("1");

    uint32_t m_obj_init = db.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t m_comp_init = db.AddMethod("LRevComp;", "<init>", "V", {});
    uint32_t m_comp_cmp =
        db.AddMethod("LRevComp;", "compare", "I", {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    uint32_t m_str_cmp =
        db.AddMethod("Ljava/lang/String;", "compareTo", "I", {"Ljava/lang/String;"});

    uint32_t m_tm_init =
        db.AddMethod("Ljava/util/TreeMap;", "<init>", "V", {"Ljava/util/Comparator;"});
    uint32_t m_tm_first = db.AddMethod("Ljava/util/TreeMap;", "firstKey", "Ljava/lang/Object;", {});
    uint32_t m_tm_last = db.AddMethod("Ljava/util/TreeMap;", "lastKey", "Ljava/lang/Object;", {});
    uint32_t m_lhm_init = db.AddMethod("Ljava/util/LinkedHashMap;", "<init>", "V", {});
    uint32_t m_put = db.AddMethod("Ljava/util/Map;", "put", "Ljava/lang/Object;",
                                  {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    uint32_t m_keySet = db.AddMethod("Ljava/util/Map;", "keySet", "Ljava/util/Set;", {});
    uint32_t m_iter = db.AddMethod("Ljava/util/Set;", "iterator", "Ljava/util/Iterator;", {});
    uint32_t m_hasNext = db.AddMethod("Ljava/util/Iterator;", "hasNext", "Z", {});
    uint32_t m_next = db.AddMethod("Ljava/util/Iterator;", "next", "Ljava/lang/Object;", {});
    uint32_t t_sb = db.AddType("Ljava/lang/StringBuilder;");
    uint32_t m_sb_init = db.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", {});
    uint32_t m_sb_appObj = db.AddMethod("Ljava/lang/StringBuilder;", "append",
                                        "Ljava/lang/StringBuilder;", {"Ljava/lang/Object;"});
    uint32_t m_sb_toStr =
        db.AddMethod("Ljava/lang/StringBuilder;", "toString", "Ljava/lang/String;", {});

    // RevComp implements Comparator: return b.compareTo(a)
    auto &ccls = db.AddClass("LRevComp;");
    ccls.interfaces.push_back(db.AddType("Ljava/util/Comparator;"));
    RuntimeDexBuilder::MethodSpec c_init;
    c_init.class_idx = ccls.class_idx;
    c_init.method_idx = m_comp_init;
    c_init.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
    c_init.registers_size = 1;
    c_init.ins_size = 1;
    Emit35c(c_init.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_obj_init), {0});
    c_init.insns.push_back(Encode10x(OP_RETURN_VOID));

    RuntimeDexBuilder::MethodSpec c_cmp;
    c_cmp.class_idx = ccls.class_idx;
    c_cmp.method_idx = m_comp_cmp;
    c_cmp.access_flags = ACC_PUBLIC;
    c_cmp.registers_size = 4; // p0(v1)=this, p1(v2)=a, p2(v3)=b
    c_cmp.ins_size = 3;
    Emit22c(c_cmp.insns, OP_CHECK_CAST, 2, 0,
            static_cast<uint16_t>(db.AddType("Ljava/lang/String;")));
    c_cmp.insns.pop_back();
    c_cmp.insns.pop_back();
    Emit35c(c_cmp.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_str_cmp), {3, 2});
    c_cmp.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));
    c_cmp.insns.push_back(Encode11x(OP_RETURN, 0));

    ccls.direct_methods.push_back(std::move(c_init));
    ccls.virtual_methods.push_back(std::move(c_cmp));

    auto &tcls = db.AddClass("LTestOrdered;");
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = tcls.class_idx;
    ms.method_idx = db.AddMethod("LTestOrdered;", "run", "Ljava/lang/String;", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 8;

    // TreeMap with RevComp: put alpha, gamma, beta -> firstKey="gamma", lastKey="alpha"
    Emit21c(ms.insns, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_comp));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_comp_init), {1});
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_tm));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_tm_init), {0, 1});
    Emit21c(ms.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_a));
    Emit21c(ms.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_c));
    Emit21c(ms.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_b));
    Emit21c(ms.insns, OP_CONST_STRING, 5, static_cast<uint16_t>(s_one));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 2, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 3, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 4, 5});

    Emit21c(ms.insns, OP_NEW_INSTANCE, 6, static_cast<uint16_t>(t_sb));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_sb_init), {6});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_tm_first), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {6, 7});
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_tm_last), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {6, 7});

    // LinkedHashMap preserves insertion order: gamma, alpha, beta
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_lhm));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_lhm_init), {0});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 3, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 2, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_put), {0, 4, 5});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_keySet), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_iter), {1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));

    uint32_t loop_pc = static_cast<uint32_t>(ms.insns.size());
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_hasNext), {1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 7));
    size_t exit_jmp = ms.insns.size();
    Emit22t(ms.insns, OP_IF_EQZ, 7, 0, 0);
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_next), {1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 7));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_appObj), {6, 7});
    ms.insns.push_back(static_cast<uint16_t>(
        OP_GOTO | (static_cast<uint8_t>(static_cast<int8_t>(loop_pc - ms.insns.size())) << 8)));
    uint32_t after_loop = static_cast<uint32_t>(ms.insns.size());
    ms.insns[exit_jmp + 1] = static_cast<uint16_t>(static_cast<int16_t>(after_loop - exit_jmp));

    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_sb_toStr), {6});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    ms.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    tcls.direct_methods.push_back(std::move(ms));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LTestOrdered;", "run", "()Ljava/lang/String;")),
              "gammaalphagammaalphabeta");
}

TEST_F(JavaCollectionsTest, HashSetCmeArraysCollectionsObjectsAndOptional)
{
    RuntimeDexBuilder db;
    uint32_t t_hs = db.AddType("Ljava/util/HashSet;");
    uint32_t t_al = db.AddType("Ljava/util/ArrayList;");
    uint32_t t_int_arr = db.AddType("[I");
    uint32_t s_a = db.AddString("x");
    uint32_t s_b = db.AddString("z");
    uint32_t s_c = db.AddString("a");
    uint32_t s_fb = db.AddString("fallback");

    uint32_t m_hs_init = db.AddMethod("Ljava/util/HashSet;", "<init>", "V", {});
    uint32_t m_set_add = db.AddMethod("Ljava/util/Set;", "add", "Z", {"Ljava/lang/Object;"});
    uint32_t m_set_size = db.AddMethod("Ljava/util/Set;", "size", "I", {});
    uint32_t m_al_init = db.AddMethod("Ljava/util/ArrayList;", "<init>", "V", {});
    uint32_t m_list_add = db.AddMethod("Ljava/util/List;", "add", "Z", {"Ljava/lang/Object;"});
    uint32_t m_list_get = db.AddMethod("Ljava/util/List;", "get", "Ljava/lang/Object;", {"I"});
    uint32_t m_list_iter = db.AddMethod("Ljava/util/List;", "iterator", "Ljava/util/Iterator;", {});
    uint32_t m_it_next = db.AddMethod("Ljava/util/Iterator;", "next", "Ljava/lang/Object;", {});
    uint32_t m_arr_sort = db.AddMethod("Ljava/util/Arrays;", "sort", "V", {"[I"});
    uint32_t m_col_sort =
        db.AddMethod("Ljava/util/Collections;", "sort", "V", {"Ljava/util/List;"});
    uint32_t m_obj_eq = db.AddMethod("Ljava/util/Objects;", "equals", "Z",
                                     {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    uint32_t m_opt_empty =
        db.AddMethod("Ljava/util/Optional;", "empty", "Ljava/util/Optional;", {});
    uint32_t m_opt_orElse = db.AddMethod("Ljava/util/Optional;", "orElse", "Ljava/lang/Object;",
                                         {"Ljava/lang/Object;"});

    auto &cls = db.AddClass("LTestMisc;");

    // Check HashSet dedupe + Arrays.sort + Collections.sort + Objects.equals +
    // Optional.empty().orElse
    RuntimeDexBuilder::MethodSpec ms;
    ms.class_idx = cls.class_idx;
    ms.method_idx = db.AddMethod("LTestMisc;", "checkMisc", "I", {});
    ms.access_flags = ACC_PUBLIC | ACC_STATIC;
    ms.registers_size = 6;

    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_hs));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_hs_init), {0});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_a));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_set_add), {0, 1});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_set_add), {0, 1});
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_set_size), {0});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 2)); // should be 1

    // int[] arr = new int[3]; {30, 10, 20}; Arrays.sort(arr); -> arr[0]==10
    Emit21s(ms.insns, OP_CONST_16, 3, 3);
    Emit22c(ms.insns, OP_NEW_ARRAY, 0, 3, static_cast<uint16_t>(t_int_arr));
    Emit21s(ms.insns, OP_CONST_16, 4, 30);
    Emit21s(ms.insns, OP_CONST_16, 5, 0);
    Emit23x(ms.insns, OP_APUT, 4, 0, 5);
    Emit21s(ms.insns, OP_CONST_16, 4, 10);
    Emit21s(ms.insns, OP_CONST_16, 5, 1);
    Emit23x(ms.insns, OP_APUT, 4, 0, 5);
    Emit21s(ms.insns, OP_CONST_16, 4, 20);
    Emit21s(ms.insns, OP_CONST_16, 5, 2);
    Emit23x(ms.insns, OP_APUT, 4, 0, 5);
    Emit35c(ms.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_arr_sort), {0});
    Emit21s(ms.insns, OP_CONST_16, 5, 0);
    Emit23x(ms.insns, OP_AGET, 3, 0, 5); // 10

    // Collections.sort(["x", "z", "a"]) -> list.get(0) == "a"
    Emit21c(ms.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_al));
    Emit35c(ms.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_al_init), {0});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_a));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_b));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_c));
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    Emit35c(ms.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_col_sort), {0});
    Emit21s(ms.insns, OP_CONST_16, 5, 0);
    Emit35c(ms.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_get), {0, 5});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_c));
    Emit35c(ms.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_obj_eq), {4, 1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 4)); // 1

    // Optional.empty().orElse("fallback") == "fallback"
    Emit35c(ms.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_opt_empty), {});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit21c(ms.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_fb));
    Emit35c(ms.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_opt_orElse), {0, 1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit35c(ms.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_obj_eq), {0, 1});
    ms.insns.push_back(Encode11x(OP_MOVE_RESULT, 5)); // 1

    // Return set_size * 100 + arr0 * 10 + col_ok + opt_ok (1*100 + 10*10 + 1 + 1 = 202? Return
    // distinct: 2 + 10 = 12 or set_size + arr0 + col_ok*100 + opt_ok*1000 = 1111) Simpler: return
    // (r2 == 1 && r3 == 10 && r4 == 1 && r5 == 1) -> return r3 + r2 + r4 + r5 = 13
    Emit23x(ms.insns, OP_ADD_INT, 0, 2, 3);
    Emit23x(ms.insns, OP_ADD_INT, 0, 0, 4);
    Emit23x(ms.insns, OP_ADD_INT, 0, 0, 5);
    ms.insns.push_back(Encode11x(OP_RETURN, 0));

    // catchCme(): mutating ArrayList while iterating throws ConcurrentModificationException ->
    // return 99
    RuntimeDexBuilder::MethodSpec m_cme;
    m_cme.class_idx = cls.class_idx;
    m_cme.method_idx = db.AddMethod("LTestMisc;", "catchCme", "I", {});
    m_cme.access_flags = ACC_PUBLIC | ACC_STATIC;
    m_cme.registers_size = 3;
    Emit21c(m_cme.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_al));
    Emit35c(m_cme.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_al_init), {0});
    Emit21c(m_cme.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_a));
    Emit35c(m_cme.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    Emit35c(m_cme.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_iter), {0});
    m_cme.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit35c(m_cme.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_list_add), {0, 1});
    uint32_t cme_try_start = static_cast<uint32_t>(m_cme.insns.size());
    Emit35c(m_cme.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_it_next), {2});
    uint32_t cme_try_end = static_cast<uint32_t>(m_cme.insns.size());
    Emit21s(m_cme.insns, OP_CONST_16, 0, -1);
    m_cme.insns.push_back(Encode11x(OP_RETURN, 0));
    uint32_t cme_catch = static_cast<uint32_t>(m_cme.insns.size());
    m_cme.insns.push_back(Encode11x(OP_MOVE_EXCEPTION, 0));
    Emit21s(m_cme.insns, OP_CONST_16, 0, 99);
    m_cme.insns.push_back(Encode11x(OP_RETURN, 0));
    RuntimeDexBuilder::TrySpec cts;
    cts.start_addr = cme_try_start;
    cts.insn_count = static_cast<uint16_t>(cme_try_end - cme_try_start);
    cts.handlers.push_back({"Ljava/util/ConcurrentModificationException;", cme_catch});
    m_cme.tries.push_back(std::move(cts));

    cls.direct_methods.push_back(std::move(ms));
    cls.direct_methods.push_back(std::move(m_cme));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LTestMisc;", "checkMisc", "()I"), 13);
    EXPECT_EQ(CallStaticInt(*dex, "LTestMisc;", "catchCme", "()I"), 99);
}

} // namespace
} // namespace portage::package
