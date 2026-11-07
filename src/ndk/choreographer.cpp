#include "ndk/choreographer.hpp"
#include "wsi/window_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>

namespace portage::ndk {

namespace {
thread_local std::shared_ptr<ChoreographerImpl> t_thread_choreographer{};
} // namespace

VsyncPacer &VsyncPacer::Instance() noexcept
{
    static VsyncPacer instance;
    return instance;
}

VsyncPacer::VsyncPacer() : origin_nanos_(NowNanos())
{
    vsync_period_nanos_.store(QueryAdaptivePeriodNanos(), std::memory_order_relaxed);
}

VsyncPacer::~VsyncPacer() { Shutdown(); }

int64_t VsyncPacer::NowNanos() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t VsyncPacer::GetVsyncPeriodNanos() const noexcept
{
    int64_t period = QueryAdaptivePeriodNanos();
    return period > 0 ? period : vsync_period_nanos_.load(std::memory_order_acquire);
}

void VsyncPacer::SetRefreshRateHz(double hz)
{
    if (hz < 1.0 || hz > 1000.0) {
        return;
    }

    auto new_period = static_cast<int64_t>(1'000'000'000.0 / hz);
    override_period_nanos_.store(new_period, std::memory_order_release);
    int64_t prev_period = vsync_period_nanos_.exchange(new_period, std::memory_order_acq_rel);

    std::vector<std::shared_ptr<ChoreographerImpl>> active;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = choreographers_.begin(); it != choreographers_.end();) {
            if (auto locked = it->lock()) {
                active.push_back(std::move(locked));
                ++it;
            } else {
                it = choreographers_.erase(it);
            }
        }
    }

    if (new_period != prev_period) {
        for (const auto &ch : active) {
            ch->OnRefreshPeriodChanged(new_period);
        }
    }

    cv_.notify_all();
}

void VsyncPacer::ClearRefreshRateOverride()
{
    override_period_nanos_.store(0, std::memory_order_release);
    int64_t adaptive = QueryAdaptivePeriodNanos();
    int64_t prev_period = vsync_period_nanos_.exchange(adaptive, std::memory_order_acq_rel);

    if (adaptive != prev_period) {
        std::vector<std::shared_ptr<ChoreographerImpl>> active;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = choreographers_.begin(); it != choreographers_.end();) {
                if (auto locked = it->lock()) {
                    active.push_back(std::move(locked));
                    ++it;
                } else {
                    it = choreographers_.erase(it);
                }
            }
        }
        for (const auto &ch : active) {
            ch->OnRefreshPeriodChanged(adaptive);
        }
    }

    cv_.notify_all();
}

void VsyncPacer::RegisterChoreographer(const std::shared_ptr<ChoreographerImpl> &choreographer)
{
    if (!choreographer) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = choreographers_.begin(); it != choreographers_.end();) {
        if (auto existing = it->lock()) {
            if (existing.get() == choreographer.get()) {
                return;
            }
            ++it;
        } else {
            it = choreographers_.erase(it);
        }
    }

    choreographers_.push_back(choreographer);
}

void VsyncPacer::NotifyScheduled()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_) {
            stop_requested_ = false;
        }
        EnsureThreadStartedLocked();
    }
    cv_.notify_all();
}

void VsyncPacer::Shutdown() noexcept
{
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        thread_started_ = false;
        if (pacer_thread_.joinable()) {
            to_join = std::move(pacer_thread_);
        }
    }
    cv_.notify_all();

    if (to_join.joinable()) {
        to_join.join();
    }
}

void VsyncPacer::EnsureThreadStartedLocked()
{
    if (!thread_started_) {
        if (pacer_thread_.joinable()) {
            pacer_thread_.join();
        }
        thread_started_ = true;
        pacer_thread_ = std::thread(&VsyncPacer::PacerThreadFunc, this);
    }
}

