#include "power_ndk.hpp"
#include "power_manager.hpp"

#include <memory>
#include <string>

struct APowerManager {
    portage::power::PowerManager *mgr{nullptr};
};

struct AWakeLock {
    uint32_t lock_id{0};
};

struct ABatteryManager {
    portage::power::PowerManager *mgr{nullptr};
};

extern "C" {

__attribute__((sysv_abi)) APowerManager *APowerManager_create(void)
{
    auto *wrapper = new APowerManager();
    wrapper->mgr = &portage::power::PowerManager::Instance();
    return wrapper;
}

__attribute__((sysv_abi)) void APowerManager_destroy(APowerManager *mgr)
{
    delete mgr;
}

__attribute__((sysv_abi)) int32_t APowerManager_isInteractive(APowerManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->IsInteractive() ? 1 : 0;
}

__attribute__((sysv_abi)) int32_t APowerManager_isPowerSaveMode(APowerManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->IsPowerSaveMode() ? 1 : 0;
}

__attribute__((sysv_abi)) AWakeLock *AWakeLock_create(int32_t level_and_flags, const char *tag)
{
    std::string tag_str = tag ? tag : "";
    uint32_t id = portage::power::PowerManager::Instance().CreateWakeLock(level_and_flags, tag_str);
    auto *lock = new AWakeLock();
    lock->lock_id = id;
    return lock;
}

__attribute__((sysv_abi)) int32_t AWakeLock_acquire(AWakeLock *lock, int64_t timeout_ms)
{
    if (!lock) {
        return -1;
    }
    bool ok = portage::power::PowerManager::Instance().AcquireWakeLock(lock->lock_id, timeout_ms);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AWakeLock_release(AWakeLock *lock)
{
    if (!lock) {
        return -1;
    }
    bool ok = portage::power::PowerManager::Instance().ReleaseWakeLock(lock->lock_id);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AWakeLock_isHeld(AWakeLock *lock)
{
    if (!lock) {
        return 0;
    }
    return portage::power::PowerManager::Instance().IsWakeLockHeld(lock->lock_id) ? 1 : 0;
}

__attribute__((sysv_abi)) void AWakeLock_destroy(AWakeLock *lock)
{
    if (!lock) {
        return;
    }
    portage::power::PowerManager::Instance().DestroyWakeLock(lock->lock_id);
    delete lock;
}

__attribute__((sysv_abi)) ABatteryManager *ABatteryManager_create(void)
{
    auto *wrapper = new ABatteryManager();
    wrapper->mgr = &portage::power::PowerManager::Instance();
    return wrapper;
}

__attribute__((sysv_abi)) void ABatteryManager_destroy(ABatteryManager *mgr)
{
    delete mgr;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getCapacity(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetCapacity();
}

__attribute__((sysv_abi)) int32_t ABatteryManager_isCharging(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->IsCharging() ? 1 : 0;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getStatus(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetBatteryState().status;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getHealth(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetBatteryState().health;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getPluggedType(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetBatteryState().plugged;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getVoltage(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetBatteryState().voltage_mv;
}

__attribute__((sysv_abi)) int32_t ABatteryManager_getTemperature(ABatteryManager *mgr)
{
    if (!mgr || !mgr->mgr) {
        return 0;
    }
    return mgr->mgr->GetBatteryState().temperature_deci_c;
}

} // extern "C"
