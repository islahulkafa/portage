#include "binder/binder_ndk.hpp"
#include "binder/service_manager.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "input/gamepad_manager.hpp"
#include "input/haptic_manager.hpp"
#include "input/input_types.hpp"
#include "mem/address_space.hpp"
#include "ndk/input_queue.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <gtest/gtest.h>

using namespace portage;

class InputGamepadTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);

        input::GamepadManager::Instance().Reset();
        input::HapticManager::Instance().Reset();
        binder::ServiceManager::Instance().Reset();
    }

    void TearDown() override
    {
        input::GamepadManager::Instance().Reset();
        input::HapticManager::Instance().Reset();
        binder::ServiceManager::Instance().Reset();

        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        as_.reset();
    }

    std::unique_ptr<mem::AddressSpace> as_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

TEST_F(InputGamepadTest, MultiTouchPointerTracking)
{
    ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    std::vector<ndk::InputPointer> ptrs = {
        {.id = 0,
         .x = 150.0f,
         .y = 250.0f,
         .raw_x = 150.0f,
         .raw_y = 250.0f,
         .pressure = 0.85f,
         .size = 1.2f,
         .touch_major = 24.0f,
         .touch_minor = 12.0f,
         .tool_major = 8.0f,
         .tool_minor = 4.0f,
         .orientation = 0.5f},
        {.id = 1,
         .x = 450.0f,
         .y = 550.0f,
         .raw_x = 450.0f,
         .raw_y = 550.0f,
         .pressure = 0.65f,
         .size = 0.9f,
         .touch_major = 32.0f,
         .touch_minor = 16.0f,
         .tool_major = 10.0f,
         .tool_minor = 5.0f,
         .orientation = -0.25f},
    };

    queue.EnqueueMotionEvent(AMOTION_EVENT_ACTION_MOVE, ptrs, AINPUT_SOURCE_TOUCHSCREEN);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 1);

    AInputEvent *event = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event), 0);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(AInputEvent_getType(event), AINPUT_EVENT_TYPE_MOTION);
    EXPECT_EQ(AInputEvent_getSource(event), AINPUT_SOURCE_TOUCHSCREEN);
    EXPECT_EQ(AMotionEvent_getAction(event), AMOTION_EVENT_ACTION_MOVE);
    EXPECT_EQ(AMotionEvent_getPointerCount(event), 2u);

    // Pointer 0
    EXPECT_EQ(AMotionEvent_getPointerId(event, 0), 0);
    EXPECT_FLOAT_EQ(AMotionEvent_getX(event, 0), 150.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getY(event, 0), 250.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getPressure(event, 0), 0.85f);
    EXPECT_FLOAT_EQ(AMotionEvent_getSize(event, 0), 1.2f);
    EXPECT_FLOAT_EQ(AMotionEvent_getTouchMajor(event, 0), 24.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getTouchMinor(event, 0), 12.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getToolMajor(event, 0), 8.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getToolMinor(event, 0), 4.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getOrientation(event, 0), 0.5f);

    // Pointer 1
    EXPECT_EQ(AMotionEvent_getPointerId(event, 1), 1);
    EXPECT_FLOAT_EQ(AMotionEvent_getX(event, 1), 450.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getY(event, 1), 550.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getPressure(event, 1), 0.65f);
    EXPECT_FLOAT_EQ(AMotionEvent_getSize(event, 1), 0.9f);
    EXPECT_FLOAT_EQ(AMotionEvent_getTouchMajor(event, 1), 32.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getTouchMinor(event, 1), 16.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getToolMajor(event, 1), 10.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getToolMinor(event, 1), 5.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getOrientation(event, 1), -0.25f);

    AInputQueue_finishEvent(q_ptr, event, 1);
}

TEST_F(InputGamepadTest, GamepadAxisMapping)
{
    ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    auto &mgr = input::GamepadManager::Instance();
    mgr.SetInputQueue(&queue);

    input::GamepadState state{};
    state.connected = true;
    state.left_stick_x = 0.75f;
    state.left_stick_y = -0.5f;
    state.right_stick_x = -0.8f;
    state.right_stick_y = 0.6f;
    state.left_trigger = 0.9f;
    state.right_trigger = 0.3f;
    state.hat_x = 1.0f;
    state.hat_y = -1.0f;

    mgr.InjectSyntheticState(0, state);

    auto retrieved = mgr.GetState(0);
    EXPECT_TRUE(retrieved.connected);
    EXPECT_FLOAT_EQ(retrieved.left_stick_x, 0.75f);
    EXPECT_FLOAT_EQ(retrieved.left_stick_y, -0.5f);
    EXPECT_FLOAT_EQ(retrieved.right_stick_x, -0.8f);
    EXPECT_FLOAT_EQ(retrieved.right_stick_y, 0.6f);
    EXPECT_FLOAT_EQ(retrieved.left_trigger, 0.9f);
    EXPECT_FLOAT_EQ(retrieved.right_trigger, 0.3f);
    EXPECT_FLOAT_EQ(retrieved.hat_x, 1.0f);
    EXPECT_FLOAT_EQ(retrieved.hat_y, -1.0f);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 1);

    AInputEvent *event = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event), 0);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(AInputEvent_getType(event), AINPUT_EVENT_TYPE_MOTION);
    EXPECT_EQ(AInputEvent_getSource(event), AINPUT_SOURCE_JOYSTICK);

    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0), 0.75f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0), -0.5f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0), -0.8f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0), 0.6f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0), 0.9f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0), 0.3f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0), 1.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0), -1.0f);

    AInputQueue_finishEvent(q_ptr, event, 1);
}

