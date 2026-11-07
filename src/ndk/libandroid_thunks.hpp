#pragma once

#include "ndk/choreographer.hpp"
#include "ndk/sensor.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ARect {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};
};

struct ANativeWindow_Buffer {
    int32_t width{0};
    int32_t height{0};
    int32_t stride{0};
    int32_t format{0};
    void *bits{nullptr};
    uint32_t reserved[6]{0};
};

struct ANativeWindow {
    int32_t width{1280};
    int32_t height{720};
    int32_t format{1}; // WINDOW_FORMAT_RGBA_8888
    int32_t ref_count{1};
    void *native_handle{nullptr};

    std::vector<uint8_t> backbuffer{};
    std::vector<uint8_t> frontbuffer{};
    int32_t locked_width{0};
    int32_t locked_height{0};
    int32_t locked_stride{0};
    int32_t locked_format{0};
    ARect dirty_rect{0, 0, 0, 0};
    bool is_locked{false};
};

namespace portage::ndk {

class NdkSymbolRegistry {
  public:
    static NdkSymbolRegistry &Instance() noexcept;

    void Register(std::string_view name, uintptr_t address);
    [[nodiscard]] uintptr_t Lookup(std::string_view name) const;
    [[nodiscard]] size_t Count() const noexcept;

  private:
    NdkSymbolRegistry();
    void RegisterAll();

    std::unordered_map<std::string, uintptr_t> symbols_{};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window,
                                                                   int32_t width, int32_t height,
                                                                   int32_t format);
__attribute__((sysv_abi)) int32_t ANativeWindow_getWidth(ANativeWindow *window);
__attribute__((sysv_abi)) int32_t ANativeWindow_getHeight(ANativeWindow *window);
__attribute__((sysv_abi)) int32_t ANativeWindow_getFormat(ANativeWindow *window);
__attribute__((sysv_abi)) void ANativeWindow_acquire(ANativeWindow *window);
__attribute__((sysv_abi)) void ANativeWindow_release(ANativeWindow *window);
__attribute__((sysv_abi)) int32_t ANativeWindow_lock(ANativeWindow *window,
                                                     ANativeWindow_Buffer *outBuffer,
                                                     ARect *inOutDirtyBounds);
__attribute__((sysv_abi)) int32_t ANativeWindow_unlockAndPost(ANativeWindow *window);

struct AAsset;
struct AAssetManager;
struct AAssetDir;

__attribute__((sysv_abi)) struct AAssetManager *AAssetManager_fromJava(void *env,
                                                                       void *assetManager);
__attribute__((sysv_abi)) struct AAsset *AAssetManager_open(struct AAssetManager *mgr,
                                                            const char *filename, int mode);
__attribute__((sysv_abi)) struct AAssetDir *AAssetManager_openDir(struct AAssetManager *mgr,
                                                                  const char *dirName);
__attribute__((sysv_abi)) int AAsset_read(struct AAsset *asset, void *buf, size_t count);
__attribute__((sysv_abi)) int64_t AAsset_seek(struct AAsset *asset, int64_t offset, int whence);
__attribute__((sysv_abi)) int64_t AAsset_seek64(struct AAsset *asset, int64_t offset, int whence);
__attribute__((sysv_abi)) int64_t AAsset_getLength(struct AAsset *asset);
__attribute__((sysv_abi)) int64_t AAsset_getLength64(struct AAsset *asset);
__attribute__((sysv_abi)) int64_t AAsset_getRemainingLength(struct AAsset *asset);
__attribute__((sysv_abi)) int64_t AAsset_getRemainingLength64(struct AAsset *asset);
__attribute__((sysv_abi)) const void *AAsset_getBuffer(struct AAsset *asset);
__attribute__((sysv_abi)) int AAsset_isAllocated(struct AAsset *asset);
__attribute__((sysv_abi)) void AAsset_close(struct AAsset *asset);
__attribute__((sysv_abi)) const char *AAssetDir_getNextFileName(struct AAssetDir *assetDir);
__attribute__((sysv_abi)) void AAssetDir_rewind(struct AAssetDir *assetDir);
__attribute__((sysv_abi)) void AAssetDir_close(struct AAssetDir *assetDir);

__attribute__((sysv_abi)) int32_t ASharedMemory_create(const char *name, size_t size);
__attribute__((sysv_abi)) size_t ASharedMemory_getSize(int fd);
__attribute__((sysv_abi)) int32_t ASharedMemory_setProt(int fd, int prot);
__attribute__((sysv_abi)) int32_t ASharedMemory_dup(int fd);
}
