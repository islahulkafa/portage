#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace portage::package {

struct ManifestInfo {
    std::string package_name{};
    int version_code{1};
    std::string version_name{"1.0"};
    int min_sdk_version{21};
    int target_sdk_version{34};
    bool has_code{true};
    std::string application_label{};
    std::string application_name{};
    std::string main_activity_name{};
    std::string launcher_activity_name{};
    std::string native_library_name{}; // e.g. "main" -> "libmain.so"
};

class ManifestParser {
  public:
    static std::optional<ManifestInfo> Parse(std::span<const uint8_t> data);

  private:
    explicit ManifestParser(std::span<const uint8_t> data);

    bool ParseDocument(ManifestInfo &out_info);
    bool ParseStringPool(size_t offset, size_t chunk_size);
    std::string GetString(uint32_t index) const;

    std::span<const uint8_t> data_{};
    std::vector<std::string> string_pool_{};
};

} // namespace portage::package
