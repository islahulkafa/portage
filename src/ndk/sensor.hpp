#pragma once

#include "looper.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Android NDK sensor constants
enum {
    ASENSOR_TYPE_INVALID = -1,
    ASENSOR_TYPE_ACCELEROMETER = 1,
    ASENSOR_TYPE_MAGNETIC_FIELD = 2,
    ASENSOR_TYPE_ORIENTATION = 3,
    ASENSOR_TYPE_GYROSCOPE = 4,
    ASENSOR_TYPE_LIGHT = 5,
    ASENSOR_TYPE_PRESSURE = 6,
    ASENSOR_TYPE_TEMPERATURE = 7,
    ASENSOR_TYPE_PROXIMITY = 8,
    ASENSOR_TYPE_GRAVITY = 9,
    ASENSOR_TYPE_LINEAR_ACCELERATION = 10,
    ASENSOR_TYPE_ROTATION_VECTOR = 11,
    ASENSOR_TYPE_RELATIVE_HUMIDITY = 12,
    ASENSOR_TYPE_AMBIENT_TEMPERATURE = 13,
    ASENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED = 14,
    ASENSOR_TYPE_GAME_ROTATION_VECTOR = 15,
    ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED = 16,
    ASENSOR_TYPE_SIGNIFICANT_MOTION = 17,
    ASENSOR_TYPE_STEP_DETECTOR = 18,
    ASENSOR_TYPE_STEP_COUNTER = 19,
    ASENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR = 20,
    ASENSOR_TYPE_HEART_RATE = 21,
    ASENSOR_TYPE_ACCELEROMETER_UNCALIBRATED = 35,
};

enum {
    ASENSOR_STATUS_NO_CONTACT = -1,
    ASENSOR_STATUS_UNRELIABLE = 0,
    ASENSOR_STATUS_ACCURACY_LOW = 1,
    ASENSOR_STATUS_ACCURACY_MEDIUM = 2,
    ASENSOR_STATUS_ACCURACY_HIGH = 3,
};

enum {
    AREPORTING_MODE_INVALID = -1,
    AREPORTING_MODE_CONTINUOUS = 0,
    AREPORTING_MODE_ON_CHANGE = 1,
    AREPORTING_MODE_ONE_SHOT = 2,
    AREPORTING_MODE_SPECIAL_TRIGGER = 3,
};

enum {
    ASENSOR_DIRECT_RATE_STOP = 0,
    ASENSOR_DIRECT_RATE_NORMAL = 1,
    ASENSOR_DIRECT_RATE_FAST = 2,
    ASENSOR_DIRECT_RATE_VERY_FAST = 3,
};

enum {
    ASENSOR_DIRECT_CHANNEL_TYPE_SHARED_MEMORY = 1,
    ASENSOR_DIRECT_CHANNEL_TYPE_HARDWARE_BUFFER = 2,
};

constexpr float ASENSOR_STANDARD_GRAVITY = 9.80665f;
constexpr float ASENSOR_MAGNETIC_FIELD_EARTH_MAX = 60.0f;
constexpr float ASENSOR_MAGNETIC_FIELD_EARTH_MIN = 30.0f;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#endif

struct ASensorVector {
    union {
        float v[3];
        struct {
            float x;
            float y;
            float z;
        };
        struct {
            float azimuth;
            float pitch;
            float roll;
        };
    };
    int8_t status;
    uint8_t reserved[3];
};
static_assert(sizeof(ASensorVector) == 16, "ASensorVector must be 16 bytes");

struct ASensorEvent {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union {
        float data[16];
        ASensorVector vector;
        ASensorVector acceleration;
        ASensorVector magnetic;
        ASensorVector gyro;
        float temperature;
        float distance;
        float light;
        float pressure;
        float relative_humidity;
        struct {
            uint64_t data[8];
        } u64;
    };
    uint32_t flags;
    int32_t reserved1[3];
};
static_assert(sizeof(ASensorEvent) == 104, "ASensorEvent must be 104 bytes");

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

struct ASensor;
struct ASensorManager;
struct ASensorEventQueue;
struct AHardwareBuffer;

