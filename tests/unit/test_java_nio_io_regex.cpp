#include "fs/vfs.hpp"
#include "java_runtime_dex_builder.hpp"
#include "ndk/android_log.hpp"
#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "package/java_runtime.hpp"
#include "package/jni_env.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace portage::package {
namespace {

using test_util::Emit21c;
using test_util::Emit21s;
using test_util::Emit22c;
using test_util::Emit23x;
using test_util::Emit35c;
using test_util::Encode11n;
using test_util::Encode11x;
using test_util::Encode12x;
using test_util::RuntimeDexBuilder;

class JavaNioIoRegexTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        JniEnvironment::Instance().Reset();
        DexVm::Instance().Reset();
        JavaRuntime::Instance().Reset();
    }

    void TearDown() override
    {
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
                                 std::string_view sig)
    {
        const DexMethodDef *m = dex.FindMethod(cls, name, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return -1;
        }
        DexValue ret = DexVm::Instance().ExecuteMethod(&dex, m, {});
        return static_cast<int32_t>(ret.raw);
    }

    static jobject CallStaticObj(const DexFile &dex, std::string_view cls, std::string_view name,
                                 std::string_view sig)
    {
        const DexMethodDef *m = dex.FindMethod(cls, name, sig);
        EXPECT_NE(m, nullptr);
        if (!m) {
            return nullptr;
        }
        DexValue ret = DexVm::Instance().ExecuteMethod(&dex, m, {});
        return reinterpret_cast<jobject>(static_cast<uintptr_t>(ret.raw));
    }
};

TEST_F(JavaNioIoRegexTest, ByteBufferAllocateWrapDirectAndTypedViews)
{
    RuntimeDexBuilder db;
    uint32_t m_alloc =
        db.AddMethod("Ljava/nio/ByteBuffer;", "allocate", "Ljava/nio/ByteBuffer;", {"I"});
    uint32_t m_alloc_dir =
        db.AddMethod("Ljava/nio/ByteBuffer;", "allocateDirect", "Ljava/nio/ByteBuffer;", {"I"});
    uint32_t m_order = db.AddMethod("Ljava/nio/ByteBuffer;", "order", "Ljava/nio/ByteBuffer;",
                                    {"Ljava/nio/ByteOrder;"});
    uint32_t m_put_int =
        db.AddMethod("Ljava/nio/ByteBuffer;", "putInt", "Ljava/nio/ByteBuffer;", {"I"});
    uint32_t m_flip = db.AddMethod("Ljava/nio/ByteBuffer;", "flip", "Ljava/nio/Buffer;", {});
    uint32_t m_as_int =
        db.AddMethod("Ljava/nio/ByteBuffer;", "asIntBuffer", "Ljava/nio/IntBuffer;", {});
    uint32_t m_ib_get = db.AddMethod("Ljava/nio/IntBuffer;", "get", "I", {"I"});
    uint32_t m_bb_get_int = db.AddMethod("Ljava/nio/ByteBuffer;", "getInt", "I", {});
    uint32_t f_le = db.AddField("Ljava/nio/ByteOrder;", "LITTLE_ENDIAN", "Ljava/nio/ByteOrder;");

    auto &cls = db.AddClass("LNioDemo;", "Ljava/lang/Object;");

    RuntimeDexBuilder::MethodSpec fn;
    fn.class_idx = cls.class_idx;
    fn.method_idx = db.AddMethod("LNioDemo;", "testHeap", "I", {});
    fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    fn.registers_size = 4;
    // bb = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
    Emit21s(fn.insns, OP_CONST_16, 1, 8);
    Emit35c(fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_alloc), {1});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit21c(fn.insns, OP_SGET_OBJECT, 1, static_cast<uint16_t>(f_le));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_order), {0, 1});
    // bb.putInt(0x11223344); bb.putInt(0x01020304); bb.flip();
    db.AddProto("V", {});
    Emit21s(fn.insns, OP_CONST_16, 1, 100);
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_put_int), {0, 1});
    Emit21s(fn.insns, OP_CONST_16, 1, 250);
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_put_int), {0, 1});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_flip), {0});
    // ib = bb.asIntBuffer(); return ib.get(0) + ib.get(1) + bb.getInt() = 100 + 250 + 100 = 450
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_as_int), {0});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    fn.insns.push_back(Encode11n(OP_CONST_4, 2, 0));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ib_get), {1, 2});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 2));
    fn.insns.push_back(Encode11n(OP_CONST_4, 3, 1));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ib_get), {1, 3});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    Emit23x(fn.insns, OP_ADD_INT, 2, 2, 3);
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_bb_get_int), {0});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    Emit23x(fn.insns, OP_ADD_INT, 2, 2, 3);
    fn.insns.push_back(Encode11x(OP_RETURN, 2));
    cls.direct_methods.push_back(std::move(fn));

    RuntimeDexBuilder::MethodSpec dir_fn;
    dir_fn.class_idx = cls.class_idx;
    dir_fn.method_idx = db.AddMethod("LNioDemo;", "makeDirect", "Ljava/nio/ByteBuffer;", {});
    dir_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    dir_fn.registers_size = 2;
    Emit21s(dir_fn.insns, OP_CONST_16, 0, 32);
    Emit35c(dir_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_alloc_dir), {0});
    dir_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit21s(dir_fn.insns, OP_CONST_16, 1, 777);
    Emit35c(dir_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_put_int), {0, 1});
    dir_fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(dir_fn));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(CallStaticInt(*dex, "LNioDemo;", "testHeap", "()I"), 450);

    jobject dir_buf = CallStaticObj(*dex, "LNioDemo;", "makeDirect", "()Ljava/nio/ByteBuffer;");
    ASSERT_NE(dir_buf, nullptr);
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    EXPECT_EQ(env->functions->GetDirectBufferCapacity(env, dir_buf), 32);
    void *ptr = env->functions->GetDirectBufferAddress(env, dir_buf);
    ASSERT_NE(ptr, nullptr);
    uint8_t b0 = 0;
    uint8_t b3 = 0;
    std::memcpy(&b0, static_cast<uint8_t *>(ptr) + 2, 1);
    std::memcpy(&b3, static_cast<uint8_t *>(ptr) + 3, 1);
    uint32_t be_val = (static_cast<uint32_t>(b0) << 8) | b3;
    EXPECT_EQ(be_val, 777u);
}

