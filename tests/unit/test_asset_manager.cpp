#include "package/asset_manager.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace portage::package {
namespace {

namespace stdfs = std::filesystem;

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

std::vector<uint8_t> BuildTestZip(const std::vector<std::pair<std::string, std::string>> &files)
{
    struct Entry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<Entry> entries;
    for (const auto &[name, content] : files) {
        entries.push_back({name, std::vector<uint8_t>(content.begin(), content.end()), 0, 0});
    }

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.crc = CalculateCrc32(e.data.data(), e.data.size());
        e.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
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

class AssetManagerTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = stdfs::temp_directory_path() / "portage_asset_test";
        stdfs::create_directories(test_dir_);
        zip_path_ = test_dir_ / "test_assets.apk";

        auto zip_data = BuildTestZip({
            {"assets/shaders/frag.glsl", "precision mediump float;\nvoid main() {}\n"},
            {"assets/shaders/vert.glsl", "attribute vec4 pos;\nvoid main() {}\n"},
            {"assets/levels/map1.bin", "MAP_BINARY_DATA_0123456789"},
            {"assets/root_asset.txt", "ROOT_ASSET_CONTENT"},
        });

        std::ofstream out(zip_path_, std::ios::binary);
        out.write(reinterpret_cast<const char *>(zip_data.data()),
                  static_cast<std::streamsize>(zip_data.size()));
        out.close();

        archive_ = ZipArchive::Open(zip_path_);
        ASSERT_NE(archive_, nullptr);

        mgr_ = AssetManagerBridge::Instance().CreateManager(archive_);
        ASSERT_NE(mgr_, nullptr);
    }

    void TearDown() override
    {
        if (mgr_) {
            AssetManagerBridge::Instance().DestroyManager(mgr_);
            mgr_ = nullptr;
        }
        archive_.reset();
        std::error_code ec;
        stdfs::remove_all(test_dir_, ec);
    }

    stdfs::path test_dir_;
    stdfs::path zip_path_;
    std::shared_ptr<ZipArchive> archive_;
    AAssetManager *mgr_{nullptr};
};

TEST_F(AssetManagerTest, OpensReadsAndSeeksAsset)
{
    AAsset *asset =
        AssetManagerBridge::Sysv_AAssetManager_open(mgr_, "shaders/frag.glsl", AASSET_MODE_BUFFER);
    ASSERT_NE(asset, nullptr);

    EXPECT_GT(AssetManagerBridge::Sysv_AAsset_getLength(asset), 0);
    EXPECT_GT(AssetManagerBridge::Sysv_AAsset_getLength64(asset), 0);

    const void *buf = AssetManagerBridge::Sysv_AAsset_getBuffer(asset);
    ASSERT_NE(buf, nullptr);

    char read_buf[64] = {0};
    int bytes_read = AssetManagerBridge::Sysv_AAsset_read(asset, read_buf, sizeof(read_buf) - 1);
    EXPECT_GT(bytes_read, 0);
    EXPECT_NE(std::strstr(read_buf, "precision mediump float"), nullptr);

    // Seek test
    int64_t seek_res = AssetManagerBridge::Sysv_AAsset_seek(asset, 0, 0); // SEEK_SET
    EXPECT_EQ(seek_res, 0);
    EXPECT_EQ(AssetManagerBridge::Sysv_AAsset_getRemainingLength(asset),
              AssetManagerBridge::Sysv_AAsset_getLength(asset));

    AssetManagerBridge::Sysv_AAsset_close(asset);
}

TEST_F(AssetManagerTest, IteratesAssetDirectory)
{
    AAssetDir *dir = AssetManagerBridge::Sysv_AAssetManager_openDir(mgr_, "shaders");
    ASSERT_NE(dir, nullptr);

    std::vector<std::string> files;
    while (const char *fname = AssetManagerBridge::Sysv_AAssetDir_getNextFileName(dir)) {
        files.emplace_back(fname);
    }

    EXPECT_EQ(files.size(), 2U);
    bool has_frag = (files[0] == "frag.glsl" || files[1] == "frag.glsl");
    bool has_vert = (files[0] == "vert.glsl" || files[1] == "vert.glsl");
    EXPECT_TRUE(has_frag);
    EXPECT_TRUE(has_vert);

    // Rewind test
    AssetManagerBridge::Sysv_AAssetDir_rewind(dir);
    const char *first = AssetManagerBridge::Sysv_AAssetDir_getNextFileName(dir);
    ASSERT_NE(first, nullptr);

    AssetManagerBridge::Sysv_AAssetDir_close(dir);
}

TEST_F(AssetManagerTest, ReturnsNullForNonExistentAsset)
{
    AAsset *asset =
        AssetManagerBridge::Sysv_AAssetManager_open(mgr_, "missing/path.dat", AASSET_MODE_BUFFER);
    EXPECT_EQ(asset, nullptr);
}

} // namespace portage::package
