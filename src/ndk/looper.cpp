#include "looper.hpp"
#include "input/input_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#include <chrono>
#include <memory>
#include <thread>

namespace portage::ndk {

namespace {
thread_local LooperImpl *t_thread_looper{nullptr};
}

LooperImpl::LooperImpl(int opts) : opts_(opts) {}

LooperImpl::~LooperImpl()
{
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_.clear();
}

void LooperImpl::Acquire() noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }

void LooperImpl::Release() noexcept
{
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (t_thread_looper == this) {
            t_thread_looper = nullptr;
        }
        delete this;
    }
}

int LooperImpl::AddFd(int fd, int ident, int events, ALooper_callbackFunc callback, void *data)
{
    if (fd < 0)
        return -1;
    if (ident < 0 && callback == nullptr) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[fd] = LooperRegistration{
        .fd = fd, .ident = ident, .events = events, .callback = callback, .data = data};
    return 1;
}

int LooperImpl::RemoveFd(int fd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) {
        return 0;
    }
    registrations_.erase(it);
    return 1;
}

size_t LooperImpl::GetRegistrationCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.size();
}

void LooperImpl::Wake()
{
    wake_signaled_.store(true, std::memory_order_release);
    cv_.notify_all();
}

int LooperImpl::CheckFdEvents(const LooperRegistration &reg) const
{
    int triggered = 0;
    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table)
        return 0;

    auto desc = fd_table->Get(reg.fd);
    if (!desc)
        return 0;

    if (desc->dev == fs::DeviceType::Pipe && desc->handle != INVALID_HANDLE_VALUE) {
        DWORD avail = 0;
        if (PeekNamedPipe(desc->handle, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            if (reg.events & ALOOPER_EVENT_INPUT) {
                triggered |= ALOOPER_EVENT_INPUT;
            }
        }
    } else if (desc->dev == fs::DeviceType::Socket && desc->handle != INVALID_HANDLE_VALUE) {
        WSAPOLLFD pfd{};
        pfd.fd = reinterpret_cast<SOCKET>(desc->handle);
        pfd.events = 0;
        if (reg.events & ALOOPER_EVENT_INPUT)
            pfd.events |= (POLLRDNORM | POLLRDBAND);
        if (reg.events & ALOOPER_EVENT_OUTPUT)
            pfd.events |= POLLWRNORM;
        pfd.revents = 0;

        if (WSAPoll(&pfd, 1, 0) > 0) {
            if ((pfd.revents & (POLLRDNORM | POLLRDBAND)) && (reg.events & ALOOPER_EVENT_INPUT)) {
                triggered |= ALOOPER_EVENT_INPUT;
            }
            if ((pfd.revents & POLLWRNORM) && (reg.events & ALOOPER_EVENT_OUTPUT)) {
                triggered |= ALOOPER_EVENT_OUTPUT;
            }
        }
    } else if (desc->dev == fs::DeviceType::InputEvent0 ||
               desc->dev == fs::DeviceType::InputEvent1) {
        int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
        if (input::InputManager::Instance().HasEvents(dev_idx)) {
            if (reg.events & ALOOPER_EVENT_INPUT) {
                triggered |= ALOOPER_EVENT_INPUT;
            }
        }
    }

    return triggered;
}

