#include "ndk/performance_hint.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <processthreadsapi.h>

#include <algorithm>

namespace portage::ndk {

PerformanceHintSession::PerformanceHintSession(const int32_t *thread_ids, size_t size,
                                               int64_t initial_target_nanos) noexcept
    : target_duration_nanos_(initial_target_nanos > 0 ? initial_target_nanos : 16'666'666)
{
    if (thread_ids && size > 0) {
        thread_ids_.assign(thread_ids, thread_ids + size);
    }
}

PerformanceHintSession::~PerformanceHintSession() noexcept
{
    RevertThreadQos();
}

void PerformanceHintSession::ApplyThreadQos(bool boost, bool power_efficient) noexcept
{
    for (int32_t tid : thread_ids_) {
        auto th = thread::ThreadManager::Instance().GetThread(tid);
        if (!th || th->win32_handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        int priority = THREAD_PRIORITY_NORMAL;
        if (boost) {
            priority = THREAD_PRIORITY_HIGHEST;
        } else if (power_efficient) {
            priority = THREAD_PRIORITY_BELOW_NORMAL;
        }
        SetThreadPriority(th->win32_handle, priority);

        THREAD_POWER_THROTTLING_STATE throttling{};
        throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        if (power_efficient && !boost) {
            throttling.StateMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        } else {
            throttling.StateMask = 0;
        }
        SetThreadInformation(th->win32_handle, ThreadPowerThrottling, &throttling,
                             sizeof(throttling));
    }
}

void PerformanceHintSession::RevertThreadQos() noexcept
{
    for (int32_t tid : thread_ids_) {
        auto th = thread::ThreadManager::Instance().GetThread(tid);
        if (!th || th->win32_handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        SetThreadPriority(th->win32_handle, THREAD_PRIORITY_NORMAL);

        THREAD_POWER_THROTTLING_STATE throttling{};
        throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        throttling.StateMask = 0;
        SetThreadInformation(th->win32_handle, ThreadPowerThrottling, &throttling,
                             sizeof(throttling));
    }
}

int PerformanceHintSession::UpdateTargetWorkDuration(int64_t target_duration_nanos) noexcept
{
    if (target_duration_nanos <= 0) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(mutex_);
    target_duration_nanos_ = target_duration_nanos;
    return 0;
}

int PerformanceHintSession::ReportActualWorkDuration(int64_t actual_duration_nanos) noexcept
{
    if (actual_duration_nanos <= 0) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(mutex_);
    last_actual_duration_nanos_ = actual_duration_nanos;

    if (target_duration_nanos_ > 0 &&
        actual_duration_nanos > (target_duration_nanos_ * 115) / 100) {
        overshoot_count_++;
        is_boosted_ = true;
    } else {
        undershoot_count_++;
        is_boosted_ = false;
    }
    ApplyThreadQos(is_boosted_, is_power_efficient_);
    return 0;
}

int PerformanceHintSession::ReportActualWorkDuration2(const AWorkDuration *work_duration) noexcept
{
    if (!work_duration || work_duration->actual_total_duration_nanos <= 0) {
        return -22; // -EINVAL
    }
    return ReportActualWorkDuration(work_duration->actual_total_duration_nanos);
}

int PerformanceHintSession::SendHint(SessionHint hint) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (hint == SESSION_CPU_LOAD_UP || hint == SESSION_GPU_LOAD_UP) {
        is_boosted_ = true;
        ApplyThreadQos(true, is_power_efficient_);
        return 0;
    }
    if (hint == SESSION_CPU_LOAD_DOWN || hint == SESSION_GPU_LOAD_DOWN) {
        is_boosted_ = false;
        ApplyThreadQos(false, is_power_efficient_);
        return 0;
    }
    if (hint == SESSION_CPU_LOAD_RESET || hint == SESSION_GPU_LOAD_RESET) {
        is_boosted_ = false;
        overshoot_count_ = 0;
        undershoot_count_ = 0;
        ApplyThreadQos(false, is_power_efficient_);
        return 0;
    }
    if (hint == SESSION_CPU_LOAD_RESUME) {
        ApplyThreadQos(is_boosted_, is_power_efficient_);
        return 0;
    }
    return -22; // -EINVAL
}

int PerformanceHintSession::SetPreferPowerEfficiency(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    is_power_efficient_ = enabled;
    ApplyThreadQos(is_boosted_, is_power_efficient_);
    return 0;
}

int PerformanceHintSession::SetThreads(const int32_t *thread_ids, size_t size) noexcept
{
    if (!thread_ids || size == 0) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(mutex_);
    RevertThreadQos();
    thread_ids_.assign(thread_ids, thread_ids + size);
    ApplyThreadQos(is_boosted_, is_power_efficient_);
    return 0;
}

int64_t PerformanceHintSession::GetTargetWorkDuration() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return target_duration_nanos_;
}

int64_t PerformanceHintSession::GetLastActualWorkDuration() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_actual_duration_nanos_;
}

const std::vector<int32_t> &PerformanceHintSession::GetThreadIds() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_ids_;
}

bool PerformanceHintSession::IsBoosted() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return is_boosted_;
}

bool PerformanceHintSession::IsPowerEfficiencyPreferred() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return is_power_efficient_;
}

uint32_t PerformanceHintSession::GetOvershootCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return overshoot_count_;
}

PerformanceHintManager &PerformanceHintManager::Instance() noexcept
{
    static PerformanceHintManager instance;
    return instance;
}

void PerformanceHintManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &s : active_sessions_) {
        if (s) {
            s->RevertThreadQos();
        }
    }
    active_sessions_.clear();
}

