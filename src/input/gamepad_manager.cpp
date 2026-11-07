#include "gamepad_manager.hpp"
#include "ndk/input_queue.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace portage::input {

namespace {

struct XInputGamepadStruct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XInputStateStruct {
    DWORD dwPacketNumber;
    XInputGamepadStruct Gamepad;
};

using PfnXInputGetState = DWORD(WINAPI *)(DWORD, XInputStateStruct *);

constexpr WORD XINPUT_GAMEPAD_DPAD_UP_VAL = 0x0001;
constexpr WORD XINPUT_GAMEPAD_DPAD_DOWN_VAL = 0x0002;
constexpr WORD XINPUT_GAMEPAD_DPAD_LEFT_VAL = 0x0004;
constexpr WORD XINPUT_GAMEPAD_DPAD_RIGHT_VAL = 0x0008;
constexpr WORD XINPUT_GAMEPAD_START_VAL = 0x0010;
constexpr WORD XINPUT_GAMEPAD_BACK_VAL = 0x0020;
constexpr WORD XINPUT_GAMEPAD_LEFT_THUMB_VAL = 0x0040;
constexpr WORD XINPUT_GAMEPAD_RIGHT_THUMB_VAL = 0x0080;
constexpr WORD XINPUT_GAMEPAD_LEFT_SHOULDER_VAL = 0x0100;
constexpr WORD XINPUT_GAMEPAD_RIGHT_SHOULDER_VAL = 0x0200;
constexpr WORD XINPUT_GAMEPAD_A_VAL = 0x1000;
constexpr WORD XINPUT_GAMEPAD_B_VAL = 0x2000;
constexpr WORD XINPUT_GAMEPAD_X_VAL = 0x4000;
constexpr WORD XINPUT_GAMEPAD_Y_VAL = 0x8000;

constexpr int16_t LEFT_THUMB_DEADZONE = 7849;
constexpr int16_t RIGHT_THUMB_DEADZONE = 8689;
constexpr uint8_t TRIGGER_THRESHOLD = 30;

float NormalizeThumb(int16_t val, int16_t deadzone) noexcept
{
    if (std::abs(val) <= deadzone) {
        return 0.0f;
    }
    float norm = static_cast<float>(val) / 32767.0f;
    return std::clamp(norm, -1.0f, 1.0f);
}

float NormalizeTrigger(uint8_t val, uint8_t threshold) noexcept
{
    if (val <= threshold) {
        return 0.0f;
    }
    float norm = static_cast<float>(val) / 255.0f;
    return std::clamp(norm, 0.0f, 1.0f);
}

} // namespace

GamepadManager &GamepadManager::Instance() noexcept
{
    static GamepadManager instance;
    return instance;
}

GamepadManager::GamepadManager() noexcept
{
    InitXInput();
}

GamepadManager::~GamepadManager() noexcept
{
    if (xinput_module_) {
        FreeLibrary(static_cast<HMODULE>(xinput_module_));
        xinput_module_ = nullptr;
    }
}

void GamepadManager::InitXInput() noexcept
{
    if (xinput_module_) {
        return;
    }
    const wchar_t *dll_names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const auto *name : dll_names) {
        HMODULE mod = LoadLibraryW(name);
        if (mod) {
            xinput_module_ = static_cast<void *>(mod);
            xinput_get_state_fn_ =
                reinterpret_cast<void *>(GetProcAddress(mod, "XInputGetState"));
            break;
        }
    }
}

void GamepadManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 4; ++i) {
        states_[i] = {};
    }
    input_queue_ = nullptr;
}

void GamepadManager::SetInputQueue(ndk::InputQueueImpl *queue) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    input_queue_ = queue;
}

