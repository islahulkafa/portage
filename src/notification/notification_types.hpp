#pragma once

#include <cstdint>
#include <string>

namespace portage::notification {

// Channel importance levels
inline constexpr int32_t IMPORTANCE_NONE = 0;
inline constexpr int32_t IMPORTANCE_MIN = 1;
inline constexpr int32_t IMPORTANCE_LOW = 2;
inline constexpr int32_t IMPORTANCE_DEFAULT = 3;
inline constexpr int32_t IMPORTANCE_HIGH = 4;
inline constexpr int32_t IMPORTANCE_MAX = 5;

// Notification flags
inline constexpr int32_t FLAG_ONGOING_EVENT = 0x00000002;
inline constexpr int32_t FLAG_AUTO_CANCEL = 0x00000010;
inline constexpr int32_t FLAG_NO_CLEAR = 0x00000020;
inline constexpr int32_t FLAG_FOREGROUND_SERVICE = 0x00000040;

// Toast duration
inline constexpr int32_t TOAST_LENGTH_SHORT = 0;
inline constexpr int32_t TOAST_LENGTH_LONG = 1;

struct NotificationChannel {
    std::string id{"default"};
    std::string name{"Default"};
    int32_t importance{IMPORTANCE_DEFAULT};
    std::string description{};
    bool show_badge{true};
    bool enable_lights{true};
    bool enable_vibration{true};
};

struct NotificationRecord {
    int32_t id{0};
    std::string tag{};
    std::string channel_id{"default"};
    std::string title{};
    std::string text{};
    int32_t small_icon{0};
    int64_t timestamp_ms{0};
    int32_t flags{0};
    int32_t progress_max{0};
    int32_t progress_current{0};
    bool progress_indeterminate{false};
    std::string category{};
};

struct ToastRecord {
    std::string text{};
    int32_t duration{TOAST_LENGTH_SHORT};
    int64_t timestamp_ms{0};
};

} // namespace portage::notification
