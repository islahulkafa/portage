#include "sensor.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace portage::ndk {

namespace {

constexpr int64_t US_TO_NS = 1000;
constexpr size_t MAX_QUEUE_EVENTS = 256;
constexpr float PI = 3.14159265358979323846f;

int64_t GetMonotonicNanos() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void RotateVectorByQuaternion(float qx, float qy, float qz, float qw, float vx, float vy, float vz,
                              float &out_x, float &out_y, float &out_z) noexcept
{
    // Rotate vector by inverse of quaternion q (i.e. world to device frame)
    float ix = -qx;
    float iy = -qy;
    float iz = -qz;
    float iw = qw;

    // t = 2 * cross(i_xyz, v)
    float tx = 2.0f * (iy * vz - iz * vy);
    float ty = 2.0f * (iz * vx - ix * vz);
    float tz = 2.0f * (ix * vy - iy * vx);

    // v' = v + iw * t + cross(i_xyz, t)
    out_x = vx + iw * tx + (iy * tz - iz * ty);
    out_y = vy + iw * ty + (iz * tx - ix * tz);
    out_z = vz + iw * tz + (ix * ty - iy * tx);
}

} // namespace

SensorEventQueueImpl::SensorEventQueueImpl(LooperImpl *looper, int ident,
                                           ALooper_callbackFunc callback, void *data)
    : looper_(looper), ident_(ident), callback_(callback), callback_data_(data)
{
    HANDLE r_pipe = NULL;
    HANDLE w_pipe = NULL;
    if (CreatePipe(&r_pipe, &w_pipe, nullptr, 0)) {
        pipe_read_handle_ = r_pipe;
        pipe_write_handle_ = w_pipe;

        if (looper_) {
            internal_looper_fd_ =
                looper_->AddInternalPipe(pipe_read_handle_, callback_, callback_data_, ident_);
        }
    }
}

void SensorEventQueueImpl::DetachLooper()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (looper_ && internal_looper_fd_ >= 0) {
        looper_->RemoveFd(internal_looper_fd_);
        internal_looper_fd_ = -1;
    }
    enabled_sensors_.clear();
    events_.clear();
    DrainPipe();
}

SensorEventQueueImpl::~SensorEventQueueImpl()
{
    DetachLooper();

    if (pipe_write_handle_) {
        CloseHandle(static_cast<HANDLE>(pipe_write_handle_));
        pipe_write_handle_ = nullptr;
    }
    if (pipe_read_handle_) {
        CloseHandle(static_cast<HANDLE>(pipe_read_handle_));
        pipe_read_handle_ = nullptr;
    }
}

int SensorEventQueueImpl::EnableSensor(const SensorDescriptor *sensor, int32_t sampling_period_us)
{
    if (!sensor) {
        return -1;
    }

    int rate_us = (sampling_period_us > 0) ? sampling_period_us : 16667;
    rate_us = std::max(rate_us, sensor->min_delay_us);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_sensors_[sensor->handle] = rate_us;
    }

    SensorManagerImpl::Instance().NotifyQueuesConfigChanged();
    return 0;
}

int SensorEventQueueImpl::DisableSensor(const SensorDescriptor *sensor)
{
    if (!sensor) {
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_sensors_.erase(sensor->handle);
    }

    SensorManagerImpl::Instance().NotifyQueuesConfigChanged();
    return 0;
}

int SensorEventQueueImpl::SetEventRate(const SensorDescriptor *sensor, int32_t sampling_period_us)
{
    if (!sensor) {
        return -1;
    }

    int rate_us = (sampling_period_us > 0) ? sampling_period_us : 16667;
    rate_us = std::max(rate_us, sensor->min_delay_us);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = enabled_sensors_.find(sensor->handle);
        if (it != enabled_sensors_.end()) {
            it->second = rate_us;
        }
    }

    SensorManagerImpl::Instance().NotifyQueuesConfigChanged();
    return 0;
}

int SensorEventQueueImpl::HasEvents() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.empty() ? 0 : 1;
}

