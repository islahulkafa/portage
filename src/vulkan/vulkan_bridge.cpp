#include "vulkan_bridge.hpp"
#include "ndk/hardware_buffer.hpp"
#include "wsi/window_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::vulkan {

namespace {

// Host function pointers resolved from vulkan-1.dll
[[maybe_unused]] PFN_vkGetInstanceProcAddr g_host_vkGetInstanceProcAddr{nullptr};
[[maybe_unused]] PFN_vkGetDeviceProcAddr g_host_vkGetDeviceProcAddr{nullptr};
PFN_vkCreateInstance g_host_vkCreateInstance{nullptr};
PFN_vkDestroyInstance g_host_vkDestroyInstance{nullptr};
PFN_vkEnumeratePhysicalDevices g_host_vkEnumeratePhysicalDevices{nullptr};
PFN_vkGetPhysicalDeviceProperties g_host_vkGetPhysicalDeviceProperties{nullptr};
PFN_vkGetPhysicalDeviceFeatures g_host_vkGetPhysicalDeviceFeatures{nullptr};
PFN_vkGetPhysicalDeviceQueueFamilyProperties g_host_vkGetPhysicalDeviceQueueFamilyProperties{
    nullptr};
PFN_vkGetPhysicalDeviceMemoryProperties g_host_vkGetPhysicalDeviceMemoryProperties{nullptr};
PFN_vkCreateDevice g_host_vkCreateDevice{nullptr};
PFN_vkDestroyDevice g_host_vkDestroyDevice{nullptr};
PFN_vkGetDeviceQueue g_host_vkGetDeviceQueue{nullptr};
PFN_vkCreateBuffer g_host_vkCreateBuffer{nullptr};
PFN_vkDestroyBuffer g_host_vkDestroyBuffer{nullptr};
PFN_vkGetBufferMemoryRequirements g_host_vkGetBufferMemoryRequirements{nullptr};
PFN_vkAllocateMemory g_host_vkAllocateMemory{nullptr};
PFN_vkFreeMemory g_host_vkFreeMemory{nullptr};
PFN_vkBindBufferMemory g_host_vkBindBufferMemory{nullptr};
PFN_vkMapMemory g_host_vkMapMemory{nullptr};
PFN_vkUnmapMemory g_host_vkUnmapMemory{nullptr};
PFN_vkCreateCommandPool g_host_vkCreateCommandPool{nullptr};
PFN_vkDestroyCommandPool g_host_vkDestroyCommandPool{nullptr};
PFN_vkAllocateCommandBuffers g_host_vkAllocateCommandBuffers{nullptr};
PFN_vkFreeCommandBuffers g_host_vkFreeCommandBuffers{nullptr};
PFN_vkBeginCommandBuffer g_host_vkBeginCommandBuffer{nullptr};
PFN_vkEndCommandBuffer g_host_vkEndCommandBuffer{nullptr};
PFN_vkQueueSubmit g_host_vkQueueSubmit{nullptr};
PFN_vkQueueWaitIdle g_host_vkQueueWaitIdle{nullptr};
PFN_vkDeviceWaitIdle g_host_vkDeviceWaitIdle{nullptr};
PFN_vkEnumerateInstanceExtensionProperties g_host_vkEnumerateInstanceExtensionProperties{nullptr};
PFN_vkEnumerateDeviceExtensionProperties g_host_vkEnumerateDeviceExtensionProperties{nullptr};

// WSI function pointers
PFN_vkCreateWin32SurfaceKHR g_host_vkCreateWin32SurfaceKHR{nullptr};
PFN_vkDestroySurfaceKHR g_host_vkDestroySurfaceKHR{nullptr};
PFN_vkGetPhysicalDeviceSurfaceSupportKHR g_host_vkGetPhysicalDeviceSurfaceSupportKHR{nullptr};
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR g_host_vkGetPhysicalDeviceSurfaceCapabilitiesKHR{
    nullptr};
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR g_host_vkGetPhysicalDeviceSurfaceFormatsKHR{nullptr};
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR g_host_vkGetPhysicalDeviceSurfacePresentModesKHR{
    nullptr};
PFN_vkCreateSwapchainKHR g_host_vkCreateSwapchainKHR{nullptr};
PFN_vkDestroySwapchainKHR g_host_vkDestroySwapchainKHR{nullptr};
PFN_vkGetSwapchainImagesKHR g_host_vkGetSwapchainImagesKHR{nullptr};
PFN_vkAcquireNextImageKHR g_host_vkAcquireNextImageKHR{nullptr};
PFN_vkQueuePresentKHR g_host_vkQueuePresentKHR{nullptr};

struct VirtualSwapchain {
    VkDevice device{VK_NULL_HANDLE};
    std::vector<VkImage> images{};
    uint32_t current_index{0};
};

std::mutex g_virtual_wsi_mutex{};
uint64_t g_next_surface_id = 0x50000001;
uint64_t g_next_swapchain_id = 0x60000001;
std::unordered_map<VkSwapchainKHR, VirtualSwapchain> g_virtual_swapchains{};

bool IsVirtualSurface(VkSurfaceKHR s)
{
    uintptr_t val = reinterpret_cast<uintptr_t>(s);
    return val >= 0x50000000 && val < 0x60000000;
}

bool IsVirtualSwapchain(VkSwapchainKHR s)
{
    uintptr_t val = reinterpret_cast<uintptr_t>(s);
    return val >= 0x60000000 && val < 0x70000000;
}

// Forward declarations
__attribute__((sysv_abi)) PFN_vkVoidFunction Sysv_vkGetInstanceProcAddr(VkInstance instance,
                                                                        const char *pName);
__attribute__((sysv_abi)) PFN_vkVoidFunction Sysv_vkGetDeviceProcAddr(VkDevice device,
                                                                      const char *pName);

__attribute__((sysv_abi)) VkResult Sysv_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                                         const VkAllocationCallbacks *pAllocator,
                                                         VkInstance *pInstance)
{
    if (!g_host_vkCreateInstance)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::vector<const char *> enabled_exts;
    if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char *ext = pCreateInfo->ppEnabledExtensionNames[i];
            if (!ext)
                continue;
            if (std::strcmp(ext, "VK_KHR_android_surface") == 0) {
                enabled_exts.push_back("VK_KHR_win32_surface");
            } else {
                enabled_exts.push_back(ext);
            }
        }
    }

    auto has_ext = [&](const char *name) {
        return std::ranges::any_of(enabled_exts,
                                   [&](const char *e) { return std::strcmp(e, name) == 0; });
    };

    if (!has_ext("VK_KHR_surface")) {
        enabled_exts.push_back("VK_KHR_surface");
    }
    if (!has_ext("VK_KHR_win32_surface")) {
        enabled_exts.push_back("VK_KHR_win32_surface");
    }

    VkInstanceCreateInfo modified_info = *pCreateInfo;
    modified_info.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
    modified_info.ppEnabledExtensionNames = enabled_exts.data();

    VkResult res = g_host_vkCreateInstance(&modified_info, pAllocator, pInstance);
    if (res != VK_SUCCESS) {
        res = g_host_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
    }
    return res;
}

