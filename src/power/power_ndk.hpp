#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct APowerManager APowerManager;
typedef struct AWakeLock AWakeLock;
typedef struct ABatteryManager ABatteryManager;

__attribute__((sysv_abi)) APowerManager *APowerManager_create(void);
__attribute__((sysv_abi)) void APowerManager_destroy(APowerManager *mgr);
__attribute__((sysv_abi)) int32_t APowerManager_isInteractive(APowerManager *mgr);
__attribute__((sysv_abi)) int32_t APowerManager_isPowerSaveMode(APowerManager *mgr);

__attribute__((sysv_abi)) AWakeLock *AWakeLock_create(int32_t level_and_flags, const char *tag);
__attribute__((sysv_abi)) int32_t AWakeLock_acquire(AWakeLock *lock, int64_t timeout_ms);
__attribute__((sysv_abi)) int32_t AWakeLock_release(AWakeLock *lock);
__attribute__((sysv_abi)) int32_t AWakeLock_isHeld(AWakeLock *lock);
__attribute__((sysv_abi)) void AWakeLock_destroy(AWakeLock *lock);

__attribute__((sysv_abi)) ABatteryManager *ABatteryManager_create(void);
__attribute__((sysv_abi)) void ABatteryManager_destroy(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getCapacity(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_isCharging(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getStatus(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getHealth(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getPluggedType(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getVoltage(ABatteryManager *mgr);
__attribute__((sysv_abi)) int32_t ABatteryManager_getTemperature(ABatteryManager *mgr);

#ifdef __cplusplus
}
#endif
