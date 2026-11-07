#include "manifest_parser.hpp"

#include <algorithm>
#include <cstring>

namespace portage::package {

namespace {

constexpr uint16_t CHUNK_AXML_ROOT = 0x0003;
constexpr uint16_t CHUNK_STRING_POOL = 0x0001;
constexpr uint16_t CHUNK_START_ELEMENT = 0x0102;

inline uint16_t ReadU16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t ReadU32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0] | (static_cast<uint32_t>(p[1]) << 8) |
                                 (static_cast<uint32_t>(p[2]) << 16) |
                                 (static_cast<uint32_t>(p[3]) << 24));
}

} // namespace

ManifestParser::ManifestParser(std::span<const uint8_t> data) : data_(data) {}

std::optional<ManifestInfo> ManifestParser::Parse(std::span<const uint8_t> data)
{
    ManifestParser parser(data);
    ManifestInfo info;
    if (!parser.ParseDocument(info)) {
        return std::nullopt;
    }
    return info;
}

std::string ManifestParser::GetString(uint32_t index) const
{
    if (index < string_pool_.size()) {
        return string_pool_[index];
    }
    return "";
}

bool ManifestParser::ParseStringPool(size_t offset, size_t chunk_size)
{
    if (offset + 28 > data_.size() || offset + chunk_size > data_.size()) {
        return false;
    }

    const uint8_t *p = data_.data() + offset;
    uint32_t string_count = ReadU32(p + 8);
    uint32_t flags = ReadU32(p + 16);
    bool is_utf8 = (flags & (1 << 8)) != 0;
    uint32_t strings_start = ReadU32(p + 20);

    if (offset + strings_start > data_.size()) {
        return false;
    }

    string_pool_.clear();
    string_pool_.reserve(string_count);

    for (uint32_t i = 0; i < string_count; ++i) {
        if (offset + 28 + i * 4 + 4 > data_.size()) {
            break;
        }

        uint32_t str_offset = ReadU32(p + 28 + i * 4);
        size_t abs_str_offset = offset + strings_start + str_offset;

        if (abs_str_offset >= data_.size()) {
            string_pool_.emplace_back("");
            continue;
        }

        const uint8_t *s = data_.data() + abs_str_offset;
        if (is_utf8) {
            // UTF-8: skip char count byte(s), read byte count byte(s)
            if (*s & 0x80)
                s += 2;
            else
                s += 1;
            size_t len = 0;
            if (*s & 0x80) {
                len = static_cast<size_t>(((s[0] & 0x7F) << 8) | s[1]);
                s += 2;
            } else {
                len = *s;
                s += 1;
            }

            if (s + len <= data_.data() + data_.size()) {
                string_pool_.emplace_back(reinterpret_cast<const char *>(s), len);
            } else {
                string_pool_.emplace_back("");
            }
        } else {
            // UTF-16: read 16-bit char length
            size_t char_len = ReadU16(s);
            s += 2;
            if (char_len & 0x8000) {
                char_len = ((char_len & 0x7FFF) << 16) | ReadU16(s);
                s += 2;
            }

            std::string utf8_str;
            utf8_str.reserve(char_len);
            for (size_t c = 0; c < char_len; ++c) {
                if (s + c * 2 + 2 > data_.data() + data_.size())
                    break;
                uint16_t w = ReadU16(s + c * 2);
                if (w < 0x80) {
                    utf8_str.push_back(static_cast<char>(w));
                } else if (w < 0x800) {
                    utf8_str.push_back(static_cast<char>(0xC0 | (w >> 6)));
                    utf8_str.push_back(static_cast<char>(0x80 | (w & 0x3F)));
                } else {
                    utf8_str.push_back(static_cast<char>(0xE0 | (w >> 12)));
                    utf8_str.push_back(static_cast<char>(0x80 | ((w >> 6) & 0x3F)));
                    utf8_str.push_back(static_cast<char>(0x80 | (w & 0x3F)));
                }
            }
            string_pool_.push_back(std::move(utf8_str));
        }
    }

    return true;
}

bool ManifestParser::ParseDocument(ManifestInfo &out_info)
{
    if (data_.size() < 8) {
        return false;
    }

    uint16_t root_type = ReadU16(data_.data());
    if (root_type != CHUNK_AXML_ROOT) {
        return false;
    }

    size_t offset = 8;
    std::string current_meta_name;

    while (offset + 8 <= data_.size()) {
        const uint8_t *p = data_.data() + offset;
        uint16_t type = ReadU16(p);
        uint32_t chunk_size = ReadU32(p + 4);

        if (chunk_size == 0 || offset + chunk_size > data_.size()) {
            break;
        }

        if (type == CHUNK_STRING_POOL) {
            if (!ParseStringPool(offset, chunk_size)) {
                return false;
            }
        } else if (type == CHUNK_START_ELEMENT && offset + 36 <= data_.size()) {
            uint32_t tag_idx = ReadU32(p + 20);
            std::string tag = GetString(tag_idx);

            uint16_t attr_start = ReadU16(p + 24);
            uint16_t attr_size = ReadU16(p + 26);
            if (attr_size == 0)
                attr_size = 20;
            uint16_t attr_count = ReadU16(p + 28);

            std::string meta_key;
            std::string meta_val;

            size_t attr_base =
                (attr_start >= 36) ? (offset + attr_start) : (offset + 16 + attr_start);

            for (uint16_t i = 0; i < attr_count; ++i) {
                size_t attr_off = attr_base + i * attr_size;
                if (attr_off + 20 > data_.size())
                    break;

                const uint8_t *a = data_.data() + attr_off;
                uint32_t name_idx = ReadU32(a + 4);
                uint32_t raw_val_idx = ReadU32(a + 8);
                uint32_t data_val = ReadU32(a + 16);

                std::string attr_name = GetString(name_idx);
                std::string str_val = (raw_val_idx != 0xFFFFFFFF) ? GetString(raw_val_idx) : "";

                if (tag == "manifest") {
                    if (attr_name == "package" && !str_val.empty()) {
                        out_info.package_name = str_val;
                    } else if (attr_name == "versionCode") {
                        out_info.version_code = static_cast<int>(data_val);
                    } else if (attr_name == "versionName" && !str_val.empty()) {
                        out_info.version_name = str_val;
                    }
                } else if (tag == "uses-sdk") {
                    if (attr_name == "minSdkVersion") {
                        out_info.min_sdk_version = static_cast<int>(data_val);
                    } else if (attr_name == "targetSdkVersion") {
                        out_info.target_sdk_version = static_cast<int>(data_val);
                    }
                } else if (tag == "application") {
                    if (attr_name == "hasCode") {
                        out_info.has_code = (data_val != 0);
                    } else if (attr_name == "label" && !str_val.empty()) {
                        out_info.application_label = str_val;
                    }
                } else if (tag == "activity") {
                    if (attr_name == "name" && out_info.main_activity_name.empty()) {
                        out_info.main_activity_name = str_val;
                    }
                } else if (tag == "meta-data") {
                    if (attr_name == "name") {
                        meta_key = str_val;
                    } else if (attr_name == "value") {
                        meta_val = str_val;
                    }
                }
            }

            if (tag == "meta-data" && meta_key == "android.app.lib_name" && !meta_val.empty()) {
                out_info.native_library_name = meta_val;
            }
        }

        offset += chunk_size;
    }

    if (out_info.native_library_name.empty()) {
        out_info.native_library_name = "main";
    }

    return !out_info.package_name.empty();
}

} // namespace portage::package
