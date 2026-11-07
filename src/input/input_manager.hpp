#pragma once

#include "input_types.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string_view>

namespace portage::input {

struct InputDeviceStats {
    uint64_t key_events_posted{0};
    uint64_t pointer_events_posted{0};
    uint64_t events_read{0};
};

class InputManager {
  public:
    static InputManager &Instance() noexcept;

    InputManager(const InputManager &) = delete;
    InputManager &operator=(const InputManager &) = delete;

    void Reset() noexcept;

    // Enqueue event APIs (invoked by Win32 window message loop or test harnesses)
    void PostKeyEvent(uint16_t linux_key, bool pressed) noexcept;
    void PostPointerEvent(int32_t client_x, int32_t client_y, int32_t win_w, int32_t win_h,
                          bool pressed) noexcept;
    void PostWin32Key(uint32_t vk_code, bool pressed) noexcept;

    // Guest read & poll APIs
    int64_t ReadEvents(int device_index, LinuxInputEvent *out_events, size_t max_events,
                       bool nonblocking) noexcept;
    int64_t WriteEvents(int device_index, const LinuxInputEvent *events, size_t count) noexcept;
    bool HasEvents(int device_index) const noexcept;

    // Evdev ioctls
    int HandleIoctl(int device_index, uint32_t cmd, void *arg) noexcept;

    // Statistics
    InputDeviceStats GetStats() const noexcept;

  private:
    InputManager() noexcept;
    ~InputManager() noexcept = default;

    static LinuxTimeval CurrentTimestamp() noexcept;

    static constexpr size_t MAX_QUEUE_CAPACITY = 1024;

    mutable std::mutex m_mutex{};
    std::condition_variable m_cv{};
    std::deque<LinuxInputEvent> m_queues[2]{};

    bool m_pointer_pressed{false};
    int32_t m_current_tracking_id{0};
    int32_t m_next_tracking_id{1};

    InputDeviceStats m_stats{};
};

} // namespace portage::input
