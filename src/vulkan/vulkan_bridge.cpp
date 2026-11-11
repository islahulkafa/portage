#include "vulkan_bridge.hpp"
#include "fs/fd_table.hpp"
#include "ndk/hardware_buffer.hpp"
#include "syscall/syscall_dispatcher.hpp"
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

// Shaders & Pipelines
PFN_vkCreateShaderModule g_host_vkCreateShaderModule{nullptr};
PFN_vkDestroyShaderModule g_host_vkDestroyShaderModule{nullptr};
PFN_vkCreatePipelineLayout g_host_vkCreatePipelineLayout{nullptr};
PFN_vkDestroyPipelineLayout g_host_vkDestroyPipelineLayout{nullptr};
PFN_vkCreatePipelineCache g_host_vkCreatePipelineCache{nullptr};
PFN_vkDestroyPipelineCache g_host_vkDestroyPipelineCache{nullptr};
PFN_vkGetPipelineCacheData g_host_vkGetPipelineCacheData{nullptr};
PFN_vkCreateGraphicsPipelines g_host_vkCreateGraphicsPipelines{nullptr};
PFN_vkCreateComputePipelines g_host_vkCreateComputePipelines{nullptr};
PFN_vkDestroyPipeline g_host_vkDestroyPipeline{nullptr};

// RenderPass, Framebuffers & Dynamic Rendering
PFN_vkCreateRenderPass g_host_vkCreateRenderPass{nullptr};
PFN_vkCreateRenderPass2 g_host_vkCreateRenderPass2{nullptr};
PFN_vkDestroyRenderPass g_host_vkDestroyRenderPass{nullptr};
PFN_vkCreateFramebuffer g_host_vkCreateFramebuffer{nullptr};
PFN_vkDestroyFramebuffer g_host_vkDestroyFramebuffer{nullptr};
PFN_vkCmdBeginRenderPass g_host_vkCmdBeginRenderPass{nullptr};
PFN_vkCmdEndRenderPass g_host_vkCmdEndRenderPass{nullptr};
PFN_vkCmdNextSubpass g_host_vkCmdNextSubpass{nullptr};
PFN_vkCmdBeginRendering g_host_vkCmdBeginRendering{nullptr};
PFN_vkCmdEndRendering g_host_vkCmdEndRendering{nullptr};

// Images, Views & Samplers
PFN_vkCreateImage g_host_vkCreateImage{nullptr};
PFN_vkDestroyImage g_host_vkDestroyImage{nullptr};
PFN_vkGetImageMemoryRequirements g_host_vkGetImageMemoryRequirements{nullptr};
PFN_vkGetImageMemoryRequirements2 g_host_vkGetImageMemoryRequirements2{nullptr};
PFN_vkBindImageMemory g_host_vkBindImageMemory{nullptr};
PFN_vkBindImageMemory2 g_host_vkBindImageMemory2{nullptr};
PFN_vkBindBufferMemory2 g_host_vkBindBufferMemory2{nullptr};
PFN_vkCreateImageView g_host_vkCreateImageView{nullptr};
PFN_vkDestroyImageView g_host_vkDestroyImageView{nullptr};
PFN_vkCreateSampler g_host_vkCreateSampler{nullptr};
PFN_vkDestroySampler g_host_vkDestroySampler{nullptr};

// Descriptors & Push Constants
PFN_vkCreateDescriptorSetLayout g_host_vkCreateDescriptorSetLayout{nullptr};
PFN_vkDestroyDescriptorSetLayout g_host_vkDestroyDescriptorSetLayout{nullptr};
PFN_vkCreateDescriptorPool g_host_vkCreateDescriptorPool{nullptr};
PFN_vkDestroyDescriptorPool g_host_vkDestroyDescriptorPool{nullptr};
PFN_vkResetDescriptorPool g_host_vkResetDescriptorPool{nullptr};
PFN_vkAllocateDescriptorSets g_host_vkAllocateDescriptorSets{nullptr};
PFN_vkFreeDescriptorSets g_host_vkFreeDescriptorSets{nullptr};
PFN_vkUpdateDescriptorSets g_host_vkUpdateDescriptorSets{nullptr};
PFN_vkCmdBindDescriptorSets g_host_vkCmdBindDescriptorSets{nullptr};
PFN_vkCmdPushConstants g_host_vkCmdPushConstants{nullptr};

// Synchronization
PFN_vkCreateFence g_host_vkCreateFence{nullptr};
PFN_vkDestroyFence g_host_vkDestroyFence{nullptr};
PFN_vkResetFences g_host_vkResetFences{nullptr};
PFN_vkGetFenceStatus g_host_vkGetFenceStatus{nullptr};
PFN_vkWaitForFences g_host_vkWaitForFences{nullptr};
PFN_vkCreateSemaphore g_host_vkCreateSemaphore{nullptr};
PFN_vkDestroySemaphore g_host_vkDestroySemaphore{nullptr};
PFN_vkGetSemaphoreCounterValue g_host_vkGetSemaphoreCounterValue{nullptr};
PFN_vkWaitSemaphores g_host_vkWaitSemaphores{nullptr};
PFN_vkSignalSemaphore g_host_vkSignalSemaphore{nullptr};
PFN_vkCreateEvent g_host_vkCreateEvent{nullptr};
PFN_vkDestroyEvent g_host_vkDestroyEvent{nullptr};
PFN_vkGetEventStatus g_host_vkGetEventStatus{nullptr};
PFN_vkSetEvent g_host_vkSetEvent{nullptr};
PFN_vkResetEvent g_host_vkResetEvent{nullptr};

// Command Buffer Actions
PFN_vkResetCommandPool g_host_vkResetCommandPool{nullptr};
PFN_vkResetCommandBuffer g_host_vkResetCommandBuffer{nullptr};
PFN_vkCmdBindPipeline g_host_vkCmdBindPipeline{nullptr};
PFN_vkCmdSetViewport g_host_vkCmdSetViewport{nullptr};
PFN_vkCmdSetScissor g_host_vkCmdSetScissor{nullptr};
PFN_vkCmdBindVertexBuffers g_host_vkCmdBindVertexBuffers{nullptr};
PFN_vkCmdBindIndexBuffer g_host_vkCmdBindIndexBuffer{nullptr};
PFN_vkCmdDraw g_host_vkCmdDraw{nullptr};
PFN_vkCmdDrawIndexed g_host_vkCmdDrawIndexed{nullptr};
PFN_vkCmdDrawIndirect g_host_vkCmdDrawIndirect{nullptr};
PFN_vkCmdDrawIndexedIndirect g_host_vkCmdDrawIndexedIndirect{nullptr};
PFN_vkCmdDispatch g_host_vkCmdDispatch{nullptr};
PFN_vkCmdDispatchIndirect g_host_vkCmdDispatchIndirect{nullptr};
PFN_vkCmdCopyBuffer g_host_vkCmdCopyBuffer{nullptr};
PFN_vkCmdCopyImage g_host_vkCmdCopyImage{nullptr};
PFN_vkCmdCopyBufferToImage g_host_vkCmdCopyBufferToImage{nullptr};
PFN_vkCmdCopyImageToBuffer g_host_vkCmdCopyImageToBuffer{nullptr};
PFN_vkCmdBlitImage g_host_vkCmdBlitImage{nullptr};
PFN_vkCmdPipelineBarrier g_host_vkCmdPipelineBarrier{nullptr};
PFN_vkCmdPipelineBarrier2 g_host_vkCmdPipelineBarrier2{nullptr};
PFN_vkCmdClearColorImage g_host_vkCmdClearColorImage{nullptr};
PFN_vkCmdClearAttachments g_host_vkCmdClearAttachments{nullptr};
PFN_vkCmdFillBuffer g_host_vkCmdFillBuffer{nullptr};
PFN_vkCmdUpdateBuffer g_host_vkCmdUpdateBuffer{nullptr};

