#include "haptic_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace portage::input {

namespace {

struct XInputVibrationStruct {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
};

using PfnXInputSetState = DWORD(WINAPI *)(DWORD, XInputVibrationStruct *);

} // namespace

HapticManager &HapticManager::Instance() noexcept
{
    static HapticManager instance;
    return instance;
}

HapticManager::HapticManager() noexcept
{
    InitXInput();
}

HapticManager::~HapticManager() noexcept
{
    StopAll();
    if (xinput_module_) {
        FreeLibrary(static_cast<HMODULE>(xinput_module_));
        xinput_module_ = nullptr;
    }
}

void HapticManager::InitXInput() noexcept
{
    if (xinput_module_) {
        return;
    }
    const wchar_t *dll_names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const auto *name : dll_names) {
        HMODULE mod = LoadLibraryW(name);
        if (mod) {
            xinput_module_ = static_cast<void *>(mod);
            xinput_set_state_fn_ =
                reinterpret_cast<void *>(GetProcAddress(mod, "XInputSetState"));
            break;
        }
    }
}

void HapticManager::ApplyXInputState(int user_index, uint16_t left, uint16_t right) noexcept
{
    if (xinput_set_state_fn_ && user_index >= 0 && user_index < 4) {
        XInputVibrationStruct vib{left, right};
        auto set_state = reinterpret_cast<PfnXInputSetState>(xinput_set_state_fn_);
        set_state(static_cast<DWORD>(user_index), &vib);
    }
}

void HapticManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 4; ++i) {
        controllers_[i].left_speed = 0;
        controllers_[i].right_speed = 0;
        controllers_[i].active = false;
        ApplyXInputState(i, 0, 0);
    }
    effects_.clear();
    next_effect_id_ = 1;
}

bool HapticManager::Vibrate(int user_index, uint16_t left_motor, uint16_t right_motor,
                            uint32_t duration_ms) noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto &c = controllers_[user_index];
    c.left_speed = left_motor;
    c.right_speed = right_motor;
    c.active = (left_motor > 0 || right_motor > 0);
    c.stop_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);

    ApplyXInputState(user_index, left_motor, right_motor);
    return true;
}

bool HapticManager::Stop(int user_index) noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto &c = controllers_[user_index];
    c.left_speed = 0;
    c.right_speed = 0;
    c.active = false;

    ApplyXInputState(user_index, 0, 0);
    return true;
}

void HapticManager::StopAll() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 4; ++i) {
        controllers_[i].left_speed = 0;
        controllers_[i].right_speed = 0;
        controllers_[i].active = false;
        ApplyXInputState(i, 0, 0);
    }
}

int HapticManager::UploadEffect(const LinuxFfEffect &effect, int16_t *out_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    int16_t id = effect.id;
    if (id < 0) {
        id = next_effect_id_++;
    }
    LinuxFfEffect stored = effect;
    stored.id = id;
    effects_[id] = stored;
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int HapticManager::PlayEffect(int16_t id, int32_t value) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = effects_.find(id);
    if (it == effects_.end()) {
        return -22; // -EINVAL
    }

    if (value > 0) {
        uint16_t strong = it->second.u.rumble.strong_magnitude;
        uint16_t weak = it->second.u.rumble.weak_magnitude;
        uint32_t length = it->second.replay.length;
        if (length == 0) {
            length = 500;
        }

        auto &c = controllers_[0];
        c.left_speed = strong;
        c.right_speed = weak;
        c.active = true;
        c.stop_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(length);
        ApplyXInputState(0, strong, weak);
    } else {
        auto &c = controllers_[0];
        c.left_speed = 0;
        c.right_speed = 0;
        c.active = false;
        ApplyXInputState(0, 0, 0);
    }
    return 0;
}

int HapticManager::EraseEffect(int16_t id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = effects_.find(id);
    if (it == effects_.end()) {
        return -22; // -EINVAL
    }
    effects_.erase(it);
    return 0;
}

bool HapticManager::GetMotorSpeeds(int user_index, uint16_t &left, uint16_t &right) const noexcept
{
    if (user_index < 0 || user_index >= 4) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &c = controllers_[user_index];
    left = c.left_speed;
    right = c.right_speed;
    return c.active;
}

size_t HapticManager::ActiveEffectsCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.size();
}

} // namespace portage::input