int64_t SensorEventQueueImpl::GetEvents(ASensorEvent *events, size_t count)
{
    if (!events || count == 0) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    size_t copied = 0;
    while (!events_.empty() && copied < count) {
        events[copied++] = events_.front();
        events_.pop_front();
    }

    if (events_.empty()) {
        DrainPipe();
    }

    return static_cast<int64_t>(copied);
}

void SensorEventQueueImpl::PushEvent(const ASensorEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool was_empty = events_.empty();
    if (events_.size() >= MAX_QUEUE_EVENTS) {
        events_.pop_front();
    }
    events_.push_back(event);

    if (was_empty) {
        SignalPipe();
    }
}

bool SensorEventQueueImpl::IsSensorEnabled(int sensor_handle, int32_t &out_rate_us) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = enabled_sensors_.find(sensor_handle);
    if (it != enabled_sensors_.end()) {
        out_rate_us = it->second;
        return true;
    }
    return false;
}

bool SensorEventQueueImpl::HasAnySensorEnabled() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !enabled_sensors_.empty();
}

int32_t SensorEventQueueImpl::GetMinSamplingPeriodUs() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    int32_t min_rate = 200000;
    for (const auto &[handle, rate] : enabled_sensors_) {
        min_rate = std::min(min_rate, rate);
    }
    return min_rate;
}

void SensorEventQueueImpl::SignalPipe()
{
    if (pipe_write_handle_ && !pipe_signaled_) {
        uint8_t byte = 0x01;
        DWORD written = 0;
        if (WriteFile(static_cast<HANDLE>(pipe_write_handle_), &byte, 1, &written, nullptr) &&
            written > 0) {
            pipe_signaled_ = true;
            if (looper_) {
                looper_->NotifyActivity();
            }
        }
    }
}

void SensorEventQueueImpl::DrainPipe()
{
    if (pipe_read_handle_ && pipe_signaled_) {
        uint8_t buf[64];
        DWORD bytes_avail = 0;
        if (PeekNamedPipe(static_cast<HANDLE>(pipe_read_handle_), nullptr, 0, nullptr, &bytes_avail,
                          nullptr) &&
            bytes_avail > 0) {
            DWORD read_bytes = 0;
            ReadFile(static_cast<HANDLE>(pipe_read_handle_), buf,
                     static_cast<DWORD>(std::min<size_t>(bytes_avail, sizeof(buf))), &read_bytes,
                     nullptr);
        }
        pipe_signaled_ = false;
    }
}

SensorManagerImpl &SensorManagerImpl::Instance() noexcept
{
    static SensorManagerImpl instance;
    return instance;
}

SensorManagerImpl::SensorManagerImpl()
{
    InitializeCatalog();
    ResetPose();

    worker_running_.store(true);
    worker_thread_ = std::thread([this]() { SamplingWorkerLoop(); });
}

