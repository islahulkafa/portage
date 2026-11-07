#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#define VULKAN_IOCTL_GET_DISPATCH_TABLE 0x80085601u

// WSI Function Pointer Types
typedef VkResult (*PFN_vkCreateAndroidSurfaceKHR)(VkInstance instance,
                                                  const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
                                                  const VkAllocationCallbacks *pAllocator,
                                                  VkSurfaceKHR *pSurface);
typedef void (*PFN_vkDestroySurfaceKHR)(VkInstance instance, VkSurfaceKHR surface,
                                        const VkAllocationCallbacks *pAllocator);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice physicalDevice,
                                                             uint32_t queueFamilyIndex,
                                                             VkSurfaceKHR surface,
                                                             VkBool32 *pSupported);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice physicalDevice,
                                                             VkSurfaceKHR surface,
                                                             uint32_t *pSurfaceFormatCount,
                                                             VkSurfaceFormatKHR *pSurfaceFormats);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice physicalDevice,
                                                                  VkSurfaceKHR surface,
                                                                  uint32_t *pPresentModeCount,
                                                                  VkPresentModeKHR *pPresentModes);
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice device,
                                             const VkSwapchainCreateInfoKHR *pCreateInfo,
                                             const VkAllocationCallbacks *pAllocator,
                                             VkSwapchainKHR *pSwapchain);
typedef void (*PFN_vkDestroySwapchainKHR)(VkDevice device, VkSwapchainKHR swapchain,
                                          const VkAllocationCallbacks *pAllocator);
typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice device, VkSwapchainKHR swapchain,
                                                uint32_t *pSwapchainImageCount,
                                                VkImage *pSwapchainImages);
typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice device, VkSwapchainKHR swapchain,
                                              uint64_t timeout, VkSemaphore semaphore,
                                              VkFence fence, uint32_t *pImageIndex);
typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);
typedef VkResult (*PFN_vkGetAndroidHardwareBufferPropertiesANDROID)(
    VkDevice device, const struct AHardwareBuffer *buffer,
    VkAndroidHardwareBufferPropertiesANDROID *pProperties);

struct VulkanDispatchTable {
    uint32_t version;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkDestroyInstance vkDestroyInstance;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkQueueWaitIdle vkQueueWaitIdle;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;

    // WSI function pointers
    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;
};

static const struct VulkanDispatchTable *g_dispatch = NULL;

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(mode)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_ioctl(int fd, uint32_t cmd, void *arg)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(16), "D"(fd), "S"(cmd), "d"(arg)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static int ensure_init(void)
{
    if (g_dispatch != NULL) {
        return 0;
    }
    int fd = (int)sys_openat(-100, "/dev/vulkan", 2, 0);
    if (fd < 0) {
        return -1;
    }
    const struct VulkanDispatchTable *table = NULL;
    int64_t r = sys_ioctl(fd, VULKAN_IOCTL_GET_DISPATCH_TABLE, &table);
    sys_close(fd);
    if (r != 0 || table == NULL) {
        return -1;
    }
    g_dispatch = table;
    return 0;
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance,
                                                                                const char *pName)
{
    if (ensure_init() != 0)
        return NULL;
    return g_dispatch->vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device,
                                                                              const char *pName)
{
    if (ensure_init() != 0)
        return NULL;
    return g_dispatch->vkGetDeviceProcAddr(device, pName);
}

__attribute__((visibility("default"))) VkResult
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                 VkInstance *pInstance)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

__attribute__((visibility("default"))) void
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0)
        g_dispatch->vkDestroyInstance(instance, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                              VkPhysicalDeviceProperties *pProperties)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                         uint32_t *pQueueFamilyPropertyCount,
                                         VkQueueFamilyProperties *pQueueFamilyProperties)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

__attribute__((visibility("default"))) VkResult
vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

__attribute__((visibility("default"))) void vkDestroyDevice(VkDevice device,
                                                            const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0)
        g_dispatch->vkDestroyDevice(device, pAllocator);
}

__attribute__((visibility("default"))) void
vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
}

__attribute__((visibility("default"))) VkResult
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
}

