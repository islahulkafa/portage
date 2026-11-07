#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    // 1. Create Vulkan Instance
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PortageVulkanDeviceTest";
    app_info.applicationVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo inst_info = {0};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&inst_info, NULL, &instance);
    if (r != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        sys_exit(1);
    }

    // 2. Enumerate Physical Devices
    uint32_t phys_count = 0;
    r = vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    if (r != VK_SUCCESS || phys_count == 0) {
        vkDestroyInstance(instance, NULL);
        sys_exit(2);
    }

    VkPhysicalDevice phys_devices[4] = {0};
    if (phys_count > 4)
        phys_count = 4;
    r = vkEnumeratePhysicalDevices(instance, &phys_count, phys_devices);
    if (r != VK_SUCCESS || phys_devices[0] == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
        sys_exit(3);
    }

    // 3. Inspect Physical Device Properties
    VkPhysicalDeviceProperties props = {0};
    vkGetPhysicalDeviceProperties(phys_devices[0], &props);
    if (props.deviceName[0] == '\0') {
        vkDestroyInstance(instance, NULL);
        sys_exit(4);
    }

    // 4. Query Queue Family Properties
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_devices[0], &qf_count, NULL);
    if (qf_count == 0) {
        vkDestroyInstance(instance, NULL);
        sys_exit(5);
    }

    // 5. Create Logical Device
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo q_info = {0};
    q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_info.queueFamilyIndex = 0;
    q_info.queueCount = 1;
    q_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo dev_info = {0};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &q_info;

    VkDevice device = VK_NULL_HANDLE;
    r = vkCreateDevice(phys_devices[0], &dev_info, NULL, &device);
    if (r != VK_SUCCESS || device == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
        sys_exit(6);
    }

    // 6. Create Buffer
    VkBufferCreateInfo buf_info = {0};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = 256;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    r = vkCreateBuffer(device, &buf_info, NULL, &buffer);
    if (r != VK_SUCCESS || buffer == VK_NULL_HANDLE) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(7);
    }

    // 7. Query Memory Requirements & Find Host-Visible Memory
    VkMemoryRequirements mem_reqs = {0};
    vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props = {0};
    vkGetPhysicalDeviceMemoryProperties(phys_devices[0], &mem_props);

    int mem_type_index = -1;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_reqs.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            mem_type_index = (int)i;
            break;
        }
    }
    if (mem_type_index < 0) {
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((mem_reqs.memoryTypeBits & (1u << i)) &&
                (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                mem_type_index = (int)i;
                break;
            }
        }
    }
    if (mem_type_index < 0) {
        vkDestroyBuffer(device, buffer, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(8);
    }

    // 8. Allocate and Bind Memory
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = (uint32_t)mem_type_index;

    VkDeviceMemory dev_mem = VK_NULL_HANDLE;
    r = vkAllocateMemory(device, &alloc_info, NULL, &dev_mem);
    if (r != VK_SUCCESS || dev_mem == VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(9);
    }

    r = vkBindBufferMemory(device, buffer, dev_mem, 0);
    if (r != VK_SUCCESS) {
        vkFreeMemory(device, dev_mem, NULL);
        vkDestroyBuffer(device, buffer, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(10);
    }

    // 9. Map Memory, Write Magic Pattern, Verify
    void *mapped_ptr = NULL;
    r = vkMapMemory(device, dev_mem, 0, 256, 0, &mapped_ptr);
    if (r != VK_SUCCESS || mapped_ptr == NULL) {
        vkFreeMemory(device, dev_mem, NULL);
        vkDestroyBuffer(device, buffer, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(11);
    }

    uint32_t *word_ptr = (uint32_t *)mapped_ptr;
    word_ptr[0] = 0xCAFEBABE;
    word_ptr[1] = 0xDEADBEEF;

    if (word_ptr[0] != 0xCAFEBABE || word_ptr[1] != 0xDEADBEEF) {
        vkUnmapMemory(device, dev_mem);
        vkFreeMemory(device, dev_mem, NULL);
        vkDestroyBuffer(device, buffer, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(12);
    }

    vkUnmapMemory(device, dev_mem);

    // 10. Clean Up Resources
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, dev_mem, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    sys_exit(0);
}
