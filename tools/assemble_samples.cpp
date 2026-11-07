#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;

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

static uint32_t CalculateCrc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

std::vector<uint8_t> BuildManifest(const std::string &pkg_name, const std::string &lib_name)
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
        "hasCode",                    // 10
        "activity",                   // 11
        "name",                       // 12
        "android.app.NativeActivity", // 13
        "meta-data",                  // 14
        "android.app.lib_name",       // 15
        lib_name,                     // 16
        "http://schemas.android.com/apk/res/android" // 17
    };

    std::vector<uint8_t> sp_chunk;
    uint32_t sp_hdr_size = 28;
    uint32_t str_count = static_cast<uint32_t>(string_pool.size());
    uint32_t strings_start = sp_hdr_size + str_count * 4;

    std::vector<uint8_t> str_data;
    std::vector<uint32_t> str_offsets;
    for (const auto &s : string_pool) {
        str_offsets.push_back(static_cast<uint32_t>(str_data.size()));
        uint32_t u16_len = static_cast<uint32_t>(s.size());
        uint32_t u8_len = static_cast<uint32_t>(s.size());
        WriteU8(str_data, static_cast<uint8_t>(u16_len));
        WriteU8(str_data, static_cast<uint8_t>(u8_len));
        str_data.insert(str_data.end(), s.begin(), s.end());
        WriteU8(str_data, 0);
    }
    while (str_data.size() % 4 != 0) {
        WriteU8(str_data, 0);
    }

    uint32_t sp_chunk_size = strings_start + static_cast<uint32_t>(str_data.size());

    WriteU16(sp_chunk, 0x0001); // CHUNK_STRING_POOL
    WriteU16(sp_chunk, static_cast<uint16_t>(sp_hdr_size));
    WriteU32(sp_chunk, sp_chunk_size);
    WriteU32(sp_chunk, str_count);
    WriteU32(sp_chunk, 0);
    WriteU32(sp_chunk, (1 << 8)); // UTF-8
    WriteU32(sp_chunk, strings_start);
    WriteU32(sp_chunk, 0);

    for (uint32_t off : str_offsets) {
        WriteU32(sp_chunk, off);
    }
    sp_chunk.insert(sp_chunk.end(), str_data.begin(), str_data.end());

    std::vector<uint8_t> body;

    auto write_start_elem = [&](uint32_t name_idx,
                                const std::vector<std::pair<uint32_t, uint32_t>> &attrs) {
        uint32_t chunk_size = 36 + static_cast<uint32_t>(attrs.size() * 20);
        WriteU16(body, 0x0102);
        WriteU16(body, 16);
        WriteU32(body, chunk_size);
        WriteU32(body, 0);
        WriteU32(body, 0xFFFFFFFF);
        WriteU32(body, 0xFFFFFFFF);
        WriteU32(body, name_idx);
        WriteU16(body, 20);
        WriteU16(body, 20);
        WriteU16(body, static_cast<uint16_t>(attrs.size()));
        WriteU16(body, 0);
        WriteU16(body, 0);
        WriteU16(body, 0);

        for (const auto &attr : attrs) {
            WriteU32(body, 17);           // uri: android
            WriteU32(body, attr.first);   // name index
            WriteU32(body, attr.second);  // value string index
            WriteU16(body, 8);            // Res_value size
            WriteU8(body, 0);             // res0
            WriteU8(body, 0x03);          // TYPE_STRING
            WriteU32(body, attr.second);  // data
        }
    };

    auto write_end_elem = [&](uint32_t name_idx) {
        WriteU16(body, 0x0103);
        WriteU16(body, 16);
        WriteU32(body, 24);
        WriteU32(body, 0);
        WriteU32(body, 0xFFFFFFFF);
        WriteU32(body, 0xFFFFFFFF);
        WriteU32(body, name_idx);
    };

    // <manifest package=pkg_name versionCode=1 versionName="1.0.0">
    write_start_elem(0, {{1, 2}, {3, 5}, {4, 5}});

    // <uses-sdk minSdkVersion=21 targetSdkVersion=34>
    write_start_elem(6, {{7, 5}, {8, 5}});
    write_end_elem(6);

    // <application hasCode=false>
    write_start_elem(9, {{10, 5}});

    // <activity name="android.app.NativeActivity">
    write_start_elem(11, {{12, 13}});

    // <meta-data name="android.app.lib_name" value=lib_name>
    write_start_elem(14, {{12, 15}, {16, 16}});
    write_end_elem(14);

    write_end_elem(11);
    write_end_elem(9);
    write_end_elem(0);

    std::vector<uint8_t> doc;
    uint32_t doc_size = 8 + static_cast<uint32_t>(sp_chunk.size() + body.size());

    WriteU16(doc, 0x0003); // CHUNK_AXML_ROOT
    WriteU16(doc, 8);
    WriteU32(doc, doc_size);

    doc.insert(doc.end(), sp_chunk.begin(), sp_chunk.end());
    doc.insert(doc.end(), body.begin(), body.end());

    return doc;
}

