#pragma once

#include <cstdarg>
#include <cstdint>
#include <deque>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT = 1,
    ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
    ANDROID_LOG_FATAL = 7,
    ANDROID_LOG_SILENT = 8,
};

namespace portage::ndk {

struct LogcatEntry {
    uint64_t timestamp_ms{0};
    int priority{ANDROID_LOG_INFO};
    uint32_t pid{0};
    uint32_t tid{0};
    std::string tag{};
    std::string message{};
};

class LogcatBuffer {
  public:
    static LogcatBuffer &Instance() noexcept;

    void Append(int priority, std::string_view tag, std::string_view message);
    [[nodiscard]] std::vector<LogcatEntry> GetEntries() const;
    void Clear();
    void DumpToStream(std::ostream &os) const;

    void SetConsoleOutputEnabled(bool enabled) noexcept { console_output_ = enabled; }
    [[nodiscard]] bool IsConsoleOutputEnabled() const noexcept { return console_output_; }

  private:
    LogcatBuffer() = default;
    static constexpr size_t kMaxEntries = 2048;

    mutable std::mutex mutex_{};
    std::deque<LogcatEntry> entries_{};
    bool console_output_{true};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) int __android_log_write(int prio, const char *tag, const char *text);
__attribute__((sysv_abi)) int __android_log_print(int prio, const char *tag, const char *fmt,
                                                  uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0,
                                                  uint64_t a4 = 0, uint64_t a5 = 0,
                                                  uint64_t a6 = 0);
__attribute__((sysv_abi)) int __android_log_vprint(int prio, const char *tag, const char *fmt,
                                                   const void *ap);
__attribute__((sysv_abi)) void __android_log_assert(const char *cond, const char *tag,
                                                    const char *fmt, uint64_t a1 = 0,
                                                    uint64_t a2 = 0, uint64_t a3 = 0);
}