typedef const struct ASensor *ASensorRef;
typedef ASensorRef const *ASensorList;

namespace portage::ndk {

struct SensorDescriptor {
    int handle{0};
    int type{0};
    std::string name{};
    std::string vendor{};
    std::string string_type{};
    float resolution{0.001f};
    int min_delay_us{2000};
    int fifo_max{1000};
    int fifo_reserved{300};
    int reporting_mode{AREPORTING_MODE_CONTINUOUS};
    bool is_wake_up{false};
};

struct DevicePose {
    float pitch{0.0f};
    float roll{0.0f};
    float yaw{0.0f};
    float qx{0.0f};
    float qy{0.0f};
    float qz{0.0f};
    float qw{1.0f};
    float gyro_x{0.0f};
    float gyro_y{0.0f};
    float gyro_z{0.0f};
    float lin_accel_x{0.0f};
    float lin_accel_y{0.0f};
    float lin_accel_z{0.0f};
    float ambient_light_lux{250.0f};
};

class SensorEventQueueImpl {
  public:
    SensorEventQueueImpl(LooperImpl *looper, int ident, ALooper_callbackFunc callback, void *data);
    ~SensorEventQueueImpl();

    void DetachLooper();

    int EnableSensor(const SensorDescriptor *sensor, int32_t sampling_period_us = 16667);
    int DisableSensor(const SensorDescriptor *sensor);
    int SetEventRate(const SensorDescriptor *sensor, int32_t sampling_period_us);

    int HasEvents() const noexcept;
    int64_t GetEvents(ASensorEvent *events, size_t count);

    void PushEvent(const ASensorEvent &event);
    bool IsSensorEnabled(int sensor_handle, int32_t &out_rate_us) const;

    [[nodiscard]] bool HasAnySensorEnabled() const noexcept;
    [[nodiscard]] int32_t GetMinSamplingPeriodUs() const noexcept;

  private:
    void SignalPipe();
    void DrainPipe();

    mutable std::mutex mutex_{};
    LooperImpl *looper_{nullptr};
    int ident_{-1};
    ALooper_callbackFunc callback_{nullptr};
    void *callback_data_{nullptr};
    int internal_looper_fd_{-1};
    void *pipe_read_handle_{nullptr};
    void *pipe_write_handle_{nullptr};
    std::deque<ASensorEvent> events_{};
    std::unordered_map<int, int32_t> enabled_sensors_{};
    bool pipe_signaled_{false};
};

class SensorManagerImpl {
  public:
    static SensorManagerImpl &Instance() noexcept;

    SensorManagerImpl(const SensorManagerImpl &) = delete;
    SensorManagerImpl &operator=(const SensorManagerImpl &) = delete;

    int GetSensorList(ASensorList *list);
    const ASensor *GetDefaultSensor(int type) const;
    const ASensor *GetDefaultSensorEx(int type, bool wake_up) const;

    SensorEventQueueImpl *CreateEventQueue(LooperImpl *looper, int ident,
                                           ALooper_callbackFunc callback, void *data);
    int DestroyEventQueue(SensorEventQueueImpl *queue);

    // Host pose control APIs
    void ResetPose();
    void SetOrientationEuler(float pitch_rad, float roll_rad, float yaw_rad);
    void AdjustInteractiveTilt(float delta_pitch_rad, float delta_roll_rad);
    void SetGyroscopeRate(float wx, float wy, float wz);
    void SetLinearAcceleration(float ax, float ay, float az);
    void SetAmbientLightLux(float lux);

    DevicePose GetPose() const;

    void NotifyQueuesConfigChanged();

  private:
    SensorManagerImpl();
    ~SensorManagerImpl();

    void InitializeCatalog();
    void SamplingWorkerLoop();
    void GenerateEventsForPose(const DevicePose &pose, int64_t timestamp_ns);

    mutable std::mutex catalog_mutex_{};
    std::vector<std::unique_ptr<SensorDescriptor>> catalog_{};
    std::vector<ASensorRef> sensor_refs_{};

