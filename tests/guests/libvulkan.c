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
    PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;

    // Shaders & Pipelines
    PFN_vkCreateShaderModule vkCreateShaderModule;
    PFN_vkDestroyShaderModule vkDestroyShaderModule;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
    PFN_vkCreatePipelineCache vkCreatePipelineCache;
    PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
    PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
    PFN_vkCreateComputePipelines vkCreateComputePipelines;
    PFN_vkDestroyPipeline vkDestroyPipeline;

    // RenderPass, Framebuffers & Dynamic Rendering
    PFN_vkCreateRenderPass vkCreateRenderPass;
    PFN_vkCreateRenderPass2 vkCreateRenderPass2;
    PFN_vkDestroyRenderPass vkDestroyRenderPass;
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
    PFN_vkCmdNextSubpass vkCmdNextSubpass;
    PFN_vkCmdBeginRendering vkCmdBeginRendering;
    PFN_vkCmdEndRendering vkCmdEndRendering;

    // Images, Views & Samplers
    PFN_vkCreateImage vkCreateImage;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkBindImageMemory2 vkBindImageMemory2;
    PFN_vkBindBufferMemory2 vkBindBufferMemory2;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkCreateSampler vkCreateSampler;
    PFN_vkDestroySampler vkDestroySampler;

    // Descriptors & Push Constants
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkResetDescriptorPool vkResetDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdPushConstants vkCmdPushConstants;

    // Synchronization
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkResetFences vkResetFences;
    PFN_vkGetFenceStatus vkGetFenceStatus;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkCreateSemaphore vkCreateSemaphore;
    PFN_vkDestroySemaphore vkDestroySemaphore;
    PFN_vkGetSemaphoreCounterValue vkGetSemaphoreCounterValue;
    PFN_vkWaitSemaphores vkWaitSemaphores;
    PFN_vkSignalSemaphore vkSignalSemaphore;
    PFN_vkCreateEvent vkCreateEvent;
    PFN_vkDestroyEvent vkDestroyEvent;
    PFN_vkGetEventStatus vkGetEventStatus;
    PFN_vkSetEvent vkSetEvent;
    PFN_vkResetEvent vkResetEvent;

    // Command Buffer Recording
    PFN_vkResetCommandPool vkResetCommandPool;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdSetViewport vkCmdSetViewport;
    PFN_vkCmdSetScissor vkCmdSetScissor;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
    PFN_vkCmdDraw vkCmdDraw;
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
    PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
    PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
    PFN_vkCmdDispatch vkCmdDispatch;
    PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
    PFN_vkCmdCopyImage vkCmdCopyImage;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
    PFN_vkCmdBlitImage vkCmdBlitImage;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2;
    PFN_vkCmdClearColorImage vkCmdClearColorImage;
    PFN_vkCmdClearAttachments vkCmdClearAttachments;
    PFN_vkCmdFillBuffer vkCmdFillBuffer;
    PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;

    // Instance & PhysicalDevice Queries
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;
    PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2;
    PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
    PFN_vkGetDeviceQueue2 vkGetDeviceQueue2;
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;

    // External FDs
    PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
    PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
    PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
    PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
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

__attribute__((visibility("default"))) VkResult vkGetMemoryAndroidHardwareBufferANDROID(
    VkDevice device, const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
    struct AHardwareBuffer **pBuffer)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetMemoryAndroidHardwareBufferANDROID)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetMemoryAndroidHardwareBufferANDROID(device, pInfo, pBuffer);
}

// Shaders & Pipelines
__attribute__((visibility("default"))) VkResult
vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateShaderModule)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
}

__attribute__((visibility("default"))) void
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyShaderModule)
        g_dispatch->vkDestroyShaderModule(device, shaderModule, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreatePipelineLayout)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
}

__attribute__((visibility("default"))) void
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyPipelineLayout)
        g_dispatch->vkDestroyPipelineLayout(device, pipelineLayout, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreatePipelineCache)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreatePipelineCache(device, pCreateInfo, pAllocator, pPipelineCache);
}

