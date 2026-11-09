#include <gtest/gtest.h>

#include "ndk/libandroid_thunks.hpp"
#include "ndk/swappy.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace portage::ndk;

class SwappyTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        SwappyManager::Instance().ClearRefreshPeriodOverride();
        SwappyManager::Instance().SetTargetFrameRateHz(60);
        SwappyManager::Instance().ResetStats();
    }

    void TearDown() override
    {
        SwappyManager::Instance().ClearRefreshPeriodOverride();
        SwappyManager::Instance().DestroyGL();
        SwappyManager::Instance().DestroyDeviceVk(nullptr);
    }
};

TEST_F(SwappyTest, SwappyVkLifecycleAndPacing)
{
    uint64_t refresh_ns = SwappyVk_initAndGetRefreshCycleDuration(nullptr, nullptr, nullptr);
    EXPECT_GT(refresh_ns, 0u);

    ANativeWindow window{};
    void *mock_queue = reinterpret_cast<void *>(0x1000);
    void *mock_swapchain = reinterpret_cast<void *>(0x2000);

    SwappyVk_setWindow(nullptr, mock_queue, mock_swapchain, &window);

    EXPECT_EQ(SwappyVk_queuePresent(mock_queue, nullptr), 0);
    EXPECT_EQ(window.frame_index, 1u);
    EXPECT_GT(window.last_queue_time_nanos, 0);
    EXPECT_GT(window.last_present_time_nanos, 0);

    auto stats = SwappyManager::Instance().GetStats();
    EXPECT_EQ(stats.total_frames_presented, 1u);

    SwappyVk_destroySwapchain(nullptr, mock_swapchain);
    SwappyVk_destroyDevice(nullptr);
}

TEST_F(SwappyTest, SwappyGLLifecycleAndPacing)
{
    EXPECT_FALSE(SwappyGL_isEnabled());

    SwappyGL_init(nullptr, nullptr);
    EXPECT_TRUE(SwappyGL_isEnabled());

    ANativeWindow window{};
    SwappyGL_setWindow(&window);

    EXPECT_TRUE(SwappyGL_swap(nullptr, nullptr));
    EXPECT_EQ(window.frame_index, 1u);
    EXPECT_GT(window.last_queue_time_nanos, 0);

    SwappyGL_destroy();
    EXPECT_FALSE(SwappyGL_isEnabled());
}

TEST_F(SwappyTest, SwappyTargetFrameRateAndInterval)
{
    SwappyManager::Instance().SetRefreshPeriodOverride(16'666'667LL);

    SwappyVk_setTargetFrameRateHz(60);
    EXPECT_EQ(SwappyManager::Instance().GetTargetFrameRateHz(), 60u);
    int64_t interval60 = SwappyVk_getSwapIntervalNS();
    EXPECT_EQ(interval60, 16'666'667LL);

    SwappyVk_setTargetFrameRateHz(30);
    EXPECT_EQ(SwappyManager::Instance().GetTargetFrameRateHz(), 30u);
    int64_t interval30 = SwappyVk_getSwapIntervalNS();
    EXPECT_EQ(interval30, 33'333'334LL);

    SwappyGL_setAutoSwapInterval(false);
    EXPECT_FALSE(SwappyManager::Instance().IsAutoSwapIntervalEnabled());
    SwappyGL_setAutoSwapInterval(true);
    EXPECT_TRUE(SwappyManager::Instance().IsAutoSwapIntervalEnabled());
}

TEST_F(SwappyTest, NativeWindowQueueDurationAndTimestamps)
{
    ANativeWindow window{};
    ANativeWindow_Buffer buffer{};

    EXPECT_EQ(ANativeWindow_lock(&window, &buffer, nullptr), 0);
    EXPECT_EQ(ANativeWindow_unlockAndPost(&window), 0);
    EXPECT_EQ(window.frame_index, 1u);

    int64_t queue_duration = -1;
    EXPECT_EQ(ANativeWindow_getLastQueueDuration(&window, &queue_duration), 0);
    EXPECT_GE(queue_duration, 0);

    int64_t qtime = 0;
    int64_t ltime = 0;
    int64_t ptime = 0;
    EXPECT_EQ(ANativeWindow_getFrameTimestamps(&window, 1, &qtime, &ltime, &ptime), 0);
    EXPECT_GT(qtime, 0);
    EXPECT_GT(ltime, 0);
    EXPECT_GT(ptime, 0);

    EXPECT_EQ(ANativeWindow_getLastQueueDuration(nullptr, &queue_duration), -1);
    EXPECT_EQ(ANativeWindow_getFrameTimestamps(nullptr, 1, &qtime, &ltime, &ptime), -1);
}

TEST_F(SwappyTest, NdkSymbolRegistryLookup)
{
    const char *kSymbols[] = {
        "SwappyVk_initAndGetRefreshCycleDuration",
        "SwappyVk_setWindow",
        "SwappyVk_queuePresent",
        "SwappyVk_destroySwapchain",
        "SwappyVk_destroyDevice",
        "SwappyVk_setTargetFrameRateHz",
        "SwappyVk_getSwapIntervalNS",
        "SwappyVk_setAutoSwapInterval",
        "SwappyGL_init",
        "SwappyGL_setWindow",
        "SwappyGL_swap",
        "SwappyGL_destroy",
        "SwappyGL_setTargetFrameRateHz",
        "SwappyGL_getSwapIntervalNS",
        "SwappyGL_setAutoSwapInterval",
        "SwappyGL_isEnabled",
        "ANativeWindow_getLastQueueDuration",
        "ANativeWindow_getFrameTimestamps",
    };

    for (const char *name : kSymbols) {
        uintptr_t addr = NdkSymbolRegistry::Instance().Lookup(name);
        EXPECT_NE(addr, 0u) << "Missing symbol in NdkSymbolRegistry: " << name;
    }
}
