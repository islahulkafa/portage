#include "vulkan/vulkan_bridge.hpp"
#include <gtest/gtest.h>

using namespace portage::vulkan;

class VulkanBridgeTest : public ::testing::Test {
  protected:
    void SetUp() override { VulkanBridge::Instance().Initialize(); }
};

TEST_F(VulkanBridgeTest, InitializesAndPopulatesDispatchTable)
{
    auto &bridge = VulkanBridge::Instance();
    ASSERT_TRUE(bridge.IsInitialized());

    const auto *table = bridge.GetDispatchTable();
    ASSERT_NE(table, nullptr);
    EXPECT_GE(table->version, 1u);
    EXPECT_NE(table->vkGetInstanceProcAddr, nullptr);
    EXPECT_NE(table->vkGetDeviceProcAddr, nullptr);
    EXPECT_NE(table->vkCreateInstance, nullptr);
    EXPECT_NE(table->vkDestroyInstance, nullptr);
    EXPECT_NE(table->vkEnumeratePhysicalDevices, nullptr);
    EXPECT_NE(table->vkGetPhysicalDeviceProperties, nullptr);
    EXPECT_NE(table->vkCreateDevice, nullptr);
    EXPECT_NE(table->vkDestroyDevice, nullptr);
    EXPECT_NE(table->vkCreateAndroidSurfaceKHR, nullptr);
    EXPECT_NE(table->vkCreateSwapchainKHR, nullptr);
    EXPECT_NE(table->vkQueuePresentKHR, nullptr);
}

TEST_F(VulkanBridgeTest, ResolvesProcAddrCorrectly)
{
    const auto *table = VulkanBridge::Instance().GetDispatchTable();
    ASSERT_NE(table, nullptr);

    auto pfn_create_inst = table->vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
    EXPECT_NE(pfn_create_inst, nullptr);

    auto pfn_bogus = table->vkGetInstanceProcAddr(nullptr, "vkInvalidFunctionXYZ");
    EXPECT_EQ(pfn_bogus, nullptr);
}

TEST_F(VulkanBridgeTest, CreatesHostInstanceAndEnumeratesPhysicalDevices)
{
    const auto *table = VulkanBridge::Instance().GetDispatchTable();
    ASSERT_NE(table, nullptr);

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PortageUnitTest";
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo inst_info{};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = table->vkCreateInstance(&inst_info, nullptr, &inst);
    ASSERT_EQ(r, VK_SUCCESS);
    ASSERT_NE(inst, VK_NULL_HANDLE);

    uint32_t count = 0;
    r = table->vkEnumeratePhysicalDevices(inst, &count, nullptr);
    EXPECT_EQ(r, VK_SUCCESS);
    EXPECT_GT(count, 0u);

    table->vkDestroyInstance(inst, nullptr);
}

TEST_F(VulkanBridgeTest, CreatesSurfaceAndVirtualSwapchain)
{
    const auto *table = VulkanBridge::Instance().GetDispatchTable();
    ASSERT_NE(table, nullptr);

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PortageWsiTest";
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo inst_info{};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = table->vkCreateInstance(&inst_info, nullptr, &inst);
    ASSERT_EQ(r, VK_SUCCESS);

    VkAndroidSurfaceCreateInfoKHR surf_info{};
    surf_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    r = table->vkCreateAndroidSurfaceKHR(inst, &surf_info, nullptr, &surface);
    EXPECT_EQ(r, VK_SUCCESS);
    EXPECT_NE(surface, VK_NULL_HANDLE);

    uint32_t dev_count = 0;
    table->vkEnumeratePhysicalDevices(inst, &dev_count, nullptr);
    ASSERT_GT(dev_count, 0u);
    std::vector<VkPhysicalDevice> gpus(dev_count);
    table->vkEnumeratePhysicalDevices(inst, &dev_count, gpus.data());

    VkBool32 supported = VK_FALSE;
    EXPECT_EQ(table->vkGetPhysicalDeviceSurfaceSupportKHR(gpus[0], 0, surface, &supported),
              VK_SUCCESS);
    EXPECT_EQ(supported, VK_TRUE);

    VkSurfaceCapabilitiesKHR caps{};
    EXPECT_EQ(table->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpus[0], surface, &caps),
              VK_SUCCESS);
    EXPECT_GE(caps.minImageCount, 1u);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo q_info{};
    q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_info.queueFamilyIndex = 0;
    q_info.queueCount = 1;
    q_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo dev_info{};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &q_info;

    VkDevice device = VK_NULL_HANDLE;
    r = table->vkCreateDevice(gpus[0], &dev_info, nullptr, &device);
    ASSERT_EQ(r, VK_SUCCESS);

    VkSwapchainCreateInfoKHR swap_info{};
    swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_info.surface = surface;
    swap_info.minImageCount = 2;
    swap_info.imageExtent = caps.currentExtent;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    r = table->vkCreateSwapchainKHR(device, &swap_info, nullptr, &swapchain);
    EXPECT_EQ(r, VK_SUCCESS);
    EXPECT_NE(swapchain, VK_NULL_HANDLE);

    uint32_t img_count = 0;
    EXPECT_EQ(table->vkGetSwapchainImagesKHR(device, swapchain, &img_count, nullptr), VK_SUCCESS);
    EXPECT_GE(img_count, 2u);

    uint32_t img_idx = 0;
    EXPECT_EQ(table->vkAcquireNextImageKHR(device, swapchain, 1000000000ull, VK_NULL_HANDLE,
                                           VK_NULL_HANDLE, &img_idx),
              VK_SUCCESS);

    VkQueue queue = VK_NULL_HANDLE;
    table->vkGetDeviceQueue(device, 0, 0, &queue);

    VkPresentInfoKHR pres_info{};
    pres_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pres_info.swapchainCount = 1;
    pres_info.pSwapchains = &swapchain;
    pres_info.pImageIndices = &img_idx;
    EXPECT_EQ(table->vkQueuePresentKHR(queue, &pres_info), VK_SUCCESS);

    table->vkDestroySwapchainKHR(device, swapchain, nullptr);
    table->vkDestroyDevice(device, nullptr);
    table->vkDestroySurfaceKHR(inst, surface, nullptr);
    table->vkDestroyInstance(inst, nullptr);
}