TEST_F(JavaNioIoRegexTest, CharsetsAndStringEncodingRoundTrip)
{
    RuntimeDexBuilder db;
    uint32_t f_utf8 =
        db.AddField("Ljava/nio/charset/StandardCharsets;", "UTF_8", "Ljava/nio/charset/Charset;");
    uint32_t f_utf16le = db.AddField("Ljava/nio/charset/StandardCharsets;", "UTF_16LE",
                                     "Ljava/nio/charset/Charset;");
    uint32_t m_get_bytes =
        db.AddMethod("Ljava/lang/String;", "getBytes", "[B", {"Ljava/nio/charset/Charset;"});
    uint32_t m_str_cs =
        db.AddMethod("Ljava/lang/String;", "<init>", "V", {"[B", "Ljava/nio/charset/Charset;"});
    uint32_t t_str = db.AddType("Ljava/lang/String;");
    uint32_t s_hello = db.AddString("abc");

    auto &cls = db.AddClass("LCsDemo;", "Ljava/lang/Object;");
    RuntimeDexBuilder::MethodSpec fn;
    fn.class_idx = cls.class_idx;
    fn.method_idx = db.AddMethod("LCsDemo;", "roundTrip", "Ljava/lang/String;", {});
    fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    fn.registers_size = 4;
    Emit21c(fn.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_hello));
    Emit21c(fn.insns, OP_SGET_OBJECT, 1, static_cast<uint16_t>(f_utf16le));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_bytes), {0, 1});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    // Verify UTF-16LE byte length of "abc" is 6
    fn.insns.push_back(Encode12x(OP_ARRAY_LENGTH, 3, 2));
    Emit21c(fn.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_str));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_str_cs), {0, 2, 1});
    fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(fn));
    (void)f_utf8;

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LCsDemo;", "roundTrip", "()Ljava/lang/String;")),
              "abc");
}

