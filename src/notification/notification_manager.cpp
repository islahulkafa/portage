#include "notification_manager.hpp"
#include "wsi/window_manager.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <chrono>

namespace portage::notification {

namespace {

std::wstring Utf8ToWide(const std::string &str)
{
    if (str.empty()) {
        return L"";
    }
    int len =
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    if (len <= 0) {
        return L"";
    }
    std::wstring wstr(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], len);
    return wstr;
}

std::string MakeKey(const std::string &tag, int32_t id) { return tag + ":" + std::to_string(id); }

} // namespace

NotificationManager &NotificationManager::Instance() noexcept
{
    static NotificationManager instance;
    return instance;
}

NotificationManager::NotificationManager() noexcept { Reset(); }

void NotificationManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_notifications_.clear();
    recent_toasts_.clear();
    channels_.clear();
    notifications_enabled_ = true;

    NotificationChannel def_channel{};
    def_channel.id = "default";
    def_channel.name = "Default";
    def_channel.importance = IMPORTANCE_DEFAULT;
    def_channel.description = "Default application notification channel";
    channels_["default"] = def_channel;
}

bool NotificationManager::CreateNotificationChannel(const NotificationChannel &channel) noexcept
{
    if (channel.id.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    channels_[channel.id] = channel;
    return true;
}

std::optional<NotificationChannel>
NotificationManager::GetNotificationChannel(const std::string &id) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(id);
    if (it != channels_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool NotificationManager::DeleteNotificationChannel(const std::string &id) noexcept
{
    if (id == "default") {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_.erase(id) > 0;
}

std::vector<NotificationChannel> NotificationManager::GetAllNotificationChannels() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NotificationChannel> result;
    result.reserve(channels_.size());
    for (const auto &pair : channels_) {
        result.push_back(pair.second);
    }
    return result;
}

bool NotificationManager::PostNotification(const std::string &tag, int32_t id,
                                           const NotificationRecord &record) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!notifications_enabled_) {
        return false;
    }

    std::string channel_id = record.channel_id.empty() ? "default" : record.channel_id;
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
        channel_id = "default";
        it = channels_.find("default");
    }

    if (it != channels_.end() && it->second.importance == IMPORTANCE_NONE) {
        return false;
    }

    NotificationRecord stored = record;
    stored.tag = tag;
    stored.id = id;
    stored.channel_id = channel_id;
    if (stored.timestamp_ms == 0) {
        stored.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    }

    std::string key = MakeKey(tag, id);
    active_notifications_[key] = stored;

    UpdateHostNotifyIcon(stored, false);
    return true;
}

bool NotificationManager::CancelNotification(const std::string &tag, int32_t id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = MakeKey(tag, id);
    auto it = active_notifications_.find(key);
    if (it == active_notifications_.end()) {
        return false;
    }

    UpdateHostNotifyIcon(it->second, true);
    active_notifications_.erase(it);
    return true;
}

void NotificationManager::CancelAllNotifications() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : active_notifications_) {
        UpdateHostNotifyIcon(pair.second, true);
    }
    active_notifications_.clear();
}

std::vector<NotificationRecord> NotificationManager::GetActiveNotifications() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NotificationRecord> result;
    result.reserve(active_notifications_.size());
    for (const auto &pair : active_notifications_) {
        result.push_back(pair.second);
    }
    return result;
}

size_t NotificationManager::GetActiveNotificationCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_notifications_.size();
}

bool NotificationManager::AreNotificationsEnabled() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return notifications_enabled_;
}

void NotificationManager::SetNotificationsEnabled(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    notifications_enabled_ = enabled;
}

void NotificationManager::ShowToast(const std::string &text, int32_t duration) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    ToastRecord toast{};
    toast.text = text;
    toast.duration = duration;
    toast.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    recent_toasts_.push_back(toast);

    NotificationRecord fake_rec{};
    fake_rec.id = 999999;
    fake_rec.title = "Toast";
    fake_rec.text = text;
    UpdateHostNotifyIcon(fake_rec, false);
}

std::vector<ToastRecord> NotificationManager::GetRecentToasts() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return recent_toasts_;
}

void NotificationManager::UpdateHostNotifyIcon(const NotificationRecord &record,
                                               bool is_delete) noexcept
{
    HWND hwnd = wsi::WindowManager::Instance().GetWindowHandle();
    if (!hwnd) {
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = static_cast<UINT>(record.id);

    if (is_delete) {
        Shell_NotifyIconW(NIM_DELETE, &nid);
        return;
    }

    nid.uFlags = NIF_INFO | NIF_TIP;
    nid.dwInfoFlags = NIIF_INFO;

    std::wstring wtitle = Utf8ToWide(record.title);
    std::wstring wtext = Utf8ToWide(record.text);

    wcsncpy_s(nid.szInfoTitle, wtitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, wtext.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szTip, wtitle.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
}

} // namespace portage::notification
