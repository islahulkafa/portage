#include "power_manager.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace portage::power {

PowerManager &PowerManager::Instance() noexcept
{
    static PowerManager instance;
    return instance;
}

PowerManager::PowerManager() noexcept
{
    simulated_battery_.capacity_percent = 100;
    simulated_battery_.status = BATTERY_STATUS_CHARGING;
    simulated_battery_.health = BATTERY_HEALTH_GOOD;
    simulated_battery_.plugged = BATTERY_PLUGGED_AC;
    simulated_battery_.voltage_mv = 4200;
    simulated_battery_.temperature_deci_c = 250;
    simulated_battery_.current_now_ua = 500000;
    simulated_battery_.current_avg_ua = 450000;
    simulated_battery_.charge_counter_uah = 4000000;
    simulated_battery_.energy_counter_nwh = 16000000;
    simulated_battery_.is_present = true;

    timer_thread_ = std::thread(&PowerManager::TimerWorkerLoop, this);
}

PowerManager::~PowerManager() noexcept
{
    running_.store(false, std::memory_order_release);
    timer_cv_.notify_all();
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
    SetThreadExecutionState(ES_CONTINUOUS);
}

void PowerManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    wakelocks_.clear();
    next_wakelock_id_ = 1;
    interactive_.store(true, std::memory_order_release);
    power_save_mode_.store(false, std::memory_order_release);
    device_idle_mode_.store(false, std::memory_order_release);
    custom_battery_override_ = false;

    simulated_battery_.capacity_percent = 100;
    simulated_battery_.status = BATTERY_STATUS_CHARGING;
    simulated_battery_.health = BATTERY_HEALTH_GOOD;
    simulated_battery_.plugged = BATTERY_PLUGGED_AC;
    simulated_battery_.voltage_mv = 4200;
    simulated_battery_.temperature_deci_c = 250;
    simulated_battery_.current_now_ua = 500000;
    simulated_battery_.current_avg_ua = 450000;
    simulated_battery_.charge_counter_uah = 4000000;
    simulated_battery_.energy_counter_nwh = 16000000;
    simulated_battery_.is_present = true;

    UpdateHostExecutionStateLocked();
}

uint32_t PowerManager::CreateWakeLock(int32_t flags, const std::string &tag) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_wakelock_id_++;
    WakeLockRecord record{};
    record.id = id;
    record.flags = flags;
    record.tag = tag;
    record.held = false;
    record.ref_counted = true;
    record.ref_count = 0;
    record.has_timeout = false;
    wakelocks_[id] = std::move(record);
    return id;
}

bool PowerManager::AcquireWakeLock(uint32_t id, int64_t timeout_ms) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wakelocks_.find(id);
    if (it == wakelocks_.end()) {
        return false;
    }

    auto &rec = it->second;
    if (rec.held && rec.ref_counted) {
        rec.ref_count++;
    } else {
        rec.held = true;
        rec.ref_count = 1;
    }

    if (timeout_ms > 0) {
        rec.expire_time =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        rec.has_timeout = true;
        timer_cv_.notify_all();
    } else {
        rec.has_timeout = false;
    }

    UpdateHostExecutionStateLocked();
    return true;
}

bool PowerManager::ReleaseWakeLock(uint32_t id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wakelocks_.find(id);
    if (it == wakelocks_.end() || !it->second.held) {
        return false;
    }

    auto &rec = it->second;
    if (rec.ref_counted && rec.ref_count > 1) {
        rec.ref_count--;
    } else {
        rec.held = false;
        rec.ref_count = 0;
        rec.has_timeout = false;
    }

    UpdateHostExecutionStateLocked();
    return true;
}

bool PowerManager::IsWakeLockHeld(uint32_t id) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wakelocks_.find(id);
    if (it == wakelocks_.end()) {
        return false;
    }
    return it->second.held;
}

void PowerManager::SetWakeLockReferenceCounted(uint32_t id, bool ref_counted) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wakelocks_.find(id);
    if (it != wakelocks_.end()) {
        it->second.ref_counted = ref_counted;
    }
}

void PowerManager::DestroyWakeLock(uint32_t id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = wakelocks_.find(id);
    if (it != wakelocks_.end()) {
        wakelocks_.erase(it);
        UpdateHostExecutionStateLocked();
    }
}

size_t PowerManager::ActiveWakeLockCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto &pair : wakelocks_) {
        if (pair.second.held) {
            count++;
        }
    }
    return count;
}

bool PowerManager::IsInteractive() const noexcept
{
    return interactive_.load(std::memory_order_acquire);
}

void PowerManager::SetInteractive(bool interactive) noexcept
{
    interactive_.store(interactive, std::memory_order_release);
}

bool PowerManager::IsPowerSaveMode() const noexcept
{
    return power_save_mode_.load(std::memory_order_acquire);
}

