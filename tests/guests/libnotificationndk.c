#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ANotificationManager;
typedef struct ANotificationManager ANotificationManager;

struct ANotificationChannel;
typedef struct ANotificationChannel ANotificationChannel;

struct ANotification;
typedef struct ANotification ANotification;

__attribute__((visibility("default"))) ANotificationManager *ANotificationManager_create(void)
{
    return (ANotificationManager *)0;
}

__attribute__((visibility("default"))) void ANotificationManager_destroy(ANotificationManager *mgr)
{
    (void)mgr;
}

__attribute__((visibility("default"))) ANotificationChannel *
ANotificationChannel_create(const char *id, const char *name, int32_t importance)
{
    (void)id;
    (void)name;
    (void)importance;
    return (ANotificationChannel *)0;
}

__attribute__((visibility("default"))) void
ANotificationChannel_destroy(ANotificationChannel *channel)
{
    (void)channel;
}

__attribute__((visibility("default"))) void
ANotificationChannel_setDescription(ANotificationChannel *channel, const char *desc)
{
    (void)channel;
    (void)desc;
}

__attribute__((visibility("default"))) int32_t
ANotificationChannel_getImportance(ANotificationChannel *channel)
{
    (void)channel;
    return 0;
}

__attribute__((visibility("default"))) int32_t ANotificationManager_createNotificationChannel(
    ANotificationManager *mgr, ANotificationChannel *channel)
{
    (void)mgr;
    (void)channel;
    return -1;
}

__attribute__((visibility("default"))) ANotificationChannel *
ANotificationManager_getNotificationChannel(ANotificationManager *mgr, const char *channel_id)
{
    (void)mgr;
    (void)channel_id;
    return (ANotificationChannel *)0;
}

__attribute__((visibility("default"))) int32_t
ANotificationManager_deleteNotificationChannel(ANotificationManager *mgr, const char *channel_id)
{
    (void)mgr;
    (void)channel_id;
    return -1;
}

__attribute__((visibility("default"))) ANotification *ANotification_create(const char *channel_id)
{
    (void)channel_id;
    return (ANotification *)0;
}

__attribute__((visibility("default"))) void ANotification_destroy(ANotification *notification)
{
    (void)notification;
}

__attribute__((visibility("default"))) void ANotification_setTitle(ANotification *notification,
                                                                   const char *title)
{
    (void)notification;
    (void)title;
}

__attribute__((visibility("default"))) void ANotification_setText(ANotification *notification,
                                                                  const char *text)
{
    (void)notification;
    (void)text;
}

__attribute__((visibility("default"))) void ANotification_setProgress(ANotification *notification,
                                                                      int32_t max, int32_t progress,
                                                                      int32_t indeterminate)
{
    (void)notification;
    (void)max;
    (void)progress;
    (void)indeterminate;
}

__attribute__((visibility("default"))) void ANotification_setOngoing(ANotification *notification,
                                                                     int32_t ongoing)
{
    (void)notification;
    (void)ongoing;
}

__attribute__((visibility("default"))) int32_t ANotificationManager_notify(
    ANotificationManager *mgr, const char *tag, int32_t id, ANotification *notification)
{
    (void)mgr;
    (void)tag;
    (void)id;
    (void)notification;
    return -1;
}

__attribute__((visibility("default"))) int32_t
ANotificationManager_cancel(ANotificationManager *mgr, const char *tag, int32_t id)
{
    (void)mgr;
    (void)tag;
    (void)id;
    return -1;
}

__attribute__((visibility("default"))) void
ANotificationManager_cancelAll(ANotificationManager *mgr)
{
    (void)mgr;
}

__attribute__((visibility("default"))) void AToast_show(const char *text, int32_t duration)
{
    (void)text;
    (void)duration;
}
