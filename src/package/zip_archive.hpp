#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::package {

struct ZipEntryInfo {
    std::string name{};
    uint16_t compression_method{0}; // 0 = Stored, 8 = Deflated
    uint32_t compressed_size{0};
    uint32_t uncompressed_size{0};
    uint32_t crc32{0};
    uint32_t local_header_offset{0};
    uint32_t data_offset{0}; // Computed offset in archive where data payload begins
};

class ZipArchive {
  public:
    ZipArchive() = default;
    ~ZipArchive();

    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;
    ZipArchive(ZipArchive &&) noexcept;
    ZipArchive &operator=(ZipArchive &&) noexcept;

    static std::unique_ptr<ZipArchive> Open(const std::filesystem::path &path);

    [[nodiscard]] bool HasEntry(std::string_view name) const;
    [[nodiscard]] const ZipEntryInfo *GetEntryInfo(std::string_view name) const;
    [[nodiscard]] std::vector<ZipEntryInfo> GetAllEntries() const;

    [[nodiscard]] std::optional<std::vector<uint8_t>> ReadEntry(std::string_view name) const;
    [[nodiscard]] std::optional<std::span<const uint8_t>>
    GetRawEntrySpan(std::string_view name) const;
    bool ExtractEntry(std::string_view name, const std::filesystem::path &destination) const;

    [[nodiscard]] const std::filesystem::path &GetPath() const noexcept { return path_; }
    [[nodiscard]] size_t GetFileSize() const noexcept { return file_size_; }

  private:
    bool Initialize();

    std::filesystem::path path_{};
    void *file_handle_{nullptr};
    void *mapping_handle_{nullptr};
    const uint8_t *mapped_data_{nullptr};
    size_t file_size_{0};
    std::unordered_map<std::string, ZipEntryInfo> entries_{};
};

} // namespace portage::package
