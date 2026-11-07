#pragma once

#include "property_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace portage::property {

class PropertyManager {
  public:
    static PropertyManager &Instance() noexcept;

    bool Initialize(const std::filesystem::path &rootfs_path = "") noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::string GetProperty(std::string_view name,
                                          std::string_view default_val = "") const;
    bool SetProperty(std::string_view name, std::string_view value);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> GetAllProperties() const;

    [[nodiscard]] HANDLE GetPropertyAreaMapping() const noexcept { return map_handle_; }
    [[nodiscard]] uint8_t *GetPropertyAreaBuffer() const noexcept { return prop_area_; }
    [[nodiscard]] size_t GetPropertyAreaSize() const noexcept { return PROP_AREA_SIZE; }

    [[nodiscard]] HANDLE GetSerialMapping() const noexcept { return serial_map_handle_; }
    [[nodiscard]] uint8_t *GetSerialBuffer() const noexcept { return serial_area_; }
    [[nodiscard]] size_t GetSerialSize() const noexcept { return 4096; }

    void RegisterPropertyArea(uint8_t *p);
    void RegisterSerialArea(uint8_t *p);

    uint32_t HandleSetPropMessage(std::span<const uint8_t> payload);

  private:
    PropertyManager();
    ~PropertyManager();

    PropertyManager(const PropertyManager &) = delete;
    PropertyManager &operator=(const PropertyManager &) = delete;

    void PopulateDefaultProperties();
    void LoadBuildPropFile(const std::filesystem::path &prop_file);
    void LoadEnvironmentOverrides();

    uint32_t AllocateObj(size_t size);
    PropTrieNode *ToNode(uint32_t off);
    PropInfo *ToPropInfo(uint32_t off);

    PropTrieNode *FindOrCreateChildNode(PropTrieNode *parent, std::string_view segment);
    PropInfo *FindPropInfo(std::string_view name);
    PropInfo *CreateOrUpdatePropInfo(std::string_view name, std::string_view value);
    void SyncViews();

    mutable std::recursive_mutex mutex_{};
    std::map<std::string, std::string> properties_{};
    std::vector<uint8_t *> area_views_{};
    std::vector<uint8_t *> serial_views_{};

    HANDLE map_handle_{NULL};
    uint8_t *prop_area_{nullptr};

    HANDLE serial_map_handle_{NULL};
    uint8_t *serial_area_{nullptr};

    uint32_t global_serial_{1};
    bool initialized_{false};
};

} // namespace portage::property