SensorManagerImpl::~SensorManagerImpl()
{
    worker_running_.store(false);
    worker_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void SensorManagerImpl::InitializeCatalog()
{
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    catalog_.clear();
    sensor_refs_.clear();

    auto add_sensor = [this](int handle, int type, std::string name, std::string vendor,
                             std::string string_type, float res, int min_delay, int fifo_max,
                             int fifo_res, int mode, bool wake_up) {
        auto desc = std::make_unique<SensorDescriptor>(SensorDescriptor{
            .handle = handle,
            .type = type,
            .name = std::move(name),
            .vendor = std::move(vendor),
            .string_type = std::move(string_type),
            .resolution = res,
            .min_delay_us = min_delay,
            .fifo_max = fifo_max,
            .fifo_reserved = fifo_res,
            .reporting_mode = mode,
            .is_wake_up = wake_up,
        });
        sensor_refs_.push_back(reinterpret_cast<ASensorRef>(desc.get()));
        catalog_.push_back(std::move(desc));
    };

    add_sensor(1, ASENSOR_TYPE_ACCELEROMETER, "Portage 3-Axis Accelerometer",
               "Portage Virtual Sensor", "android.sensor.accelerometer", 0.001f, 2000, 1000, 300,
               AREPORTING_MODE_CONTINUOUS, false);

    add_sensor(2, ASENSOR_TYPE_MAGNETIC_FIELD, "Portage 3-Axis Magnetometer",
               "Portage Virtual Sensor", "android.sensor.magnetic_field", 0.1f, 10000, 500, 100,
               AREPORTING_MODE_CONTINUOUS, false);

    add_sensor(3, ASENSOR_TYPE_GYROSCOPE, "Portage 3-Axis Gyroscope", "Portage Virtual Sensor",
               "android.sensor.gyroscope", 0.001f, 2000, 1000, 300, AREPORTING_MODE_CONTINUOUS,
               false);

    add_sensor(4, ASENSOR_TYPE_LIGHT, "Portage Ambient Light Sensor", "Portage Virtual Sensor",
               "android.sensor.light", 1.0f, 50000, 100, 10, AREPORTING_MODE_ON_CHANGE, false);

    add_sensor(5, ASENSOR_TYPE_GRAVITY, "Portage Gravity Sensor", "Portage Virtual Sensor",
               "android.sensor.gravity", 0.001f, 2000, 1000, 300, AREPORTING_MODE_CONTINUOUS,
               false);

    add_sensor(6, ASENSOR_TYPE_LINEAR_ACCELERATION, "Portage Linear Acceleration Sensor",
               "Portage Virtual Sensor", "android.sensor.linear_acceleration", 0.001f, 2000, 1000,
               300, AREPORTING_MODE_CONTINUOUS, false);

    add_sensor(7, ASENSOR_TYPE_ROTATION_VECTOR, "Portage Rotation Vector Sensor",
               "Portage Virtual Sensor", "android.sensor.rotation_vector", 0.0001f, 2000, 1000, 300,
               AREPORTING_MODE_CONTINUOUS, false);

    add_sensor(8, ASENSOR_TYPE_GAME_ROTATION_VECTOR, "Portage Game Rotation Vector Sensor",
               "Portage Virtual Sensor", "android.sensor.game_rotation_vector", 0.0001f, 2000, 1000,
               300, AREPORTING_MODE_CONTINUOUS, false);
}

int SensorManagerImpl::GetSensorList(ASensorList *list)
{
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    if (list) {
        *list = sensor_refs_.data();
    }
    return static_cast<int>(sensor_refs_.size());
}

const ASensor *SensorManagerImpl::GetDefaultSensor(int type) const
{
    return GetDefaultSensorEx(type, false);
}

const ASensor *SensorManagerImpl::GetDefaultSensorEx(int type, bool wake_up) const
{
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    for (const auto &desc : catalog_) {
        if (desc->type == type && (desc->is_wake_up == wake_up || !wake_up)) {
            return reinterpret_cast<const ASensor *>(desc.get());
        }
    }
    return nullptr;
}

SensorEventQueueImpl *SensorManagerImpl::CreateEventQueue(LooperImpl *looper, int ident,
                                                          ALooper_callbackFunc callback, void *data)
{
    auto queue = std::make_shared<SensorEventQueueImpl>(looper, ident, callback, data);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queues_.push_back(queue);
    }
    return queue.get();
}

int SensorManagerImpl::DestroyEventQueue(SensorEventQueueImpl *queue)
{
    if (!queue) {
        return -1;
    }

    std::shared_ptr<SensorEventQueueImpl> to_destroy{};
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        auto it = std::find_if(queues_.begin(), queues_.end(),
                               [queue](const auto &q) { return q.get() == queue; });
        if (it != queues_.end()) {
            to_destroy = *it;
            queues_.erase(it);
        }
    }

    if (to_destroy) {
        to_destroy->DetachLooper();
    }

    NotifyQueuesConfigChanged();
    return to_destroy ? 0 : -1;
}

void SensorManagerImpl::ResetPose()
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_ = DevicePose{
        .pitch = 0.0f,
        .roll = 0.0f,
        .yaw = 0.0f,
        .qx = 0.0f,
        .qy = 0.0f,
        .qz = 0.0f,
        .qw = 1.0f,
        .gyro_x = 0.0f,
        .gyro_y = 0.0f,
        .gyro_z = 0.0f,
        .lin_accel_x = 0.0f,
        .lin_accel_y = 0.0f,
        .lin_accel_z = 0.0f,
        .ambient_light_lux = 250.0f,
    };
}

