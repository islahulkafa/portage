#pragma once

#include "binder_ndk.hpp"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::binder {

class ServiceManager {
  public:
    static ServiceManager &Instance() noexcept;

    void Reset();

    binder_status_t AddService(std::string_view name, AIBinder *binder);
    AIBinder *GetService(std::string_view name);
    AIBinder *CheckService(std::string_view name);

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
};

} // namespace portage::binder

extern "C" {

__attribute__((sysv_abi)) binder_status_t AServiceManager_addService(AIBinder *binder,
                                                                     const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_checkService(const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_getService(const char *instance);

__attribute__((sysv_abi)) AIBinder *AServiceManager_waitForService(const char *instance);

} // extern "C"
