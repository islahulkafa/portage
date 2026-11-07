#include "android_log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <iostream>

namespace portage::ndk {

LogcatBuffer &LogcatBuffer::Instance() noexcept
{
    static LogcatBuffer instance;
    return instance;
}

void LogcatBuffer::Append(int priority, std::string_view tag, std::string_view message)
{
    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());

    uint32_t pid = GetCurrentProcessId();
    uint32_t tid = GetCurrentThreadId();

    LogcatEntry entry{.timestamp_ms = now_ms,
                      .priority = priority,
                      .pid = pid,
                      .tid = tid,
                      .tag = std::string(tag),
                      .message = std::string(message)};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.size() >= kMaxEntries) {
            entries_.pop_front();
        }
        entries_.push_back(entry);
    }

    if (console_output_) {
        const char *prio_char = "V";
        switch (priority) {
        case ANDROID_LOG_VERBOSE:
            prio_char = "V";
            break;
        case ANDROID_LOG_DEBUG:
            prio_char = "D";
            break;
        case ANDROID_LOG_INFO:
            prio_char = "I";
            break;
        case ANDROID_LOG_WARN:
            prio_char = "W";
            break;
        case ANDROID_LOG_ERROR:
            prio_char = "E";
            break;
        case ANDROID_LOG_FATAL:
            prio_char = "F";
            break;
        default:
            prio_char = "I";
            break;
        }

        if (priority >= ANDROID_LOG_WARN) {
            std::cerr << "[" << prio_char << "/" << tag << "] " << message << "\n";
        } else {
            std::cout << "[" << prio_char << "/" << tag << "] " << message << "\n";
        }
    }
}

std::vector<LogcatEntry> LogcatBuffer::GetEntries() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<LogcatEntry>(entries_.begin(), entries_.end());
}

void LogcatBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

void LogcatBuffer::DumpToStream(std::ostream &os) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &entry : entries_) {
        const char *prio_char = "I";
        switch (entry.priority) {
        case ANDROID_LOG_VERBOSE:
            prio_char = "V";
            break;
        case ANDROID_LOG_DEBUG:
            prio_char = "D";
            break;
        case ANDROID_LOG_INFO:
            prio_char = "I";
            break;
        case ANDROID_LOG_WARN:
            prio_char = "W";
            break;
        case ANDROID_LOG_ERROR:
            prio_char = "E";
            break;
        case ANDROID_LOG_FATAL:
            prio_char = "F";
            break;
        default:
            prio_char = "I";
            break;
        }

        os << entry.pid << " " << entry.tid << " " << prio_char << " " << entry.tag << ": "
           << entry.message << "\n";
    }
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int __android_log_write(int prio, const char *tag, const char *text)
{
    const char *safe_tag = tag ? tag : "portage";
    const char *safe_text = text ? text : "";
    portage::ndk::LogcatBuffer::Instance().Append(prio, safe_tag, safe_text);
    return 1;
}

__attribute__((sysv_abi)) int __android_log_vprint(int prio, const char *tag, const char *fmt,
                                                   const void *ap)
{
    (void)ap;
    if (!fmt)
        return 0;
    const char *safe_tag = tag ? tag : "portage";
    portage::ndk::LogcatBuffer::Instance().Append(prio, safe_tag, fmt);
    return static_cast<int>(strlen(fmt));
}

__attribute__((sysv_abi)) int __android_log_print(int prio, const char *tag, const char *fmt,
                                                  uint64_t a1, uint64_t a2, uint64_t a3,
                                                  uint64_t a4, uint64_t a5, uint64_t a6)
{
    if (!fmt)
        return 0;
    char buffer[1024];
    int len = 0;
    if (strchr(fmt, '%')) {
        len = snprintf(buffer, sizeof(buffer), fmt, a1, a2, a3, a4, a5, a6);
    } else {
        len = snprintf(buffer, sizeof(buffer), "%s", fmt);
    }

    const char *safe_tag = tag ? tag : "portage";
    portage::ndk::LogcatBuffer::Instance().Append(prio, safe_tag, buffer);
    return len;
}

__attribute__((sysv_abi)) void __android_log_assert(const char *cond, const char *tag,
                                                    const char *fmt, uint64_t a1, uint64_t a2,
                                                    uint64_t a3)
{
    char buffer[1024];
    if (fmt) {
        snprintf(buffer, sizeof(buffer), fmt, a1, a2, a3);
    } else {
        snprintf(buffer, sizeof(buffer), "Assertion failed: %s", cond ? cond : "unknown");
    }

    const char *safe_tag = tag ? tag : "assert";
    portage::ndk::LogcatBuffer::Instance().Append(ANDROID_LOG_FATAL, safe_tag, buffer);
}

} // extern "C"
