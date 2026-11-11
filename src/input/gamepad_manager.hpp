#pragma once

#include <cstdint>
#include <mutex>

namespace portage::ndk {
class InputQueueImpl;
}

namespace portage::input {

enum GamepadButton : uint32_t {
    GAMEPAD_BUTTON_A = 1u << 0,
    GAMEPAD_BUTTON_B = 1u << 1,
    GAMEPAD_BUTTON_X = 1u << 2,
    GAMEPAD_BUTTON_Y = 1u << 3,
    GAMEPAD_BUTTON_L1 = 1u << 4,
    GAMEPAD_BUTTON_R1 = 1u << 5,
    GAMEPAD_BUTTON_THUMBL = 1u << 6,
    GAMEPAD_BUTTON_THUMBR = 1u << 7,
    GAMEPAD_BUTTON_START = 1u << 8,
    GAMEPAD_BUTTON_SELECT = 1u << 9,
    GAMEPAD_BUTTON_MODE = 1u << 10,
    GAMEPAD_BUTTON_DPAD_UP = 1u << 11,
    GAMEPAD_BUTTON_DPAD_DOWN = 1u << 12,
    GAMEPAD_BUTTON_DPAD_LEFT = 1u << 13,
    GAMEPAD_BUTTON_DPAD_RIGHT = 1u << 14,
};

struct GamepadState {
    float left_stick_x{0.0f};  // -1.0 to 1.0 (AXIS_X)
    float left_stick_y{0.0f};  // -1.0 to 1.0 (AXIS_Y)
    float right_stick_x{0.0f}; // -1.0 to 1.0 (AXIS_Z)
    float right_stick_y{0.0f}; // -1.0 to 1.0 (AXIS_RZ)
    float left_trigger{0.0f};  // 0.0 to 1.0 (AXIS_LTRIGGER)
    float right_trigger{0.0f}; // 0.0 to 1.0 (AXIS_RTRIGGER)
    float hat_x{0.0f};         // -1.0 to 1.0 (AXIS_HAT_X)
    float hat_y{0.0f};         // -1.0 to 1.0 (AXIS_HAT_Y)
    uint32_t buttons{0};
    bool connected{false};
};

class GamepadManager {
  public:
    static GamepadManager &Instance() noexcept;

    void Reset() noexcept;

    // Polling and synthetic injection
    void PollControllers() noexcept;
    void InjectSyntheticState(int user_index, const GamepadState &state) noexcept;

    // Queue connection
    void SetInputQueue(ndk::InputQueueImpl *queue) noexcept;

    // State inspection
    [[nodiscard]] GamepadState GetState(int user_index) const noexcept;
    [[nodiscard]] bool IsConnected(int user_index) const noexcept;

  private:
    GamepadManager() noexcept;
    ~GamepadManager() noexcept;

    GamepadManager(const GamepadManager &) = delete;
    GamepadManager &operator=(const GamepadManager &) = delete;

    void InitXInput() noexcept;
    void DispatchStateChange(int user_index, const GamepadState &old_state,
                             const GamepadState &new_state) noexcept;

    mutable std::mutex mutex_{};
    void *xinput_module_{nullptr};
    void *xinput_get_state_fn_{nullptr};
    GamepadState states_[4]{};
    ndk::InputQueueImpl *input_queue_{nullptr};
};

} // namespace portage::input
