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

TEST(FutexTest, BitsetSelectiveWakeAndAbsoluteTimeouts)
{
    FutexManager futex;
    uint32_t val = 10;
    std::atomic<bool> woken1{false};
    std::atomic<bool> woken2{false};
    std::atomic<bool> woken3{false};

    std::thread t1([&]() {
        int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET, 10, nullptr, nullptr, 0x1);
        if (ret == 0) {
            woken1 = true;
        }
    });

    std::thread t2([&]() {
        int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET, 10, nullptr, nullptr, 0x2);
        if (ret == 0) {
            woken2 = true;
        }
    });

    std::thread t3([&]() {
        int64_t ret = futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET, 10, nullptr, nullptr, 0x4);
        if (ret == 0) {
            woken3 = true;
        }
    });

    while (futex.GetWaiterCount(&val) < 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Zero bitset returns -EINVAL
    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET, 10, nullptr, nullptr, 0), -22);
    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAKE_BITSET, 1, nullptr, nullptr, 0), -22);

    // Wake only mask 0x2 (thread 2)
    int64_t woke = futex.Futex(&val, LINUX_FUTEX_WAKE_BITSET, 10, nullptr, nullptr, 0x2);
    EXPECT_EQ(woke, 1);

    t2.join();
    EXPECT_TRUE(woken2);
    EXPECT_FALSE(woken1);
    EXPECT_FALSE(woken3);

    // Wake mask 0x5 (threads 1 and 3)
    woke = futex.Futex(&val, LINUX_FUTEX_WAKE_BITSET, 10, nullptr, nullptr, 0x5);
    EXPECT_EQ(woke, 2);

    t1.join();
    t3.join();
    EXPECT_TRUE(woken1);
    EXPECT_TRUE(woken3);

    // Absolute monotonic timeout in past returns -ETIMEDOUT immediately
    LinuxTimespec past_mono{.tv_sec = 1, .tv_nsec = 0};
    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET, 10, &past_mono, nullptr, 0x1), -110);

    // Absolute realtime timeout in past returns -ETIMEDOUT immediately
    LinuxTimespec past_real{.tv_sec = 1000, .tv_nsec = 0};
    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAIT_BITSET | LINUX_FUTEX_CLOCK_REALTIME, 10, &past_real,
                          nullptr, 0x1),
              -110);
}

TEST(FutexTest, RequeueAndCmpRequeueMoveWaiters)
{
    FutexManager futex;
    uint32_t uaddr1 = 100;
    uint32_t uaddr2 = 200;

    std::atomic<int> woken_count{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            int64_t ret = futex.Futex(&uaddr1, LINUX_FUTEX_WAIT, 100, nullptr, nullptr, 0);
            if (ret == 0) {
                woken_count++;
            }
        });
    }

    while (futex.GetWaiterCount(&uaddr1) < 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // CMP_REQUEUE mismatch returns -EAGAIN
    EXPECT_EQ(futex.Futex(&uaddr1, LINUX_FUTEX_CMP_REQUEUE, 1,
                          reinterpret_cast<const LinuxTimespec *>(static_cast<uintptr_t>(2)),
                          &uaddr2, 999),
              -11);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr1), 4u);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr2), 0u);

    // CMP_REQUEUE match: wake 1, requeue 2 to uaddr2 -> returns 1 + 2 = 3
    int64_t ret = futex.Futex(&uaddr1, LINUX_FUTEX_CMP_REQUEUE, 1,
                              reinterpret_cast<const LinuxTimespec *>(static_cast<uintptr_t>(2)),
                              &uaddr2, 100);
    EXPECT_EQ(ret, 3);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr1), 1u);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr2), 2u);

    // Wake 1 remaining waiter on uaddr1
    EXPECT_EQ(futex.Futex(&uaddr1, LINUX_FUTEX_WAKE, 1, nullptr, nullptr, 0), 1);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr1), 0u);

    // Wake 2 requeued waiters on uaddr2
    EXPECT_EQ(futex.Futex(&uaddr2, LINUX_FUTEX_WAKE, 2, nullptr, nullptr, 0), 2);
    EXPECT_EQ(futex.GetWaiterCount(&uaddr2), 0u);

    for (auto &t : threads) {
        t.join();
    }
    EXPECT_EQ(woken_count, 4);
}

