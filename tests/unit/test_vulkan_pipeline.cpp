#include "fs/fd_table.hpp"
#include "ndk/hardware_buffer.hpp"
#include "vulkan/vulkan_bridge.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace portage;
using namespace portage::vulkan;

namespace {

// Embedded SPIR-V bytecode for compute shader:
// layout(local_size_x = 1) in;
// layout(std430, binding = 0) buffer Buf { uint data[]; };
// void main() { uint idx = gl_GlobalInvocationID.x; data[idx] = data[idx] * 2 + 1; }
const uint32_t kComputeShaderSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000021, 0x00000000, 0x00020011, 0x00000001,
    0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
    0x00000000, 0x00000001, 0x0006000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000,
    0x0000000b, 0x00060010, 0x00000004, 0x00000011, 0x00000001, 0x00000001, 0x00000001,
    0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45,
    0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004,
    0x475f4c47, 0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000008, 0x00786469,
    0x00080005, 0x0000000b, 0x475f6c67, 0x61626f6c, 0x766e496c, 0x7461636f, 0x496e6f69,
    0x00000044, 0x00030005, 0x00000011, 0x00667542, 0x00050006, 0x00000011, 0x00000000,
    0x61746164, 0x00000000, 0x00030005, 0x00000013, 0x00000000, 0x00040047, 0x0000000b,
    0x0000000b, 0x0000001c, 0x00040047, 0x00000010, 0x00000006, 0x00000004, 0x00030047,
    0x00000011, 0x00000003, 0x00050048, 0x00000011, 0x00000000, 0x00000023, 0x00000000,
    0x00040047, 0x00000013, 0x00000021, 0x00000000, 0x00040047, 0x00000013, 0x00000022,
    0x00000000, 0x00040047, 0x00000020, 0x0000000b, 0x00000019, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000,
    0x00040020, 0x00000007, 0x00000007, 0x00000006, 0x00040017, 0x00000009, 0x00000006,
    0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000009, 0x0004003b, 0x0000000a,
    0x0000000b, 0x00000001, 0x0004002b, 0x00000006, 0x0000000c, 0x00000000, 0x00040020,
    0x0000000d, 0x00000001, 0x00000006, 0x0003001d, 0x00000010, 0x00000006, 0x0003001e,
    0x00000011, 0x00000010, 0x00040020, 0x00000012, 0x00000002, 0x00000011, 0x0004003b,
    0x00000012, 0x00000013, 0x00000002, 0x00040015, 0x00000014, 0x00000020, 0x00000001,
    0x0004002b, 0x00000014, 0x00000015, 0x00000000, 0x00040020, 0x00000018, 0x00000002,
    0x00000006, 0x0004002b, 0x00000006, 0x0000001b, 0x00000002, 0x0004002b, 0x00000006,
    0x0000001d, 0x00000001, 0x0006002c, 0x00000009, 0x00000020, 0x0000001d, 0x0000001d,
    0x0000001d, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
    0x00000005, 0x0004003b, 0x00000007, 0x00000008, 0x00000007, 0x00050041, 0x0000000d,
    0x0000000e, 0x0000000b, 0x0000000c, 0x0004003d, 0x00000006, 0x0000000f, 0x0000000e,
    0x0003003e, 0x00000008, 0x0000000f, 0x0004003d, 0x00000006, 0x00000016, 0x00000008,
    0x0004003d, 0x00000006, 0x00000017, 0x00000008, 0x00060041, 0x00000018, 0x00000019,
    0x00000013, 0x00000015, 0x00000017, 0x0004003d, 0x00000006, 0x0000001a, 0x00000019,
    0x00050084, 0x00000006, 0x0000001c, 0x0000001a, 0x0000001b, 0x00050080, 0x00000006,
    0x0000001e, 0x0000001c, 0x0000001d, 0x00060041, 0x00000018, 0x0000001f, 0x00000013,
    0x00000015, 0x00000016, 0x0003003e, 0x0000001f, 0x0000001e, 0x000100fd, 0x00010038};

