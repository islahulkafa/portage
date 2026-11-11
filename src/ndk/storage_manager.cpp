#include "storage_manager.hpp"

#include "fs/vfs.hpp"
#include "package/package_manager.hpp"
#include "package/zip_archive.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>

namespace portage::ndk {

StorageManagerImpl::~StorageManagerImpl()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto *vfs = syscall::SyscallDispatcher::GetVfs();
    for (const auto &[_, info] : mounted_obbs_) {
        if (vfs) {
            vfs->RemoveMount(info.guest_mount_path);
        }
        std::error_code ec;
        std::filesystem::remove_all(info.host_mount_path, ec);
    }
    mounted_obbs_.clear();
}

std::string StorageManagerImpl::CanonicalizePath(const char *path)
{
    if (!path) {
        return "";
    }
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');

    std::string collapsed;
    collapsed.reserve(result.size());
    bool last_was_slash = false;
    for (char c : result) {
        if (c == '/') {
            if (!last_was_slash) {
                collapsed.push_back(c);
                last_was_slash = true;
            }
        } else {
            collapsed.push_back(c);
            last_was_slash = false;
        }
    }
    return collapsed;
}

std::string StorageManagerImpl::ComputeMountHash(const std::string &path)
{
    size_t h = std::hash<std::string>{}(path);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << h;
    return oss.str();
}

std::optional<AndroidObbFooter>
StorageManagerImpl::ParseObbFooter(const std::filesystem::path &host_path)
{
    std::ifstream file(host_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }
    auto file_size = file.tellg();
    if (file_size < 33) {
        return std::nullopt;
    }

    file.seekg(file_size - static_cast<std::streamoff>(8));
    uint32_t footer_size = 0;
    uint32_t marker = 0;
    file.read(reinterpret_cast<char *>(&footer_size), 4);
    file.read(reinterpret_cast<char *>(&marker), 4);

    constexpr uint32_t SIGNATURE_AOSP = 0x01059983;
    constexpr uint32_t SIGNATURE_ALT = 0x01050a4c;
    if (marker != SIGNATURE_AOSP && marker != SIGNATURE_ALT) {
        return std::nullopt;
    }

    if (footer_size < 33 || static_cast<std::streamoff>(footer_size) > file_size) {
        return std::nullopt;
    }

    file.seekg(file_size - static_cast<std::streamoff>(footer_size));
    AndroidObbFooter footer;
    file.read(reinterpret_cast<char *>(&footer.sig_version), 4);
    file.read(reinterpret_cast<char *>(&footer.package_version), 4);
    file.read(reinterpret_cast<char *>(&footer.flags), 4);
    file.read(reinterpret_cast<char *>(&footer.salt), 8);
    uint32_t pkg_name_len = 0;
    file.read(reinterpret_cast<char *>(&pkg_name_len), 4);

    if (32 + pkg_name_len != footer_size) {
        return std::nullopt;
    }

    footer.package_name.resize(pkg_name_len);
    file.read(footer.package_name.data(), pkg_name_len);
    footer.valid = true;
    return footer;
}

void StorageManagerImpl::Reset()
{
    auto *vfs = syscall::SyscallDispatcher::GetVfs();
    if (vfs) {
        vfs->ClearMounts();
    }
}