void PowerManager::SetPowerSaveMode(bool mode) noexcept
{
    power_save_mode_.store(mode, std::memory_order_release);
}

bool PowerManager::IsDeviceIdleMode() const noexcept
{
    return device_idle_mode_.load(std::memory_order_acquire);
}

void PowerManager::SetDeviceIdleMode(bool idle) noexcept
{
    device_idle_mode_.store(idle, std::memory_order_release);
}

BatteryState PowerManager::GetBatteryState() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (custom_battery_override_) {
        return simulated_battery_;
    }

    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps) && sps.BatteryFlag != 128 && sps.BatteryLifePercent != 255) {
        BatteryState state = simulated_battery_;
        state.capacity_percent = static_cast<int32_t>(sps.BatteryLifePercent);
        if (state.capacity_percent > 100) {
            state.capacity_percent = 100;
        }

        if (sps.BatteryFlag & 8) {
            state.status = BATTERY_STATUS_CHARGING;
        } else if (state.capacity_percent >= 100) {
            state.status = BATTERY_STATUS_FULL;
        } else if (sps.ACLineStatus == 1) {
            state.status = BATTERY_STATUS_CHARGING;
        } else {
            state.status = BATTERY_STATUS_DISCHARGING;
        }

        if (sps.ACLineStatus == 1) {
            state.plugged = BATTERY_PLUGGED_AC;
        } else {
            state.plugged = 0;
        }

        return state;
    }

    return simulated_battery_;
}

void PowerManager::SetSimulatedBattery(int32_t capacity, bool charging, int32_t plugged) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    custom_battery_override_ = true;
    simulated_battery_.capacity_percent = capacity;
    if (charging) {
        simulated_battery_.status =
            (capacity >= 100) ? BATTERY_STATUS_FULL : BATTERY_STATUS_CHARGING;
        simulated_battery_.plugged = plugged;
    } else {
        simulated_battery_.status = BATTERY_STATUS_DISCHARGING;
        simulated_battery_.plugged = 0;
    }
}

void PowerManager::ResetSimulatedBattery() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    custom_battery_override_ = false;
    simulated_battery_.capacity_percent = 100;
    simulated_battery_.status = BATTERY_STATUS_CHARGING;
    simulated_battery_.plugged = BATTERY_PLUGGED_AC;
}

int32_t PowerManager::GetBatteryProperty(int32_t prop_id) const noexcept
{
    BatteryState state = GetBatteryState();
    switch (prop_id) {
    case BATTERY_PROPERTY_CHARGE_COUNTER:
        return static_cast<int32_t>(state.charge_counter_uah);
    case BATTERY_PROPERTY_CURRENT_NOW:
        return state.current_now_ua;
    case BATTERY_PROPERTY_CURRENT_AVERAGE:
        return state.current_avg_ua;
    case BATTERY_PROPERTY_CAPACITY:
        return state.capacity_percent;
    case BATTERY_PROPERTY_ENERGY_COUNTER:
        return static_cast<int32_t>(state.energy_counter_nwh);
    case BATTERY_PROPERTY_STATUS:
        return state.status;
    default:
        return 0;
    }
}

bool PowerManager::IsCharging() const noexcept
{
    BatteryState state = GetBatteryState();
    return state.status == BATTERY_STATUS_CHARGING || state.status == BATTERY_STATUS_FULL;
}

int32_t PowerManager::GetCapacity() const noexcept
{
    return GetBatteryState().capacity_percent;
}

void PowerManager::UpdateHostExecutionStateLocked() noexcept
{
    bool has_screen_lock = false;
    bool has_cpu_lock = false;

    for (const auto &pair : wakelocks_) {
        if (!pair.second.held) {
            continue;
        }
        int32_t level = pair.second.flags & 0x0000ffff;
        if (level == SCREEN_BRIGHT_WAKE_LOCK || level == SCREEN_DIM_WAKE_LOCK ||
            level == FULL_WAKE_LOCK) {
            has_screen_lock = true;
        } else {
            has_cpu_lock = true;
        }
    }

    if (has_screen_lock) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    } else if (has_cpu_lock) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

void PowerManager::TimerWorkerLoop() noexcept
{
    while (running_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto next_wake = now + std::chrono::seconds(1);
        bool any_timeout = false;

        for (auto &pair : wakelocks_) {
            auto &rec = pair.second;
            if (rec.held && rec.has_timeout) {
                if (rec.expire_time <= now) {
                    rec.held = false;
                    rec.ref_count = 0;
                    rec.has_timeout = false;
                    UpdateHostExecutionStateLocked();
                } else {
                    if (!any_timeout || rec.expire_time < next_wake) {
                        next_wake = rec.expire_time;
                        any_timeout = true;
                    }
                }
            }
        }

        timer_cv_.wait_until(lock, next_wake, [this] {
            return !running_.load(std::memory_order_acquire);
        });
    }
}

} // namespace portage::power
