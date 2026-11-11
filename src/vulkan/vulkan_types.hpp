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
using SysvPfnVkGetMemoryAndroidHardwareBufferANDROID = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
             struct AHardwareBuffer **pBuffer);

// Vulkan 1.0-1.3 Shaders & Pipelines
using SysvPfnVkCreateShaderModule = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
using SysvPfnVkDestroyShaderModule = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCreatePipelineLayout = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
using SysvPfnVkDestroyPipelineLayout = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCreatePipelineCache = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache);
using SysvPfnVkDestroyPipelineCache = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetPipelineCacheData = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize, void *pData);
using SysvPfnVkCreateGraphicsPipelines = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
             const VkGraphicsPipelineCreateInfo *pCreateInfos,
             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
using SysvPfnVkCreateComputePipelines = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
             const VkComputePipelineCreateInfo *pCreateInfos,
             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
using SysvPfnVkDestroyPipeline = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);

// RenderPass, Framebuffers & Dynamic Rendering
using SysvPfnVkCreateRenderPass = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
using SysvPfnVkCreateRenderPass2 = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
using SysvPfnVkDestroyRenderPass = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCreateFramebuffer = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
using SysvPfnVkDestroyFramebuffer = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCmdBeginRenderPass = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
    VkSubpassContents contents);
using SysvPfnVkCmdEndRenderPass = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer);
using SysvPfnVkCmdNextSubpass = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                   VkSubpassContents contents);
using SysvPfnVkCmdBeginRendering = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo);
using SysvPfnVkCmdEndRendering = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer);

// Images, Views & Samplers
using SysvPfnVkCreateImage = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkImageCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkImage *pImage);
using SysvPfnVkDestroyImage = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetImageMemoryRequirements = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
using SysvPfnVkGetImageMemoryRequirements2 =
    __attribute__((sysv_abi)) void (*)(VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements);
using SysvPfnVkBindImageMemory = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
using SysvPfnVkBindImageMemory2 = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo *pBindInfos);
using SysvPfnVkBindBufferMemory2 = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo *pBindInfos);
using SysvPfnVkCreateImageView = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkImageView *pView);
using SysvPfnVkDestroyImageView = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCreateSampler = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
using SysvPfnVkDestroySampler = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);

// Descriptors & Push Constants
using SysvPfnVkCreateDescriptorSetLayout = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
using SysvPfnVkDestroyDescriptorSetLayout =
    __attribute__((sysv_abi)) void (*)(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                       const VkAllocationCallbacks *pAllocator);
using SysvPfnVkCreateDescriptorPool = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
using SysvPfnVkDestroyDescriptorPool = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkResetDescriptorPool = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);
using SysvPfnVkAllocateDescriptorSets = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
             VkDescriptorSet *pDescriptorSets);
using SysvPfnVkFreeDescriptorSets = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
             const VkDescriptorSet *pDescriptorSets);
using SysvPfnVkUpdateDescriptorSets = __attribute__((sysv_abi)) void (*)(
    VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites,
    uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies);
using SysvPfnVkCmdBindDescriptorSets = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
    uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets,
    uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);
using SysvPfnVkCmdPushConstants = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                     VkPipelineLayout layout,
                                                                     VkShaderStageFlags stageFlags,
                                                                     uint32_t offset, uint32_t size,
                                                                     const void *pValues);

// Synchronization (Fences, Semaphores, Events)
using SysvPfnVkCreateFence = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkFence *pFence);
using SysvPfnVkDestroyFence = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkResetFences = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
using SysvPfnVkGetFenceStatus = __attribute__((sysv_abi)) VkResult (*)(VkDevice device,
                                                                       VkFence fence);
using SysvPfnVkWaitForFences = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll,
             uint64_t timeout);
using SysvPfnVkCreateSemaphore = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore);
using SysvPfnVkDestroySemaphore = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetSemaphoreCounterValue = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkSemaphore semaphore, uint64_t *pValue);
using SysvPfnVkWaitSemaphores = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo, uint64_t timeout);
using SysvPfnVkSignalSemaphore = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo);
using SysvPfnVkCreateEvent = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkEventCreateInfo *pCreateInfo,
             const VkAllocationCallbacks *pAllocator, VkEvent *pEvent);
using SysvPfnVkDestroyEvent = __attribute__((sysv_abi)) void (*)(
    VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator);
using SysvPfnVkGetEventStatus = __attribute__((sysv_abi)) VkResult (*)(VkDevice device,
                                                                       VkEvent event);
