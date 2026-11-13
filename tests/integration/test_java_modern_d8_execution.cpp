#include <windows.h>

#include "../unit/java_runtime_dex_builder.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

namespace stdfs = std::filesystem;
using namespace portage::package;
using test_util::Align4;
using test_util::AppendU16;
using test_util::AppendU32;
using test_util::Emit21c;
using test_util::Emit21s;
using test_util::Emit22c;
using test_util::Emit23x;
using test_util::Emit35c;
using test_util::Encode10x;
using test_util::Encode11n;
using test_util::Encode11x;
using test_util::RuntimeDexBuilder;

struct ProcessResult {
    DWORD exit_code{static_cast<DWORD>(-1)};
    std::string output;
};

stdfs::path FindPortageExe()
{
    stdfs::path cur = stdfs::current_path();
    for (int i = 0; i < 6; ++i) {
        stdfs::path cand = cur / "src" / "portage.exe";
        if (stdfs::exists(cand)) {
            return cand;
        }
        cand = cur / "build" / "debug" / "src" / "portage.exe";
        if (stdfs::exists(cand)) {
            return cand;
        }
        if (!cur.has_parent_path()) {
            break;
        }
        cur = cur.parent_path();
    }
    return {};
}

ProcessResult RunPortageHeadless(const std::string &args)
{
    ProcessResult res{};
    stdfs::path exe = FindPortageExe();
    if (exe.empty()) {
        return res;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return res;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + exe.string() + "\" " + args;
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return res;
    }

    char buf[4096];
    DWORD read_bytes = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &read_bytes, nullptr) && read_bytes > 0) {
        res.output.append(buf, read_bytes);
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &res.exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

std::vector<uint8_t> BuildJavaManifest(const std::string &pkg_name, const std::string &act_name)
{
    std::vector<std::string> pool = {
        "manifest",
        "package",
        pkg_name,
        "application",
        "activity",
        "name",
        act_name,
        "intent-filter",
        "action",
        "android.intent.action.MAIN",
        "category",
        "android.intent.category.LAUNCHER",
        "http://schemas.android.com/apk/res/android",
    };
    auto find_idx = [&](const std::string &s) -> uint32_t {
        for (uint32_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == s) {
                return i;
            }
        }
        return 0xFFFFFFFFu;
    };

    std::vector<uint8_t> sp_strings;
    std::vector<uint32_t> sp_offs;
    for (const auto &s : pool) {
        sp_offs.push_back(static_cast<uint32_t>(sp_strings.size()));
        AppendU16(sp_strings, static_cast<uint16_t>(s.size()));
        for (char c : s) {
            AppendU16(sp_strings, static_cast<uint16_t>(static_cast<uint8_t>(c)));
        }
        AppendU16(sp_strings, 0);
    }
    Align4(sp_strings);

    std::vector<uint8_t> sp_chunk;
    uint32_t sp_size =
        28 + static_cast<uint32_t>(pool.size() * 4) + static_cast<uint32_t>(sp_strings.size());
    AppendU16(sp_chunk, 0x0001);
    AppendU16(sp_chunk, 28);
    AppendU32(sp_chunk, sp_size);
    AppendU32(sp_chunk, static_cast<uint32_t>(pool.size()));
    AppendU32(sp_chunk, 0);
    AppendU32(sp_chunk, 0);
    AppendU32(sp_chunk, 28 + static_cast<uint32_t>(pool.size() * 4));
    AppendU32(sp_chunk, 0);
    for (uint32_t o : sp_offs) {
        AppendU32(sp_chunk, o);
    }
    sp_chunk.insert(sp_chunk.end(), sp_strings.begin(), sp_strings.end());

    std::vector<uint8_t> body;
    auto push_start = [&](const std::string &tag,
                          const std::vector<std::pair<std::string, std::string>> &attrs) {
        uint32_t sz = 36 + static_cast<uint32_t>(attrs.size() * 20);
        AppendU16(body, 0x0102);
        AppendU16(body, 16);
        AppendU32(body, sz);
        AppendU32(body, 1);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, find_idx(tag));
        AppendU16(body, 20);
        AppendU16(body, 20);
        AppendU16(body, static_cast<uint16_t>(attrs.size()));
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0);
        for (const auto &[k, v] : attrs) {
            AppendU32(body, find_idx("http://schemas.android.com/apk/res/android"));
            AppendU32(body, find_idx(k));
            uint32_t vi = find_idx(v);
            AppendU32(body, vi);
            AppendU16(body, 8);
            body.push_back(0);
            body.push_back(0x03);
            AppendU32(body, vi);
        }
    };
    auto push_end = [&](const std::string &tag) {
        AppendU16(body, 0x0103);
        AppendU16(body, 16);
        AppendU32(body, 24);
        AppendU32(body, 1);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, find_idx(tag));
    };

    push_start("manifest", {{"package", pkg_name}});
    push_start("application", {});
    push_start("activity", {{"name", act_name}});
    push_start("intent-filter", {});
    push_start("action", {{"name", "android.intent.action.MAIN"}});
    push_end("action");
    push_start("category", {{"name", "android.intent.category.LAUNCHER"}});
    push_end("category");
    push_end("intent-filter");
    push_end("activity");
    push_end("application");
    push_end("manifest");

    std::vector<uint8_t> doc;
    uint32_t total = 8 + static_cast<uint32_t>(sp_chunk.size() + body.size());
    AppendU16(doc, 0x0003);
    AppendU16(doc, 8);
    AppendU32(doc, total);
    doc.insert(doc.end(), sp_chunk.begin(), sp_chunk.end());
    doc.insert(doc.end(), body.begin(), body.end());
    return doc;
}

