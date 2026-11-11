#pragma once

#include "resource_types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::package {

struct ResourceValue {
    uint8_t data_type{0};
    uint32_t data{0};
    std::string string_value{};
};

struct ResourceEntry {
    uint32_t id{0}; // 0xPPTTEEEE
    std::string type_name{};
    std::string key_name{};
    ResourceValue value{};
};

class ResourceTable {
  public:
    static std::unique_ptr<ResourceTable> Open(std::span<const uint8_t> data);

    [[nodiscard]] std::optional<std::string> GetString(uint32_t res_id) const;
    [[nodiscard]] std::optional<int32_t> GetInteger(uint32_t res_id) const;
    [[nodiscard]] std::optional<bool> GetBoolean(uint32_t res_id) const;
    [[nodiscard]] std::optional<ResourceValue> GetResource(uint32_t res_id) const;

    [[nodiscard]] uint32_t GetIdentifier(std::string_view name, std::string_view type,
                                         std::string_view package) const;

    [[nodiscard]] const std::string &GetPackageName() const noexcept { return package_name_; }
    [[nodiscard]] uint32_t GetPackageId() const noexcept { return package_id_; }
    [[nodiscard]] size_t GetEntryCount() const noexcept { return entries_.size(); }

  private:
    ResourceTable() = default;

    bool Parse(std::span<const uint8_t> data);
    bool ParseStringPool(std::span<const uint8_t> pool_data, std::vector<std::string> &out_strings);
    bool ParsePackage(std::span<const uint8_t> pkg_data);

    std::string package_name_{};
    uint32_t package_id_{0};
    std::vector<std::string> global_strings_{};
    std::vector<std::string> type_strings_{};
    std::vector<std::string> key_strings_{};

    std::unordered_map<uint32_t, ResourceEntry> entries_{};
};

} // namespace portage::package
