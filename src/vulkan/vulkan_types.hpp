#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <windows.h>

#include <cstdint>

struct AHardwareBuffer;

namespace portage::vulkan {

inline constexpr uint32_t VULKAN_IOCTL_GET_DISPATCH_TABLE = 0x80085601u;

// SysV ABI Function Pointer Types for Vulkan Passthrough
using SysvPfnVkGetInstanceProcAddr = __attribute__((sysv_abi))
PFN_vkVoidFunction (*)(VkInstance instance, const char *pName);
using SysvPfnVkGetDeviceProcAddr = __attribute__((sysv_abi))
PFN_vkVoidFunction (*)(VkDevice device, const char *pName);
using SysvPfnVkCreateInstance = __attribute__((sysv_abi))
VkResult (*)(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
             VkInstance *pInstance);
using SysvPfnVkDestroyInstance = __attribute__((sysv_abi)) void (*)(
    VkInstance instance, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkEnumeratePhysicalDevices = __attribute__((sysv_abi))
VkResult (*)(VkInstance instance, uint32_t *pPhysicalDeviceCount,
             VkPhysicalDevice *pPhysicalDevices);
using SysvPfnVkGetPhysicalDeviceProperties = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
using SysvPfnVkGetPhysicalDeviceFeatures = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
using SysvPfnVkGetPhysicalDeviceQueueFamilyProperties = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties);
using SysvPfnVkGetPhysicalDeviceMemoryProperties = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
using SysvPfnVkCreateDevice = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
using SysvPfnVkDestroyDevice =
    __attribute__((sysv_abi)) void (*)(VkDevice device, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetDeviceQueue = __attribute__((sysv_abi)) void (*)(VkDevice device,
                                                                   uint32_t queueFamilyIndex,
                                                                   uint32_t queueIndex,
                                                                   VkQueue *pQueue);
using SysvPfnVkCreateBuffer = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
using SysvPfnVkDestroyBuffer = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetBufferMemoryRequirements = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
using SysvPfnVkAllocateMemory = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
             const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
using SysvPfnVkFreeMemory = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkBindBufferMemory = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
using SysvPfnVkMapMemory = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
             VkMemoryMapFlags flags, void **ppData);
using SysvPfnVkUnmapMemory = __attribute__((sysv_abi)) void (*)(VkDevice device,
                                                                VkDeviceMemory memory);
using SysvPfnVkCreateCommandPool = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
using SysvPfnVkDestroyCommandPool = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkAllocateCommandBuffers = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
             VkCommandBuffer *pCommandBuffers);
using SysvPfnVkFreeCommandBuffers = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers);
using SysvPfnVkBeginCommandBuffer = __attribute__((sysv_abi))
VkResult (*)(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
using SysvPfnVkEndCommandBuffer = __attribute__((sysv_abi))
VkResult (*)(VkCommandBuffer commandBuffer);
using SysvPfnVkQueueSubmit = __attribute__((sysv_abi))
VkResult (*)(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
using SysvPfnVkQueueWaitIdle = __attribute__((sysv_abi)) VkResult (*)(VkQueue queue);
using SysvPfnVkDeviceWaitIdle = __attribute__((sysv_abi)) VkResult (*)(VkDevice device);
using SysvPfnVkEnumerateInstanceExtensionProperties = __attribute__((sysv_abi))
VkResult (*)(const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties);
using SysvPfnVkEnumerateDeviceExtensionProperties = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount,
             VkExtensionProperties *pProperties);

// WSI Function Pointer Types
using SysvPfnVkCreateAndroidSurfaceKHR = __attribute__((sysv_abi))
VkResult (*)(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
using SysvPfnVkCreateWin32SurfaceKHR = __attribute__((sysv_abi))
VkResult (*)(VkInstance instance, const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
using SysvPfnVkDestroySurfaceKHR = __attribute__((sysv_abi)) void (*)(
    VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetPhysicalDeviceSurfaceSupportKHR = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface,
             VkBool32 *pSupported);
using SysvPfnVkGetPhysicalDeviceSurfaceCapabilitiesKHR = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
             VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
using SysvPfnVkGetPhysicalDeviceSurfaceFormatsKHR = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount,
             VkSurfaceFormatKHR *pSurfaceFormats);
using SysvPfnVkGetPhysicalDeviceSurfacePresentModesKHR = __attribute__((sysv_abi))
VkResult (*)(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount,
             VkPresentModeKHR *pPresentModes);

using SysvPfnVkCreateSwapchainKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
using SysvPfnVkDestroySwapchainKHR = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetSwapchainImagesKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
             VkImage *pSwapchainImages);
using SysvPfnVkAcquireNextImageKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
             VkFence fence, uint32_t *pImageIndex);