bool WriteZipApk(const stdfs::path &dest,
                 const std::vector<std::pair<std::string, std::vector<uint8_t>>> &entries)
{
    std::vector<uint8_t> archive;
    struct CentralRecord {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };
    std::vector<CentralRecord> records;

    for (const auto &[name, data] : entries) {
        uint32_t off = static_cast<uint32_t>(archive.size());
        uint32_t crc = static_cast<uint32_t>(
            crc32(0, data.empty() ? nullptr : data.data(), static_cast<uInt>(data.size())));
        uint32_t sz = static_cast<uint32_t>(data.size());
        records.push_back({name, crc, sz, off});

        AppendU32(archive, 0x04034B50u);
        AppendU16(archive, 20);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, crc);
        AppendU32(archive, sz);
        AppendU32(archive, sz);
        AppendU16(archive, static_cast<uint16_t>(name.size()));
        AppendU16(archive, 0);
        archive.insert(archive.end(), name.begin(), name.end());
        archive.insert(archive.end(), data.begin(), data.end());
    }

    uint32_t cd_off = static_cast<uint32_t>(archive.size());
    for (const auto &r : records) {
        AppendU32(archive, 0x02014B50u);
        AppendU16(archive, 20);
        AppendU16(archive, 20);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, r.crc);
        AppendU32(archive, r.size);
        AppendU32(archive, r.size);
        AppendU16(archive, static_cast<uint16_t>(r.name.size()));
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, 0);
        AppendU32(archive, r.offset);
        archive.insert(archive.end(), r.name.begin(), r.name.end());
    }
    uint32_t cd_sz = static_cast<uint32_t>(archive.size()) - cd_off;
    AppendU32(archive, 0x06054B50u);
    AppendU16(archive, 0);
    AppendU16(archive, 0);
    AppendU16(archive, static_cast<uint16_t>(records.size()));
    AppendU16(archive, static_cast<uint16_t>(records.size()));
    AppendU32(archive, cd_sz);
    AppendU32(archive, cd_off);
    AppendU16(archive, 0);

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(archive.data()),
              static_cast<std::streamsize>(archive.size()));
    return out.good();
}

