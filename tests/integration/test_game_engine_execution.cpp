#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/android_log.hpp"
#include "ndk/input_queue.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"
#include "package/native_activity.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

inline void WriteU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

inline void WriteU16(std::vector<uint8_t> &buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void WriteU32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

stdfs::path FindGuestBinary(const std::string &filename)
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("guests") / filename,
        stdfs::path("tests/guests") / filename,
        stdfs::current_path() / "guests" / filename,
        stdfs::current_path() / "tests" / "guests" / filename,
        stdfs::current_path() / "build" / "debug" / "tests" / "guests" / filename,
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return stdfs::path("guests") / filename;
}

static stdfs::path FindPortageExecutable()
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("src") / "portage.exe",
        stdfs::path("../src") / "portage.exe",
        stdfs::path("build/debug/src") / "portage.exe",
        stdfs::current_path() / "src" / "portage.exe",
        stdfs::current_path() / ".." / "src" / "portage.exe",
        stdfs::current_path() / "build" / "debug" / "src" / "portage.exe",
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return "";
}

std::vector<uint8_t> BuildEngineManifest(const std::string &pkg_name, const std::string &lib_name)
{
    std::vector<std::string> string_pool = {
        "manifest",                   // 0
        "package",                    // 1
        pkg_name,                     // 2
        "versionCode",                // 3
        "versionName",                // 4
        "1.0.0",                      // 5
        "uses-sdk",                   // 6
        "minSdkVersion",              // 7
        "targetSdkVersion",           // 8
        "application",                // 9
        "label",                      // 10
        "Portage Engine App",         // 11
        "hasCode",                    // 12
        "activity",                   // 13
        "name",                       // 14
        "android.app.NativeActivity", // 15
        "meta-data",                  // 16
        "value",                      // 17
        "android.app.lib_name",       // 18
        lib_name                      // 19
    };

    std::vector<uint8_t> doc;
    WriteU16(doc, 0x0003); // CHUNK_AXML_ROOT
    WriteU16(doc, 0x0008);
    WriteU32(doc, 0);

    uint32_t str_pool_start = static_cast<uint32_t>(doc.size());
    WriteU16(doc, 0x0001); // CHUNK_STRING_POOL
    WriteU16(doc, 0x001C);
    WriteU32(doc, 0);
    WriteU32(doc, static_cast<uint32_t>(string_pool.size()));
    WriteU32(doc, 0);
    WriteU32(doc, 1 << 8); // UTF-8
    WriteU32(doc, 28 + static_cast<uint32_t>(string_pool.size()) * 4);
    WriteU32(doc, 0);

    std::vector<uint32_t> offsets;
    std::vector<uint8_t> str_data;
    for (const auto &str : string_pool) {
        offsets.push_back(static_cast<uint32_t>(str_data.size()));
        str_data.push_back(static_cast<uint8_t>(str.size()));
        str_data.push_back(static_cast<uint8_t>(str.size()));
        for (char c : str)
            str_data.push_back(static_cast<uint8_t>(c));
        str_data.push_back(0);
    }
    while (str_data.size() % 4 != 0)
        str_data.push_back(0);

    for (uint32_t off : offsets)
        WriteU32(doc, off);
    doc.insert(doc.end(), str_data.begin(), str_data.end());

    uint32_t str_pool_len = static_cast<uint32_t>(doc.size() - str_pool_start);
    doc[str_pool_start + 4] = static_cast<uint8_t>(str_pool_len & 0xFF);
    doc[str_pool_start + 5] = static_cast<uint8_t>((str_pool_len >> 8) & 0xFF);
    doc[str_pool_start + 6] = static_cast<uint8_t>((str_pool_len >> 16) & 0xFF);
    doc[str_pool_start + 7] = static_cast<uint8_t>((str_pool_len >> 24) & 0xFF);

    auto emit_element = [&](uint32_t name_idx,
                            const std::vector<std::pair<uint32_t, uint32_t>> &attrs) {
        WriteU16(doc, 0x0102); // START_ELEMENT
        WriteU16(doc, 0x0010);
        WriteU32(doc, 36 + static_cast<uint32_t>(attrs.size()) * 20);
        WriteU32(doc, 1);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, name_idx);
        WriteU16(doc, 20);                                  // attr_start
        WriteU16(doc, 20);                                  // attr_size
        WriteU16(doc, static_cast<uint16_t>(attrs.size())); // attr_count
        WriteU16(doc, 0);
        WriteU16(doc, 0);
        WriteU16(doc, 0);

        for (const auto &[attr_name, attr_val] : attrs) {
            WriteU32(doc, 0xFFFFFFFF);
            WriteU32(doc, attr_name);
            WriteU32(doc, attr_val);
            WriteU16(doc, 0x0008);
            WriteU8(doc, 0);
            WriteU8(doc, 0x03); // TYPE_STRING
            WriteU32(doc, attr_val);
        }
    };

    emit_element(0, {{1, 2}, {3, 5}, {4, 5}});
    emit_element(6, {{7, 5}, {8, 5}});
    emit_element(9, {{10, 11}, {12, 5}});
    emit_element(13, {{14, 15}});
    emit_element(16, {{14, 18}, {17, 19}});

    for (int i = 0; i < 5; ++i) {
        WriteU16(doc, 0x0103); // END_ELEMENT
        WriteU16(doc, 0x0010);
        WriteU32(doc, 24);
        WriteU32(doc, 1);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0);
    }

    uint32_t total_len = static_cast<uint32_t>(doc.size());
    doc[4] = static_cast<uint8_t>(total_len & 0xFF);
    doc[5] = static_cast<uint8_t>((total_len >> 8) & 0xFF);
    doc[6] = static_cast<uint8_t>((total_len >> 16) & 0xFF);
    doc[7] = static_cast<uint8_t>((total_len >> 24) & 0xFF);

    return doc;
}

