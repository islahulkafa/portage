#include "notification_ndk.hpp"
#include "notification_manager.hpp"

#include <memory>
#include <string>

struct ANotificationManager {
    portage::notification::NotificationManager *mgr{nullptr};
};

struct ANotificationChannel {
    portage::notification::NotificationChannel channel{};
};

struct ANotification {
    portage::notification::NotificationRecord record{};
};

extern "C" {

__attribute__((sysv_abi)) ANotificationManager *ANotificationManager_create(void)
{
    auto *wrapper = new ANotificationManager();
    wrapper->mgr = &portage::notification::NotificationManager::Instance();
    return wrapper;
}

__attribute__((sysv_abi)) void ANotificationManager_destroy(ANotificationManager *mgr)
{
    delete mgr;
}

__attribute__((sysv_abi)) ANotificationChannel *
ANotificationChannel_create(const char *id, const char *name, int32_t importance)
{
    auto *ch = new ANotificationChannel();
    ch->channel.id = id ? id : "default";
    ch->channel.name = name ? name : "Default";
    ch->channel.importance = importance;
    return ch;
}

__attribute__((sysv_abi)) void ANotificationChannel_destroy(ANotificationChannel *channel)
{
    delete channel;
}

__attribute__((sysv_abi)) void
ANotificationChannel_setDescription(ANotificationChannel *channel, const char *desc)
{
    if (channel && desc) {
        channel->channel.description = desc;
    }
}

__attribute__((sysv_abi)) int32_t
ANotificationChannel_getImportance(ANotificationChannel *channel)
{
    if (!channel) {
        return 0;
    }
    return channel->channel.importance;
}

__attribute__((sysv_abi)) int32_t
ANotificationManager_createNotificationChannel(ANotificationManager *mgr,
                                               ANotificationChannel *channel)
{
    if (!mgr || !mgr->mgr || !channel) {
        return -1;
    }
    bool ok = mgr->mgr->CreateNotificationChannel(channel->channel);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) ANotificationChannel *
ANotificationManager_getNotificationChannel(ANotificationManager *mgr, const char *channel_id)
{
    if (!mgr || !mgr->mgr || !channel_id) {
        return nullptr;
    }
    auto opt = mgr->mgr->GetNotificationChannel(channel_id);
    if (!opt) {
        return nullptr;
    }
    auto *ch = new ANotificationChannel();
    ch->channel = *opt;
    return ch;
}

__attribute__((sysv_abi)) int32_t
ANotificationManager_deleteNotificationChannel(ANotificationManager *mgr, const char *channel_id)
{
    if (!mgr || !mgr->mgr || !channel_id) {
        return -1;
    }
    bool ok = mgr->mgr->DeleteNotificationChannel(channel_id);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) ANotification *ANotification_create(const char *channel_id)
{
    auto *n = new ANotification();
    n->record.channel_id = channel_id ? channel_id : "default";
    return n;
}

__attribute__((sysv_abi)) void ANotification_destroy(ANotification *notification)
{
    delete notification;
}

__attribute__((sysv_abi)) void ANotification_setTitle(ANotification *notification,
                                                      const char *title)
{
    if (notification && title) {
        notification->record.title = title;
    }
}

__attribute__((sysv_abi)) void ANotification_setText(ANotification *notification,
                                                     const char *text)
{
    if (notification && text) {
        notification->record.text = text;
    }
}

__attribute__((sysv_abi)) void ANotification_setProgress(ANotification *notification, int32_t max,
                                                         int32_t progress, int32_t indeterminate)
{
    if (notification) {
        notification->record.progress_max = max;
        notification->record.progress_current = progress;
        notification->record.progress_indeterminate = (indeterminate != 0);
    }
}

__attribute__((sysv_abi)) void ANotification_setOngoing(ANotification *notification,
                                                        int32_t ongoing)
{
    if (notification) {
        if (ongoing) {
            notification->record.flags |= portage::notification::FLAG_ONGOING_EVENT;
        } else {
            notification->record.flags &= ~portage::notification::FLAG_ONGOING_EVENT;
        }
    }
}

__attribute__((sysv_abi)) int32_t ANotificationManager_notify(ANotificationManager *mgr,
                                                              const char *tag, int32_t id,
                                                              ANotification *notification)
{
    if (!mgr || !mgr->mgr || !notification) {
        return -1;
    }
    std::string tag_str = tag ? tag : "";
    bool ok = mgr->mgr->PostNotification(tag_str, id, notification->record);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t ANotificationManager_cancel(ANotificationManager *mgr,
                                                              const char *tag, int32_t id)
{
    if (!mgr || !mgr->mgr) {
        return -1;
    }
    std::string tag_str = tag ? tag : "";
    bool ok = mgr->mgr->CancelNotification(tag_str, id);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) void ANotificationManager_cancelAll(ANotificationManager *mgr)
{
    if (mgr && mgr->mgr) {
        mgr->mgr->CancelAllNotifications();
    }
}

__attribute__((sysv_abi)) void AToast_show(const char *text, int32_t duration)
{
    std::string str = text ? text : "";
    portage::notification::NotificationManager::Instance().ShowToast(str, duration);
}

} // extern "C"
