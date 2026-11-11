#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct APowerManager;
typedef struct APowerManager APowerManager;

struct AWakeLock;
typedef struct AWakeLock AWakeLock;

struct ABatteryManager;
typedef struct ABatteryManager ABatteryManager;

__attribute__((visibility("default"))) APowerManager *APowerManager_create(void)
{
    return (APowerManager *)0;
}

__attribute__((visibility("default"))) void APowerManager_destroy(APowerManager *mgr) { (void)mgr; }

__attribute__((visibility("default"))) int32_t APowerManager_isInteractive(APowerManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t APowerManager_isPowerSaveMode(APowerManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) AWakeLock *AWakeLock_create(int32_t level_and_flags,
                                                                   const char *tag)
{
    (void)level_and_flags;
    (void)tag;
    return (AWakeLock *)0;
}

__attribute__((visibility("default"))) int32_t AWakeLock_acquire(AWakeLock *lock,
                                                                 int64_t timeout_ms)
{
    (void)lock;
    (void)timeout_ms;
    return -1;
}

__attribute__((visibility("default"))) int32_t AWakeLock_release(AWakeLock *lock)
{
    (void)lock;
    return -1;
}

__attribute__((visibility("default"))) int32_t AWakeLock_isHeld(AWakeLock *lock)
{
    (void)lock;
    return 0;
}

__attribute__((visibility("default"))) void AWakeLock_destroy(AWakeLock *lock) { (void)lock; }

__attribute__((visibility("default"))) ABatteryManager *ABatteryManager_create(void)
{
    return (ABatteryManager *)0;
}

__attribute__((visibility("default"))) void ABatteryManager_destroy(ABatteryManager *mgr)
{
    (void)mgr;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getCapacity(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_isCharging(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getStatus(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getHealth(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getPluggedType(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getVoltage(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}

__attribute__((visibility("default"))) int32_t ABatteryManager_getTemperature(ABatteryManager *mgr)
{
    (void)mgr;
    return 0;
}