__attribute__((visibility("default"))) void
vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyPipelineCache)
        g_dispatch->vkDestroyPipelineCache(device, pipelineCache, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkGetPipelineCacheData(
    VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetPipelineCacheData)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetPipelineCacheData(device, pipelineCache, pDataSize, pData);
}

__attribute__((visibility("default"))) VkResult
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateGraphicsPipelines)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount,
                                                 pCreateInfos, pAllocator, pPipelines);
}

__attribute__((visibility("default"))) VkResult
vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo *pCreateInfos,
                         const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateComputePipelines(device, pipelineCache, createInfoCount,
                                                pCreateInfos, pAllocator, pPipelines);
}

__attribute__((visibility("default"))) void
vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyPipeline)
        g_dispatch->vkDestroyPipeline(device, pipeline, pAllocator);
}

// RenderPass, Framebuffers & Dynamic Rendering
__attribute__((visibility("default"))) VkResult
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateRenderPass)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
}

__attribute__((visibility("default"))) VkResult
vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateRenderPass2)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
}

__attribute__((visibility("default"))) void
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyRenderPass)
        g_dispatch->vkDestroyRenderPass(device, renderPass, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateFramebuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
}

__attribute__((visibility("default"))) void
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyFramebuffer)
        g_dispatch->vkDestroyFramebuffer(device, framebuffer, pAllocator);
}

__attribute__((visibility("default"))) void
vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                     VkSubpassContents contents)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBeginRenderPass)
        g_dispatch->vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

__attribute__((visibility("default"))) void vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdEndRenderPass)
        g_dispatch->vkCmdEndRenderPass(commandBuffer);
}

__attribute__((visibility("default"))) void vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                                             VkSubpassContents contents)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdNextSubpass)
        g_dispatch->vkCmdNextSubpass(commandBuffer, contents);
}

__attribute__((visibility("default"))) void
vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBeginRendering)
        g_dispatch->vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

__attribute__((visibility("default"))) void vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdEndRendering)
        g_dispatch->vkCmdEndRendering(commandBuffer);
}

// Images, Views & Samplers
__attribute__((visibility("default"))) VkResult
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateImage)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateImage(device, pCreateInfo, pAllocator, pImage);
}

__attribute__((visibility("default"))) void vkDestroyImage(VkDevice device, VkImage image,
                                                           const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyImage)
        g_dispatch->vkDestroyImage(device, image, pAllocator);
}

__attribute__((visibility("default"))) void
vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                             VkMemoryRequirements *pMemoryRequirements)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetImageMemoryRequirements)
        g_dispatch->vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
}

__attribute__((visibility("default"))) void
vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo,
                              VkMemoryRequirements2 *pMemoryRequirements)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetImageMemoryRequirements2)
        g_dispatch->vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

__attribute__((visibility("default"))) VkResult vkBindImageMemory(VkDevice device, VkImage image,
                                                                  VkDeviceMemory memory,
                                                                  VkDeviceSize memoryOffset)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkBindImageMemory)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkBindImageMemory(device, image, memory, memoryOffset);
}

__attribute__((visibility("default"))) VkResult
vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkBindImageMemory2)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

__attribute__((visibility("default"))) VkResult vkBindBufferMemory2(
    VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkBindBufferMemory2)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

__attribute__((visibility("default"))) VkResult
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateImageView)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateImageView(device, pCreateInfo, pAllocator, pView);
}

__attribute__((visibility("default"))) void
vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyImageView)
        g_dispatch->vkDestroyImageView(device, imageView, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateSampler)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
}

__attribute__((visibility("default"))) void
vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroySampler)
        g_dispatch->vkDestroySampler(device, sampler, pAllocator);
}

// Descriptors & Push Constants
__attribute__((visibility("default"))) VkResult vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateDescriptorSetLayout)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
}

__attribute__((visibility("default"))) void
vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                             const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyDescriptorSetLayout)
        g_dispatch->vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
}