using SysvPfnVkQueuePresentKHR = __attribute__((sysv_abi))
VkResult (*)(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);
using SysvPfnVkGetAndroidHardwareBufferPropertiesANDROID = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const struct AHardwareBuffer *buffer,
             VkAndroidHardwareBufferPropertiesANDROID *pProperties);

struct VulkanDispatchTable {
    uint32_t version{2};
    SysvPfnVkGetInstanceProcAddr vkGetInstanceProcAddr{nullptr};
    SysvPfnVkGetDeviceProcAddr vkGetDeviceProcAddr{nullptr};
    SysvPfnVkCreateInstance vkCreateInstance{nullptr};
    SysvPfnVkDestroyInstance vkDestroyInstance{nullptr};
    SysvPfnVkEnumeratePhysicalDevices vkEnumeratePhysicalDevices{nullptr};
    SysvPfnVkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties{nullptr};
    SysvPfnVkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures{nullptr};
    SysvPfnVkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties{
        nullptr};
    SysvPfnVkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties{nullptr};
    SysvPfnVkCreateDevice vkCreateDevice{nullptr};
    SysvPfnVkDestroyDevice vkDestroyDevice{nullptr};
    SysvPfnVkGetDeviceQueue vkGetDeviceQueue{nullptr};
    SysvPfnVkCreateBuffer vkCreateBuffer{nullptr};
    SysvPfnVkDestroyBuffer vkDestroyBuffer{nullptr};
    SysvPfnVkGetBufferMemoryRequirements vkGetBufferMemoryRequirements{nullptr};
    SysvPfnVkAllocateMemory vkAllocateMemory{nullptr};
    SysvPfnVkFreeMemory vkFreeMemory{nullptr};
    SysvPfnVkBindBufferMemory vkBindBufferMemory{nullptr};
    SysvPfnVkMapMemory vkMapMemory{nullptr};
    SysvPfnVkUnmapMemory vkUnmapMemory{nullptr};
    SysvPfnVkCreateCommandPool vkCreateCommandPool{nullptr};
    SysvPfnVkDestroyCommandPool vkDestroyCommandPool{nullptr};
    SysvPfnVkAllocateCommandBuffers vkAllocateCommandBuffers{nullptr};
    SysvPfnVkFreeCommandBuffers vkFreeCommandBuffers{nullptr};
    SysvPfnVkBeginCommandBuffer vkBeginCommandBuffer{nullptr};
    SysvPfnVkEndCommandBuffer vkEndCommandBuffer{nullptr};
    SysvPfnVkQueueSubmit vkQueueSubmit{nullptr};
    SysvPfnVkQueueWaitIdle vkQueueWaitIdle{nullptr};
    SysvPfnVkDeviceWaitIdle vkDeviceWaitIdle{nullptr};
    SysvPfnVkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties{nullptr};
    SysvPfnVkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties{nullptr};

    // WSI dispatch entries
    SysvPfnVkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR{nullptr};
    SysvPfnVkDestroySurfaceKHR vkDestroySurfaceKHR{nullptr};
    SysvPfnVkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR{nullptr};
    SysvPfnVkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR{
        nullptr};
    SysvPfnVkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR{nullptr};
    SysvPfnVkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR{
        nullptr};
    SysvPfnVkCreateSwapchainKHR vkCreateSwapchainKHR{nullptr};
    SysvPfnVkDestroySwapchainKHR vkDestroySwapchainKHR{nullptr};
    SysvPfnVkGetSwapchainImagesKHR vkGetSwapchainImagesKHR{nullptr};
    SysvPfnVkAcquireNextImageKHR vkAcquireNextImageKHR{nullptr};
    SysvPfnVkQueuePresentKHR vkQueuePresentKHR{nullptr};
    SysvPfnVkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID{
        nullptr};
};

} // namespace portage::vulkan