void SensorManagerImpl::SetOrientationEuler(float pitch_rad, float roll_rad, float yaw_rad)
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_.pitch = pitch_rad;
    pose_.roll = roll_rad;
    pose_.yaw = yaw_rad;

    float sx = std::sin(pitch_rad * 0.5f);
    float cx = std::cos(pitch_rad * 0.5f);
    float sy = std::sin(roll_rad * 0.5f);
    float cy = std::cos(roll_rad * 0.5f);
    float sz = std::sin(yaw_rad * 0.5f);
    float cz = std::cos(yaw_rad * 0.5f);

    pose_.qw = cz * cx * cy + sz * sx * sy;
    pose_.qx = cz * sx * cy + sz * cx * sy;
    pose_.qy = cz * cx * sy - sz * sx * cy;
    pose_.qz = sz * cx * cy - cz * sx * sy;
}

void SensorManagerImpl::AdjustInteractiveTilt(float delta_pitch_rad, float delta_roll_rad)
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_.pitch += delta_pitch_rad;
    pose_.roll += delta_roll_rad;

    // Clamp pitch/roll to [-pi, pi]
    if (pose_.pitch > PI)
        pose_.pitch -= 2.0f * PI;
    if (pose_.pitch < -PI)
        pose_.pitch += 2.0f * PI;
    if (pose_.roll > PI)
        pose_.roll -= 2.0f * PI;
    if (pose_.roll < -PI)
        pose_.roll += 2.0f * PI;

    float sx = std::sin(pose_.pitch * 0.5f);
    float cx = std::cos(pose_.pitch * 0.5f);
    float sy = std::sin(pose_.roll * 0.5f);
    float cy = std::cos(pose_.roll * 0.5f);
    float sz = std::sin(pose_.yaw * 0.5f);
    float cz = std::cos(pose_.yaw * 0.5f);

    pose_.qw = cz * cx * cy + sz * sx * sy;
    pose_.qx = cz * sx * cy + sz * cx * sy;
    pose_.qy = cz * cx * sy - sz * sx * cy;
    pose_.qz = sz * cx * cy - cz * sx * sy;

    // Transient angular velocity pulse from tilt
    pose_.gyro_x = delta_pitch_rad * 50.0f;
    pose_.gyro_y = delta_roll_rad * 50.0f;
}

void SensorManagerImpl::SetGyroscopeRate(float wx, float wy, float wz)
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_.gyro_x = wx;
    pose_.gyro_y = wy;
    pose_.gyro_z = wz;
}

void SensorManagerImpl::SetLinearAcceleration(float ax, float ay, float az)
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_.lin_accel_x = ax;
    pose_.lin_accel_y = ay;
    pose_.lin_accel_z = az;
}

void SensorManagerImpl::SetAmbientLightLux(float lux)
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_.ambient_light_lux = std::max(0.0f, lux);
}

DevicePose SensorManagerImpl::GetPose() const
{
    std::lock_guard<std::mutex> lock(pose_mutex_);
    return pose_;
}

void SensorManagerImpl::NotifyQueuesConfigChanged() { worker_cv_.notify_all(); }

