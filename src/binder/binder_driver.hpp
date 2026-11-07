#pragma once

#include "binder_types.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace portage::binder {

struct BinderInstance {
    int id{0};
    bool is_hw{false};
    uint32_t max_threads{15};
    int64_t idle_timeout_ns{0};
};

class BinderDriver {
  public:
    static BinderDriver &Instance() noexcept;

    int CreateInstance(bool is_hw) noexcept;
    int CloseInstance(int binder_id) noexcept;

    int64_t Ioctl(int binder_id, uint32_t cmd, void *arg) noexcept;

    void Reset() noexcept;
    [[nodiscard]] size_t ActiveInstances() noexcept;

  private:
    BinderDriver() = default;
    ~BinderDriver() = default;

    BinderDriver(const BinderDriver &) = delete;
    BinderDriver &operator=(const BinderDriver &) = delete;

    std::mutex m_mutex{};
    std::unordered_map<int, BinderInstance> m_instances{};
    int m_next_id{1};
};

} // namespace portage::binder
