#pragma once

#include "notification_types.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace portage::notification {

class NotificationManager {
  public:
    static NotificationManager &Instance() noexcept;

    void Reset() noexcept;

    // Channel management
    bool CreateNotificationChannel(const NotificationChannel &channel) noexcept;
    [[nodiscard]] std::optional<NotificationChannel>
    GetNotificationChannel(const std::string &id) const noexcept;
    bool DeleteNotificationChannel(const std::string &id) noexcept;
    [[nodiscard]] std::vector<NotificationChannel> GetAllNotificationChannels() const noexcept;

    // Notification lifecycle
    bool PostNotification(const std::string &tag, int32_t id,
                          const NotificationRecord &record) noexcept;
    bool CancelNotification(const std::string &tag, int32_t id) noexcept;
    void CancelAllNotifications() noexcept;
    [[nodiscard]] std::vector<NotificationRecord> GetActiveNotifications() const noexcept;
    [[nodiscard]] size_t GetActiveNotificationCount() const noexcept;

    // Permission & Global Settings
    [[nodiscard]] bool AreNotificationsEnabled() const noexcept;
    void SetNotificationsEnabled(bool enabled) noexcept;

    // Toast dispatching
    void ShowToast(const std::string &text, int32_t duration) noexcept;
    [[nodiscard]] std::vector<ToastRecord> GetRecentToasts() const noexcept;

  private:
    NotificationManager() noexcept;
    ~NotificationManager() noexcept = default;

    NotificationManager(const NotificationManager &) = delete;
    NotificationManager &operator=(const NotificationManager &) = delete;

    void UpdateHostNotifyIcon(const NotificationRecord &record, bool is_delete) noexcept;

    mutable std::mutex mutex_{};
    bool notifications_enabled_{true};
    std::unordered_map<std::string, NotificationChannel> channels_{};
    std::unordered_map<std::string, NotificationRecord> active_notifications_{};
    std::vector<ToastRecord> recent_toasts_{};
};

} // namespace portage::notification
