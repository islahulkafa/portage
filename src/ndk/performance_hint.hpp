#pragma once

#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct APerformanceHintManager;
typedef struct APerformanceHintManager APerformanceHintManager;

struct APerformanceHintSession;
typedef struct APerformanceHintSession APerformanceHintSession;

struct AWorkDuration {
    int64_t work_period_start_timestamp_nanos{0};
    int64_t actual_total_duration_nanos{0};
    int64_t actual_cpu_duration_nanos{0};
    int64_t actual_gpu_duration_nanos{0};
};
typedef struct AWorkDuration AWorkDuration;

enum SessionHint {
    SESSION_CPU_LOAD_UP = 0,
    SESSION_CPU_LOAD_DOWN = 1,
    SESSION_CPU_LOAD_RESET = 2,
    SESSION_CPU_LOAD_RESUME = 3,
    SESSION_GPU_LOAD_UP = 4,
    SESSION_GPU_LOAD_DOWN = 5,
    SESSION_GPU_LOAD_RESET = 6
};
typedef enum SessionHint SessionHint;

namespace portage::ndk {

class PerformanceHintSession {
  public:
    PerformanceHintSession(const int32_t *thread_ids, size_t size,
                           int64_t initial_target_nanos) noexcept;
    ~PerformanceHintSession() noexcept;

    int UpdateTargetWorkDuration(int64_t target_duration_nanos) noexcept;
    int ReportActualWorkDuration(int64_t actual_duration_nanos) noexcept;
    int ReportActualWorkDuration2(const AWorkDuration *work_duration) noexcept;
    int SendHint(SessionHint hint) noexcept;
    int SetPreferPowerEfficiency(bool enabled) noexcept;
    int SetThreads(const int32_t *thread_ids, size_t size) noexcept;

    [[nodiscard]] int64_t GetTargetWorkDuration() const noexcept;
    [[nodiscard]] int64_t GetLastActualWorkDuration() const noexcept;
    [[nodiscard]] const std::vector<int32_t> &GetThreadIds() const noexcept;
    [[nodiscard]] bool IsBoosted() const noexcept;
    [[nodiscard]] bool IsPowerEfficiencyPreferred() const noexcept;
    [[nodiscard]] uint32_t GetOvershootCount() const noexcept;

    void ApplyThreadQos(bool boost, bool power_efficient) noexcept;
    void RevertThreadQos() noexcept;

  private:
    mutable std::mutex mutex_{};
    std::vector<int32_t> thread_ids_{};
    int64_t target_duration_nanos_{16'666'666};
    int64_t last_actual_duration_nanos_{0};
    uint32_t overshoot_count_{0};
    uint32_t undershoot_count_{0};
    bool is_boosted_{false};
    bool is_power_efficient_{false};
};

class PerformanceHintManager {
  public:
    static PerformanceHintManager &Instance() noexcept;

    void Reset() noexcept;

    APerformanceHintManager *CreateManager() noexcept;
    APerformanceHintSession *CreateSession(const int32_t *thread_ids, size_t size,
                                           int64_t initial_target_nanos) noexcept;
    void CloseSession(APerformanceHintSession *session) noexcept;

    [[nodiscard]] int64_t GetPreferredUpdateRateNanos() const noexcept;
    [[nodiscard]] size_t GetActiveSessionCount() const noexcept;

  private:
    PerformanceHintManager() = default;
    ~PerformanceHintManager() = default;

    PerformanceHintManager(const PerformanceHintManager &) = delete;
    PerformanceHintManager &operator=(const PerformanceHintManager &) = delete;

    std::mutex mutex_{};
    std::vector<std::shared_ptr<PerformanceHintSession>> active_sessions_{};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) AWorkDuration *AWorkDuration_create(void);
__attribute__((sysv_abi)) void AWorkDuration_release(AWorkDuration *aWorkDuration);
__attribute__((sysv_abi)) void
AWorkDuration_setWorkPeriodStartTimestampNanos(AWorkDuration *aWorkDuration,
                                               int64_t workPeriodStartTimestampNanos);
__attribute__((sysv_abi)) void
AWorkDuration_setActualTotalDurationNanos(AWorkDuration *aWorkDuration,
                                          int64_t actualTotalDurationNanos);
__attribute__((sysv_abi)) void
AWorkDuration_setActualCpuDurationNanos(AWorkDuration *aWorkDuration,
                                        int64_t actualCpuDurationNanos);
__attribute__((sysv_abi)) void
AWorkDuration_setActualGpuDurationNanos(AWorkDuration *aWorkDuration,
                                        int64_t actualGpuDurationNanos);

__attribute__((sysv_abi)) APerformanceHintManager *APerformanceHintManager_create(void);
__attribute__((sysv_abi)) APerformanceHintSession *
APerformanceHintManager_createSession(APerformanceHintManager *manager, const int32_t *threadIds,
                                      size_t size, int64_t initialTargetWorkDurationNanos);
__attribute__((sysv_abi)) int64_t
APerformanceHintManager_getPreferredUpdateRateNanos(APerformanceHintManager *manager);
__attribute__((sysv_abi)) int
APerformanceHintSession_updateTargetWorkDuration(APerformanceHintSession *session,
                                                 int64_t targetDurationNanos);
__attribute__((sysv_abi)) int
APerformanceHintSession_reportActualWorkDuration(APerformanceHintSession *session,
                                                 int64_t actualDurationNanos);
__attribute__((sysv_abi)) int
APerformanceHintSession_reportActualWorkDuration2(APerformanceHintSession *session,
                                                  AWorkDuration *workDuration);
__attribute__((sysv_abi)) int APerformanceHintSession_sendHint(APerformanceHintSession *session,
                                                               SessionHint hint);
__attribute__((sysv_abi)) int
APerformanceHintSession_setPreferPowerEfficiency(APerformanceHintSession *session, bool enabled);
__attribute__((sysv_abi)) void APerformanceHintSession_close(APerformanceHintSession *session);
__attribute__((sysv_abi)) int APerformanceHintSession_setThreads(APerformanceHintSession *session,
                                                                 const int32_t *threadIds,
                                                                 size_t size);
}