// Instance & Physical Device Queries
PFN_vkEnumerateInstanceVersion g_host_vkEnumerateInstanceVersion{nullptr};
PFN_vkGetPhysicalDeviceProperties2 g_host_vkGetPhysicalDeviceProperties2{nullptr};
PFN_vkGetPhysicalDeviceFeatures2 g_host_vkGetPhysicalDeviceFeatures2{nullptr};
PFN_vkGetPhysicalDeviceFormatProperties g_host_vkGetPhysicalDeviceFormatProperties{nullptr};
PFN_vkGetPhysicalDeviceFormatProperties2 g_host_vkGetPhysicalDeviceFormatProperties2{nullptr};
PFN_vkGetPhysicalDeviceMemoryProperties2 g_host_vkGetPhysicalDeviceMemoryProperties2{nullptr};
PFN_vkGetPhysicalDeviceQueueFamilyProperties2 g_host_vkGetPhysicalDeviceQueueFamilyProperties2{nullptr};
PFN_vkGetBufferMemoryRequirements2 g_host_vkGetBufferMemoryRequirements2{nullptr};
PFN_vkGetDeviceQueue2 g_host_vkGetDeviceQueue2{nullptr};
PFN_vkFlushMappedMemoryRanges g_host_vkFlushMappedMemoryRanges{nullptr};
PFN_vkInvalidateMappedMemoryRanges g_host_vkInvalidateMappedMemoryRanges{nullptr};

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

std::mutex g_ahb_memory_mutex{};
std::unordered_map<VkDeviceMemory, struct AHardwareBuffer *> g_imported_ahb_buffers{};

std::mutex g_sync_fence_mutex{};
std::unordered_map<VkFence, std::shared_ptr<fs::SyncFenceState>> g_fence_sync_states{};
std::unordered_map<VkSemaphore, std::shared_ptr<fs::SyncFenceState>> g_semaphore_sync_states{};

template <typename T> T LoadProc(HMODULE mod, const char *name)
{
    return reinterpret_cast<T>(GetProcAddress(mod, name));
}

template <typename T> T ResolveInstanceProc(VkInstance instance, const char *name)
{
    if (g_host_vkGetInstanceProcAddr && instance) {
        auto pfn = reinterpret_cast<T>(g_host_vkGetInstanceProcAddr(instance, name));
        if (pfn)
            return pfn;
    }
    if (VulkanBridge::Instance().GetModule()) {
        return LoadProc<T>(static_cast<HMODULE>(VulkanBridge::Instance().GetModule()), name);
    }
    return nullptr;
}

template <typename T> T ResolveDeviceProc(VkDevice device, const char *name)
{
    if (g_host_vkGetDeviceProcAddr && device) {
        auto pfn = reinterpret_cast<T>(g_host_vkGetDeviceProcAddr(device, name));
        if (pfn)
            return pfn;
    }
    return ResolveInstanceProc<T>(VK_NULL_HANDLE, name);
}

fs::FileDescriptorTable *GetActiveFdTable()
{
    auto *table = VulkanBridge::Instance().GetFdTable();
    if (table)
        return table;
    return syscall::SyscallDispatcher::GetFdTable();
}