// Embedded SPIR-V bytecode for vertex shader:
// layout(location = 0) in vec2 inPos;
// layout(location = 1) in vec3 inColor;
// layout(location = 0) out vec3 fragColor;
// void main() { gl_Position = vec4(inPos, 0.0, 1.0); fragColor = inColor; }
const uint32_t kTriangleVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000021, 0x00000000, 0x00020011, 0x00000001,
    0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
    0x00000000, 0x00000001, 0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000,
    0x0000000d, 0x00000012, 0x0000001d, 0x0000001f, 0x00030003, 0x00000002, 0x000001c2,
    0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c,
    0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45,
    0x64756c63, 0x69645f65, 0x74636572, 0x00657669, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00060005, 0x0000000b, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000,
    0x00060006, 0x0000000b, 0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006,
    0x0000000b, 0x00000001, 0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000, 0x00070006,
    0x0000000b, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369, 0x0065636e, 0x00070006,
    0x0000000b, 0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e, 0x00030005,
    0x0000000d, 0x00000000, 0x00040005, 0x00000012, 0x6f506e69, 0x00000073, 0x00050005,
    0x0000001d, 0x67617266, 0x6f6c6f43, 0x00000072, 0x00040005, 0x0000001f, 0x6f436e69,
    0x00726f6c, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b, 0x00000000,
    0x0000000b, 0x00000000, 0x00050048, 0x0000000b, 0x00000001, 0x0000000b, 0x00000001,
    0x00050048, 0x0000000b, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b,
    0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x00000012, 0x0000001e, 0x00000000,
    0x00040047, 0x0000001d, 0x0000001e, 0x00000000, 0x00040047, 0x0000001f, 0x0000001e,
    0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015,
    0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001,
    0x0004001c, 0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007,
    0x00000006, 0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000003, 0x0000000b,
    0x0004003b, 0x0000000c, 0x0000000d, 0x00000003, 0x00040015, 0x0000000e, 0x00000020,
    0x00000001, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000000, 0x00040017, 0x00000010,
    0x00000006, 0x00000002, 0x00040020, 0x00000011, 0x00000001, 0x00000010, 0x0004003b,
    0x00000011, 0x00000012, 0x00000001, 0x0004002b, 0x00000006, 0x00000014, 0x00000000,
    0x0004002b, 0x00000006, 0x00000015, 0x3f800000, 0x00040020, 0x00000019, 0x00000003,
    0x00000007, 0x00040017, 0x0000001b, 0x00000006, 0x00000003, 0x00040020, 0x0000001c,
    0x00000003, 0x0000001b, 0x0004003b, 0x0000001c, 0x0000001d, 0x00000003, 0x00040020,
    0x0000001e, 0x00000001, 0x0000001b, 0x0004003b, 0x0000001e, 0x0000001f, 0x00000001,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x00000010, 0x00000013, 0x00000012, 0x00050051, 0x00000006, 0x00000016,
    0x00000013, 0x00000000, 0x00050051, 0x00000006, 0x00000017, 0x00000013, 0x00000001,
    0x00070050, 0x00000007, 0x00000018, 0x00000016, 0x00000017, 0x00000014, 0x00000015,
    0x00050041, 0x00000019, 0x0000001a, 0x0000000d, 0x0000000f, 0x0003003e, 0x0000001a,
    0x00000018, 0x0004003d, 0x0000001b, 0x00000020, 0x0000001f, 0x0003003e, 0x0000001d,
    0x00000020, 0x000100fd, 0x00010038};

// Embedded SPIR-V bytecode for fragment shader:
// layout(location = 0) in vec3 fragColor;
// layout(location = 0) out vec4 outColor;
// void main() { outColor = vec4(fragColor, 1.0); }
const uint32_t kTriangleFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000013, 0x00000000, 0x00020011, 0x00000001,
    0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
    0x00000000, 0x00000001, 0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000,
    0x00000009, 0x0000000c, 0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002,
    0x000001c2, 0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79,
    0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f,
    0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x4374756f, 0x726f6c6f, 0x00000000,
    0x00050005, 0x0000000c, 0x67617266, 0x6f6c6f43, 0x00000072, 0x00040047, 0x00000009,
    0x0000001e, 0x00000000, 0x00040047, 0x0000000c, 0x0000001e, 0x00000000, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a,
    0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b,
    0x0000000b, 0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000e, 0x3f800000,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x00050051, 0x00000006, 0x0000000f,
    0x0000000d, 0x00000000, 0x00050051, 0x00000006, 0x00000010, 0x0000000d, 0x00000001,
    0x00050051, 0x00000006, 0x00000011, 0x0000000d, 0x00000002, 0x00070050, 0x00000007,
    0x00000012, 0x0000000f, 0x00000010, 0x00000011, 0x0000000e, 0x0003003e, 0x00000009,
    0x00000012, 0x000100fd, 0x00010038};