TEST_F(JavaNioIoRegexTest, VfsFileStreamsReadersWritersAndSystemOut)
{
    auto temp_dir = std::filesystem::temp_directory_path() / "portage_java_io_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    fs::Vfs local_vfs;
    fs::Vfs *prev_vfs = syscall::SyscallDispatcher::GetVfs();
    fs::Vfs *vfs = prev_vfs ? prev_vfs : &local_vfs;
    if (!prev_vfs) {
        syscall::SyscallDispatcher::Initialize(nullptr, vfs, nullptr);
    }
    ASSERT_TRUE(vfs->AddMount("/data/local/tmp/jio", temp_dir));

    RuntimeDexBuilder db;
    uint32_t t_file = db.AddType("Ljava/io/File;");
    uint32_t t_fos = db.AddType("Ljava/io/FileOutputStream;");
    uint32_t t_fis = db.AddType("Ljava/io/FileInputStream;");
    uint32_t t_isr = db.AddType("Ljava/io/InputStreamReader;");
    uint32_t t_br = db.AddType("Ljava/io/BufferedReader;");

    uint32_t m_file_init = db.AddMethod("Ljava/io/File;", "<init>", "V", {"Ljava/lang/String;"});
    uint32_t m_fos_init =
        db.AddMethod("Ljava/io/FileOutputStream;", "<init>", "V", {"Ljava/io/File;"});
    uint32_t m_fos_write = db.AddMethod("Ljava/io/FileOutputStream;", "write", "V", {"[B"});
    uint32_t m_fos_close = db.AddMethod("Ljava/io/FileOutputStream;", "close", "V", {});
    uint32_t m_fis_init =
        db.AddMethod("Ljava/io/FileInputStream;", "<init>", "V", {"Ljava/io/File;"});
    uint32_t m_isr_init =
        db.AddMethod("Ljava/io/InputStreamReader;", "<init>", "V", {"Ljava/io/InputStream;"});
    uint32_t m_br_init =
        db.AddMethod("Ljava/io/BufferedReader;", "<init>", "V", {"Ljava/io/Reader;"});
    uint32_t m_br_readline =
        db.AddMethod("Ljava/io/BufferedReader;", "readLine", "Ljava/lang/String;", {});
    uint32_t m_br_close = db.AddMethod("Ljava/io/BufferedReader;", "close", "V", {});
    uint32_t m_get_bytes = db.AddMethod("Ljava/lang/String;", "getBytes", "[B", {});
    uint32_t f_sys_out = db.AddField("Ljava/lang/System;", "out", "Ljava/io/PrintStream;");
    uint32_t m_println =
        db.AddMethod("Ljava/io/PrintStream;", "println", "V", {"Ljava/lang/String;"});

    uint32_t s_path = db.AddString("/data/local/tmp/jio/note.txt");
    uint32_t s_line = db.AddString("hello-vfs-io\nsecond");
    uint32_t s_log = db.AddString("portage-sysout-marker");

    auto &cls = db.AddClass("LIoDemo;", "Ljava/lang/Object;");
    RuntimeDexBuilder::MethodSpec fn;
    fn.class_idx = cls.class_idx;
    fn.method_idx = db.AddMethod("LIoDemo;", "writeAndRead", "Ljava/lang/String;", {});
    fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    fn.registers_size = 5;

    Emit21c(fn.insns, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_file));
    Emit21c(fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_path));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_file_init), {0, 1});

    Emit21c(fn.insns, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_fos));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_fos_init), {1, 0});
    Emit21c(fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_line));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_bytes), {2});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_fos_write), {1, 2});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_fos_close), {1});

    Emit21c(fn.insns, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_fis));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_fis_init), {1, 0});
    Emit21c(fn.insns, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_isr));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_isr_init), {2, 1});
    Emit21c(fn.insns, OP_NEW_INSTANCE, 3, static_cast<uint16_t>(t_br));
    Emit35c(fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_br_init), {3, 2});
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_br_readline), {3});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 4));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_br_close), {3});

    Emit21c(fn.insns, OP_SGET_OBJECT, 0, static_cast<uint16_t>(f_sys_out));
    Emit21c(fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_log));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_println), {0, 1});

    fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 4));
    cls.direct_methods.push_back(std::move(fn));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(
        ReadJavaString(CallStaticObj(*dex, "LIoDemo;", "writeAndRead", "()Ljava/lang/String;")),
        "hello-vfs-io");
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "note.txt"));

    auto logs = ndk::LogcatBuffer::Instance().GetEntries();
    bool found_marker = false;
    for (const auto &entry : logs) {
        if (entry.message.find("portage-sysout-marker") != std::string::npos) {
            found_marker = true;
            break;
        }
    }
    EXPECT_TRUE(found_marker);

    vfs->RemoveMount("/data/local/tmp/jio");
    if (!prev_vfs) {
        syscall::SyscallDispatcher::Shutdown();
    }
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}

