#include "sync/futex.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace portage::sync;

TEST(FutexTest, ReturnsEagainWhenValMismatch)
{
    FutexManager futex;
    uint32_t val = 100;
    // Expected is 200, but val is 100 -> must return -EAGAIN (-11)
    int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT, 200, nullptr, nullptr, 0);
    EXPECT_EQ(ret, -11);
}

TEST(FutexTest, TimesOutWhenNoWake)
{
    FutexManager futex;
    uint32_t val = 42;
    LinuxTimespec timeout{
        .tv_sec = 0,
        .tv_nsec = 10'000'000 // 10 ms
    };

    int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT, 42, &timeout, nullptr, 0);
    EXPECT_EQ(ret, -110); // -ETIMEDOUT
}

TEST(FutexTest, WakesWaitingThread)
{
    FutexManager futex;
    uint32_t val = 1;
    bool woken = false;

    std::thread waiter([&]() {
        int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT, 1, nullptr, nullptr, 0);
        if (ret == 0) {
            woken = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    val = 2;
    int64_t wake_ret = futex.Futex(&val, LINUX_FUTEX_WAKE, 1, nullptr, nullptr, 0);
    EXPECT_GE(wake_ret, 1);

    waiter.join();
    EXPECT_TRUE(woken);
}