TEST_F(InputGamepadTest, GamepadButtonKeyEvents)
{
    ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    auto &mgr = input::GamepadManager::Instance();
    mgr.SetInputQueue(&queue);

    input::GamepadState press_state{};
    press_state.connected = true;
    press_state.buttons = input::GAMEPAD_BUTTON_A | input::GAMEPAD_BUTTON_START;

    mgr.InjectSyntheticState(0, press_state);

    // Expect 2 key-down events (Button A and Button Start)
    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 1);

    std::vector<int32_t> down_keys;
    AInputEvent *event = nullptr;
    while (AInputQueue_getEvent(q_ptr, &event) == 0 && event) {
        EXPECT_EQ(AInputEvent_getType(event), AINPUT_EVENT_TYPE_KEY);
        EXPECT_EQ(AInputEvent_getSource(event), AINPUT_SOURCE_GAMEPAD);
        EXPECT_EQ(AKeyEvent_getAction(event), AKEY_EVENT_ACTION_DOWN);
        down_keys.push_back(AKeyEvent_getKeyCode(event));
        AInputQueue_finishEvent(q_ptr, event, 1);
    }

    EXPECT_EQ(down_keys.size(), 2u);
    EXPECT_NE(std::find(down_keys.begin(), down_keys.end(), AKEYCODE_BUTTON_A), down_keys.end());
    EXPECT_NE(std::find(down_keys.begin(), down_keys.end(), AKEYCODE_BUTTON_START), down_keys.end());

    // Release buttons
    input::GamepadState release_state{};
    release_state.connected = true;
    release_state.buttons = 0;

    mgr.InjectSyntheticState(0, release_state);

    std::vector<int32_t> up_keys;
    while (AInputQueue_getEvent(q_ptr, &event) == 0 && event) {
        EXPECT_EQ(AInputEvent_getType(event), AINPUT_EVENT_TYPE_KEY);
        EXPECT_EQ(AInputEvent_getSource(event), AINPUT_SOURCE_GAMEPAD);
        EXPECT_EQ(AKeyEvent_getAction(event), AKEY_EVENT_ACTION_UP);
        up_keys.push_back(AKeyEvent_getKeyCode(event));
        AInputQueue_finishEvent(q_ptr, event, 1);
    }

    EXPECT_EQ(up_keys.size(), 2u);
    EXPECT_NE(std::find(up_keys.begin(), up_keys.end(), AKEYCODE_BUTTON_A), up_keys.end());
    EXPECT_NE(std::find(up_keys.begin(), up_keys.end(), AKEYCODE_BUTTON_START), up_keys.end());
}

TEST_F(InputGamepadTest, EvdevForceFeedbackIoctls)
{
    auto &haptic = input::HapticManager::Instance();

    input::LinuxFfEffect effect{};
    effect.type = input::LINUX_FF_RUMBLE;
    effect.id = -1;
    effect.replay.length = 300;
    effect.u.rumble.strong_magnitude = 45000;
    effect.u.rumble.weak_magnitude = 25000;

    int16_t effect_id = -1;
    EXPECT_EQ(haptic.UploadEffect(effect, &effect_id), 0);
    EXPECT_GE(effect_id, 0);
    EXPECT_EQ(haptic.ActiveEffectsCount(), 1u);

    // Play effect
    EXPECT_EQ(haptic.PlayEffect(effect_id, 1), 0);

    uint16_t left = 0;
    uint16_t right = 0;
    EXPECT_TRUE(haptic.GetMotorSpeeds(0, left, right));
    EXPECT_EQ(left, 45000);
    EXPECT_EQ(right, 25000);

    // Stop effect
    EXPECT_EQ(haptic.PlayEffect(effect_id, 0), 0);
    EXPECT_FALSE(haptic.GetMotorSpeeds(0, left, right));
    EXPECT_EQ(left, 0);
    EXPECT_EQ(right, 0);

    // Erase effect
    EXPECT_EQ(haptic.EraseEffect(effect_id), 0);
    EXPECT_EQ(haptic.ActiveEffectsCount(), 0u);
}

