#include "input_queue.hpp"
#include "looper.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>

namespace portage::ndk {

InputQueueImpl::InputQueueImpl() = default;

InputQueueImpl::~InputQueueImpl() { DetachLooper(); }

void InputQueueImpl::AttachLooper(ALooper *looper, int ident, void *callback, void *data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (attached_looper_ == looper && looper_ident_ == ident) {
        return;
    }

    if (attached_looper_) {
        DetachLooper();
    }

    attached_looper_ = looper;
    looper_ident_ = ident;

    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    if (fd_table && attached_looper_) {
        int pipefds[2] = {-1, -1};
        if (fd_table->CreatePipe(pipefds, 0) == 0) {
            read_pipe_fd_ = pipefds[0];
            write_pipe_fd_ = pipefds[1];

            auto cb = reinterpret_cast<ALooper_callbackFunc>(callback);
            ALooper_addFd(attached_looper_, read_pipe_fd_, looper_ident_, ALOOPER_EVENT_INPUT, cb,
                          data);

            if (!queue_.empty()) {
                SignalWakePipe();
            }
        }
    }
}

void InputQueueImpl::DetachLooper()
{
    if (attached_looper_ && read_pipe_fd_ >= 0) {
        ALooper_removeFd(attached_looper_, read_pipe_fd_);
    }

    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    if (fd_table) {
        if (read_pipe_fd_ >= 0) {
            fd_table->Close(read_pipe_fd_);
            read_pipe_fd_ = -1;
        }
        if (write_pipe_fd_ >= 0) {
            fd_table->Close(write_pipe_fd_);
            write_pipe_fd_ = -1;
        }
    }

    attached_looper_ = nullptr;
    looper_ident_ = -1;
}

int InputQueueImpl::HasEvents() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() ? 0 : 1;
}

int InputQueueImpl::GetEvent(AInputEvent **out_event)
{
    if (!out_event)
        return -1;

    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        *out_event = nullptr;
        DrainWakePipe();
        return -1;
    }

    auto evt = std::move(queue_.front());
    queue_.pop_front();
    if (queue_.empty()) {
        DrainWakePipe();
    }

    *out_event = reinterpret_cast<AInputEvent *>(evt.release());
    return 0;
}

int InputQueueImpl::PreDispatchEvent(AInputEvent *event)
{
    (void)event;
    return 0; // Not consumed by framework, pass to app
}

void InputQueueImpl::FinishEvent(AInputEvent *event, int handled)
{
    (void)handled;
    if (event) {
        auto *internal_evt = reinterpret_cast<InternalInputEvent *>(event);
        delete internal_evt;
    }
}

void InputQueueImpl::EnqueueKeyEvent(int32_t action, int32_t key_code, int32_t repeat_count,
                                     int32_t meta_state, int32_t source)
{
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

    auto evt = std::make_unique<InternalInputEvent>();
    evt->type = AINPUT_EVENT_TYPE_KEY;
    evt->device_id = 0;
    evt->source = source;
    evt->key_action = action;
    evt->key_code = key_code;
    evt->repeat_count = repeat_count;
    evt->meta_state = meta_state;
    evt->down_time = now_ns;
    evt->event_time = now_ns;

    std::lock_guard<std::mutex> lock(mutex_);
    bool was_empty = queue_.empty();
    queue_.push_back(std::move(evt));

    if (was_empty) {
        SignalWakePipe();
    }
}

void InputQueueImpl::EnqueueMotionEvent(int32_t action, const std::vector<InputPointer> &pointers,
                                        int32_t source,
                                        const std::vector<HistoricalSample> &history)
{
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

    auto evt = std::make_unique<InternalInputEvent>();
    evt->type = AINPUT_EVENT_TYPE_MOTION;
    evt->device_id = 0;
    evt->source = source;
    evt->motion_action = action;
    evt->pointers = pointers;
    evt->history = history;
    evt->down_time = now_ns;
    evt->event_time = now_ns;

    std::lock_guard<std::mutex> lock(mutex_);
    bool was_empty = queue_.empty();
    queue_.push_back(std::move(evt));

    if (was_empty) {
        SignalWakePipe();
    }
}

void InputQueueImpl::SignalWakePipe()
{
    if (write_pipe_fd_ < 0)
        return;
    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table)
        return;

    auto desc = fd_table->Get(write_pipe_fd_);
    if (desc && desc->handle != INVALID_HANDLE_VALUE) {
        uint8_t byte = 1;
        DWORD written = 0;
        WriteFile(desc->handle, &byte, 1, &written, nullptr);
    }
}

