#include "zip_archive.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <zlib.h>

#include <cstring>
#include <fstream>

namespace portage::package {

namespace {

constexpr uint32_t SIGNATURE_EOCD = 0x06054b50;
constexpr uint32_t SIGNATURE_CD = 0x02014b50;
constexpr uint32_t SIGNATURE_LOCAL = 0x04034b50;

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

ZipArchive::~ZipArchive()
{
    if (mapped_data_) {
        UnmapViewOfFile(mapped_data_);
        mapped_data_ = nullptr;
    }
    if (mapping_handle_) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ && file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
}

ZipArchive::ZipArchive(ZipArchive &&other) noexcept
    : path_(std::move(other.path_)), file_handle_(other.file_handle_),
      mapping_handle_(other.mapping_handle_), mapped_data_(other.mapped_data_),
      file_size_(other.file_size_), entries_(std::move(other.entries_))
{
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
    other.mapped_data_ = nullptr;
    other.file_size_ = 0;
}

ZipArchive &ZipArchive::operator=(ZipArchive &&other) noexcept
{
    if (this != &other) {
        if (mapped_data_)
            UnmapViewOfFile(mapped_data_);
        if (mapping_handle_)
            CloseHandle(mapping_handle_);
        if (file_handle_ && file_handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_handle_);

        path_ = std::move(other.path_);
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        mapped_data_ = other.mapped_data_;
        file_size_ = other.file_size_;
        entries_ = std::move(other.entries_);

        other.file_handle_ = nullptr;
        other.mapping_handle_ = nullptr;
        other.mapped_data_ = nullptr;
        other.file_size_ = 0;
    }
    return *this;
}

std::unique_ptr<ZipArchive> ZipArchive::Open(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

    auto archive = std::make_unique<ZipArchive>();
    archive->path_ = path;
    if (!archive->Initialize()) {
        return nullptr;
    }
    return archive;
}

bool ZipArchive::Initialize()
{
    file_handle_ =
        CreateFileW(path_.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!file_handle_ || file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(file_handle_, &sz) || sz.QuadPart < 22) {
        return false;
    }
    file_size_ = static_cast<size_t>(sz.QuadPart);

    mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_) {
        return false;
    }

    mapped_data_ =
        static_cast<const uint8_t *>(MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!mapped_data_) {
        return false;
    }

    // Locate EOCD signature (search backward from end)
    size_t search_limit = std::min<size_t>(file_size_, 65535 + 22);
    size_t search_start = file_size_ - search_limit;
    const uint8_t *eocd_ptr = nullptr;

    for (size_t i = file_size_ - 22; i >= search_start; --i) {
        if (ReadU32(mapped_data_ + i) == SIGNATURE_EOCD) {
            eocd_ptr = mapped_data_ + i;
            break;
        }
        if (i == 0)
            break;
    }

    if (!eocd_ptr) {
        return false;
    }

    uint16_t total_entries = ReadU16(eocd_ptr + 10);
    uint32_t cd_size = ReadU32(eocd_ptr + 12);
    uint32_t cd_offset = ReadU32(eocd_ptr + 16);

    if (cd_offset + cd_size > file_size_) {
        return false;
    }

    // Parse Central Directory
    const uint8_t *cd_ptr = mapped_data_ + cd_offset;
    for (uint16_t i = 0; i < total_entries; ++i) {
        if (cd_ptr + 46 > mapped_data_ + file_size_) {
            break;
        }
        if (ReadU32(cd_ptr) != SIGNATURE_CD) {
            break;
        }

        uint16_t method = ReadU16(cd_ptr + 10);
        uint32_t crc = ReadU32(cd_ptr + 16);
        uint32_t comp_sz = ReadU32(cd_ptr + 20);
        uint32_t uncomp_sz = ReadU32(cd_ptr + 24);
        uint16_t name_len = ReadU16(cd_ptr + 28);
        uint16_t extra_len = ReadU16(cd_ptr + 30);
        uint16_t comment_len = ReadU16(cd_ptr + 32);
        uint32_t local_hdr_off = ReadU32(cd_ptr + 42);

        if (cd_ptr + 46 + name_len > mapped_data_ + file_size_) {
            break;
        }

        std::string name(reinterpret_cast<const char *>(cd_ptr + 46), name_len);

        // Inspect local header to calculate exact data offset
        if (local_hdr_off + 30 <= file_size_ &&
            ReadU32(mapped_data_ + local_hdr_off) == SIGNATURE_LOCAL) {
            uint16_t loc_name_len = ReadU16(mapped_data_ + local_hdr_off + 26);
            uint16_t loc_extra_len = ReadU16(mapped_data_ + local_hdr_off + 28);
            uint32_t data_off = local_hdr_off + 30 + loc_name_len + loc_extra_len;

            ZipEntryInfo info;
            info.name = name;
            info.compression_method = method;
            info.compressed_size = comp_sz;
            info.uncompressed_size = uncomp_sz;
            info.crc32 = crc;
            info.local_header_offset = local_hdr_off;
            info.data_offset = data_off;

            entries_[name] = std::move(info);
        }

        cd_ptr += 46 + name_len + extra_len + comment_len;
    }

    return true;
}

bool ZipArchive::HasEntry(std::string_view name) const
{
    return entries_.find(std::string(name)) != entries_.end();
}

const ZipEntryInfo *ZipArchive::GetEntryInfo(std::string_view name) const
{
    auto it = entries_.find(std::string(name));
    if (it != entries_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<ZipEntryInfo> ZipArchive::GetAllEntries() const
{
    std::vector<ZipEntryInfo> res;
    res.reserve(entries_.size());
    for (const auto &[_, info] : entries_) {
        res.push_back(info);
    }
    return res;
}

std::optional<std::span<const uint8_t>> ZipArchive::GetRawEntrySpan(std::string_view name) const
{
    const auto *info = GetEntryInfo(name);
    if (!info || info->compression_method != 0) {
        return std::nullopt;
    }
    if (info->data_offset + info->uncompressed_size > file_size_) {
        return std::nullopt;
    }
    return std::span<const uint8_t>(mapped_data_ + info->data_offset, info->uncompressed_size);
}

std::optional<std::vector<uint8_t>> ZipArchive::ReadEntry(std::string_view name) const
{
    const auto *info = GetEntryInfo(name);
    if (!info) {
        return std::nullopt;
    }
    if (info->data_offset + info->compressed_size > file_size_) {
        return std::nullopt;
    }

    std::vector<uint8_t> result(info->uncompressed_size);

    if (info->compression_method == 0) {
        std::memcpy(result.data(), mapped_data_ + info->data_offset, info->uncompressed_size);
        return result;
    }

    if (info->compression_method == 8) {
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(mapped_data_ + info->data_offset);
        stream.avail_in = info->compressed_size;
        stream.next_out = reinterpret_cast<Bytef *>(result.data());
        stream.avail_out = info->uncompressed_size;

        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return std::nullopt;
        }

        int ret = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);

        if (ret != Z_STREAM_END && ret != Z_OK) {
            return std::nullopt;
        }
        return result;
    }

    return std::nullopt;
}

bool ZipArchive::ExtractEntry(std::string_view name, const std::filesystem::path &destination) const
{
    auto data = ReadEntry(name);
    if (!data.has_value()) {
        return false;
    }

    if (destination.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
    }

    std::ofstream out(destination, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write(reinterpret_cast<const char *>(data->data()),
              static_cast<std::streamsize>(data->size()));
    return out.good();
}

} // namespace portage::package