TEST_F(InputGamepadTest, VibratorServiceIpc)
{
    auto *vib_binder = binder::ServiceManager::Instance().GetService("vibrator");
    ASSERT_NE(vib_binder, nullptr);

    // Transact vibrate for 250ms
    AParcel *in_parcel = AParcel_create();
    AParcel *out_parcel = AParcel_create();
    ASSERT_NE(in_parcel, nullptr);
    ASSERT_NE(out_parcel, nullptr);

    AParcel_writeInt32(in_parcel, 250);
    EXPECT_EQ(AIBinder_transact(vib_binder, 1, in_parcel, out_parcel, 0), STATUS_OK);

    int32_t ex = -1;
    EXPECT_EQ(AParcel_readInt32(out_parcel, &ex), STATUS_OK);
    EXPECT_EQ(ex, 0);

    uint16_t left = 0;
    uint16_t right = 0;
    EXPECT_TRUE(input::HapticManager::Instance().GetMotorSpeeds(0, left, right));
    EXPECT_GT(left, 0);
    EXPECT_GT(right, 0);

    AParcel_delete(in_parcel);
    AParcel_delete(out_parcel);

    // Transact cancel
    in_parcel = AParcel_create();
    out_parcel = AParcel_create();
    EXPECT_EQ(AIBinder_transact(vib_binder, 2, in_parcel, out_parcel, 0), STATUS_OK);
    EXPECT_FALSE(input::HapticManager::Instance().GetMotorSpeeds(0, left, right));

    AParcel_delete(in_parcel);
    AParcel_delete(out_parcel);
    AIBinder_decStrong(vib_binder);
}

TEST_F(InputGamepadTest, HistoricalMotionSamples)
{
    ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    std::vector<ndk::InputPointer> current_ptrs = {
        {.id = 0, .x = 100.0f, .y = 200.0f, .raw_x = 100.0f, .raw_y = 200.0f, .pressure = 1.0f, .size = 1.0f}
    };

    std::vector<ndk::HistoricalSample> history = {
        {.event_time = 1000000LL,
         .pointers = {{.id = 0,
                       .x = 90.0f,
                       .y = 190.0f,
                       .raw_x = 90.0f,
                       .raw_y = 190.0f,
                       .pressure = 0.8f,
                       .size = 1.0f,
                       .touch_major = 15.0f,
                       .touch_minor = 10.0f,
                       .tool_major = 5.0f,
                       .tool_minor = 3.0f,
                       .orientation = 0.1f}}},
        {.event_time = 2000000LL,
         .pointers = {{.id = 0,
                       .x = 95.0f,
                       .y = 195.0f,
                       .raw_x = 95.0f,
                       .raw_y = 195.0f,
                       .pressure = 0.9f,
                       .size = 1.0f,
                       .touch_major = 16.0f,
                       .touch_minor = 11.0f,
                       .tool_major = 6.0f,
                       .tool_minor = 4.0f,
                       .orientation = 0.2f}}}
    };

    queue.EnqueueMotionEvent(AMOTION_EVENT_ACTION_MOVE, current_ptrs, AINPUT_SOURCE_TOUCHSCREEN,
                            history);

    AInputEvent *event = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event), 0);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(AMotionEvent_getHistorySize(event), 2u);

    // History 0
    EXPECT_EQ(AMotionEvent_getHistoricalEventTime(event, 0), 1000000LL);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalX(event, 0, 0), 90.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalY(event, 0, 0), 190.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalPressure(event, 0, 0), 0.8f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalTouchMajor(event, 0, 0), 15.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalTouchMinor(event, 0, 0), 10.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalToolMajor(event, 0, 0), 5.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalToolMinor(event, 0, 0), 3.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalOrientation(event, 0, 0), 0.1f);

    // History 1
    EXPECT_EQ(AMotionEvent_getHistoricalEventTime(event, 1), 2000000LL);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalX(event, 0, 1), 95.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalY(event, 0, 1), 195.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalPressure(event, 0, 1), 0.9f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalTouchMajor(event, 0, 1), 16.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalTouchMinor(event, 0, 1), 11.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalToolMajor(event, 0, 1), 6.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalToolMinor(event, 0, 1), 4.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getHistoricalOrientation(event, 0, 1), 0.2f);

    AInputQueue_finishEvent(q_ptr, event, 1);
}

TEST_F(InputGamepadTest, RegistersAllNdkSymbols)
{
    const auto &reg = ndk::NdkSymbolRegistry::Instance();

    EXPECT_NE(reg.Lookup("AMotionEvent_getAxisValue"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getTouchMajor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getTouchMinor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getToolMajor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getToolMinor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getOrientation"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistorySize"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalEventTime"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalRawX"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalRawY"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalX"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalY"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalPressure"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalSize"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalTouchMajor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalTouchMinor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalToolMajor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalToolMinor"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalOrientation"), 0u);
    EXPECT_NE(reg.Lookup("AMotionEvent_getHistoricalAxisValue"), 0u);
}
