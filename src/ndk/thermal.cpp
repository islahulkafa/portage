#include "ndk/thermal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace portage::ndk {

ThermalManager &ThermalManager::Instance() noexcept
{
    static ThermalManager instance;
    return instance;
}

void ThermalManager::Reset() noexcept
{
    StopMonitor();
    std::lock_guard<std::mutex> lock(listener_mutex_);
    listeners_.clear();
    status_override_ = std::nullopt;
    headroom_override_ = std::nullopt;
    ref_count_.store(0, std::memory_order_release);
    prev_idle_time_ = 0;
    prev_kernel_time_ = 0;
    prev_user_time_ = 0;
    last_notified_status_ = ATHERMAL_STATUS_NONE;
}

AThermalManager *ThermalManager::Acquire() noexcept
{
    ref_count_.fetch_add(1, std::memory_order_acq_rel);
    return reinterpret_cast<AThermalManager *>(this);
}

void ThermalManager::Release(AThermalManager *manager) noexcept
{
    if (manager != reinterpret_cast<AThermalManager *>(this)) {
        return;
    }
    int32_t prev = ref_count_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 1) {
        ref_count_.store(0, std::memory_order_release);
    }
}

double ThermalManager::SampleHostCpuUtilization() noexcept
{
    FILETIME idle_time{}, kernel_time{}, user_time{};
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return 0.15;
    }

    uint64_t idle =
        (static_cast<uint64_t>(idle_time.dwHighDateTime) << 32) | idle_time.dwLowDateTime;
    uint64_t kernel =
        (static_cast<uint64_t>(kernel_time.dwHighDateTime) << 32) | kernel_time.dwLowDateTime;
    uint64_t user =
        (static_cast<uint64_t>(user_time.dwHighDateTime) << 32) | user_time.dwLowDateTime;

    if (prev_kernel_time_ == 0 && prev_user_time_ == 0) {
        prev_idle_time_ = idle;
        prev_kernel_time_ = kernel;
        prev_user_time_ = user;
        return 0.10;
    }

    uint64_t d_idle = idle - prev_idle_time_;
    uint64_t d_kernel = kernel - prev_kernel_time_;
    uint64_t d_user = user - prev_user_time_;

    prev_idle_time_ = idle;
    prev_kernel_time_ = kernel;
    prev_user_time_ = user;

    uint64_t total = d_kernel + d_user;
    if (total == 0) {
        return 0.10;
    }

    double busy = static_cast<double>(total - d_idle) / static_cast<double>(total);
    return std::clamp(busy, 0.0, 1.0);
}

AThermalStatus ThermalManager::GetCurrentThermalStatus() noexcept
{
    std::lock_guard<std::mutex> lock(listener_mutex_);
    if (status_override_.has_value()) {
        return *status_override_;
    }

    double util = SampleHostCpuUtilization();
    SYSTEM_POWER_STATUS sps{};
    bool on_battery_saver = false;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.SystemStatusFlag == 1) {
            on_battery_saver = true;
        }
    }

    if (util < 0.50) {
        return on_battery_saver ? ATHERMAL_STATUS_LIGHT : ATHERMAL_STATUS_NONE;
    } else if (util < 0.75) {
        return on_battery_saver ? ATHERMAL_STATUS_MODERATE : ATHERMAL_STATUS_LIGHT;
    } else if (util < 0.90) {
        return on_battery_saver ? ATHERMAL_STATUS_SEVERE : ATHERMAL_STATUS_MODERATE;
    }
    return ATHERMAL_STATUS_SEVERE;
}

float ThermalManager::GetThermalHeadroom(int forecast_seconds) noexcept
{
    std::lock_guard<std::mutex> lock(listener_mutex_);
    if (headroom_override_.has_value()) {
        return *headroom_override_;
    }

    double util = SampleHostCpuUtilization();
    float headroom = static_cast<float>(util * 0.95);

    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        if (sps.SystemStatusFlag == 1) {
            headroom = std::max(0.05f, headroom - 0.20f);
        }
    }

    int clamped_seconds = std::clamp(forecast_seconds, 0, 60);
    if (clamped_seconds > 0) {
        float horizon_factor = 1.0f - (static_cast<float>(clamped_seconds) * 0.005f);
        headroom *= horizon_factor;
    }

    return std::clamp(headroom, 0.05f, 1.25f);
}

