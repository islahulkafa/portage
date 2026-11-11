#include <gtest/gtest.h>

#include "wsi/window_manager.hpp"

namespace portage::wsi {

class WindowManagerTest : public ::testing::Test {
  protected:
    void SetUp() override { WindowManager::Instance().Reset(); }

    void TearDown() override { WindowManager::Instance().Reset(); }
};

TEST_F(WindowManagerTest, HeadlessModeOperatesWithoutNativeHwnd)
{
    auto &mgr = WindowManager::Instance();
    mgr.SetHeadless(true);
    EXPECT_TRUE(mgr.IsHeadless());

    EXPECT_TRUE(mgr.EnsureWindow(800, 600, "HeadlessTest"));
    EXPECT_EQ(mgr.GetWindowHandle(), nullptr);
    EXPECT_EQ(mgr.GetWidth(), 800u);
    EXPECT_EQ(mgr.GetHeight(), 600u);

    mgr.RecordFramePresented();
    mgr.RecordFramePresented();

    auto stats = mgr.GetStats();
    EXPECT_TRUE(stats.is_headless);
    EXPECT_TRUE(stats.is_open);
    EXPECT_EQ(stats.frames_presented, 2u);
    EXPECT_EQ(stats.width, 800u);
    EXPECT_EQ(stats.height, 600u);

    mgr.DestroyWindow();
    EXPECT_EQ(mgr.GetWindowHandle(), nullptr);
}

TEST_F(WindowManagerTest, NativeWindowCreationAndDestruction)
{
    auto &mgr = WindowManager::Instance();
    mgr.SetHeadless(false);
    EXPECT_FALSE(mgr.IsHeadless());

    EXPECT_TRUE(mgr.EnsureWindow(640, 480, "Portage Native Test Window"));
    HWND hwnd = mgr.GetWindowHandle();
    EXPECT_NE(hwnd, nullptr);
    EXPECT_EQ(mgr.GetWidth(), 640u);
    EXPECT_EQ(mgr.GetHeight(), 480u);

    // Idempotent call
    EXPECT_TRUE(mgr.EnsureWindow(640, 480, "Portage Native Test Window"));
    EXPECT_EQ(mgr.GetWindowHandle(), hwnd);

    mgr.RecordFramePresented();
    auto stats = mgr.GetStats();
    EXPECT_FALSE(stats.is_headless);
    EXPECT_TRUE(stats.is_open);
    EXPECT_EQ(stats.frames_presented, 1u);

    mgr.DestroyWindow();
    EXPECT_EQ(mgr.GetWindowHandle(), nullptr);
}

TEST_F(WindowManagerTest, WmCloseDispatchesCloseOnceAndWaitUntilClosedWaitsForHwndCleanup)
{
    auto &mgr = WindowManager::Instance();
    mgr.SetHeadless(false);

    std::atomic<int> close_count{0};
    mgr.SetCloseCallback([&close_count]() { close_count.fetch_add(1); });

    ASSERT_TRUE(mgr.EnsureWindow(320, 240, "Portage Close Test"));
    HWND hwnd = mgr.GetWindowHandle();
    ASSERT_NE(hwnd, nullptr);

    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    mgr.WaitUntilClosed();

    EXPECT_EQ(mgr.GetWindowHandle(), nullptr);
    mgr.DestroyWindow();
    EXPECT_EQ(close_count.load(), 1);
}

TEST_F(WindowManagerTest, ReopeningWindowAfterNaturalCloseSucceedsWithoutTerminating)
{
    auto &mgr = WindowManager::Instance();
    mgr.SetHeadless(false);

    ASSERT_TRUE(mgr.EnsureWindow(320, 240, "Portage Reopen Test 1"));
    HWND hwnd1 = mgr.GetWindowHandle();
    ASSERT_NE(hwnd1, nullptr);

    PostMessageW(hwnd1, WM_CLOSE, 0, 0);
    mgr.WaitUntilClosed();
    mgr.DestroyWindow();

    ASSERT_TRUE(mgr.EnsureWindow(320, 240, "Portage Reopen Test 2"));
    EXPECT_NE(mgr.GetWindowHandle(), nullptr);
    mgr.DestroyWindow();
    EXPECT_EQ(mgr.GetWindowHandle(), nullptr);
}

} // namespace portage::wsi
