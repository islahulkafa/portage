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
        "label",                      // 10
        "Portage Sample App",         // 11
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

std::vector<uint8_t> AssembleApk(const std::vector<uint8_t> &manifest,
                                 const std::string &lib_filename,
                                 const std::vector<uint8_t> &lib_bytes)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<FileEntry> entries = {{"AndroidManifest.xml", manifest, 0, 0},
                                      {"lib/x86_64/" + lib_filename, lib_bytes, 0, 0}};

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
        std::cerr << "Usage: assemble_samples <out_apk> <pkg_name> <lib_name> <so_path>\n";
        return 1;
    }

    std::string out_apk = argv[1];
    std::string pkg_name = argv[2];
    std::string lib_name = argv[3];
    std::string so_path = argv[4];

    std::ifstream so_in(so_path, std::ios::binary);
    if (!so_in) {
        std::cerr << "Failed to open " << so_path << "\n";
        return 1;
    }

    std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                  std::istreambuf_iterator<char>());
    auto manifest = BuildManifest(pkg_name, lib_name);
    std::string filename = stdfs::path(so_path).filename().string();
    auto apk_bytes = AssembleApk(manifest, filename, so_bytes);

    std::ofstream apk_out(out_apk, std::ios::binary);
    apk_out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                  static_cast<std::streamsize>(apk_bytes.size()));
    apk_out.close();

    std::cout << "Successfully assembled: " << out_apk << " (" << apk_bytes.size() << " bytes)\n";
    return 0;
}
