#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ANotificationManager ANotificationManager;
typedef struct ANotificationChannel ANotificationChannel;
typedef struct ANotification ANotification;

__attribute__((sysv_abi)) ANotificationManager *ANotificationManager_create(void);
__attribute__((sysv_abi)) void ANotificationManager_destroy(ANotificationManager *mgr);

__attribute__((sysv_abi)) ANotificationChannel *
ANotificationChannel_create(const char *id, const char *name, int32_t importance);
__attribute__((sysv_abi)) void ANotificationChannel_destroy(ANotificationChannel *channel);
__attribute__((sysv_abi)) void
ANotificationChannel_setDescription(ANotificationChannel *channel, const char *desc);
__attribute__((sysv_abi)) int32_t
ANotificationChannel_getImportance(ANotificationChannel *channel);

__attribute__((sysv_abi)) int32_t
ANotificationManager_createNotificationChannel(ANotificationManager *mgr,
                                               ANotificationChannel *channel);
__attribute__((sysv_abi)) ANotificationChannel *
ANotificationManager_getNotificationChannel(ANotificationManager *mgr, const char *channel_id);
__attribute__((sysv_abi)) int32_t
ANotificationManager_deleteNotificationChannel(ANotificationManager *mgr, const char *channel_id);

__attribute__((sysv_abi)) ANotification *ANotification_create(const char *channel_id);
__attribute__((sysv_abi)) void ANotification_destroy(ANotification *notification);
__attribute__((sysv_abi)) void ANotification_setTitle(ANotification *notification,
                                                      const char *title);
__attribute__((sysv_abi)) void ANotification_setText(ANotification *notification,
                                                     const char *text);
__attribute__((sysv_abi)) void ANotification_setProgress(ANotification *notification, int32_t max,
                                                         int32_t progress, int32_t indeterminate);
__attribute__((sysv_abi)) void ANotification_setOngoing(ANotification *notification,
                                                        int32_t ongoing);

__attribute__((sysv_abi)) int32_t ANotificationManager_notify(ANotificationManager *mgr,
                                                              const char *tag, int32_t id,
                                                              ANotification *notification);
__attribute__((sysv_abi)) int32_t ANotificationManager_cancel(ANotificationManager *mgr,
                                                              const char *tag, int32_t id);
__attribute__((sysv_abi)) void ANotificationManager_cancelAll(ANotificationManager *mgr);

__attribute__((sysv_abi)) void AToast_show(const char *text, int32_t duration);

#ifdef __cplusplus
}
#endif