void ResolveInstanceFunctions(VkInstance instance)
{
    g_host_vkGetPhysicalDeviceProperties2 =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceProperties2>(instance,
                                                                "vkGetPhysicalDeviceProperties2");
    g_host_vkGetPhysicalDeviceFeatures2 =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceFeatures2>(instance,
                                                              "vkGetPhysicalDeviceFeatures2");
    g_host_vkGetPhysicalDeviceFormatProperties =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceFormatProperties>(
            instance, "vkGetPhysicalDeviceFormatProperties");
    g_host_vkGetPhysicalDeviceFormatProperties2 =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceFormatProperties2>(
            instance, "vkGetPhysicalDeviceFormatProperties2");
    g_host_vkGetPhysicalDeviceMemoryProperties2 =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties2>(
            instance, "vkGetPhysicalDeviceMemoryProperties2");
    g_host_vkGetPhysicalDeviceQueueFamilyProperties2 =
        ResolveInstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties2>(
            instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
}

void ResolveDeviceFunctions(VkDevice device)
{
    // Shaders & Pipelines
    g_host_vkCreateShaderModule =
        ResolveDeviceProc<PFN_vkCreateShaderModule>(device, "vkCreateShaderModule");
    g_host_vkDestroyShaderModule =
        ResolveDeviceProc<PFN_vkDestroyShaderModule>(device, "vkDestroyShaderModule");
    g_host_vkCreatePipelineLayout =
        ResolveDeviceProc<PFN_vkCreatePipelineLayout>(device, "vkCreatePipelineLayout");
    g_host_vkDestroyPipelineLayout =
        ResolveDeviceProc<PFN_vkDestroyPipelineLayout>(device, "vkDestroyPipelineLayout");
    g_host_vkCreatePipelineCache =
        ResolveDeviceProc<PFN_vkCreatePipelineCache>(device, "vkCreatePipelineCache");
    g_host_vkDestroyPipelineCache =
        ResolveDeviceProc<PFN_vkDestroyPipelineCache>(device, "vkDestroyPipelineCache");
    g_host_vkGetPipelineCacheData =
        ResolveDeviceProc<PFN_vkGetPipelineCacheData>(device, "vkGetPipelineCacheData");
    g_host_vkCreateGraphicsPipelines =
        ResolveDeviceProc<PFN_vkCreateGraphicsPipelines>(device, "vkCreateGraphicsPipelines");
    g_host_vkCreateComputePipelines =
        ResolveDeviceProc<PFN_vkCreateComputePipelines>(device, "vkCreateComputePipelines");
    g_host_vkDestroyPipeline =
        ResolveDeviceProc<PFN_vkDestroyPipeline>(device, "vkDestroyPipeline");

    // RenderPass, Framebuffers & Dynamic Rendering
    g_host_vkCreateRenderPass =
        ResolveDeviceProc<PFN_vkCreateRenderPass>(device, "vkCreateRenderPass");
    g_host_vkCreateRenderPass2 =
        ResolveDeviceProc<PFN_vkCreateRenderPass2>(device, "vkCreateRenderPass2");
    g_host_vkDestroyRenderPass =
        ResolveDeviceProc<PFN_vkDestroyRenderPass>(device, "vkDestroyRenderPass");
    g_host_vkCreateFramebuffer =
        ResolveDeviceProc<PFN_vkCreateFramebuffer>(device, "vkCreateFramebuffer");
    g_host_vkDestroyFramebuffer =
        ResolveDeviceProc<PFN_vkDestroyFramebuffer>(device, "vkDestroyFramebuffer");
    g_host_vkCmdBeginRenderPass =
        ResolveDeviceProc<PFN_vkCmdBeginRenderPass>(device, "vkCmdBeginRenderPass");
    g_host_vkCmdEndRenderPass =
        ResolveDeviceProc<PFN_vkCmdEndRenderPass>(device, "vkCmdEndRenderPass");
    g_host_vkCmdNextSubpass =
        ResolveDeviceProc<PFN_vkCmdNextSubpass>(device, "vkCmdNextSubpass");
    g_host_vkCmdBeginRendering =
        ResolveDeviceProc<PFN_vkCmdBeginRendering>(device, "vkCmdBeginRendering");
    if (!g_host_vkCmdBeginRendering) {
        g_host_vkCmdBeginRendering =
            ResolveDeviceProc<PFN_vkCmdBeginRendering>(device, "vkCmdBeginRenderingKHR");
    }
    g_host_vkCmdEndRendering =
        ResolveDeviceProc<PFN_vkCmdEndRendering>(device, "vkCmdEndRendering");
    if (!g_host_vkCmdEndRendering) {
        g_host_vkCmdEndRendering =
            ResolveDeviceProc<PFN_vkCmdEndRendering>(device, "vkCmdEndRenderingKHR");
    }

    // Images, Views & Samplers
    g_host_vkCreateImage = ResolveDeviceProc<PFN_vkCreateImage>(device, "vkCreateImage");
    g_host_vkDestroyImage = ResolveDeviceProc<PFN_vkDestroyImage>(device, "vkDestroyImage");
    g_host_vkGetImageMemoryRequirements =
        ResolveDeviceProc<PFN_vkGetImageMemoryRequirements>(device,
                                                            "vkGetImageMemoryRequirements");
    g_host_vkGetImageMemoryRequirements2 =
        ResolveDeviceProc<PFN_vkGetImageMemoryRequirements2>(device,
                                                             "vkGetImageMemoryRequirements2");
    g_host_vkBindImageMemory =
        ResolveDeviceProc<PFN_vkBindImageMemory>(device, "vkBindImageMemory");
    g_host_vkBindImageMemory2 =
        ResolveDeviceProc<PFN_vkBindImageMemory2>(device, "vkBindImageMemory2");
    g_host_vkBindBufferMemory2 =
        ResolveDeviceProc<PFN_vkBindBufferMemory2>(device, "vkBindBufferMemory2");
    g_host_vkCreateImageView =
        ResolveDeviceProc<PFN_vkCreateImageView>(device, "vkCreateImageView");
    g_host_vkDestroyImageView =
        ResolveDeviceProc<PFN_vkDestroyImageView>(device, "vkDestroyImageView");
    g_host_vkCreateSampler = ResolveDeviceProc<PFN_vkCreateSampler>(device, "vkCreateSampler");
    g_host_vkDestroySampler = ResolveDeviceProc<PFN_vkDestroySampler>(device, "vkDestroySampler");

    // Descriptors & Push Constants
    g_host_vkCreateDescriptorSetLayout =
        ResolveDeviceProc<PFN_vkCreateDescriptorSetLayout>(device,
                                                           "vkCreateDescriptorSetLayout");
    g_host_vkDestroyDescriptorSetLayout =
        ResolveDeviceProc<PFN_vkDestroyDescriptorSetLayout>(device,
                                                            "vkDestroyDescriptorSetLayout");
    g_host_vkCreateDescriptorPool =
        ResolveDeviceProc<PFN_vkCreateDescriptorPool>(device, "vkCreateDescriptorPool");
    g_host_vkDestroyDescriptorPool =
        ResolveDeviceProc<PFN_vkDestroyDescriptorPool>(device, "vkDestroyDescriptorPool");
    g_host_vkResetDescriptorPool =
        ResolveDeviceProc<PFN_vkResetDescriptorPool>(device, "vkResetDescriptorPool");
    g_host_vkAllocateDescriptorSets =
        ResolveDeviceProc<PFN_vkAllocateDescriptorSets>(device, "vkAllocateDescriptorSets");
    g_host_vkFreeDescriptorSets =
        ResolveDeviceProc<PFN_vkFreeDescriptorSets>(device, "vkFreeDescriptorSets");
    g_host_vkUpdateDescriptorSets =
        ResolveDeviceProc<PFN_vkUpdateDescriptorSets>(device, "vkUpdateDescriptorSets");
    g_host_vkCmdBindDescriptorSets =
        ResolveDeviceProc<PFN_vkCmdBindDescriptorSets>(device, "vkCmdBindDescriptorSets");
    g_host_vkCmdPushConstants =
        ResolveDeviceProc<PFN_vkCmdPushConstants>(device, "vkCmdPushConstants");

    // Synchronization
    g_host_vkCreateFence = ResolveDeviceProc<PFN_vkCreateFence>(device, "vkCreateFence");
    g_host_vkDestroyFence = ResolveDeviceProc<PFN_vkDestroyFence>(device, "vkDestroyFence");
    g_host_vkResetFences = ResolveDeviceProc<PFN_vkResetFences>(device, "vkResetFences");
    g_host_vkGetFenceStatus = ResolveDeviceProc<PFN_vkGetFenceStatus>(device, "vkGetFenceStatus");
    g_host_vkWaitForFences = ResolveDeviceProc<PFN_vkWaitForFences>(device, "vkWaitForFences");
    g_host_vkCreateSemaphore =
        ResolveDeviceProc<PFN_vkCreateSemaphore>(device, "vkCreateSemaphore");
    g_host_vkDestroySemaphore =
        ResolveDeviceProc<PFN_vkDestroySemaphore>(device, "vkDestroySemaphore");
    g_host_vkGetSemaphoreCounterValue =
        ResolveDeviceProc<PFN_vkGetSemaphoreCounterValue>(device,
                                                          "vkGetSemaphoreCounterValue");
    g_host_vkWaitSemaphores =
        ResolveDeviceProc<PFN_vkWaitSemaphores>(device, "vkWaitSemaphores");
    g_host_vkSignalSemaphore =
        ResolveDeviceProc<PFN_vkSignalSemaphore>(device, "vkSignalSemaphore");
    g_host_vkCreateEvent = ResolveDeviceProc<PFN_vkCreateEvent>(device, "vkCreateEvent");
    g_host_vkDestroyEvent = ResolveDeviceProc<PFN_vkDestroyEvent>(device, "vkDestroyEvent");
    g_host_vkGetEventStatus = ResolveDeviceProc<PFN_vkGetEventStatus>(device, "vkGetEventStatus");
    g_host_vkSetEvent = ResolveDeviceProc<PFN_vkSetEvent>(device, "vkSetEvent");
    g_host_vkResetEvent = ResolveDeviceProc<PFN_vkResetEvent>(device, "vkResetEvent");

    // Command Buffer Recording & Actions
    g_host_vkResetCommandPool =
        ResolveDeviceProc<PFN_vkResetCommandPool>(device, "vkResetCommandPool");
    g_host_vkResetCommandBuffer =
        ResolveDeviceProc<PFN_vkResetCommandBuffer>(device, "vkResetCommandBuffer");
    g_host_vkCmdBindPipeline =
        ResolveDeviceProc<PFN_vkCmdBindPipeline>(device, "vkCmdBindPipeline");
    g_host_vkCmdSetViewport =
        ResolveDeviceProc<PFN_vkCmdSetViewport>(device, "vkCmdSetViewport");
    g_host_vkCmdSetScissor = ResolveDeviceProc<PFN_vkCmdSetScissor>(device, "vkCmdSetScissor");
    g_host_vkCmdBindVertexBuffers =
        ResolveDeviceProc<PFN_vkCmdBindVertexBuffers>(device, "vkCmdBindVertexBuffers");
    g_host_vkCmdBindIndexBuffer =
        ResolveDeviceProc<PFN_vkCmdBindIndexBuffer>(device, "vkCmdBindIndexBuffer");
    g_host_vkCmdDraw = ResolveDeviceProc<PFN_vkCmdDraw>(device, "vkCmdDraw");
    g_host_vkCmdDrawIndexed =
        ResolveDeviceProc<PFN_vkCmdDrawIndexed>(device, "vkCmdDrawIndexed");
    g_host_vkCmdDrawIndirect =
        ResolveDeviceProc<PFN_vkCmdDrawIndirect>(device, "vkCmdDrawIndirect");
    g_host_vkCmdDrawIndexedIndirect =
        ResolveDeviceProc<PFN_vkCmdDrawIndexedIndirect>(device, "vkCmdDrawIndexedIndirect");
    g_host_vkCmdDispatch = ResolveDeviceProc<PFN_vkCmdDispatch>(device, "vkCmdDispatch");
    g_host_vkCmdDispatchIndirect =
        ResolveDeviceProc<PFN_vkCmdDispatchIndirect>(device, "vkCmdDispatchIndirect");
    g_host_vkCmdCopyBuffer = ResolveDeviceProc<PFN_vkCmdCopyBuffer>(device, "vkCmdCopyBuffer");
    g_host_vkCmdCopyImage = ResolveDeviceProc<PFN_vkCmdCopyImage>(device, "vkCmdCopyImage");
    g_host_vkCmdCopyBufferToImage =
        ResolveDeviceProc<PFN_vkCmdCopyBufferToImage>(device, "vkCmdCopyBufferToImage");
    g_host_vkCmdCopyImageToBuffer =
        ResolveDeviceProc<PFN_vkCmdCopyImageToBuffer>(device, "vkCmdCopyImageToBuffer");
    g_host_vkCmdBlitImage = ResolveDeviceProc<PFN_vkCmdBlitImage>(device, "vkCmdBlitImage");
    g_host_vkCmdPipelineBarrier =
        ResolveDeviceProc<PFN_vkCmdPipelineBarrier>(device, "vkCmdPipelineBarrier");
    g_host_vkCmdPipelineBarrier2 =
        ResolveDeviceProc<PFN_vkCmdPipelineBarrier2>(device, "vkCmdPipelineBarrier2");
    g_host_vkCmdClearColorImage =
        ResolveDeviceProc<PFN_vkCmdClearColorImage>(device, "vkCmdClearColorImage");
    g_host_vkCmdClearAttachments =
        ResolveDeviceProc<PFN_vkCmdClearAttachments>(device, "vkCmdClearAttachments");
    g_host_vkCmdFillBuffer = ResolveDeviceProc<PFN_vkCmdFillBuffer>(device, "vkCmdFillBuffer");
    g_host_vkCmdUpdateBuffer =
        ResolveDeviceProc<PFN_vkCmdUpdateBuffer>(device, "vkCmdUpdateBuffer");

    // Device-level queries
    g_host_vkGetBufferMemoryRequirements2 =
        ResolveDeviceProc<PFN_vkGetBufferMemoryRequirements2>(device,
                                                             "vkGetBufferMemoryRequirements2");
    g_host_vkGetDeviceQueue2 =
        ResolveDeviceProc<PFN_vkGetDeviceQueue2>(device, "vkGetDeviceQueue2");
    g_host_vkFlushMappedMemoryRanges =
        ResolveDeviceProc<PFN_vkFlushMappedMemoryRanges>(device, "vkFlushMappedMemoryRanges");
    g_host_vkInvalidateMappedMemoryRanges =
        ResolveDeviceProc<PFN_vkInvalidateMappedMemoryRanges>(device,
                                                             "vkInvalidateMappedMemoryRanges");
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
    if (res == VK_SUCCESS && pInstance && *pInstance) {
        ResolveInstanceFunctions(*pInstance);
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
            if (!ext)
                continue;
            if (std::strcmp(ext, "VK_ANDROID_external_memory_android_hardware_buffer") == 0 ||
                std::strcmp(ext, "VK_ANDROID_native_buffer") == 0 ||
                std::strcmp(ext, "VK_KHR_external_memory_fd") == 0 ||
                std::strcmp(ext, "VK_KHR_external_semaphore_fd") == 0 ||
                std::strcmp(ext, "VK_KHR_external_fence_fd") == 0) {
                continue;
            }
            dev_exts.push_back(ext);
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
    if (res == VK_SUCCESS && pDevice && *pDevice) {
        ResolveDeviceFunctions(*pDevice);
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
    if (!g_host_vkAllocateMemory || !pAllocateInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    const struct AHardwareBuffer *imported_ahb = nullptr;
    const auto *curr = reinterpret_cast<const VkBaseInStructure *>(pAllocateInfo->pNext);
    while (curr) {
        if (curr->sType == static_cast<VkStructureType>(1000129000)) {
            auto import_info =
                reinterpret_cast<const VkImportAndroidHardwareBufferInfoANDROID *>(curr);
            imported_ahb = import_info->buffer;
            break;
        }
        curr = curr->pNext;
    }

    VkMemoryAllocateInfo sanitized_info = *pAllocateInfo;
    if (imported_ahb) {
        sanitized_info.pNext = nullptr;
    }

    VkResult res = g_host_vkAllocateMemory(device, &sanitized_info, pAllocator, pMemory);
    if (res == VK_SUCCESS && imported_ahb && pMemory) {
        std::lock_guard<std::mutex> lock(g_ahb_memory_mutex);
        g_imported_ahb_buffers[*pMemory] = const_cast<struct AHardwareBuffer *>(imported_ahb);
    }
    return res;
}

__attribute__((sysv_abi)) void Sysv_vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                                                 const VkAllocationCallbacks *pAllocator)
{
    {
        std::lock_guard<std::mutex> lock(g_ahb_memory_mutex);
        g_imported_ahb_buffers.erase(memory);
    }
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
    if (!pPropertyCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    static const VkExtensionProperties kAndroidExts[] = {
        {"VK_ANDROID_external_memory_android_hardware_buffer", 3},
        {"VK_KHR_external_memory_fd", 1},
        {"VK_KHR_external_semaphore_fd", 1},
        {"VK_KHR_external_fence_fd", 1},
    };
    constexpr uint32_t kExtraCount = static_cast<uint32_t>(sizeof(kAndroidExts) / sizeof(kAndroidExts[0]));

    if (!pProperties) {
        uint32_t host_count = 0;
        VkResult res = VK_SUCCESS;
        if (g_host_vkEnumerateDeviceExtensionProperties) {
            res = g_host_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName,
                                                              &host_count, nullptr);
        }
        *pPropertyCount = host_count + kExtraCount;
        return res;
    }

    uint32_t capacity = *pPropertyCount;
    uint32_t host_count = capacity;
    VkResult res = VK_SUCCESS;
    if (g_host_vkEnumerateDeviceExtensionProperties) {
        res = g_host_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName,
                                                          &host_count, pProperties);
        if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
            host_count = 0;
        }
    } else {
        host_count = 0;
    }

    uint32_t added = 0;
    for (uint32_t i = 0; i < kExtraCount; ++i) {
        if (host_count + added < capacity) {
            pProperties[host_count + added] = kAndroidExts[i];
            added++;
        }
    }
    *pPropertyCount = host_count + added;
    return res;
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

__attribute__((sysv_abi)) VkResult Sysv_vkGetMemoryAndroidHardwareBufferANDROID(
    VkDevice device, const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
    struct AHardwareBuffer **pBuffer)
{
    (void)device;
    if (!pInfo || !pBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::lock_guard<std::mutex> lock(g_ahb_memory_mutex);
    auto it = g_imported_ahb_buffers.find(pInfo->memory);
    if (it != g_imported_ahb_buffers.end()) {
        *pBuffer = it->second;
        return VK_SUCCESS;
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// Shaders & Pipelines
__attribute__((sysv_abi)) VkResult
Sysv_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule)
{
    if (!g_host_vkCreateShaderModule)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                           const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyShaderModule)
        g_host_vkDestroyShaderModule(device, shaderModule, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipelineLayout *pPipelineLayout)
{
    if (!g_host_vkCreatePipelineLayout)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                             const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyPipelineLayout)
        g_host_vkDestroyPipelineLayout(device, pipelineLayout, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipelineCache *pPipelineCache)
{
    if (!g_host_vkCreatePipelineCache)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreatePipelineCache(device, pCreateInfo, pAllocator, pPipelineCache);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                            const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyPipelineCache)
        g_host_vkDestroyPipelineCache(device, pipelineCache, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t *pDataSize,
                            void *pData)
{
    if (!g_host_vkGetPipelineCacheData)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkGetPipelineCacheData(device, pipelineCache, pDataSize, pData);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                               uint32_t createInfoCount,
                               const VkGraphicsPipelineCreateInfo *pCreateInfos,
                               const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    if (!g_host_vkCreateGraphicsPipelines)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos,
                                            pAllocator, pPipelines);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                              uint32_t createInfoCount,
                              const VkComputePipelineCreateInfo *pCreateInfos,
                              const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
    if (!g_host_vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos,
                                           pAllocator, pPipelines);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                                                      const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyPipeline)
        g_host_vkDestroyPipeline(device, pipeline, pAllocator);
}

// RenderPass, Framebuffers & Dynamic Rendering
__attribute__((sysv_abi)) VkResult
Sysv_vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    if (!g_host_vkCreateRenderPass)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass)
{
    if (!g_host_vkCreateRenderPass2)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return g_host_vkCreateRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                                                        const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyRenderPass)
        g_host_vkDestroyRenderPass(device, renderPass, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer)
{
    if (!g_host_vkCreateFramebuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                          const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyFramebuffer)
        g_host_vkDestroyFramebuffer(device, framebuffer, pAllocator);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                          const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents)
{
    if (g_host_vkCmdBeginRenderPass)
        g_host_vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

__attribute__((sysv_abi)) void Sysv_vkCmdEndRenderPass(VkCommandBuffer commandBuffer)
{
    if (g_host_vkCmdEndRenderPass)
        g_host_vkCmdEndRenderPass(commandBuffer);
}

__attribute__((sysv_abi)) void Sysv_vkCmdNextSubpass(VkCommandBuffer commandBuffer,
                                                     VkSubpassContents contents)
{
    if (g_host_vkCmdNextSubpass)
        g_host_vkCmdNextSubpass(commandBuffer, contents);
}

__attribute__((sysv_abi)) void Sysv_vkCmdBeginRendering(VkCommandBuffer commandBuffer,
                                                        const VkRenderingInfo *pRenderingInfo)
{
    if (g_host_vkCmdBeginRendering)
        g_host_vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

__attribute__((sysv_abi)) void Sysv_vkCmdEndRendering(VkCommandBuffer commandBuffer)
{
    if (g_host_vkCmdEndRendering)
        g_host_vkCmdEndRendering(commandBuffer);
}

// Images, Views & Samplers
__attribute__((sysv_abi)) VkResult Sysv_vkCreateImage(VkDevice device,
                                                      const VkImageCreateInfo *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkImage *pImage)
{
    if (!g_host_vkCreateImage)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateImage(device, pCreateInfo, pAllocator, pImage);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyImage(VkDevice device, VkImage image,
                                                   const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyImage)
        g_host_vkDestroyImage(device, image, pAllocator);
}

__attribute__((sysv_abi)) void
Sysv_vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                                  VkMemoryRequirements *pMemoryRequirements)
{
    if (g_host_vkGetImageMemoryRequirements)
        g_host_vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
}

__attribute__((sysv_abi)) void
Sysv_vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                   VkMemoryRequirements2 *pMemoryRequirements)
{
    if (g_host_vkGetImageMemoryRequirements2)
        g_host_vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

__attribute__((sysv_abi)) VkResult Sysv_vkBindImageMemory(VkDevice device, VkImage image,
                                                          VkDeviceMemory memory,
                                                          VkDeviceSize memoryOffset)
{
    if (!g_host_vkBindImageMemory)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkBindImageMemory(device, image, memory, memoryOffset);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                        const VkBindImageMemoryInfo *pBindInfos)
{
    if (!g_host_vkBindImageMemory2)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return g_host_vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                         const VkBindBufferMemoryInfo *pBindInfos)
{
    if (!g_host_vkBindBufferMemory2)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return g_host_vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
    if (!g_host_vkCreateImageView)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateImageView(device, pCreateInfo, pAllocator, pView);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyImageView(VkDevice device, VkImageView imageView,
                                                       const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyImageView)
        g_host_vkDestroyImageView(device, imageView, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
    if (!g_host_vkCreateSampler)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
}

__attribute__((sysv_abi)) void Sysv_vkDestroySampler(VkDevice device, VkSampler sampler,
                                                     const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroySampler)
        g_host_vkDestroySampler(device, sampler, pAllocator);
}

// Descriptors & Push Constants
__attribute__((sysv_abi)) VkResult Sysv_vkCreateDescriptorSetLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
    if (!g_host_vkCreateDescriptorSetLayout)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                  const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyDescriptorSetLayout)
        g_host_vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDescriptorPool *pDescriptorPool)
{
    if (!g_host_vkCreateDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
}

__attribute__((sysv_abi)) void
Sysv_vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                             const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyDescriptorPool)
        g_host_vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                           VkDescriptorPoolResetFlags flags)
{
    if (!g_host_vkResetDescriptorPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkResetDescriptorPool(device, descriptorPool, flags);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                              VkDescriptorSet *pDescriptorSets)
{
    if (!g_host_vkAllocateDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                          uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets)
{
    if (!g_host_vkFreeDescriptorSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

__attribute__((sysv_abi)) void
Sysv_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount,
                            const VkCopyDescriptorSet *pDescriptorCopies)
{
    if (g_host_vkUpdateDescriptorSets)
        g_host_vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites,
                                      descriptorCopyCount, pDescriptorCopies);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                             VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
                             const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                             const uint32_t *pDynamicOffsets)
{
    if (g_host_vkCmdBindDescriptorSets)
        g_host_vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet,
                                       descriptorSetCount, pDescriptorSets, dynamicOffsetCount,
                                       pDynamicOffsets);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                        const void *pValues)
{
    if (g_host_vkCmdPushConstants)
        g_host_vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

// Synchronization (Fences, Semaphores, Events)
__attribute__((sysv_abi)) VkResult Sysv_vkCreateFence(VkDevice device,
                                                      const VkFenceCreateInfo *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkFence *pFence)
{
    if (!g_host_vkCreateFence)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateFence(device, pCreateInfo, pAllocator, pFence);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyFence(VkDevice device, VkFence fence,
                                                   const VkAllocationCallbacks *pAllocator)
{
    {
        std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
        g_fence_sync_states.erase(fence);
    }
    if (g_host_vkDestroyFence)
        g_host_vkDestroyFence(device, fence, pAllocator);
}

__attribute__((sysv_abi)) VkResult Sysv_vkResetFences(VkDevice device, uint32_t fenceCount,
                                                      const VkFence *pFences)
{
    if (!g_host_vkResetFences)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkResetFences(device, fenceCount, pFences);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetFenceStatus(VkDevice device, VkFence fence)
{
    if (!g_host_vkGetFenceStatus)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = g_host_vkGetFenceStatus(device, fence);
    if (res == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
        auto it = g_fence_sync_states.find(fence);
        if (it != g_fence_sync_states.end() && it->second) {
            it->second->Signal();
        }
    }
    return res;
}

__attribute__((sysv_abi)) VkResult Sysv_vkWaitForFences(VkDevice device, uint32_t fenceCount,
                                                        const VkFence *pFences, VkBool32 waitAll,
                                                        uint64_t timeout)
{
    if (!g_host_vkWaitForFences)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = g_host_vkWaitForFences(device, fenceCount, pFences, waitAll, timeout);
    if (res == VK_SUCCESS && pFences) {
        std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
        for (uint32_t i = 0; i < fenceCount; ++i) {
            auto it = g_fence_sync_states.find(pFences[i]);
            if (it != g_fence_sync_states.end() && it->second) {
                it->second->Signal();
            }
        }
    }
    return res;
}

__attribute__((sysv_abi)) VkResult
Sysv_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore)
{
    if (!g_host_vkCreateSemaphore)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
}

__attribute__((sysv_abi)) void Sysv_vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                                       const VkAllocationCallbacks *pAllocator)
{
    {
        std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
        g_semaphore_sync_states.erase(semaphore);
    }
    if (g_host_vkDestroySemaphore)
        g_host_vkDestroySemaphore(device, semaphore, pAllocator);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t *pValue)
{
    if (!g_host_vkGetSemaphoreCounterValue)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return g_host_vkGetSemaphoreCounterValue(device, semaphore, pValue);
}

__attribute__((sysv_abi)) VkResult Sysv_vkWaitSemaphores(VkDevice device,
                                                         const VkSemaphoreWaitInfo *pWaitInfo,
                                                         uint64_t timeout)
{
    if (!g_host_vkWaitSemaphores)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return g_host_vkWaitSemaphores(device, pWaitInfo, timeout);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
    if (!g_host_vkSignalSemaphore)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkResult res = g_host_vkSignalSemaphore(device, pSignalInfo);
    if (res == VK_SUCCESS && pSignalInfo) {
        std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
        auto it = g_semaphore_sync_states.find(pSignalInfo->semaphore);
        if (it != g_semaphore_sync_states.end() && it->second) {
            it->second->Signal();
        }
    }
    return res;
}

__attribute__((sysv_abi)) VkResult Sysv_vkCreateEvent(VkDevice device,
                                                      const VkEventCreateInfo *pCreateInfo,
                                                      const VkAllocationCallbacks *pAllocator,
                                                      VkEvent *pEvent)
{
    if (!g_host_vkCreateEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkCreateEvent(device, pCreateInfo, pAllocator, pEvent);
}

__attribute__((sysv_abi)) void Sysv_vkDestroyEvent(VkDevice device, VkEvent event,
                                                   const VkAllocationCallbacks *pAllocator)
{
    if (g_host_vkDestroyEvent)
        g_host_vkDestroyEvent(device, event, pAllocator);
}

__attribute__((sysv_abi)) VkResult Sysv_vkGetEventStatus(VkDevice device, VkEvent event)
{
    if (!g_host_vkGetEventStatus)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkGetEventStatus(device, event);
}

__attribute__((sysv_abi)) VkResult Sysv_vkSetEvent(VkDevice device, VkEvent event)
{
    if (!g_host_vkSetEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkSetEvent(device, event);
}

__attribute__((sysv_abi)) VkResult Sysv_vkResetEvent(VkDevice device, VkEvent event)
{
    if (!g_host_vkResetEvent)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkResetEvent(device, event);
}

// Command Buffer Recording & Actions
__attribute__((sysv_abi)) VkResult
Sysv_vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                        VkCommandPoolResetFlags flags)
{
    if (!g_host_vkResetCommandPool)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkResetCommandPool(device, commandPool, flags);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    if (!g_host_vkResetCommandBuffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    return g_host_vkResetCommandBuffer(commandBuffer, flags);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                       VkPipeline pipeline)
{
    if (g_host_vkCmdBindPipeline)
        g_host_vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount,
                      const VkViewport *pViewports)
{
    if (g_host_vkCmdSetViewport)
        g_host_vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount,
                     const VkRect2D *pScissors)
{
    if (g_host_vkCmdSetScissor)
        g_host_vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                            uint32_t bindingCount, const VkBuffer *pBuffers,
                            const VkDeviceSize *pOffsets)
{
    if (g_host_vkCmdBindVertexBuffers)
        g_host_vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers,
                                      pOffsets);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                          VkIndexType indexType)
{
    if (g_host_vkCmdBindIndexBuffer)
        g_host_vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
               uint32_t firstVertex, uint32_t firstInstance)
{
    if (g_host_vkCmdDraw)
        g_host_vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    if (g_host_vkCmdDrawIndexed)
        g_host_vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                                firstInstance);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                       uint32_t drawCount, uint32_t stride)
{
    if (g_host_vkCmdDrawIndirect)
        g_host_vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride)
{
    if (g_host_vkCmdDrawIndexedIndirect)
        g_host_vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                   uint32_t groupCountZ)
{
    if (g_host_vkCmdDispatch)
        g_host_vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
{
    if (g_host_vkCmdDispatchIndirect)
        g_host_vkCmdDispatchIndirect(commandBuffer, buffer, offset);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const VkBufferCopy *pRegions)
{
    if (g_host_vkCmdCopyBuffer)
        g_host_vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                    const VkImageCopy *pRegions)
{
    if (g_host_vkCmdCopyImage)
        g_host_vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                              regionCount, pRegions);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                            VkImageLayout dstImageLayout, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions)
{
    if (g_host_vkCmdCopyBufferToImage)
        g_host_vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout,
                                      regionCount, pRegions);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                            VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions)
{
    if (g_host_vkCmdCopyImageToBuffer)
        g_host_vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer,
                                      regionCount, pRegions);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                    VkImageLayout srcImageLayout, VkImage dstImage,
                    VkImageLayout dstImageLayout, uint32_t regionCount,
                    const VkImageBlit *pRegions, VkFilter filter)
{
    if (g_host_vkCmdBlitImage)
        g_host_vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                              regionCount, pRegions, filter);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount,
                          const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount,
                          const VkImageMemoryBarrier *pImageMemoryBarriers)
{
    if (g_host_vkCmdPipelineBarrier)
        g_host_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                    memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                    pBufferMemoryBarriers, imageMemoryBarrierCount,
                                    pImageMemoryBarriers);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                           const VkDependencyInfo *pDependencyInfo)
{
    if (g_host_vkCmdPipelineBarrier2)
        g_host_vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                          VkImageLayout imageLayout, const VkClearColorValue *pColor,
                          uint32_t rangeCount, const VkImageSubresourceRange *pRanges)
{
    if (g_host_vkCmdClearColorImage)
        g_host_vkCmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                           const VkClearAttachment *pAttachments, uint32_t rectCount,
                           const VkClearRect *pRects)
{
    if (g_host_vkCmdClearAttachments)
        g_host_vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount,
                                     pRects);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                     VkDeviceSize size, uint32_t data)
{
    if (g_host_vkCmdFillBuffer)
        g_host_vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

__attribute__((sysv_abi)) void
Sysv_vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                       VkDeviceSize dataSize, const void *pData)
{
    if (g_host_vkCmdUpdateBuffer)
        g_host_vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

// Instance & PhysicalDevice Queries
__attribute__((sysv_abi)) VkResult
Sysv_vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (!pApiVersion)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (g_host_vkEnumerateInstanceVersion)
        return g_host_vkEnumerateInstanceVersion(pApiVersion);
    *pApiVersion = VK_API_VERSION_1_0;
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceProperties2 *pProperties)
{
    if (g_host_vkGetPhysicalDeviceProperties2)
        g_host_vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceFeatures2 *pFeatures)
{
    if (g_host_vkGetPhysicalDeviceFeatures2)
        g_host_vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                         VkFormatProperties *pFormatProperties)
{
    if (g_host_vkGetPhysicalDeviceFormatProperties)
        g_host_vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                          VkFormatProperties2 *pFormatProperties)
{
    if (g_host_vkGetPhysicalDeviceFormatProperties2)
        g_host_vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

__attribute__((sysv_abi)) void
Sysv_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                          VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
    if (g_host_vkGetPhysicalDeviceMemoryProperties2)
        g_host_vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}

