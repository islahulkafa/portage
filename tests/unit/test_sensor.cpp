#include "ndk/libandroid_thunks.hpp"
#include "ndk/looper.hpp"
#include "ndk/sensor.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <thread>

namespace portage::ndk {

class SensorTest : public ::testing::Test {
  protected:
    void SetUp() override { SensorManagerImpl::Instance().ResetPose(); }

    void TearDown() override { SensorManagerImpl::Instance().ResetPose(); }
};

TEST_F(SensorTest, ExposesSensorCatalogAndMetadata)
{
    auto *mgr = ASensorManager_getInstance();
    ASSERT_NE(mgr, nullptr);

    auto *mgr_pkg = ASensorManager_getInstanceForPackage("com.portage.test");
    EXPECT_EQ(mgr, mgr_pkg);

    ASensorList list = nullptr;
    int count = ASensorManager_getSensorList(mgr, &list);
    EXPECT_GE(count, 8);
    ASSERT_NE(list, nullptr);

    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    ASSERT_NE(accel, nullptr);
    EXPECT_STREQ(ASensor_getName(accel), "Portage 3-Axis Accelerometer");
    EXPECT_STREQ(ASensor_getVendor(accel), "Portage Virtual Sensor");
    EXPECT_EQ(ASensor_getType(accel), ASENSOR_TYPE_ACCELEROMETER);
    EXPECT_STREQ(ASensor_getStringType(accel), "android.sensor.accelerometer");
    EXPECT_GT(ASensor_getResolution(accel), 0.0f);
    EXPECT_GT(ASensor_getMinDelay(accel), 0);
    EXPECT_GT(ASensor_getFifoMaxEventCount(accel), 0);
    EXPECT_GT(ASensor_getFifoReservedEventCount(accel), 0);
    EXPECT_EQ(ASensor_getReportingMode(accel), AREPORTING_MODE_CONTINUOUS);
    EXPECT_FALSE(ASensor_isWakeUpSensor(accel));
    EXPECT_EQ(ASensor_getHandle(accel), 1);

    EXPECT_FALSE(
        ASensor_isDirectChannelTypeSupported(accel, ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY));
    EXPECT_EQ(ASensor_getHighestDirectReportRateLevel(accel), ASENSOR_DIRECT_RATE_STOP);

    EXPECT_EQ(ASensorManager_createSharedMemoryDirectChannel(mgr, 0, 1024), -38);
    EXPECT_EQ(ASensorManager_createHardwareBufferDirectChannel(mgr, nullptr, 1024), -38);
    EXPECT_EQ(ASensorManager_destroyDirectChannel(mgr, 1), -38);
    EXPECT_EQ(ASensorManager_configureDirectReport(mgr, accel, 1, 1), -38);

    const auto *gyro = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_GYROSCOPE);
    ASSERT_NE(gyro, nullptr);
    EXPECT_EQ(ASensor_getType(gyro), ASENSOR_TYPE_GYROSCOPE);

    const auto *mag = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_MAGNETIC_FIELD);
    ASSERT_NE(mag, nullptr);
    EXPECT_EQ(ASensor_getType(mag), ASENSOR_TYPE_MAGNETIC_FIELD);

    const auto *light = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_LIGHT);
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(ASensor_getType(light), ASENSOR_TYPE_LIGHT);
    EXPECT_EQ(ASensor_getReportingMode(light), AREPORTING_MODE_ON_CHANGE);
}

