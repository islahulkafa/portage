#pragma once

#include "vulkan_types.hpp"

#include <mutex>

namespace portage::fs {
class FileDescriptorTable;
}

namespace portage::vulkan {

class VulkanBridge {
  public:
    static VulkanBridge &Instance() noexcept;

    bool Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const VulkanDispatchTable *GetDispatchTable() const noexcept;
    [[nodiscard]] void *GetModule() const noexcept { return m_module; }

    void SetFdTable(fs::FileDescriptorTable *fd_table) noexcept { m_fd_table_override = fd_table; }
    [[nodiscard]] fs::FileDescriptorTable *GetFdTable() const noexcept { return m_fd_table_override; }

  private:
    VulkanBridge() = default;
    ~VulkanBridge() = default;

    VulkanBridge(const VulkanBridge &) = delete;
    VulkanBridge &operator=(const VulkanBridge &) = delete;

    std::mutex m_mutex{};
    bool m_initialized{false};
    void *m_module{nullptr};
    VulkanDispatchTable m_dispatch_table{};
    fs::FileDescriptorTable *m_fd_table_override{nullptr};
};

} // namespace portage::vulkan
