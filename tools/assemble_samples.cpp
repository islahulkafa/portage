#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

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
    std::vector<std::string> string_pool = {"manifest",
                                            "package",
                                            pkg_name,
                                            "versionCode",
                                            "versionName",
                                            "1.0.0",
                                            "uses-sdk",
                                            "minSdkVersion",
                                            "targetSdkVersion",
                                            "application",
                                            "hasCode",
                                            "activity",
                                            "name",
                                            "android.app.NativeActivity",
                                            "meta-data",
                                            "android.app.lib_name",
                                            "value",
                                            lib_name,
                                            "http://schemas.android.com/apk/res/android"};

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

    WriteU16(sp_chunk, 0x0001);
    WriteU16(sp_chunk, static_cast<uint16_t>(sp_hdr_size));
    WriteU32(sp_chunk, sp_chunk_size);
    WriteU32(sp_chunk, str_count);
    WriteU32(sp_chunk, 0);
    WriteU32(sp_chunk, (1 << 8));
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
            WriteU32(body, 18);
            WriteU32(body, attr.first);
            WriteU32(body, attr.second);
            WriteU16(body, 8);
            WriteU8(body, 0);
            WriteU8(body, 0x03);
            WriteU32(body, attr.second);
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

    write_start_elem(0, {{1, 2}, {3, 5}, {4, 5}});

    write_start_elem(6, {{7, 5}, {8, 5}});
    write_end_elem(6);

    write_start_elem(9, {{10, 5}});

    write_start_elem(11, {{12, 13}});

    write_start_elem(14, {{12, 15}, {16, 17}});
    write_end_elem(14);

    write_end_elem(11);
    write_end_elem(9);
    write_end_elem(0);

    std::vector<uint8_t> doc;
    uint32_t doc_size = 8 + static_cast<uint32_t>(sp_chunk.size() + body.size());

    WriteU16(doc, 0x0003);
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

    WriteU16(pool, 0x0001);
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

std::vector<uint8_t> BuildResourceTable(const std::string &pkg_name,
                                        const std::string &second_string = "Welcome to Portage!")
{
    std::vector<uint8_t> table;

    std::vector<std::string> global_strings = {
        "Portage Game Engine",
        second_string,
    };
    auto global_pool = BuildStringPool(global_strings, true);

    std::vector<std::string> type_strings = {
        "string",
        "integer",
        "bool",
    };
    auto type_pool = BuildStringPool(type_strings, true);

    std::vector<std::string> key_strings = {
        "app_name",
        "welcome_message",
        "game_difficulty",
        "sound_enabled",
    };
    auto key_pool = BuildStringPool(key_strings, true);

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

namespace {

void WriteU32BE(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void AppendPngChunk(std::vector<uint8_t> &png, const char *type4, const std::vector<uint8_t> &body)
{
    WriteU32BE(png, static_cast<uint32_t>(body.size()));
    std::vector<uint8_t> crc_input(4 + body.size());
    std::memcpy(crc_input.data(), type4, 4);
    if (!body.empty()) {
        std::memcpy(crc_input.data() + 4, body.data(), body.size());
    }
    png.insert(png.end(), crc_input.begin(), crc_input.end());
    WriteU32BE(png, CalculateCrc32(crc_input.data(), crc_input.size()));
}

std::vector<uint8_t> BuildShowcasePng4x4()
{
    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    WriteU32BE(ihdr, 4);
    WriteU32BE(ihdr, 4);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendPngChunk(png, "IHDR", ihdr);

    std::vector<uint8_t> scanlines(4 * (1 + 4 * 4), 0);
    for (int y = 0; y < 4; ++y) {
        size_t row_off = static_cast<size_t>(y) * 17;
        scanlines[row_off] = 0;
        for (int x = 0; x < 4; ++x) {
            size_t px = row_off + 1 + static_cast<size_t>(x) * 4;
            scanlines[px + 0] = (x < 2) ? 255 : 40;
            scanlines[px + 1] = (y < 2) ? 200 : 120;
            scanlines[px + 2] = 60;
            scanlines[px + 3] = 255;
        }
    }

    uLongf comp_cap = compressBound(static_cast<uLong>(scanlines.size()));
    std::vector<uint8_t> idat(comp_cap);
    compress2(idat.data(), &comp_cap, scanlines.data(), static_cast<uLong>(scanlines.size()),
              Z_DEFAULT_COMPRESSION);
    idat.resize(comp_cap);
    AppendPngChunk(png, "IDAT", idat);
    AppendPngChunk(png, "IEND", {});
    return png;
}

std::vector<uint8_t> BuildShowcaseGif2x2()
{
    static const uint8_t kGif[] = {
        'G',  'I',  'F',  '8',  '9',  'a',  0x02, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00, 0xFF, 0x00,
        0x00, 0x00, 0x00, 0xFF, 0x21, 0xFF, 0x0B, 'N',  'E',  'T',  'S',  'C',  'A',  'P',  'E',
        '2',  '.',  '0',  0x03, 0x01, 0x02, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x04, 0x0C, 0x00, 0x00,
        0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x02, 0x02, 0x84, 0x51,
        0x00, 0x21, 0xF9, 0x04, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x00, 0x00, 0x02, 0x02, 0x8C, 0x53, 0x00, 0x3B};
    return std::vector<uint8_t>(std::begin(kGif), std::end(kGif));
}

std::vector<uint8_t> DeflateRaw(const std::vector<uint8_t> &src)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        return {};
    }
    std::vector<uint8_t> out(deflateBound(&zs, static_cast<uLong>(src.size())) + 16);
    zs.next_in = const_cast<Bytef *>(src.data());
    zs.avail_in = static_cast<uInt>(src.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

} // namespace

struct ZipInputEntry {
    std::string name;
    std::vector<uint8_t> data;
    bool deflate_compress{false};
};

std::vector<uint8_t> AssembleZipEntries(const std::vector<ZipInputEntry> &inputs)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> payload;
        uint16_t method{0};
        uint32_t uncomp_size{0};
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<FileEntry> entries;
    for (const auto &in : inputs) {
        FileEntry fe{};
        fe.name = in.name;
        fe.uncomp_size = static_cast<uint32_t>(in.data.size());
        fe.crc = CalculateCrc32(in.data.data(), in.data.size());
        if (in.deflate_compress) {
            auto comp = DeflateRaw(in.data);
            if (!comp.empty()) {
                fe.method = 8;
                fe.payload = std::move(comp);
            }
        }
        if (fe.payload.empty()) {
            fe.method = 0;
            fe.payload = in.data;
        }
        entries.push_back(std::move(fe));
    }

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, e.method);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.payload.size()));
        WriteU32(zip, e.uncomp_size);
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.payload.begin(), e.payload.end());
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    for (const auto &e : entries) {
        WriteU32(zip, 0x02014b50);
        WriteU16(zip, 20);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, e.method);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.payload.size()));
        WriteU32(zip, e.uncomp_size);
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

