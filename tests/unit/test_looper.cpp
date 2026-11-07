#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/looper.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class LooperTest : public ::testing::Test {
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

TEST_F(LooperTest, InitializesAndAttachesLooperToThread)
{
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASSERT_NE(looper, nullptr);

    ALooper *thread_looper = ALooper_forThread();
    EXPECT_EQ(looper, thread_looper);

    ALooper_acquire(looper);
    ALooper_release(looper);
}

TEST_F(LooperTest, PollsPipeDescriptorAndInvokesCallback)
{
    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ASSERT_NE(looper, nullptr);

    int pipefds[2] = {-1, -1};
    ASSERT_EQ(fd_table_->CreatePipe(pipefds, 0), 0);

    struct CallbackState {
        bool called{false};
        int received_fd{-1};
        int received_events{0};

        static int __attribute__((sysv_abi)) Callback(int fd, int events, void *data)
        {
            auto *s = static_cast<CallbackState *>(data);
            s->called = true;
            s->received_fd = fd;
            s->received_events = events;
            return 1;
        }
    } state;

    int add_res = ALooper_addFd(looper, pipefds[0], 101, ALOOPER_EVENT_INPUT,
                                &CallbackState::Callback, &state);
    EXPECT_EQ(add_res, 1);

    // No events yet -> timeout
    int out_fd = 0;
    int out_events = 0;
    void *out_data = nullptr;
    int poll_res = ALooper_pollOnce(10, &out_fd, &out_events, &out_data);
    EXPECT_EQ(poll_res, ALOOPER_POLL_TIMEOUT);
    EXPECT_FALSE(state.called);

    // Write 1 byte to pipe write-end
    auto write_desc = fd_table_->Get(pipefds[1]);
    ASSERT_NE(write_desc, nullptr);
    uint8_t byte = 0x42;
    DWORD written = 0;
    BOOL ok = WriteFile(write_desc->handle, &byte, 1, &written, nullptr);
    EXPECT_TRUE(ok);
    EXPECT_EQ(written, 1U);

    // Now poll should invoke callback
    poll_res = ALooper_pollOnce(50, &out_fd, &out_events, &out_data);
    EXPECT_EQ(poll_res, ALOOPER_POLL_CALLBACK);
    EXPECT_TRUE(state.called);
    EXPECT_EQ(state.received_fd, pipefds[0]);
    EXPECT_EQ(state.received_events, ALOOPER_EVENT_INPUT);

    // Remove fd
    int rem_res = ALooper_removeFd(looper, pipefds[0]);
    EXPECT_EQ(rem_res, 1);

    fd_table_->Close(pipefds[0]);
    fd_table_->Close(pipefds[1]);
}

TEST_F(LooperTest, WakesUpLooperFromAnotherThread)
{
    ALooper *looper = ALooper_prepare(0);
    ASSERT_NE(looper, nullptr);

    std::thread wake_thread([looper]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ALooper_wake(looper);
    });

    int poll_res = ALooper_pollOnce(500, nullptr, nullptr, nullptr);
    EXPECT_EQ(poll_res, ALOOPER_POLL_WAKE);

    wake_thread.join();
}