TEST(FutexTest, WakeOpAtomicallyMutatesAndConditionallyWakes)
{
    FutexManager futex;
    uint32_t uaddr1 = 1;
    uint32_t uaddr2 = 10;

    std::atomic<bool> woken1{false};
    std::atomic<bool> woken2{false};

    std::thread t1([&]() {
        if (futex.Futex(&uaddr1, LINUX_FUTEX_WAIT, 1, nullptr, nullptr, 0) == 0) {
            woken1 = true;
        }
    });

    std::thread t2([&]() {
        if (futex.Futex(&uaddr2, LINUX_FUTEX_WAIT, 10, nullptr, nullptr, 0) == 0) {
            woken2 = true;
        }
    });

    while (futex.GetWaiterCount(&uaddr1) < 1 || futex.GetWaiterCount(&uaddr2) < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // val3 encodes: OP_ADD 5, CMP_EQ 10 (evaluated on oldval 10)
    // op = 1 (ADD), cmp = 0 (EQ), oparg = 5, cmparg = 10
    uint32_t val3 = (LINUX_FUTEX_OP_ADD << 28) | (5 << 12) | (LINUX_FUTEX_OP_CMP_EQ << 24) | 10;

    int64_t woken = futex.Futex(&uaddr1, LINUX_FUTEX_WAKE_OP, 1,
                                reinterpret_cast<const LinuxTimespec *>(static_cast<uintptr_t>(1)),
                                &uaddr2, val3);
    EXPECT_EQ(woken, 2);
    EXPECT_EQ(uaddr2, 15u);

    t1.join();
    t2.join();
    EXPECT_TRUE(woken1);
    EXPECT_TRUE(woken2);
}

TEST(FutexTest, PiMutexLockTrylockUnlockAndDeadlockCheck)
{
    FutexManager futex;
    uint32_t pi_futex = 0;

    // Fast acquisition via TRYLOCK_PI
    EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_TRYLOCK_PI, 0, nullptr, nullptr, 0), 0);
    EXPECT_NE(pi_futex, 0u);

    // Re-entrant lock on same thread returns -EDEADLK
    EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_TRYLOCK_PI, 0, nullptr, nullptr, 0), -35);
    EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_LOCK_PI, 0, nullptr, nullptr, 0), -35);

    std::atomic<bool> t2_acquired{false};
    std::thread t2([&]() {
        // Contended TRYLOCK_PI from other thread returns -EAGAIN
        EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_TRYLOCK_PI, 0, nullptr, nullptr, 0), -11);

        // Blocking LOCK_PI
        if (futex.Futex(&pi_futex, LINUX_FUTEX_LOCK_PI, 0, nullptr, nullptr, 0) == 0) {
            t2_acquired = true;
            // Unlock from holder
            EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0), 0);
        }
    });

    while (futex.GetWaiterCount(&pi_futex) < 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Unlock from owner hands off to t2
    EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0), 0);

    t2.join();
    EXPECT_TRUE(t2_acquired);
    EXPECT_EQ(pi_futex, 0u);

    // Unlock on unowned mutex returns -EPERM
    EXPECT_EQ(futex.Futex(&pi_futex, LINUX_FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0), -1);
}

TEST(FutexTest, WakeReturnsExactCountAndHandlesErrors)
{
    FutexManager futex;
    uint32_t val = 42;

    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAKE, 5, nullptr, nullptr, 0), 0);
    EXPECT_EQ(futex.Futex(&val, LINUX_FUTEX_WAKE, 0, nullptr, nullptr, 0), 0);

    // Misaligned pointer returns -EINVAL
    uint32_t *misaligned = reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(&val) + 1);
    EXPECT_EQ(futex.Futex(misaligned, LINUX_FUTEX_WAKE, 1, nullptr, nullptr, 0), -22);
    EXPECT_EQ(futex.Futex(nullptr, LINUX_FUTEX_WAKE, 1, nullptr, nullptr, 0), -22);
}