__attribute__((visibility("default"))) void vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                                            const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0)
        g_dispatch->vkDestroyBuffer(device, buffer, pAllocator);
}

__attribute__((visibility("default"))) void
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                              VkMemoryRequirements *pMemoryRequirements)
{
    if (ensure_init() == 0)
        g_dispatch->vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
}

__attribute__((visibility("default"))) VkResult
vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                 const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

__attribute__((visibility("default"))) void vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                                         const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0)
        g_dispatch->vkFreeMemory(device, memory, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer,
                                                                   VkDeviceMemory memory,
                                                                   VkDeviceSize memoryOffset)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkBindBufferMemory(device, buffer, memory, memoryOffset);
}

__attribute__((visibility("default"))) VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory,
                                                            VkDeviceSize offset, VkDeviceSize size,
                                                            VkMemoryMapFlags flags, void **ppData)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkMapMemory(device, memory, offset, size, flags, ppData);
}

__attribute__((visibility("default"))) void vkUnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    if (ensure_init() == 0)
        g_dispatch->vkUnmapMemory(device, memory);
}

__attribute__((visibility("default"))) VkResult
vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
}

__attribute__((visibility("default"))) void
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0)
        g_dispatch->vkDestroyCommandPool(device, commandPool, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                         VkCommandBuffer *pCommandBuffers)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}

__attribute__((visibility("default"))) void
vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                     const VkCommandBuffer *pCommandBuffers)
{
    if (ensure_init() == 0)
        g_dispatch->vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}

__attribute__((visibility("default"))) VkResult
vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

__attribute__((visibility("default"))) VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkEndCommandBuffer(commandBuffer);
}

__attribute__((visibility("default"))) VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount,
                                                              const VkSubmitInfo *pSubmits,
                                                              VkFence fence)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkQueueSubmit(queue, submitCount, pSubmits, fence);
}

__attribute__((visibility("default"))) VkResult vkQueueWaitIdle(VkQueue queue)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkQueueWaitIdle(queue);
}

__attribute__((visibility("default"))) VkResult vkDeviceWaitIdle(VkDevice device)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkDeviceWaitIdle(device);
}

__attribute__((visibility("default"))) VkResult vkEnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount,
                                                              pProperties);
}

__attribute__((visibility("default"))) VkResult
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                     uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName,
                                                            pPropertyCount, pProperties);
}

__attribute__((visibility("default"))) VkResult
vkCreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateAndroidSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}

__attribute__((visibility("default"))) void
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                    const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch->vkDestroySurfaceKHR) {
        g_dispatch->vkDestroySurfaceKHR(instance, surface, pAllocator);
    }
}

__attribute__((visibility("default"))) VkResult
vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                     VkSurfaceKHR surface, VkBool32 *pSupported)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex,
                                                            surface, pSupported);
}

__attribute__((visibility("default"))) VkResult
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                          VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                                                 pSurfaceCapabilities);
}

__attribute__((visibility("default"))) VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                            pSurfaceFormatCount, pSurfaceFormats);
}

__attribute__((visibility("default"))) VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                                 pPresentModeCount, pPresentModes);
}

__attribute__((visibility("default"))) VkResult
vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

__attribute__((visibility("default"))) void
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch->vkDestroySwapchainKHR) {
        g_dispatch->vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }
}

__attribute__((visibility("default"))) VkResult
vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
                        VkImage *pSwapchainImages)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount,
                                               pSwapchainImages);
}

__attribute__((visibility("default"))) VkResult
vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                      VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence,
                                             pImageIndex);
}

__attribute__((visibility("default"))) VkResult
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    if (ensure_init() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkQueuePresentKHR(queue, pPresentInfo);
}

__attribute__((visibility("default"))) VkResult
vkGetAndroidHardwareBufferPropertiesANDROID(VkDevice device, const struct AHardwareBuffer *buffer,
                                            VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
    if (ensure_init() != 0 || !g_dispatch ||
        !g_dispatch->vkGetAndroidHardwareBufferPropertiesANDROID) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_dispatch->vkGetAndroidHardwareBufferPropertiesANDROID(device, buffer, pProperties);
}
