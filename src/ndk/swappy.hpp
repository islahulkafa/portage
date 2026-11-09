#pragma once

#include "ndk/libandroid_thunks.hpp"

#include <cstdint>
#include <memory>

struct SwappyStats {
    uint64_t total_frames_presented{0};
    uint64_t frames_dropped{0};
    uint32_t target_fps{60};
    uint32_t swap_interval{1};
    int64_t refresh_period_nanos{16'666'667LL};
    int64_t last_queue_duration_nanos{0};
    bool auto_swap_interval_enabled{true};
};

namespace portage::ndk {

class SwappyManager {
  public:
    static SwappyManager &Instance() noexcept;

    SwappyManager(const SwappyManager &) = delete;
    SwappyManager &operator=(const SwappyManager &) = delete;

    uint64_t InitVk(void *physicalDevice, void *device, const void *allocationCallbacks);
    void SetWindowVk(void *device, void *queue, void *swapchain, ANativeWindow *window);
    int32_t QueuePresentVk(void *queue, const void *presentInfo);
    void DestroySwapchainVk(void *device, void *swapchain);
    void DestroyDeviceVk(void *device);

    void InitGL(void *env, void *activity);
    void SetWindowGL(ANativeWindow *window);
    bool SwapGL(void *display, void *surface);
    void DestroyGL();

    void SetTargetFrameRateHz(uint32_t targetFps);
    uint32_t GetTargetFrameRateHz() const noexcept;
    int64_t GetSwapIntervalNS() const noexcept;
    void SetAutoSwapInterval(bool enabled);
    bool IsAutoSwapIntervalEnabled() const noexcept;
    bool IsGLEnabled() const noexcept;

    void SetRefreshPeriodOverride(int64_t period_nanos);
    void ClearRefreshPeriodOverride();
    void ResetStats();
    [[nodiscard]] SwappyStats GetStats() const;

  private:
    SwappyManager();
    ~SwappyManager();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace portage::ndk

extern "C" {

uint64_t SwappyVk_initAndGetRefreshCycleDuration(void *physicalDevice, void *device,
                                                 const void *pAllocator) __attribute__((sysv_abi));
void SwappyVk_setWindow(void *device, void *queue, void *swapchain, ANativeWindow *window)
    __attribute__((sysv_abi));
int32_t SwappyVk_queuePresent(void *queue, const void *pPresentInfo) __attribute__((sysv_abi));
void SwappyVk_destroySwapchain(void *device, void *swapchain) __attribute__((sysv_abi));
void SwappyVk_destroyDevice(void *device) __attribute__((sysv_abi));
void SwappyVk_setTargetFrameRateHz(uint32_t targetFps) __attribute__((sysv_abi));
int64_t SwappyVk_getSwapIntervalNS() __attribute__((sysv_abi));
void SwappyVk_setAutoSwapInterval(bool enabled) __attribute__((sysv_abi));

void SwappyGL_init(void *env, void *activity) __attribute__((sysv_abi));
void SwappyGL_setWindow(ANativeWindow *window) __attribute__((sysv_abi));
bool SwappyGL_swap(void *display, void *surface) __attribute__((sysv_abi));
void SwappyGL_destroy() __attribute__((sysv_abi));
void SwappyGL_setTargetFrameRateHz(uint32_t targetFps) __attribute__((sysv_abi));
int64_t SwappyGL_getSwapIntervalNS() __attribute__((sysv_abi));
void SwappyGL_setAutoSwapInterval(bool enabled) __attribute__((sysv_abi));
bool SwappyGL_isEnabled() __attribute__((sysv_abi));

}
