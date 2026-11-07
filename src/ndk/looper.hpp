#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

struct ALooper;

enum {
    ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1 << 0,
};

enum {
    ALOOPER_POLL_WAKE = -1,
    ALOOPER_POLL_CALLBACK = -2,
    ALOOPER_POLL_TIMEOUT = -3,
    ALOOPER_POLL_ERROR = -4,
};

enum {
    ALOOPER_EVENT_INPUT = 1 << 0,
    ALOOPER_EVENT_OUTPUT = 1 << 1,
    ALOOPER_EVENT_ERROR = 1 << 2,
    ALOOPER_EVENT_HANGUP = 1 << 3,
    ALOOPER_EVENT_INVALID = 1 << 4,
};

using ALooper_callbackFunc = int(__attribute__((sysv_abi)) *)(int fd, int events, void *data);

namespace portage::ndk {

struct LooperRegistration {
    int fd{0};
    int ident{0};
    int events{0};
    ALooper_callbackFunc callback{nullptr};
    void *data{nullptr};
};

class LooperImpl {
  public:
    explicit LooperImpl(int opts = 0);
    ~LooperImpl();

    void Acquire() noexcept;
    void Release() noexcept;

    int AddFd(int fd, int ident, int events, ALooper_callbackFunc callback, void *data);
    int RemoveFd(int fd);

    int PollOnce(int timeout_ms, int *out_fd, int *out_events, void **out_data);
    int PollAll(int timeout_ms, int *out_fd, int *out_events, void **out_data);

    void Wake();

    [[nodiscard]] int GetOpts() const noexcept { return opts_; }
    [[nodiscard]] size_t GetRegistrationCount() const noexcept;

  private:
    int CheckFdEvents(const LooperRegistration &reg) const;

    int opts_{0};
    std::atomic<int32_t> ref_count_{1};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::unordered_map<int, LooperRegistration> registrations_{};
    std::atomic<bool> wake_signaled_{false};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) ALooper *ALooper_forThread();
__attribute__((sysv_abi)) ALooper *ALooper_prepare(int opts);
__attribute__((sysv_abi)) void ALooper_acquire(ALooper *looper);
__attribute__((sysv_abi)) void ALooper_release(ALooper *looper);
__attribute__((sysv_abi)) int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents,
                                               void **outData);
__attribute__((sysv_abi)) int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents,
                                              void **outData);
__attribute__((sysv_abi)) void ALooper_wake(ALooper *looper);
__attribute__((sysv_abi)) int ALooper_addFd(ALooper *looper, int fd, int ident, int events,
                                            ALooper_callbackFunc callback, void *data);
__attribute__((sysv_abi)) int ALooper_removeFd(ALooper *looper, int fd);
}
