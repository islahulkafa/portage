#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"
#include "power/power_manager.hpp"
#include "power/power_ndk.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <thread>

using namespace portage::power;

TEST(PowerTest, PowerManagerDefaultBatteryState)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    EXPECT_TRUE(mgr.IsInteractive());
    EXPECT_FALSE(mgr.IsPowerSaveMode());
    EXPECT_FALSE(mgr.IsDeviceIdleMode());

    BatteryState state = mgr.GetBatteryState();
    EXPECT_TRUE(state.is_present);
    EXPECT_GE(state.capacity_percent, 0);
    EXPECT_LE(state.capacity_percent, 100);
    EXPECT_GT(state.voltage_mv, 0);
    EXPECT_EQ(state.health, BATTERY_HEALTH_GOOD);
}

TEST(PowerTest, PowerManagerCustomBatteryInjection)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    mgr.SetSimulatedBattery(72, false, 0);
    EXPECT_EQ(mgr.GetCapacity(), 72);
    EXPECT_FALSE(mgr.IsCharging());
    EXPECT_EQ(mgr.GetBatteryProperty(BATTERY_PROPERTY_CAPACITY), 72);
    EXPECT_EQ(mgr.GetBatteryProperty(BATTERY_PROPERTY_STATUS), BATTERY_STATUS_DISCHARGING);

    mgr.SetSimulatedBattery(100, true, BATTERY_PLUGGED_AC);
    EXPECT_EQ(mgr.GetCapacity(), 100);
    EXPECT_TRUE(mgr.IsCharging());
    EXPECT_EQ(mgr.GetBatteryProperty(BATTERY_PROPERTY_CAPACITY), 100);
    EXPECT_EQ(mgr.GetBatteryProperty(BATTERY_PROPERTY_STATUS), BATTERY_STATUS_FULL);

    mgr.ResetSimulatedBattery();
}

TEST(PowerTest, PowerManagerInteractiveAndPowerSaveMode)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    mgr.SetInteractive(false);
    EXPECT_FALSE(mgr.IsInteractive());
    mgr.SetInteractive(true);
    EXPECT_TRUE(mgr.IsInteractive());

    mgr.SetPowerSaveMode(true);
    EXPECT_TRUE(mgr.IsPowerSaveMode());
    mgr.SetPowerSaveMode(false);
    EXPECT_FALSE(mgr.IsPowerSaveMode());

    mgr.SetDeviceIdleMode(true);
    EXPECT_TRUE(mgr.IsDeviceIdleMode());
    mgr.SetDeviceIdleMode(false);
    EXPECT_FALSE(mgr.IsDeviceIdleMode());
}

TEST(PowerTest, WakeLockLifecycle)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    uint32_t id = mgr.CreateWakeLock(PARTIAL_WAKE_LOCK, "test_cpu_lock");
    EXPECT_GT(id, 0u);
    EXPECT_FALSE(mgr.IsWakeLockHeld(id));
    EXPECT_EQ(mgr.ActiveWakeLockCount(), 0u);

    EXPECT_TRUE(mgr.AcquireWakeLock(id));
    EXPECT_TRUE(mgr.IsWakeLockHeld(id));
    EXPECT_EQ(mgr.ActiveWakeLockCount(), 1u);

    EXPECT_TRUE(mgr.ReleaseWakeLock(id));
    EXPECT_FALSE(mgr.IsWakeLockHeld(id));
    EXPECT_EQ(mgr.ActiveWakeLockCount(), 0u);

    mgr.DestroyWakeLock(id);
}

TEST(PowerTest, WakeLockTimedAutoRelease)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    uint32_t id = mgr.CreateWakeLock(SCREEN_BRIGHT_WAKE_LOCK, "timed_screen_lock");
    EXPECT_TRUE(mgr.AcquireWakeLock(id, 40));
    EXPECT_TRUE(mgr.IsWakeLockHeld(id));

    std::this_thread::sleep_for(std::chrono::milliseconds(90));

    EXPECT_FALSE(mgr.IsWakeLockHeld(id));
    mgr.DestroyWakeLock(id);
}

TEST(PowerTest, WakeLockReferenceCounting)
{
    auto &mgr = PowerManager::Instance();
    mgr.Reset();

    uint32_t id = mgr.CreateWakeLock(PARTIAL_WAKE_LOCK, "ref_counted_lock");
    mgr.SetWakeLockReferenceCounted(id, true);

    EXPECT_TRUE(mgr.AcquireWakeLock(id));
    EXPECT_TRUE(mgr.AcquireWakeLock(id));
    EXPECT_TRUE(mgr.IsWakeLockHeld(id));

    EXPECT_TRUE(mgr.ReleaseWakeLock(id));
    EXPECT_TRUE(mgr.IsWakeLockHeld(id));

    EXPECT_TRUE(mgr.ReleaseWakeLock(id));
    EXPECT_FALSE(mgr.IsWakeLockHeld(id));

    EXPECT_FALSE(mgr.ReleaseWakeLock(id));
    mgr.DestroyWakeLock(id);
}

