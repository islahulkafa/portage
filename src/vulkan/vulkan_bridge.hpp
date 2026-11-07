#pragma once

#include "vulkan_types.hpp"

#include <mutex>

namespace portage::vulkan {

class VulkanBridge {
  public:
    static VulkanBridge &Instance() noexcept;

    bool Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const VulkanDispatchTable *GetDispatchTable() const noexcept;

  private:
    VulkanBridge() = default;
    ~VulkanBridge() = default;

    VulkanBridge(const VulkanBridge &) = delete;
    VulkanBridge &operator=(const VulkanBridge &) = delete;

    std::mutex m_mutex{};
    bool m_initialized{false};
    void *m_module{nullptr};
    VulkanDispatchTable m_dispatch_table{};
};

} // namespace portage::vulkan
