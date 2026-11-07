#include "resource_table.hpp"

#include <algorithm>
#include <cstring>

namespace portage::package {

namespace {

inline uint16_t ReadU16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

std::string Utf16ToUtf8(const char16_t *u16_str, size_t len)
{
    std::string utf8;
    utf8.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char16_t c = u16_str[i];
        if (c == 0)
            break;
        if (c < 0x80) {
            utf8.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return utf8;
}

} // namespace

std::unique_ptr<ResourceTable> ResourceTable::Open(std::span<const uint8_t> data)
{
    auto table = std::unique_ptr<ResourceTable>(new ResourceTable());
    if (!table->Parse(data)) {
        return nullptr;
    }
    return table;
}

bool ResourceTable::Parse(std::span<const uint8_t> data)
{
    if (data.size() < sizeof(ResTable_header)) {
        return false;
    }

    const auto *table_hdr = reinterpret_cast<const ResTable_header *>(data.data());
    if (table_hdr->header.type != RES_TABLE_TYPE || table_hdr->header.header_size < sizeof(ResTable_header)) {
        return false;
    }

    size_t offset = table_hdr->header.header_size;
    while (offset + sizeof(ResChunk_header) <= data.size()) {
        const auto *chunk = reinterpret_cast<const ResChunk_header *>(data.data() + offset);
        if (chunk->size == 0 || offset + chunk->size > data.size()) {
            break;
        }

        if (chunk->type == RES_STRING_POOL_TYPE && global_strings_.empty()) {
            std::span<const uint8_t> pool_data(data.data() + offset, chunk->size);
            if (!ParseStringPool(pool_data, global_strings_)) {
                return false;
            }
        } else if (chunk->type == RES_TABLE_PACKAGE_TYPE) {
            std::span<const uint8_t> pkg_data(data.data() + offset, chunk->size);
            if (!ParsePackage(pkg_data)) {
                return false;
            }
        }

        offset += chunk->size;
    }

    return !entries_.empty() || package_id_ != 0;
}

bool ResourceTable::ParseStringPool(std::span<const uint8_t> pool_data,
                                   std::vector<std::string> &out_strings)
{
    if (pool_data.size() < sizeof(ResStringPool_header)) {
        return false;
    }

    const auto *header = reinterpret_cast<const ResStringPool_header *>(pool_data.data());
    if (header->header.type != RES_STRING_POOL_TYPE) {
        return false;
    }

    uint32_t string_count = header->string_count;
    bool is_utf8 = (header->flags & (1 << 8)) != 0;
    uint32_t strings_start = header->strings_start;

    if (strings_start > pool_data.size()) {
        return false;
    }

    size_t offsets_size = string_count * sizeof(uint32_t);
    if (sizeof(ResStringPool_header) + offsets_size > pool_data.size()) {
        return false;
    }

    out_strings.clear();
    out_strings.reserve(string_count);

    const auto *offsets =
        reinterpret_cast<const uint32_t *>(pool_data.data() + sizeof(ResStringPool_header));

    for (uint32_t i = 0; i < string_count; ++i) {
        uint32_t str_offset = offsets[i];
        size_t abs_offset = strings_start + str_offset;
        if (abs_offset >= pool_data.size()) {
            out_strings.emplace_back("");
            continue;
        }

        const uint8_t *p = pool_data.data() + abs_offset;
        if (is_utf8) {
            // UTF-8 string: skip UTF-16 character count, then read UTF-8 byte count
            if (*p & 0x80) {
                p += 2;
            } else {
                p += 1;
            }
            if (p >= pool_data.data() + pool_data.size()) {
                out_strings.emplace_back("");
                continue;
            }

            size_t len = 0;
            if (*p & 0x80) {
                len = static_cast<size_t>(((p[0] & 0x7F) << 8) | p[1]);
                p += 2;
            } else {
                len = *p;
                p += 1;
            }

            if (p + len <= pool_data.data() + pool_data.size()) {
                out_strings.emplace_back(reinterpret_cast<const char *>(p), len);
            } else {
                out_strings.emplace_back("");
            }
        } else {
            // UTF-16LE string
            if (p + 2 > pool_data.data() + pool_data.size()) {
                out_strings.emplace_back("");
                continue;
            }
            uint16_t len = ReadU16(p);
            p += 2;
            if (len & 0x8000) {
                if (p + 2 > pool_data.data() + pool_data.size()) {
                    out_strings.emplace_back("");
                    continue;
                }
                len = static_cast<uint16_t>(((len & 0x7FFF) << 16) | ReadU16(p));
                p += 2;
            }

            size_t byte_len = len * sizeof(char16_t);
            if (p + byte_len <= pool_data.data() + pool_data.size()) {
                out_strings.push_back(
                    Utf16ToUtf8(reinterpret_cast<const char16_t *>(p), len));
            } else {
                out_strings.emplace_back("");
            }
        }
    }

    return true;
}

bool ResourceTable::ParsePackage(std::span<const uint8_t> pkg_data)
{
    if (pkg_data.size() < sizeof(ResTable_package)) {
        return false;
    }

    const auto *pkg_hdr = reinterpret_cast<const ResTable_package *>(pkg_data.data());
    if (pkg_hdr->header.type != RES_TABLE_PACKAGE_TYPE) {
        return false;
    }

    package_id_ = pkg_hdr->id;
    package_name_ = Utf16ToUtf8(pkg_hdr->name, 128);

    // Parse type strings string pool
    if (pkg_hdr->type_strings != 0 && pkg_hdr->type_strings < pkg_data.size()) {
        std::span<const uint8_t> pool_data(pkg_data.data() + pkg_hdr->type_strings,
                                           pkg_data.size() - pkg_hdr->type_strings);
        ParseStringPool(pool_data, type_strings_);
    }

    // Parse key strings string pool
    if (pkg_hdr->key_strings != 0 && pkg_hdr->key_strings < pkg_data.size()) {
        std::span<const uint8_t> pool_data(pkg_data.data() + pkg_hdr->key_strings,
                                           pkg_data.size() - pkg_hdr->key_strings);
        ParseStringPool(pool_data, key_strings_);
    }

    // Traverse sub-chunks within package
    size_t offset = pkg_hdr->header.header_size;
    while (offset + sizeof(ResChunk_header) <= pkg_data.size()) {
        const auto *chunk = reinterpret_cast<const ResChunk_header *>(pkg_data.data() + offset);
        if (chunk->size == 0 || offset + chunk->size > pkg_data.size()) {
            break;
        }

        if (chunk->type == RES_TABLE_TYPE_TYPE) {
            if (chunk->header_size >= 20 && chunk->size >= chunk->header_size) {
                const auto *type_hdr =
                    reinterpret_cast<const ResTable_type *>(pkg_data.data() + offset);
                uint8_t type_id = type_hdr->id;
                uint32_t entry_count = type_hdr->entry_count;
                uint32_t entries_start = type_hdr->entries_start;

                std::string type_name;
                if (type_id > 0 && type_id <= type_strings_.size()) {
                    type_name = type_strings_[type_id - 1];
                }

                size_t offsets_start = offset + type_hdr->header.header_size;
                if (offsets_start + entry_count * sizeof(uint32_t) <= pkg_data.size()) {
                    const auto *entry_offsets =
                        reinterpret_cast<const uint32_t *>(pkg_data.data() + offsets_start);

                    for (uint32_t entry_idx = 0; entry_idx < entry_count; ++entry_idx) {
                        uint32_t entry_off = entry_offsets[entry_idx];
                        if (entry_off == RES_NO_ENTRY) {
                            continue;
                        }

                        size_t abs_entry_off = offset + entries_start + entry_off;
                        if (abs_entry_off + sizeof(ResTable_entry) > pkg_data.size()) {
                            continue;
                        }

                        const auto *res_entry = reinterpret_cast<const ResTable_entry *>(
                            pkg_data.data() + abs_entry_off);

                        std::string key_name;
                        if (res_entry->key < key_strings_.size()) {
                            key_name = key_strings_[res_entry->key];
                        }

                        uint32_t full_id =
                            (package_id_ << 24) | (static_cast<uint32_t>(type_id) << 16) | entry_idx;

                        if (!(res_entry->flags & RES_FLAG_COMPLEX)) {
                            size_t val_off = abs_entry_off + res_entry->size;
                            if (val_off + sizeof(Res_value) <= pkg_data.size()) {
                                const auto *val = reinterpret_cast<const Res_value *>(
                                    pkg_data.data() + val_off);

                                ResourceValue res_val;
                                res_val.data_type = val->data_type;
                                res_val.data = val->data;

                                if (val->data_type == TYPE_STRING && val->data < global_strings_.size()) {
                                    res_val.string_value = global_strings_[val->data];
                                }

                                ResourceEntry entry;
                                entry.id = full_id;
                                entry.type_name = type_name;
                                entry.key_name = key_name;
                                entry.value = std::move(res_val);

                                entries_[full_id] = std::move(entry);
                            }
                        }
                    }
                }
            }
        }

        offset += chunk->size;
    }

    return true;
}

std::optional<std::string> ResourceTable::GetString(uint32_t res_id) const
{
    auto it = entries_.find(res_id);
    if (it != entries_.end() && it->second.value.data_type == TYPE_STRING) {
        return it->second.value.string_value;
    }
    return std::nullopt;
}

std::optional<int32_t> ResourceTable::GetInteger(uint32_t res_id) const
{
    auto it = entries_.find(res_id);
    if (it != entries_.end()) {
        uint8_t dt = it->second.value.data_type;
        if (dt == TYPE_INT_DEC || dt == TYPE_INT_HEX) {
            return static_cast<int32_t>(it->second.value.data);
        }
    }
    return std::nullopt;
}

std::optional<bool> ResourceTable::GetBoolean(uint32_t res_id) const
{
    auto it = entries_.find(res_id);
    if (it != entries_.end() && it->second.value.data_type == TYPE_INT_BOOLEAN) {
        return it->second.value.data != 0;
    }
    return std::nullopt;
}

std::optional<ResourceValue> ResourceTable::GetResource(uint32_t res_id) const
{
    auto it = entries_.find(res_id);
    if (it != entries_.end()) {
        return it->second.value;
    }
    return std::nullopt;
}

uint32_t ResourceTable::GetIdentifier(std::string_view name, std::string_view type,
                                      std::string_view package) const
{
    if (!package.empty() && package != package_name_) {
        return 0;
    }

    for (const auto &[id, entry] : entries_) {
        if (entry.key_name == name) {
            if (type.empty() || entry.type_name == type) {
                return id;
            }
        }
    }

    return 0;
}

} // namespace portage::package