int64_t VsyncPacer::QueryAdaptivePeriodNanos() const noexcept
{
    int64_t override_ns = override_period_nanos_.load(std::memory_order_acquire);
    if (override_ns > 0) {
        return override_ns;
    }

    auto &wm = wsi::WindowManager::Instance();
    if (!wm.IsHeadless() && wm.GetWindowHandle() != nullptr) {
        DWM_TIMING_INFO timing{};
        timing.cbSize = sizeof(DWM_TIMING_INFO);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) &&
            timing.rateRefresh.uiNumerator > 0 && timing.rateRefresh.uiDenominator > 0) {
            double hz = static_cast<double>(timing.rateRefresh.uiNumerator) /
                        static_cast<double>(timing.rateRefresh.uiDenominator);
            if (hz >= 24.0 && hz <= 500.0) {
                return static_cast<int64_t>(1'000'000'000.0 / hz);
            }
        }

        DEVMODEW dm{};
        dm.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) != FALSE &&
            dm.dmDisplayFrequency >= 24 && dm.dmDisplayFrequency <= 500) {
            return 1'000'000'000LL / static_cast<int64_t>(dm.dmDisplayFrequency);
        }
    }

    return kDefaultRefreshPeriodNanos;
}

bool VsyncPacer::HasActiveWorkLocked()
{
    bool any_pending = false;
    for (auto it = choreographers_.begin(); it != choreographers_.end();) {
        if (auto ch = it->lock()) {
            if (ch->HasPendingFrameCallbacks()) {
                any_pending = true;
            }
            ++it;
        } else {
            it = choreographers_.erase(it);
        }
    }
    return any_pending;
}

void VsyncPacer::PacerThreadFunc()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_requested_) {
        cv_.wait(lock, [this]() { return stop_requested_ || HasActiveWorkLocked(); });
        if (stop_requested_) {
            break;
        }

        int64_t period_ns = QueryAdaptivePeriodNanos();
        int64_t prev_period_ns = vsync_period_nanos_.exchange(period_ns, std::memory_order_acq_rel);

        int64_t now_ns = NowNanos();
        int64_t elapsed_ns = std::max<int64_t>(0, now_ns - origin_nanos_);
        int64_t next_tick_ns = origin_nanos_ + ((elapsed_ns / period_ns) + 1) * period_ns;
        if (next_tick_ns <= last_tick_nanos_) {
            next_tick_ns = last_tick_nanos_ + period_ns;
        }

        while (!stop_requested_) {
            now_ns = NowNanos();
            if (now_ns >= next_tick_ns) {
                break;
            }
            int64_t remaining_ns = next_tick_ns - now_ns;
            cv_.wait_for(lock, std::chrono::nanoseconds(remaining_ns),
                         [this]() { return stop_requested_; });

            int64_t updated_period = QueryAdaptivePeriodNanos();
            if (updated_period != period_ns) {
                period_ns = updated_period;
                vsync_period_nanos_.store(period_ns, std::memory_order_release);
                elapsed_ns = std::max<int64_t>(0, NowNanos() - origin_nanos_);
                next_tick_ns = origin_nanos_ + ((elapsed_ns / period_ns) + 1) * period_ns;
                if (next_tick_ns <= last_tick_nanos_) {
                    next_tick_ns = last_tick_nanos_ + period_ns;
                }
            }
        }

        if (stop_requested_) {
            break;
        }

        int64_t tick_ns = std::max(NowNanos(), next_tick_ns);
        if (tick_ns <= last_tick_nanos_) {
            tick_ns = last_tick_nanos_ + 1;
        }
        last_tick_nanos_ = tick_ns;

        std::vector<std::shared_ptr<ChoreographerImpl>> active;
        active.reserve(choreographers_.size());
        for (auto it = choreographers_.begin(); it != choreographers_.end();) {
            if (auto ch = it->lock()) {
                active.push_back(std::move(ch));
                ++it;
            } else {
                it = choreographers_.erase(it);
            }
        }

        lock.unlock();
        for (const auto &ch : active) {
            if (period_ns != prev_period_ns) {
                ch->OnRefreshPeriodChanged(period_ns);
            }
            ch->OnVsyncTick(tick_ns, period_ns);
        }
        lock.lock();
    }
}

