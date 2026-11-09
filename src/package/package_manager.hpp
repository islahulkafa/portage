#pragma once

#include "asset_manager.hpp"
#include "manifest_parser.hpp"
#include "resource_table.hpp"
#include "zip_archive.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::package {

struct PackageInfo {
    ManifestInfo manifest{};
    std::filesystem::path apk_path{};
    std::filesystem::path app_dir{};
    std::filesystem::path lib_dir{};
    std::filesystem::path data_dir{};
    std::filesystem::path cache_dir{};
    std::filesystem::path external_data_dir{};
    std::filesystem::path obb_dir{};
    std::vector<std::string> native_libraries{};
    std::shared_ptr<ZipArchive> archive{nullptr};
    std::shared_ptr<ResourceTable> resource_table{nullptr};
    AAssetManager *asset_manager{nullptr};
};

class PackageManager {
  public:
    static PackageManager &Instance() noexcept;

    bool Initialize(const std::filesystem::path &sandbox_base = "") noexcept;
    void Reset() noexcept;

    std::optional<PackageInfo> InstallPackage(const std::filesystem::path &apk_path);

    [[nodiscard]] const PackageInfo *GetPackage(std::string_view package_name) const;
    [[nodiscard]] const PackageInfo *GetActivePackage() const;
    void SetActivePackage(std::string_view package_name);

    [[nodiscard]] std::filesystem::path GetPrimaryLibraryPath(const PackageInfo &pkg) const;
    [[nodiscard]] std::filesystem::path GetObbDir(std::string_view package_name) const;
    [[nodiscard]] std::filesystem::path GetObbMountBase() const;
    [[nodiscard]] const std::filesystem::path &GetSandboxBase() const noexcept
    {
        return sandbox_base_;
    }

  private:
    PackageManager();
    ~PackageManager();

    PackageManager(const PackageManager &) = delete;
    PackageManager &operator=(const PackageManager &) = delete;

    std::filesystem::path DetermineDefaultSandbox() const;

    mutable std::mutex mutex_{};
    std::filesystem::path sandbox_base_{};
    std::unordered_map<std::string, PackageInfo> packages_{};
    std::string active_package_name_{};
    bool initialized_{false};
};

} // namespace portage::package