TEST_F(JavaNioIoRegexTest, PatternMatcherAndStringRegexMethods)
{
    RuntimeDexBuilder db;
    uint32_t m_compile = db.AddMethod("Ljava/util/regex/Pattern;", "compile",
                                      "Ljava/util/regex/Pattern;", {"Ljava/lang/String;"});
    uint32_t m_matcher = db.AddMethod("Ljava/util/regex/Pattern;", "matcher",
                                      "Ljava/util/regex/Matcher;", {"Ljava/lang/CharSequence;"});
    uint32_t m_find = db.AddMethod("Ljava/util/regex/Matcher;", "find", "Z", {});
    uint32_t m_group =
        db.AddMethod("Ljava/util/regex/Matcher;", "group", "Ljava/lang/String;", {"I"});
    uint32_t m_matches = db.AddMethod("Ljava/lang/String;", "matches", "Z", {"Ljava/lang/String;"});
    uint32_t m_rep_all = db.AddMethod("Ljava/lang/String;", "replaceAll", "Ljava/lang/String;",
                                      {"Ljava/lang/String;", "Ljava/lang/String;"});

    uint32_t s_pat = db.AddString("([a-z]+)-([0-9]+)");
    uint32_t s_in = db.AddString("id=item-42!");
    uint32_t s_dig = db.AddString("\\d+");
    uint32_t s_num = db.AddString("1234");
    uint32_t s_x = db.AddString("X");

    auto &cls = db.AddClass("LRegexDemo;", "Ljava/lang/Object;");
    RuntimeDexBuilder::MethodSpec fn;
    fn.class_idx = cls.class_idx;
    fn.method_idx = db.AddMethod("LRegexDemo;", "run", "Ljava/lang/String;", {});
    fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    fn.registers_size = 5;

    Emit21c(fn.insns, OP_CONST_STRING, 0, static_cast<uint16_t>(s_num));
    Emit21c(fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_dig));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_matches), {0, 1});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));

    Emit21c(fn.insns, OP_CONST_STRING, 1, static_cast<uint16_t>(s_pat));
    Emit35c(fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_compile), {1});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit21c(fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_in));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_matcher), {1, 2});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_find), {1});
    fn.insns.push_back(Encode11n(OP_CONST_4, 2, 2));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_group), {1, 2});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));

    Emit21c(fn.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_dig));
    Emit21c(fn.insns, OP_CONST_STRING, 4, static_cast<uint16_t>(s_x));
    Emit35c(fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rep_all), {2, 3, 4});
    fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    fn.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back(std::move(fn));

    auto raw = db.Build();
    auto dex = DexFile::Parse(raw);
    ASSERT_NE(dex, nullptr);
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));

    EXPECT_EQ(ReadJavaString(CallStaticObj(*dex, "LRegexDemo;", "run", "()Ljava/lang/String;")),
              "X");
}

} // namespace
} // namespace portage::package