__attribute__((sysv_abi)) void Sysv_vkDestroyInstance(VkInstance instance,
                                                      const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyInstance)
        g_host_vkDestroyInstance(instance, pAllocator);
}

__attribute__((sysv_abi)) VkResult Sysv_vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices)
{
    if (!g_host_vkEnumeratePhysicalDevices)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                   VkPhysicalDeviceProperties *pProperties)
{
    if (g_host_vkGetPhysicalDeviceProperties)
        g_host_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
}

__attribute__((sysv_abi)) void Sysv_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                                                VkPhysicalDeviceFeatures *pFeatures)
{
    if (g_host_vkGetPhysicalDeviceFeatures)
        g_host_vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                              uint32_t *pQueueFamilyPropertyCount,
                                              VkQueueFamilyProperties *pQueueFamilyProperties)
{
    if (g_host_vkGetPhysicalDeviceQueueFamilyProperties)
        g_host_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount,
                                                        pQueueFamilyProperties);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                         VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    if (g_host_vkGetPhysicalDeviceMemoryProperties)
        g_host_vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

__attribute__((sysv_abi)) VkResult Sysv_vkCreateDevice(VkPhysicalDevice physicalDevice,
                                                       const VkDeviceCreateInfo *pCreateInfo,
                                                       const VkAllocationCallbacks *pAllocator,
                                                       VkDevice *pDevice)
{
    if (!g_host_vkCreateDevice)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::vector<const char *> dev_exts;
    if (pCreateInfo && pCreateInfo->ppEnabledExtensionNames) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char *ext = pCreateInfo->ppEnabledExtensionNames[i];
            if (ext) {
                dev_exts.push_back(ext);
            }
        }
    }
    bool has_swapchain = std::ranges::any_of(
        dev_exts, [](const char *e) { return std::strcmp(e, "VK_KHR_swapchain") == 0; });
    if (!has_swapchain) {
        dev_exts.push_back("VK_KHR_swapchain");
    }

    VkDeviceCreateInfo modified_info = *pCreateInfo;
    modified_info.enabledExtensionCount = static_cast<uint32_t>(dev_exts.size());
    modified_info.ppEnabledExtensionNames = dev_exts.data();

    VkResult res = g_host_vkCreateDevice(physicalDevice, &modified_info, pAllocator, pDevice);
    if (res != VK_SUCCESS) {
        res = g_host_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    }
    return res;
}

__attribute__((sysv_abi)) void Sysv_vkDestroyDevice(VkDevice device,
                                                    const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyDevice)
        g_host_vkDestroyDevice(device, pAllocator);
}