TEST(JavaModernD8Execution, HeadlessActivityExercisesD8InvokeCustomNioIoRegexAndResources)
{
    RuntimeDexBuilder db;
    db.SetDexVersion("039");

    uint32_t t_act = db.AddType("Lcom/portage/d8modern/MainActivity;");
    uint32_t t_str = db.AddType("Ljava/lang/String;");
    uint32_t t_barr = db.AddType("[B");
    uint32_t t_file = db.AddType("Ljava/io/File;");
    uint32_t t_fos = db.AddType("Ljava/io/FileOutputStream;");
    uint32_t t_fis = db.AddType("Ljava/io/FileInputStream;");
    uint32_t t_isr = db.AddType("Ljava/io/InputStreamReader;");
    uint32_t t_br = db.AddType("Ljava/io/BufferedReader;");

    uint32_t m_act_super = db.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    uint32_t m_act_oncreate_super =
        db.AddMethod("Landroid/app/Activity;", "onCreate", "V", {"Landroid/os/Bundle;"});
    uint32_t m_finish = db.AddMethod("Landroid/app/Activity;", "finish", "V", {});

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
    uint32_t m_lambda =
        db.AddMethod("Lcom/portage/d8modern/MainActivity;", "addBias", "I", {"I", "I"});
    uint32_t m_apply =
        db.AddMethod("Ljava/util/function/IntUnaryOperator;", "applyAsInt", "I", {"I"});

    uint32_t m_get_res = db.AddMethod("Ljava/lang/Class;", "getResourceAsStream",
                                      "Ljava/io/InputStream;", {"Ljava/lang/String;"});
    uint32_t m_in_read = db.AddMethod("Ljava/io/InputStream;", "read", "I", {"[B"});
    uint32_t m_str_bytes = db.AddMethod("Ljava/lang/String;", "<init>", "V", {"[B", "I", "I"});
    uint32_t m_compile = db.AddMethod("Ljava/util/regex/Pattern;", "compile",
                                      "Ljava/util/regex/Pattern;", {"Ljava/lang/String;"});
    uint32_t m_matcher = db.AddMethod("Ljava/util/regex/Pattern;", "matcher",
                                      "Ljava/util/regex/Matcher;", {"Ljava/lang/CharSequence;"});
    uint32_t m_find = db.AddMethod("Ljava/util/regex/Matcher;", "find", "Z", {});
    uint32_t m_group =
        db.AddMethod("Ljava/util/regex/Matcher;", "group", "Ljava/lang/String;", {"I"});

    uint32_t m_alloc_dir =
        db.AddMethod("Ljava/nio/ByteBuffer;", "allocateDirect", "Ljava/nio/ByteBuffer;", {"I"});
    uint32_t m_order = db.AddMethod("Ljava/nio/ByteBuffer;", "order", "Ljava/nio/ByteBuffer;",
                                    {"Ljava/nio/ByteOrder;"});
    uint32_t m_put_int =
        db.AddMethod("Ljava/nio/ByteBuffer;", "putInt", "Ljava/nio/ByteBuffer;", {"I"});
    uint32_t m_flip = db.AddMethod("Ljava/nio/ByteBuffer;", "flip", "Ljava/nio/Buffer;", {});
    uint32_t m_get_int = db.AddMethod("Ljava/nio/ByteBuffer;", "getInt", "I", {});
    uint32_t f_le = db.AddField("Ljava/nio/ByteOrder;", "LITTLE_ENDIAN", "Ljava/nio/ByteOrder;");

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

    uint32_t p_sam = db.AddProto("I", {"I"});
    uint32_t p_factory = db.AddProto("Ljava/util/function/IntUnaryOperator;", {"I"});
    uint32_t p_concat = db.AddProto("Ljava/lang/String;", {"I", "I", "Ljava/lang/String;"});

    uint32_t mh_lmf =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_lmf));
    uint32_t mh_scf =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_scf));
    uint32_t mh_impl =
        db.AddMethodHandle(DexMethodHandleType::InvokeStatic, static_cast<uint16_t>(m_lambda));

    stdfs::path temp_dir = stdfs::temp_directory_path() / "portage_d8_modern_e2e";
    stdfs::remove_all(temp_dir);
    stdfs::create_directories(temp_dir);
    stdfs::path out_file_path = temp_dir / "out.txt";

    uint32_t s_apply = db.AddString("applyAsInt");
    uint32_t s_make = db.AddString("makeConcatWithConstants");
    uint32_t s_recipe = db.AddString("D8_E2E_OK:lam=\x01,nio=\x01,cfg=\x01");
    uint32_t s_res_path = db.AddString("/assets/d8_config.txt");
    uint32_t s_pat = db.AddString("cfg=(\\d+)");
    uint32_t s_out_path = db.AddString(out_file_path.generic_string());

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

    auto &cls = db.AddClass("Lcom/portage/d8modern/MainActivity;", "Landroid/app/Activity;");

    RuntimeDexBuilder::MethodSpec init_fn;
    init_fn.class_idx = cls.class_idx;
    init_fn.method_idx = db.AddMethod("Lcom/portage/d8modern/MainActivity;", "<init>", "V", {});
    init_fn.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
    init_fn.registers_size = 1;
    init_fn.ins_size = 1;
    Emit35c(init_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_act_super), {0});
    init_fn.insns.push_back(Encode10x(OP_RETURN_VOID));
    cls.direct_methods.push_back(std::move(init_fn));

    RuntimeDexBuilder::MethodSpec lam_fn;
    lam_fn.class_idx = cls.class_idx;
    lam_fn.method_idx = m_lambda;
    lam_fn.access_flags = ACC_PUBLIC | ACC_STATIC;
    lam_fn.registers_size = 3;
    lam_fn.ins_size = 2;
    Emit23x(lam_fn.insns, OP_ADD_INT, 0, 1, 2);
    lam_fn.insns.push_back(Encode11x(OP_RETURN, 0));
    cls.direct_methods.push_back(std::move(lam_fn));

    RuntimeDexBuilder::MethodSpec oncreate_fn;
    oncreate_fn.class_idx = cls.class_idx;
    oncreate_fn.method_idx = db.AddMethod("Lcom/portage/d8modern/MainActivity;", "onCreate", "V",
                                          {"Landroid/os/Bundle;"});
    oncreate_fn.access_flags = ACC_PUBLIC;
    oncreate_fn.registers_size = 10;
    oncreate_fn.ins_size = 2;
    // v8=this, v9=savedInstanceState
    Emit35c(oncreate_fn.insns, OP_INVOKE_SUPER, static_cast<uint16_t>(m_act_oncreate_super),
            {8, 9});

    // v0 = lambda(100).applyAsInt(23) -> 123
    Emit21s(oncreate_fn.insns, OP_CONST_16, 0, 100);
    Emit35c(oncreate_fn.insns, OP_INVOKE_CUSTOM, static_cast<uint16_t>(cs_lambda), {0});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit21s(oncreate_fn.insns, OP_CONST_16, 1, 23);
    Emit35c(oncreate_fn.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_apply), {0, 1});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));

    // v1 = ByteBuffer.allocateDirect(8).order(LE).putInt(42).flip().getInt() -> 42
    Emit21s(oncreate_fn.insns, OP_CONST_16, 2, 8);
    Emit35c(oncreate_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_alloc_dir), {2});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));
    Emit21c(oncreate_fn.insns, OP_SGET_OBJECT, 2, static_cast<uint16_t>(f_le));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_order), {1, 2});
    Emit21s(oncreate_fn.insns, OP_CONST_16, 2, 42);
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_put_int), {1, 2});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_flip), {1});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_int), {1});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 1));

    // v2 = regex group(1) from MainActivity.class.getResourceAsStream("/assets/d8_config.txt") ->
    // "99"
    Emit21c(oncreate_fn.insns, OP_CONST_CLASS, 2, static_cast<uint16_t>(t_act));
    Emit21c(oncreate_fn.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_res_path));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_res), {2, 3});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit21s(oncreate_fn.insns, OP_CONST_16, 3, 32);
    Emit22c(oncreate_fn.insns, OP_NEW_ARRAY, 4, 3, static_cast<uint16_t>(t_barr));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_in_read), {2, 4});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));
    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_str));
    oncreate_fn.insns.push_back(Encode11n(OP_CONST_4, 5, 0));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_str_bytes), {2, 4, 5, 3});
    Emit21c(oncreate_fn.insns, OP_CONST_STRING, 3, static_cast<uint16_t>(s_pat));
    Emit35c(oncreate_fn.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_compile), {3});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_matcher), {3, 2});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_find), {2});
    oncreate_fn.insns.push_back(Encode11n(OP_CONST_4, 3, 1));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_group), {2, 3});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));

    // v0 = StringConcatFactory(v0, v1, v2) -> "D8_E2E_OK:lam=123,nio=42,cfg=99"
    Emit35c(oncreate_fn.insns, OP_INVOKE_CUSTOM, static_cast<uint16_t>(cs_concat), {0, 1, 2});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));

    // Write v0 to /data/data/com.portage.d8modern/out.txt, read it back, System.out.println,
    // finish()
    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_file));
    Emit21c(oncreate_fn.insns, OP_CONST_STRING, 2, static_cast<uint16_t>(s_out_path));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_file_init), {1, 2});
    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_fos));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_fos_init), {2, 1});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_get_bytes), {0});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_fos_write), {2, 3});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_fos_close), {2});

    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_fis));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_fis_init), {2, 1});
    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 3, static_cast<uint16_t>(t_isr));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_isr_init), {3, 2});
    Emit21c(oncreate_fn.insns, OP_NEW_INSTANCE, 4, static_cast<uint16_t>(t_br));
    Emit35c(oncreate_fn.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_br_init), {4, 3});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_br_readline), {4});
    oncreate_fn.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_br_close), {4});

    Emit21c(oncreate_fn.insns, OP_SGET_OBJECT, 1, static_cast<uint16_t>(f_sys_out));
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_println), {1, 0});
    Emit35c(oncreate_fn.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_finish), {8});
    oncreate_fn.insns.push_back(Encode10x(OP_RETURN_VOID));
    cls.virtual_methods.push_back(std::move(oncreate_fn));

    auto dex_bytes = db.Build();
    auto manifest_bytes = BuildJavaManifest("com.portage.d8modern", ".MainActivity");
    const std::string cfg_str = "cfg=99";
    std::vector<uint8_t> cfg_bytes(cfg_str.begin(), cfg_str.end());

    stdfs::path apk_path = temp_dir / "d8_modern.apk";
    ASSERT_TRUE(WriteZipApk(apk_path, {{"AndroidManifest.xml", manifest_bytes},
                                       {"classes.dex", dex_bytes},
                                       {"assets/d8_config.txt", cfg_bytes}}));

    ProcessResult proc =
        RunPortageHeadless("--verbose --headless run \"" + apk_path.string() + "\"");
    EXPECT_TRUE(stdfs::exists(out_file_path)) << proc.output;
    EXPECT_EQ(proc.exit_code, 0u) << proc.output;
    EXPECT_NE(proc.output.find("D8_E2E_OK:lam=123,nio=42,cfg=99"), std::string::npos)
        << proc.output;

    std::error_code ec;
    stdfs::remove_all(temp_dir, ec);
}

} // namespace