void SensorManagerImpl::SamplingWorkerLoop()
{
    std::unordered_map<int64_t, int64_t> next_sample_times{};

    while (worker_running_.load()) {
        std::vector<std::shared_ptr<SensorEventQueueImpl>> active_queues{};
        int32_t min_period_us = 200000;
        bool has_active_sensors = false;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            for (const auto &q : queues_) {
                if (q->HasAnySensorEnabled()) {
                    has_active_sensors = true;
                    min_period_us = std::min(min_period_us, q->GetMinSamplingPeriodUs());
                    active_queues.push_back(q);
                }
            }
        }

        if (!has_active_sensors) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            worker_cv_.wait(lock, [this]() {
                if (!worker_running_.load())
                    return true;
                for (const auto &q : queues_) {
                    if (q->HasAnySensorEnabled())
                        return true;
                }
                return false;
            });
            next_sample_times.clear();
            continue;
        }

        min_period_us = std::clamp(min_period_us, 2000, 200000);
        int64_t now_ns = GetMonotonicNanos();

        DevicePose current_pose = GetPose();

        // Decay transient gyro rates smoothly
        {
            std::lock_guard<std::mutex> lock(pose_mutex_);
            pose_.gyro_x *= 0.9f;
            pose_.gyro_y *= 0.9f;
            pose_.gyro_z *= 0.9f;
            if (std::abs(pose_.gyro_x) < 0.001f)
                pose_.gyro_x = 0.0f;
            if (std::abs(pose_.gyro_y) < 0.001f)
                pose_.gyro_y = 0.0f;
            if (std::abs(pose_.gyro_z) < 0.001f)
                pose_.gyro_z = 0.0f;
        }

        // Compute physical sensor readings for current pose
        float gx = 0.0f, gy = 0.0f, gz = 0.0f;
        RotateVectorByQuaternion(current_pose.qx, current_pose.qy, current_pose.qz, current_pose.qw,
                                 0.0f, 0.0f, ASENSOR_STANDARD_GRAVITY, gx, gy, gz);

        float mx = 0.0f, my = 0.0f, mz = 0.0f;
        RotateVectorByQuaternion(current_pose.qx, current_pose.qy, current_pose.qz, current_pose.qw,
                                 0.0f, 25.0f, -40.0f, mx, my, mz);

        for (const auto &queue : active_queues) {
            for (const auto &desc : catalog_) {
                int32_t rate_us = 0;
                if (!queue->IsSensorEnabled(desc->handle, rate_us)) {
                    continue;
                }

                int64_t key =
                    (static_cast<int64_t>(reinterpret_cast<uintptr_t>(queue.get())) << 16) ^
                    desc->handle;
                auto it = next_sample_times.find(key);
                if (it != next_sample_times.end() && now_ns < it->second) {
                    continue;
                }

                int64_t period_ns = static_cast<int64_t>(rate_us) * US_TO_NS;
                next_sample_times[key] = now_ns + period_ns;

                ASensorEvent ev{};
                std::memset(&ev, 0, sizeof(ev));
                ev.version = sizeof(ASensorEvent);
                ev.sensor = desc->handle;
                ev.type = desc->type;
                ev.timestamp = now_ns;

                switch (desc->type) {
                case ASENSOR_TYPE_ACCELEROMETER:
                    ev.acceleration.x = gx + current_pose.lin_accel_x;
                    ev.acceleration.y = gy + current_pose.lin_accel_y;
                    ev.acceleration.z = gz + current_pose.lin_accel_z;
                    ev.acceleration.status = ASENSOR_STATUS_ACCURACY_HIGH;
                    break;
                case ASENSOR_TYPE_MAGNETIC_FIELD:
                    ev.magnetic.x = mx;
                    ev.magnetic.y = my;
                    ev.magnetic.z = mz;
                    ev.magnetic.status = ASENSOR_STATUS_ACCURACY_HIGH;
                    break;
                case ASENSOR_TYPE_GYROSCOPE:
                    ev.gyro.x = current_pose.gyro_x;
                    ev.gyro.y = current_pose.gyro_y;
                    ev.gyro.z = current_pose.gyro_z;
                    ev.gyro.status = ASENSOR_STATUS_ACCURACY_HIGH;
                    break;
                case ASENSOR_TYPE_LIGHT:
                    ev.light = current_pose.ambient_light_lux;
                    break;
                case ASENSOR_TYPE_GRAVITY:
                    ev.vector.x = gx;
                    ev.vector.y = gy;
                    ev.vector.z = gz;
                    ev.vector.status = ASENSOR_STATUS_ACCURACY_HIGH;
                    break;
                case ASENSOR_TYPE_LINEAR_ACCELERATION:
                    ev.vector.x = current_pose.lin_accel_x;
                    ev.vector.y = current_pose.lin_accel_y;
                    ev.vector.z = current_pose.lin_accel_z;
                    ev.vector.status = ASENSOR_STATUS_ACCURACY_HIGH;
                    break;
                case ASENSOR_TYPE_ROTATION_VECTOR:
                    ev.data[0] = current_pose.qx;
                    ev.data[1] = current_pose.qy;
                    ev.data[2] = current_pose.qz;
                    ev.data[3] = current_pose.qw;
                    ev.data[4] = 0.05f;
                    break;
                case ASENSOR_TYPE_GAME_ROTATION_VECTOR:
                    ev.data[0] = current_pose.qx;
                    ev.data[1] = current_pose.qy;
                    ev.data[2] = current_pose.qz;
                    ev.data[3] = current_pose.qw;
                    break;
                default:
                    break;
                }

                queue->PushEvent(ev);
            }
        }

        std::unique_lock<std::mutex> lock(queue_mutex_);
        worker_cv_.wait_for(lock, std::chrono::microseconds(min_period_us));
    }
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) ASensorManager *ASensorManager_getInstance()
{
    return reinterpret_cast<ASensorManager *>(&portage::ndk::SensorManagerImpl::Instance());
}

