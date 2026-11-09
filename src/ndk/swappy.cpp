#include "ndk/swappy.hpp"
#include "gles/egl_dispatcher.hpp"
#include "ndk/choreographer.hpp"
#include "vulkan/vulkan_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace portage::ndk {

struct VkSwapchainBinding {
    void *device{nullptr};
    void *queue{nullptr};
    void *swapchain{nullptr};
    ANativeWindow *window{nullptr};
};

struct SwappyManager::Impl {
    mutable std::mutex mutex{};

    std::optional<int64_t> refresh_period_override{};
    uint32_t target_fps{60};
    uint32_t swap_interval{1};
    bool auto_swap_interval{true};

    bool gl_initialized{false};
    ANativeWindow *gl_window{nullptr};

    std::unordered_map<void *, VkSwapchainBinding> vk_bindings{};

    uint64_t total_frames_presented{0};
    uint64_t frames_dropped{0};
    int64_t last_queue_duration_nanos{0};
    std::chrono::steady_clock::time_point last_present_time{};

    int64_t GetEffectiveRefreshPeriodNanos() const
    {
        if (refresh_period_override.has_value()) {
            return *refresh_period_override;
        }
        int64_t p = VsyncPacer::Instance().GetVsyncPeriodNanos();
        return (p > 0) ? p : 16'666'667LL;
    }

    void UpdateSwapIntervalLocked()
    {
        int64_t refresh_ns = GetEffectiveRefreshPeriodNanos();
        if (target_fps > 0 && refresh_ns > 0) {
            int64_t target_frame_duration_ns = 1'000'000'000LL / target_fps;
            swap_interval = std::max<uint32_t>(
                1, static_cast<uint32_t>((target_frame_duration_ns + (refresh_ns / 2)) / refresh_ns));
        } else {
            swap_interval = 1;
        }
    }

    void PacePresentationLocked(ANativeWindow *window)
    {
        auto now = std::chrono::steady_clock::now();
        int64_t target_ns = GetEffectiveRefreshPeriodNanos() * swap_interval;

        if (last_present_time.time_since_epoch().count() != 0) {
            auto elapsed_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_present_time)
                    .count();
            if (elapsed_ns < target_ns) {
                int64_t sleep_ns = target_ns - elapsed_ns;
                if (sleep_ns > 1'000'000LL) {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns - 500'000LL));
                }
                while (std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - last_present_time)
                           .count() < target_ns) {
                }
            } else if (elapsed_ns > (target_ns * 2)) {
                frames_dropped++;
            }
        }

        auto present_done = std::chrono::steady_clock::now();
        auto queue_duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(present_done - now).count();
        last_queue_duration_nanos = queue_duration;
        last_present_time = present_done;
        total_frames_presented++;

        if (window) {
            auto now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 present_done.time_since_epoch())
                                 .count();
            window->last_queue_duration_nanos = queue_duration;
            window->last_queue_time_nanos = now_nanos;
            window->last_latch_time_nanos = now_nanos;
            window->last_present_time_nanos = now_nanos;
            window->frame_index++;
        }
    }
};

SwappyManager &SwappyManager::Instance() noexcept
{
    static SwappyManager instance;
    return instance;
}

SwappyManager::SwappyManager() : impl_(std::make_unique<Impl>())
{
    impl_->UpdateSwapIntervalLocked();
}

SwappyManager::~SwappyManager() = default;

uint64_t SwappyManager::InitVk(void *physicalDevice, void *device, const void *allocationCallbacks)
{
    (void)physicalDevice;
    (void)device;
    (void)allocationCallbacks;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<uint64_t>(impl_->GetEffectiveRefreshPeriodNanos());
}

void SwappyManager::SetWindowVk(void *device, void *queue, void *swapchain, ANativeWindow *window)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->vk_bindings[queue] = VkSwapchainBinding{
        .device = device,
        .queue = queue,
        .swapchain = swapchain,
        .window = window,
    };
    if (swapchain) {
        impl_->vk_bindings[swapchain] = impl_->vk_bindings[queue];
    }
}

int32_t SwappyManager::QueuePresentVk(void *queue, const void *presentInfo)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ANativeWindow *win = nullptr;
    auto it = impl_->vk_bindings.find(queue);
    if (it != impl_->vk_bindings.end()) {
        win = it->second.window;
    } else if (!impl_->vk_bindings.empty()) {
        win = impl_->vk_bindings.begin()->second.window;
    }

    impl_->PacePresentationLocked(win);

    const auto *table = vulkan::VulkanBridge::Instance().GetDispatchTable();
    if (table && table->vkQueuePresentKHR && queue && presentInfo) {
        return static_cast<int32_t>(table->vkQueuePresentKHR(
            reinterpret_cast<VkQueue>(queue),
            reinterpret_cast<const VkPresentInfoKHR *>(presentInfo)));
    }
    return 0;
}

void SwappyManager::DestroySwapchainVk(void *device, void *swapchain)
{
    (void)device;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->vk_bindings.erase(swapchain);
}

void SwappyManager::DestroyDeviceVk(void *device)
{
    (void)device;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->vk_bindings.clear();
}

