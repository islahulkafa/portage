#include "service_manager.hpp"
#include "input/haptic_manager.hpp"
#include "play_billing_service.hpp"
#include "play_licensing_service.hpp"
#include "power/power_manager.hpp"

namespace portage::binder {

namespace {

__attribute__((sysv_abi)) binder_status_t VibratorTransact(AIBinder *binder,
                                                           transaction_code_t code,
                                                           const AParcel *in, AParcel *out)
{
    (void)binder;
    if (code == 1) {
        int32_t ms = 100;
        if (in) {
            AParcel_readInt32(in, &ms);
        }
        if (ms <= 0) {
            ms = 100;
        }
        input::HapticManager::Instance().Vibrate(0, 32767, 32767, static_cast<uint32_t>(ms));
    } else if (code == 2) {
        input::HapticManager::Instance().Stop(0);
    }

    if (out) {
        AParcel_writeInt32(out, 0);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t SurfaceFlingerTransact(AIBinder *binder,
                                                                 transaction_code_t code,
                                                                 const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0);
        AParcel_writeInt32(out, 1920);
        AParcel_writeInt32(out, 1080);
        AParcel_writeFloat(out, 60.0f);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t PowerManagerTransact(AIBinder *binder,
                                                               transaction_code_t code,
                                                               const AParcel *in, AParcel *out)
{
    (void)binder;
    if (code == 1) {
        if (out) {
            AParcel_writeInt32(out, 0);
            AParcel_writeBool(out, power::PowerManager::Instance().IsInteractive());
        }
        return STATUS_OK;
    }
    if (code == 2) {
        if (out) {
            AParcel_writeInt32(out, 0);
            AParcel_writeBool(out, power::PowerManager::Instance().IsPowerSaveMode());
        }
        return STATUS_OK;
    }
    if (code == 3) {
        int32_t flags = power::PARTIAL_WAKE_LOCK;
        if (in) {
            AParcel_readInt32(in, &flags);
        }
        uint32_t id = power::PowerManager::Instance().CreateWakeLock(flags, "binder_wake_lock");
        power::PowerManager::Instance().AcquireWakeLock(id);
        if (out) {
            AParcel_writeInt32(out, 0);
            AParcel_writeInt32(out, static_cast<int32_t>(id));
        }
        return STATUS_OK;
    }
    if (code == 4) {
        int32_t id = 0;
        if (in) {
            AParcel_readInt32(in, &id);
        }
        if (id > 0) {
            power::PowerManager::Instance().ReleaseWakeLock(static_cast<uint32_t>(id));
            power::PowerManager::Instance().DestroyWakeLock(static_cast<uint32_t>(id));
        }
        if (out) {
            AParcel_writeInt32(out, 0);
        }
        return STATUS_OK;
    }

    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0);
        AParcel_writeBool(out, true);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t PackageManagerTransact(AIBinder *binder,
                                                                 transaction_code_t code,
                                                                 const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0);
        AParcel_writeString(out, "com.portage.gameengine", -1);
        AParcel_writeInt32(out, 1);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t BatteryPropertiesTransact(AIBinder *binder,
                                                                    transaction_code_t code,
                                                                    const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0);
        AParcel_writeInt32(out, 1);
        AParcel_writeInt32(out, 1);
        AParcel_writeInt32(out, 100);
        AParcel_writeInt32(out, 2);
    }
    return STATUS_OK;
}

} // namespace

ServiceManager &ServiceManager::Instance() noexcept
{
    static ServiceManager instance;
    return instance;
}

ServiceManager::ServiceManager() { InitializeSyntheticServices(); }

ServiceManager::~ServiceManager() { Reset(); }

void ServiceManager::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[name, binder] : services_) {
        if (binder) {
            AIBinder_decStrong(binder);
        }
    }
    services_.clear();

    for (auto *cl : synthetic_classes_) {
        delete cl;
    }
    synthetic_classes_.clear();

    PlayLicensingService::Instance().Reset();
    PlayBillingService::Instance().Reset();

    InitializeSyntheticServices();
}