__attribute__((sysv_abi)) ASensorManager *
ASensorManager_getInstanceForPackage(const char *packageName)
{
    (void)packageName;
    return ASensorManager_getInstance();
}

__attribute__((sysv_abi)) int ASensorManager_getSensorList(ASensorManager *manager,
                                                           ASensorList *list)
{
    if (!manager)
        return -1;
    return reinterpret_cast<portage::ndk::SensorManagerImpl *>(manager)->GetSensorList(list);
}

__attribute__((sysv_abi)) ASensor const *ASensorManager_getDefaultSensor(ASensorManager *manager,
                                                                         int type)
{
    if (!manager)
        return nullptr;
    return reinterpret_cast<portage::ndk::SensorManagerImpl *>(manager)->GetDefaultSensor(type);
}

__attribute__((sysv_abi)) ASensor const *ASensorManager_getDefaultSensorEx(ASensorManager *manager,
                                                                           int type, bool wakeUp)
{
    if (!manager)
        return nullptr;
    return reinterpret_cast<portage::ndk::SensorManagerImpl *>(manager)->GetDefaultSensorEx(type,
                                                                                            wakeUp);
}

__attribute__((sysv_abi)) ASensorEventQueue *
ASensorManager_createEventQueue(ASensorManager *manager, ALooper *looper, int ident,
                                ALooper_callbackFunc callback, void *data)
{
    if (!manager)
        return nullptr;
    auto *l_impl = reinterpret_cast<portage::ndk::LooperImpl *>(looper);
    auto *q = reinterpret_cast<portage::ndk::SensorManagerImpl *>(manager)->CreateEventQueue(
        l_impl, ident, callback, data);
    return reinterpret_cast<ASensorEventQueue *>(q);
}

__attribute__((sysv_abi)) int ASensorManager_destroyEventQueue(ASensorManager *manager,
                                                               ASensorEventQueue *queue)
{
    if (!manager || !queue)
        return -1;
    auto *q_impl = reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue);
    return reinterpret_cast<portage::ndk::SensorManagerImpl *>(manager)->DestroyEventQueue(q_impl);
}

__attribute__((sysv_abi)) int
ASensorManager_createSharedMemoryDirectChannel(ASensorManager *manager, int fd, size_t size)
{
    (void)manager;
    (void)fd;
    (void)size;
    return -38; // -ENOSYS
}

__attribute__((sysv_abi)) int
ASensorManager_createHardwareBufferDirectChannel(ASensorManager *manager,
                                                 struct AHardwareBuffer const *buffer, size_t size)
{
    (void)manager;
    (void)buffer;
    (void)size;
    return -38; // -ENOSYS
}

__attribute__((sysv_abi)) int ASensorManager_destroyDirectChannel(ASensorManager *manager,
                                                                  int channelId)
{
    (void)manager;
    (void)channelId;
    return -38; // -ENOSYS
}

__attribute__((sysv_abi)) int ASensorManager_configureDirectReport(ASensorManager *manager,
                                                                   ASensor const *sensor,
                                                                   int channelId, int rate)
{
    (void)manager;
    (void)sensor;
    (void)channelId;
    (void)rate;
    return -38; // -ENOSYS
}