using SysvPfnVkSetEvent = __attribute__((sysv_abi)) VkResult (*)(VkDevice device, VkEvent event);
using SysvPfnVkResetEvent = __attribute__((sysv_abi)) VkResult (*)(VkDevice device, VkEvent event);

// Command Buffer Recording & Actions
using SysvPfnVkResetCommandPool = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
using SysvPfnVkResetCommandBuffer = __attribute__((sysv_abi))
VkResult (*)(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
using SysvPfnVkCmdBindPipeline = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
using SysvPfnVkCmdSetViewport = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                   uint32_t firstViewport,
                                                                   uint32_t viewportCount,
                                                                   const VkViewport *pViewports);
using SysvPfnVkCmdSetScissor = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                  uint32_t firstScissor,
                                                                  uint32_t scissorCount,
                                                                  const VkRect2D *pScissors);
using SysvPfnVkCmdBindVertexBuffers = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
    const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
using SysvPfnVkCmdBindIndexBuffer = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
using SysvPfnVkCmdDraw = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                            uint32_t vertexCount,
                                                            uint32_t instanceCount,
                                                            uint32_t firstVertex,
                                                            uint32_t firstInstance);
using SysvPfnVkCmdDrawIndexed = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
    int32_t vertexOffset, uint32_t firstInstance);
using SysvPfnVkCmdDrawIndirect = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                    VkBuffer buffer,
                                                                    VkDeviceSize offset,
                                                                    uint32_t drawCount,
                                                                    uint32_t stride);
using SysvPfnVkCmdDrawIndexedIndirect =
    __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                       VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
using SysvPfnVkCmdDispatch = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                uint32_t groupCountX,
                                                                uint32_t groupCountY,
                                                                uint32_t groupCountZ);
using SysvPfnVkCmdDispatchIndirect = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
using SysvPfnVkCmdCopyBuffer = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                  VkBuffer srcBuffer,
                                                                  VkBuffer dstBuffer,
                                                                  uint32_t regionCount,
                                                                  const VkBufferCopy *pRegions);
using SysvPfnVkCmdCopyImage = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy *pRegions);
using SysvPfnVkCmdCopyBufferToImage = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
using SysvPfnVkCmdCopyImageToBuffer = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions);
using SysvPfnVkCmdBlitImage = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions,
    VkFilter filter);
using SysvPfnVkCmdPipelineBarrier = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers);
using SysvPfnVkCmdPipelineBarrier2 = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo);
using SysvPfnVkCmdClearColorImage = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
    const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
using SysvPfnVkCmdClearAttachments = __attribute__((sysv_abi)) void (*)(
    VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment *pAttachments,
    uint32_t rectCount, const VkClearRect *pRects);
using SysvPfnVkCmdFillBuffer = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                  VkBuffer dstBuffer,
                                                                  VkDeviceSize dstOffset,
                                                                  VkDeviceSize size, uint32_t data);
using SysvPfnVkCmdUpdateBuffer = __attribute__((sysv_abi)) void (*)(VkCommandBuffer commandBuffer,
                                                                    VkBuffer dstBuffer,
                                                                    VkDeviceSize dstOffset,
                                                                    VkDeviceSize dataSize,
                                                                    const void *pData);

// Vulkan 1.1-1.2 Instance & PhysicalDevice Queries
using SysvPfnVkEnumerateInstanceVersion = __attribute__((sysv_abi))
VkResult (*)(uint32_t *pApiVersion);
using SysvPfnVkGetPhysicalDeviceProperties2 = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties);
using SysvPfnVkGetPhysicalDeviceFeatures2 = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 *pFeatures);
using SysvPfnVkGetPhysicalDeviceFormatProperties = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties);
using SysvPfnVkGetPhysicalDeviceFormatProperties2 = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2 *pFormatProperties);
using SysvPfnVkGetPhysicalDeviceMemoryProperties2 = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
using SysvPfnVkGetPhysicalDeviceQueueFamilyProperties2 = __attribute__((sysv_abi)) void (*)(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties);
using SysvPfnVkGetBufferMemoryRequirements2 = __attribute__((sysv_abi)) void (*)(
    VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements);
using SysvPfnVkGetDeviceQueue2 = __attribute__((sysv_abi)) void (*)(
    VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue);
using SysvPfnVkFlushMappedMemoryRanges = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);
using SysvPfnVkInvalidateMappedMemoryRanges = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange *pMemoryRanges);