__attribute__((sysv_abi)) void Sysv_vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
    if (g_host_vkGetPhysicalDeviceQueueFamilyProperties2)
        g_host_vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pQueueFamilyPropertyCount,
                                                         pQueueFamilyProperties);
}

__attribute__((sysv_abi)) void
Sysv_vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2 *pInfo,
                                    VkMemoryRequirements2 *pMemoryRequirements)
{
    if (g_host_vkGetBufferMemoryRequirements2)
        g_host_vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

__attribute__((sysv_abi)) void Sysv_vkGetDeviceQueue2(VkDevice device,
                                                      const VkDeviceQueueInfo2 *pQueueInfo,
                                                      VkQueue *pQueue)
{
    if (g_host_vkGetDeviceQueue2)
        g_host_vkGetDeviceQueue2(device, pQueueInfo, pQueue);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                               const VkMappedMemoryRange *pMemoryRanges)
{
    if (!g_host_vkFlushMappedMemoryRanges)
        return VK_SUCCESS;
    return g_host_vkFlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
}

__attribute__((sysv_abi)) VkResult
Sysv_vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                    const VkMappedMemoryRange *pMemoryRanges)
{
    if (!g_host_vkInvalidateMappedMemoryRanges)
        return VK_SUCCESS;
    return g_host_vkInvalidateMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
}