int LooperImpl::PollOnce(int timeout_ms, int *out_fd, int *out_events, void **out_data)
{
    if (wake_signaled_.exchange(false, std::memory_order_acq_rel)) {
        return ALOOPER_POLL_WAKE;
    }

    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        LooperRegistration ready_reg{};
        int ready_events = 0;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto &[fd, reg] : registrations_) {
                int ev = CheckFdEvents(reg);
                if (ev != 0) {
                    ready_reg = reg;
                    ready_events = ev;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            if (ready_reg.callback != nullptr) {
                int cb_res = ready_reg.callback(ready_reg.fd, ready_events, ready_reg.data);
                if (cb_res == 0) {
                    RemoveFd(ready_reg.fd);
                }
                return ALOOPER_POLL_CALLBACK;
            }

            if (out_fd)
                *out_fd = ready_reg.fd;
            if (out_events)
                *out_events = ready_events;
            if (out_data)
                *out_data = ready_reg.data;
            return ready_reg.ident;
        }

        if (wake_signaled_.exchange(false, std::memory_order_acq_rel)) {
            return ALOOPER_POLL_WAKE;
        }

        if (timeout_ms == 0) {
            return ALOOPER_POLL_TIMEOUT;
        }

        if (timeout_ms > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();
            if (elapsed >= timeout_ms) {
                return ALOOPER_POLL_TIMEOUT;
            }
        }

        std::unique_lock<std::mutex> lock(mutex_);
        auto slice = (timeout_ms < 0) ? std::chrono::milliseconds(10)
                                      : std::chrono::milliseconds(std::min(timeout_ms, 10));
        cv_.wait_for(lock, slice,
                     [this]() { return wake_signaled_.load(std::memory_order_acquire); });

        if (wake_signaled_.exchange(false, std::memory_order_acq_rel)) {
            return ALOOPER_POLL_WAKE;
        }
    }
}

int LooperImpl::PollAll(int timeout_ms, int *out_fd, int *out_events, void **out_data)
{
    int res = PollOnce(timeout_ms, out_fd, out_events, out_data);
    while (res == ALOOPER_POLL_CALLBACK) {
        res = PollOnce(0, out_fd, out_events, out_data);
    }
    return res;
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) ALooper *ALooper_forThread()
{
    return reinterpret_cast<ALooper *>(portage::ndk::t_thread_looper);
}

__attribute__((sysv_abi)) ALooper *ALooper_prepare(int opts)
{
    if (!portage::ndk::t_thread_looper) {
        portage::ndk::t_thread_looper = new portage::ndk::LooperImpl(opts);
    }
    return reinterpret_cast<ALooper *>(portage::ndk::t_thread_looper);
}

__attribute__((sysv_abi)) void ALooper_acquire(ALooper *looper)
{
    if (looper) {
        reinterpret_cast<portage::ndk::LooperImpl *>(looper)->Acquire();
    }
}

__attribute__((sysv_abi)) void ALooper_release(ALooper *looper)
{
    if (looper) {
        reinterpret_cast<portage::ndk::LooperImpl *>(looper)->Release();
    }
}

__attribute__((sysv_abi)) int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents,
                                               void **outData)
{
    if (!portage::ndk::t_thread_looper) {
        return ALOOPER_POLL_ERROR;
    }
    return portage::ndk::t_thread_looper->PollOnce(timeoutMillis, outFd, outEvents, outData);
}

__attribute__((sysv_abi)) int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents,
                                              void **outData)
{
    if (!portage::ndk::t_thread_looper) {
        return ALOOPER_POLL_ERROR;
    }
    return portage::ndk::t_thread_looper->PollAll(timeoutMillis, outFd, outEvents, outData);
}

__attribute__((sysv_abi)) void ALooper_wake(ALooper *looper)
{
    if (looper) {
        reinterpret_cast<portage::ndk::LooperImpl *>(looper)->Wake();
    }
}

__attribute__((sysv_abi)) int ALooper_addFd(ALooper *looper, int fd, int ident, int events,
                                            ALooper_callbackFunc callback, void *data)
{
    if (!looper)
        return -1;
    return reinterpret_cast<portage::ndk::LooperImpl *>(looper)->AddFd(fd, ident, events, callback,
                                                                       data);
}

__attribute__((sysv_abi)) int ALooper_removeFd(ALooper *looper, int fd)
{
    if (!looper)
        return -1;
    return reinterpret_cast<portage::ndk::LooperImpl *>(looper)->RemoveFd(fd);
}

} // extern "C"