uint32_t FindMemoryType(VkPhysicalDevice phys_dev, const VulkanDispatchTable *table,
                        uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props{};
    table->vkGetPhysicalDeviceMemoryProperties(phys_dev, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

} // namespace

class VulkanPipelineTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        ASSERT_TRUE(VulkanBridge::Instance().Initialize());
        table = VulkanBridge::Instance().GetDispatchTable();
        ASSERT_NE(table, nullptr);

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "PortagePipelineTest";
        app_info.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo inst_info{};
        inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        inst_info.pApplicationInfo = &app_info;

        ASSERT_EQ(table->vkCreateInstance(&inst_info, nullptr, &instance), VK_SUCCESS);
        ASSERT_NE(instance, VK_NULL_HANDLE);

        uint32_t gpu_count = 0;
        ASSERT_EQ(table->vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr), VK_SUCCESS);
        ASSERT_GT(gpu_count, 0u);
        std::vector<VkPhysicalDevice> gpus(gpu_count);
        ASSERT_EQ(table->vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()), VK_SUCCESS);
        physical_device = gpus[0];

        // Find queue family supporting compute & graphics
        uint32_t qf_count = 0;
        table->vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, nullptr);
        ASSERT_GT(qf_count, 0u);
        std::vector<VkQueueFamilyProperties> qf_props(qf_count);
        table->vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count,
                                                        qf_props.data());

        for (uint32_t i = 0; i < qf_count; ++i) {
            if ((qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                queue_family_index = i;
                break;
            }
        }

        float priority = 1.0f;
        VkDeviceQueueCreateInfo q_info{};
        q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q_info.queueFamilyIndex = queue_family_index;
        q_info.queueCount = 1;
        q_info.pQueuePriorities = &priority;

        const char *dev_exts[] = {"VK_KHR_swapchain",
                                  "VK_ANDROID_external_memory_android_hardware_buffer",
                                  "VK_KHR_external_fence_fd"};

        VkDeviceCreateInfo dev_info{};
        dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dev_info.queueCreateInfoCount = 1;
        dev_info.pQueueCreateInfos = &q_info;
        dev_info.enabledExtensionCount = static_cast<uint32_t>(std::size(dev_exts));
        dev_info.ppEnabledExtensionNames = dev_exts;

        ASSERT_EQ(table->vkCreateDevice(physical_device, &dev_info, nullptr, &device), VK_SUCCESS);
        ASSERT_NE(device, VK_NULL_HANDLE);

        table->vkGetDeviceQueue(device, queue_family_index, 0, &queue);
        ASSERT_NE(queue, VK_NULL_HANDLE);
    }

    void TearDown() override
    {
        if (device != VK_NULL_HANDLE) {
            table->vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            table->vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }

    const VulkanDispatchTable *table{nullptr};
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    uint32_t queue_family_index{0};
};

