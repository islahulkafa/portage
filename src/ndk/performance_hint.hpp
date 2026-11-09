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

namespace portage::ndk {

class PerformanceHintSession {
  public:
    PerformanceHintSession(const int32_t *thread_ids, size_t size,
                           int64_t initial_target_nanos) noexcept;
    ~PerformanceHintSession() = default;

    int UpdateTargetWorkDuration(int64_t target_duration_nanos) noexcept;
    int ReportActualWorkDuration(int64_t actual_duration_nanos) noexcept;
    int SetThreads(const int32_t *thread_ids, size_t size) noexcept;

    [[nodiscard]] int64_t GetTargetWorkDuration() const noexcept;
    [[nodiscard]] int64_t GetLastActualWorkDuration() const noexcept;
    [[nodiscard]] const std::vector<int32_t> &GetThreadIds() const noexcept;
    [[nodiscard]] bool IsBoosted() const noexcept;
    [[nodiscard]] uint32_t GetOvershootCount() const noexcept;

  private:
    mutable std::mutex mutex_{};
    std::vector<int32_t> thread_ids_{};
    int64_t target_duration_nanos_{16'666'666};
    int64_t last_actual_duration_nanos_{0};
    uint32_t overshoot_count_{0};
    uint32_t undershoot_count_{0};
    bool is_boosted_{false};
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
__attribute__((sysv_abi)) void APerformanceHintSession_close(APerformanceHintSession *session);
__attribute__((sysv_abi)) int APerformanceHintSession_setThreads(APerformanceHintSession *session,
                                                                 const int32_t *threadIds,
                                                                 size_t size);
}