TEST_F(SensorTest, DeliversCoherentEventsInIdentPollingMode)
{
    auto *looper = ALooper_prepare(0);
    ASSERT_NE(looper, nullptr);

    auto *mgr = ASensorManager_getInstance();
    ASSERT_NE(mgr, nullptr);

    constexpr int SENSOR_IDENT = 101;
    auto *queue = ASensorManager_createEventQueue(mgr, looper, SENSOR_IDENT, nullptr, nullptr);
    ASSERT_NE(queue, nullptr);

    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    const auto *rot = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ROTATION_VECTOR);
    ASSERT_NE(accel, nullptr);
    ASSERT_NE(rot, nullptr);

    EXPECT_EQ(ASensorEventQueue_registerSensor(queue, accel, 10000, 0), 0);
    EXPECT_EQ(ASensorEventQueue_registerSensor(queue, rot, 10000, 0), 0);

    int out_fd = 0;
    int out_events = 0;
    void *out_data = nullptr;
    int poll_res = ALooper_pollOnce(500, &out_fd, &out_events, &out_data);
    EXPECT_EQ(poll_res, SENSOR_IDENT);

    EXPECT_EQ(ASensorEventQueue_hasEvents(queue), 1);

    ASensorEvent events[16];
    int64_t count = ASensorEventQueue_getEvents(queue, events, 16);
    EXPECT_GT(count, 0);

    bool checked_accel = false;
    bool checked_rot = false;

    for (int64_t i = 0; i < count; ++i) {
        if (events[i].type == ASENSOR_TYPE_ACCELEROMETER) {
            float x = events[i].acceleration.x;
            float y = events[i].acceleration.y;
            float z = events[i].acceleration.z;
            float mag = std::sqrt(x * x + y * y + z * z);
            EXPECT_NEAR(mag, 9.80665f, 0.1f);
            EXPECT_NEAR(z, 9.80665f, 0.1f);
            EXPECT_NEAR(x, 0.0f, 0.05f);
            EXPECT_NEAR(y, 0.0f, 0.05f);
            EXPECT_EQ(events[i].acceleration.status, ASENSOR_STATUS_ACCURACY_HIGH);
            checked_accel = true;
        } else if (events[i].type == ASENSOR_TYPE_ROTATION_VECTOR) {
            EXPECT_NEAR(events[i].data[0], 0.0f, 0.01f);
            EXPECT_NEAR(events[i].data[1], 0.0f, 0.01f);
            EXPECT_NEAR(events[i].data[2], 0.0f, 0.01f);
            EXPECT_NEAR(events[i].data[3], 1.0f, 0.01f);
            checked_rot = true;
        }
    }

    EXPECT_TRUE(checked_accel || checked_rot);

    EXPECT_EQ(ASensorEventQueue_disableSensor(queue, accel), 0);
    EXPECT_EQ(ASensorEventQueue_disableSensor(queue, rot), 0);
    EXPECT_EQ(ASensorManager_destroyEventQueue(mgr, queue), 0);
}

TEST_F(SensorTest, UpdatesVectorsCoherentlyWhenTilted)
{
    auto *looper = ALooper_prepare(0);
    ASSERT_NE(looper, nullptr);

    auto *mgr = ASensorManager_getInstance();
    ASSERT_NE(mgr, nullptr);

    constexpr float PI_VAL = 3.14159265358979323846f;
    // Pitch tilted 90 degrees forward
    SensorManagerImpl::Instance().SetOrientationEuler(PI_VAL * 0.5f, 0.0f, 0.0f);

    auto *queue = ASensorManager_createEventQueue(mgr, looper, 102, nullptr, nullptr);
    ASSERT_NE(queue, nullptr);

    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    const auto *grav = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_GRAVITY);
    ASSERT_NE(accel, nullptr);
    ASSERT_NE(grav, nullptr);

    ASensorEventQueue_registerSensor(queue, accel, 10000, 0);
    ASensorEventQueue_registerSensor(queue, grav, 10000, 0);

    int poll_res = ALooper_pollOnce(500, nullptr, nullptr, nullptr);
    EXPECT_EQ(poll_res, 102);

    ASensorEvent events[16];
    int64_t count = ASensorEventQueue_getEvents(queue, events, 16);
    EXPECT_GT(count, 0);

    for (int64_t i = 0; i < count; ++i) {
        if (events[i].type == ASENSOR_TYPE_ACCELEROMETER ||
            events[i].type == ASENSOR_TYPE_GRAVITY) {
            float y = (events[i].type == ASENSOR_TYPE_ACCELEROMETER) ? events[i].acceleration.y
                                                                     : events[i].vector.y;
            float z = (events[i].type == ASENSOR_TYPE_ACCELEROMETER) ? events[i].acceleration.z
                                                                     : events[i].vector.z;
            EXPECT_NEAR(y, 9.80665f, 0.2f);
            EXPECT_NEAR(z, 0.0f, 0.2f);
        }
    }

    ASensorEventQueue_disableSensor(queue, accel);
    ASensorEventQueue_disableSensor(queue, grav);
    ASensorManager_destroyEventQueue(mgr, queue);
}

