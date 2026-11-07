#pragma once

#include "ndk/looper.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct AChoreographer;

using AChoreographer_frameCallback = void(__attribute__((sysv_abi)) *)(int64_t frameTimeNanos,
                                                                       void *data);
using AChoreographer_frameCallback64 = void(__attribute__((sysv_abi)) *)(int64_t frameTimeNanos,
                                                                         void *data);
using AChoreographer_refreshRateCallback =
    void(__attribute__((sysv_abi)) *)(int64_t vsyncPeriodNanos, void *data);

namespace portage::ndk {

class ChoreographerImpl;

class VsyncPacer {
  public:
    static constexpr int64_t kDefaultRefreshPeriodNanos = 16'666'667LL; // 60 Hz

    static VsyncPacer &Instance() noexcept;

    [[nodiscard]] static int64_t NowNanos() noexcept;

    [[nodiscard]] int64_t GetVsyncPeriodNanos() const noexcept;
    void SetRefreshRateHz(double hz);
    void ClearRefreshRateOverride();

    void RegisterChoreographer(const std::shared_ptr<ChoreographerImpl> &choreographer);
    void NotifyScheduled();
    void Shutdown() noexcept;

  private:
    VsyncPacer();
    ~VsyncPacer();

    VsyncPacer(const VsyncPacer &) = delete;
    VsyncPacer &operator=(const VsyncPacer &) = delete;

    void EnsureThreadStartedLocked();
    void PacerThreadFunc();
    [[nodiscard]] int64_t QueryAdaptivePeriodNanos() const noexcept;
    bool HasActiveWorkLocked();

    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::vector<std::weak_ptr<ChoreographerImpl>> choreographers_{};
    std::thread pacer_thread_{};
    bool thread_started_{false};
    bool stop_requested_{false};
    int64_t origin_nanos_{0};
    int64_t last_tick_nanos_{0};
    std::atomic<int64_t> vsync_period_nanos_{kDefaultRefreshPeriodNanos};
    std::atomic<int64_t> override_period_nanos_{0};
};

class ChoreographerImpl {
  public:
    ChoreographerImpl();
    ~ChoreographerImpl();

    ChoreographerImpl(const ChoreographerImpl &) = delete;
    ChoreographerImpl &operator=(const ChoreographerImpl &) = delete;
    ChoreographerImpl(ChoreographerImpl &&) = delete;
    ChoreographerImpl &operator=(ChoreographerImpl &&) = delete;

    static ChoreographerImpl *GetInstance();

    void PostFrameCallback(AChoreographer_frameCallback cb, void *data);
    void PostFrameCallbackDelayed(AChoreographer_frameCallback cb, void *data, int64_t delay_ms);
    void PostFrameCallback64(AChoreographer_frameCallback64 cb, void *data);
    void PostFrameCallbackDelayed64(AChoreographer_frameCallback64 cb, void *data,
                                    uint32_t delay_ms);

    void RegisterRefreshRateCallback(AChoreographer_refreshRateCallback cb, void *data);
    void UnregisterRefreshRateCallback(AChoreographer_refreshRateCallback cb, void *data);

    int DispatchCallbacks(int64_t frame_time_nanos);

    void OnVsyncTick(int64_t frame_time_nanos, int64_t vsync_period_nanos);
    void OnRefreshPeriodChanged(int64_t vsync_period_nanos);

    [[nodiscard]] bool HasPendingFrameCallbacks() const noexcept;
    [[nodiscard]] ALooper *GetLooper() const noexcept;

  private:
    struct PendingFrameCallback {
        bool is_64bit{true};
        AChoreographer_frameCallback cb32{nullptr};
        AChoreographer_frameCallback64 cb64{nullptr};
        void *data{nullptr};
        int64_t due_time_nanos{0};
    };

    struct RefreshRateListener {
        AChoreographer_refreshRateCallback callback{nullptr};
        void *data{nullptr};
        bool pending{false};
        int64_t period_nanos{0};
    };

    void EnsureLooperAttachedLocked();
    void DetachLooperLocked() noexcept;
    void SignalPipeLocked() noexcept;
    void DrainPipeLocked() noexcept;

    static int __attribute__((sysv_abi)) OnLooperSignaled(int fd, int events, void *data);

    mutable std::mutex mutex_{};
    LooperImpl *attached_looper_{nullptr};
    int internal_pipe_fd_{-1};
    void *read_pipe_{nullptr};
    void *write_pipe_{nullptr};
    bool pipe_signaled_{false};
    bool vsync_pending_{false};
    int64_t latest_vsync_nanos_{0};
    int64_t last_dispatched_frame_nanos_{0};
    int64_t last_known_period_nanos_{VsyncPacer::kDefaultRefreshPeriodNanos};
    std::vector<PendingFrameCallback> frame_callbacks_{};
    std::vector<RefreshRateListener> refresh_listeners_{};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) AChoreographer *AChoreographer_getInstance();
__attribute__((sysv_abi)) void
AChoreographer_postFrameCallback(AChoreographer *choreographer,
                                 AChoreographer_frameCallback callback, void *data);
__attribute__((sysv_abi)) void
AChoreographer_postFrameCallbackDelayed(AChoreographer *choreographer,
                                        AChoreographer_frameCallback callback, void *data,
                                        int64_t delayMillis);
__attribute__((sysv_abi)) void
AChoreographer_postFrameCallback64(AChoreographer *choreographer,
                                   AChoreographer_frameCallback64 callback, void *data);
__attribute__((sysv_abi)) void
AChoreographer_postFrameCallbackDelayed64(AChoreographer *choreographer,
                                          AChoreographer_frameCallback64 callback, void *data,
                                          uint32_t delayMillis);
__attribute__((sysv_abi)) void
AChoreographer_registerRefreshRateCallback(AChoreographer *choreographer,
                                           AChoreographer_refreshRateCallback callback, void *data);
__attribute__((sysv_abi)) void AChoreographer_unregisterRefreshRateCallback(
    AChoreographer *choreographer, AChoreographer_refreshRateCallback callback, void *data);
}
