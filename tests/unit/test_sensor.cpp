#include "fs/fd_table.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/hardware_buffer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "ndk/looper.hpp"
#include "ndk/sensor.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstring>
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

    EXPECT_TRUE(
        ASensor_isDirectChannelTypeSupported(accel, ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY));
    EXPECT_TRUE(
        ASensor_isDirectChannelTypeSupported(accel, ASENSOR_DIRECT_CHANNEL_TYPE_HARDWARE_BUFFER));
    EXPECT_EQ(ASensor_getHighestDirectReportRateLevel(accel), ASENSOR_DIRECT_RATE_VERY_FAST);

    EXPECT_LT(ASensorManager_createSharedMemoryDirectChannel(mgr, -1, 1024), 0);
    EXPECT_LT(ASensorManager_createHardwareBufferDirectChannel(mgr, nullptr, 1024), 0);
    EXPECT_LT(ASensorManager_destroyDirectChannel(mgr, 9999), 0);
    EXPECT_LT(ASensorManager_configureDirectReport(mgr, accel, 9999, 1), 0);

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

TEST_F(SensorTest, DirectChannelCapabilityQueries)
{
    auto *mgr = ASensorManager_getInstance();
    ASSERT_NE(mgr, nullptr);

    const int supported_types[] = {
        ASENSOR_TYPE_ACCELEROMETER,
        ASENSOR_TYPE_GYROSCOPE,
        ASENSOR_TYPE_MAGNETIC_FIELD,
        ASENSOR_TYPE_LINEAR_ACCELERATION,
        ASENSOR_TYPE_GAME_ROTATION_VECTOR,
    };

    for (int type : supported_types) {
        const auto *s = ASensorManager_getDefaultSensor(mgr, type);
        ASSERT_NE(s, nullptr);
        EXPECT_TRUE(
            ASensor_isDirectChannelTypeSupported(s, ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY));
        EXPECT_TRUE(
            ASensor_isDirectChannelTypeSupported(s, ASENSOR_DIRECT_CHANNEL_TYPE_HARDWARE_BUFFER));
        EXPECT_FALSE(ASensor_isDirectChannelTypeSupported(s, 999));
        EXPECT_EQ(ASensor_getHighestDirectReportRateLevel(s), ASENSOR_DIRECT_RATE_VERY_FAST);
    }

    const auto *light = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_LIGHT);
    ASSERT_NE(light, nullptr);
    EXPECT_FALSE(
        ASensor_isDirectChannelTypeSupported(light, ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY));
    EXPECT_FALSE(
        ASensor_isDirectChannelTypeSupported(light, ASENSOR_DIRECT_CHANNEL_TYPE_HARDWARE_BUFFER));
    EXPECT_EQ(ASensor_getHighestDirectReportRateLevel(light), ASENSOR_DIRECT_RATE_STOP);

    EXPECT_FALSE(
        ASensor_isDirectChannelTypeSupported(nullptr, ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY));
    EXPECT_EQ(ASensor_getHighestDirectReportRateLevel(nullptr), ASENSOR_DIRECT_RATE_STOP);
}

