#include "service_manager.hpp"
#include "input/haptic_manager.hpp"

namespace portage::binder {

namespace {

__attribute__((sysv_abi)) binder_status_t
VibratorTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
    (void)binder;
    if (code == 1) { // vibrate
        int32_t ms = 100;
        if (in) {
            AParcel_readInt32(in, &ms);
        }
        if (ms <= 0) {
            ms = 100;
        }
        input::HapticManager::Instance().Vibrate(0, 32767, 32767, static_cast<uint32_t>(ms));
    } else if (code == 2) { // cancelVibrate
        input::HapticManager::Instance().Stop(0);
    }

    if (out) {
        AParcel_writeInt32(out, 0); // No exception
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
SurfaceFlingerTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0); // No exception
        AParcel_writeInt32(out, 1920);
        AParcel_writeInt32(out, 1080);
        AParcel_writeFloat(out, 60.0f);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
PowerManagerTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0); // No exception
        AParcel_writeBool(out, true); // isInteractive / state active
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
PackageManagerTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0); // No exception
        AParcel_writeString(out, "com.portage.gameengine", -1);
        AParcel_writeInt32(out, 1);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t
BatteryPropertiesTransact(AIBinder *binder, transaction_code_t code, const AParcel *in,
                          AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 0); // No exception
        AParcel_writeInt32(out, 1); // Battery present
        AParcel_writeInt32(out, 1); // Plugged AC
        AParcel_writeInt32(out, 100); // 100% level
        AParcel_writeInt32(out, 2); // Health GOOD
    }
    return STATUS_OK;
}

} // namespace

ServiceManager &ServiceManager::Instance() noexcept
{
    static ServiceManager instance;
    return instance;
}

ServiceManager::ServiceManager()
{
    InitializeSyntheticServices();
}

ServiceManager::~ServiceManager()
{
    Reset();
}

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

    InitializeSyntheticServices();
}

void ServiceManager::InitializeSyntheticServices()
{
    // surfaceflinger
    auto *sf_class = AIBinder_Class_define("android.ui.ISurfaceComposer", nullptr, nullptr,
                                           &SurfaceFlingerTransact);
    synthetic_classes_.push_back(sf_class);
    auto *sf_binder = AIBinder_new(sf_class, nullptr);
    services_["surfaceflinger"] = sf_binder;

    // power
    auto *power_class =
        AIBinder_Class_define("android.os.IPowerManager", nullptr, nullptr, &PowerManagerTransact);
    synthetic_classes_.push_back(power_class);
    auto *power_binder = AIBinder_new(power_class, nullptr);
    services_["power"] = power_binder;

    // package
    auto *pkg_class = AIBinder_Class_define("android.content.pm.IPackageManager", nullptr, nullptr,
                                            &PackageManagerTransact);
    synthetic_classes_.push_back(pkg_class);
    auto *pkg_binder = AIBinder_new(pkg_class, nullptr);
    services_["package"] = pkg_binder;

    // batteryproperties & batterystats
    auto *bat_class = AIBinder_Class_define("android.os.IBatteryPropertiesRegistrar", nullptr,
                                            nullptr, &BatteryPropertiesTransact);
    synthetic_classes_.push_back(bat_class);
    auto *bat_binder = AIBinder_new(bat_class, nullptr);
    services_["batteryproperties"] = bat_binder;
    AIBinder_incStrong(bat_binder);
    services_["batterystats"] = bat_binder;

    // vibrator
    auto *vib_class =
        AIBinder_Class_define("android.os.IVibrator", nullptr, nullptr, &VibratorTransact);
    synthetic_classes_.push_back(vib_class);
    auto *vib_binder = AIBinder_new(vib_class, nullptr);
    services_["vibrator"] = vib_binder;
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

AIBinder *ServiceManager::CheckService(std::string_view name)
{
    return GetService(name);
}

size_t ServiceManager::ServiceCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

} // namespace portage::binder

extern "C" {

__attribute__((sysv_abi)) binder_status_t
AServiceManager_addService(AIBinder *binder, const char *instance)
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