ChoreographerImpl::ChoreographerImpl()
    : last_known_period_nanos_(VsyncPacer::Instance().GetVsyncPeriodNanos())
{
    HANDLE read_h = nullptr;
    HANDLE write_h = nullptr;
    if (CreatePipe(&read_h, &write_h, nullptr, 0) != FALSE) {
        read_pipe_ = read_h;
        write_pipe_ = write_h;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    EnsureLooperAttachedLocked();
}

ChoreographerImpl::~ChoreographerImpl()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DetachLooperLocked();
    if (read_pipe_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(read_pipe_));
        read_pipe_ = nullptr;
    }
    if (write_pipe_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(write_pipe_));
        write_pipe_ = nullptr;
    }
}

ChoreographerImpl *ChoreographerImpl::GetInstance()
{
    if (!t_thread_choreographer) {
        t_thread_choreographer = std::make_shared<ChoreographerImpl>();
        VsyncPacer::Instance().RegisterChoreographer(t_thread_choreographer);
    } else {
        std::lock_guard<std::mutex> lock(t_thread_choreographer->mutex_);
        t_thread_choreographer->EnsureLooperAttachedLocked();
        VsyncPacer::Instance().RegisterChoreographer(t_thread_choreographer);
    }
    return t_thread_choreographer.get();
}

void ChoreographerImpl::EnsureLooperAttachedLocked()
{
    ALooper *looper = ALooper_forThread();
    if (!looper) {
        looper = ALooper_prepare(0);
    }

    auto *looper_impl = reinterpret_cast<LooperImpl *>(looper);
    if (looper_impl == attached_looper_) {
        return;
    }

    DetachLooperLocked();
    if (looper_impl && read_pipe_ != nullptr) {
        looper_impl->Acquire();
        internal_pipe_fd_ =
            looper_impl->AddInternalPipe(read_pipe_, &ChoreographerImpl::OnLooperSignaled, this);
        attached_looper_ = looper_impl;
    }
}

void ChoreographerImpl::DetachLooperLocked() noexcept
{
    if (attached_looper_) {
        if (internal_pipe_fd_ >= 0) {
            attached_looper_->RemoveFd(internal_pipe_fd_);
            internal_pipe_fd_ = -1;
        }
        attached_looper_->Release();
        attached_looper_ = nullptr;
    }
}

void ChoreographerImpl::SignalPipeLocked() noexcept
{
    if (!pipe_signaled_ && write_pipe_ != nullptr) {
        uint8_t byte = 1;
        DWORD written = 0;
        if (WriteFile(static_cast<HANDLE>(write_pipe_), &byte, 1, &written, nullptr) != FALSE &&
            written > 0) {
            pipe_signaled_ = true;
        }
    }
    if (attached_looper_) {
        attached_looper_->NotifyActivity();
    }
}

void ChoreographerImpl::DrainPipeLocked() noexcept
{
    if (read_pipe_ != nullptr) {
        HANDLE h = static_cast<HANDLE>(read_pipe_);
        DWORD avail = 0;
        uint8_t buf[64];
        while (PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) != FALSE && avail > 0) {
            DWORD bytes_read = 0;
            if (ReadFile(h, buf, std::min<DWORD>(avail, sizeof(buf)), &bytes_read, nullptr) ==
                    FALSE ||
                bytes_read == 0) {
                break;
            }
        }
    }
    pipe_signaled_ = false;
}

void ChoreographerImpl::PostFrameCallback(AChoreographer_frameCallback cb, void *data)
{
    PostFrameCallbackDelayed(cb, data, 0);
}