std::vector<uint8_t> BuildStringPool(const std::vector<std::string> &strings, bool is_utf8)
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
            if (u16_len < 128) {
                WriteU8(str_data, static_cast<uint8_t>(u16_len));
            } else {
                WriteU8(str_data, static_cast<uint8_t>((u16_len >> 8) | 0x80));
                WriteU8(str_data, static_cast<uint8_t>(u16_len & 0xFF));
            }
            if (u8_len < 128) {
                WriteU8(str_data, static_cast<uint8_t>(u8_len));
            } else {
                WriteU8(str_data, static_cast<uint8_t>((u8_len >> 8) | 0x80));
                WriteU8(str_data, static_cast<uint8_t>(u8_len & 0xFF));
            }
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

std::vector<uint8_t> BuildResourceTable(const std::string &pkg_name)
{
    std::vector<uint8_t> table;

    std::vector<std::string> global_strings = {
        "Portage Game Engine", // 0
        "Welcome to Portage!", // 1
    };
    auto global_pool = BuildStringPool(global_strings, true);

    std::vector<std::string> type_strings = {
        "string",  // 0 (id 1)
        "integer", // 1 (id 2)
        "bool",    // 2 (id 3)
    };
    auto type_pool = BuildStringPool(type_strings, true);

    std::vector<std::string> key_strings = {
        "app_name",        // 0
        "welcome_message", // 1
        "game_difficulty", // 2
        "sound_enabled",   // 3
    };
    auto key_pool = BuildStringPool(key_strings, true);

    // Type 1 (string)
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
    uint32_t entry_size = 8 + 8;
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

    // Entry 0: app_name -> string index 0 ("Portage Game Engine")
    WriteU16(type1_type, 8);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 0);
    WriteU16(type1_type, 8);
    WriteU8(type1_type, 0);
    WriteU8(type1_type, 0x03);
    WriteU32(type1_type, 0);

    // Entry 1: welcome_message -> string index 1 ("Welcome to Portage!")
    WriteU16(type1_type, 8);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 1);
    WriteU16(type1_type, 8);
    WriteU8(type1_type, 0);
    WriteU8(type1_type, 0x03);
    WriteU32(type1_type, 1);

    // Type 2 (integer)
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

    // Entry 0: game_difficulty -> 42
    WriteU16(type2_type, 8);
    WriteU16(type2_type, 0);
    WriteU32(type2_type, 2);
    WriteU16(type2_type, 8);
    WriteU8(type2_type, 0);
    WriteU8(type2_type, 0x10); // TYPE_INT_DEC
    WriteU32(type2_type, 42);

    // Type 3 (bool)
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

    // Entry 0: sound_enabled -> true (1)
    WriteU16(type3_type, 8);
    WriteU16(type3_type, 0);
    WriteU32(type3_type, 3);
    WriteU16(type3_type, 8);
    WriteU8(type3_type, 0);
    WriteU8(type3_type, 0x12); // TYPE_INT_BOOLEAN
    WriteU32(type3_type, 1);

    // Package header
    uint32_t pkg_hdr_size = 288;
    uint32_t type_strings_offset = pkg_hdr_size;
    uint32_t key_strings_offset = type_strings_offset + static_cast<uint32_t>(type_pool.size());
    uint32_t pkg_total_size = key_strings_offset + static_cast<uint32_t>(key_pool.size()) +
                              static_cast<uint32_t>(type1_spec.size() + type1_type.size() +
                                                   type2_spec.size() + type2_type.size() +
                                                   type3_spec.size() + type3_type.size());

    std::vector<uint8_t> pkg;
    WriteU16(pkg, 0x0200); // RES_TABLE_PACKAGE_TYPE
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

    // Root table
    uint32_t table_hdr_size = 12;
    uint32_t table_total_size =
        table_hdr_size + static_cast<uint32_t>(global_pool.size() + pkg.size());

    WriteU16(table, 0x0002); // RES_TABLE_TYPE
    WriteU16(table, static_cast<uint16_t>(table_hdr_size));
    WriteU32(table, table_total_size);
    WriteU32(table, 1);

    table.insert(table.end(), global_pool.begin(), global_pool.end());
    table.insert(table.end(), pkg.begin(), pkg.end());

    return table;
}