__attribute__((sysv_abi)) void Sysv_vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                                                     uint32_t queueIndex, VkQueue *pQueue)
{
    if (g_host_vkGetDeviceQueue)
        g_host_vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
}

__attribute__((sysv_abi)) VkResult Sysv_vkCreateBuffer(VkDevice device,
                                                       const VkBufferCreateInfo *pCreateInfo,
                                                       const VkAllocationCallbacks *pAllocator,
                                                       VkBuffer *pBuffer)
{
    if (!g_host_vkCreateBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                                    const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyBuffer)
        g_host_vkDestroyBuffer(device, buffer, pAllocator);
}

__attribute__((sysv_abi)) void
Sysv_vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                                   VkMemoryRequirements *pMemoryRequirements)
{
    if (g_host_vkGetBufferMemoryRequirements)
        g_host_vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
}

__attribute__((sysv_abi)) VkResult Sysv_vkAllocateMemory(VkDevice device,
                                                         const VkMemoryAllocateInfo *pAllocateInfo,
                                                         const VkAllocationCallbacks *pAllocator,
                                                         VkDeviceMemory *pMemory)
{
    if (!g_host_vkAllocateMemory)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

__attribute__((sysv_abi)) void Sysv_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                                 const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkFreeMemory)
        g_host_vkFreeMemory(device, memory, pAllocator);
}

__attribute__((sysv_abi)) VkResult Sysv_vkBindBufferMemory(VkDevice device, VkBuffer buffer,
                                                           VkDeviceMemory memory,
                                                           VkDeviceSize memoryOffset)
{
    if (!g_host_vkBindBufferMemory)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkBindBufferMemory(device, buffer, memory, memoryOffset);
}

__attribute__((sysv_abi)) VkResult Sysv_vkMapMemory(VkDevice device, VkDeviceMemory memory,
                                                    VkDeviceSize offset, VkDeviceSize size,
                                                    VkMemoryMapFlags flags, void **ppData)
{
    if (!g_host_vkMapMemory)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkMapMemory(device, memory, offset, size, flags, ppData);
}

__attribute__((sysv_abi)) void Sysv_vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    if (g_host_vkUnmapMemory)
        g_host_vkUnmapMemory(device, memory);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool)
{
    if (!g_host_vkCreateCommandPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                                                         const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyCommandPool)
        g_host_vkDestroyCommandPool(device, commandPool, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                              VkCommandBuffer *pCommandBuffers)
{
    if (!g_host_vkAllocateCommandBuffers)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}

__attribute__((sysv_abi)) void Sysv_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                                         uint32_t commandBufferCount,
                                                         const VkCommandBuffer *pCommandBuffers)
{
    if (g_host_vkFreeCommandBuffers)
        g_host_vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
    if (!g_host_vkBeginCommandBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

__attribute__((sysv_abi)) VkResult Sysv_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    if (!g_host_vkEndCommandBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkEndCommandBuffer(commandBuffer);
}

__attribute__((sysv_abi)) VkResult Sysv_vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                                      const VkSubmitInfo *pSubmits, VkFence fence)
{
    if (!g_host_vkQueueSubmit)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkQueueSubmit(queue, submitCount, pSubmits, fence);
}

__attribute__((sysv_abi)) VkResult Sysv_vkQueueWaitIdle(VkQueue queue)
{
    if (!g_host_vkQueueWaitIdle)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkQueueWaitIdle(queue);
}

__attribute__((sysv_abi)) VkResult Sysv_vkDeviceWaitIdle(VkDevice device)
{
    if (!g_host_vkDeviceWaitIdle)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkDeviceWaitIdle(device);
}

__attribute__((sysv_abi)) VkResult Sysv_vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    if (!g_host_vkEnumerateInstanceExtensionProperties)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res =
        g_host_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
    if (pProperties && pPropertyCount && *pPropertyCount > 0) {
        // Ensure VK_KHR_android_surface is exposed in the returned list
        bool found = false;
        for (uint32_t i = 0; i < *pPropertyCount; ++i) {
            if (std::strcmp(pProperties[i].extensionName, "VK_KHR_android_surface") == 0) {
                found = true;
                break;
            }
        }
        if (!found && *pPropertyCount < 256) {
            std::strncpy(pProperties[*pPropertyCount - 1].extensionName, "VK_KHR_android_surface",
                         VK_MAX_EXTENSION_NAME_SIZE);
            pProperties[*pPropertyCount - 1].specVersion = 6;
        }
    }
    return res;
}

__attribute__((sysv_abi)) VkResult Sysv_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
    if (!g_host_vkEnumerateDeviceExtensionProperties)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount,
                                                       pProperties);
}