void GamepadManager::PollControllers() noexcept
{
    if (!xinput_get_state_fn_) {
        return;
    }

    auto get_state = reinterpret_cast<PfnXInputGetState>(xinput_get_state_fn_);

    std::lock_guard<std::mutex> lock(mutex_);
    for (DWORD i = 0; i < 4; ++i) {
        XInputStateStruct xstate{};
        DWORD res = get_state(i, &xstate);

        GamepadState new_state{};
        if (res == ERROR_SUCCESS) {
            new_state.connected = true;
            new_state.left_stick_x = NormalizeThumb(xstate.Gamepad.sThumbLX, LEFT_THUMB_DEADZONE);
            new_state.left_stick_y = NormalizeThumb(xstate.Gamepad.sThumbLY, LEFT_THUMB_DEADZONE);
            new_state.right_stick_x = NormalizeThumb(xstate.Gamepad.sThumbRX, RIGHT_THUMB_DEADZONE);
            new_state.right_stick_y = NormalizeThumb(xstate.Gamepad.sThumbRY, RIGHT_THUMB_DEADZONE);
            new_state.left_trigger =
                NormalizeTrigger(xstate.Gamepad.bLeftTrigger, TRIGGER_THRESHOLD);
            new_state.right_trigger =
                NormalizeTrigger(xstate.Gamepad.bRightTrigger, TRIGGER_THRESHOLD);

            uint32_t btns = 0;
            const WORD w = xstate.Gamepad.wButtons;
            if (w & XINPUT_GAMEPAD_A_VAL) btns |= GAMEPAD_BUTTON_A;
            if (w & XINPUT_GAMEPAD_B_VAL) btns |= GAMEPAD_BUTTON_B;
            if (w & XINPUT_GAMEPAD_X_VAL) btns |= GAMEPAD_BUTTON_X;
            if (w & XINPUT_GAMEPAD_Y_VAL) btns |= GAMEPAD_BUTTON_Y;
            if (w & XINPUT_GAMEPAD_LEFT_SHOULDER_VAL) btns |= GAMEPAD_BUTTON_L1;
            if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER_VAL) btns |= GAMEPAD_BUTTON_R1;
            if (w & XINPUT_GAMEPAD_LEFT_THUMB_VAL) btns |= GAMEPAD_BUTTON_THUMBL;
            if (w & XINPUT_GAMEPAD_RIGHT_THUMB_VAL) btns |= GAMEPAD_BUTTON_THUMBR;
            if (w & XINPUT_GAMEPAD_START_VAL) btns |= GAMEPAD_BUTTON_START;
            if (w & XINPUT_GAMEPAD_BACK_VAL) btns |= GAMEPAD_BUTTON_SELECT;
            if (w & XINPUT_GAMEPAD_DPAD_UP_VAL) btns |= GAMEPAD_BUTTON_DPAD_UP;
            if (w & XINPUT_GAMEPAD_DPAD_DOWN_VAL) btns |= GAMEPAD_BUTTON_DPAD_DOWN;
            if (w & XINPUT_GAMEPAD_DPAD_LEFT_VAL) btns |= GAMEPAD_BUTTON_DPAD_LEFT;
            if (w & XINPUT_GAMEPAD_DPAD_RIGHT_VAL) btns |= GAMEPAD_BUTTON_DPAD_RIGHT;
            new_state.buttons = btns;

            if (w & XINPUT_GAMEPAD_DPAD_LEFT_VAL) {
                new_state.hat_x = -1.0f;
            } else if (w & XINPUT_GAMEPAD_DPAD_RIGHT_VAL) {
                new_state.hat_x = 1.0f;
            }
            if (w & XINPUT_GAMEPAD_DPAD_UP_VAL) {
                new_state.hat_y = -1.0f;
            } else if (w & XINPUT_GAMEPAD_DPAD_DOWN_VAL) {
                new_state.hat_y = 1.0f;
            }
        } else {
            new_state.connected = false;
        }

        GamepadState old_state = states_[i];
        states_[i] = new_state;

        if (new_state.connected) {
            DispatchStateChange(static_cast<int>(i), old_state, new_state);
        }
    }
}

void GamepadManager::InjectSyntheticState(int user_index, const GamepadState &state) noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    GamepadState old_state = states_[user_index];
    states_[user_index] = state;
    DispatchStateChange(user_index, old_state, state);
}

GamepadState GamepadManager::GetState(int user_index) const noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return {};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return states_[user_index];
}

bool GamepadManager::IsConnected(int user_index) const noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return states_[user_index].connected;
}

