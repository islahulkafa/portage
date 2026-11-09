#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace portage::power {

// WakeLock flags & levels
inline constexpr int32_t PARTIAL_WAKE_LOCK = 0x00000001;
inline constexpr int32_t SCREEN_DIM_WAKE_LOCK = 0x00000006;
inline constexpr int32_t SCREEN_BRIGHT_WAKE_LOCK = 0x0000000a;
inline constexpr int32_t FULL_WAKE_LOCK = 0x0000001a;
inline constexpr int32_t PROXIMITY_SCREEN_OFF_WAKE_LOCK = 0x00000020;
inline constexpr int32_t DOZE_WAKE_LOCK = 0x00000040;
inline constexpr int32_t DRAW_WAKE_LOCK = 0x00000080;
inline constexpr int32_t ACQUIRE_CAUSES_WAKEUP = 0x10000000;
inline constexpr int32_t ON_AFTER_RELEASE = 0x20000000;

// Battery status constants
inline constexpr int32_t BATTERY_STATUS_UNKNOWN = 1;
inline constexpr int32_t BATTERY_STATUS_CHARGING = 2;
inline constexpr int32_t BATTERY_STATUS_DISCHARGING = 3;
inline constexpr int32_t BATTERY_STATUS_NOT_CHARGING = 4;
inline constexpr int32_t BATTERY_STATUS_FULL = 5;

// Battery health constants
inline constexpr int32_t BATTERY_HEALTH_UNKNOWN = 1;
inline constexpr int32_t BATTERY_HEALTH_GOOD = 2;
inline constexpr int32_t BATTERY_HEALTH_OVERHEAT = 3;
inline constexpr int32_t BATTERY_HEALTH_DEAD = 4;
inline constexpr int32_t BATTERY_HEALTH_OVER_VOLTAGE = 5;
inline constexpr int32_t BATTERY_HEALTH_UNSPECIFIED_FAILURE = 6;
inline constexpr int32_t BATTERY_HEALTH_COLD = 7;

// Battery plugged constants
inline constexpr int32_t BATTERY_PLUGGED_AC = 1;
inline constexpr int32_t BATTERY_PLUGGED_USB = 2;
inline constexpr int32_t BATTERY_PLUGGED_WIRELESS = 4;
inline constexpr int32_t BATTERY_PLUGGED_DOCK = 8;

// Battery property IDs
inline constexpr int32_t BATTERY_PROPERTY_CHARGE_COUNTER = 1;
inline constexpr int32_t BATTERY_PROPERTY_CURRENT_NOW = 2;
inline constexpr int32_t BATTERY_PROPERTY_CURRENT_AVERAGE = 3;
inline constexpr int32_t BATTERY_PROPERTY_CAPACITY = 4;
inline constexpr int32_t BATTERY_PROPERTY_ENERGY_COUNTER = 5;
inline constexpr int32_t BATTERY_PROPERTY_STATUS = 6;

struct BatteryState {
    int32_t capacity_percent{100};
    int32_t status{BATTERY_STATUS_CHARGING};
    int32_t health{BATTERY_HEALTH_GOOD};
    int32_t plugged{BATTERY_PLUGGED_AC};
    int32_t voltage_mv{4200};
    int32_t temperature_deci_c{250};
    int32_t current_now_ua{500000};
    int32_t current_avg_ua{450000};
    int64_t charge_counter_uah{4000000};
    int64_t energy_counter_nwh{16000000};
    bool is_present{true};
};

struct WakeLockRecord {
    uint32_t id{0};
    int32_t flags{PARTIAL_WAKE_LOCK};
    std::string tag{};
    bool held{false};
    bool ref_counted{true};
    int32_t ref_count{0};
    std::chrono::steady_clock::time_point expire_time{};
    bool has_timeout{false};
};

} // namespace portage::power