struct NativeLibraryEntry {
    std::string filename;
    std::vector<uint8_t> data;
};

static std::vector<uint8_t> BuildTestStringPool(const std::vector<std::string> &strings,
                                                bool is_utf8 = true)
{
    std::vector<uint8_t> pool;
    uint32_t string_count = static_cast<uint32_t>(strings.size());
    uint32_t style_count = 0;
    uint32_t flags = is_utf8 ? (1 << 8) : 0;
    uint32_t header_size = 28;
    uint32_t strings_start = header_size + string_count * 4;

    std::vector<uint8_t> str_data;
    std::vector<uint32_t> offsets;
    offsets.reserve(string_count);

    for (const auto &str : strings) {
        offsets.push_back(static_cast<uint32_t>(str_data.size()));
        if (is_utf8) {
            uint32_t u16_len = static_cast<uint32_t>(str.size());
            uint32_t u8_len = static_cast<uint32_t>(str.size());
            WriteU8(str_data, static_cast<uint8_t>(u16_len));
            WriteU8(str_data, static_cast<uint8_t>(u8_len));
            str_data.insert(str_data.end(), str.begin(), str.end());
            WriteU8(str_data, 0);
        }
    }

    while (str_data.size() % 4 != 0) {
        WriteU8(str_data, 0);
    }

    uint32_t total_size = strings_start + static_cast<uint32_t>(str_data.size());

    WriteU16(pool, 0x0001); // RES_STRING_POOL_TYPE
    WriteU16(pool, static_cast<uint16_t>(header_size));
    WriteU32(pool, total_size);
    WriteU32(pool, string_count);
    WriteU32(pool, style_count);
    WriteU32(pool, flags);
    WriteU32(pool, strings_start);
    WriteU32(pool, 0);

    for (uint32_t off : offsets) {
        WriteU32(pool, off);
    }

    pool.insert(pool.end(), str_data.begin(), str_data.end());
    return pool;
}

