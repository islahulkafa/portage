#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

struct ANativeWindow {
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t ref_count;
};

extern void ANativeWindow_acquire(struct ANativeWindow *window);
extern void ANativeWindow_release(struct ANativeWindow *window);
extern int32_t ANativeWindow_getWidth(struct ANativeWindow *window);
extern int32_t ANativeWindow_getHeight(struct ANativeWindow *window);
extern struct ANativeWindow *ANativeWindow_create(int32_t width, int32_t height);

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

void *memset(void *dest, int val, size_t len)
{
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < len; ++i) {
        p[i] = (uint8_t)val;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t len)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
    return dest;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    // 1. ANativeWindow API verification
    struct ANativeWindow *window = ANativeWindow_create(800, 600);
    if (!window) {
        sys_exit(1);
    }
    if (ANativeWindow_getWidth(window) != 800 || ANativeWindow_getHeight(window) != 600) {
        sys_exit(2);
    }
    ANativeWindow_acquire(window);

    // 2. Initialize Vulkan instance with surface extensions
    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PortagePresentationGuest";
    app_info.apiVersion = VK_API_VERSION_1_0;

    const char ext_surf[] = "VK_KHR_surface";
    const char ext_android[] = "VK_KHR_android_surface";
    const char *inst_exts[2];
    inst_exts[0] = ext_surf;
    inst_exts[1] = ext_android;

    VkInstanceCreateInfo inst_info;
    memset(&inst_info, 0, sizeof(inst_info));
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;
    inst_info.enabledExtensionCount = 2;
    inst_info.ppEnabledExtensionNames = inst_exts;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&inst_info, NULL, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        sys_exit(3);
    }

    // 3. Create Android Surface
    VkAndroidSurfaceCreateInfoKHR surf_info;
    memset(&surf_info, 0, sizeof(surf_info));
    surf_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surf_info.window = window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateAndroidSurfaceKHR(instance, &surf_info, NULL, &surface) != VK_SUCCESS ||
        surface == VK_NULL_HANDLE) {
        sys_exit(4);
    }

    // 4. Enumerate GPU and verify presentation support
    uint32_t gpu_count = 0;
    if (vkEnumeratePhysicalDevices(instance, &gpu_count, NULL) != VK_SUCCESS || gpu_count == 0) {
        sys_exit(5);
    }

    VkPhysicalDevice gpus[4];
    if (gpu_count > 4)
        gpu_count = 4;
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);

    VkBool32 supported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(gpus[0], 0, surface, &supported) != VK_SUCCESS ||
        supported != VK_TRUE) {
        sys_exit(6);
    }

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpus[0], surface, &caps) != VK_SUCCESS) {
        sys_exit(7);
    }

    // 5. Create logical device with VK_KHR_swapchain
    float priority = 1.0f;
    VkDeviceQueueCreateInfo q_info;
    memset(&q_info, 0, sizeof(q_info));
    q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_info.queueFamilyIndex = 0;
    q_info.queueCount = 1;
    q_info.pQueuePriorities = &priority;

    const char ext_swapchain[] = "VK_KHR_swapchain";
    const char *dev_exts[1];
    dev_exts[0] = ext_swapchain;

    VkDeviceCreateInfo dev_info;
    memset(&dev_info, 0, sizeof(dev_info));
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &q_info;
    dev_info.enabledExtensionCount = 1;
    dev_info.ppEnabledExtensionNames = dev_exts;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(gpus[0], &dev_info, NULL, &device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE) {
        sys_exit(8);
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    // 6. Create Swapchain
    VkSwapchainCreateInfoKHR swap_info;
    memset(&swap_info, 0, sizeof(swap_info));
    swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_info.surface = surface;
    swap_info.minImageCount = 2;
    swap_info.imageExtent = caps.currentExtent;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swap_info, NULL, &swapchain) != VK_SUCCESS ||
        swapchain == VK_NULL_HANDLE) {
        sys_exit(9);
    }

    // 7. Enumerate images and acquire
    uint32_t img_count = 0;
    if (vkGetSwapchainImagesKHR(device, swapchain, &img_count, NULL) != VK_SUCCESS ||
        img_count < 2) {
        sys_exit(10);
    }

    VkImage images[8];
    if (img_count > 8)
        img_count = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &img_count, images);

    uint32_t image_index = 0;
    if (vkAcquireNextImageKHR(device, swapchain, 1000000000ull, VK_NULL_HANDLE, VK_NULL_HANDLE,
                              &image_index) != VK_SUCCESS) {
        sys_exit(11);
    }

    // 8. Present frame to surface
    VkPresentInfoKHR pres_info;
    memset(&pres_info, 0, sizeof(pres_info));
    pres_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pres_info.swapchainCount = 1;
    pres_info.pSwapchains = &swapchain;
    pres_info.pImageIndices = &image_index;

    if (vkQueuePresentKHR(queue, &pres_info) != VK_SUCCESS) {
        sys_exit(12);
    }

    // 9. Teardown
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    ANativeWindow_release(window);
    ANativeWindow_release(window);

    sys_exit(0);
}
