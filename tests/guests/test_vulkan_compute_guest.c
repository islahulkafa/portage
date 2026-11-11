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

static const uint32_t kComputeShaderSpv[] = {
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

static uint32_t find_memory_type(VkPhysicalDevice phys_dev, uint32_t type_filter,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props = {0};
    vkGetPhysicalDeviceMemoryProperties(phys_dev, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    // Create Instance
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "GuestComputeTest";
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo inst_info = {0};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&inst_info, NULL, &instance);
    if (r != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        sys_exit(1);
    }

    uint32_t phys_count = 0;
    r = vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    if (r != VK_SUCCESS || phys_count == 0) {
        vkDestroyInstance(instance, NULL);
        sys_exit(2);
    }

    VkPhysicalDevice phys_dev = VK_NULL_HANDLE;
    r = vkEnumeratePhysicalDevices(instance, &phys_count, &phys_dev);
    if (r != VK_SUCCESS || phys_dev == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
        sys_exit(3);
    }

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &qf_count, NULL);
    if (qf_count == 0) {
        vkDestroyInstance(instance, NULL);
        sys_exit(4);
    }

    VkQueueFamilyProperties qf_props[16] = {0};
    if (qf_count > 16)
        qf_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &qf_count, qf_props);

    uint32_t queue_family = 0;
    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queue_family = i;
            break;
        }
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo q_info = {0};
    q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_info.queueFamilyIndex = queue_family;
    q_info.queueCount = 1;
    q_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo dev_info = {0};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &q_info;

    VkDevice device = VK_NULL_HANDLE;
    r = vkCreateDevice(phys_dev, &dev_info, NULL, &device);
    if (r != VK_SUCCESS || device == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
        sys_exit(5);
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // Create Shader Module
    VkShaderModuleCreateInfo sm_info = {0};
    sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm_info.codeSize = sizeof(kComputeShaderSpv);
    sm_info.pCode = kComputeShaderSpv;

    VkShaderModule shader_module = VK_NULL_HANDLE;
    r = vkCreateShaderModule(device, &sm_info, NULL, &shader_module);
    if (r != VK_SUCCESS || shader_module == VK_NULL_HANDLE) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        sys_exit(6);
    }

    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_info = {0};
    dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.bindingCount = 1;
    dsl_info.pBindings = &binding;

    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
    r = vkCreateDescriptorSetLayout(device, &dsl_info, NULL, &desc_layout);
    if (r != VK_SUCCESS) {
        sys_exit(7);
    }

    VkPipelineLayoutCreateInfo pl_info = {0};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 1;
    pl_info.pSetLayouts = &desc_layout;

    VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
    r = vkCreatePipelineLayout(device, &pl_info, NULL, &pipe_layout);
    if (r != VK_SUCCESS) {
        sys_exit(8);
    }

    VkComputePipelineCreateInfo cp_info = {0};
    cp_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp_info.stage.module = shader_module;
    cp_info.stage.pName = "main";
    cp_info.layout = pipe_layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp_info, NULL, &pipeline);
    if (r != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        sys_exit(9);
    }

    // Storage Buffer
    VkDeviceSize buf_size = 4 * sizeof(uint32_t);
    VkBufferCreateInfo buf_info = {0};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = buf_size;
    buf_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    r = vkCreateBuffer(device, &buf_info, NULL, &buffer);
    if (r != VK_SUCCESS) {
        sys_exit(10);
    }

    VkMemoryRequirements mem_reqs = {0};
    vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        phys_dev, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory memory = VK_NULL_HANDLE;
    r = vkAllocateMemory(device, &alloc_info, NULL, &memory);
    if (r != VK_SUCCESS) {
        sys_exit(11);
    }
    vkBindBufferMemory(device, buffer, memory, 0);

    // Initialize buffer: [5, 10, 15, 20]
    void *mapped = NULL;
    r = vkMapMemory(device, memory, 0, buf_size, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        sys_exit(12);
    }
    uint32_t *data = (uint32_t *)mapped;
    data[0] = 5;
    data[1] = 10;
    data[2] = 15;
    data[3] = 20;
    vkUnmapMemory(device, memory);

    // Descriptor Pool & Set
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dp_info = {0};
    dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp_info.maxSets = 1;
    dp_info.poolSizeCount = 1;
    dp_info.pPoolSizes = &pool_size;

    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    r = vkCreateDescriptorPool(device, &dp_info, NULL, &desc_pool);
    if (r != VK_SUCCESS) {
        sys_exit(13);
    }

    VkDescriptorSetAllocateInfo dsa_info = {0};
    dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa_info.descriptorPool = desc_pool;
    dsa_info.descriptorSetCount = 1;
    dsa_info.pSetLayouts = &desc_layout;

    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    r = vkAllocateDescriptorSets(device, &dsa_info, &desc_set);
    if (r != VK_SUCCESS) {
        sys_exit(14);
    }

    VkDescriptorBufferInfo dbi = {0};
    dbi.buffer = buffer;
    dbi.offset = 0;
    dbi.range = buf_size;

    VkWriteDescriptorSet write = {0};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &dbi;

    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

    // Command Recording
    VkCommandPoolCreateInfo cp_pool_info = {0};
    cp_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp_pool_info.queueFamilyIndex = queue_family;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    r = vkCreateCommandPool(device, &cp_pool_info, NULL, &cmd_pool);
    if (r != VK_SUCCESS) {
        sys_exit(15);
    }

    VkCommandBufferAllocateInfo cb_info = {0};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_info.commandPool = cmd_pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;

    VkCommandBuffer cmd_buf = VK_NULL_HANDLE;
    r = vkAllocateCommandBuffers(device, &cb_info, &cmd_buf);
    if (r != VK_SUCCESS) {
        sys_exit(16);
    }

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd_buf, &begin_info);

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout, 0, 1, &desc_set,
                            0, NULL);
    vkCmdDispatch(cmd_buf, 4, 1, 1);
    vkEndCommandBuffer(cmd_buf);

    // Submit with Fence
    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    r = vkCreateFence(device, &fence_info, NULL, &fence);
    if (r != VK_SUCCESS) {
        sys_exit(17);
    }

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buf;

    r = vkQueueSubmit(queue, 1, &submit_info, fence);
    if (r != VK_SUCCESS) {
        sys_exit(18);
    }

    r = vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL);
    if (r != VK_SUCCESS) {
        sys_exit(19);
    }

    // Verify Compute output: data[i] = data[i] * 2 + 1
    r = vkMapMemory(device, memory, 0, buf_size, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        sys_exit(20);
    }
    const uint32_t *res = (const uint32_t *)mapped;
    if (res[0] != 11 || res[1] != 21 || res[2] != 31 || res[3] != 41) {
        vkUnmapMemory(device, memory);
        sys_exit(21);
    }
    vkUnmapMemory(device, memory);

    // Cleanup
    vkDestroyFence(device, fence, NULL);
    vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buf);
    vkDestroyCommandPool(device, cmd_pool, NULL);
    vkDestroyDescriptorPool(device, desc_pool, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipe_layout, NULL);
    vkDestroyDescriptorSetLayout(device, desc_layout, NULL);
    vkDestroyShaderModule(device, shader_module, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    sys_exit(0);
}