// External FDs
__attribute__((sysv_abi)) VkResult
Sysv_vkGetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR *pGetFdInfo, int *pFd)
{
    (void)device;
    if (!pGetFdInfo || !pFd)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto *fd_table = GetActiveFdTable();
    if (!fd_table)
        return VK_ERROR_INITIALIZATION_FAILED;

    bool is_signaled = false;
    if (g_host_vkGetFenceStatus) {
        is_signaled = (g_host_vkGetFenceStatus(device, pGetFdInfo->fence) == VK_SUCCESS);
    }
    std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
    auto it = g_fence_sync_states.find(pGetFdInfo->fence);
    std::shared_ptr<fs::SyncFenceState> state;
    if (it != g_fence_sync_states.end()) {
        state = it->second;
    }
    int fd = fd_table->AllocateSyncFence(nullptr, nullptr, is_signaled, 0);
    if (fd < 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto desc = fd_table->Get(fd);
    if (desc && desc->sync_fence) {
        if (state) {
            desc->sync_fence = state;
        } else {
            g_fence_sync_states[pGetFdInfo->fence] = desc->sync_fence;
        }
    }
    *pFd = fd;
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) VkResult
Sysv_vkImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
    (void)device;
    if (!pImportFenceFdInfo)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto *fd_table = GetActiveFdTable();
    if (!fd_table)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto desc = fd_table->Get(pImportFenceFdInfo->fd);
    if (!desc || !desc->sync_fence)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
    g_fence_sync_states[pImportFenceFdInfo->fence] = desc->sync_fence;
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) VkResult
Sysv_vkGetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR *pGetFdInfo, int *pFd)
{
    (void)device;
    if (!pGetFdInfo || !pFd)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto *fd_table = GetActiveFdTable();
    if (!fd_table)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
    auto it = g_semaphore_sync_states.find(pGetFdInfo->semaphore);
    std::shared_ptr<fs::SyncFenceState> state;
    if (it != g_semaphore_sync_states.end()) {
        state = it->second;
    }
    int fd = fd_table->AllocateSyncFence(nullptr, nullptr, false, 0);
    if (fd < 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto desc = fd_table->Get(fd);
    if (desc && desc->sync_fence) {
        if (state) {
            desc->sync_fence = state;
        } else {
            g_semaphore_sync_states[pGetFdInfo->semaphore] = desc->sync_fence;
        }
    }
    *pFd = fd;
    return VK_SUCCESS;
}

__attribute__((sysv_abi)) VkResult
Sysv_vkImportSemaphoreFdKHR(VkDevice device,
                            const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
    (void)device;
    if (!pImportSemaphoreFdInfo)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto *fd_table = GetActiveFdTable();
    if (!fd_table)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto desc = fd_table->Get(pImportSemaphoreFdInfo->fd);
    if (!desc || !desc->sync_fence)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::lock_guard<std::mutex> lock(g_sync_fence_mutex);
    g_semaphore_sync_states[pImportSemaphoreFdInfo->semaphore] = desc->sync_fence;
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
    if (name == "vkGetMemoryAndroidHardwareBufferANDROID")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetMemoryAndroidHardwareBufferANDROID);

    // Shaders & Pipelines
    if (name == "vkCreateShaderModule")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateShaderModule);
    if (name == "vkDestroyShaderModule")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyShaderModule);
    if (name == "vkCreatePipelineLayout")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreatePipelineLayout);
    if (name == "vkDestroyPipelineLayout")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyPipelineLayout);
    if (name == "vkCreatePipelineCache")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreatePipelineCache);
    if (name == "vkDestroyPipelineCache")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyPipelineCache);
    if (name == "vkGetPipelineCacheData")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPipelineCacheData);
    if (name == "vkCreateGraphicsPipelines")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateGraphicsPipelines);
    if (name == "vkCreateComputePipelines")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateComputePipelines);
    if (name == "vkDestroyPipeline")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyPipeline);

    // RenderPass, Framebuffers & Dynamic Rendering
    if (name == "vkCreateRenderPass")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateRenderPass);
    if (name == "vkCreateRenderPass2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateRenderPass2);
    if (name == "vkDestroyRenderPass")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyRenderPass);
    if (name == "vkCreateFramebuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateFramebuffer);
    if (name == "vkDestroyFramebuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyFramebuffer);
    if (name == "vkCmdBeginRenderPass")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBeginRenderPass);
    if (name == "vkCmdEndRenderPass")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdEndRenderPass);
    if (name == "vkCmdNextSubpass")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdNextSubpass);
    if (name == "vkCmdBeginRendering" || name == "vkCmdBeginRenderingKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBeginRendering);
    if (name == "vkCmdEndRendering" || name == "vkCmdEndRenderingKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdEndRendering);

    // Images, Views & Samplers
    if (name == "vkCreateImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateImage);
    if (name == "vkDestroyImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyImage);
    if (name == "vkGetImageMemoryRequirements")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetImageMemoryRequirements);
    if (name == "vkGetImageMemoryRequirements2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetImageMemoryRequirements2);
    if (name == "vkBindImageMemory")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkBindImageMemory);
    if (name == "vkBindImageMemory2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkBindImageMemory2);
    if (name == "vkBindBufferMemory2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkBindBufferMemory2);
    if (name == "vkCreateImageView")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateImageView);
    if (name == "vkDestroyImageView")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyImageView);
    if (name == "vkCreateSampler")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateSampler);
    if (name == "vkDestroySampler")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroySampler);

    // Descriptors & Push Constants
    if (name == "vkCreateDescriptorSetLayout")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateDescriptorSetLayout);
    if (name == "vkDestroyDescriptorSetLayout")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyDescriptorSetLayout);
    if (name == "vkCreateDescriptorPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateDescriptorPool);
    if (name == "vkDestroyDescriptorPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyDescriptorPool);
    if (name == "vkResetDescriptorPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkResetDescriptorPool);
    if (name == "vkAllocateDescriptorSets")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkAllocateDescriptorSets);
    if (name == "vkFreeDescriptorSets")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkFreeDescriptorSets);
    if (name == "vkUpdateDescriptorSets")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkUpdateDescriptorSets);
    if (name == "vkCmdBindDescriptorSets")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBindDescriptorSets);
    if (name == "vkCmdPushConstants")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdPushConstants);

    // Synchronization
    if (name == "vkCreateFence")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateFence);
    if (name == "vkDestroyFence")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyFence);
    if (name == "vkResetFences")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkResetFences);
    if (name == "vkGetFenceStatus")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetFenceStatus);
    if (name == "vkWaitForFences")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkWaitForFences);
    if (name == "vkCreateSemaphore")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateSemaphore);
    if (name == "vkDestroySemaphore")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroySemaphore);
    if (name == "vkGetSemaphoreCounterValue")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetSemaphoreCounterValue);
    if (name == "vkWaitSemaphores")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkWaitSemaphores);
    if (name == "vkSignalSemaphore")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkSignalSemaphore);
    if (name == "vkCreateEvent")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCreateEvent);
    if (name == "vkDestroyEvent")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkDestroyEvent);
    if (name == "vkGetEventStatus")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetEventStatus);
    if (name == "vkSetEvent")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkSetEvent);
    if (name == "vkResetEvent")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkResetEvent);

    // Command Buffer Recording
    if (name == "vkResetCommandPool")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkResetCommandPool);
    if (name == "vkResetCommandBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkResetCommandBuffer);
    if (name == "vkCmdBindPipeline")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBindPipeline);
    if (name == "vkCmdSetViewport")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdSetViewport);
    if (name == "vkCmdSetScissor")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdSetScissor);
    if (name == "vkCmdBindVertexBuffers")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBindVertexBuffers);
    if (name == "vkCmdBindIndexBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBindIndexBuffer);
    if (name == "vkCmdDraw")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDraw);
    if (name == "vkCmdDrawIndexed")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDrawIndexed);
    if (name == "vkCmdDrawIndirect")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDrawIndirect);
    if (name == "vkCmdDrawIndexedIndirect")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDrawIndexedIndirect);
    if (name == "vkCmdDispatch")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDispatch);
    if (name == "vkCmdDispatchIndirect")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdDispatchIndirect);
    if (name == "vkCmdCopyBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdCopyBuffer);
    if (name == "vkCmdCopyImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdCopyImage);
    if (name == "vkCmdCopyBufferToImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdCopyBufferToImage);
    if (name == "vkCmdCopyImageToBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdCopyImageToBuffer);
    if (name == "vkCmdBlitImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdBlitImage);
    if (name == "vkCmdPipelineBarrier")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdPipelineBarrier);
    if (name == "vkCmdPipelineBarrier2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdPipelineBarrier2);
    if (name == "vkCmdClearColorImage")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdClearColorImage);
    if (name == "vkCmdClearAttachments")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdClearAttachments);
    if (name == "vkCmdFillBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdFillBuffer);
    if (name == "vkCmdUpdateBuffer")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkCmdUpdateBuffer);

    // Queries
    if (name == "vkEnumerateInstanceVersion")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkEnumerateInstanceVersion);
    if (name == "vkGetPhysicalDeviceProperties2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceProperties2);
    if (name == "vkGetPhysicalDeviceFeatures2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceFeatures2);
    if (name == "vkGetPhysicalDeviceFormatProperties")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceFormatProperties);
    if (name == "vkGetPhysicalDeviceFormatProperties2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceFormatProperties2);
    if (name == "vkGetPhysicalDeviceMemoryProperties2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceMemoryProperties2);
    if (name == "vkGetPhysicalDeviceQueueFamilyProperties2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetPhysicalDeviceQueueFamilyProperties2);
    if (name == "vkGetBufferMemoryRequirements2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetBufferMemoryRequirements2);
    if (name == "vkGetDeviceQueue2")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetDeviceQueue2);
    if (name == "vkFlushMappedMemoryRanges")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkFlushMappedMemoryRanges);
    if (name == "vkInvalidateMappedMemoryRanges")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkInvalidateMappedMemoryRanges);

    // External FDs & AHB
    if (name == "vkGetFenceFdKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetFenceFdKHR);
    if (name == "vkImportFenceFdKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkImportFenceFdKHR);
    if (name == "vkGetSemaphoreFdKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkGetSemaphoreFdKHR);
    if (name == "vkImportSemaphoreFdKHR")
        return reinterpret_cast<PFN_vkVoidFunction>(Sysv_vkImportSemaphoreFdKHR);

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

    m_dispatch_table.version = 3;
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
    m_dispatch_table.vkGetMemoryAndroidHardwareBufferANDROID =
        Sysv_vkGetMemoryAndroidHardwareBufferANDROID;

    // Shaders & Pipelines
    m_dispatch_table.vkCreateShaderModule = Sysv_vkCreateShaderModule;
    m_dispatch_table.vkDestroyShaderModule = Sysv_vkDestroyShaderModule;
    m_dispatch_table.vkCreatePipelineLayout = Sysv_vkCreatePipelineLayout;
    m_dispatch_table.vkDestroyPipelineLayout = Sysv_vkDestroyPipelineLayout;
    m_dispatch_table.vkCreatePipelineCache = Sysv_vkCreatePipelineCache;
    m_dispatch_table.vkDestroyPipelineCache = Sysv_vkDestroyPipelineCache;
    m_dispatch_table.vkGetPipelineCacheData = Sysv_vkGetPipelineCacheData;
    m_dispatch_table.vkCreateGraphicsPipelines = Sysv_vkCreateGraphicsPipelines;
    m_dispatch_table.vkCreateComputePipelines = Sysv_vkCreateComputePipelines;
    m_dispatch_table.vkDestroyPipeline = Sysv_vkDestroyPipeline;

    // RenderPass, Framebuffers & Dynamic Rendering
    m_dispatch_table.vkCreateRenderPass = Sysv_vkCreateRenderPass;
    m_dispatch_table.vkCreateRenderPass2 = Sysv_vkCreateRenderPass2;
    m_dispatch_table.vkDestroyRenderPass = Sysv_vkDestroyRenderPass;
    m_dispatch_table.vkCreateFramebuffer = Sysv_vkCreateFramebuffer;
    m_dispatch_table.vkDestroyFramebuffer = Sysv_vkDestroyFramebuffer;
    m_dispatch_table.vkCmdBeginRenderPass = Sysv_vkCmdBeginRenderPass;
    m_dispatch_table.vkCmdEndRenderPass = Sysv_vkCmdEndRenderPass;
    m_dispatch_table.vkCmdNextSubpass = Sysv_vkCmdNextSubpass;
    m_dispatch_table.vkCmdBeginRendering = Sysv_vkCmdBeginRendering;
    m_dispatch_table.vkCmdEndRendering = Sysv_vkCmdEndRendering;

    // Images, Views & Samplers
    m_dispatch_table.vkCreateImage = Sysv_vkCreateImage;
    m_dispatch_table.vkDestroyImage = Sysv_vkDestroyImage;
    m_dispatch_table.vkGetImageMemoryRequirements = Sysv_vkGetImageMemoryRequirements;
    m_dispatch_table.vkGetImageMemoryRequirements2 = Sysv_vkGetImageMemoryRequirements2;
    m_dispatch_table.vkBindImageMemory = Sysv_vkBindImageMemory;
    m_dispatch_table.vkBindImageMemory2 = Sysv_vkBindImageMemory2;
    m_dispatch_table.vkBindBufferMemory2 = Sysv_vkBindBufferMemory2;
    m_dispatch_table.vkCreateImageView = Sysv_vkCreateImageView;
    m_dispatch_table.vkDestroyImageView = Sysv_vkDestroyImageView;
    m_dispatch_table.vkCreateSampler = Sysv_vkCreateSampler;
    m_dispatch_table.vkDestroySampler = Sysv_vkDestroySampler;

    // Descriptors & Push Constants
    m_dispatch_table.vkCreateDescriptorSetLayout = Sysv_vkCreateDescriptorSetLayout;
    m_dispatch_table.vkDestroyDescriptorSetLayout = Sysv_vkDestroyDescriptorSetLayout;
    m_dispatch_table.vkCreateDescriptorPool = Sysv_vkCreateDescriptorPool;
    m_dispatch_table.vkDestroyDescriptorPool = Sysv_vkDestroyDescriptorPool;
    m_dispatch_table.vkResetDescriptorPool = Sysv_vkResetDescriptorPool;
    m_dispatch_table.vkAllocateDescriptorSets = Sysv_vkAllocateDescriptorSets;
    m_dispatch_table.vkFreeDescriptorSets = Sysv_vkFreeDescriptorSets;
    m_dispatch_table.vkUpdateDescriptorSets = Sysv_vkUpdateDescriptorSets;
    m_dispatch_table.vkCmdBindDescriptorSets = Sysv_vkCmdBindDescriptorSets;
    m_dispatch_table.vkCmdPushConstants = Sysv_vkCmdPushConstants;

    // Synchronization
    m_dispatch_table.vkCreateFence = Sysv_vkCreateFence;
    m_dispatch_table.vkDestroyFence = Sysv_vkDestroyFence;
    m_dispatch_table.vkResetFences = Sysv_vkResetFences;
    m_dispatch_table.vkGetFenceStatus = Sysv_vkGetFenceStatus;
    m_dispatch_table.vkWaitForFences = Sysv_vkWaitForFences;
    m_dispatch_table.vkCreateSemaphore = Sysv_vkCreateSemaphore;
    m_dispatch_table.vkDestroySemaphore = Sysv_vkDestroySemaphore;
    m_dispatch_table.vkGetSemaphoreCounterValue = Sysv_vkGetSemaphoreCounterValue;
    m_dispatch_table.vkWaitSemaphores = Sysv_vkWaitSemaphores;
    m_dispatch_table.vkSignalSemaphore = Sysv_vkSignalSemaphore;
    m_dispatch_table.vkCreateEvent = Sysv_vkCreateEvent;
    m_dispatch_table.vkDestroyEvent = Sysv_vkDestroyEvent;
    m_dispatch_table.vkGetEventStatus = Sysv_vkGetEventStatus;
    m_dispatch_table.vkSetEvent = Sysv_vkSetEvent;
    m_dispatch_table.vkResetEvent = Sysv_vkResetEvent;

    // Command Buffer Recording
    m_dispatch_table.vkResetCommandPool = Sysv_vkResetCommandPool;
    m_dispatch_table.vkResetCommandBuffer = Sysv_vkResetCommandBuffer;
    m_dispatch_table.vkCmdBindPipeline = Sysv_vkCmdBindPipeline;
    m_dispatch_table.vkCmdSetViewport = Sysv_vkCmdSetViewport;
    m_dispatch_table.vkCmdSetScissor = Sysv_vkCmdSetScissor;
    m_dispatch_table.vkCmdBindVertexBuffers = Sysv_vkCmdBindVertexBuffers;
    m_dispatch_table.vkCmdBindIndexBuffer = Sysv_vkCmdBindIndexBuffer;
    m_dispatch_table.vkCmdDraw = Sysv_vkCmdDraw;
    m_dispatch_table.vkCmdDrawIndexed = Sysv_vkCmdDrawIndexed;
    m_dispatch_table.vkCmdDrawIndirect = Sysv_vkCmdDrawIndirect;
    m_dispatch_table.vkCmdDrawIndexedIndirect = Sysv_vkCmdDrawIndexedIndirect;
    m_dispatch_table.vkCmdDispatch = Sysv_vkCmdDispatch;
    m_dispatch_table.vkCmdDispatchIndirect = Sysv_vkCmdDispatchIndirect;
    m_dispatch_table.vkCmdCopyBuffer = Sysv_vkCmdCopyBuffer;
    m_dispatch_table.vkCmdCopyImage = Sysv_vkCmdCopyImage;
    m_dispatch_table.vkCmdCopyBufferToImage = Sysv_vkCmdCopyBufferToImage;
    m_dispatch_table.vkCmdCopyImageToBuffer = Sysv_vkCmdCopyImageToBuffer;
    m_dispatch_table.vkCmdBlitImage = Sysv_vkCmdBlitImage;
    m_dispatch_table.vkCmdPipelineBarrier = Sysv_vkCmdPipelineBarrier;
    m_dispatch_table.vkCmdPipelineBarrier2 = Sysv_vkCmdPipelineBarrier2;
    m_dispatch_table.vkCmdClearColorImage = Sysv_vkCmdClearColorImage;
    m_dispatch_table.vkCmdClearAttachments = Sysv_vkCmdClearAttachments;
    m_dispatch_table.vkCmdFillBuffer = Sysv_vkCmdFillBuffer;
    m_dispatch_table.vkCmdUpdateBuffer = Sysv_vkCmdUpdateBuffer;

    // Instance & PhysicalDevice Queries
    m_dispatch_table.vkEnumerateInstanceVersion = Sysv_vkEnumerateInstanceVersion;
    m_dispatch_table.vkGetPhysicalDeviceProperties2 = Sysv_vkGetPhysicalDeviceProperties2;
    m_dispatch_table.vkGetPhysicalDeviceFeatures2 = Sysv_vkGetPhysicalDeviceFeatures2;
    m_dispatch_table.vkGetPhysicalDeviceFormatProperties = Sysv_vkGetPhysicalDeviceFormatProperties;
    m_dispatch_table.vkGetPhysicalDeviceFormatProperties2 =
        Sysv_vkGetPhysicalDeviceFormatProperties2;
    m_dispatch_table.vkGetPhysicalDeviceMemoryProperties2 =
        Sysv_vkGetPhysicalDeviceMemoryProperties2;
    m_dispatch_table.vkGetPhysicalDeviceQueueFamilyProperties2 =
        Sysv_vkGetPhysicalDeviceQueueFamilyProperties2;
    m_dispatch_table.vkGetBufferMemoryRequirements2 = Sysv_vkGetBufferMemoryRequirements2;
    m_dispatch_table.vkGetDeviceQueue2 = Sysv_vkGetDeviceQueue2;
    m_dispatch_table.vkFlushMappedMemoryRanges = Sysv_vkFlushMappedMemoryRanges;
    m_dispatch_table.vkInvalidateMappedMemoryRanges = Sysv_vkInvalidateMappedMemoryRanges;

    // External FDs
    m_dispatch_table.vkGetFenceFdKHR = Sysv_vkGetFenceFdKHR;
    m_dispatch_table.vkImportFenceFdKHR = Sysv_vkImportFenceFdKHR;
    m_dispatch_table.vkGetSemaphoreFdKHR = Sysv_vkGetSemaphoreFdKHR;
    m_dispatch_table.vkImportSemaphoreFdKHR = Sysv_vkImportSemaphoreFdKHR;

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
