#include "ndk/choreographer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "ndk/looper.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace portage::ndk {

class ChoreographerTest : public ::testing::Test {
  protected:
    void TearDown() override { VsyncPacer::Instance().ClearRefreshRateOverride(); }
};

TEST_F(ChoreographerTest, GetInstancePreservesThreadAffinityAndAutoPreparesLooper)
{
    AChoreographer *ch1_a = nullptr;
    AChoreographer *ch1_b = nullptr;
    ALooper *looper1 = nullptr;

    std::thread t1([&]() {
        EXPECT_EQ(ALooper_forThread(), nullptr);
        ch1_a = AChoreographer_getInstance();
        ch1_b = AChoreographer_getInstance();
        looper1 = ALooper_forThread();
    });
    t1.join();

    AChoreographer *ch2 = nullptr;
    ALooper *looper2 = nullptr;
    std::thread t2([&]() {
        EXPECT_EQ(ALooper_forThread(), nullptr);
        ch2 = AChoreographer_getInstance();
        looper2 = ALooper_forThread();
    });
    t2.join();

    ASSERT_NE(ch1_a, nullptr);
    EXPECT_EQ(ch1_a, ch1_b);
    EXPECT_NE(looper1, nullptr);

    ASSERT_NE(ch2, nullptr);
    EXPECT_NE(looper2, nullptr);
    EXPECT_NE(ch1_a, ch2);
}

TEST_F(ChoreographerTest, DispatchesImmediate64BitFrameCallbacksWithMonotonicTimestamps)
{
    std::thread worker([]() {
        ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        ASSERT_NE(looper, nullptr);

        AChoreographer *ch = AChoreographer_getInstance();
        ASSERT_NE(ch, nullptr);

        struct FrameTracker {
            AChoreographer *choreographer{nullptr};
            std::thread::id owner_tid{};
            bool thread_matched{true};
            std::vector<int64_t> timestamps{};

            static void __attribute__((sysv_abi)) OnFrame(int64_t frame_time_nanos, void *data)
            {
                auto *self = static_cast<FrameTracker *>(data);
                if (std::this_thread::get_id() != self->owner_tid) {
                    self->thread_matched = false;
                }
                self->timestamps.push_back(frame_time_nanos);
                if (self->timestamps.size() < 3) {
                    AChoreographer_postFrameCallback64(self->choreographer, &FrameTracker::OnFrame,
                                                       self);
                }
            }
        } tracker;

        tracker.choreographer = ch;
        tracker.owner_tid = std::this_thread::get_id();

        AChoreographer_postFrameCallback64(ch, &FrameTracker::OnFrame, &tracker);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (tracker.timestamps.size() < 3 && std::chrono::steady_clock::now() < deadline) {
            int res = ALooper_pollOnce(50, nullptr, nullptr, nullptr);
            EXPECT_EQ(res, ALOOPER_POLL_CALLBACK);
        }

        ASSERT_EQ(tracker.timestamps.size(), 3U);
        EXPECT_TRUE(tracker.thread_matched);
        EXPECT_GT(tracker.timestamps[0], 0);
        EXPECT_GT(tracker.timestamps[1], tracker.timestamps[0]);
        EXPECT_GT(tracker.timestamps[2], tracker.timestamps[1]);
    });
    worker.join();
}