void SwappyManager::InitGL(void *env, void *activity)
{
    (void)env;
    (void)activity;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->gl_initialized = true;
}

void SwappyManager::SetWindowGL(ANativeWindow *window)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->gl_window = window;
}

bool SwappyManager::SwapGL(void *display, void *surface)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->gl_initialized) {
        return false;
    }
    impl_->PacePresentationLocked(impl_->gl_window);
    if (display && surface) {
        return ::eglSwapBuffers(static_cast<EGLDisplay>(display),
                                static_cast<EGLSurface>(surface)) == EGL_TRUE;
    }
    return true;
}

void SwappyManager::DestroyGL()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->gl_initialized = false;
    impl_->gl_window = nullptr;
}

void SwappyManager::SetTargetFrameRateHz(uint32_t targetFps)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->target_fps = (targetFps > 0) ? targetFps : 60;
    impl_->UpdateSwapIntervalLocked();
}

uint32_t SwappyManager::GetTargetFrameRateHz() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->target_fps;
}

int64_t SwappyManager::GetSwapIntervalNS() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->GetEffectiveRefreshPeriodNanos() * impl_->swap_interval;
}

void SwappyManager::SetAutoSwapInterval(bool enabled)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->auto_swap_interval = enabled;
}

bool SwappyManager::IsAutoSwapIntervalEnabled() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->auto_swap_interval;
}

bool SwappyManager::IsGLEnabled() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->gl_initialized;
}

void SwappyManager::SetRefreshPeriodOverride(int64_t period_nanos)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->refresh_period_override = period_nanos;
    impl_->UpdateSwapIntervalLocked();
}

void SwappyManager::ClearRefreshPeriodOverride()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->refresh_period_override.reset();
    impl_->UpdateSwapIntervalLocked();
}

void SwappyManager::ResetStats()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->total_frames_presented = 0;
    impl_->frames_dropped = 0;
    impl_->last_queue_duration_nanos = 0;
    impl_->last_present_time = {};
}

SwappyStats SwappyManager::GetStats() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return SwappyStats{
        .total_frames_presented = impl_->total_frames_presented,
        .frames_dropped = impl_->frames_dropped,
        .target_fps = impl_->target_fps,
        .swap_interval = impl_->swap_interval,
        .refresh_period_nanos = impl_->GetEffectiveRefreshPeriodNanos(),
        .last_queue_duration_nanos = impl_->last_queue_duration_nanos,
        .auto_swap_interval_enabled = impl_->auto_swap_interval,
    };
}

} // namespace portage::ndk

extern "C" {

uint64_t SwappyVk_initAndGetRefreshCycleDuration(void *physicalDevice, void *device,
                                                 const void *pAllocator)
{
    return portage::ndk::SwappyManager::Instance().InitVk(physicalDevice, device, pAllocator);
}

void SwappyVk_setWindow(void *device, void *queue, void *swapchain, ANativeWindow *window)
{
    portage::ndk::SwappyManager::Instance().SetWindowVk(device, queue, swapchain, window);
}

int32_t SwappyVk_queuePresent(void *queue, const void *pPresentInfo)
{
    return portage::ndk::SwappyManager::Instance().QueuePresentVk(queue, pPresentInfo);
}

void SwappyVk_destroySwapchain(void *device, void *swapchain)
{
    portage::ndk::SwappyManager::Instance().DestroySwapchainVk(device, swapchain);
}

void SwappyVk_destroyDevice(void *device)
{
    portage::ndk::SwappyManager::Instance().DestroyDeviceVk(device);
}

void SwappyVk_setTargetFrameRateHz(uint32_t targetFps)
{
    portage::ndk::SwappyManager::Instance().SetTargetFrameRateHz(targetFps);
}

int64_t SwappyVk_getSwapIntervalNS()
{
    return portage::ndk::SwappyManager::Instance().GetSwapIntervalNS();
}

void SwappyVk_setAutoSwapInterval(bool enabled)
{
    portage::ndk::SwappyManager::Instance().SetAutoSwapInterval(enabled);
}

void SwappyGL_init(void *env, void *activity)
{
    portage::ndk::SwappyManager::Instance().InitGL(env, activity);
}

void SwappyGL_setWindow(ANativeWindow *window)
{
    portage::ndk::SwappyManager::Instance().SetWindowGL(window);
}

bool SwappyGL_swap(void *display, void *surface)
{
    return portage::ndk::SwappyManager::Instance().SwapGL(display, surface);
}

void SwappyGL_destroy()
{
    portage::ndk::SwappyManager::Instance().DestroyGL();
}

void SwappyGL_setTargetFrameRateHz(uint32_t targetFps)
{
    portage::ndk::SwappyManager::Instance().SetTargetFrameRateHz(targetFps);
}

int64_t SwappyGL_getSwapIntervalNS()
{
    return portage::ndk::SwappyManager::Instance().GetSwapIntervalNS();
}

void SwappyGL_setAutoSwapInterval(bool enabled)
{
    portage::ndk::SwappyManager::Instance().SetAutoSwapInterval(enabled);
}

bool SwappyGL_isEnabled()
{
    return portage::ndk::SwappyManager::Instance().IsGLEnabled();
}

}