TEST_F(VulkanPipelineTest, ProcResolutionAndTableVersion)
{
    EXPECT_GE(table->version, 3u);

    // Verify Vulkan 1.0-1.3 function pointers in dispatch table
    EXPECT_NE(table->vkCreateShaderModule, nullptr);
    EXPECT_NE(table->vkDestroyShaderModule, nullptr);
    EXPECT_NE(table->vkCreatePipelineLayout, nullptr);
    EXPECT_NE(table->vkDestroyPipelineLayout, nullptr);
    EXPECT_NE(table->vkCreateComputePipelines, nullptr);
    EXPECT_NE(table->vkCreateGraphicsPipelines, nullptr);
    EXPECT_NE(table->vkDestroyPipeline, nullptr);
    EXPECT_NE(table->vkCreateRenderPass, nullptr);
    EXPECT_NE(table->vkDestroyRenderPass, nullptr);
    EXPECT_NE(table->vkCreateFramebuffer, nullptr);
    EXPECT_NE(table->vkDestroyFramebuffer, nullptr);
    EXPECT_NE(table->vkCreateImage, nullptr);
    EXPECT_NE(table->vkDestroyImage, nullptr);
    EXPECT_NE(table->vkCreateImageView, nullptr);
    EXPECT_NE(table->vkDestroyImageView, nullptr);
    EXPECT_NE(table->vkCreateDescriptorSetLayout, nullptr);
    EXPECT_NE(table->vkCreateDescriptorPool, nullptr);
    EXPECT_NE(table->vkAllocateDescriptorSets, nullptr);
    EXPECT_NE(table->vkUpdateDescriptorSets, nullptr);
    EXPECT_NE(table->vkCreateFence, nullptr);
    EXPECT_NE(table->vkDestroyFence, nullptr);
    EXPECT_NE(table->vkWaitForFences, nullptr);
    EXPECT_NE(table->vkCreateSemaphore, nullptr);
    EXPECT_NE(table->vkDestroySemaphore, nullptr);
    EXPECT_NE(table->vkCmdDispatch, nullptr);
    EXPECT_NE(table->vkCmdDraw, nullptr);
    EXPECT_NE(table->vkGetFenceFdKHR, nullptr);
    EXPECT_NE(table->vkImportFenceFdKHR, nullptr);
    EXPECT_NE(table->vkGetMemoryAndroidHardwareBufferANDROID, nullptr);

    // Dynamic proc addr resolution
    EXPECT_NE(table->vkGetDeviceProcAddr(device, "vkCmdDispatch"), nullptr);
    EXPECT_NE(table->vkGetDeviceProcAddr(device, "vkCreateComputePipelines"), nullptr);
    EXPECT_NE(table->vkGetDeviceProcAddr(device, "vkGetFenceFdKHR"), nullptr);
    EXPECT_NE(table->vkGetDeviceProcAddr(device, "vkGetMemoryAndroidHardwareBufferANDROID"),
              nullptr);
}

TEST_F(VulkanPipelineTest, DeviceExtensionFilteringAndAdvertisement)
{
    uint32_t ext_count = 0;
    ASSERT_EQ(table->vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count,
                                                          nullptr),
              VK_SUCCESS);
    ASSERT_GT(ext_count, 0u);

    std::vector<VkExtensionProperties> exts(ext_count);
    ASSERT_EQ(table->vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count,
                                                          exts.data()),
              VK_SUCCESS);

    auto has_ext = [&](const char *name) {
        return std::ranges::any_of(exts,
                                   [&](const auto &e) { return std::strcmp(e.extensionName, name) == 0; });
    };

    EXPECT_TRUE(has_ext("VK_ANDROID_external_memory_android_hardware_buffer"));
    EXPECT_TRUE(has_ext("VK_KHR_external_memory_fd"));
    EXPECT_TRUE(has_ext("VK_KHR_external_semaphore_fd"));
    EXPECT_TRUE(has_ext("VK_KHR_external_fence_fd"));
}

