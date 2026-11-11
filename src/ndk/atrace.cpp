#include "ndk/atrace.hpp"
#include <chrono>

namespace portage::ndk {

namespace {
constexpr size_t kMaxRecordedTraceEvents = 4096;
} // namespace

TraceManager &TraceManager::Instance() noexcept
{
    static TraceManager instance;
    return instance;
}

uint64_t TraceManager::QueryTimestampNs() noexcept
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void TraceManager::SetEnabled(bool enabled) noexcept
{
    enabled_.store(enabled, std::memory_order_relaxed);
}

bool TraceManager::IsEnabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

void TraceManager::BeginSection(std::string_view section_name)
{
    if (!IsEnabled()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (events_.size() >= kMaxRecordedTraceEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(TraceEvent{
        .type = TraceEvent::Type::Begin,
        .name = std::string(section_name),
        .value = 0,
        .timestamp_ns = QueryTimestampNs(),
    });
}

void TraceManager::EndSection()
{
    if (!IsEnabled()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (events_.size() >= kMaxRecordedTraceEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(TraceEvent{
        .type = TraceEvent::Type::End,
        .name = {},
        .value = 0,
        .timestamp_ns = QueryTimestampNs(),
    });
}

void TraceManager::BeginAsyncSection(std::string_view section_name, int32_t cookie)
{
    if (!IsEnabled()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (events_.size() >= kMaxRecordedTraceEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(TraceEvent{
        .type = TraceEvent::Type::AsyncBegin,
        .name = std::string(section_name),
        .value = cookie,
        .timestamp_ns = QueryTimestampNs(),
    });
}

void TraceManager::EndAsyncSection(std::string_view section_name, int32_t cookie)
{
    if (!IsEnabled()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (events_.size() >= kMaxRecordedTraceEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(TraceEvent{
        .type = TraceEvent::Type::AsyncEnd,
        .name = std::string(section_name),
        .value = cookie,
        .timestamp_ns = QueryTimestampNs(),
    });
}

void TraceManager::SetCounter(std::string_view counter_name, int64_t counter_value)
{
    if (!IsEnabled()) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (events_.size() >= kMaxRecordedTraceEvents) {
        events_.erase(events_.begin());
    }
    events_.push_back(TraceEvent{
        .type = TraceEvent::Type::Counter,
        .name = std::string(counter_name),
        .value = counter_value,
        .timestamp_ns = QueryTimestampNs(),
    });
}

std::vector<TraceEvent> TraceManager::GetEvents() const
{
    std::lock_guard lock(mutex_);
    return events_;
}

void TraceManager::Clear() noexcept
{
    std::lock_guard lock(mutex_);
    events_.clear();
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) bool ATrace_isEnabled()
{
    return portage::ndk::TraceManager::Instance().IsEnabled();
}

__attribute__((sysv_abi)) void ATrace_beginSection(const char *sectionName)
{
    portage::ndk::TraceManager::Instance().BeginSection(sectionName != nullptr ? sectionName : "");
}

__attribute__((sysv_abi)) void ATrace_endSection()
{
    portage::ndk::TraceManager::Instance().EndSection();
}

__attribute__((sysv_abi)) void ATrace_beginAsyncSection(const char *sectionName, int32_t cookie)
{
    portage::ndk::TraceManager::Instance().BeginAsyncSection(
        sectionName != nullptr ? sectionName : "", cookie);
}

__attribute__((sysv_abi)) void ATrace_endAsyncSection(const char *sectionName, int32_t cookie)
{
    portage::ndk::TraceManager::Instance().EndAsyncSection(
        sectionName != nullptr ? sectionName : "", cookie);
}

__attribute__((sysv_abi)) void ATrace_setCounter(const char *counterName, int64_t counterValue)
{
    portage::ndk::TraceManager::Instance().SetCounter(counterName != nullptr ? counterName : "",
                                                      counterValue);
}

} // extern "C"