void ChoreographerImpl::PostFrameCallbackDelayed(AChoreographer_frameCallback cb, void *data,
                                                 int64_t delay_ms)
{
    if (!cb) {
        return;
    }

    int64_t clamped_delay = std::max<int64_t>(0, delay_ms);
    int64_t due_ns = VsyncPacer::NowNanos() + clamped_delay * 1'000'000LL;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureLooperAttachedLocked();
        frame_callbacks_.push_back(PendingFrameCallback{.is_64bit = false,
                                                        .cb32 = cb,
                                                        .cb64 = nullptr,
                                                        .data = data,
                                                        .due_time_nanos = due_ns});
    }

    VsyncPacer::Instance().NotifyScheduled();
}

void ChoreographerImpl::PostFrameCallback64(AChoreographer_frameCallback64 cb, void *data)
{
    PostFrameCallbackDelayed64(cb, data, 0);
}

void ChoreographerImpl::PostFrameCallbackDelayed64(AChoreographer_frameCallback64 cb, void *data,
                                                   uint32_t delay_ms)
{
    if (!cb) {
        return;
    }

    int64_t due_ns = VsyncPacer::NowNanos() + static_cast<int64_t>(delay_ms) * 1'000'000LL;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        EnsureLooperAttachedLocked();
        frame_callbacks_.push_back(PendingFrameCallback{
            .is_64bit = true, .cb32 = nullptr, .cb64 = cb, .data = data, .due_time_nanos = due_ns});
    }

    VsyncPacer::Instance().NotifyScheduled();
}

void ChoreographerImpl::RegisterRefreshRateCallback(AChoreographer_refreshRateCallback cb,
                                                    void *data)
{
    if (!cb) {
        return;
    }

    int64_t current_period = VsyncPacer::Instance().GetVsyncPeriodNanos();

    std::lock_guard<std::mutex> lock(mutex_);
    EnsureLooperAttachedLocked();
    last_known_period_nanos_ = current_period;

    for (const auto &existing : refresh_listeners_) {
        if (existing.callback == cb && existing.data == data) {
            return;
        }
    }

    refresh_listeners_.push_back(RefreshRateListener{
        .callback = cb, .data = data, .pending = true, .period_nanos = current_period});
    SignalPipeLocked();
}

void ChoreographerImpl::UnregisterRefreshRateCallback(AChoreographer_refreshRateCallback cb,
                                                      void *data)
{
    if (!cb) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(refresh_listeners_, [&](const RefreshRateListener &listener) {
        return listener.callback == cb && listener.data == data;
    });
}

void ChoreographerImpl::OnVsyncTick(int64_t frame_time_nanos, int64_t vsync_period_nanos)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool should_signal = false;

    if (vsync_period_nanos != last_known_period_nanos_) {
        last_known_period_nanos_ = vsync_period_nanos;
        for (auto &listener : refresh_listeners_) {
            listener.pending = true;
            listener.period_nanos = vsync_period_nanos;
            should_signal = true;
        }
    }

    for (const auto &entry : frame_callbacks_) {
        if (entry.due_time_nanos <= frame_time_nanos) {
            latest_vsync_nanos_ = frame_time_nanos;
            vsync_pending_ = true;
            should_signal = true;
            break;
        }
    }

    if (should_signal) {
        SignalPipeLocked();
    }
}

void ChoreographerImpl::OnRefreshPeriodChanged(int64_t vsync_period_nanos)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (vsync_period_nanos == last_known_period_nanos_) {
        return;
    }

    last_known_period_nanos_ = vsync_period_nanos;
    if (refresh_listeners_.empty()) {
        return;
    }

    for (auto &listener : refresh_listeners_) {
        listener.pending = true;
        listener.period_nanos = vsync_period_nanos;
    }
    SignalPipeLocked();
}

bool ChoreographerImpl::HasPendingFrameCallbacks() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !frame_callbacks_.empty();
}

ALooper *ChoreographerImpl::GetLooper() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return reinterpret_cast<ALooper *>(attached_looper_);
}