void GamepadManager::DispatchStateChange(int user_index, const GamepadState &old_state,
                                         const GamepadState &new_state) noexcept
{
    if (!input_queue_) {
        return;
    }

    bool analog_changed =
        std::abs(old_state.left_stick_x - new_state.left_stick_x) > 0.001f ||
        std::abs(old_state.left_stick_y - new_state.left_stick_y) > 0.001f ||
        std::abs(old_state.right_stick_x - new_state.right_stick_x) > 0.001f ||
        std::abs(old_state.right_stick_y - new_state.right_stick_y) > 0.001f ||
        std::abs(old_state.left_trigger - new_state.left_trigger) > 0.001f ||
        std::abs(old_state.right_trigger - new_state.right_trigger) > 0.001f ||
        std::abs(old_state.hat_x - new_state.hat_x) > 0.001f ||
        std::abs(old_state.hat_y - new_state.hat_y) > 0.001f;

    if (analog_changed) {
        ndk::InputPointer ptr{};
        ptr.id = user_index;
        ptr.x = new_state.left_stick_x;
        ptr.y = new_state.left_stick_y;
        ptr.raw_x = new_state.left_stick_x;
        ptr.raw_y = new_state.left_stick_y;
        ptr.axes[AMOTION_EVENT_AXIS_X] = new_state.left_stick_x;
        ptr.axes[AMOTION_EVENT_AXIS_Y] = new_state.left_stick_y;
        ptr.axes[AMOTION_EVENT_AXIS_Z] = new_state.right_stick_x;
        ptr.axes[AMOTION_EVENT_AXIS_RZ] = new_state.right_stick_y;
        ptr.axes[AMOTION_EVENT_AXIS_LTRIGGER] = new_state.left_trigger;
        ptr.axes[AMOTION_EVENT_AXIS_RTRIGGER] = new_state.right_trigger;
        ptr.axes[AMOTION_EVENT_AXIS_HAT_X] = new_state.hat_x;
        ptr.axes[AMOTION_EVENT_AXIS_HAT_Y] = new_state.hat_y;
        input_queue_->EnqueueMotionEvent(AMOTION_EVENT_ACTION_MOVE, {ptr}, AINPUT_SOURCE_JOYSTICK);
    }

    uint32_t diff = old_state.buttons ^ new_state.buttons;
    if (diff != 0) {
        static const struct {
            uint32_t mask;
            int32_t key_code;
        } button_map[] = {
            {GAMEPAD_BUTTON_A, AKEYCODE_BUTTON_A},
            {GAMEPAD_BUTTON_B, AKEYCODE_BUTTON_B},
            {GAMEPAD_BUTTON_X, AKEYCODE_BUTTON_X},
            {GAMEPAD_BUTTON_Y, AKEYCODE_BUTTON_Y},
            {GAMEPAD_BUTTON_L1, AKEYCODE_BUTTON_L1},
            {GAMEPAD_BUTTON_R1, AKEYCODE_BUTTON_R1},
            {GAMEPAD_BUTTON_THUMBL, AKEYCODE_BUTTON_THUMBL},
            {GAMEPAD_BUTTON_THUMBR, AKEYCODE_BUTTON_THUMBR},
            {GAMEPAD_BUTTON_START, AKEYCODE_BUTTON_START},
            {GAMEPAD_BUTTON_SELECT, AKEYCODE_BUTTON_SELECT},
            {GAMEPAD_BUTTON_MODE, AKEYCODE_BUTTON_MODE},
            {GAMEPAD_BUTTON_DPAD_UP, AKEYCODE_DPAD_UP},
            {GAMEPAD_BUTTON_DPAD_DOWN, AKEYCODE_DPAD_DOWN},
            {GAMEPAD_BUTTON_DPAD_LEFT, AKEYCODE_DPAD_LEFT},
            {GAMEPAD_BUTTON_DPAD_RIGHT, AKEYCODE_DPAD_RIGHT},
        };

        for (const auto &mapping : button_map) {
            if (diff & mapping.mask) {
                bool pressed = (new_state.buttons & mapping.mask) != 0;
                int32_t action = pressed ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
                input_queue_->EnqueueKeyEvent(action, mapping.key_code, 0, 0,
                                              AINPUT_SOURCE_GAMEPAD);
            }
        }
    }
}

} // namespace portage::input