__attribute__((visibility("default"))) VkResult
vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
}

__attribute__((visibility("default"))) void
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyDescriptorPool)
        g_dispatch->vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkResetDescriptorPool(
    VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkResetDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkResetDescriptorPool(device, descriptorPool, flags);
}

__attribute__((visibility("default"))) VkResult
vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                         VkDescriptorSet *pDescriptorSets)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkAllocateDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
}

__attribute__((visibility("default"))) VkResult
vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
                     const VkDescriptorSet *pDescriptorSets)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkFreeDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount,
                                            pDescriptorSets);
}

__attribute__((visibility("default"))) void
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                       const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
                       const VkCopyDescriptorSet *pDescriptorCopies)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkUpdateDescriptorSets)
        g_dispatch->vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites,
                                           descriptorCopyCount, pDescriptorCopies);
}

__attribute__((visibility("default"))) void
vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                        VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                        const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                        const uint32_t *pDynamicOffsets)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBindDescriptorSets)
        g_dispatch->vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
                                            descriptorSetCount, pDescriptorSets, dynamicOffsetCount,
                                            pDynamicOffsets);
}

__attribute__((visibility("default"))) void vkCmdPushConstants(VkCommandBuffer commandBuffer,
                                                               VkPipelineLayout layout,
                                                               VkShaderStageFlags stageFlags,
                                                               uint32_t offset, uint32_t size,
                                                               const void *pValues)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdPushConstants)
        g_dispatch->vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

// Synchronization
__attribute__((visibility("default"))) VkResult
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateFence)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateFence(device, pCreateInfo, pAllocator, pFence);
}

__attribute__((visibility("default"))) void vkDestroyFence(VkDevice device, VkFence fence,
                                                           const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyFence)
        g_dispatch->vkDestroyFence(device, fence, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkResetFences(VkDevice device, uint32_t fenceCount,
                                                              const VkFence *pFences)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkResetFences)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkResetFences(device, fenceCount, pFences);
}

__attribute__((visibility("default"))) VkResult vkGetFenceStatus(VkDevice device, VkFence fence)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetFenceStatus)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetFenceStatus(device, fence);
}

__attribute__((visibility("default"))) VkResult vkWaitForFences(VkDevice device,
                                                                uint32_t fenceCount,
                                                                const VkFence *pFences,
                                                                VkBool32 waitAll, uint64_t timeout)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkWaitForFences)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkWaitForFences(device, fenceCount, pFences, waitAll, timeout);
}

__attribute__((visibility("default"))) VkResult
vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateSemaphore)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
}

__attribute__((visibility("default"))) void
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroySemaphore)
        g_dispatch->vkDestroySemaphore(device, semaphore, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkGetSemaphoreCounterValue(VkDevice device,
                                                                           VkSemaphore semaphore,
                                                                           uint64_t *pValue)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetSemaphoreCounterValue)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetSemaphoreCounterValue(device, semaphore, pValue);
}

__attribute__((visibility("default"))) VkResult
vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkWaitSemaphores)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkWaitSemaphores(device, pWaitInfo, timeout);
}

__attribute__((visibility("default"))) VkResult
vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkSignalSemaphore)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkSignalSemaphore(device, pSignalInfo);
}

__attribute__((visibility("default"))) VkResult
vkCreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkCreateEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkCreateEvent(device, pCreateInfo, pAllocator, pEvent);
}

__attribute__((visibility("default"))) void vkDestroyEvent(VkDevice device, VkEvent event,
                                                           const VkAllocationCallbacks *pAllocator)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkDestroyEvent)
        g_dispatch->vkDestroyEvent(device, event, pAllocator);
}

__attribute__((visibility("default"))) VkResult vkGetEventStatus(VkDevice device, VkEvent event)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetEventStatus)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetEventStatus(device, event);
}

__attribute__((visibility("default"))) VkResult vkSetEvent(VkDevice device, VkEvent event)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkSetEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkSetEvent(device, event);
}