std::vector<uint8_t>
AssembleApk(const std::vector<uint8_t> &manifest, const std::vector<uint8_t> &resources,
            const std::vector<std::pair<std::string, std::vector<uint8_t>>> &assets,
            const std::vector<std::pair<std::string, std::vector<uint8_t>>> &libs)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<FileEntry> entries = {
        {"AndroidManifest.xml", manifest, 0, 0},
        {"resources.arsc", resources, 0, 0},
    };
    for (const auto &asset : assets) {
        entries.push_back({asset.first, asset.second, 0, 0});
    }
    for (const auto &lib : libs) {
        entries.push_back({"lib/x86_64/" + lib.first, lib.second, 0, 0});
    }

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.crc = CalculateCrc32(e.data.data(), e.data.size());
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

int main(int argc, char *argv[])
{
    if (argc < 5) {
        std::cerr << "Usage: assemble_samples <out_apk> <pkg_name> <lib_name> <so_path> "
                     "[extra_so_paths...]\n";
        return 1;
    }

    std::string out_apk = argv[1];
    std::string pkg_name = argv[2];
    std::string lib_name = argv[3];

    std::vector<std::pair<std::string, std::vector<uint8_t>>> libs;
    for (int i = 4; i < argc; ++i) {
        std::string so_path = argv[i];
        std::ifstream so_in(so_path, std::ios::binary);
        if (!so_in) {
            std::cerr << "Failed to open " << so_path << "\n";
            return 1;
        }
        std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                      std::istreambuf_iterator<char>());
        std::string filename = stdfs::path(so_path).filename().string();
        libs.push_back({filename, std::move(so_bytes)});
    }

    auto manifest = BuildManifest(pkg_name, lib_name);
    auto resources = BuildResourceTable(pkg_name);

    // Bundle sample assets into APK
    std::string vert_shader =
        "attribute vec2 a_pos;\nvoid main() {\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n}\n";
    std::string config_dat = "PORTAGE_CONFIG_VERSION=1\nENGINE_MODE=FAST\n";

    std::vector<std::pair<std::string, std::vector<uint8_t>>> assets = {
        {"assets/shaders/triangle.vert",
         std::vector<uint8_t>(vert_shader.begin(), vert_shader.end())},
        {"assets/data/config.dat", std::vector<uint8_t>(config_dat.begin(), config_dat.end())},
    };

    auto apk_bytes = AssembleApk(manifest, resources, assets, libs);

    std::ofstream apk_out(out_apk, std::ios::binary);
    apk_out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                  static_cast<std::streamsize>(apk_bytes.size()));
    apk_out.close();

    std::cout << "Successfully assembled: " << out_apk << " (" << apk_bytes.size() << " bytes, "
              << libs.size() << " libraries, " << assets.size() << " assets)\n";
    return 0;
}
