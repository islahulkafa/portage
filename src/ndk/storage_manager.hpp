#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum {
    AOBB_STATE_MOUNTED = 1,
    AOBB_STATE_UNMOUNTED = 2,
    AOBB_STATE_ERROR_INTERNAL = 20,
    AOBB_STATE_ERROR_COULD_NOT_MOUNT = 21,
    AOBB_STATE_ERROR_COULD_NOT_UNMOUNT = 22,
    AOBB_STATE_ERROR_NOT_MOUNTED = 23,
    AOBB_STATE_ERROR_ALREADY_MOUNTED = 24,
    AOBB_STATE_ERROR_PERMISSION_DENIED = 25,
};

struct AStorageManager;

using AStorageManager_obbCallbackFunc =
    void (__attribute__((sysv_abi)) *)(const char *filename, const int32_t state, void *data);

namespace portage::ndk {

struct AndroidObbFooter {
    uint32_t sig_version{0};
    uint32_t package_version{0};
    uint32_t flags{0};
    uint64_t salt{0};
    std::string package_name{};
    bool valid{false};
};

struct MountedObbInfo {
    std::string canonical_filename{};
    std::string guest_mount_path{};
    std::filesystem::path host_mount_path{};
    AndroidObbFooter footer{};
};

class StorageManagerImpl {
  public:
    StorageManagerImpl() = default;
    ~StorageManagerImpl();

    void MountObb(const char *filename, const char *key, AStorageManager_obbCallbackFunc cb,
                  void *data);
    void UnmountObb(const char *filename, int force, AStorageManager_obbCallbackFunc cb,
                    void *data);
    [[nodiscard]] bool IsObbMounted(const char *filename) const;
    [[nodiscard]] const char *GetMountedObbPath(const char *filename) const;

    static std::optional<AndroidObbFooter> ParseObbFooter(const std::filesystem::path &host_path);
    static void Reset();

  private:
    static std::string CanonicalizePath(const char *path);
    static std::string ComputeMountHash(const std::string &path);

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, MountedObbInfo> mounted_obbs_{};
    mutable std::string last_retrieved_path_{};
};

} // namespace portage::ndk

struct AStorageManager : public portage::ndk::StorageManagerImpl {};

extern "C" {
__attribute__((sysv_abi)) AStorageManager *AStorageManager_new();
__attribute__((sysv_abi)) void AStorageManager_delete(AStorageManager *mgr);
__attribute__((sysv_abi)) void AStorageManager_mountObb(AStorageManager *mgr, const char *filename,
                                                      const char *key,
                                                      AStorageManager_obbCallbackFunc cb,
                                                      void *data);
__attribute__((sysv_abi)) void AStorageManager_unmountObb(AStorageManager *mgr, const char *filename,
                                                        int force,
                                                        AStorageManager_obbCallbackFunc cb,
                                                        void *data);
__attribute__((sysv_abi)) int AStorageManager_isObbMounted(AStorageManager *mgr,
                                                          const char *filename);
__attribute__((sysv_abi)) const char *AStorageManager_getMountedObbPath(AStorageManager *mgr,
                                                                      const char *filename);
}
