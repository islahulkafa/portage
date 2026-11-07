#include "package/manifest_parser.hpp"
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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

struct TestAttribute {
    uint32_t name_idx{0};
    uint32_t raw_val_idx{0xFFFFFFFF};
    uint8_t data_type{0x03}; // 0x03 string, 0x10 int_dec
    uint32_t data_val{0};
};

struct TestElement {
    uint32_t tag_idx{0};
    std::vector<TestAttribute> attrs;
};

std::vector<uint8_t> BuildTestAxml(const std::vector<std::string> &strings,
                                   const std::vector<TestElement> &elements)
{
    std::vector<uint8_t> doc;

    // We will reserve 8 bytes for root chunk header
    WriteU16(doc, 0x0003); // CHUNK_AXML_ROOT
    WriteU16(doc, 0x0008); // Header size
    WriteU32(doc, 0);      // Total size placeholder

    // String Pool Chunk
    uint32_t str_pool_start = static_cast<uint32_t>(doc.size());
    WriteU16(doc, 0x0001); // CHUNK_STRING_POOL
    WriteU16(doc, 0x001C); // Header size: 28 bytes
    WriteU32(doc, 0);      // Chunk size placeholder
    WriteU32(doc, static_cast<uint32_t>(strings.size()));
    WriteU32(doc, 0);                                              // Style count
    WriteU32(doc, 1 << 8);                                         // Flags: UTF-8
    WriteU32(doc, 28 + static_cast<uint32_t>(strings.size()) * 4); // Strings start
    WriteU32(doc, 0);                                              // Styles start

    // String offsets
    std::vector<uint8_t> str_bytes;
    for (const auto &s : strings) {
        WriteU32(doc, static_cast<uint32_t>(str_bytes.size()));
        str_bytes.push_back(static_cast<uint8_t>(s.size()));
        str_bytes.push_back(static_cast<uint8_t>(s.size()));
        str_bytes.insert(str_bytes.end(), s.begin(), s.end());
        str_bytes.push_back(0); // null terminator
    }
    doc.insert(doc.end(), str_bytes.begin(), str_bytes.end());

    // Align to 4 bytes
    while (doc.size() % 4 != 0) {
        doc.push_back(0);
    }

    uint32_t str_pool_size = static_cast<uint32_t>(doc.size()) - str_pool_start;
    std::memcpy(&doc[str_pool_start + 4], &str_pool_size, sizeof(uint32_t));

    // Elements
    for (const auto &elem : elements) {
        uint32_t elem_start = static_cast<uint32_t>(doc.size());
        WriteU16(doc, 0x0102);     // CHUNK_START_ELEMENT
        WriteU16(doc, 0x0010);     // Header size
        WriteU32(doc, 0);          // Chunk size placeholder
        WriteU32(doc, 1);          // Line number
        WriteU32(doc, 0xFFFFFFFF); // Comment
        WriteU32(doc, 0xFFFFFFFF); // Namespace
        WriteU32(doc, elem.tag_idx);
        WriteU16(doc, 0x0014); // Attribute start offset (from tag_idx + 8 -> total 36)
        WriteU16(doc, 0x0014); // Attribute size: 20 bytes
        WriteU16(doc, static_cast<uint16_t>(elem.attrs.size()));
        WriteU16(doc, 0); // Id index
        WriteU16(doc, 0); // Class index
        WriteU16(doc, 0); // Style index

        for (const auto &a : elem.attrs) {
            WriteU32(doc, 0xFFFFFFFF); // Namespace
            WriteU32(doc, a.name_idx);
            WriteU32(doc, a.raw_val_idx);
            doc.push_back(0x08); // size
            doc.push_back(0x00); // res
            doc.push_back(a.data_type);
            doc.push_back(0x00);
            WriteU32(doc, a.data_val);
        }

        uint32_t elem_size = static_cast<uint32_t>(doc.size()) - elem_start;
        std::memcpy(&doc[elem_start + 4], &elem_size, sizeof(uint32_t));
    }

    uint32_t total_size = static_cast<uint32_t>(doc.size());
    std::memcpy(&doc[4], &total_size, sizeof(uint32_t));

    return doc;
}

} // namespace

TEST(ManifestParserTest, ParsesCompleteAndroidManifest)
{
    std::vector<std::string> string_pool = {
        "manifest",                   // 0
        "package",                    // 1
        "com.portage.game",           // 2
        "versionCode",                // 3
        "versionName",                // 4
        "2.1.0",                      // 5
        "uses-sdk",                   // 6
        "minSdkVersion",              // 7
        "targetSdkVersion",           // 8
        "application",                // 9
        "label",                      // 10
        "Portage Game",               // 11
        "hasCode",                    // 12
        "activity",                   // 13
        "name",                       // 14
        "android.app.NativeActivity", // 15
        "meta-data",                  // 16
        "value",                      // 17
        "android.app.lib_name",       // 18
        "super_game"                  // 19
    };

    std::vector<TestElement> elements = {
        {0, {{1, 2, 0x03, 0}, {3, 0xFFFFFFFF, 0x10, 42}, {4, 5, 0x03, 0}}}, // manifest
        {6, {{7, 0xFFFFFFFF, 0x10, 24}, {8, 0xFFFFFFFF, 0x10, 34}}},        // uses-sdk
        {9, {{10, 11, 0x03, 0}, {12, 0xFFFFFFFF, 0x12, 1}}},                // application
        {13, {{14, 15, 0x03, 0}}},                                          // activity
        {16, {{14, 18, 0x03, 0}, {17, 19, 0x03, 0}}}                        // meta-data lib_name
    };

    auto axml_bytes = BuildTestAxml(string_pool, elements);

    auto result = portage::package::ManifestParser::Parse(axml_bytes);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->package_name, "com.portage.game");
    EXPECT_EQ(result->version_code, 42);
    EXPECT_EQ(result->version_name, "2.1.0");
    EXPECT_EQ(result->min_sdk_version, 24);
    EXPECT_EQ(result->target_sdk_version, 34);
    EXPECT_EQ(result->application_label, "Portage Game");
    EXPECT_EQ(result->main_activity_name, "android.app.NativeActivity");
    EXPECT_EQ(result->native_library_name, "super_game");
}

TEST(ManifestParserTest, RejectsInvalidMagicOrTruncatedBuffer)
{
    std::vector<uint8_t> invalid = {0x00, 0x00, 0x01, 0x02};
    auto result = portage::package::ManifestParser::Parse(invalid);
    EXPECT_FALSE(result.has_value());
}