// External Synchronization FDs
using SysvPfnVkGetFenceFdKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkFenceGetFdInfoKHR *pGetFdInfo, int *pFd);
using SysvPfnVkImportFenceFdKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkImportFenceFdInfoKHR *pImportFenceFdInfo);
using SysvPfnVkGetSemaphoreFdKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkSemaphoreGetFdInfoKHR *pGetFdInfo, int *pFd);
using SysvPfnVkImportSemaphoreFdKHR = __attribute__((sysv_abi))
VkResult (*)(VkDevice device, const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo);

struct VulkanDispatchTable {
    uint32_t version{3};
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
    SysvPfnVkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID{nullptr};

    // Shaders & Pipelines
    SysvPfnVkCreateShaderModule vkCreateShaderModule{nullptr};
    SysvPfnVkDestroyShaderModule vkDestroyShaderModule{nullptr};
    SysvPfnVkCreatePipelineLayout vkCreatePipelineLayout{nullptr};
    SysvPfnVkDestroyPipelineLayout vkDestroyPipelineLayout{nullptr};
    SysvPfnVkCreatePipelineCache vkCreatePipelineCache{nullptr};
    SysvPfnVkDestroyPipelineCache vkDestroyPipelineCache{nullptr};
    SysvPfnVkGetPipelineCacheData vkGetPipelineCacheData{nullptr};
    SysvPfnVkCreateGraphicsPipelines vkCreateGraphicsPipelines{nullptr};
    SysvPfnVkCreateComputePipelines vkCreateComputePipelines{nullptr};
    SysvPfnVkDestroyPipeline vkDestroyPipeline{nullptr};

    // RenderPass, Framebuffers & Dynamic Rendering
    SysvPfnVkCreateRenderPass vkCreateRenderPass{nullptr};
    SysvPfnVkCreateRenderPass2 vkCreateRenderPass2{nullptr};
    SysvPfnVkDestroyRenderPass vkDestroyRenderPass{nullptr};
    SysvPfnVkCreateFramebuffer vkCreateFramebuffer{nullptr};
    SysvPfnVkDestroyFramebuffer vkDestroyFramebuffer{nullptr};
    SysvPfnVkCmdBeginRenderPass vkCmdBeginRenderPass{nullptr};
    SysvPfnVkCmdEndRenderPass vkCmdEndRenderPass{nullptr};
    SysvPfnVkCmdNextSubpass vkCmdNextSubpass{nullptr};
    SysvPfnVkCmdBeginRendering vkCmdBeginRendering{nullptr};
    SysvPfnVkCmdEndRendering vkCmdEndRendering{nullptr};

    // Images, Views & Samplers
    SysvPfnVkCreateImage vkCreateImage{nullptr};
    SysvPfnVkDestroyImage vkDestroyImage{nullptr};
    SysvPfnVkGetImageMemoryRequirements vkGetImageMemoryRequirements{nullptr};
    SysvPfnVkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2{nullptr};
    SysvPfnVkBindImageMemory vkBindImageMemory{nullptr};
    SysvPfnVkBindImageMemory2 vkBindImageMemory2{nullptr};
    SysvPfnVkBindBufferMemory2 vkBindBufferMemory2{nullptr};
    SysvPfnVkCreateImageView vkCreateImageView{nullptr};
    SysvPfnVkDestroyImageView vkDestroyImageView{nullptr};
    SysvPfnVkCreateSampler vkCreateSampler{nullptr};
    SysvPfnVkDestroySampler vkDestroySampler{nullptr};

    // Descriptors & Push Constants
    SysvPfnVkCreateDescriptorSetLayout vkCreateDescriptorSetLayout{nullptr};
    SysvPfnVkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout{nullptr};
    SysvPfnVkCreateDescriptorPool vkCreateDescriptorPool{nullptr};
    SysvPfnVkDestroyDescriptorPool vkDestroyDescriptorPool{nullptr};
    SysvPfnVkResetDescriptorPool vkResetDescriptorPool{nullptr};
    SysvPfnVkAllocateDescriptorSets vkAllocateDescriptorSets{nullptr};
    SysvPfnVkFreeDescriptorSets vkFreeDescriptorSets{nullptr};
    SysvPfnVkUpdateDescriptorSets vkUpdateDescriptorSets{nullptr};
    SysvPfnVkCmdBindDescriptorSets vkCmdBindDescriptorSets{nullptr};
    SysvPfnVkCmdPushConstants vkCmdPushConstants{nullptr};

