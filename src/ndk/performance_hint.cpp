#include "ndk/performance_hint.hpp"

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
    return 0;
}

int PerformanceHintSession::SetThreads(const int32_t *thread_ids, size_t size) noexcept
{
    if (!thread_ids || size == 0) {
        return -22; // -EINVAL
    }

    std::lock_guard<std::mutex> lock(mutex_);
    thread_ids_.assign(thread_ids, thread_ids + size);
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
    auto session =
        std::make_shared<PerformanceHintSession>(thread_ids, size, initial_target_nanos);

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
    auto it = std::remove_if(active_sessions_.begin(), active_sessions_.end(),
                             [&](const std::shared_ptr<PerformanceHintSession> &s) {
                                 return reinterpret_cast<APerformanceHintSession *>(s.get()) ==
                                        session;
                             });
    if (it != active_sessions_.end()) {
        active_sessions_.erase(it, active_sessions_.end());
    }
}

int64_t PerformanceHintManager::GetPreferredUpdateRateNanos() const noexcept
{
    return 16'666'666;
}

size_t PerformanceHintManager::GetActiveSessionCount() const noexcept
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
    return active_sessions_.size();
}

} // namespace portage::ndk

extern "C" {

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