void ThermalManager::StartMonitorLocked() noexcept
{
    if (monitor_running_) {
        return;
    }

    monitor_stop_ = false;
    monitor_running_ = true;
    last_notified_status_ = status_override_.value_or(ATHERMAL_STATUS_NONE);

    monitor_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(listener_mutex_);
        while (!monitor_stop_) {
            monitor_cv_.wait_for(lock, std::chrono::milliseconds(250),
                                 [this] { return monitor_stop_; });
            if (monitor_stop_) {
                break;
            }

            AThermalStatus current = ATHERMAL_STATUS_NONE;
            if (status_override_.has_value()) {
                current = *status_override_;
            } else {
                double util = SampleHostCpuUtilization();
                SYSTEM_POWER_STATUS sps{};
                bool on_battery_saver = false;
                if (GetSystemPowerStatus(&sps)) {
                    if (sps.SystemStatusFlag == 1) {
                        on_battery_saver = true;
                    }
                }
                if (util < 0.50) {
                    current = on_battery_saver ? ATHERMAL_STATUS_LIGHT : ATHERMAL_STATUS_NONE;
                } else if (util < 0.75) {
                    current = on_battery_saver ? ATHERMAL_STATUS_MODERATE : ATHERMAL_STATUS_LIGHT;
                } else if (util < 0.90) {
                    current = on_battery_saver ? ATHERMAL_STATUS_SEVERE : ATHERMAL_STATUS_MODERATE;
                } else {
                    current = ATHERMAL_STATUS_SEVERE;
                }
            }

            if (current != last_notified_status_) {
                last_notified_status_ = current;
                auto notify_list = listeners_;
                lock.unlock();
                for (const auto &l : notify_list) {
                    if (l.callback) {
                        l.callback(l.data, current);
                    }
                }
                lock.lock();
            }
        }
    });
}

void ThermalManager::StopMonitor() noexcept
{
    std::thread thread_to_join;
    {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        if (!monitor_running_) {
            return;
        }
        monitor_stop_ = true;
        monitor_running_ = false;
        monitor_cv_.notify_all();
        thread_to_join = std::move(monitor_thread_);
    }
    if (thread_to_join.joinable()) {
        thread_to_join.join();
    }
}

int ThermalManager::RegisterListener(AThermal_StatusCallback callback, void *data) noexcept
{
    if (!callback) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(listener_mutex_);
    for (const auto &entry : listeners_) {
        if (entry.callback == callback && entry.data == data) {
            return 0;
        }
    }

    listeners_.push_back(ThermalListenerEntry{callback, data});
    StartMonitorLocked();
    return 0;
}

int ThermalManager::UnregisterListener(AThermal_StatusCallback callback, void *data) noexcept
{
    if (!callback) {
        return -22; // -EINVAL
    }

    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        auto it = std::remove_if(listeners_.begin(), listeners_.end(),
                                 [&](const ThermalListenerEntry &e) {
                                     return e.callback == callback && e.data == data;
                                 });

        if (it != listeners_.end()) {
            listeners_.erase(it, listeners_.end());
            if (listeners_.empty()) {
                should_stop = true;
            }
        } else {
            return -22;
        }
    }

    if (should_stop) {
        StopMonitor();
    }
    return 0;
}

void ThermalManager::SetStatusOverride(std::optional<AThermalStatus> status) noexcept
{
    std::vector<ThermalListenerEntry> notify_list;
    AThermalStatus notify_status = ATHERMAL_STATUS_NONE;
    {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        status_override_ = status;
        if (status.has_value()) {
            notify_status = *status;
            last_notified_status_ = *status;
            notify_list = listeners_;
        }
        monitor_cv_.notify_all();
    }

    for (const auto &listener : notify_list) {
        if (listener.callback) {
            listener.callback(listener.data, notify_status);
        }
    }
}

void ThermalManager::SetHeadroomOverride(std::optional<float> headroom) noexcept
{
    std::lock_guard<std::mutex> lock(listener_mutex_);
    headroom_override_ = headroom;
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) AThermalManager *AThermalManager_acquireManager(void)
{
    return portage::ndk::ThermalManager::Instance().Acquire();
}

__attribute__((sysv_abi)) void AThermalManager_releaseManager(AThermalManager *manager)
{
    portage::ndk::ThermalManager::Instance().Release(manager);
}

__attribute__((sysv_abi)) AThermalStatus
AThermalManager_getCurrentThermalStatus(AThermalManager *manager)
{
    (void)manager;
    return portage::ndk::ThermalManager::Instance().GetCurrentThermalStatus();
}

__attribute__((sysv_abi)) int
AThermalManager_registerThermalStatusListener(AThermalManager *manager,
                                              AThermal_StatusCallback callback, void *data)
{
    (void)manager;
    return portage::ndk::ThermalManager::Instance().RegisterListener(callback, data);
}

__attribute__((sysv_abi)) int
AThermalManager_unregisterThermalStatusListener(AThermalManager *manager,
                                                AThermal_StatusCallback callback, void *data)
{
    (void)manager;
    return portage::ndk::ThermalManager::Instance().UnregisterListener(callback, data);
}

__attribute__((sysv_abi)) float AThermalManager_getThermalHeadroom(AThermalManager *manager,
                                                                   int forecastSeconds)
{
    (void)manager;
    return portage::ndk::ThermalManager::Instance().GetThermalHeadroom(forecastSeconds);
}

} // extern "C"
