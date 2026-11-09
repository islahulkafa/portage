#include "location/location_manager.hpp"
#include "location/location_ndk.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace portage::location;

TEST(LocationTest, LocationManagerDefaultCoordinates)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();

    auto fix = mgr.GetLastKnownLocation("gps");
    EXPECT_NEAR(fix.latitude, 37.4220, 0.0001);
    EXPECT_NEAR(fix.longitude, -122.0841, 0.0001);
    EXPECT_NEAR(fix.altitude, 10.0, 0.001);
    EXPECT_NEAR(fix.accuracy, 5.0f, 0.001f);
    EXPECT_EQ(fix.provider, "gps");
    EXPECT_GT(fix.time_ms, 0);
    EXPECT_GT(fix.elapsed_realtime_ns, 0);
}

TEST(LocationTest, LocationManagerCustomCoordinateInjection)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();

    mgr.SetLocation(40.7128, -74.0060, 15.0, 3.5f, 12.0f, 180.0f);
    auto fix = mgr.GetLastKnownLocation("gps");

    EXPECT_NEAR(fix.latitude, 40.7128, 0.0001);
    EXPECT_NEAR(fix.longitude, -74.0060, 0.0001);
    EXPECT_NEAR(fix.altitude, 15.0, 0.001);
    EXPECT_NEAR(fix.accuracy, 3.5f, 0.001f);
    EXPECT_NEAR(fix.speed, 12.0f, 0.001f);
    EXPECT_NEAR(fix.bearing, 180.0f, 0.001f);
}

TEST(LocationTest, LocationManagerProviderQueries)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();

    EXPECT_TRUE(mgr.IsProviderEnabled("gps"));
    EXPECT_TRUE(mgr.IsProviderEnabled("network"));
    EXPECT_TRUE(mgr.IsProviderEnabled("passive"));
    EXPECT_FALSE(mgr.IsProviderEnabled("invalid_provider"));

    mgr.SetProviderEnabled("gps", false);
    EXPECT_FALSE(mgr.IsProviderEnabled("gps"));
    mgr.SetProviderEnabled("gps", true);
    EXPECT_TRUE(mgr.IsProviderEnabled("gps"));

    auto provs = mgr.GetAllProviders();
    EXPECT_EQ(provs.size(), 3u);
}

namespace {
struct TestListenerContext {
    std::atomic<int> call_count{0};
    double last_lat{0.0};
    double last_lon{0.0};
};

__attribute__((sysv_abi)) void OnLocationChangedCallback(const ALocation *loc, void *ctx)
{
    if (loc && ctx) {
        auto *c = static_cast<TestListenerContext *>(ctx);
        c->last_lat = loc->latitude;
        c->last_lon = loc->longitude;
        c->call_count.fetch_add(1);
    }
}
} // namespace

TEST(LocationTest, LocationManagerDynamicUpdates)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();
    mgr.SetLocation(37.4220, -122.0841);

    TestListenerContext context;
    mgr.RequestLocationUpdates("gps", 10, 0.0f, &OnLocationChangedCallback, &context);

    mgr.SetSimulatedVelocity(20.0f, 90.0f); // 20 m/s East
    mgr.AdvanceSimulation(1.0f);

    for (int i = 0; i < 50 && context.call_count.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(context.call_count.load(), 0);
    EXPECT_GT(context.last_lon, -122.0841);

    mgr.RemoveLocationUpdates(&OnLocationChangedCallback);
}

namespace {
struct TestGeofenceContext {
    std::atomic<int> transition_count{0};
    int last_fence_id{0};
    int last_transition{0};
};

__attribute__((sysv_abi)) void OnGeofenceTransitionCallback(int fence_id, int transition,
                                                            const ALocation *, void *ctx)
{
    if (ctx) {
        auto *c = static_cast<TestGeofenceContext *>(ctx);
        c->last_fence_id = fence_id;
        c->last_transition = transition;
        c->transition_count.fetch_add(1);
    }
}
} // namespace

TEST(LocationTest, LocationManagerGeofenceEnterTransition)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();

    // Place starting position outside fence
    mgr.SetLocation(37.0000, -122.0000);

    TestGeofenceContext context;
    // Fence at (37.1000, -122.0000) with 1000 meter radius
    mgr.AddCircularGeofence(101, 37.1000, -122.0000, 1000.0f, 0, 0,
                           GEOFENCE_TRANSITION_ENTER | GEOFENCE_TRANSITION_EXIT,
                           &OnGeofenceTransitionCallback, &context);

    // Initial check while outside should not trigger Enter
    EXPECT_EQ(context.transition_count.load(), 0);

    // Move inside the fence: ~11 meters from fence center
    mgr.SetLocation(37.1001, -122.0000);

    EXPECT_GT(context.transition_count.load(), 0);
    EXPECT_EQ(context.last_fence_id, 101);
    EXPECT_EQ(context.last_transition, GEOFENCE_TRANSITION_ENTER);

    mgr.RemoveGeofence(101);
}