void StorageManagerImpl::MountObb(const char *filename, const char *,
                                  AStorageManager_obbCallbackFunc cb, void *data)
{
    if (!filename || filename[0] == '\0') {
        if (cb) {
            std::thread([cb, data]() { cb("", AOBB_STATE_ERROR_INTERNAL, data); }).detach();
        }
        return;
    }

    std::string canonical = CanonicalizePath(filename);
    auto *vfs = syscall::SyscallDispatcher::GetVfs();
    if (!vfs) {
        if (cb) {
            std::thread([cb, fn = canonical, data]() {
                cb(fn.c_str(), AOBB_STATE_ERROR_INTERNAL, data);
            }).detach();
        }
        return;
    }

    auto resolved = vfs->Resolve(canonical);
    std::error_code ec;
    if (!std::filesystem::exists(resolved.host_path, ec) ||
        !std::filesystem::is_regular_file(resolved.host_path, ec)) {
        if (cb) {
            std::thread([cb, fn = canonical, data]() {
                cb(fn.c_str(), AOBB_STATE_ERROR_COULD_NOT_MOUNT, data);
            }).detach();
        }
        return;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (mounted_obbs_.find(canonical) != mounted_obbs_.end()) {
        lock.unlock();
        if (cb) {
            std::thread([cb, fn = canonical, data]() {
                cb(fn.c_str(), AOBB_STATE_ERROR_ALREADY_MOUNTED, data);
            }).detach();
        }
        return;
    }

    auto zip = package::ZipArchive::Open(resolved.host_path);
    if (!zip) {
        lock.unlock();
        if (cb) {
            std::thread([cb, fn = canonical, data]() {
                cb(fn.c_str(), AOBB_STATE_ERROR_COULD_NOT_MOUNT, data);
            }).detach();
        }
        return;
    }

    std::string hash = ComputeMountHash(canonical);
    std::string guest_mount = "/storage/obb/" + hash;

    std::filesystem::path host_mount_base = package::PackageManager::Instance().GetObbMountBase();
    if (host_mount_base.empty()) {
        host_mount_base = std::filesystem::temp_directory_path() / "portage" / "obb_mounts";
    }
    std::filesystem::path host_dest = host_mount_base / hash;
    std::filesystem::create_directories(host_dest, ec);

    auto entries = zip->GetAllEntries();
    for (const auto &entry : entries) {
        if (entry.name.ends_with('/') || entry.name.ends_with('\\')) {
            std::filesystem::create_directories(host_dest / entry.name, ec);
            continue;
        }
        std::filesystem::path out_file = host_dest / entry.name;
        std::filesystem::create_directories(out_file.parent_path(), ec);
        zip->ExtractEntry(entry.name, out_file);
    }

    auto footer = ParseObbFooter(resolved.host_path);

    vfs->AddMount(guest_mount, host_dest);

    MountedObbInfo info;
    info.canonical_filename = canonical;
    info.guest_mount_path = guest_mount;
    info.host_mount_path = host_dest;
    info.footer = footer.value_or(AndroidObbFooter{});
    mounted_obbs_[canonical] = std::move(info);

    lock.unlock();

    if (cb) {
        std::thread([cb, fn = canonical, data]() {
            cb(fn.c_str(), AOBB_STATE_MOUNTED, data);
        }).detach();
    }
}

void StorageManagerImpl::UnmountObb(const char *filename, int, AStorageManager_obbCallbackFunc cb,
                                    void *data)
{
    if (!filename || filename[0] == '\0') {
        if (cb) {
            std::thread([cb, data]() { cb("", AOBB_STATE_ERROR_NOT_MOUNTED, data); }).detach();
        }
        return;
    }

    std::string canonical = CanonicalizePath(filename);
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = mounted_obbs_.find(canonical);
    if (it == mounted_obbs_.end()) {
        lock.unlock();
        if (cb) {
            std::thread([cb, fn = canonical, data]() {
                cb(fn.c_str(), AOBB_STATE_ERROR_NOT_MOUNTED, data);
            }).detach();
        }
        return;
    }

    std::string guest_mount = it->second.guest_mount_path;
    std::filesystem::path host_mount = it->second.host_mount_path;
    mounted_obbs_.erase(it);
    lock.unlock();

    auto *vfs = syscall::SyscallDispatcher::GetVfs();
    if (vfs) {
        vfs->RemoveMount(guest_mount);
    }

    std::error_code ec;
    std::filesystem::remove_all(host_mount, ec);

    if (cb) {
        std::thread([cb, fn = canonical, data]() {
            cb(fn.c_str(), AOBB_STATE_UNMOUNTED, data);
        }).detach();
    }
}

bool StorageManagerImpl::IsObbMounted(const char *filename) const
{
    if (!filename) {
        return false;
    }
    std::string canonical = CanonicalizePath(filename);
    std::lock_guard<std::mutex> lock(mutex_);
    return mounted_obbs_.find(canonical) != mounted_obbs_.end();
}

const char *StorageManagerImpl::GetMountedObbPath(const char *filename) const
{
    if (!filename) {
        return nullptr;
    }
    std::string canonical = CanonicalizePath(filename);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mounted_obbs_.find(canonical);
    if (it != mounted_obbs_.end()) {
        last_retrieved_path_ = it->second.guest_mount_path;
        return last_retrieved_path_.c_str();
    }
    return nullptr;
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) AStorageManager *AStorageManager_new() { return new AStorageManager(); }

__attribute__((sysv_abi)) void AStorageManager_delete(AStorageManager *mgr) { delete mgr; }

__attribute__((sysv_abi)) void AStorageManager_mountObb(AStorageManager *mgr, const char *filename,
                                                        const char *key,
                                                        AStorageManager_obbCallbackFunc cb,
                                                        void *data)
{
    if (mgr) {
        mgr->MountObb(filename, key, cb, data);
    }
}

__attribute__((sysv_abi)) void AStorageManager_unmountObb(AStorageManager *mgr,
                                                          const char *filename, int force,
                                                          AStorageManager_obbCallbackFunc cb,
                                                          void *data)
{
    if (mgr) {
        mgr->UnmountObb(filename, force, cb, data);
    }
}

__attribute__((sysv_abi)) int AStorageManager_isObbMounted(AStorageManager *mgr,
                                                           const char *filename)
{
    return (mgr && mgr->IsObbMounted(filename)) ? 1 : 0;
}

__attribute__((sysv_abi)) const char *AStorageManager_getMountedObbPath(AStorageManager *mgr,
                                                                        const char *filename)
{
    return mgr ? mgr->GetMountedObbPath(filename) : nullptr;
}

} // extern "C"