TEST_F(ChoreographerTest, RespectsDelayedFrameCallbackScheduling)
{
    std::thread worker([]() {
        ALooper_prepare(0);
        AChoreographer *ch = AChoreographer_getInstance();
        ASSERT_NE(ch, nullptr);

        struct DelayState {
            bool immediate_fired{false};
            bool delayed_fired{false};
            int64_t immediate_time{0};
            int64_t delayed_time{0};

            static void __attribute__((sysv_abi)) OnImmediate(int64_t ts, void *data)
            {
                auto *s = static_cast<DelayState *>(data);
                s->immediate_fired = true;
                s->immediate_time = ts;
            }

            static void __attribute__((sysv_abi)) OnDelayed(int64_t ts, void *data)
            {
                auto *s = static_cast<DelayState *>(data);
                s->delayed_fired = true;
                s->delayed_time = ts;
            }
        } state;

        int64_t post_time_ns = VsyncPacer::NowNanos();
        AChoreographer_postFrameCallback64(ch, &DelayState::OnImmediate, &state);
        AChoreographer_postFrameCallbackDelayed64(ch, &DelayState::OnDelayed, &state, 120);

        int res = ALooper_pollOnce(50, nullptr, nullptr, nullptr);
        EXPECT_EQ(res, ALOOPER_POLL_CALLBACK);
        EXPECT_TRUE(state.immediate_fired);
        EXPECT_FALSE(state.delayed_fired);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (!state.delayed_fired && std::chrono::steady_clock::now() < deadline) {
            ALooper_pollOnce(50, nullptr, nullptr, nullptr);
        }

        EXPECT_TRUE(state.delayed_fired);
        EXPECT_GE(state.delayed_time - post_time_ns, 120'000'000LL);
        EXPECT_GT(state.delayed_time, state.immediate_time);
    });
    worker.join();
}

TEST_F(ChoreographerTest, DeliversInitialAndUpdatedRefreshRateCallbacksAndUnregisters)
{
    std::thread worker([]() {
        ALooper_prepare(0);
        AChoreographer *ch = AChoreographer_getInstance();
        ASSERT_NE(ch, nullptr);

        struct RefreshState {
            std::vector<int64_t> periods{};

            static void __attribute__((sysv_abi)) OnRefresh(int64_t period_nanos, void *data)
            {
                static_cast<RefreshState *>(data)->periods.push_back(period_nanos);
            }
        } state;

        VsyncPacer::Instance().SetRefreshRateHz(60.0);
        AChoreographer_registerRefreshRateCallback(ch, &RefreshState::OnRefresh, &state);

        int res = ALooper_pollOnce(50, nullptr, nullptr, nullptr);
        EXPECT_EQ(res, ALOOPER_POLL_CALLBACK);
        ASSERT_EQ(state.periods.size(), 1U);
        EXPECT_NEAR(static_cast<double>(state.periods[0]), 16'666'666.0, 50'000.0);

        VsyncPacer::Instance().SetRefreshRateHz(120.0);
        res = ALooper_pollOnce(50, nullptr, nullptr, nullptr);
        EXPECT_EQ(res, ALOOPER_POLL_CALLBACK);
        ASSERT_EQ(state.periods.size(), 2U);
        EXPECT_NEAR(static_cast<double>(state.periods[1]), 8'333'333.0, 50'000.0);

        AChoreographer_unregisterRefreshRateCallback(ch, &RefreshState::OnRefresh, &state);
        VsyncPacer::Instance().SetRefreshRateHz(90.0);
        res = ALooper_pollOnce(15, nullptr, nullptr, nullptr);
        EXPECT_EQ(res, ALOOPER_POLL_TIMEOUT);
        EXPECT_EQ(state.periods.size(), 2U);
    });
    worker.join();
}

TEST_F(ChoreographerTest, SupportsLegacyCallbacksAndRegistersAllNdkSymbols)
{
    auto &reg = NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("AChoreographer_getInstance"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_postFrameCallback"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_postFrameCallbackDelayed"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_postFrameCallback64"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_postFrameCallbackDelayed64"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_registerRefreshRateCallback"), 0U);
    EXPECT_NE(reg.Lookup("AChoreographer_unregisterRefreshRateCallback"), 0U);

    std::thread worker([]() {
        ALooper_prepare(0);
        AChoreographer *ch = AChoreographer_getInstance();
        ASSERT_NE(ch, nullptr);

        int64_t recorded_ts = 0;
        struct FrameCallbackHelper {
            static void __attribute__((sysv_abi)) Callback(int64_t frame_time_nanos, void *data)
            {
                *static_cast<int64_t *>(data) = frame_time_nanos;
            }
        };

        AChoreographer_postFrameCallback(ch, &FrameCallbackHelper::Callback, &recorded_ts);
        int res = ALooper_pollOnce(100, nullptr, nullptr, nullptr);
        EXPECT_EQ(res, ALOOPER_POLL_CALLBACK);
        EXPECT_GT(recorded_ts, 0);
    });
    worker.join();
}

} // namespace portage::ndk