void InputQueueImpl::DrainWakePipe()
{
    if (read_pipe_fd_ < 0)
        return;
    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    if (!fd_table)
        return;

    auto desc = fd_table->Get(read_pipe_fd_);
    if (desc && desc->handle != INVALID_HANDLE_VALUE) {
        uint8_t buf[64];
        DWORD bytes_read = 0;
        DWORD avail = 0;
        if (PeekNamedPipe(desc->handle, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            ReadFile(desc->handle, buf, std::min<DWORD>(avail, sizeof(buf)), &bytes_read, nullptr);
        }
    }
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) void AInputQueue_attachLooper(AInputQueue *queue, ALooper *looper,
                                                        int ident, void *callback, void *data)
{
    if (queue) {
        reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->AttachLooper(looper, ident,
                                                                              callback, data);
    }
}

__attribute__((sysv_abi)) void AInputQueue_detachLooper(AInputQueue *queue)
{
    if (queue) {
        reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->DetachLooper();
    }
}

__attribute__((sysv_abi)) int AInputQueue_hasEvents(AInputQueue *queue)
{
    if (!queue)
        return 0;
    return reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->HasEvents();
}

__attribute__((sysv_abi)) int AInputQueue_getEvent(AInputQueue *queue, AInputEvent **outEvent)
{
    if (!queue)
        return -1;
    return reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->GetEvent(outEvent);
}

__attribute__((sysv_abi)) int AInputQueue_preDispatchEvent(AInputQueue *queue, AInputEvent *event)
{
    if (!queue)
        return 0;
    return reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->PreDispatchEvent(event);
}

__attribute__((sysv_abi)) void AInputQueue_finishEvent(AInputQueue *queue, AInputEvent *event,
                                                       int handled)
{
    if (queue) {
        reinterpret_cast<portage::ndk::InputQueueImpl *>(queue)->FinishEvent(event, handled);
    }
}

__attribute__((sysv_abi)) int32_t AInputEvent_getType(const AInputEvent *event)
{
    if (!event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(event)->type;
}

__attribute__((sysv_abi)) int32_t AInputEvent_getDeviceId(const AInputEvent *event)
{
    if (!event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(event)->device_id;
}

__attribute__((sysv_abi)) int32_t AInputEvent_getSource(const AInputEvent *event)
{
    if (!event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(event)->source;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getAction(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->key_action;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getFlags(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->key_flags;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getKeyCode(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->key_code;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getScanCode(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->scan_code;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getMetaState(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->meta_state;
}

__attribute__((sysv_abi)) int32_t AKeyEvent_getRepeatCount(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->repeat_count;
}

__attribute__((sysv_abi)) int64_t AKeyEvent_getDownTime(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->down_time;
}

__attribute__((sysv_abi)) int64_t AKeyEvent_getEventTime(const AInputEvent *key_event)
{
    if (!key_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(key_event)->event_time;
}

__attribute__((sysv_abi)) int32_t AMotionEvent_getAction(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->motion_action;
}

__attribute__((sysv_abi)) int32_t AMotionEvent_getFlags(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->motion_flags;
}

__attribute__((sysv_abi)) int64_t AMotionEvent_getDownTime(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->down_time;
}

__attribute__((sysv_abi)) int64_t AMotionEvent_getEventTime(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->event_time;
}

__attribute__((sysv_abi)) size_t AMotionEvent_getPointerCount(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)
        ->pointers.size();
}

__attribute__((sysv_abi)) int32_t AMotionEvent_getPointerId(const AInputEvent *motion_event,
                                                            size_t pointer_index)
{
    if (!motion_event)
        return 0;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].id : 0;
}

__attribute__((sysv_abi)) float AMotionEvent_getX(const AInputEvent *motion_event,
                                                  size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].x : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getY(const AInputEvent *motion_event,
                                                  size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].y : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getPressure(const AInputEvent *motion_event,
                                                         size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].pressure : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getSize(const AInputEvent *motion_event,
                                                     size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].size : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getRawX(const AInputEvent *motion_event,
                                                     size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].raw_x : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getRawY(const AInputEvent *motion_event,
                                                     size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    return (pointer_index < ptrs.size()) ? ptrs[pointer_index].raw_y : 0.0f;
}

__attribute__((sysv_abi)) float AMotionEvent_getAxisValue(const AInputEvent *motion_event,
                                                          int32_t axis, size_t pointer_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &ptrs =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->pointers;
    if (pointer_index >= ptrs.size())
        return 0.0f;

    const auto &p = ptrs[pointer_index];
    switch (axis) {
    case AMOTION_EVENT_AXIS_X:
        return p.x;
    case AMOTION_EVENT_AXIS_Y:
        return p.y;
    case AMOTION_EVENT_AXIS_PRESSURE:
        return p.pressure;
    case AMOTION_EVENT_AXIS_SIZE:
        return p.size;
    case AMOTION_EVENT_AXIS_TOUCH_MAJOR:
        return p.touch_major;
    case AMOTION_EVENT_AXIS_TOUCH_MINOR:
        return p.touch_minor;
    case AMOTION_EVENT_AXIS_TOOL_MAJOR:
        return p.tool_major;
    case AMOTION_EVENT_AXIS_TOOL_MINOR:
        return p.tool_minor;
    case AMOTION_EVENT_AXIS_ORIENTATION:
        return p.orientation;
    default: {
        auto it = p.axes.find(axis);
        return (it != p.axes.end()) ? it->second : 0.0f;
    }
    }
}

__attribute__((sysv_abi)) float AMotionEvent_getTouchMajor(const AInputEvent *motion_event,
                                                           size_t pointer_index)
{
    return AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_TOUCH_MAJOR, pointer_index);
}

__attribute__((sysv_abi)) float AMotionEvent_getTouchMinor(const AInputEvent *motion_event,
                                                           size_t pointer_index)
{
    return AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_TOUCH_MINOR, pointer_index);
}

__attribute__((sysv_abi)) float AMotionEvent_getToolMajor(const AInputEvent *motion_event,
                                                          size_t pointer_index)
{
    return AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_TOOL_MAJOR, pointer_index);
}

__attribute__((sysv_abi)) float AMotionEvent_getToolMinor(const AInputEvent *motion_event,
                                                          size_t pointer_index)
{
    return AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_TOOL_MINOR, pointer_index);
}

__attribute__((sysv_abi)) float AMotionEvent_getOrientation(const AInputEvent *motion_event,
                                                            size_t pointer_index)
{
    return AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_ORIENTATION, pointer_index);
}

__attribute__((sysv_abi)) size_t AMotionEvent_getHistorySize(const AInputEvent *motion_event)
{
    if (!motion_event)
        return 0;
    return reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history.size();
}

__attribute__((sysv_abi)) int64_t
AMotionEvent_getHistoricalEventTime(const AInputEvent *motion_event, size_t history_index)
{
    if (!motion_event)
        return 0;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    return (history_index < hist.size()) ? hist[history_index].event_time : 0;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalRawX(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].raw_x;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalRawY(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].raw_y;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalX(const AInputEvent *motion_event,
                                                            size_t pointer_index,
                                                            size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].x;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalY(const AInputEvent *motion_event,
                                                            size_t pointer_index,
                                                            size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].y;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalPressure(const AInputEvent *motion_event,
                                                                   size_t pointer_index,
                                                                   size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].pressure;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalSize(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].size;
}

__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalTouchMajor(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].touch_major;
}

__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalTouchMinor(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].touch_minor;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalToolMajor(const AInputEvent *motion_event,
                                                                    size_t pointer_index,
                                                                    size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].tool_major;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalToolMinor(const AInputEvent *motion_event,
                                                                    size_t pointer_index,
                                                                    size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].tool_minor;
}

__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalOrientation(const AInputEvent *motion_event, size_t pointer_index,
                                      size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;
    return hist[history_index].pointers[pointer_index].orientation;
}

__attribute__((sysv_abi)) float AMotionEvent_getHistoricalAxisValue(const AInputEvent *motion_event,
                                                                    int32_t axis,
                                                                    size_t pointer_index,
                                                                    size_t history_index)
{
    if (!motion_event)
        return 0.0f;
    const auto &hist =
        reinterpret_cast<const portage::ndk::InternalInputEvent *>(motion_event)->history;
    if (history_index >= hist.size() || pointer_index >= hist[history_index].pointers.size())
        return 0.0f;

    const auto &p = hist[history_index].pointers[pointer_index];
    switch (axis) {
    case AMOTION_EVENT_AXIS_X:
        return p.x;
    case AMOTION_EVENT_AXIS_Y:
        return p.y;
    case AMOTION_EVENT_AXIS_PRESSURE:
        return p.pressure;
    case AMOTION_EVENT_AXIS_SIZE:
        return p.size;
    case AMOTION_EVENT_AXIS_TOUCH_MAJOR:
        return p.touch_major;
    case AMOTION_EVENT_AXIS_TOUCH_MINOR:
        return p.touch_minor;
    case AMOTION_EVENT_AXIS_TOOL_MAJOR:
        return p.tool_major;
    case AMOTION_EVENT_AXIS_TOOL_MINOR:
        return p.tool_minor;
    case AMOTION_EVENT_AXIS_ORIENTATION:
        return p.orientation;
    default: {
        auto it = p.axes.find(axis);
        return (it != p.axes.end()) ? it->second : 0.0f;
    }
    }
}

} // extern "C"
