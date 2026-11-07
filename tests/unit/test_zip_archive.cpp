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

struct TestFileEntry {
    std::string name;
    std::vector<uint8_t> uncompressed;
    std::vector<uint8_t> compressed;
    uint16_t method{0};
    uint32_t crc{0};
    uint32_t local_offset{0};
};

std::vector<uint8_t> CreateTestZip(const std::vector<std::pair<std::string, std::string>> &files)
{
    std::vector<uint8_t> zip;
    std::vector<TestFileEntry> entries;

    for (const auto &[name, content] : files) {
        TestFileEntry e;
        e.name = name;
        e.uncompressed.assign(content.begin(), content.end());
        e.crc = static_cast<uint32_t>(crc32(0L, reinterpret_cast<const Bytef *>(content.data()),
                                            static_cast<uInt>(content.size())));
        e.local_offset = static_cast<uint32_t>(zip.size());

        if (name.ends_with(".deflate")) {
            e.method = 8;
            z_stream strm{};
            deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY);
            e.compressed.resize(content.size() + 128);
            strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(content.data()));
            strm.avail_in = static_cast<uInt>(content.size());
            strm.next_out = e.compressed.data();
            strm.avail_out = static_cast<uInt>(e.compressed.size());
            deflate(&strm, Z_FINISH);
            e.compressed.resize(strm.total_out);
            deflateEnd(&strm);
        } else {
            e.method = 0;
            e.compressed = e.uncompressed;
        }

        // Local File Header
        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, e.method);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.compressed.size()));
        WriteU32(zip, static_cast<uint32_t>(e.uncompressed.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0); // extra len
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.compressed.begin(), e.compressed.end());

        entries.push_back(std::move(e));
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());

    // Central Directory
    for (const auto &e : entries) {
        WriteU32(zip, 0x02014b50);
        WriteU16(zip, 20); // version made by
        WriteU16(zip, 20); // version needed
        WriteU16(zip, 0);  // flags
        WriteU16(zip, e.method);
        WriteU16(zip, 0); // mod time
        WriteU16(zip, 0); // mod date
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.compressed.size()));
        WriteU32(zip, static_cast<uint32_t>(e.uncompressed.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0); // extra len
        WriteU16(zip, 0); // comment len
        WriteU16(zip, 0); // disk start
        WriteU16(zip, 0); // int attr
        WriteU32(zip, 0); // ext attr
        WriteU32(zip, e.local_offset);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;

    // EOCD
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

class ZipArchiveTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_zip_test";
        std::filesystem::create_directories(test_dir_);
        zip_path_ = test_dir_ / "test_package.zip";

        std::string repeated;
        for (int i = 0; i < 50; ++i) {
            repeated += "Portage Android Compatibility Layer Compression Test String\n";
        }

        auto bytes =
            CreateTestZip({{"assets/stored.txt", "This is a stored asset without compression."},
                           {"assets/compressed.deflate", repeated}});

        std::ofstream out(zip_path_, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        out.close();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path zip_path_;
};

TEST_F(ZipArchiveTest, OpensAndIndexesEntries)
{
    auto archive = portage::package::ZipArchive::Open(zip_path_);
    ASSERT_NE(archive, nullptr);

    EXPECT_TRUE(archive->HasEntry("assets/stored.txt"));
    EXPECT_TRUE(archive->HasEntry("assets/compressed.deflate"));
    EXPECT_FALSE(archive->HasEntry("nonexistent.txt"));

    auto all = archive->GetAllEntries();
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(ZipArchiveTest, ReadsStoredEntryDirectlyAndViaSpan)
{
    auto archive = portage::package::ZipArchive::Open(zip_path_);
    ASSERT_NE(archive, nullptr);

    auto span = archive->GetRawEntrySpan("assets/stored.txt");
    ASSERT_TRUE(span.has_value());
    std::string text(reinterpret_cast<const char *>(span->data()), span->size());
    EXPECT_EQ(text, "This is a stored asset without compression.");

    auto buf = archive->ReadEntry("assets/stored.txt");
    ASSERT_TRUE(buf.has_value());
    std::string buf_text(reinterpret_cast<const char *>(buf->data()), buf->size());
    EXPECT_EQ(buf_text, "This is a stored asset without compression.");
}

TEST_F(ZipArchiveTest, DecompressesDeflatedEntry)
{
    auto archive = portage::package::ZipArchive::Open(zip_path_);
    ASSERT_NE(archive, nullptr);

    auto buf = archive->ReadEntry("assets/compressed.deflate");
    ASSERT_TRUE(buf.has_value());
    std::string text(reinterpret_cast<const char *>(buf->data()), buf->size());
    EXPECT_TRUE(text.starts_with("Portage Android Compatibility Layer"));
    EXPECT_EQ(text.size(), 3000u); // 50 * 60 chars
}

TEST_F(ZipArchiveTest, ExtractsEntryToFileSystem)
{
    auto archive = portage::package::ZipArchive::Open(zip_path_);
    ASSERT_NE(archive, nullptr);

    std::filesystem::path dest = test_dir_ / "extracted" / "stored.txt";
    EXPECT_TRUE(archive->ExtractEntry("assets/stored.txt", dest));
    EXPECT_TRUE(std::filesystem::exists(dest));

    std::ifstream in(dest);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "This is a stored asset without compression.");
}
