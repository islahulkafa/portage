#pragma once

#include "zip_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace portage::package {

enum AssetMode {
    AASSET_MODE_UNKNOWN = 0,
    AASSET_MODE_RANDOM = 1,
    AASSET_MODE_STREAMING = 2,
    AASSET_MODE_BUFFER = 3
};

struct AAsset;
struct AAssetManager;
struct AAssetDir;

using PFN_AAssetManager_open = AAsset *(__attribute__((sysv_abi)) *)(AAssetManager * mgr,
                                                                     const char *filename,
                                                                     int mode);
using PFN_AAsset_read = int(__attribute__((sysv_abi)) *)(AAsset *asset, void *buf, size_t count);
using PFN_AAsset_seek = int64_t(__attribute__((sysv_abi)) *)(AAsset *asset, int64_t offset,
                                                             int whence);
using PFN_AAsset_getLength = int64_t(__attribute__((sysv_abi)) *)(AAsset *asset);
using PFN_AAsset_getBuffer = const void *(__attribute__((sysv_abi)) *)(AAsset * asset);
using PFN_AAsset_close = void(__attribute__((sysv_abi)) *)(AAsset *asset);

struct AAsset {
    void *internal_ptr{nullptr};
    PFN_AAsset_read readFunc{nullptr};
    PFN_AAsset_seek seekFunc{nullptr};
    PFN_AAsset_getLength getLengthFunc{nullptr};
    PFN_AAsset_getBuffer getBufferFunc{nullptr};
    PFN_AAsset_close closeFunc{nullptr};

    std::string filename{};
    std::vector<uint8_t> buffer{};
    const uint8_t *raw_buffer{nullptr};
    size_t length{0};
    size_t offset{0};
    bool is_allocated{false};
};

struct AAssetDir {
    std::vector<std::string> file_names{};
    size_t current_index{0};
};

struct AAssetManager {
    void *internal_ptr{nullptr};
    PFN_AAssetManager_open openFunc{nullptr};
    PFN_AAsset_read readFunc{nullptr};
    PFN_AAsset_seek seekFunc{nullptr};
    PFN_AAsset_getLength getLengthFunc{nullptr};
    PFN_AAsset_getBuffer getBufferFunc{nullptr};
    PFN_AAsset_close closeFunc{nullptr};

    std::shared_ptr<ZipArchive> archive{nullptr};
};

class AssetManagerBridge {
  public:
    static AssetManagerBridge &Instance() noexcept;

    AAssetManager *CreateManager(std::shared_ptr<ZipArchive> archive);
    void DestroyManager(AAssetManager *mgr);

    // C-compatible SysV ABI entry points
    __attribute__((sysv_abi)) static AAsset *
    Sysv_AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
    __attribute__((sysv_abi)) static AAssetDir *Sysv_AAssetManager_openDir(AAssetManager *mgr,
                                                                           const char *dirName);

    __attribute__((sysv_abi)) static int Sysv_AAsset_read(AAsset *asset, void *buf, size_t count);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_seek(AAsset *asset, int64_t offset,
                                                              int whence);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_seek64(AAsset *asset, int64_t offset,
                                                                int whence);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_getLength(AAsset *asset);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_getLength64(AAsset *asset);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_getRemainingLength(AAsset *asset);
    __attribute__((sysv_abi)) static int64_t Sysv_AAsset_getRemainingLength64(AAsset *asset);
    __attribute__((sysv_abi)) static const void *Sysv_AAsset_getBuffer(AAsset *asset);
    __attribute__((sysv_abi)) static int Sysv_AAsset_isAllocated(AAsset *asset);
    __attribute__((sysv_abi)) static void Sysv_AAsset_close(AAsset *asset);

    __attribute__((sysv_abi)) static const char *
    Sysv_AAssetDir_getNextFileName(AAssetDir *assetDir);
    __attribute__((sysv_abi)) static void Sysv_AAssetDir_rewind(AAssetDir *assetDir);
    __attribute__((sysv_abi)) static void Sysv_AAssetDir_close(AAssetDir *assetDir);

  private:
    AssetManagerBridge() = default;
    ~AssetManagerBridge() = default;

    std::vector<std::unique_ptr<AAssetManager>> managers_{};
};

} // namespace portage::package