static std::vector<uint8_t> BuildTestResourceTable(const std::string &pkg_name)
{
    std::vector<uint8_t> table;

    std::vector<std::string> global_strings = {
        "Portage Game Engine",
        "Welcome to Portage!",
    };
    auto global_pool = BuildTestStringPool(global_strings, true);

    std::vector<std::string> type_strings = {"string", "integer", "bool"};
    auto type_pool = BuildTestStringPool(type_strings, true);

    std::vector<std::string> key_strings = {"app_name", "welcome_message", "game_difficulty",
                                            "sound_enabled"};
    auto key_pool = BuildTestStringPool(key_strings, true);

    // Type 1: string
    std::vector<uint8_t> type1_spec;
    WriteU16(type1_spec, 0x0202);
    WriteU16(type1_spec, 16);
    WriteU32(type1_spec, 16 + 2 * 4);
    WriteU8(type1_spec, 1);
    WriteU8(type1_spec, 0);
    WriteU16(type1_spec, 0);
    WriteU32(type1_spec, 2);
    WriteU32(type1_spec, 0);
    WriteU32(type1_spec, 0);

    uint32_t config_size = 56;
    uint32_t type1_hdr_size = 20 + config_size;
    uint32_t type1_entries_start = type1_hdr_size + 2 * 4;
    uint32_t entry_size = 16;
    uint32_t type1_total_size = type1_entries_start + 2 * entry_size;

    std::vector<uint8_t> type1_type;
    WriteU16(type1_type, 0x0201);
    WriteU16(type1_type, static_cast<uint16_t>(type1_hdr_size));
    WriteU32(type1_type, type1_total_size);
    WriteU8(type1_type, 1);
    WriteU8(type1_type, 0);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 2);
    WriteU32(type1_type, type1_entries_start);
    WriteU32(type1_type, config_size);
    for (size_t i = 0; i < (config_size - 4); ++i) {
        WriteU8(type1_type, 0);
    }
    WriteU32(type1_type, 0);
    WriteU32(type1_type, entry_size);

    WriteU16(type1_type, 8);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 0);
    WriteU16(type1_type, 8);
    WriteU8(type1_type, 0);
    WriteU8(type1_type, 0x03);
    WriteU32(type1_type, 0);

    WriteU16(type1_type, 8);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 1);
    WriteU16(type1_type, 8);
    WriteU8(type1_type, 0);
    WriteU8(type1_type, 0x03);
    WriteU32(type1_type, 1);

    // Type 2: integer
    std::vector<uint8_t> type2_spec;
    WriteU16(type2_spec, 0x0202);
    WriteU16(type2_spec, 16);
    WriteU32(type2_spec, 16 + 1 * 4);
    WriteU8(type2_spec, 2);
    WriteU8(type2_spec, 0);
    WriteU16(type2_spec, 0);
    WriteU32(type2_spec, 1);
    WriteU32(type2_spec, 0);

    uint32_t type2_entries_start = type1_hdr_size + 1 * 4;
    uint32_t type2_total_size = type2_entries_start + 1 * entry_size;

    std::vector<uint8_t> type2_type;
    WriteU16(type2_type, 0x0201);
    WriteU16(type2_type, static_cast<uint16_t>(type1_hdr_size));
    WriteU32(type2_type, type2_total_size);
    WriteU8(type2_type, 2);
    WriteU8(type2_type, 0);
    WriteU16(type2_type, 0);
    WriteU32(type2_type, 1);
    WriteU32(type2_type, type2_entries_start);
    WriteU32(type2_type, config_size);
    for (size_t i = 0; i < (config_size - 4); ++i) {
        WriteU8(type2_type, 0);
    }
    WriteU32(type2_type, 0);

    WriteU16(type2_type, 8);
    WriteU16(type2_type, 0);
    WriteU32(type2_type, 2);
    WriteU16(type2_type, 8);
    WriteU8(type2_type, 0);
    WriteU8(type2_type, 0x10);
    WriteU32(type2_type, 42);

    // Type 3: bool
    std::vector<uint8_t> type3_spec;
    WriteU16(type3_spec, 0x0202);
    WriteU16(type3_spec, 16);
    WriteU32(type3_spec, 16 + 1 * 4);
    WriteU8(type3_spec, 3);
    WriteU8(type3_spec, 0);
    WriteU16(type3_spec, 0);
    WriteU32(type3_spec, 1);
    WriteU32(type3_spec, 0);

    uint32_t type3_entries_start = type1_hdr_size + 1 * 4;
    uint32_t type3_total_size = type3_entries_start + 1 * entry_size;

    std::vector<uint8_t> type3_type;
    WriteU16(type3_type, 0x0201);
    WriteU16(type3_type, static_cast<uint16_t>(type1_hdr_size));
    WriteU32(type3_type, type3_total_size);
    WriteU8(type3_type, 3);
    WriteU8(type3_type, 0);
    WriteU16(type3_type, 0);
    WriteU32(type3_type, 1);
    WriteU32(type3_type, type3_entries_start);
    WriteU32(type3_type, config_size);
    for (size_t i = 0; i < (config_size - 4); ++i) {
        WriteU8(type3_type, 0);
    }
    WriteU32(type3_type, 0);

    WriteU16(type3_type, 8);
    WriteU16(type3_type, 0);
    WriteU32(type3_type, 3);
    WriteU16(type3_type, 8);
    WriteU8(type3_type, 0);
    WriteU8(type3_type, 0x12);
    WriteU32(type3_type, 1);

    uint32_t pkg_hdr_size = 288;
    uint32_t type_strings_offset = pkg_hdr_size;
    uint32_t key_strings_offset = type_strings_offset + static_cast<uint32_t>(type_pool.size());
    uint32_t pkg_total_size =
        key_strings_offset + static_cast<uint32_t>(key_pool.size()) +
        static_cast<uint32_t>(type1_spec.size() + type1_type.size() + type2_spec.size() +
                              type2_type.size() + type3_spec.size() + type3_type.size());

    std::vector<uint8_t> pkg;
    WriteU16(pkg, 0x0200);
    WriteU16(pkg, static_cast<uint16_t>(pkg_hdr_size));
    WriteU32(pkg, pkg_total_size);
    WriteU32(pkg, 0x7F);

    for (size_t i = 0; i < 128; ++i) {
        char16_t c = (i < pkg_name.size()) ? static_cast<char16_t>(pkg_name[i]) : 0;
        WriteU16(pkg, c);
    }

    WriteU32(pkg, type_strings_offset);
    WriteU32(pkg, static_cast<uint32_t>(type_strings.size()));
    WriteU32(pkg, key_strings_offset);
    WriteU32(pkg, static_cast<uint32_t>(key_strings.size()));
    WriteU32(pkg, 0);

    pkg.insert(pkg.end(), type_pool.begin(), type_pool.end());
    pkg.insert(pkg.end(), key_pool.begin(), key_pool.end());
    pkg.insert(pkg.end(), type1_spec.begin(), type1_spec.end());
    pkg.insert(pkg.end(), type1_type.begin(), type1_type.end());
    pkg.insert(pkg.end(), type2_spec.begin(), type2_spec.end());
    pkg.insert(pkg.end(), type2_type.begin(), type2_type.end());
    pkg.insert(pkg.end(), type3_spec.begin(), type3_spec.end());
    pkg.insert(pkg.end(), type3_type.begin(), type3_type.end());

    uint32_t table_hdr_size = 12;
    uint32_t table_total_size =
        table_hdr_size + static_cast<uint32_t>(global_pool.size() + pkg.size());

    WriteU16(table, 0x0002);
    WriteU16(table, static_cast<uint16_t>(table_hdr_size));
    WriteU32(table, table_total_size);
    WriteU32(table, 1);

    table.insert(table.end(), global_pool.begin(), global_pool.end());
    table.insert(table.end(), pkg.begin(), pkg.end());

    return table;
}

