#include "package_manager.hpp"
#include "fs/vfs.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace portage::package {

PackageManager &PackageManager::Instance() noexcept
{
    static PackageManager instance;
    return instance;
}

PackageManager::PackageManager() = default;

PackageManager::~PackageManager() { Reset(); }

std::filesystem::path PackageManager::DetermineDefaultSandbox() const
{
    wchar_t local_app_data[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(local_app_data) / "Portage" / "sandbox";
    }
    return std::filesystem::temp_directory_path() / "Portage" / "sandbox";
}

bool PackageManager::Initialize(const std::filesystem::path &sandbox_base) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    if (sandbox_base.empty()) {
        sandbox_base_ = DetermineDefaultSandbox();
    } else {
        sandbox_base_ = sandbox_base;
    }

    std::error_code ec;
    std::filesystem::create_directories(sandbox_base_, ec);
    initialized_ = true;
    return true;
}

void PackageManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[_, pkg] : packages_) {
        if (pkg.asset_manager) {
            AssetManagerBridge::Instance().DestroyManager(pkg.asset_manager);
            pkg.asset_manager = nullptr;
        }
    }
    packages_.clear();
    active_package_name_.clear();
    initialized_ = false;
}

std::optional<PackageInfo> PackageManager::InstallPackage(const std::filesystem::path &apk_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        sandbox_base_ = DetermineDefaultSandbox();
        std::error_code ec;
        std::filesystem::create_directories(sandbox_base_, ec);
        initialized_ = true;
    }

    auto archive = ZipArchive::Open(apk_path);
    if (!archive) {
        return std::nullopt;
    }

    auto manifest_data = archive->ReadEntry("AndroidManifest.xml");
    if (!manifest_data.has_value()) {
        return std::nullopt;
    }

    auto manifest = ManifestParser::Parse(*manifest_data);
    if (!manifest.has_value()) {
        return std::nullopt;
    }

    PackageInfo pkg;
    pkg.manifest = *manifest;
    pkg.apk_path = std::filesystem::absolute(apk_path);
    pkg.archive = std::shared_ptr<ZipArchive>(std::move(archive));

    pkg.app_dir = sandbox_base_ / "data" / "app" / manifest->package_name;
    pkg.lib_dir = pkg.app_dir / "lib" / "x86_64";
    pkg.data_dir = sandbox_base_ / "data" / "data" / manifest->package_name / "files";
    pkg.cache_dir = sandbox_base_ / "data" / "data" / manifest->package_name / "cache";
    pkg.external_data_dir =
        sandbox_base_ / "sdcard" / "Android" / "data" / manifest->package_name / "files";
    pkg.obb_dir = sandbox_base_ / "sdcard" / "Android" / "obb" / manifest->package_name;

    std::error_code ec;
    std::filesystem::create_directories(pkg.app_dir, ec);
    std::filesystem::create_directories(pkg.lib_dir, ec);
    std::filesystem::create_directories(pkg.data_dir, ec);
    std::filesystem::create_directories(pkg.cache_dir, ec);
    std::filesystem::create_directories(pkg.external_data_dir, ec);
    std::filesystem::create_directories(pkg.obb_dir, ec);
    std::filesystem::create_directories(sandbox_base_ / "sdcard" / "Download", ec);
    std::filesystem::create_directories(sandbox_base_ / "sdcard" / "DCIM", ec);
    std::filesystem::create_directories(sandbox_base_ / "sdcard" / "Documents", ec);

    auto *vfs = syscall::SyscallDispatcher::GetVfs();
    if (vfs) {
        vfs->AddMount("/sdcard", sandbox_base_ / "sdcard");
    }

    // Copy base.apk into app directory
    std::filesystem::copy_file(apk_path, pkg.app_dir / "base.apk",
                               std::filesystem::copy_options::overwrite_existing, ec);

    // Copy expansion OBB files alongside the APK into obb_dir
    auto apk_dir = apk_path.parent_path();
    if (!apk_dir.empty() && std::filesystem::exists(apk_dir, ec)) {
        for (const auto &dir_entry : std::filesystem::directory_iterator(apk_dir, ec)) {
            if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".obb") {
                auto obb_filename = dir_entry.path().filename().string();
                if (obb_filename.find(manifest->package_name) != std::string::npos) {
                    std::filesystem::copy_file(dir_entry.path(), pkg.obb_dir / obb_filename,
                                               std::filesystem::copy_options::overwrite_existing,
                                               ec);
                }
            }
        }
    }

    // Extract native libraries for x86_64
    for (const auto &entry : pkg.archive->GetAllEntries()) {
        if (entry.name.starts_with("lib/x86_64/") && entry.name.ends_with(".so")) {
            auto filename = std::filesystem::path(entry.name).filename();
            std::filesystem::path dest = pkg.lib_dir / filename;
            if (pkg.archive->ExtractEntry(entry.name, dest)) {
                pkg.native_libraries.push_back(filename.string());
            }
        }
    }

    // Load binary resource table if present
    auto arsc_data = pkg.archive->ReadEntry("resources.arsc");
    if (arsc_data.has_value()) {
        pkg.resource_table = ResourceTable::Open(*arsc_data);
    }
    pkg.asset_manager = AssetManagerBridge::Instance().CreateManager(pkg.archive);

    packages_[manifest->package_name] = pkg;
    active_package_name_ = manifest->package_name;

    return pkg;
}

const PackageInfo *PackageManager::GetPackage(std::string_view package_name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = packages_.find(std::string(package_name));
    if (it != packages_.end()) {
        return &it->second;
    }
    return nullptr;
}

const PackageInfo *PackageManager::GetActivePackage() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_package_name_.empty()) {
        return nullptr;
    }
    auto it = packages_.find(active_package_name_);
    if (it != packages_.end()) {
        return &it->second;
    }
    return nullptr;
}

void PackageManager::SetActivePackage(std::string_view package_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_package_name_ = std::string(package_name);
}

std::filesystem::path PackageManager::GetPrimaryLibraryPath(const PackageInfo &pkg) const
{
    if (!pkg.manifest.native_library_name.empty()) {
        std::string lib_filename = "lib" + pkg.manifest.native_library_name + ".so";
        std::filesystem::path candidate = pkg.lib_dir / lib_filename;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        std::string test_lib_filename = "libtest_" + pkg.manifest.native_library_name + ".so";
        candidate = pkg.lib_dir / test_lib_filename;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    for (const auto &lib_name : pkg.native_libraries) {
        if (lib_name.find("main") != std::string::npos ||
            lib_name.find("engine") != std::string::npos ||
            lib_name.find("activity") != std::string::npos) {
            return pkg.lib_dir / lib_name;
        }
    }

    if (!pkg.native_libraries.empty()) {
        return pkg.lib_dir / pkg.native_libraries.front();
    }

    return pkg.lib_dir / "libmain.so";
}

std::filesystem::path PackageManager::GetObbDir(std::string_view package_name) const
{
    return sandbox_base_ / "sdcard" / "Android" / "obb" / package_name;
}

std::filesystem::path PackageManager::GetObbMountBase() const
{
    return sandbox_base_ / "data" / "obb_mounts";
}

} // namespace portage::package
