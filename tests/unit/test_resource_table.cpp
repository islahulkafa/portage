#include "package/resource_table.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace portage::package {
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

std::vector<uint8_t> BuildTestStringPool(const std::vector<std::string> &strings,
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

std::vector<uint8_t> BuildTestResourceTable(const std::string &pkg_name)
{
    std::vector<uint8_t> table;

    std::vector<std::string> global_strings = {
        "Test App Name",
        "Hello from Portage ARSC",
    };
    auto global_pool = BuildTestStringPool(global_strings, true);

    std::vector<std::string> type_strings = {"string", "integer", "bool"};
    auto type_pool = BuildTestStringPool(type_strings, true);

    std::vector<std::string> key_strings = {"app_title", "greeting", "max_players",
                                            "is_multiplayer"};
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

    // Entry 0: app_title -> global string 0
    WriteU16(type1_type, 8);
    WriteU16(type1_type, 0);
    WriteU32(type1_type, 0);
    WriteU16(type1_type, 8);
    WriteU8(type1_type, 0);
    WriteU8(type1_type, 0x03);
    WriteU32(type1_type, 0);

    // Entry 1: greeting -> global string 1
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

    // Entry 0: max_players -> 16
    WriteU16(type2_type, 8);
    WriteU16(type2_type, 0);
    WriteU32(type2_type, 2);
    WriteU16(type2_type, 8);
    WriteU8(type2_type, 0);
    WriteU8(type2_type, 0x10);
    WriteU32(type2_type, 16);

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

    // Entry 0: is_multiplayer -> true
    WriteU16(type3_type, 8);
    WriteU16(type3_type, 0);
    WriteU32(type3_type, 3);
    WriteU16(type3_type, 8);
    WriteU8(type3_type, 0);
    WriteU8(type3_type, 0x12);
    WriteU32(type3_type, 1);

    // Package chunk
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

    // Root table
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

} // namespace

TEST(ResourceTableTest, ParsesValidTableAndQueriesResources)
{
    auto data = BuildTestResourceTable("com.example.app");
    auto table = ResourceTable::Open(data);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->GetPackageId(), 0x7FU);
    EXPECT_EQ(table->GetPackageName(), "com.example.app");

    // Strings
    auto title = table->GetString(0x7F010000);
    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "Test App Name");

    auto greeting = table->GetString(0x7F010001);
    ASSERT_TRUE(greeting.has_value());
    EXPECT_EQ(*greeting, "Hello from Portage ARSC");

    // Integer
    auto max_players = table->GetInteger(0x7F020000);
    ASSERT_TRUE(max_players.has_value());
    EXPECT_EQ(*max_players, 16);

    // Boolean
    auto is_multi = table->GetBoolean(0x7F030000);
    ASSERT_TRUE(is_multi.has_value());
    EXPECT_TRUE(*is_multi);

    // Identifier lookup
    uint32_t id1 = table->GetIdentifier("app_title", "string", "com.example.app");
    EXPECT_EQ(id1, 0x7F010000U);

    uint32_t id2 = table->GetIdentifier("max_players", "integer", "com.example.app");
    EXPECT_EQ(id2, 0x7F020000U);

    uint32_t id3 = table->GetIdentifier("is_multiplayer", "bool", "com.example.app");
    EXPECT_EQ(id3, 0x7F030000U);

    // Non-existent identifier
    EXPECT_EQ(table->GetIdentifier("missing_key", "string", "com.example.app"), 0U);
}

TEST(ResourceTableTest, HandlesInvalidDataGracefully)
{
    EXPECT_EQ(ResourceTable::Open({}), nullptr);

    std::vector<uint8_t> truncated = {0x02, 0x00, 0x0c, 0x00};
    EXPECT_EQ(ResourceTable::Open(truncated), nullptr);

    std::vector<uint8_t> corrupted(64, 0xFF);
    EXPECT_EQ(ResourceTable::Open(corrupted), nullptr);
}

TEST(ResourceTableTest, ReturnsNulloptForMissingResourceIds)
{
    auto data = BuildTestResourceTable("com.example.app");
    auto table = ResourceTable::Open(data);
    ASSERT_NE(table, nullptr);

    EXPECT_FALSE(table->GetString(0x7F999999).has_value());
    EXPECT_FALSE(table->GetInteger(0x7F999999).has_value());
    EXPECT_FALSE(table->GetBoolean(0x7F999999).has_value());
    EXPECT_FALSE(table->GetResource(0x7F999999).has_value());
}

} // namespace portage::package
