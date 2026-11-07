#include "thread/thread_manager.hpp"
#include <gtest/gtest.h>
#include <synchapi.h>

using namespace portage::thread;

TEST(ThreadManagerTest, RegistersMainThread)
{
    auto &tm = ThreadManager::Instance();
    int tid = tm.RegisterMainThread();
    EXPECT_GT(tid, 0);

    auto thread = tm.GetThread(tid);
    ASSERT_NE(thread, nullptr);
    EXPECT_EQ(thread->tid, tid);
    EXPECT_EQ(thread->win32_thread_id, GetCurrentThreadId());
}

TEST(ThreadManagerTest, ClearsChildTidOnExit)
{
    auto &tm = ThreadManager::Instance();
    int tid = tm.RegisterMainThread();

    int test_child_tid_var = 1234;
    tm.SetClearChildTid(tid, reinterpret_cast<uintptr_t>(&test_child_tid_var));

    EXPECT_EQ(test_child_tid_var, 1234);
    tm.OnThreadExit(tid, 0);

    // Memory must be zeroed upon exit per Linux CLONE_CHILD_CLEARTID semantics
    EXPECT_EQ(test_child_tid_var, 0);
}
