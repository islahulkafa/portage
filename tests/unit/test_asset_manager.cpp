#include "package/asset_manager.hpp"
#include "package/zip_archive.hpp"
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

std::vector<uint8_t> CreateAssetZip()
{
    struct Item {
        std::string name;
        std::vector<uint8_t> data;
        std::vector<uint8_t> comp;
        uint16_t method{0};
        uint32_t crc{0};
        uint32_t off{0};
    };

    std::string text = "0123456789ABCDEF-Portage-Asset-Data";
    std::string deflated_src;
    for (int i = 0; i < 20; ++i)
        deflated_src += "Deflated-Asset-Stream-Chunk-";

    std::vector<Item> items = {
        {"assets/test.bin", {text.begin(), text.end()}, {}, 0, 0, 0},
        {"assets/stream.dat", {deflated_src.begin(), deflated_src.end()}, {}, 8, 0, 0}};

    std::vector<uint8_t> zip;
    for (auto &item : items) {
        item.crc =
            static_cast<uint32_t>(crc32(0L, item.data.data(), static_cast<uInt>(item.data.size())));
        item.off = static_cast<uint32_t>(zip.size());

        if (item.method == 8) {
            z_stream strm{};
            deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY);
            item.comp.resize(item.data.size() + 128);
            strm.next_in = item.data.data();
            strm.avail_in = static_cast<uInt>(item.data.size());
            strm.next_out = item.comp.data();
            strm.avail_out = static_cast<uInt>(item.comp.size());
            deflate(&strm, Z_FINISH);
            item.comp.resize(strm.total_out);
            deflateEnd(&strm);
        } else {
            item.comp = item.data;
        }

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, item.method);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, item.crc);
        WriteU32(zip, static_cast<uint32_t>(item.comp.size()));
        WriteU32(zip, static_cast<uint32_t>(item.data.size()));
        WriteU16(zip, static_cast<uint16_t>(item.name.size()));
        WriteU16(zip, 0);
        zip.insert(zip.end(), item.name.begin(), item.name.end());
        zip.insert(zip.end(), item.comp.begin(), item.comp.end());
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    for (const auto &item : items) {
        WriteU32(zip, 0x02014b50);
        WriteU16(zip, 20);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, item.method);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, item.crc);
        WriteU32(zip, static_cast<uint32_t>(item.comp.size()));
        WriteU32(zip, static_cast<uint32_t>(item.data.size()));
        WriteU16(zip, static_cast<uint16_t>(item.name.size()));
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, 0);
        WriteU32(zip, item.off);
        zip.insert(zip.end(), item.name.begin(), item.name.end());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;
    WriteU32(zip, 0x06054b50);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, static_cast<uint16_t>(items.size()));
    WriteU16(zip, static_cast<uint16_t>(items.size()));
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
        test_dir_ = std::filesystem::temp_directory_path() / "portage_asset_test";
        std::filesystem::create_directories(test_dir_);
        zip_path_ = test_dir_ / "assets.apk";

        auto bytes = CreateAssetZip();
        std::ofstream out(zip_path_, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();

        archive_ = portage::package::ZipArchive::Open(zip_path_);
        ASSERT_NE(archive_, nullptr);
    }

    void TearDown() override
    {
        archive_.reset();
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path zip_path_;
    std::shared_ptr<portage::package::ZipArchive> archive_;
};

TEST_F(AssetManagerTest, OpensAssetAndAccessesBuffer)
{
    auto *mgr = portage::package::AssetManagerBridge::Instance().CreateManager(archive_);
    ASSERT_NE(mgr, nullptr);

    auto *asset = mgr->openFunc(mgr, "test.bin", portage::package::AASSET_MODE_BUFFER);
    ASSERT_NE(asset, nullptr);

    int64_t len = asset->getLengthFunc(asset);
    EXPECT_GT(len, 0);

    const void *buf = asset->getBufferFunc(asset);
    ASSERT_NE(buf, nullptr);

    std::string text(static_cast<const char *>(buf), static_cast<size_t>(len));
    EXPECT_EQ(text, "0123456789ABCDEF-Portage-Asset-Data");

    char read_buf[5] = {0};
    int read_bytes = asset->readFunc(asset, read_buf, 4);
    EXPECT_EQ(read_bytes, 4);
    EXPECT_STREQ(read_buf, "0123");

    int64_t new_pos = asset->seekFunc(asset, 10, SEEK_SET);
    EXPECT_EQ(new_pos, 10);

    read_bytes = asset->readFunc(asset, read_buf, 4);
    EXPECT_EQ(read_bytes, 4);
    EXPECT_STREQ(read_buf, "ABCD");

    asset->closeFunc(asset);
    portage::package::AssetManagerBridge::Instance().DestroyManager(mgr);
}

TEST_F(AssetManagerTest, DecompressesAssetOnTheFly)
{
    auto *mgr = portage::package::AssetManagerBridge::Instance().CreateManager(archive_);
    ASSERT_NE(mgr, nullptr);

    auto *asset = mgr->openFunc(mgr, "stream.dat", portage::package::AASSET_MODE_STREAMING);
    ASSERT_NE(asset, nullptr);

    int64_t len = asset->getLengthFunc(asset);
    EXPECT_EQ(len, 20 * 28); // 560 bytes

    char chunk[9] = {0};
    int read_bytes = asset->readFunc(asset, chunk, 8);
    EXPECT_EQ(read_bytes, 8);
    EXPECT_STREQ(chunk, "Deflated");

    asset->closeFunc(asset);
    portage::package::AssetManagerBridge::Instance().DestroyManager(mgr);
}