std::vector<uint8_t> AssembleEngineApk(const std::vector<uint8_t> &manifest,
                                       const std::vector<NativeLibraryEntry> &native_libraries)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::string vert_shader =
        "attribute vec2 a_pos;\nvoid main() {\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n}\n";
    auto arsc_bytes = BuildTestResourceTable("com.portage.gameengine");

    std::vector<FileEntry> entries = {
        {"AndroidManifest.xml", manifest, 0, 0},
        {"resources.arsc", arsc_bytes, 0, 0},
        {"assets/shaders/triangle.vert",
         std::vector<uint8_t>(vert_shader.begin(), vert_shader.end()), 0, 0},
        {"assets/engine.json", {'{', '}'}, 0, 0},
    };
    for (const auto &lib : native_libraries) {
        entries.push_back({"lib/x86_64/" + lib.filename, lib.data, 0, 0});
    }

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.crc = static_cast<uint32_t>(crc32(0L, e.data.data(), static_cast<uInt>(e.data.size())));
        e.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0); // Stored
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.data.begin(), e.data.end());
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    for (const auto &e : entries) {
        WriteU32(zip, 0x02014b50);
        WriteU16(zip, 20);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, 0);
        WriteU32(zip, e.offset);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;
    WriteU32(zip, 0x06054b50);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, static_cast<uint16_t>(entries.size()));
    WriteU16(zip, static_cast<uint16_t>(entries.size()));
    WriteU32(zip, cd_size);
    WriteU32(zip, cd_offset);
    WriteU16(zip, 0);

    return zip;
}

