#pragma once

#include "power_types.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace portage::power {

class PowerManager {
  public:
    static PowerManager &Instance() noexcept;

    void Reset() noexcept;

    // WakeLock lifecycle
    uint32_t CreateWakeLock(int32_t flags, const std::string &tag) noexcept;
    bool AcquireWakeLock(uint32_t id, int64_t timeout_ms = -1) noexcept;
    bool ReleaseWakeLock(uint32_t id) noexcept;
    bool IsWakeLockHeld(uint32_t id) const noexcept;
    void SetWakeLockReferenceCounted(uint32_t id, bool ref_counted) noexcept;
    void DestroyWakeLock(uint32_t id) noexcept;
    [[nodiscard]] size_t ActiveWakeLockCount() const noexcept;

    // Power state queries & modifiers
    [[nodiscard]] bool IsInteractive() const noexcept;
    void SetInteractive(bool interactive) noexcept;
    [[nodiscard]] bool IsPowerSaveMode() const noexcept;
    void SetPowerSaveMode(bool mode) noexcept;
    [[nodiscard]] bool IsDeviceIdleMode() const noexcept;
    void SetDeviceIdleMode(bool idle) noexcept;

    // Battery state queries & modifiers
    [[nodiscard]] BatteryState GetBatteryState() const noexcept;
    void SetSimulatedBattery(int32_t capacity, bool charging,
                             int32_t plugged = BATTERY_PLUGGED_AC) noexcept;
    void ResetSimulatedBattery() noexcept;
    [[nodiscard]] int32_t GetBatteryProperty(int32_t prop_id) const noexcept;
    [[nodiscard]] bool IsCharging() const noexcept;
    [[nodiscard]] int32_t GetCapacity() const noexcept;

  private:
    PowerManager() noexcept;
    ~PowerManager() noexcept;

    PowerManager(const PowerManager &) = delete;
    PowerManager &operator=(const PowerManager &) = delete;

    void UpdateHostExecutionStateLocked() noexcept;
    void TimerWorkerLoop() noexcept;

    mutable std::mutex mutex_{};
    std::condition_variable timer_cv_{};
    std::thread timer_thread_{};
    std::atomic<bool> running_{true};

    uint32_t next_wakelock_id_{1};
    std::unordered_map<uint32_t, WakeLockRecord> wakelocks_{};

    std::atomic<bool> interactive_{true};
    std::atomic<bool> power_save_mode_{false};
    std::atomic<bool> device_idle_mode_{false};

    bool custom_battery_override_{false};
    BatteryState simulated_battery_{};
};

} // namespace portage::power