__attribute__((visibility("default"))) VkResult vkResetEvent(VkDevice device, VkEvent event)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkResetEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkResetEvent(device, event);
}

// Command Buffer Recording
__attribute__((visibility("default"))) VkResult vkResetCommandPool(VkDevice device,
                                                                   VkCommandPool commandPool,
                                                                   VkCommandPoolResetFlags flags)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkResetCommandPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkResetCommandPool(device, commandPool, flags);
}

__attribute__((visibility("default"))) VkResult
vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkResetCommandBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkResetCommandBuffer(commandBuffer, flags);
}

__attribute__((visibility("default"))) void vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                                                              VkPipelineBindPoint pipelineBindPoint,
                                                              VkPipeline pipeline)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBindPipeline)
        g_dispatch->vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

__attribute__((visibility("default"))) void vkCmdSetViewport(VkCommandBuffer commandBuffer,
                                                             uint32_t firstViewport,
                                                             uint32_t viewportCount,
                                                             const VkViewport *pViewports)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdSetViewport)
        g_dispatch->vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

__attribute__((visibility("default"))) void vkCmdSetScissor(VkCommandBuffer commandBuffer,
                                                            uint32_t firstScissor,
                                                            uint32_t scissorCount,
                                                            const VkRect2D *pScissors)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdSetScissor)
        g_dispatch->vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

__attribute__((visibility("default"))) void
vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
                       const VkBuffer *pBuffers, const VkDeviceSize *pOffsets)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBindVertexBuffers)
        g_dispatch->vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers,
                                           pOffsets);
}

__attribute__((visibility("default"))) void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                                                                 VkBuffer buffer,
                                                                 VkDeviceSize offset,
                                                                 VkIndexType indexType)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBindIndexBuffer)
        g_dispatch->vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

__attribute__((visibility("default"))) void vkCmdDraw(VkCommandBuffer commandBuffer,
                                                      uint32_t vertexCount, uint32_t instanceCount,
                                                      uint32_t firstVertex, uint32_t firstInstance)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDraw)
        g_dispatch->vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex,
                              firstInstance);
}

__attribute__((visibility("default"))) void
vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                 uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDrawIndexed)
        g_dispatch->vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex,
                                     vertexOffset, firstInstance);
}

__attribute__((visibility("default"))) void vkCmdDrawIndirect(VkCommandBuffer commandBuffer,
                                                              VkBuffer buffer, VkDeviceSize offset,
                                                              uint32_t drawCount, uint32_t stride)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDrawIndirect)
        g_dispatch->vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

__attribute__((visibility("default"))) void
vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                         uint32_t drawCount, uint32_t stride)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDrawIndexedIndirect)
        g_dispatch->vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

__attribute__((visibility("default"))) void vkCmdDispatch(VkCommandBuffer commandBuffer,
                                                          uint32_t groupCountX,
                                                          uint32_t groupCountY,
                                                          uint32_t groupCountZ)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDispatch)
        g_dispatch->vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

__attribute__((visibility("default"))) void
vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdDispatchIndirect)
        g_dispatch->vkCmdDispatchIndirect(commandBuffer, buffer, offset);
}

__attribute__((visibility("default"))) void vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                                                            VkBuffer srcBuffer, VkBuffer dstBuffer,
                                                            uint32_t regionCount,
                                                            const VkBufferCopy *pRegions)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdCopyBuffer)
        g_dispatch->vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

__attribute__((visibility("default"))) void
vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
               VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
               const VkImageCopy *pRegions)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdCopyImage)
        g_dispatch->vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage,
                                   dstImageLayout, regionCount, pRegions);
}

__attribute__((visibility("default"))) void
vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                       VkImageLayout dstImageLayout, uint32_t regionCount,
                       const VkBufferImageCopy *pRegions)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdCopyBufferToImage)
        g_dispatch->vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout,
                                           regionCount, pRegions);
}

