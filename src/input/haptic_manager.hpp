#pragma once

#include "input_types.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace portage::input {

class HapticManager {
  public:
    static HapticManager &Instance() noexcept;

    void Reset() noexcept;

    // Direct motor control
    bool Vibrate(int user_index, uint16_t left_motor, uint16_t right_motor,
                 uint32_t duration_ms) noexcept;
    bool Stop(int user_index) noexcept;
    void StopAll() noexcept;

    // Evdev force-feedback effect management
    int UploadEffect(const LinuxFfEffect &effect, int16_t *out_id) noexcept;
    int PlayEffect(int16_t id, int32_t value) noexcept;
    int EraseEffect(int16_t id) noexcept;

    // Inspection
    bool GetMotorSpeeds(int user_index, uint16_t &left, uint16_t &right) const noexcept;
    [[nodiscard]] size_t ActiveEffectsCount() const noexcept;

  private:
    HapticManager() noexcept;
    ~HapticManager() noexcept;

    HapticManager(const HapticManager &) = delete;
    HapticManager &operator=(const HapticManager &) = delete;

    void InitXInput() noexcept;
    void ApplyXInputState(int user_index, uint16_t left, uint16_t right) noexcept;

    mutable std::mutex mutex_{};
    void *xinput_module_{nullptr};
    void *xinput_set_state_fn_{nullptr};

    struct ControllerHapticState {
        uint16_t left_speed{0};
        uint16_t right_speed{0};
        std::chrono::steady_clock::time_point stop_time{};
        bool active{false};
    };

    ControllerHapticState controllers_[4]{};
    std::unordered_map<int16_t, LinuxFfEffect> effects_{};
    int16_t next_effect_id_{1};
};

} // namespace portage::input
