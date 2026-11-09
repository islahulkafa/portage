#include <stdbool.h>
#include <stdint.h>

__attribute__((visibility("default"))) uint64_t
SwappyVk_initAndGetRefreshCycleDuration(void *physicalDevice, void *device, const void *pAllocator)
{
    (void)physicalDevice;
    (void)device;
    (void)pAllocator;
    return 16666667ULL;
}

__attribute__((visibility("default"))) void
SwappyVk_setWindow(void *device, void *queue, void *swapchain, void *window)
{
    (void)device;
    (void)queue;
    (void)swapchain;
    (void)window;
}

__attribute__((visibility("default"))) int32_t SwappyVk_queuePresent(void *queue,
                                                                     const void *pPresentInfo)
{
    (void)queue;
    (void)pPresentInfo;
    return 0;
}

__attribute__((visibility("default"))) void SwappyVk_destroySwapchain(void *device, void *swapchain)
{
    (void)device;
    (void)swapchain;
}

__attribute__((visibility("default"))) void SwappyVk_destroyDevice(void *device)
{
    (void)device;
}

__attribute__((visibility("default"))) void SwappyVk_setTargetFrameRateHz(uint32_t targetFps)
{
    (void)targetFps;
}

__attribute__((visibility("default"))) int64_t SwappyVk_getSwapIntervalNS(void)
{
    return 16666667LL;
}

__attribute__((visibility("default"))) void SwappyVk_setAutoSwapInterval(bool enabled)
{
    (void)enabled;
}