TEST(LocationTest, LocationManagerGeofenceExitTransition)
{
    auto &mgr = LocationManager::Instance();
    mgr.Reset();

    // Place starting position inside fence
    mgr.SetLocation(37.1000, -122.0000);

    TestGeofenceContext context;
    mgr.AddCircularGeofence(202, 37.1000, -122.0000, 500.0f, 0, 0,
                           GEOFENCE_TRANSITION_ENTER | GEOFENCE_TRANSITION_EXIT,
                           &OnGeofenceTransitionCallback, &context);

    // Initial check while inside triggers ENTER
    EXPECT_EQ(context.transition_count.load(), 1);
    EXPECT_EQ(context.last_transition, GEOFENCE_TRANSITION_ENTER);

    // Move outside the fence
    mgr.SetLocation(37.2000, -122.0000);

    EXPECT_EQ(context.transition_count.load(), 2);
    EXPECT_EQ(context.last_fence_id, 202);
    EXPECT_EQ(context.last_transition, GEOFENCE_TRANSITION_EXIT);

    mgr.RemoveGeofence(202);
}

TEST(LocationTest, SyntheticLocationJniReflection)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jclass context_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/Context"));
    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject context_obj = jenv->functions->NewObject(jenv, context_cls, ctor_mid);
    ASSERT_NE(context_obj, nullptr);

    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jstring loc_str = jenv->functions->NewStringUTF(jenv, "location");
    jobject loc_mgr = jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, loc_str);
    ASSERT_NE(loc_mgr, nullptr);

    jmethodID is_enabled_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("isProviderEnabled"));
    jstring gps_str = jenv->functions->NewStringUTF(jenv, "gps");
    jboolean gps_enabled =
        jenv->functions->CallBooleanMethod(jenv, loc_mgr, is_enabled_mid, gps_str);
    EXPECT_EQ(gps_enabled, JNI_TRUE);

    jmethodID get_last_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getLastKnownLocation"));
    jobject loc_obj = jenv->functions->CallObjectMethod(jenv, loc_mgr, get_last_mid, gps_str);
    ASSERT_NE(loc_obj, nullptr);

    jmethodID get_lat_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getLatitude"));
    jmethodID get_lon_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getLongitude"));
    jmethodID get_alt_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getAltitude"));
    jmethodID get_acc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getAccuracy"));
    jmethodID get_spd_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSpeed"));
    jmethodID get_brg_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getBearing"));
    jmethodID get_time_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getTime"));
    jmethodID get_elapsed_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getElapsedRealtimeNanos"));

    jdouble lat = jenv->functions->CallDoubleMethod(jenv, loc_obj, get_lat_mid);
    jdouble lon = jenv->functions->CallDoubleMethod(jenv, loc_obj, get_lon_mid);
    jdouble alt = jenv->functions->CallDoubleMethod(jenv, loc_obj, get_alt_mid);
    jfloat acc = jenv->functions->CallFloatMethod(jenv, loc_obj, get_acc_mid);
    jfloat spd = jenv->functions->CallFloatMethod(jenv, loc_obj, get_spd_mid);
    jfloat brg = jenv->functions->CallFloatMethod(jenv, loc_obj, get_brg_mid);
    jlong time_val = jenv->functions->CallLongMethod(jenv, loc_obj, get_time_mid);
    jlong elapsed_val = jenv->functions->CallLongMethod(jenv, loc_obj, get_elapsed_mid);

    EXPECT_GT(lat, 0.0);
    EXPECT_LT(lon, 0.0);
    EXPECT_GE(alt, 0.0);
    EXPECT_GT(acc, 0.0f);
    EXPECT_GE(spd, 0.0f);
    EXPECT_GE(brg, 0.0f);
    EXPECT_GT(time_val, 0);
    EXPECT_GT(elapsed_val, 0);

    jmethodID has_alt_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasAltitude"));
    jmethodID has_acc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasAccuracy"));
    EXPECT_EQ(jenv->functions->CallBooleanMethod(jenv, loc_obj, has_alt_mid), JNI_TRUE);
    EXPECT_EQ(jenv->functions->CallBooleanMethod(jenv, loc_obj, has_acc_mid), JNI_TRUE);

    jmethodID get_prov_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getProvider"));
    jstring prov_res = reinterpret_cast<jstring>(
        jenv->functions->CallObjectMethod(jenv, loc_obj, get_prov_mid));
    ASSERT_NE(prov_res, nullptr);
    const char *prov_chars = env.GetStringChars(prov_res);
    EXPECT_STREQ(prov_chars, "gps");
}

TEST(LocationTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("ALocationManager_create"), 0u);
    EXPECT_NE(registry.Lookup("ALocationManager_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ALocationManager_isProviderEnabled"), 0u);
    EXPECT_NE(registry.Lookup("ALocationManager_getLastKnownLocation"), 0u);
    EXPECT_NE(registry.Lookup("ALocationManager_requestLocationUpdates"), 0u);
    EXPECT_NE(registry.Lookup("ALocationManager_removeUpdates"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getLatitude"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getLongitude"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getAltitude"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getAccuracy"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getSpeed"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getBearing"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getTime"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_getElapsedRealtimeNanos"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_hasAltitude"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_hasSpeed"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_hasBearing"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_hasAccuracy"), 0u);
    EXPECT_NE(registry.Lookup("ALocation_destroy"), 0u);
    EXPECT_NE(registry.Lookup("AGeofenceManager_addCircularGeofence"), 0u);
    EXPECT_NE(registry.Lookup("AGeofenceManager_removeGeofence"), 0u);
}