    mutable std::mutex queue_mutex_{};
    std::vector<std::shared_ptr<SensorEventQueueImpl>> queues_{};

    mutable std::mutex pose_mutex_{};
    DevicePose pose_{};

    std::atomic<bool> worker_running_{false};
    std::condition_variable worker_cv_{};
    std::thread worker_thread_{};
};

} // namespace portage::ndk

extern "C" {

// ASensorManager APIs
__attribute__((sysv_abi)) ASensorManager *ASensorManager_getInstance();
__attribute__((sysv_abi)) ASensorManager *
ASensorManager_getInstanceForPackage(const char *packageName);
__attribute__((sysv_abi)) int ASensorManager_getSensorList(ASensorManager *manager,
                                                           ASensorList *list);
__attribute__((sysv_abi)) ASensor const *ASensorManager_getDefaultSensor(ASensorManager *manager,
                                                                         int type);
__attribute__((sysv_abi)) ASensor const *ASensorManager_getDefaultSensorEx(ASensorManager *manager,
                                                                           int type, bool wakeUp);
__attribute__((sysv_abi)) ASensorEventQueue *
ASensorManager_createEventQueue(ASensorManager *manager, ALooper *looper, int ident,
                                ALooper_callbackFunc callback, void *data);
__attribute__((sysv_abi)) int ASensorManager_destroyEventQueue(ASensorManager *manager,
                                                               ASensorEventQueue *queue);
__attribute__((sysv_abi)) int
ASensorManager_createSharedMemoryDirectChannel(ASensorManager *manager, int fd, size_t size);
__attribute__((sysv_abi)) int
ASensorManager_createHardwareBufferDirectChannel(ASensorManager *manager,
                                                 struct AHardwareBuffer const *buffer, size_t size);
__attribute__((sysv_abi)) int ASensorManager_destroyDirectChannel(ASensorManager *manager,
                                                                  int channelId);
__attribute__((sysv_abi)) int ASensorManager_configureDirectReport(ASensorManager *manager,
                                                                   ASensor const *sensor,
                                                                   int channelId, int rate);

// ASensorEventQueue APIs
__attribute__((sysv_abi)) int ASensorEventQueue_registerSensor(ASensorEventQueue *queue,
                                                               ASensor const *sensor,
                                                               int32_t samplingPeriodUs,
                                                               int64_t maxBatchReportLatencyUs);
__attribute__((sysv_abi)) int ASensorEventQueue_enableSensor(ASensorEventQueue *queue,
                                                             ASensor const *sensor);
__attribute__((sysv_abi)) int ASensorEventQueue_disableSensor(ASensorEventQueue *queue,
                                                              ASensor const *sensor);
__attribute__((sysv_abi)) int ASensorEventQueue_setEventRate(ASensorEventQueue *queue,
                                                             ASensor const *sensor, int32_t usec);
__attribute__((sysv_abi)) int ASensorEventQueue_hasEvents(ASensorEventQueue *queue);
__attribute__((sysv_abi)) int64_t ASensorEventQueue_getEvents(ASensorEventQueue *queue,
                                                              ASensorEvent *events, size_t count);

// ASensor metadata APIs
__attribute__((sysv_abi)) const char *ASensor_getName(ASensor const *sensor);
__attribute__((sysv_abi)) const char *ASensor_getVendor(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getType(ASensor const *sensor);
__attribute__((sysv_abi)) float ASensor_getResolution(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getMinDelay(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getFifoMaxEventCount(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getFifoReservedEventCount(ASensor const *sensor);
__attribute__((sysv_abi)) const char *ASensor_getStringType(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getReportingMode(ASensor const *sensor);
__attribute__((sysv_abi)) bool ASensor_isWakeUpSensor(ASensor const *sensor);
__attribute__((sysv_abi)) bool ASensor_isDirectChannelTypeSupported(ASensor const *sensor,
                                                                    int channelType);
__attribute__((sysv_abi)) int ASensor_getHighestDirectReportRateLevel(ASensor const *sensor);
__attribute__((sysv_abi)) int ASensor_getHandle(ASensor const *sensor);

} // extern "C"