TEST_F(VulkanPipelineTest, AndroidHardwareBufferImportAndQuery)
{
    AHardwareBuffer_Desc ahb_desc{};
    ahb_desc.width = 64;
    ahb_desc.height = 64;
    ahb_desc.layers = 1;
    ahb_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    ahb_desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

    AHardwareBuffer *ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&ahb_desc, &ahb), 0);
    ASSERT_NE(ahb, nullptr);

    VkAndroidHardwareBufferPropertiesANDROID ahb_props{};
    ahb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    ASSERT_EQ(table->vkGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &ahb_props),
              VK_SUCCESS);
    EXPECT_GT(ahb_props.allocationSize, 0u);

    // Import AHB into VkDeviceMemory
    VkImportAndroidHardwareBufferInfoANDROID import_info{};
    import_info.sType = static_cast<VkStructureType>(1000129000); // VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID
    import_info.buffer = ahb;

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &import_info;
    alloc_info.allocationSize = ahb_props.allocationSize;
    alloc_info.memoryTypeIndex = FindMemoryType(physical_device, table, ahb_props.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    VkDeviceMemory memory = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateMemory(device, &alloc_info, nullptr, &memory), VK_SUCCESS);
    ASSERT_NE(memory, VK_NULL_HANDLE);

    // Query back imported AHB from VkDeviceMemory
    VkMemoryGetAndroidHardwareBufferInfoANDROID query_info{};
    query_info.sType = static_cast<VkStructureType>(1000129002); // VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID
    query_info.memory = memory;

    AHardwareBuffer *retrieved_ahb = nullptr;
    ASSERT_EQ(table->vkGetMemoryAndroidHardwareBufferANDROID(device, &query_info, &retrieved_ahb),
              VK_SUCCESS);
    EXPECT_EQ(retrieved_ahb, ahb);

    table->vkFreeMemory(device, memory, nullptr);
    AHardwareBuffer_release(ahb);
}

TEST_F(VulkanPipelineTest, SyncFenceFdEpollIntegration)
{
    fs::FileDescriptorTable fd_table;
    VulkanBridge::Instance().SetFdTable(&fd_table);

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence fence = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateFence(device, &fence_info, nullptr, &fence), VK_SUCCESS);
    ASSERT_NE(fence, VK_NULL_HANDLE);

    VkFenceGetFdInfoKHR get_fd_info{};
    get_fd_info.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
    get_fd_info.fence = fence;
    get_fd_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

    int sync_fd = -1;
    ASSERT_EQ(table->vkGetFenceFdKHR(device, &get_fd_info, &sync_fd), VK_SUCCESS);
    EXPECT_GE(sync_fd, 3);

    auto desc = fd_table.Get(sync_fd);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->dev, fs::DeviceType::SyncFence);
    ASSERT_NE(desc->sync_fence, nullptr);
    EXPECT_TRUE(desc->sync_fence->IsSignaled());

    // Import fence FD into a new fence
    VkFenceCreateInfo import_fence_info{};
    import_fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence imported_fence = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateFence(device, &import_fence_info, nullptr, &imported_fence),
              VK_SUCCESS);

    VkImportFenceFdInfoKHR import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR;
    import_info.fence = imported_fence;
    import_info.fd = sync_fd;
    import_info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

    EXPECT_EQ(table->vkImportFenceFdKHR(device, &import_info), VK_SUCCESS);

    table->vkDestroyFence(device, imported_fence, nullptr);
    table->vkDestroyFence(device, fence, nullptr);
    fd_table.Close(sync_fd);
    VulkanBridge::Instance().SetFdTable(nullptr);
}

