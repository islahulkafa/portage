#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

struct ALooper;
struct AInputQueue;
struct AInputEvent;

enum {
    AINPUT_EVENT_TYPE_KEY = 1,
    AINPUT_EVENT_TYPE_MOTION = 2,
};

enum {
    AINPUT_SOURCE_KEYBOARD = 0x00000101,
    AINPUT_SOURCE_TOUCHSCREEN = 0x00001002,
    AINPUT_SOURCE_MOUSE = 0x00002002,
};

enum {
    AKEY_EVENT_ACTION_DOWN = 0,
    AKEY_EVENT_ACTION_UP = 1,
    AKEY_EVENT_ACTION_MULTIPLE = 2,
};

enum {
    AMOTION_EVENT_ACTION_MASK = 0xff,
    AMOTION_EVENT_ACTION_POINTER_INDEX_MASK = 0xff00,
    AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT = 8,
    AMOTION_EVENT_ACTION_DOWN = 0,
    AMOTION_EVENT_ACTION_UP = 1,
    AMOTION_EVENT_ACTION_MOVE = 2,
    AMOTION_EVENT_ACTION_CANCEL = 3,
    AMOTION_EVENT_ACTION_OUTSIDE = 4,
    AMOTION_EVENT_ACTION_POINTER_DOWN = 5,
    AMOTION_EVENT_ACTION_POINTER_UP = 6,
    AMOTION_EVENT_ACTION_HOVER_MOVE = 7,
    AMOTION_EVENT_ACTION_SCROLL = 8,
    AMOTION_EVENT_ACTION_HOVER_ENTER = 9,
    AMOTION_EVENT_ACTION_HOVER_EXIT = 10,
};

namespace portage::ndk {

struct InputPointer {
    int32_t id{0};
    float x{0.0f};
    float y{0.0f};
    float raw_x{0.0f};
    float raw_y{0.0f};
    float pressure{1.0f};
    float size{1.0f};
};

struct InternalInputEvent {
    int32_t type{0};
    int32_t device_id{0};
    int32_t source{0};

    // Key fields
    int32_t key_action{0};
    int32_t key_flags{0};
    int32_t key_code{0};
    int32_t scan_code{0};
    int32_t meta_state{0};
    int32_t repeat_count{0};

    // Motion fields
    int32_t motion_action{0};
    int32_t motion_flags{0};
    std::vector<InputPointer> pointers{};

    int64_t down_time{0};
    int64_t event_time{0};
};

class InputQueueImpl {
  public:
    InputQueueImpl();
    ~InputQueueImpl();

    void AttachLooper(ALooper *looper, int ident, void *callback, void *data);
    void DetachLooper();

    int HasEvents() const noexcept;
    int GetEvent(AInputEvent **out_event);
    int PreDispatchEvent(AInputEvent *event);
    void FinishEvent(AInputEvent *event, int handled);

    void EnqueueKeyEvent(int32_t action, int32_t key_code, int32_t repeat_count = 0,
                         int32_t meta_state = 0);
    void EnqueueMotionEvent(int32_t action, const std::vector<InputPointer> &pointers);

    [[nodiscard]] int GetReadPipeFd() const noexcept { return read_pipe_fd_; }

  private:
    void SignalWakePipe();
    void DrainWakePipe();

    mutable std::mutex mutex_{};
    std::deque<std::unique_ptr<InternalInputEvent>> queue_{};
    ALooper *attached_looper_{nullptr};
    int looper_ident_{-1};
    int read_pipe_fd_{-1};
    int write_pipe_fd_{-1};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) void AInputQueue_attachLooper(AInputQueue *queue, ALooper *looper,
                                                        int ident, void *callback, void *data);
__attribute__((sysv_abi)) void AInputQueue_detachLooper(AInputQueue *queue);
__attribute__((sysv_abi)) int AInputQueue_hasEvents(AInputQueue *queue);
__attribute__((sysv_abi)) int AInputQueue_getEvent(AInputQueue *queue, AInputEvent **outEvent);
__attribute__((sysv_abi)) int AInputQueue_preDispatchEvent(AInputQueue *queue, AInputEvent *event);
__attribute__((sysv_abi)) void AInputQueue_finishEvent(AInputQueue *queue, AInputEvent *event,
                                                       int handled);

__attribute__((sysv_abi)) int32_t AInputEvent_getType(const AInputEvent *event);
__attribute__((sysv_abi)) int32_t AInputEvent_getDeviceId(const AInputEvent *event);
__attribute__((sysv_abi)) int32_t AInputEvent_getSource(const AInputEvent *event);

__attribute__((sysv_abi)) int32_t AKeyEvent_getAction(const AInputEvent *key_event);
__attribute__((sysv_abi)) int32_t AKeyEvent_getFlags(const AInputEvent *key_event);
__attribute__((sysv_abi)) int32_t AKeyEvent_getKeyCode(const AInputEvent *key_event);
__attribute__((sysv_abi)) int32_t AKeyEvent_getScanCode(const AInputEvent *key_event);
__attribute__((sysv_abi)) int32_t AKeyEvent_getMetaState(const AInputEvent *key_event);
__attribute__((sysv_abi)) int32_t AKeyEvent_getRepeatCount(const AInputEvent *key_event);
__attribute__((sysv_abi)) int64_t AKeyEvent_getDownTime(const AInputEvent *key_event);
__attribute__((sysv_abi)) int64_t AKeyEvent_getEventTime(const AInputEvent *key_event);

__attribute__((sysv_abi)) int32_t AMotionEvent_getAction(const AInputEvent *motion_event);
__attribute__((sysv_abi)) int32_t AMotionEvent_getFlags(const AInputEvent *motion_event);
__attribute__((sysv_abi)) int64_t AMotionEvent_getDownTime(const AInputEvent *motion_event);
__attribute__((sysv_abi)) int64_t AMotionEvent_getEventTime(const AInputEvent *motion_event);
__attribute__((sysv_abi)) size_t AMotionEvent_getPointerCount(const AInputEvent *motion_event);
__attribute__((sysv_abi)) int32_t AMotionEvent_getPointerId(const AInputEvent *motion_event,
                                                            size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getX(const AInputEvent *motion_event,
                                                  size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getY(const AInputEvent *motion_event,
                                                  size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getPressure(const AInputEvent *motion_event,
                                                         size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getSize(const AInputEvent *motion_event,
                                                     size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getRawX(const AInputEvent *motion_event,
                                                     size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getRawY(const AInputEvent *motion_event,
                                                     size_t pointer_index);
}