TEST_F(SensorTest, DispatchesLooperCallbackAndRespectsDisable)
{
    auto *looper = ALooper_prepare(0);
    ASSERT_NE(looper, nullptr);

    auto *mgr = ASensorManager_getInstance();
    ASSERT_NE(mgr, nullptr);

    struct CallbackState {
        int call_count{0};
        ASensorEventQueue *queue{nullptr};
    };

    CallbackState state{};

    auto callback_func = +[](int fd, int events, void *data) __attribute__((sysv_abi)) -> int {
        (void)fd;
        (void)events;
        auto *st = static_cast<CallbackState *>(data);
        st->call_count++;
        ASensorEvent ev[8];
        ASensorEventQueue_getEvents(st->queue, ev, 8);
        return 1;
    };

    auto *queue =
        ASensorManager_createEventQueue(mgr, looper, ALOOPER_POLL_CALLBACK, callback_func, &state);
    ASSERT_NE(queue, nullptr);
    state.queue = queue;

    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    ASSERT_NE(accel, nullptr);

    EXPECT_EQ(ASensorEventQueue_enableSensor(queue, accel), 0);
    EXPECT_EQ(ASensorEventQueue_setEventRate(queue, accel, 5000), 0);

    int poll_res = ALooper_pollOnce(500, nullptr, nullptr, nullptr);
    EXPECT_EQ(poll_res, ALOOPER_POLL_CALLBACK);
    EXPECT_GT(state.call_count, 0);

    EXPECT_EQ(ASensorEventQueue_disableSensor(queue, accel), 0);

    // Drain remaining
    ALooper_pollOnce(50, nullptr, nullptr, nullptr);
    int prev_count = state.call_count;

    // After disabling, no new events arrive
    ALooper_pollOnce(100, nullptr, nullptr, nullptr);
    EXPECT_EQ(state.call_count, prev_count);

    EXPECT_EQ(ASensorManager_destroyEventQueue(mgr, queue), 0);
}

TEST_F(SensorTest, RegistersAllNdkSymbols)
{
    const char *kSymbols[] = {
        "ASensorManager_getInstance",
        "ASensorManager_getInstanceForPackage",
        "ASensorManager_getSensorList",
        "ASensorManager_getDefaultSensor",
        "ASensorManager_getDefaultSensorEx",
        "ASensorManager_createEventQueue",
        "ASensorManager_destroyEventQueue",
        "ASensorManager_createSharedMemoryDirectChannel",
        "ASensorManager_createHardwareBufferDirectChannel",
        "ASensorManager_destroyDirectChannel",
        "ASensorManager_configureDirectReport",
        "ASensorEventQueue_registerSensor",
        "ASensorEventQueue_enableSensor",
        "ASensorEventQueue_disableSensor",
        "ASensorEventQueue_setEventRate",
        "ASensorEventQueue_hasEvents",
        "ASensorEventQueue_getEvents",
        "ASensor_getName",
        "ASensor_getVendor",
        "ASensor_getType",
        "ASensor_getResolution",
        "ASensor_getMinDelay",
        "ASensor_getFifoMaxEventCount",
        "ASensor_getFifoReservedEventCount",
        "ASensor_getStringType",
        "ASensor_getReportingMode",
        "ASensor_isWakeUpSensor",
        "ASensor_isDirectChannelTypeSupported",
        "ASensor_getHighestDirectReportRateLevel",
        "ASensor_getHandle",
    };

    for (const char *name : kSymbols) {
        uintptr_t addr = NdkSymbolRegistry::Instance().Lookup(name);
        EXPECT_NE(addr, 0u) << "Symbol missing in NdkSymbolRegistry: " << name;
    }
}

} // namespace portage::ndk