TEST_F(VulkanPipelineTest, SPIRVComputePipelineExecution)
{
    // Create Shader Module
    VkShaderModuleCreateInfo sm_info{};
    sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm_info.codeSize = sizeof(kComputeShaderSpv);
    sm_info.pCode = kComputeShaderSpv;

    VkShaderModule shader_module = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateShaderModule(device, &sm_info, nullptr, &shader_module), VK_SUCCESS);
    ASSERT_NE(shader_module, VK_NULL_HANDLE);

    // Create Descriptor Set Layout (1 SSBO)
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_info{};
    dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.bindingCount = 1;
    dsl_info.pBindings = &binding;

    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateDescriptorSetLayout(device, &dsl_info, nullptr, &desc_layout),
              VK_SUCCESS);

    // Create Pipeline Layout
    VkPipelineLayoutCreateInfo pl_info{};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 1;
    pl_info.pSetLayouts = &desc_layout;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreatePipelineLayout(device, &pl_info, nullptr, &pipeline_layout),
              VK_SUCCESS);

    // Create Compute Pipeline
    VkComputePipelineCreateInfo cp_info{};
    cp_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp_info.stage.module = shader_module;
    cp_info.stage.pName = "main";
    cp_info.layout = pipeline_layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp_info, nullptr,
                                             &pipeline),
              VK_SUCCESS);
    ASSERT_NE(pipeline, VK_NULL_HANDLE);

    // Create Storage Buffer (4 uint32 elements)
    constexpr VkDeviceSize kBufferSize = 4 * sizeof(uint32_t);
    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = kBufferSize;
    buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateBuffer(device, &buf_info, nullptr, &buffer), VK_SUCCESS);

    VkMemoryRequirements mem_reqs{};
    table->vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = FindMemoryType(
        physical_device, table, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory memory = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateMemory(device, &alloc_info, nullptr, &memory), VK_SUCCESS);
    ASSERT_EQ(table->vkBindBufferMemory(device, buffer, memory, 0), VK_SUCCESS);

    // Write initial buffer contents: [10, 20, 30, 40]
    void *mapped = nullptr;
    ASSERT_EQ(table->vkMapMemory(device, memory, 0, kBufferSize, 0, &mapped), VK_SUCCESS);
    auto *uint_ptr = static_cast<uint32_t *>(mapped);
    uint_ptr[0] = 10;
    uint_ptr[1] = 20;
    uint_ptr[2] = 30;
    uint_ptr[3] = 40;
    table->vkUnmapMemory(device, memory);

    // Create Descriptor Pool and Allocate Set
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dp_info{};
    dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp_info.maxSets = 1;
    dp_info.poolSizeCount = 1;
    dp_info.pPoolSizes = &pool_size;

    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateDescriptorPool(device, &dp_info, nullptr, &desc_pool), VK_SUCCESS);

    VkDescriptorSetAllocateInfo dsa_info{};
    dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa_info.descriptorPool = desc_pool;
    dsa_info.descriptorSetCount = 1;
    dsa_info.pSetLayouts = &desc_layout;

    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateDescriptorSets(device, &dsa_info, &desc_set), VK_SUCCESS);

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = buffer;
    dbi.offset = 0;
    dbi.range = kBufferSize;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &dbi;

    table->vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // Command Recording
    VkCommandPoolCreateInfo cp_pool_info{};
    cp_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_pool_info.queueFamilyIndex = queue_family_index;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateCommandPool(device, &cp_pool_info, nullptr, &cmd_pool), VK_SUCCESS);

    VkCommandBufferAllocateInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_info.commandPool = cmd_pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;

    VkCommandBuffer cmd_buf = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateCommandBuffers(device, &cb_info, &cmd_buf), VK_SUCCESS);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ASSERT_EQ(table->vkBeginCommandBuffer(cmd_buf, &begin_info), VK_SUCCESS);

    table->vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    table->vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                                  &desc_set, 0, nullptr);
    table->vkCmdDispatch(cmd_buf, 4, 1, 1);
    ASSERT_EQ(table->vkEndCommandBuffer(cmd_buf), VK_SUCCESS);

    // Submit and Wait
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateFence(device, &fence_info, nullptr, &fence), VK_SUCCESS);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buf;

    ASSERT_EQ(table->vkQueueSubmit(queue, 1, &submit_info, fence), VK_SUCCESS);
    ASSERT_EQ(table->vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL), VK_SUCCESS);

    // Read back and verify compute calculation: data[i] = data[i] * 2 + 1
    ASSERT_EQ(table->vkMapMemory(device, memory, 0, kBufferSize, 0, &mapped), VK_SUCCESS);
    auto *result = static_cast<const uint32_t *>(mapped);
    EXPECT_EQ(result[0], 21u); // 10 * 2 + 1
    EXPECT_EQ(result[1], 41u); // 20 * 2 + 1
    EXPECT_EQ(result[2], 61u); // 30 * 2 + 1
    EXPECT_EQ(result[3], 81u); // 40 * 2 + 1
    table->vkUnmapMemory(device, memory);

    // Cleanup
    table->vkDestroyFence(device, fence, nullptr);
    table->vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buf);
    table->vkDestroyCommandPool(device, cmd_pool, nullptr);
    table->vkDestroyDescriptorPool(device, desc_pool, nullptr);
    table->vkDestroyBuffer(device, buffer, nullptr);
    table->vkFreeMemory(device, memory, nullptr);
    table->vkDestroyPipeline(device, pipeline, nullptr);
    table->vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    table->vkDestroyDescriptorSetLayout(device, desc_layout, nullptr);
    table->vkDestroyShaderModule(device, shader_module, nullptr);
}