__attribute__((visibility("default"))) void
vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                       VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
                       const VkBufferImageCopy *pRegions)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdCopyImageToBuffer)
        g_dispatch->vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer,
                                           regionCount, pRegions);
}

__attribute__((visibility("default"))) void
vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
               VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
               const VkImageBlit *pRegions, VkFilter filter)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdBlitImage)
        g_dispatch->vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage,
                                   dstImageLayout, regionCount, pRegions, filter);
}

__attribute__((visibility("default"))) void vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdPipelineBarrier)
        g_dispatch->vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                         memoryBarrierCount, pMemoryBarriers,
                                         bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                         imageMemoryBarrierCount, pImageMemoryBarriers);
}

__attribute__((visibility("default"))) void
vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdPipelineBarrier2)
        g_dispatch->vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

__attribute__((visibility("default"))) void
vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                     const VkClearColorValue *pColor, uint32_t rangeCount,
                     const VkImageSubresourceRange *pRanges)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdClearColorImage)
        g_dispatch->vkCmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount,
                                         pRanges);
}

__attribute__((visibility("default"))) void
vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                      const VkClearAttachment *pAttachments, uint32_t rectCount,
                      const VkClearRect *pRects)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdClearAttachments)
        g_dispatch->vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount,
                                          pRects);
}

__attribute__((visibility("default"))) void vkCmdFillBuffer(VkCommandBuffer commandBuffer,
                                                            VkBuffer dstBuffer,
                                                            VkDeviceSize dstOffset,
                                                            VkDeviceSize size, uint32_t data)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdFillBuffer)
        g_dispatch->vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

__attribute__((visibility("default"))) void
vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                  VkDeviceSize dataSize, const void *pData)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkCmdUpdateBuffer)
        g_dispatch->vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

// Queries
__attribute__((visibility("default"))) VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkEnumerateInstanceVersion)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkEnumerateInstanceVersion(pApiVersion);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceProperties2 *pProperties)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceProperties2)
        g_dispatch->vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceFeatures2)
        g_dispatch->vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                    VkFormatProperties *pFormatProperties)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceFormatProperties)
        g_dispatch->vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                     VkFormatProperties2 *pFormatProperties)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceFormatProperties2)
        g_dispatch->vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                     VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceMemoryProperties2)
        g_dispatch->vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}

__attribute__((visibility("default"))) void
vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                          uint32_t *pQueueFamilyPropertyCount,
                                          VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetPhysicalDeviceQueueFamilyProperties2)
        g_dispatch->vkGetPhysicalDeviceQueueFamilyProperties2(
            physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

__attribute__((visibility("default"))) void
vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetBufferMemoryRequirements2)
        g_dispatch->vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

__attribute__((visibility("default"))) void
vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue)
{
    if (ensure_init() == 0 && g_dispatch && g_dispatch->vkGetDeviceQueue2)
        g_dispatch->vkGetDeviceQueue2(device, pQueueInfo, pQueue);
}

__attribute__((visibility("default"))) VkResult vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkFlushMappedMemoryRanges)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkFlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
}

__attribute__((visibility("default"))) VkResult vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkInvalidateMappedMemoryRanges)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkInvalidateMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
}

// External FDs
__attribute__((visibility("default"))) VkResult
vkGetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR *pGetFdInfo, int *pFd)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetFenceFdKHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetFenceFdKHR(device, pGetFdInfo, pFd);
}

__attribute__((visibility("default"))) VkResult
vkImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkImportFenceFdKHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkImportFenceFdKHR(device, pImportFenceFdInfo);
}

__attribute__((visibility("default"))) VkResult
vkGetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR *pGetFdInfo, int *pFd)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkGetSemaphoreFdKHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkGetSemaphoreFdKHR(device, pGetFdInfo, pFd);
}

__attribute__((visibility("default"))) VkResult
vkImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
    if (ensure_init() != 0 || !g_dispatch || !g_dispatch->vkImportSemaphoreFdKHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_dispatch->vkImportSemaphoreFdKHR(device, pImportSemaphoreFdInfo);
}