std::vector<uint8_t> CreateEngineObb(const std::string &pkg_name)
{
    std::string content = "OBB_CONTENT_OK\n";
    std::vector<uint8_t> data(content.begin(), content.end());
    uint32_t crc = static_cast<uint32_t>(crc32(0L, data.data(), static_cast<uInt>(data.size())));

    std::vector<uint8_t> zip;
    std::string entry_name = "obb_asset.txt";

    // Local Header
    WriteU32(zip, 0x04034b50);
    WriteU16(zip, 20);
    WriteU16(zip, 0);
    WriteU16(zip, 0); // Stored
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU32(zip, crc);
    WriteU32(zip, static_cast<uint32_t>(data.size()));
    WriteU32(zip, static_cast<uint32_t>(data.size()));
    WriteU16(zip, static_cast<uint16_t>(entry_name.size()));
    WriteU16(zip, 0);
    zip.insert(zip.end(), entry_name.begin(), entry_name.end());
    zip.insert(zip.end(), data.begin(), data.end());

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    // Central Directory
    WriteU32(zip, 0x02014b50);
    WriteU16(zip, 20);
    WriteU16(zip, 20);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU32(zip, crc);
    WriteU32(zip, static_cast<uint32_t>(data.size()));
    WriteU32(zip, static_cast<uint32_t>(data.size()));
    WriteU16(zip, static_cast<uint16_t>(entry_name.size()));
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU32(zip, 0);
    WriteU32(zip, 0);
    zip.insert(zip.end(), entry_name.begin(), entry_name.end());

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;
    // EOCD
    WriteU32(zip, 0x06054b50);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, 1);
    WriteU16(zip, 1);
    WriteU32(zip, cd_size);
    WriteU32(zip, cd_offset);
    WriteU16(zip, 0);

    // OBB Footer
    uint32_t footer_size = static_cast<uint32_t>(32 + pkg_name.size());
    WriteU32(zip, 1);
    WriteU32(zip, 1);
    WriteU32(zip, 0);
    for (int j = 0; j < 8; ++j) {
        WriteU8(zip, 0);
    }
    WriteU32(zip, static_cast<uint32_t>(pkg_name.size()));
    zip.insert(zip.end(), pkg_name.begin(), pkg_name.end());
    WriteU32(zip, footer_size);
    WriteU32(zip, 0x01059983);

    return zip;
}

} // namespace

class GameEngineExecutionFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = stdfs::temp_directory_path() / "portage_game_engine_test";
        sandbox_dir_ = test_dir_ / "sandbox";
        stdfs::create_directories(test_dir_);

        stdfs::path guest_so_path = FindGuestBinary("libtest_game_engine.so");
        ASSERT_TRUE(stdfs::exists(guest_so_path))
            << "Guest shared library not found at: " << guest_so_path;

        std::ifstream so_in(guest_so_path, std::ios::binary);
        std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                      std::istreambuf_iterator<char>());
        ASSERT_FALSE(so_bytes.empty());

        stdfs::path math_so_path = FindGuestBinary("libgame_math.so");
        ASSERT_TRUE(stdfs::exists(math_so_path))
            << "Math shared library not found at: " << math_so_path;
        std::ifstream math_in(math_so_path, std::ios::binary);
        std::vector<uint8_t> math_bytes((std::istreambuf_iterator<char>(math_in)),
                                        std::istreambuf_iterator<char>());
        ASSERT_FALSE(math_bytes.empty());

        stdfs::path plugin_so_path = FindGuestBinary("libgame_plugin.so");
        ASSERT_TRUE(stdfs::exists(plugin_so_path))
            << "Plugin shared library not found at: " << plugin_so_path;
        std::ifstream plugin_in(plugin_so_path, std::ios::binary);
        std::vector<uint8_t> plugin_bytes((std::istreambuf_iterator<char>(plugin_in)),
                                          std::istreambuf_iterator<char>());
        ASSERT_FALSE(plugin_bytes.empty());

        std::vector<NativeLibraryEntry> native_libs = {
            {"libtest_game_engine.so", std::move(so_bytes)},
            {"libgame_math.so", std::move(math_bytes)},
            {"libgame_plugin.so", std::move(plugin_bytes)},
        };

        auto manifest_bytes = BuildEngineManifest("com.portage.gameengine", "test_game_engine");
        auto apk_bytes = AssembleEngineApk(manifest_bytes, native_libs);

        apk_path_ = test_dir_ / "game_engine.apk";
        std::ofstream apk_out(apk_path_, std::ios::binary);
        apk_out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                      static_cast<std::streamsize>(apk_bytes.size()));
        apk_out.close();

        auto obb_bytes = CreateEngineObb("com.portage.gameengine");
        stdfs::path obb_path = test_dir_ / "main.1.com.portage.gameengine.obb";
        std::ofstream obb_out(obb_path, std::ios::binary);
        obb_out.write(reinterpret_cast<const char *>(obb_bytes.data()),
                      static_cast<std::streamsize>(obb_bytes.size()));
        obb_out.close();

        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);
        thread::ThreadManager::Instance().RegisterMainThread();
        loader_ = std::make_unique<elf::ElfLoader>(address_space_.get());
        guest_context_ = std::make_unique<arch::GuestContext>();
        ASSERT_TRUE(guest_context_->InitializeStack());
    }

    void TearDown() override
    {
        guest_context_.reset();
        loader_.reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();

        std::error_code ec;
        stdfs::remove_all(test_dir_, ec);
    }

    stdfs::path test_dir_;
    stdfs::path sandbox_dir_;
    stdfs::path apk_path_;

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
    std::unique_ptr<elf::ElfLoader> loader_;
    std::unique_ptr<arch::GuestContext> guest_context_;
};