    // Synchronization
    SysvPfnVkCreateFence vkCreateFence{nullptr};
    SysvPfnVkDestroyFence vkDestroyFence{nullptr};
    SysvPfnVkResetFences vkResetFences{nullptr};
    SysvPfnVkGetFenceStatus vkGetFenceStatus{nullptr};
    SysvPfnVkWaitForFences vkWaitForFences{nullptr};
    SysvPfnVkCreateSemaphore vkCreateSemaphore{nullptr};
    SysvPfnVkDestroySemaphore vkDestroySemaphore{nullptr};
    SysvPfnVkGetSemaphoreCounterValue vkGetSemaphoreCounterValue{nullptr};
    SysvPfnVkWaitSemaphores vkWaitSemaphores{nullptr};
    SysvPfnVkSignalSemaphore vkSignalSemaphore{nullptr};
    SysvPfnVkCreateEvent vkCreateEvent{nullptr};
    SysvPfnVkDestroyEvent vkDestroyEvent{nullptr};
    SysvPfnVkGetEventStatus vkGetEventStatus{nullptr};
    SysvPfnVkSetEvent vkSetEvent{nullptr};
    SysvPfnVkResetEvent vkResetEvent{nullptr};

    // Command Buffer Recording
    SysvPfnVkResetCommandPool vkResetCommandPool{nullptr};
    SysvPfnVkResetCommandBuffer vkResetCommandBuffer{nullptr};
    SysvPfnVkCmdBindPipeline vkCmdBindPipeline{nullptr};
    SysvPfnVkCmdSetViewport vkCmdSetViewport{nullptr};
    SysvPfnVkCmdSetScissor vkCmdSetScissor{nullptr};
    SysvPfnVkCmdBindVertexBuffers vkCmdBindVertexBuffers{nullptr};
    SysvPfnVkCmdBindIndexBuffer vkCmdBindIndexBuffer{nullptr};
    SysvPfnVkCmdDraw vkCmdDraw{nullptr};
    SysvPfnVkCmdDrawIndexed vkCmdDrawIndexed{nullptr};
    SysvPfnVkCmdDrawIndirect vkCmdDrawIndirect{nullptr};
    SysvPfnVkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect{nullptr};
    SysvPfnVkCmdDispatch vkCmdDispatch{nullptr};
    SysvPfnVkCmdDispatchIndirect vkCmdDispatchIndirect{nullptr};
    SysvPfnVkCmdCopyBuffer vkCmdCopyBuffer{nullptr};
    SysvPfnVkCmdCopyImage vkCmdCopyImage{nullptr};
    SysvPfnVkCmdCopyBufferToImage vkCmdCopyBufferToImage{nullptr};
    SysvPfnVkCmdCopyImageToBuffer vkCmdCopyImageToBuffer{nullptr};
    SysvPfnVkCmdBlitImage vkCmdBlitImage{nullptr};
    SysvPfnVkCmdPipelineBarrier vkCmdPipelineBarrier{nullptr};
    SysvPfnVkCmdPipelineBarrier2 vkCmdPipelineBarrier2{nullptr};
    SysvPfnVkCmdClearColorImage vkCmdClearColorImage{nullptr};
    SysvPfnVkCmdClearAttachments vkCmdClearAttachments{nullptr};
    SysvPfnVkCmdFillBuffer vkCmdFillBuffer{nullptr};
    SysvPfnVkCmdUpdateBuffer vkCmdUpdateBuffer{nullptr};

    // Instance & PhysicalDevice Queries
    SysvPfnVkEnumerateInstanceVersion vkEnumerateInstanceVersion{nullptr};
    SysvPfnVkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2{nullptr};
    SysvPfnVkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2{nullptr};
    SysvPfnVkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties{nullptr};
    SysvPfnVkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2{nullptr};
    SysvPfnVkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2{nullptr};
    SysvPfnVkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2{
        nullptr};
    SysvPfnVkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2{nullptr};
    SysvPfnVkGetDeviceQueue2 vkGetDeviceQueue2{nullptr};
    SysvPfnVkFlushMappedMemoryRanges vkFlushMappedMemoryRanges{nullptr};
    SysvPfnVkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges{nullptr};

    // External FDs
    SysvPfnVkGetFenceFdKHR vkGetFenceFdKHR{nullptr};
    SysvPfnVkImportFenceFdKHR vkImportFenceFdKHR{nullptr};
    SysvPfnVkGetSemaphoreFdKHR vkGetSemaphoreFdKHR{nullptr};
    SysvPfnVkImportSemaphoreFdKHR vkImportSemaphoreFdKHR{nullptr};
};

} // namespace portage::vulkan