TEST_F(SensorTest, DirectChannelSharedMemoryLifecycle)
{
    fs::FileDescriptorTable fd_table;
    syscall::SyscallDispatcher::Initialize(nullptr, nullptr, &fd_table, false);
    auto cleanup_syscall = [&]() { syscall::SyscallDispatcher::Shutdown(); };

    constexpr size_t kBufferSize = sizeof(ASensorEvent) * 16;
    auto shm = mem::SharedMemoryManager::Instance().CreateRegion("sensor_direct_shm", kBufferSize);
    ASSERT_NE(shm, nullptr);

    void *guest_view = mem::SharedMemoryManager::Instance().MapShared(
        *shm, nullptr, kBufferSize, 3 /* PROT_READ | PROT_WRITE */, 1 /* MAP_SHARED */, 0);
    ASSERT_NE(guest_view, nullptr);

    int shm_fd = fd_table.AllocateSharedMemory(shm, fs::DeviceType::Ashmem, 2, "/dev/ashmem");
    ASSERT_GE(shm_fd, 0);

    auto *mgr = ASensorManager_getInstance();
    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    ASSERT_NE(accel, nullptr);

    int channel_id = ASensorManager_createSharedMemoryDirectChannel(mgr, shm_fd, kBufferSize);
    ASSERT_GT(channel_id, 0);

    int token = ASensorManager_configureDirectReport(mgr, accel, channel_id,
                                                     ASENSOR_DIRECT_RATE_FAST);
    ASSERT_GT(token, 0);

    auto *ring = reinterpret_cast<const ASensorEvent *>(guest_view);
    bool got_valid_sequence = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (ring[0].reserved0 >= 1 && ring[1].reserved0 >= 2) {
            got_valid_sequence = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(got_valid_sequence);
    EXPECT_EQ(ring[0].version, static_cast<int32_t>(sizeof(ASensorEvent)));
    EXPECT_EQ(ring[0].sensor, ASensor_getHandle(accel));
    EXPECT_EQ(ring[0].type, ASENSOR_TYPE_ACCELEROMETER);
    EXPECT_GT(ring[0].timestamp, 0);
    EXPECT_NEAR(ring[0].acceleration.z, ASENSOR_STANDARD_GRAVITY, 0.05f);
    EXPECT_EQ(ring[1].reserved0, ring[0].reserved0 + 1);

    EXPECT_EQ(
        ASensorManager_configureDirectReport(mgr, accel, channel_id, ASENSOR_DIRECT_RATE_STOP), 0);
    EXPECT_EQ(ASensorManager_destroyDirectChannel(mgr, channel_id), 0);

    mem::SharedMemoryManager::Instance().UnmapShared(guest_view);
    cleanup_syscall();
}

TEST_F(SensorTest, DirectChannelHardwareBufferLifecycleAndWrapAround)
{
    constexpr size_t kSlots = 4;
    constexpr size_t kBufferSize = sizeof(ASensorEvent) * kSlots;

    AHardwareBuffer_Desc desc{};
    desc.width = static_cast<uint32_t>(kBufferSize);
    desc.height = 1;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
    desc.usage = AHARDWAREBUFFER_USAGE_SENSOR_DIRECT_DATA | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;

    AHardwareBuffer *ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &ahb), 0);
    ASSERT_NE(ahb, nullptr);

    void *locked_ptr = nullptr;
    ASSERT_EQ(AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr,
                                   &locked_ptr),
              0);
    ASSERT_NE(locked_ptr, nullptr);

    auto *mgr = ASensorManager_getInstance();
    const auto *gyro = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_GYROSCOPE);
    ASSERT_NE(gyro, nullptr);

    SensorManagerImpl::Instance().SetGyroscopeRate(0.25f, -0.5f, 1.0f);

    int channel_id = ASensorManager_createHardwareBufferDirectChannel(mgr, ahb, kBufferSize);
    ASSERT_GT(channel_id, 0);

    int token = ASensorManager_configureDirectReport(mgr, gyro, channel_id,
                                                     ASENSOR_DIRECT_RATE_VERY_FAST);
    ASSERT_GT(token, 0);

    auto *ring = reinterpret_cast<const ASensorEvent *>(locked_ptr);
    bool wrapped = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
        std::atomic_thread_fence(std::memory_order_acquire);
        // After > 4 events, slot 0 will have been overwritten with sequence >= 5
        if (ring[0].reserved0 >= static_cast<int32_t>(kSlots + 1)) {
            wrapped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_TRUE(wrapped);
    EXPECT_EQ(ring[0].version, static_cast<int32_t>(sizeof(ASensorEvent)));
    EXPECT_EQ(ring[0].sensor, ASensor_getHandle(gyro));
    EXPECT_EQ(ring[0].type, ASENSOR_TYPE_GYROSCOPE);
    EXPECT_GT(ring[0].timestamp, 0);

    EXPECT_EQ(ASensorManager_destroyDirectChannel(mgr, channel_id), 0);
    EXPECT_EQ(AHardwareBuffer_unlock(ahb, nullptr), 0);
    AHardwareBuffer_release(ahb);
}

