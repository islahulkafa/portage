#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

struct ANativeWindow {
    int32_t width{1280};
    int32_t height{720};
    int32_t format{1}; // WINDOW_FORMAT_RGBA_8888
    int32_t ref_count{1};
    void *native_handle{nullptr};
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
}