APerformanceHintManager *PerformanceHintManager::CreateManager() noexcept
{
    return reinterpret_cast<APerformanceHintManager *>(this);
}

APerformanceHintSession *
PerformanceHintManager::CreateSession(const int32_t *thread_ids, size_t size,
                                      int64_t initial_target_nanos) noexcept
{
    auto session = std::make_shared<PerformanceHintSession>(thread_ids, size, initial_target_nanos);

    std::lock_guard<std::mutex> lock(mutex_);
    active_sessions_.push_back(session);
    return reinterpret_cast<APerformanceHintSession *>(session.get());
}

void PerformanceHintManager::CloseSession(APerformanceHintSession *session) noexcept
{
    if (!session) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &s : active_sessions_) {
        if (reinterpret_cast<APerformanceHintSession *>(s.get()) == session) {
            s->RevertThreadQos();
            break;
        }
    }
    auto it =
        std::remove_if(active_sessions_.begin(), active_sessions_.end(),
                       [&](const std::shared_ptr<PerformanceHintSession> &s) {
                           return reinterpret_cast<APerformanceHintSession *>(s.get()) == session;
                       });
    if (it != active_sessions_.end()) {
        active_sessions_.erase(it, active_sessions_.end());
    }
}

int64_t PerformanceHintManager::GetPreferredUpdateRateNanos() const noexcept { return 16'666'666; }

size_t PerformanceHintManager::GetActiveSessionCount() const noexcept
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
    return active_sessions_.size();
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) AWorkDuration *AWorkDuration_create(void)
{
    return new AWorkDuration{};
}

__attribute__((sysv_abi)) void AWorkDuration_release(AWorkDuration *aWorkDuration)
{
    delete aWorkDuration;
}

__attribute__((sysv_abi)) void
AWorkDuration_setWorkPeriodStartTimestampNanos(AWorkDuration *aWorkDuration,
                                               int64_t workPeriodStartTimestampNanos)
{
    if (aWorkDuration) {
        aWorkDuration->work_period_start_timestamp_nanos = workPeriodStartTimestampNanos;
    }
}

__attribute__((sysv_abi)) void
AWorkDuration_setActualTotalDurationNanos(AWorkDuration *aWorkDuration,
                                          int64_t actualTotalDurationNanos)
{
    if (aWorkDuration) {
        aWorkDuration->actual_total_duration_nanos = actualTotalDurationNanos;
    }
}

__attribute__((sysv_abi)) void
AWorkDuration_setActualCpuDurationNanos(AWorkDuration *aWorkDuration,
                                        int64_t actualCpuDurationNanos)
{
    if (aWorkDuration) {
        aWorkDuration->actual_cpu_duration_nanos = actualCpuDurationNanos;
    }
}

__attribute__((sysv_abi)) void
AWorkDuration_setActualGpuDurationNanos(AWorkDuration *aWorkDuration,
                                        int64_t actualGpuDurationNanos)
{
    if (aWorkDuration) {
        aWorkDuration->actual_gpu_duration_nanos = actualGpuDurationNanos;
    }
}

__attribute__((sysv_abi)) APerformanceHintManager *APerformanceHintManager_create(void)
{
    return portage::ndk::PerformanceHintManager::Instance().CreateManager();
}

__attribute__((sysv_abi)) APerformanceHintSession *
APerformanceHintManager_createSession(APerformanceHintManager *manager, const int32_t *threadIds,
                                      size_t size, int64_t initialTargetWorkDurationNanos)
{
    (void)manager;
    return portage::ndk::PerformanceHintManager::Instance().CreateSession(
        threadIds, size, initialTargetWorkDurationNanos);
}

__attribute__((sysv_abi)) int64_t
APerformanceHintManager_getPreferredUpdateRateNanos(APerformanceHintManager *manager)
{
    (void)manager;
    return portage::ndk::PerformanceHintManager::Instance().GetPreferredUpdateRateNanos();
}

__attribute__((sysv_abi)) int
APerformanceHintSession_updateTargetWorkDuration(APerformanceHintSession *session,
                                                 int64_t targetDurationNanos)
{
    if (!session) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->UpdateTargetWorkDuration(targetDurationNanos);
}

__attribute__((sysv_abi)) int
APerformanceHintSession_reportActualWorkDuration(APerformanceHintSession *session,
                                                 int64_t actualDurationNanos)
{
    if (!session) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->ReportActualWorkDuration(actualDurationNanos);
}

__attribute__((sysv_abi)) int
APerformanceHintSession_reportActualWorkDuration2(APerformanceHintSession *session,
                                                  AWorkDuration *workDuration)
{
    if (!session || !workDuration) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->ReportActualWorkDuration2(workDuration);
}

__attribute__((sysv_abi)) int
APerformanceHintSession_sendHint(APerformanceHintSession *session, SessionHint hint)
{
    if (!session) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->SendHint(hint);
}

__attribute__((sysv_abi)) int
APerformanceHintSession_setPreferPowerEfficiency(APerformanceHintSession *session, bool enabled)
{
    if (!session) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->SetPreferPowerEfficiency(enabled);
}

__attribute__((sysv_abi)) void APerformanceHintSession_close(APerformanceHintSession *session)
{
    portage::ndk::PerformanceHintManager::Instance().CloseSession(session);
}

__attribute__((sysv_abi)) int APerformanceHintSession_setThreads(APerformanceHintSession *session,
                                                                 const int32_t *threadIds,
                                                                 size_t size)
{
    if (!session) {
        return -22;
    }
    auto *s = reinterpret_cast<portage::ndk::PerformanceHintSession *>(session);
    return s->SetThreads(threadIds, size);
}

} // extern "C"