TEST_F(SensorTest, DirectChannelMultipleSensorsAndRates)
{
    constexpr size_t kBufferSize = sizeof(ASensorEvent) * 32;

    AHardwareBuffer_Desc desc{};
    desc.width = static_cast<uint32_t>(kBufferSize);
    desc.height = 1;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
    desc.usage = AHARDWAREBUFFER_USAGE_SENSOR_DIRECT_DATA | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;

    AHardwareBuffer *ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &ahb), 0);

    void *locked_ptr = nullptr;
    ASSERT_EQ(AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr,
                                   &locked_ptr),
              0);

    auto *mgr = ASensorManager_getInstance();
    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    const auto *mag = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_MAGNETIC_FIELD);
    ASSERT_NE(accel, nullptr);
    ASSERT_NE(mag, nullptr);

    int channel_id = ASensorManager_createHardwareBufferDirectChannel(mgr, ahb, kBufferSize);
    ASSERT_GT(channel_id, 0);

    int token1 =
        ASensorManager_configureDirectReport(mgr, accel, channel_id, ASENSOR_DIRECT_RATE_FAST);
    int token2 =
        ASensorManager_configureDirectReport(mgr, mag, channel_id, ASENSOR_DIRECT_RATE_NORMAL);
    EXPECT_GT(token1, 0);
    EXPECT_GT(token2, 0);
    EXPECT_NE(token1, token2);

    auto *ring = reinterpret_cast<const ASensorEvent *>(locked_ptr);
    bool saw_accel = false;
    bool saw_mag = false;
    for (int attempt = 0; attempt < 60 && !(saw_accel && saw_mag); ++attempt) {
        std::atomic_thread_fence(std::memory_order_acquire);
        for (size_t i = 0; i < 32; ++i) {
            if (ring[i].reserved0 > 0) {
                if (ring[i].type == ASENSOR_TYPE_ACCELEROMETER)
                    saw_accel = true;
                if (ring[i].type == ASENSOR_TYPE_MAGNETIC_FIELD)
                    saw_mag = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(saw_accel);
    EXPECT_TRUE(saw_mag);

    EXPECT_EQ(ASensorManager_destroyDirectChannel(mgr, channel_id), 0);
    EXPECT_EQ(AHardwareBuffer_unlock(ahb, nullptr), 0);
    AHardwareBuffer_release(ahb);
}

TEST_F(SensorTest, DirectChannelInvalidArgsAndErrorHandling)
{
    auto *mgr = ASensorManager_getInstance();
    const auto *accel = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_ACCELEROMETER);
    const auto *light = ASensorManager_getDefaultSensor(mgr, ASENSOR_TYPE_LIGHT);
    ASSERT_NE(accel, nullptr);
    ASSERT_NE(light, nullptr);

    // Invalid size (< sizeof(ASensorEvent))
    EXPECT_LT(ASensorManager_createSharedMemoryDirectChannel(mgr, 1, 64), 0);
    EXPECT_LT(ASensorManager_createHardwareBufferDirectChannel(mgr, nullptr, 64), 0);

    // Non-BLOB or missing SENSOR_DIRECT_DATA usage AHardwareBuffer
    AHardwareBuffer_Desc bad_desc{};
    bad_desc.width = 16;
    bad_desc.height = 16;
    bad_desc.layers = 1;
    bad_desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    bad_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;

    AHardwareBuffer *bad_ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&bad_desc, &bad_ahb), 0);
    EXPECT_LT(ASensorManager_createHardwareBufferDirectChannel(mgr, bad_ahb, 1024), 0);
    AHardwareBuffer_release(bad_ahb);

    // Valid AHardwareBuffer but unsupported sensor (light) or bad rate
    AHardwareBuffer_Desc good_desc{};
    good_desc.width = 4096;
    good_desc.height = 1;
    good_desc.layers = 1;
    good_desc.format = AHARDWAREBUFFER_FORMAT_BLOB;
    good_desc.usage = AHARDWAREBUFFER_USAGE_SENSOR_DIRECT_DATA;

    AHardwareBuffer *good_ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&good_desc, &good_ahb), 0);
    int ch = ASensorManager_createHardwareBufferDirectChannel(mgr, good_ahb, 4096);
    ASSERT_GT(ch, 0);

    EXPECT_EQ(ASensorManager_configureDirectReport(mgr, light, ch, ASENSOR_DIRECT_RATE_NORMAL),
              -38);
    EXPECT_LT(ASensorManager_configureDirectReport(mgr, accel, ch, 999), 0);
    EXPECT_LT(ASensorManager_configureDirectReport(mgr, nullptr, ch, ASENSOR_DIRECT_RATE_NORMAL),
              0);

    EXPECT_EQ(ASensorManager_destroyDirectChannel(mgr, ch), 0);
    EXPECT_LT(ASensorManager_destroyDirectChannel(mgr, ch), 0);
    AHardwareBuffer_release(good_ahb);
}

} // namespace portage::ndk
