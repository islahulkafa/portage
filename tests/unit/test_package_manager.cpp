#include "package/manifest_parser.hpp"
#include "package/package_manager.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

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

std::vector<uint8_t> BuildTestManifest()
{
    std::vector<std::string> string_pool = {
        "manifest",                   // 0
        "package",                    // 1
        "com.portage.testpkg",        // 2
        "versionCode",                // 3
        "versionName",                // 4
        "1.0.0",                      // 5
        "uses-sdk",                   // 6
        "minSdkVersion",              // 7
        "targetSdkVersion",           // 8
        "application",                // 9
        "label",                      // 10
        "Test App",                   // 11
        "hasCode",                    // 12
        "activity",                   // 13
        "name",                       // 14
        "android.app.NativeActivity", // 15
        "meta-data",                  // 16
        "value",                      // 17
        "android.app.lib_name",       // 18
        "test_native"                 // 19
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

    std::vector<uint8_t> str_bytes;
    for (const auto &s : string_pool) {
        WriteU32(doc, static_cast<uint32_t>(str_bytes.size()));
        str_bytes.push_back(static_cast<uint8_t>(s.size()));
        str_bytes.push_back(static_cast<uint8_t>(s.size()));
        str_bytes.insert(str_bytes.end(), s.begin(), s.end());
        str_bytes.push_back(0);
    }
    doc.insert(doc.end(), str_bytes.begin(), str_bytes.end());
    while (doc.size() % 4 != 0)
        doc.push_back(0);

    uint32_t str_pool_size = static_cast<uint32_t>(doc.size()) - str_pool_start;
    std::memcpy(&doc[str_pool_start + 4], &str_pool_size, sizeof(uint32_t));

    auto append_elem =
        [&](uint32_t tag_idx,
            const std::vector<std::tuple<uint32_t, uint32_t, uint8_t, uint32_t>> &attrs) {
            uint32_t elem_start = static_cast<uint32_t>(doc.size());
            WriteU16(doc, 0x0102); // CHUNK_START_ELEMENT
            WriteU16(doc, 0x0010);
            WriteU32(doc, 0);
            WriteU32(doc, 1);
            WriteU32(doc, 0xFFFFFFFF);
            WriteU32(doc, 0xFFFFFFFF);
            WriteU32(doc, tag_idx);
            WriteU16(doc, 0x0014);
            WriteU16(doc, 0x0014);
            WriteU16(doc, static_cast<uint16_t>(attrs.size()));
            WriteU16(doc, 0);
            WriteU16(doc, 0);
            WriteU16(doc, 0);

            for (const auto &[name_idx, raw_idx, type, val] : attrs) {
                WriteU32(doc, 0xFFFFFFFF);
                WriteU32(doc, name_idx);
                WriteU32(doc, raw_idx);
                doc.push_back(0x08);
                doc.push_back(0x00);
                doc.push_back(type);
                doc.push_back(0x00);
                WriteU32(doc, val);
            }

            uint32_t elem_sz = static_cast<uint32_t>(doc.size()) - elem_start;
            std::memcpy(&doc[elem_start + 4], &elem_sz, sizeof(uint32_t));
        };

    append_elem(0, {{1, 2, 0x03, 0}, {3, 0xFFFFFFFF, 0x10, 1}, {4, 5, 0x03, 0}});
    append_elem(6, {{7, 0xFFFFFFFF, 0x10, 21}, {8, 0xFFFFFFFF, 0x10, 34}});
    append_elem(9, {{10, 11, 0x03, 0}, {12, 0xFFFFFFFF, 0x12, 1}});
    append_elem(13, {{14, 15, 0x03, 0}});
    append_elem(16, {{14, 18, 0x03, 0}, {17, 19, 0x03, 0}});

    uint32_t total_sz = static_cast<uint32_t>(doc.size());
    std::memcpy(&doc[4], &total_sz, sizeof(uint32_t));

    return doc;
}

std::vector<uint8_t> CreateTestApkWithLib(const std::vector<uint8_t> &manifest_bytes,
                                          const std::string &lib_name,
                                          const std::string &lib_content)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<FileEntry> entries = {
        {"AndroidManifest.xml", manifest_bytes, 0, 0},
        {"lib/x86_64/" + lib_name, {lib_content.begin(), lib_content.end()}, 0, 0},
        {"assets/config.json", {'{', '}'}, 0, 0}};

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

} // namespace

class PackageManagerTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_pkg_test";
        sandbox_dir_ = test_dir_ / "sandbox";
        std::filesystem::create_directories(test_dir_);

        apk_path_ = test_dir_ / "test_app.apk";
        auto manifest = BuildTestManifest();
        auto apk_bytes = CreateTestApkWithLib(manifest, "libtest_native.so", "MOCK_ELF_SO_BYTES");

        std::ofstream out(apk_path_, std::ios::binary);
        out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                  static_cast<std::streamsize>(apk_bytes.size()));
        out.close();

        portage::package::PackageManager::Instance().Reset();
        portage::package::PackageManager::Instance().Initialize(sandbox_dir_);
    }

    void TearDown() override
    {
        portage::package::PackageManager::Instance().Reset();
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path sandbox_dir_;
    std::filesystem::path apk_path_;
};

TEST_F(PackageManagerTest, InstallsApkAndCreatesSandboxLayout)
{
    auto &mgr = portage::package::PackageManager::Instance();
    auto pkg_opt = mgr.InstallPackage(apk_path_);
    ASSERT_TRUE(pkg_opt.has_value());

    const auto &pkg = *pkg_opt;
    EXPECT_EQ(pkg.manifest.package_name, "com.portage.testpkg");
    EXPECT_EQ(pkg.manifest.native_library_name, "test_native");

    EXPECT_TRUE(std::filesystem::exists(pkg.app_dir));
    EXPECT_TRUE(std::filesystem::exists(pkg.app_dir / "base.apk"));
    EXPECT_TRUE(std::filesystem::exists(pkg.lib_dir / "libtest_native.so"));
    EXPECT_TRUE(std::filesystem::exists(pkg.data_dir));
    EXPECT_TRUE(std::filesystem::exists(pkg.cache_dir));
    EXPECT_TRUE(std::filesystem::exists(pkg.external_data_dir));

    auto lib_path = mgr.GetPrimaryLibraryPath(pkg);
    EXPECT_EQ(lib_path, pkg.lib_dir / "libtest_native.so");

    auto retrieved = mgr.GetPackage("com.portage.testpkg");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->manifest.package_name, "com.portage.testpkg");

    auto active = mgr.GetActivePackage();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->manifest.package_name, "com.portage.testpkg");
}