__attribute__((sysv_abi)) int ASensorEventQueue_registerSensor(ASensorEventQueue *queue,
                                                               ASensor const *sensor,
                                                               int32_t samplingPeriodUs,
                                                               int64_t maxBatchReportLatencyUs)
{
    (void)maxBatchReportLatencyUs;
    if (!queue || !sensor)
        return -1;
    auto *q = reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue);
    auto *s = reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor);
    return q->EnableSensor(s, samplingPeriodUs);
}

__attribute__((sysv_abi)) int ASensorEventQueue_enableSensor(ASensorEventQueue *queue,
                                                             ASensor const *sensor)
{
    if (!queue || !sensor)
        return -1;
    auto *q = reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue);
    auto *s = reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor);
    return q->EnableSensor(s);
}

__attribute__((sysv_abi)) int ASensorEventQueue_disableSensor(ASensorEventQueue *queue,
                                                              ASensor const *sensor)
{
    if (!queue || !sensor)
        return -1;
    auto *q = reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue);
    auto *s = reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor);
    return q->DisableSensor(s);
}

__attribute__((sysv_abi)) int ASensorEventQueue_setEventRate(ASensorEventQueue *queue,
                                                             ASensor const *sensor, int32_t usec)
{
    if (!queue || !sensor)
        return -1;
    auto *q = reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue);
    auto *s = reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor);
    return q->SetEventRate(s, usec);
}

__attribute__((sysv_abi)) int ASensorEventQueue_hasEvents(ASensorEventQueue *queue)
{
    if (!queue)
        return 0;
    return reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue)->HasEvents();
}

__attribute__((sysv_abi)) int64_t ASensorEventQueue_getEvents(ASensorEventQueue *queue,
                                                              ASensorEvent *events, size_t count)
{
    if (!queue)
        return -1;
    return reinterpret_cast<portage::ndk::SensorEventQueueImpl *>(queue)->GetEvents(events, count);
}

__attribute__((sysv_abi)) const char *ASensor_getName(ASensor const *sensor)
{
    if (!sensor)
        return nullptr;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->name.c_str();
}

__attribute__((sysv_abi)) const char *ASensor_getVendor(ASensor const *sensor)
{
    if (!sensor)
        return nullptr;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->vendor.c_str();
}

__attribute__((sysv_abi)) int ASensor_getType(ASensor const *sensor)
{
    if (!sensor)
        return ASENSOR_TYPE_INVALID;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->type;
}

__attribute__((sysv_abi)) float ASensor_getResolution(ASensor const *sensor)
{
    if (!sensor)
        return 0.0f;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->resolution;
}

__attribute__((sysv_abi)) int ASensor_getMinDelay(ASensor const *sensor)
{
    if (!sensor)
        return 0;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->min_delay_us;
}

__attribute__((sysv_abi)) int ASensor_getFifoMaxEventCount(ASensor const *sensor)
{
    if (!sensor)
        return 0;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->fifo_max;
}

__attribute__((sysv_abi)) int ASensor_getFifoReservedEventCount(ASensor const *sensor)
{
    if (!sensor)
        return 0;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->fifo_reserved;
}

__attribute__((sysv_abi)) const char *ASensor_getStringType(ASensor const *sensor)
{
    if (!sensor)
        return nullptr;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->string_type.c_str();
}

__attribute__((sysv_abi)) int ASensor_getReportingMode(ASensor const *sensor)
{
    if (!sensor)
        return AREPORTING_MODE_INVALID;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->reporting_mode;
}

__attribute__((sysv_abi)) bool ASensor_isWakeUpSensor(ASensor const *sensor)
{
    if (!sensor)
        return false;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->is_wake_up;
}

__attribute__((sysv_abi)) bool ASensor_isDirectChannelTypeSupported(ASensor const *sensor,
                                                                    int channelType)
{
    (void)sensor;
    (void)channelType;
    return false;
}

__attribute__((sysv_abi)) int ASensor_getHighestDirectReportRateLevel(ASensor const *sensor)
{
    (void)sensor;
    return ASENSOR_DIRECT_RATE_STOP;
}

__attribute__((sysv_abi)) int ASensor_getHandle(ASensor const *sensor)
{
    if (!sensor)
        return -1;
    return reinterpret_cast<const portage::ndk::SensorDescriptor *>(sensor)->handle;
}

} // extern "C"