TEST_F(VulkanPipelineTest, TriangleRenderingAndReadback)
{
    // Create Vertex & Fragment Shader Modules
    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = sizeof(kTriangleVertSpv);
    vert_info.pCode = kTriangleVertSpv;

    VkShaderModule vert_module = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateShaderModule(device, &vert_info, nullptr, &vert_module), VK_SUCCESS);

    VkShaderModuleCreateInfo frag_info{};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = sizeof(kTriangleFragSpv);
    frag_info.pCode = kTriangleFragSpv;

    VkShaderModule frag_module = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateShaderModule(device, &frag_info, nullptr, &frag_module), VK_SUCCESS);

    // Create Render Pass
    VkAttachmentDescription color_attachment{};
    color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &color_attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateRenderPass(device, &rp_info, nullptr, &render_pass), VK_SUCCESS);

    // Create Pipeline Layout
    VkPipelineLayoutCreateInfo pl_info{};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreatePipelineLayout(device, &pl_info, nullptr, &pipeline_layout),
              VK_SUCCESS);

    // Create Graphics Pipeline
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    struct Vertex {
        float pos[2];
        float col[3];
    };

    VkVertexInputBindingDescription v_binding{};
    v_binding.binding = 0;
    v_binding.stride = sizeof(Vertex);
    v_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription v_attribs[2]{};
    v_attribs[0].location = 0;
    v_attribs[0].binding = 0;
    v_attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
    v_attribs[0].offset = offsetof(Vertex, pos);

    v_attribs[1].location = 1;
    v_attribs[1].binding = 0;
    v_attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    v_attribs[1].offset = offsetof(Vertex, col);

    VkPipelineVertexInputStateCreateInfo vi_state{};
    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_state.vertexBindingDescriptionCount = 1;
    vi_state.pVertexBindingDescriptions = &v_binding;
    vi_state.vertexAttributeDescriptionCount = 2;
    vi_state.pVertexAttributeDescriptions = v_attribs;

    VkPipelineInputAssemblyStateCreateInfo ia_state{};
    ia_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {64, 64}};

    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.pViewports = &viewport;
    vp_state.scissorCount = 1;
    vp_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster_state{};
    raster_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_NONE;
    raster_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster_state.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms_state{};
    ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb_state{};
    cb_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_state.attachmentCount = 1;
    cb_state.pAttachments = &cb_att;

    VkGraphicsPipelineCreateInfo gp_info{};
    gp_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_info.stageCount = 2;
    gp_info.pStages = stages;
    gp_info.pVertexInputState = &vi_state;
    gp_info.pInputAssemblyState = &ia_state;
    gp_info.pViewportState = &vp_state;
    gp_info.pRasterizationState = &raster_state;
    gp_info.pMultisampleState = &ms_state;
    gp_info.pColorBlendState = &cb_state;
    gp_info.layout = pipeline_layout;
    gp_info.renderPass = render_pass;

    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp_info, nullptr,
                                              &graphics_pipeline),
              VK_SUCCESS);
    ASSERT_NE(graphics_pipeline, VK_NULL_HANDLE);

    // Create Offscreen Render Target Image (64x64)
    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = {64, 64, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateImage(device, &img_info, nullptr, &image), VK_SUCCESS);

    VkMemoryRequirements img_mem_reqs{};
    table->vkGetImageMemoryRequirements(device, image, &img_mem_reqs);

    VkMemoryAllocateInfo img_alloc{};
    img_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_alloc.allocationSize = img_mem_reqs.size;
    img_alloc.memoryTypeIndex = FindMemoryType(physical_device, table, img_mem_reqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory img_memory = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateMemory(device, &img_alloc, nullptr, &img_memory), VK_SUCCESS);
    ASSERT_EQ(table->vkBindImageMemory(device, image, img_memory, 0), VK_SUCCESS);

    // Create Image View & Framebuffer
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateImageView(device, &view_info, nullptr, &image_view), VK_SUCCESS);

    VkFramebufferCreateInfo fb_info{};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &image_view;
    fb_info.width = 64;
    fb_info.height = 64;
    fb_info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffer), VK_SUCCESS);

    // Vertex Buffer
    const Vertex vertices[3] = {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    };

    VkBufferCreateInfo vb_info{};
    vb_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vb_info.size = sizeof(vertices);
    vb_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VkBuffer v_buffer = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateBuffer(device, &vb_info, nullptr, &v_buffer), VK_SUCCESS);

    VkMemoryRequirements vb_mem_reqs{};
    table->vkGetBufferMemoryRequirements(device, v_buffer, &vb_mem_reqs);

    VkMemoryAllocateInfo vb_alloc{};
    vb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vb_alloc.allocationSize = vb_mem_reqs.size;
    vb_alloc.memoryTypeIndex = FindMemoryType(
        physical_device, table, vb_mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory vb_memory = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateMemory(device, &vb_alloc, nullptr, &vb_memory), VK_SUCCESS);
    ASSERT_EQ(table->vkBindBufferMemory(device, v_buffer, vb_memory, 0), VK_SUCCESS);

    void *vb_mapped = nullptr;
    ASSERT_EQ(table->vkMapMemory(device, vb_memory, 0, sizeof(vertices), 0, &vb_mapped),
              VK_SUCCESS);
    std::memcpy(vb_mapped, vertices, sizeof(vertices));
    table->vkUnmapMemory(device, vb_memory);

    // Command Recording
    VkCommandPoolCreateInfo cp_pool_info{};
    cp_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_pool_info.queueFamilyIndex = queue_family_index;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateCommandPool(device, &cp_pool_info, nullptr, &cmd_pool), VK_SUCCESS);

    VkCommandBufferAllocateInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_info.commandPool = cmd_pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;

    VkCommandBuffer cmd_buf = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkAllocateCommandBuffers(device, &cb_info, &cmd_buf), VK_SUCCESS);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ASSERT_EQ(table->vkBeginCommandBuffer(cmd_buf, &begin_info), VK_SUCCESS);

    VkClearValue clear_color{};
    clear_color.color = {{0.1f, 0.2f, 0.3f, 1.0f}};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea.extent = {64, 64};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    table->vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    table->vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

    VkDeviceSize offsets[] = {0};
    table->vkCmdBindVertexBuffers(cmd_buf, 0, 1, &v_buffer, offsets);
    table->vkCmdDraw(cmd_buf, 3, 1, 0, 0);
    table->vkCmdEndRenderPass(cmd_buf);

    ASSERT_EQ(table->vkEndCommandBuffer(cmd_buf), VK_SUCCESS);

    // Submit and Wait
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    ASSERT_EQ(table->vkCreateFence(device, &fence_info, nullptr, &fence), VK_SUCCESS);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buf;

    ASSERT_EQ(table->vkQueueSubmit(queue, 1, &submit_info, fence), VK_SUCCESS);
    ASSERT_EQ(table->vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL), VK_SUCCESS);

    // Cleanup
    table->vkDestroyFence(device, fence, nullptr);
    table->vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buf);
    table->vkDestroyCommandPool(device, cmd_pool, nullptr);
    table->vkDestroyBuffer(device, v_buffer, nullptr);
    table->vkFreeMemory(device, vb_memory, nullptr);
    table->vkDestroyFramebuffer(device, framebuffer, nullptr);
    table->vkDestroyImageView(device, image_view, nullptr);
    table->vkDestroyImage(device, image, nullptr);
    table->vkFreeMemory(device, img_memory, nullptr);
    table->vkDestroyPipeline(device, graphics_pipeline, nullptr);
    table->vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    table->vkDestroyRenderPass(device, render_pass, nullptr);
    table->vkDestroyShaderModule(device, frag_module, nullptr);
    table->vkDestroyShaderModule(device, vert_module, nullptr);
}