TEST_F(GameEngineExecutionFixture, RunsAndroidNativeAppGlueLoop)
{
    package::PackageManager::Instance().Initialize(sandbox_dir_);
    auto pkg_opt = package::PackageManager::Instance().InstallPackage(apk_path_);
    ASSERT_TRUE(pkg_opt.has_value());
    auto pkg = pkg_opt.value();
    EXPECT_EQ(pkg.manifest.package_name, "com.portage.gameengine");

    ANativeWindow native_window{.width = 1280, .height = 720, .format = 1, .ref_count = 1};

    ASSERT_TRUE(package::NativeActivityRunner::Instance().SetupActivity(pkg, &native_window));

    stdfs::path primary_lib = package::PackageManager::Instance().GetPrimaryLibraryPath(pkg);
    ASSERT_TRUE(stdfs::exists(primary_lib)) << "Primary lib not found at: " << primary_lib;

    elf::LinkMap::Instance().Reset();
    elf::LinkMap::Instance().SetAddressSpace(address_space_.get());
    elf::LinkMap::Instance().SetVfs(vfs_.get());
    elf::LinkMap::Instance().AddSearchPath(pkg.lib_dir.string());
    elf::LinkMap::Instance().AddSearchPath("/system/lib64");

    auto load_res = loader_->LoadFromFile(
        primary_lib.string(), vfs_.get(),
        [](std::string_view sym) { return elf::LinkMap::Instance().ResolveGlobalSymbol(sym); },
        [&](const std::vector<std::string> &needed) {
            for (const auto &dep : needed) {
                (void)elf::LinkMap::Instance().DlOpen(dep.c_str(), elf::RTLD_NOW);
            }
        });
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(load_res));
    const auto &loaded = std::get<elf::LoadedElf>(load_res);
    elf::LinkMap::Instance().RegisterLoadedElf(loaded, primary_lib.string());

    uintptr_t on_create_addr = loaded.FindSymbol("ANativeActivity_onCreate");
    ASSERT_NE(on_create_addr, 0ULL) << "ANativeActivity_onCreate not found";

    auto on_create = reinterpret_cast<package::ANativeActivity_createFunc>(on_create_addr);
    on_create(package::NativeActivityRunner::Instance().GetActivity(), nullptr, 0);

    // Wait briefly for worker thread to create marker
    stdfs::path ready_marker = pkg.data_dir / "engine_worker_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(ready_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(ready_marker));

    stdfs::path math_marker = pkg.data_dir / "engine_math_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(math_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(math_marker));

    stdfs::path plugin_marker = pkg.data_dir / "engine_plugin_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(plugin_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(plugin_marker));

    stdfs::path ashmem_marker = pkg.data_dir / "engine_ashmem_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(ashmem_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(ashmem_marker));
    {
        std::ifstream ifs(ashmem_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "ASHMEM_OK");
    }

    stdfs::path hb_marker = pkg.data_dir / "engine_hardware_buffer_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(hb_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(hb_marker));
    {
        std::ifstream ifs(hb_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "HARDWARE_BUFFER_OK");
    }

    stdfs::path media_marker = pkg.data_dir / "engine_media_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(media_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(media_marker));
    {
        std::ifstream ifs(media_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "MEDIA_OK");
    }

    stdfs::path binder_marker = pkg.data_dir / "engine_binder_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(binder_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(binder_marker));
    {
        std::ifstream ifs(binder_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "BINDER_OK");
    }

    stdfs::path gamepad_marker = pkg.data_dir / "engine_gamepad_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(gamepad_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(gamepad_marker));
    {
        std::ifstream ifs(gamepad_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "GAMEPAD_OK");
    }

    stdfs::path jni_marker = pkg.data_dir / "engine_jni_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(jni_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(jni_marker));

    stdfs::path resources_marker = pkg.data_dir / "engine_resources_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(resources_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(resources_marker));

    stdfs::path audio_ready_marker = pkg.data_dir / "engine_audio_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(audio_ready_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(audio_ready_marker));

    stdfs::path audio_playing_marker = pkg.data_dir / "engine_audio_playing.txt";
    for (int i = 0; i < 50 && !stdfs::exists(audio_playing_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(audio_playing_marker));

    // Dispatch lifecycle: Start & Resume
    package::NativeActivityRunner::Instance().DispatchStart();
    package::NativeActivityRunner::Instance().DispatchResume();

    // Dispatch Window creation
    package::NativeActivityRunner::Instance().DispatchWindowCreated(&native_window);
    stdfs::path win_marker = pkg.data_dir / "engine_window_init.txt";
    for (int i = 0; i < 50 && !stdfs::exists(win_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(win_marker));

    // Attach input queue
    portage::ndk::InputQueueImpl input_queue;
    package::NativeActivityRunner::Instance().DispatchInputQueueCreated(
        reinterpret_cast<AInputQueue *>(&input_queue));

    // Dispatch focus
    package::NativeActivityRunner::Instance().DispatchWindowFocus(true);
    stdfs::path focus_marker = pkg.data_dir / "engine_focus_gained.txt";
    for (int i = 0; i < 50 && !stdfs::exists(focus_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(focus_marker));

    // Enqueue motion input
    std::vector<portage::ndk::InputPointer> ptrs = {{.id = 0,
                                                     .x = 400.0f,
                                                     .y = 300.0f,
                                                     .raw_x = 400.0f,
                                                     .raw_y = 300.0f,
                                                     .pressure = 1.0f,
                                                     .size = 1.0f}};
    input_queue.EnqueueMotionEvent(AMOTION_EVENT_ACTION_DOWN, ptrs);

    stdfs::path motion_marker = pkg.data_dir / "engine_motion_received.txt";
    for (int i = 0; i < 50 && !stdfs::exists(motion_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(motion_marker));

    // Enqueue key input
    input_queue.EnqueueKeyEvent(AKEY_EVENT_ACTION_DOWN, 88);

    stdfs::path key_marker = pkg.data_dir / "engine_key_received.txt";
    for (int i = 0; i < 50 && !stdfs::exists(key_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(key_marker));

    stdfs::path choreographer_marker = pkg.data_dir / "engine_choreographer_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(choreographer_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(choreographer_marker));
    {
        std::ifstream ifs(choreographer_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "CHOREOGRAPHER_OK");
    }

    stdfs::path sensor_marker = pkg.data_dir / "engine_sensor_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(sensor_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(sensor_marker));
    {
        std::ifstream ifs(sensor_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "SENSOR_OK");
    }

    stdfs::path surface_marker = pkg.data_dir / "engine_surface_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(surface_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(surface_marker));
    {
        std::ifstream ifs(surface_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "SURFACE_OK");
    }

    stdfs::path storage_marker = pkg.data_dir / "engine_storage_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(storage_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(storage_marker));
    {
        std::ifstream ifs(storage_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "STORAGE_OK");
    }

    stdfs::path licensing_marker = pkg.data_dir / "engine_licensing_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(licensing_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(licensing_marker));
    {
        std::ifstream ifs(licensing_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "LICENSING_OK");
    }

    stdfs::path billing_marker = pkg.data_dir / "engine_billing_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(billing_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(billing_marker));
    {
        std::ifstream ifs(billing_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "BILLING_OK");
    }

    stdfs::path multinetwork_marker = pkg.data_dir / "engine_multinetwork_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(multinetwork_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(multinetwork_marker));
    {
        std::ifstream ifs(multinetwork_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "MULTINETWORK_OK");
    }

    stdfs::path adpf_marker = pkg.data_dir / "engine_adpf_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(adpf_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(adpf_marker));
    {
        std::ifstream ifs(adpf_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "ADPF_OK");
    }

    stdfs::path audio_record_marker = pkg.data_dir / "engine_audio_record_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(audio_record_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(audio_record_marker));
    {
        std::ifstream ifs(audio_record_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "AUDIO_RECORD_OK");
    }

    stdfs::path swappy_marker = pkg.data_dir / "engine_swappy_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(swappy_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(swappy_marker));
    {
        std::ifstream ifs(swappy_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "SWAPPY_OK");
    }

    stdfs::path camera_marker = pkg.data_dir / "engine_camera_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(camera_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(camera_marker));
    {
        std::ifstream ifs(camera_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "CAMERA_OK");
    }

    stdfs::path keystore_marker = pkg.data_dir / "engine_keystore_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(keystore_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(keystore_marker));
    {
        std::ifstream ifs(keystore_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "KEYSTORE_OK");
    }

    stdfs::path location_marker = pkg.data_dir / "engine_location_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(location_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(location_marker));
    {
        std::ifstream ifs(location_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "LOCATION_OK");
    }

    stdfs::path clipboard_marker = pkg.data_dir / "engine_clipboard_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(clipboard_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(clipboard_marker));
    {
        std::ifstream ifs(clipboard_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "CLIPBOARD_OK");
    }

    stdfs::path power_marker = pkg.data_dir / "engine_power_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(power_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(power_marker));
    {
        std::ifstream ifs(power_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "POWER_OK");
    }

    stdfs::path notif_marker = pkg.data_dir / "engine_notification_ready.txt";
    for (int i = 0; i < 60 && !stdfs::exists(notif_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(stdfs::exists(notif_marker));
    {
        std::ifstream ifs(notif_marker);
        std::string content;
        std::getline(ifs, content);
        EXPECT_EQ(content, "NOTIFICATION_OK");
    }

    // Verify logs in LogcatBuffer
    auto log_entries = portage::ndk::LogcatBuffer::Instance().GetEntries();
    bool found_engine_log = false;
    for (const auto &entry : log_entries) {
        if (entry.tag == "PortageEngine") {
            found_engine_log = true;
            break;
        }
    }
    EXPECT_TRUE(found_engine_log);

    // Shutdown activity
    package::NativeActivityRunner::Instance().DispatchWindowFocus(false);
    package::NativeActivityRunner::Instance().DispatchPause();
    package::NativeActivityRunner::Instance().DispatchStop();
    package::NativeActivityRunner::Instance().DispatchInputQueueDestroyed(
        reinterpret_cast<AInputQueue *>(&input_queue));
    package::NativeActivityRunner::Instance().DispatchWindowDestroyed(&native_window);
    package::NativeActivityRunner::Instance().DispatchDestroy();

    stdfs::path destroyed_marker = pkg.data_dir / "engine_destroyed.txt";
    EXPECT_TRUE(stdfs::exists(destroyed_marker));
}

TEST_F(GameEngineExecutionFixture, RunsGameEngineViaLauncherCli)
{
    stdfs::path portage_exe = FindPortageExecutable();
    ASSERT_TRUE(stdfs::exists(portage_exe))
        << "portage.exe not found at current_path=" << stdfs::current_path();

    std::string cmd =
        "\"\"" + portage_exe.string() + "\" --headless run \"" + apk_path_.string() + "\"\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 0);

    // Also verify `portage logcat` CLI subcommand succeeds
    std::string logcat_cmd = "\"\"" + portage_exe.string() + "\" logcat\"";
    int logcat_ret = std::system(logcat_cmd.c_str());
    EXPECT_EQ(logcat_ret, 0);
}