std::vector<uint8_t>
AssembleZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files)
{
    std::vector<ZipInputEntry> inputs;
    inputs.reserve(files.size());
    for (const auto &[name, data] : files) {
        inputs.push_back({name, data, false});
    }
    return AssembleZipEntries(inputs);
}

std::vector<uint8_t>
AssembleApk(const std::vector<uint8_t> &manifest, const std::vector<uint8_t> &resources,
            const std::vector<std::pair<std::string, std::vector<uint8_t>>> &assets,
            const std::vector<std::pair<std::string, std::vector<uint8_t>>> &libs)
{
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    files.push_back({"AndroidManifest.xml", manifest});
    files.push_back({"resources.arsc", resources});
    for (const auto &asset : assets) {
        files.push_back({asset.first, asset.second});
    }
    for (const auto &lib : libs) {
        files.push_back({"lib/x86_64/" + lib.first, lib.second});
    }
    return AssembleZip(files);
}

int main(int argc, char *argv[])
{
    bool showcase = false;
    int arg_idx = 1;
    if (argc >= 2 && std::string(argv[1]) == "--showcase") {
        showcase = true;
        arg_idx = 2;
    }

    if (argc - arg_idx < 4) {
        std::cerr << "Usage: assemble_samples [--showcase] <out_apk> <pkg_name> <lib_name> "
                     "<so_path> [extra_so_paths...]\n";
        return 1;
    }

    std::string out_apk = argv[arg_idx++];
    std::string pkg_name = argv[arg_idx++];
    std::string lib_name = argv[arg_idx++];

    std::vector<std::pair<std::string, std::vector<uint8_t>>> libs;
    for (int i = arg_idx; i < argc; ++i) {
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

    if (showcase) {
        auto resources = BuildResourceTable(pkg_name, "res/drawable/sample_logo.png");
        auto png_bytes = BuildShowcasePng4x4();
        auto gif_bytes = BuildShowcaseGif2x2();

        std::vector<ZipInputEntry> entries = {
            {"AndroidManifest.xml", manifest, false},
            {"resources.arsc", resources, false},
            {"assets/test.png", png_bytes, true},
            {"assets/anim.gif", gif_bytes, true},
            {"res/drawable/sample_logo.png", png_bytes, false},
        };
        for (const auto &lib : libs) {
            entries.push_back({"lib/x86_64/" + lib.first, lib.second, false});
        }
        auto apk_bytes = AssembleZipEntries(entries);
        std::ofstream apk_out(out_apk, std::ios::binary);
        apk_out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                      static_cast<std::streamsize>(apk_bytes.size()));
        std::cout << "Successfully assembled showcase APK: " << out_apk << " (" << apk_bytes.size()
                  << " bytes)\n";
        return 0;
    }

    auto resources = BuildResourceTable(pkg_name);

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

    std::string obb_asset = "OBB_CONTENT_OK\n";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> obb_files = {
        {"obb_asset.txt", std::vector<uint8_t>(obb_asset.begin(), obb_asset.end())},
    };
    auto obb_bytes = AssembleZip(obb_files);

    uint32_t footer_size = static_cast<uint32_t>(32 + pkg_name.size());
    WriteU32(obb_bytes, 1);
    WriteU32(obb_bytes, 1);
    WriteU32(obb_bytes, 0);
    for (int j = 0; j < 8; ++j) {
        WriteU8(obb_bytes, 0);
    }
    WriteU32(obb_bytes, static_cast<uint32_t>(pkg_name.size()));
    obb_bytes.insert(obb_bytes.end(), pkg_name.begin(), pkg_name.end());
    WriteU32(obb_bytes, footer_size);
    WriteU32(obb_bytes, 0x01059983);

    auto apk_path_obj = stdfs::path(out_apk);
    auto obb_filename = "main.1." + pkg_name + ".obb";
    auto obb_out_path = apk_path_obj.parent_path() / obb_filename;

    std::ofstream obb_out(obb_out_path, std::ios::binary);
    obb_out.write(reinterpret_cast<const char *>(obb_bytes.data()),
                  static_cast<std::streamsize>(obb_bytes.size()));
    obb_out.close();

    std::cout << "Successfully assembled: " << out_apk << " (" << apk_bytes.size() << " bytes, "
              << libs.size() << " libraries, " << assets.size() << " assets)\n";
    std::cout << "Successfully assembled OBB: " << obb_out_path << " (" << obb_bytes.size()
              << " bytes)\n";
    return 0;
}