TEST(PowerTest, SyntheticPowerAndBatteryJniReflection)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    env.Reset();
    auto &mgr = PowerManager::Instance();
    mgr.Reset();
    mgr.SetSimulatedBattery(88, true, BATTERY_PLUGGED_AC);

    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);
    ASSERT_NE(jenv->functions, nullptr);

    jclass context_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/Context"));
    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject context_obj = jenv->functions->NewObject(jenv, context_cls, ctor_mid);
    ASSERT_NE(context_obj, nullptr);

    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jstring power_str = jenv->functions->NewStringUTF(jenv, "power");
    jobject power_mgr = jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, power_str);
    ASSERT_NE(power_mgr, nullptr);

    jmethodID is_inter_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("isInteractive"));
    jboolean is_inter = jenv->functions->CallBooleanMethod(jenv, power_mgr, is_inter_mid);
    EXPECT_EQ(is_inter, JNI_TRUE);

    jmethodID new_wl_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("newWakeLock"));
    jstring tag_str = jenv->functions->NewStringUTF(jenv, "jni_wake_lock");
    jobject wl_obj = jenv->functions->CallObjectMethod(
        jenv, power_mgr, new_wl_mid, static_cast<uintptr_t>(PARTIAL_WAKE_LOCK), tag_str);
    ASSERT_NE(wl_obj, nullptr);

    jmethodID is_held_mid = reinterpret_cast<jmethodID>(const_cast<char *>("isHeld"));
    EXPECT_EQ(jenv->functions->CallBooleanMethod(jenv, wl_obj, is_held_mid), JNI_FALSE);

    jmethodID acquire_mid = reinterpret_cast<jmethodID>(const_cast<char *>("acquire"));
    jenv->functions->CallVoidMethod(jenv, wl_obj, acquire_mid);
    EXPECT_EQ(jenv->functions->CallBooleanMethod(jenv, wl_obj, is_held_mid), JNI_TRUE);

    jmethodID release_mid = reinterpret_cast<jmethodID>(const_cast<char *>("release"));
    jenv->functions->CallVoidMethod(jenv, wl_obj, release_mid);
    EXPECT_EQ(jenv->functions->CallBooleanMethod(jenv, wl_obj, is_held_mid), JNI_FALSE);

    jstring bat_str = jenv->functions->NewStringUTF(jenv, "batterymanager");
    jobject bat_mgr = jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, bat_str);
    ASSERT_NE(bat_mgr, nullptr);

    jmethodID is_charging_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("isCharging"));
    jboolean is_charging = jenv->functions->CallBooleanMethod(jenv, bat_mgr, is_charging_mid);
    EXPECT_EQ(is_charging, JNI_TRUE);

    jmethodID get_prop_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getIntProperty"));
    jint cap = jenv->functions->CallIntMethod(
        jenv, bat_mgr, get_prop_mid, static_cast<uintptr_t>(BATTERY_PROPERTY_CAPACITY));
    EXPECT_EQ(cap, 88);

    jmethodID reg_rcv_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("registerReceiver"));
    jobject intent_obj = jenv->functions->CallObjectMethod(
        jenv, context_obj, reg_rcv_mid, nullptr, nullptr);
    ASSERT_NE(intent_obj, nullptr);

    jmethodID get_int_extra_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getIntExtra"));
    jstring level_str = jenv->functions->NewStringUTF(jenv, "level");
    jint level = jenv->functions->CallIntMethod(jenv, intent_obj, get_int_extra_mid, level_str, -1);
    EXPECT_EQ(level, 88);

    jstring scale_str = jenv->functions->NewStringUTF(jenv, "scale");
    jint scale = jenv->functions->CallIntMethod(jenv, intent_obj, get_int_extra_mid, scale_str, -1);
    EXPECT_EQ(scale, 100);

    mgr.ResetSimulatedBattery();
}

TEST(PowerTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("APowerManager_create"), 0u);
    EXPECT_NE(registry.Lookup("APowerManager_destroy"), 0u);
    EXPECT_NE(registry.Lookup("APowerManager_isInteractive"), 0u);
    EXPECT_NE(registry.Lookup("APowerManager_isPowerSaveMode"), 0u);
    EXPECT_NE(registry.Lookup("AWakeLock_create"), 0u);
    EXPECT_NE(registry.Lookup("AWakeLock_acquire"), 0u);
    EXPECT_NE(registry.Lookup("AWakeLock_release"), 0u);
    EXPECT_NE(registry.Lookup("AWakeLock_isHeld"), 0u);
    EXPECT_NE(registry.Lookup("AWakeLock_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_create"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getCapacity"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_isCharging"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getStatus"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getHealth"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getPluggedType"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getVoltage"), 0u);
    EXPECT_NE(registry.Lookup("ABatteryManager_getTemperature"), 0u);
}
