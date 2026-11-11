#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct ALooper;
struct AInputQueue;
struct AInputEvent;

enum {
    AINPUT_EVENT_TYPE_KEY = 1,
    AINPUT_EVENT_TYPE_MOTION = 2,
};

enum {
    AINPUT_SOURCE_UNKNOWN = 0x00000000,
    AINPUT_SOURCE_KEYBOARD = 0x00000101,
    AINPUT_SOURCE_DPAD = 0x00000201,
    AINPUT_SOURCE_GAMEPAD = 0x00000401,
    AINPUT_SOURCE_TOUCHSCREEN = 0x00001002,
    AINPUT_SOURCE_MOUSE = 0x00002002,
    AINPUT_SOURCE_STYLUS = 0x00004002,
    AINPUT_SOURCE_TRACKBALL = 0x00010004,
    AINPUT_SOURCE_TOUCHPAD = 0x00100008,
    AINPUT_SOURCE_TOUCH_NAVIGATION = 0x00200008,
    AINPUT_SOURCE_ROTARY_ENCODER = 0x00400000,
    AINPUT_SOURCE_JOYSTICK = 0x01000010,
};

enum {
    AKEY_EVENT_ACTION_DOWN = 0,
    AKEY_EVENT_ACTION_UP = 1,
    AKEY_EVENT_ACTION_MULTIPLE = 2,
};

enum {
    AKEYCODE_UNKNOWN = 0,
    AKEYCODE_DPAD_UP = 19,
    AKEYCODE_DPAD_DOWN = 20,
    AKEYCODE_DPAD_LEFT = 21,
    AKEYCODE_DPAD_RIGHT = 22,
    AKEYCODE_DPAD_CENTER = 23,
    AKEYCODE_BUTTON_A = 96,
    AKEYCODE_BUTTON_B = 97,
    AKEYCODE_BUTTON_C = 98,
    AKEYCODE_BUTTON_X = 99,
    AKEYCODE_BUTTON_Y = 100,
    AKEYCODE_BUTTON_Z = 101,
    AKEYCODE_BUTTON_L1 = 102,
    AKEYCODE_BUTTON_R1 = 103,
    AKEYCODE_BUTTON_L2 = 104,
    AKEYCODE_BUTTON_R2 = 105,
    AKEYCODE_BUTTON_THUMBL = 106,
    AKEYCODE_BUTTON_THUMBR = 107,
    AKEYCODE_BUTTON_START = 108,
    AKEYCODE_BUTTON_SELECT = 109,
    AKEYCODE_BUTTON_MODE = 110,
};

enum {
    AMOTION_EVENT_AXIS_X = 0,
    AMOTION_EVENT_AXIS_Y = 1,
    AMOTION_EVENT_AXIS_PRESSURE = 2,
    AMOTION_EVENT_AXIS_SIZE = 3,
    AMOTION_EVENT_AXIS_TOUCH_MAJOR = 4,
    AMOTION_EVENT_AXIS_TOUCH_MINOR = 5,
    AMOTION_EVENT_AXIS_TOOL_MAJOR = 6,
    AMOTION_EVENT_AXIS_TOOL_MINOR = 7,
    AMOTION_EVENT_AXIS_ORIENTATION = 8,
    AMOTION_EVENT_AXIS_VSCROLL = 9,
    AMOTION_EVENT_AXIS_HSCROLL = 10,
    AMOTION_EVENT_AXIS_Z = 11,
    AMOTION_EVENT_AXIS_RX = 12,
    AMOTION_EVENT_AXIS_RY = 13,
    AMOTION_EVENT_AXIS_RZ = 14,
    AMOTION_EVENT_AXIS_HAT_X = 15,
    AMOTION_EVENT_AXIS_HAT_Y = 16,
    AMOTION_EVENT_AXIS_LTRIGGER = 17,
    AMOTION_EVENT_AXIS_RTRIGGER = 18,
    AMOTION_EVENT_AXIS_THROTTLE = 19,
    AMOTION_EVENT_AXIS_RUDDER = 20,
    AMOTION_EVENT_AXIS_WHEEL = 21,
    AMOTION_EVENT_AXIS_GAS = 22,
    AMOTION_EVENT_AXIS_BRAKE = 23,
    AMOTION_EVENT_AXIS_DISTANCE = 24,
    AMOTION_EVENT_AXIS_TILT = 25,
    AMOTION_EVENT_AXIS_GENERIC_1 = 32,
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
    float touch_major{0.0f};
    float touch_minor{0.0f};
    float tool_major{0.0f};
    float tool_minor{0.0f};
    float orientation{0.0f};
    std::unordered_map<int32_t, float> axes{};
};

struct HistoricalSample {
    int64_t event_time{0};
    std::vector<InputPointer> pointers{};
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
    std::vector<HistoricalSample> history{};

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
                         int32_t meta_state = 0, int32_t source = AINPUT_SOURCE_KEYBOARD);
    void EnqueueMotionEvent(int32_t action, const std::vector<InputPointer> &pointers,
                            int32_t source = AINPUT_SOURCE_TOUCHSCREEN,
                            const std::vector<HistoricalSample> &history = {});

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
__attribute__((sysv_abi)) float AMotionEvent_getAxisValue(const AInputEvent *motion_event,
                                                          int32_t axis, size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getTouchMajor(const AInputEvent *motion_event,
                                                           size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getTouchMinor(const AInputEvent *motion_event,
                                                           size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getToolMajor(const AInputEvent *motion_event,
                                                          size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getToolMinor(const AInputEvent *motion_event,
                                                          size_t pointer_index);
__attribute__((sysv_abi)) float AMotionEvent_getOrientation(const AInputEvent *motion_event,
                                                            size_t pointer_index);

__attribute__((sysv_abi)) size_t AMotionEvent_getHistorySize(const AInputEvent *motion_event);
__attribute__((sysv_abi)) int64_t
AMotionEvent_getHistoricalEventTime(const AInputEvent *motion_event, size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalRawX(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalRawY(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalX(const AInputEvent *motion_event,
                                                            size_t pointer_index,
                                                            size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalY(const AInputEvent *motion_event,
                                                            size_t pointer_index,
                                                            size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalPressure(const AInputEvent *motion_event,
                                                                   size_t pointer_index,
                                                                   size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalSize(const AInputEvent *motion_event,
                                                               size_t pointer_index,
                                                               size_t history_index);
__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalTouchMajor(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index);
__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalTouchMinor(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalToolMajor(const AInputEvent *motion_event,
                                                                    size_t pointer_index,
                                                                    size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalToolMinor(const AInputEvent *motion_event,
                                                                    size_t pointer_index,
                                                                    size_t history_index);
__attribute__((sysv_abi)) float
AMotionEvent_getHistoricalOrientation(const AInputEvent *motion_event, size_t pointer_index,
                                      size_t history_index);
__attribute__((sysv_abi)) float AMotionEvent_getHistoricalAxisValue(const AInputEvent *motion_event,
                                                                    int32_t axis,
                                                                    size_t pointer_index,
                                                                    size_t history_index);
}