int ChoreographerImpl::DispatchCallbacks(int64_t frame_time_nanos)
{
    struct ReadyRefresh {
        AChoreographer_refreshRateCallback cb;
        void *data;
        int64_t period_nanos;
    };

    std::vector<ReadyRefresh> ready_refresh;
    std::vector<PendingFrameCallback> ready_frames;
    int64_t dispatch_frame_nanos = frame_time_nanos;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        DrainPipeLocked();

        for (auto &listener : refresh_listeners_) {
            if (listener.pending) {
                ready_refresh.push_back(
                    ReadyRefresh{listener.callback, listener.data, listener.period_nanos});
                listener.pending = false;
            }
        }

        if (frame_time_nanos > 0 || vsync_pending_) {
            vsync_pending_ = false;
            int64_t cutoff_nanos = (frame_time_nanos > 0) ? frame_time_nanos : latest_vsync_nanos_;
            if (cutoff_nanos <= last_dispatched_frame_nanos_) {
                cutoff_nanos = last_dispatched_frame_nanos_ + 1;
            }
            dispatch_frame_nanos = cutoff_nanos;
            last_dispatched_frame_nanos_ = dispatch_frame_nanos;

            std::vector<PendingFrameCallback> remaining;
            for (auto &entry : frame_callbacks_) {
                if (entry.due_time_nanos <= cutoff_nanos) {
                    ready_frames.push_back(entry);
                } else {
                    remaining.push_back(entry);
                }
            }
            frame_callbacks_ = std::move(remaining);
        }
    }

    int invoked = 0;
    for (const auto &r : ready_refresh) {
        if (r.cb) {
            r.cb(r.period_nanos, r.data);
            ++invoked;
        }
    }

    for (const auto &f : ready_frames) {
        if (f.is_64bit && f.cb64) {
            f.cb64(dispatch_frame_nanos, f.data);
            ++invoked;
        } else if (!f.is_64bit && f.cb32) {
            f.cb32(dispatch_frame_nanos, f.data);
            ++invoked;
        }
    }

    return invoked;
}

int __attribute__((sysv_abi)) ChoreographerImpl::OnLooperSignaled(int fd, int events, void *data)
{
    (void)fd;
    (void)events;
    if (data) {
        static_cast<ChoreographerImpl *>(data)->DispatchCallbacks(0);
    }
    return 1;
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) AChoreographer *AChoreographer_getInstance()
{
    return reinterpret_cast<AChoreographer *>(portage::ndk::ChoreographerImpl::GetInstance());
}

__attribute__((sysv_abi)) void
AChoreographer_postFrameCallback(AChoreographer *choreographer,
                                 AChoreographer_frameCallback callback, void *data)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->PostFrameCallback(callback, data);
    }
}

__attribute__((sysv_abi)) void
AChoreographer_postFrameCallbackDelayed(AChoreographer *choreographer,
                                        AChoreographer_frameCallback callback, void *data,
                                        int64_t delayMillis)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->PostFrameCallbackDelayed(callback, data, delayMillis);
    }
}

__attribute__((sysv_abi)) void
AChoreographer_postFrameCallback64(AChoreographer *choreographer,
                                   AChoreographer_frameCallback64 callback, void *data)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->PostFrameCallback64(callback, data);
    }
}

__attribute__((sysv_abi)) void
AChoreographer_postFrameCallbackDelayed64(AChoreographer *choreographer,
                                          AChoreographer_frameCallback64 callback, void *data,
                                          uint32_t delayMillis)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->PostFrameCallbackDelayed64(callback, data, delayMillis);
    }
}

__attribute__((sysv_abi)) void
AChoreographer_registerRefreshRateCallback(AChoreographer *choreographer,
                                           AChoreographer_refreshRateCallback callback, void *data)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->RegisterRefreshRateCallback(callback, data);
    }
}

__attribute__((sysv_abi)) void AChoreographer_unregisterRefreshRateCallback(
    AChoreographer *choreographer, AChoreographer_refreshRateCallback callback, void *data)
{
    if (choreographer) {
        reinterpret_cast<portage::ndk::ChoreographerImpl *>(choreographer)
            ->UnregisterRefreshRateCallback(callback, data);
    }
}

} // extern "C"