void ServiceManager::InitializeSyntheticServices()
{
    auto *sf_class = AIBinder_Class_define("android.ui.ISurfaceComposer", nullptr, nullptr,
                                           &SurfaceFlingerTransact);
    synthetic_classes_.push_back(sf_class);
    auto *sf_binder = AIBinder_new(sf_class, nullptr);
    services_["surfaceflinger"] = sf_binder;

    auto *power_class =
        AIBinder_Class_define("android.os.IPowerManager", nullptr, nullptr, &PowerManagerTransact);
    synthetic_classes_.push_back(power_class);
    auto *power_binder = AIBinder_new(power_class, nullptr);
    services_["power"] = power_binder;

    auto *pkg_class = AIBinder_Class_define("android.content.pm.IPackageManager", nullptr, nullptr,
                                            &PackageManagerTransact);
    synthetic_classes_.push_back(pkg_class);
    auto *pkg_binder = AIBinder_new(pkg_class, nullptr);
    services_["package"] = pkg_binder;

    auto *bat_class = AIBinder_Class_define("android.os.IBatteryPropertiesRegistrar", nullptr,
                                            nullptr, &BatteryPropertiesTransact);
    synthetic_classes_.push_back(bat_class);
    auto *bat_binder = AIBinder_new(bat_class, nullptr);
    services_["batteryproperties"] = bat_binder;
    AIBinder_incStrong(bat_binder);
    services_["batterystats"] = bat_binder;

    auto *vib_class =
        AIBinder_Class_define("android.os.IVibrator", nullptr, nullptr, &VibratorTransact);
    synthetic_classes_.push_back(vib_class);
    auto *vib_binder = AIBinder_new(vib_class, nullptr);
    services_["vibrator"] = vib_binder;

    auto *licensing_binder = PlayLicensingService::Instance().GetBinder();
    if (licensing_binder) {
        services_["com.android.vending.licensing.ILicensingService"] = licensing_binder;
    }

    auto *billing_binder = PlayBillingService::Instance().GetBinder();
    if (billing_binder) {
        services_["com.android.vending.billing.IInAppBillingService"] = billing_binder;
    }
}

binder_status_t ServiceManager::AddService(std::string_view name, AIBinder *binder)
{
    if (name.empty() || !binder) {
        return STATUS_BAD_VALUE;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(std::string(name));
    if (it != services_.end()) {
        if (it->second) {
            AIBinder_decStrong(it->second);
        }
    }
    AIBinder_incStrong(binder);
    services_[std::string(name)] = binder;
    return STATUS_OK;
}

AIBinder *ServiceManager::GetService(std::string_view name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(std::string(name));
    if (it != services_.end() && it->second) {
        AIBinder_incStrong(it->second);
        return it->second;
    }
    return nullptr;
}

AIBinder *ServiceManager::CheckService(std::string_view name) { return GetService(name); }

size_t ServiceManager::ServiceCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

} // namespace portage::binder

extern "C" {

__attribute__((sysv_abi)) binder_status_t AServiceManager_addService(AIBinder *binder,
                                                                     const char *instance)
{
    if (!binder || !instance) {
        return STATUS_BAD_VALUE;
    }
    return portage::binder::ServiceManager::Instance().AddService(instance, binder);
}

__attribute__((sysv_abi)) AIBinder *AServiceManager_checkService(const char *instance)
{
    if (!instance) {
        return nullptr;
    }
    return portage::binder::ServiceManager::Instance().CheckService(instance);
}

__attribute__((sysv_abi)) AIBinder *AServiceManager_getService(const char *instance)
{
    if (!instance) {
        return nullptr;
    }
    return portage::binder::ServiceManager::Instance().GetService(instance);
}

__attribute__((sysv_abi)) AIBinder *AServiceManager_waitForService(const char *instance)
{
    return AServiceManager_getService(instance);
}

} // extern "C"