// WSI Implementation
__attribute__((sysv_abi)) VkResult Sysv_vkCreateAndroidSurfaceKHR(
    VkInstance instance, [[maybe_unused]] const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    if (!pSurface) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    bool is_headless = wsi::WindowManager::Instance().IsHeadless();
    if (!is_headless && g_host_vkCreateWin32SurfaceKHR) {
        wsi::WindowManager::Instance().EnsureWindow();
        HWND hwnd = wsi::WindowManager::Instance().GetWindowHandle();
        HINSTANCE hinstance = wsi::WindowManager::Instance().GetInstanceHandle();
        if (hwnd) {
            VkWin32SurfaceCreateInfoKHR win32_info{};
            win32_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            win32_info.hinstance = hinstance;
            win32_info.hwnd = hwnd;
            VkResult res =
                g_host_vkCreateWin32SurfaceKHR(instance, &win32_info, pAllocator, pSurface);
            if (res == VK_SUCCESS) {
                return VK_SUCCESS;
            }
        }
    }

    // Virtual surface fallback for headless / display-less CI
    std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
    *pSurface = reinterpret_cast<VkSurfaceKHR>(g_next_surface_id++);
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    if (g_host_vkCreateWin32SurfaceKHR) {
        return g_host_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    }
    std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
    *pSurface = reinterpret_cast<VkSurfaceKHR>(g_next_surface_id++);
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) void Sysv_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                                                        const VkAllocationCallbacks *pAllocator)
{
    if (IsVirtualSurface(surface)) {
        return;
    }
    if (g_host_vkDestroySurfaceKHR) {
        g_host_vkDestroySurfaceKHR(instance, surface, pAllocator);
    }
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
    VkBool32 *pSupported)
{
    if (!pSupported)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSurface(surface) || !g_host_vkGetPhysicalDeviceSurfaceSupportKHR) {
        *pSupported = VK_TRUE;
        return VK_SUCCESS;
    }
    return g_host_vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface,
                                                       pSupported);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    if (!pSurfaceCapabilities)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSurface(surface) || !g_host_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
        uint32_t w = wsi::WindowManager::Instance().GetWidth();
        uint32_t h = wsi::WindowManager::Instance().GetHeight();
        pSurfaceCapabilities->minImageCount = 2;
        pSurfaceCapabilities->maxImageCount = 8;
        pSurfaceCapabilities->currentExtent.width = w;
        pSurfaceCapabilities->currentExtent.height = h;
        pSurfaceCapabilities->minImageExtent.width = w;
        pSurfaceCapabilities->minImageExtent.height = h;
        pSurfaceCapabilities->maxImageExtent.width = w;
        pSurfaceCapabilities->maxImageExtent.height = h;
        pSurfaceCapabilities->maxImageArrayLayers = 1;
        pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return VK_SUCCESS;
    }
    return g_host_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                            pSurfaceCapabilities);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats)
{
    if (!pSurfaceFormatCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSurface(surface) || !g_host_vkGetPhysicalDeviceSurfaceFormatsKHR) {
        if (!pSurfaceFormats) {
            *pSurfaceFormatCount = 2;
            return VK_SUCCESS;
        }
        uint32_t count = std::min(*pSurfaceFormatCount, 2u);
        if (count >= 1) {
            pSurfaceFormats[0] = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }
        if (count >= 2) {
            pSurfaceFormats[1] = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        }
        *pSurfaceFormatCount = count;
        return VK_SUCCESS;
    }
    return g_host_vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount,
                                                       pSurfaceFormats);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes)
{
    if (!pPresentModeCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSurface(surface) || !g_host_vkGetPhysicalDeviceSurfacePresentModesKHR) {
        if (!pPresentModes) {
            *pPresentModeCount = 2;
            return VK_SUCCESS;
        }
        uint32_t count = std::min(*pPresentModeCount, 2u);
        if (count >= 1)
            pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
        if (count >= 2)
            pPresentModes[1] = VK_PRESENT_MODE_MAILBOX_KHR;
        *pPresentModeCount = count;
        return VK_SUCCESS;
    }
    return g_host_vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                            pPresentModeCount, pPresentModes);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    if (!pSwapchain)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (!IsVirtualSurface(pCreateInfo->surface) && g_host_vkCreateSwapchainKHR) {
        *pSwapchain = VK_NULL_HANDLE;
        VkResult res = g_host_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        if (res == VK_SUCCESS && *pSwapchain != VK_NULL_HANDLE) {
            return VK_SUCCESS;
        }
    }

    // Virtual swapchain for headless presentation
    std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
    auto swapchain_id = reinterpret_cast<VkSwapchainKHR>(g_next_swapchain_id++);
    VirtualSwapchain vswap;
    vswap.device = device;
    vswap.current_index = 0;

    uint32_t img_count = std::max(pCreateInfo->minImageCount, 2u);
    for (uint32_t i = 0; i < img_count; ++i) {
        vswap.images.push_back(reinterpret_cast<VkImage>(0x70000001 + i));
    }

    g_virtual_swapchains[swapchain_id] = std::move(vswap);
    *pSwapchain = swapchain_id;
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) void Sysv_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                          const VkAllocationCallbacks *pAllocator)
{
    if (IsVirtualSwapchain(swapchain)) {
        std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
        g_virtual_swapchains.erase(swapchain);
        return;
    }
    if (g_host_vkDestroySwapchainKHR) {
        g_host_vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetSwapchainImagesKHR(VkDevice device,
                                                                VkSwapchainKHR swapchain,
                                                                uint32_t *pSwapchainImageCount,
                                                                VkImage *pSwapchainImages)
{
    if (!pSwapchainImageCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSwapchain(swapchain)) {
        std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
        auto it = g_virtual_swapchains.find(swapchain);
        if (it == g_virtual_swapchains.end())
            return VK_ERROR_INITIALIZATION_FAILED;

        const auto &imgs = it->second.images;
        if (!pSwapchainImages) {
            *pSwapchainImageCount = static_cast<uint32_t>(imgs.size());
            return VK_SUCCESS;
        }
        uint32_t to_copy = std::min(*pSwapchainImageCount, static_cast<uint32_t>(imgs.size()));
        std::memcpy(pSwapchainImages, imgs.data(), to_copy * sizeof(VkImage));
        *pSwapchainImageCount = to_copy;
        return VK_SUCCESS;
    }

    if (g_host_vkGetSwapchainImagesKHR) {
        return g_host_vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount,
                                              pSwapchainImages);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

__attribute__((sysv_abi)) VkResult Sysv_vkAcquireNextImageKHR(VkDevice device,
                                                              VkSwapchainKHR swapchain,
                                                              uint64_t timeout,
                                                              VkSemaphore semaphore, VkFence fence,
                                                              uint32_t *pImageIndex)
{
    if (!pImageIndex)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (IsVirtualSwapchain(swapchain)) {
        std::lock_guard<std::mutex> lock(g_virtual_wsi_mutex);
        auto it = g_virtual_swapchains.find(swapchain);
        if (it == g_virtual_swapchains.end())
            return VK_ERROR_INITIALIZATION_FAILED;

        *pImageIndex = it->second.current_index;
        it->second.current_index = (it->second.current_index + 1) % it->second.images.size();
        return VK_SUCCESS;
    }

    if (g_host_vkAcquireNextImageKHR) {
        return g_host_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence,
                                            pImageIndex);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

__attribute__((sysv_abi)) VkResult Sysv_vkQueuePresentKHR(VkQueue queue,
                                                          const VkPresentInfoKHR *pPresentInfo)
{
    if (!pPresentInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    wsi::WindowManager::Instance().RecordFramePresented();

    bool any_virtual = false;
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        if (IsVirtualSwapchain(pPresentInfo->pSwapchains[i])) {
            any_virtual = true;
            break;
        }
    }

    if (any_virtual || !g_host_vkQueuePresentKHR) {
        return VK_SUCCESS;
    }
    return g_host_vkQueuePresentKHR(queue, pPresentInfo);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetAndroidHardwareBufferPropertiesANDROID(
    VkDevice device, const struct AHardwareBuffer *buffer,
    VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
    (void)device;
    if (!buffer || !pProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    pProperties->sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    pProperties->allocationSize = buffer->total_size;
    pProperties->memoryTypeBits = 0x1;
    return VK_SUCCESS;
}

PFN_vkVoidFunction ResolveSysvProc(std::string_view name) noexcept
{
    if (name == "vkGetInstanceProcAddr")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetInstanceProcAddr);
    if (name == "vkGetDeviceProcAddr")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetDeviceProcAddr);
    if (name == "vkCreateInstance")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateInstance);
    if (name == "vkDestroyInstance")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyInstance);
    if (name == "vkEnumeratePhysicalDevices")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkEnumeratePhysicalDevices);
    if (name == "vkGetPhysicalDeviceProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceProperties);
    if (name == "vkGetPhysicalDeviceFeatures")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceFeatures);
    if (name == "vkGetPhysicalDeviceQueueFamilyProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceQueueFamilyProperties);
    if (name == "vkGetPhysicalDeviceMemoryProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceMemoryProperties);
    if (name == "vkCreateDevice")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateDevice);
    if (name == "vkDestroyDevice")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyDevice);
    if (name == "vkGetDeviceQueue")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetDeviceQueue);
    if (name == "vkCreateBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateBuffer);
    if (name == "vkDestroyBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyBuffer);
    if (name == "vkGetBufferMemoryRequirements")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetBufferMemoryRequirements);
    if (name == "vkAllocateMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkAllocateMemory);
    if (name == "vkFreeMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkFreeMemory);
    if (name == "vkBindBufferMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkBindBufferMemory);
    if (name == "vkMapMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkMapMemory);
    if (name == "vkUnmapMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkUnmapMemory);
    if (name == "vkCreateCommandPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateCommandPool);
    if (name == "vkDestroyCommandPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyCommandPool);
    if (name == "vkAllocateCommandBuffers")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkAllocateCommandBuffers);
    if (name == "vkFreeCommandBuffers")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkFreeCommandBuffers);
    if (name == "vkBeginCommandBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkBeginCommandBuffer);
    if (name == "vkEndCommandBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkEndCommandBuffer);
    if (name == "vkQueueSubmit")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkQueueSubmit);
    if (name == "vkQueueWaitIdle")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkQueueWaitIdle);
    if (name == "vkDeviceWaitIdle")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDeviceWaitIdle);
    if (name == "vkEnumerateInstanceExtensionProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkEnumerateInstanceExtensionProperties);
    if (name == "vkEnumerateDeviceExtensionProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkEnumerateDeviceExtensionProperties);

    // WSI exports
    if (name == "vkCreateAndroidSurfaceKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateAndroidSurfaceKHR);
    if (name == "vkCreateWin32SurfaceKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateWin32SurfaceKHR);
    if (name == "vkDestroySurfaceKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroySurfaceKHR);
    if (name == "vkGetPhysicalDeviceSurfaceSupportKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceSurfaceSupportKHR);
    if (name == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    if (name == "vkGetPhysicalDeviceSurfaceFormatsKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceSurfaceFormatsKHR);
    if (name == "vkGetPhysicalDeviceSurfacePresentModesKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceSurfacePresentModesKHR);
    if (name == "vkCreateSwapchainKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateSwapchainKHR);
    if (name == "vkDestroySwapchainKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroySwapchainKHR);
    if (name == "vkGetSwapchainImagesKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetSwapchainImagesKHR);
    if (name == "vkAcquireNextImageKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkAcquireNextImageKHR);
    if (name == "vkQueuePresentKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkQueuePresentKHR);
    if (name == "vkGetAndroidHardwareBufferPropertiesANDROID")
        return reinterpret_cast<PFN_vkVoidFunction>(
            Sysv_vkGetAndroidHardwareBufferPropertiesANDROID);

    return nullptr;
}

__attribute__((sysv_abi)) PFN_vkVoidFunction Sysv_vkGetInstanceProcAddr(VkInstance,
                                                                        const char *pName)
{
    if (!pName)
        return nullptr;
    return ResolveSysvProc(pName);
}

__attribute__((sysv_abi)) PFN_vkVoidFunction Sysv_vkGetDeviceProcAddr(VkDevice, const char *pName)
{
    if (!pName)
        return nullptr;
    return ResolveSysvProc(pName);
}

template <typename T> T LoadProc(HMODULE mod, const char *name)
{
    return reinterpret_cast<T>(GetProcAddress(mod, name));
}

} // namespace

VulkanBridge &VulkanBridge::Instance() noexcept
{
    static VulkanBridge instance;
    return instance;
}

bool VulkanBridge::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    HMODULE mod = LoadLibraryA("vulkan-1.dll");
    if (!mod) {
        return false;
    }

    m_module = mod;

    g_host_vkGetInstanceProcAddr =
        LoadProc<PFN_vkGetInstanceProcAddr>(mod, "vkGetInstanceProcAddr");
    g_host_vkGetDeviceProcAddr = LoadProc<PFN_vkGetDeviceProcAddr>(mod, "vkGetDeviceProcAddr");
    g_host_vkCreateInstance = LoadProc<PFN_vkCreateInstance>(mod, "vkCreateInstance");
    g_host_vkDestroyInstance = LoadProc<PFN_vkDestroyInstance>(mod, "vkDestroyInstance");
    g_host_vkEnumeratePhysicalDevices =
        LoadProc<PFN_vkEnumeratePhysicalDevices>(mod, "vkEnumeratePhysicalDevices");
    g_host_vkGetPhysicalDeviceProperties =
        LoadProc<PFN_vkGetPhysicalDeviceProperties>(mod, "vkGetPhysicalDeviceProperties");
    g_host_vkGetPhysicalDeviceFeatures =
        LoadProc<PFN_vkGetPhysicalDeviceFeatures>(mod, "vkGetPhysicalDeviceFeatures");
    g_host_vkGetPhysicalDeviceQueueFamilyProperties =
        LoadProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            mod, "vkGetPhysicalDeviceQueueFamilyProperties");
    g_host_vkGetPhysicalDeviceMemoryProperties = LoadProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
        mod, "vkGetPhysicalDeviceMemoryProperties");
    g_host_vkCreateDevice = LoadProc<PFN_vkCreateDevice>(mod, "vkCreateDevice");
    g_host_vkDestroyDevice = LoadProc<PFN_vkDestroyDevice>(mod, "vkDestroyDevice");
    g_host_vkGetDeviceQueue = LoadProc<PFN_vkGetDeviceQueue>(mod, "vkGetDeviceQueue");
    g_host_vkCreateBuffer = LoadProc<PFN_vkCreateBuffer>(mod, "vkCreateBuffer");
    g_host_vkDestroyBuffer = LoadProc<PFN_vkDestroyBuffer>(mod, "vkDestroyBuffer");
    g_host_vkGetBufferMemoryRequirements =
        LoadProc<PFN_vkGetBufferMemoryRequirements>(mod, "vkGetBufferMemoryRequirements");
    g_host_vkAllocateMemory = LoadProc<PFN_vkAllocateMemory>(mod, "vkAllocateMemory");
    g_host_vkFreeMemory = LoadProc<PFN_vkFreeMemory>(mod, "vkFreeMemory");
    g_host_vkBindBufferMemory = LoadProc<PFN_vkBindBufferMemory>(mod, "vkBindBufferMemory");
    g_host_vkMapMemory = LoadProc<PFN_vkMapMemory>(mod, "vkMapMemory");
    g_host_vkUnmapMemory = LoadProc<PFN_vkUnmapMemory>(mod, "vkUnmapMemory");
    g_host_vkCreateCommandPool = LoadProc<PFN_vkCreateCommandPool>(mod, "vkCreateCommandPool");
    g_host_vkDestroyCommandPool = LoadProc<PFN_vkDestroyCommandPool>(mod, "vkDestroyCommandPool");
    g_host_vkAllocateCommandBuffers =
        LoadProc<PFN_vkAllocateCommandBuffers>(mod, "vkAllocateCommandBuffers");
    g_host_vkFreeCommandBuffers = LoadProc<PFN_vkFreeCommandBuffers>(mod, "vkFreeCommandBuffers");
    g_host_vkBeginCommandBuffer = LoadProc<PFN_vkBeginCommandBuffer>(mod, "vkBeginCommandBuffer");
    g_host_vkEndCommandBuffer = LoadProc<PFN_vkEndCommandBuffer>(mod, "vkEndCommandBuffer");
    g_host_vkQueueSubmit = LoadProc<PFN_vkQueueSubmit>(mod, "vkQueueSubmit");
    g_host_vkQueueWaitIdle = LoadProc<PFN_vkQueueWaitIdle>(mod, "vkQueueWaitIdle");
    g_host_vkDeviceWaitIdle = LoadProc<PFN_vkDeviceWaitIdle>(mod, "vkDeviceWaitIdle");
    g_host_vkEnumerateInstanceExtensionProperties =
        LoadProc<PFN_vkEnumerateInstanceExtensionProperties>(
            mod, "vkEnumerateInstanceExtensionProperties");
    g_host_vkEnumerateDeviceExtensionProperties =
        LoadProc<PFN_vkEnumerateDeviceExtensionProperties>(mod,
                                                           "vkEnumerateDeviceExtensionProperties");

    // WSI host functions
    g_host_vkCreateWin32SurfaceKHR =
        LoadProc<PFN_vkCreateWin32SurfaceKHR>(mod, "vkCreateWin32SurfaceKHR");
    g_host_vkDestroySurfaceKHR = LoadProc<PFN_vkDestroySurfaceKHR>(mod, "vkDestroySurfaceKHR");
    g_host_vkGetPhysicalDeviceSurfaceSupportKHR =
        LoadProc<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(mod,
                                                           "vkGetPhysicalDeviceSurfaceSupportKHR");
    g_host_vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        LoadProc<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            mod, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    g_host_vkGetPhysicalDeviceSurfaceFormatsKHR =
        LoadProc<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(mod,
                                                           "vkGetPhysicalDeviceSurfaceFormatsKHR");
    g_host_vkGetPhysicalDeviceSurfacePresentModesKHR =
        LoadProc<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
            mod, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    g_host_vkCreateSwapchainKHR = LoadProc<PFN_vkCreateSwapchainKHR>(mod, "vkCreateSwapchainKHR");
    g_host_vkDestroySwapchainKHR =
        LoadProc<PFN_vkDestroySwapchainKHR>(mod, "vkDestroySwapchainKHR");
    g_host_vkGetSwapchainImagesKHR =
        LoadProc<PFN_vkGetSwapchainImagesKHR>(mod, "vkGetSwapchainImagesKHR");
    g_host_vkAcquireNextImageKHR =
        LoadProc<PFN_vkAcquireNextImageKHR>(mod, "vkAcquireNextImageKHR");
    g_host_vkQueuePresentKHR = LoadProc<PFN_vkQueuePresentKHR>(mod, "vkQueuePresentKHR");

    m_dispatch_table.version = 2;
    m_dispatch_table.vkGetInstanceProcAddr = Sysv_vkGetInstanceProcAddr;
    m_dispatch_table.vkGetDeviceProcAddr = Sysv_vkGetDeviceProcAddr;
    m_dispatch_table.vkCreateInstance = Sysv_vkCreateInstance;
    m_dispatch_table.vkDestroyInstance = Sysv_vkDestroyInstance;
    m_dispatch_table.vkEnumeratePhysicalDevices = Sysv_vkEnumeratePhysicalDevices;
    m_dispatch_table.vkGetPhysicalDeviceProperties = Sysv_vkGetPhysicalDeviceProperties;
    m_dispatch_table.vkGetPhysicalDeviceFeatures = Sysv_vkGetPhysicalDeviceFeatures;
    m_dispatch_table.vkGetPhysicalDeviceQueueFamilyProperties =
        Sysv_vkGetPhysicalDeviceQueueFamilyProperties;
    m_dispatch_table.vkGetPhysicalDeviceMemoryProperties = Sysv_vkGetPhysicalDeviceMemoryProperties;
    m_dispatch_table.vkCreateDevice = Sysv_vkCreateDevice;
    m_dispatch_table.vkDestroyDevice = Sysv_vkDestroyDevice;
    m_dispatch_table.vkGetDeviceQueue = Sysv_vkGetDeviceQueue;
    m_dispatch_table.vkCreateBuffer = Sysv_vkCreateBuffer;
    m_dispatch_table.vkDestroyBuffer = Sysv_vkDestroyBuffer;
    m_dispatch_table.vkGetBufferMemoryRequirements = Sysv_vkGetBufferMemoryRequirements;
    m_dispatch_table.vkAllocateMemory = Sysv_vkAllocateMemory;
    m_dispatch_table.vkFreeMemory = Sysv_vkFreeMemory;
    m_dispatch_table.vkBindBufferMemory = Sysv_vkBindBufferMemory;
    m_dispatch_table.vkMapMemory = Sysv_vkMapMemory;
    m_dispatch_table.vkUnmapMemory = Sysv_vkUnmapMemory;
    m_dispatch_table.vkCreateCommandPool = Sysv_vkCreateCommandPool;
    m_dispatch_table.vkDestroyCommandPool = Sysv_vkDestroyCommandPool;
    m_dispatch_table.vkAllocateCommandBuffers = Sysv_vkAllocateCommandBuffers;
    m_dispatch_table.vkFreeCommandBuffers = Sysv_vkFreeCommandBuffers;
    m_dispatch_table.vkBeginCommandBuffer = Sysv_vkBeginCommandBuffer;
    m_dispatch_table.vkEndCommandBuffer = Sysv_vkEndCommandBuffer;
    m_dispatch_table.vkQueueSubmit = Sysv_vkQueueSubmit;
    m_dispatch_table.vkQueueWaitIdle = Sysv_vkQueueWaitIdle;
    m_dispatch_table.vkDeviceWaitIdle = Sysv_vkDeviceWaitIdle;
    m_dispatch_table.vkEnumerateInstanceExtensionProperties =
        Sysv_vkEnumerateInstanceExtensionProperties;
    m_dispatch_table.vkEnumerateDeviceExtensionProperties =
        Sysv_vkEnumerateDeviceExtensionProperties;

    // WSI dispatch table
    m_dispatch_table.vkCreateAndroidSurfaceKHR = Sysv_vkCreateAndroidSurfaceKHR;
    m_dispatch_table.vkDestroySurfaceKHR = Sysv_vkDestroySurfaceKHR;
    m_dispatch_table.vkGetPhysicalDeviceSurfaceSupportKHR =
        Sysv_vkGetPhysicalDeviceSurfaceSupportKHR;
    m_dispatch_table.vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        Sysv_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    m_dispatch_table.vkGetPhysicalDeviceSurfaceFormatsKHR =
        Sysv_vkGetPhysicalDeviceSurfaceFormatsKHR;
    m_dispatch_table.vkGetPhysicalDeviceSurfacePresentModesKHR =
        Sysv_vkGetPhysicalDeviceSurfacePresentModesKHR;
    m_dispatch_table.vkCreateSwapchainKHR = Sysv_vkCreateSwapchainKHR;
    m_dispatch_table.vkDestroySwapchainKHR = Sysv_vkDestroySwapchainKHR;
    m_dispatch_table.vkGetSwapchainImagesKHR = Sysv_vkGetSwapchainImagesKHR;
    m_dispatch_table.vkAcquireNextImageKHR = Sysv_vkAcquireNextImageKHR;
    m_dispatch_table.vkQueuePresentKHR = Sysv_vkQueuePresentKHR;
    m_dispatch_table.vkGetAndroidHardwareBufferPropertiesANDROID =
        Sysv_vkGetAndroidHardwareBufferPropertiesANDROID;

    m_initialized = true;
    return true;
}

void VulkanBridge::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_module) {
        FreeLibrary(static_cast<HMODULE>(m_module));
        m_module = nullptr;
    }
    m_dispatch_table = {};
    m_initialized = false;
}

bool VulkanBridge::IsInitialized() const noexcept { return m_initialized; }

const VulkanDispatchTable *VulkanBridge::GetDispatchTable() const noexcept
{
    return &m_dispatch_table;
}

} // namespace portage::vulkan
