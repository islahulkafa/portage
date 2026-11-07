#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/input_queue.hpp"
#include "ndk/looper.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <gtest/gtest.h>

class InputQueueTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<portage::mem::AddressSpace>();
        vfs_ = std::make_unique<portage::fs::Vfs>();
        fd_table_ = std::make_unique<portage::fs::FileDescriptorTable>();
        portage::syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(),
                                                        false);
    }

    void TearDown() override
    {
        portage::syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        as_.reset();
    }

    std::unique_ptr<portage::mem::AddressSpace> as_;
    std::unique_ptr<portage::fs::Vfs> vfs_;
    std::unique_ptr<portage::fs::FileDescriptorTable> fd_table_;
};

TEST_F(InputQueueTest, EnqueuesAndDequeuesKeyEvents)
{
    portage::ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 0);

    queue.EnqueueKeyEvent(AKEY_EVENT_ACTION_DOWN, 65, 0, 0); // Key 'A'
    queue.EnqueueKeyEvent(AKEY_EVENT_ACTION_UP, 65, 0, 0);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 1);

    AInputEvent *event1 = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event1), 0);
    ASSERT_NE(event1, nullptr);

    EXPECT_EQ(AInputEvent_getType(event1), AINPUT_EVENT_TYPE_KEY);
    EXPECT_EQ(AKeyEvent_getAction(event1), AKEY_EVENT_ACTION_DOWN);
    EXPECT_EQ(AKeyEvent_getKeyCode(event1), 65);
    EXPECT_EQ(AInputQueue_preDispatchEvent(q_ptr, event1), 0);

    AInputQueue_finishEvent(q_ptr, event1, 1);

    AInputEvent *event2 = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event2), 0);
    ASSERT_NE(event2, nullptr);

    EXPECT_EQ(AInputEvent_getType(event2), AINPUT_EVENT_TYPE_KEY);
    EXPECT_EQ(AKeyEvent_getAction(event2), AKEY_EVENT_ACTION_UP);
    EXPECT_EQ(AKeyEvent_getKeyCode(event2), 65);

    AInputQueue_finishEvent(q_ptr, event2, 1);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 0);
    AInputEvent *event3 = nullptr;
    EXPECT_EQ(AInputQueue_getEvent(q_ptr, &event3), -1);
}

TEST_F(InputQueueTest, EnqueuesAndDequeuesMotionEvents)
{
    portage::ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    std::vector<portage::ndk::InputPointer> ptrs = {
        {.id = 0,
         .x = 100.5f,
         .y = 200.5f,
         .raw_x = 100.5f,
         .raw_y = 200.5f,
         .pressure = 1.0f,
         .size = 1.0f},
        {.id = 1,
         .x = 300.0f,
         .y = 400.0f,
         .raw_x = 300.0f,
         .raw_y = 400.0f,
         .pressure = 0.5f,
         .size = 0.8f},
    };

    queue.EnqueueMotionEvent(AMOTION_EVENT_ACTION_MOVE, ptrs);

    EXPECT_EQ(AInputQueue_hasEvents(q_ptr), 1);

    AInputEvent *event = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event), 0);
    ASSERT_NE(event, nullptr);

    EXPECT_EQ(AInputEvent_getType(event), AINPUT_EVENT_TYPE_MOTION);
    EXPECT_EQ(AMotionEvent_getAction(event), AMOTION_EVENT_ACTION_MOVE);
    EXPECT_EQ(AMotionEvent_getPointerCount(event), 2U);

    EXPECT_EQ(AMotionEvent_getPointerId(event, 0), 0);
    EXPECT_FLOAT_EQ(AMotionEvent_getX(event, 0), 100.5f);
    EXPECT_FLOAT_EQ(AMotionEvent_getY(event, 0), 200.5f);

    EXPECT_EQ(AMotionEvent_getPointerId(event, 1), 1);
    EXPECT_FLOAT_EQ(AMotionEvent_getX(event, 1), 300.0f);
    EXPECT_FLOAT_EQ(AMotionEvent_getY(event, 1), 400.0f);

    AInputQueue_finishEvent(q_ptr, event, 1);
}

TEST_F(InputQueueTest, WakesUpLooperWhenEventIsEnqueued)
{
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASSERT_NE(looper, nullptr);

    portage::ndk::InputQueueImpl queue;
    auto *q_ptr = reinterpret_cast<AInputQueue *>(&queue);

    constexpr int kInputIdent = 200;
    AInputQueue_attachLooper(q_ptr, looper, kInputIdent, nullptr, nullptr);

    int out_fd = 0;
    int out_events = 0;
    void *out_data = nullptr;
    int poll_res = ALooper_pollOnce(10, &out_fd, &out_events, &out_data);
    EXPECT_EQ(poll_res, ALOOPER_POLL_TIMEOUT);

    queue.EnqueueKeyEvent(AKEY_EVENT_ACTION_DOWN, 42);

    poll_res = ALooper_pollOnce(50, &out_fd, &out_events, &out_data);
    EXPECT_EQ(poll_res, kInputIdent);

    AInputEvent *event = nullptr;
    ASSERT_EQ(AInputQueue_getEvent(q_ptr, &event), 0);
    ASSERT_NE(event, nullptr);
    AInputQueue_finishEvent(q_ptr, event, 1);

    AInputQueue_detachLooper(q_ptr);
}
