#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace portage::ndk {

struct TraceEvent {
    enum class Type {
        Begin,
        End,
        AsyncBegin,
        AsyncEnd,
        Counter,
    };

    Type type{Type::Begin};
    std::string name;
    int64_t value{0};
    uint64_t timestamp_ns{0};
};

class TraceManager {
  public:
    static TraceManager &Instance() noexcept;

    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

    void BeginSection(std::string_view section_name);
    void EndSection();
    void BeginAsyncSection(std::string_view section_name, int32_t cookie);
    void EndAsyncSection(std::string_view section_name, int32_t cookie);
    void SetCounter(std::string_view counter_name, int64_t counter_value);

    [[nodiscard]] std::vector<TraceEvent> GetEvents() const;
    void Clear() noexcept;

  private:
    TraceManager() = default;

    static uint64_t QueryTimestampNs() noexcept;

    std::atomic<bool> enabled_{true};
    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
};

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) bool ATrace_isEnabled();
__attribute__((sysv_abi)) void ATrace_beginSection(const char *sectionName);
__attribute__((sysv_abi)) void ATrace_endSection();
__attribute__((sysv_abi)) void ATrace_beginAsyncSection(const char *sectionName, int32_t cookie);
__attribute__((sysv_abi)) void ATrace_endAsyncSection(const char *sectionName, int32_t cookie);
__attribute__((sysv_abi)) void ATrace_setCounter(const char *counterName, int64_t counterValue);

} // extern "C"
