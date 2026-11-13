#pragma once

#include "binder_ndk.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

typedef void (*AServiceManager_NotificationRegistrationCallback)(const char *instance, void *cookie)
    __attribute__((sysv_abi));

namespace portage::binder {

struct ServiceNotificationRegistration {
    std::string instance{};
    AServiceManager_NotificationRegistrationCallback callback{nullptr};
    void *cookie{nullptr};
};

class ServiceManager {
  public:
    static ServiceManager &Instance() noexcept;

    void Reset();

    binder_status_t AddService(std::string_view name, AIBinder *binder);
    AIBinder *GetService(std::string_view name);
    AIBinder *CheckService(std::string_view name);
    bool IsDeclared(std::string_view name) const;

    void *RegisterForNotifications(std::string_view name,
                                   AServiceManager_NotificationRegistrationCallback callback,
                                   void *cookie);
    void UnregisterForNotifications(void *token);

    [[nodiscard]] size_t ServiceCount() const noexcept;

  private:
    ServiceManager();
    ~ServiceManager();

    ServiceManager(const ServiceManager &) = delete;
    ServiceManager &operator=(const ServiceManager &) = delete;

    void InitializeSyntheticServices();

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, AIBinder *> services_{};
    std::vector<AIBinder_Class *> synthetic_classes_{};
    std::unordered_set<std::string> declared_interfaces_{};
    std::vector<std::unique_ptr<ServiceNotificationRegistration>> notification_listeners_{};
};

} // namespace portage::binder

extern "C" {

__attribute__((sysv_abi)) binder_status_t AServiceManager_addService(AIBinder *binder,
                                                                     const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_checkService(const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_getService(const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_waitForService(const char *instance);

__attribute__((sysv_abi)) bool AServiceManager_isDeclared(const char *instance);

__attribute__((sysv_abi)) void *AServiceManager_registerForNotifications(
    const char *instance, AServiceManager_NotificationRegistrationCallback callback, void *cookie);

__attribute__((sysv_abi)) void
AServiceManager_unregisterForNotifications(void *notificationRegistration);

} // extern "C"
