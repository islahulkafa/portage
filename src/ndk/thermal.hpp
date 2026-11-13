#pragma once

#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

enum AThermalStatus {
    ATHERMAL_STATUS_ERROR = -1,
    ATHERMAL_STATUS_NONE = 0,
    ATHERMAL_STATUS_LIGHT = 1,
    ATHERMAL_STATUS_MODERATE = 2,
    ATHERMAL_STATUS_SEVERE = 3,
    ATHERMAL_STATUS_CRITICAL = 4,
    ATHERMAL_STATUS_EMERGENCY = 5,
    ATHERMAL_STATUS_SHUTDOWN = 6
};
typedef enum AThermalStatus AThermalStatus;

struct AThermalManager;
typedef struct AThermalManager AThermalManager;

typedef void(__attribute__((sysv_abi)) * AThermal_StatusCallback)(void *data,
                                                                  AThermalStatus status);

namespace portage::ndk {

struct ThermalListenerEntry {
    AThermal_StatusCallback callback{nullptr};
    void *data{nullptr};
};

class ThermalManager {
  public:
    static ThermalManager &Instance() noexcept;

    void Reset() noexcept;

    AThermalManager *Acquire() noexcept;
    void Release(AThermalManager *manager) noexcept;

    AThermalStatus GetCurrentThermalStatus() noexcept;
    float GetThermalHeadroom(int forecast_seconds) noexcept;

    int RegisterListener(AThermal_StatusCallback callback, void *data) noexcept;
    int UnregisterListener(AThermal_StatusCallback callback, void *data) noexcept;

    void SetStatusOverride(std::optional<AThermalStatus> status) noexcept;
    void SetHeadroomOverride(std::optional<float> headroom) noexcept;

    double SampleHostCpuUtilization() noexcept;

    void StartMonitorLocked() noexcept;
    void StopMonitor() noexcept;

  private:
    ThermalManager() = default;
    ~ThermalManager() { StopMonitor(); }

    ThermalManager(const ThermalManager &) = delete;
    ThermalManager &operator=(const ThermalManager &) = delete;

    std::atomic<int32_t> ref_count_{0};
    std::mutex listener_mutex_{};
    std::vector<ThermalListenerEntry> listeners_{};

    std::optional<AThermalStatus> status_override_{std::nullopt};
    std::optional<float> headroom_override_{std::nullopt};

    uint64_t prev_idle_time_{0};
    uint64_t prev_kernel_time_{0};
    uint64_t prev_user_time_{0};

    std::thread monitor_thread_{};
    std::condition_variable monitor_cv_{};
    bool monitor_running_{false};
    bool monitor_stop_{false};
    AThermalStatus last_notified_status_{ATHERMAL_STATUS_NONE};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) AThermalManager *AThermalManager_acquireManager(void);
__attribute__((sysv_abi)) void AThermalManager_releaseManager(AThermalManager *manager);
__attribute__((sysv_abi)) AThermalStatus
AThermalManager_getCurrentThermalStatus(AThermalManager *manager);
__attribute__((sysv_abi)) int
AThermalManager_registerThermalStatusListener(AThermalManager *manager,
                                              AThermal_StatusCallback callback, void *data);
__attribute__((sysv_abi)) int
AThermalManager_unregisterThermalStatusListener(AThermalManager *manager,
                                                AThermal_StatusCallback callback, void *data);
__attribute__((sysv_abi)) float AThermalManager_getThermalHeadroom(AThermalManager *manager,
                                                                   int forecastSeconds);
}
