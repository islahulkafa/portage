#include "input/key_codes.hpp"
#include <gtest/gtest.h>

using namespace portage::input;

TEST(KeyCodesTest, MapsAlphanumericKeys)
{
    EXPECT_EQ(Win32VkToAndroidKeyCode('A'), AKEYCODE_A);
    EXPECT_EQ(Win32VkToAndroidKeyCode('Z'), AKEYCODE_Z);
    EXPECT_EQ(Win32VkToAndroidKeyCode('M'), AKEYCODE_M);

    EXPECT_EQ(Win32VkToAndroidKeyCode('0'), AKEYCODE_0);
    EXPECT_EQ(Win32VkToAndroidKeyCode('9'), AKEYCODE_9);
    EXPECT_EQ(Win32VkToAndroidKeyCode('5'), AKEYCODE_5);
}

TEST(KeyCodesTest, MapsNavigationAndActionKeys)
{
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x1B), AKEYCODE_BACK);        // VK_ESCAPE
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x0D), AKEYCODE_ENTER);       // VK_RETURN
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x20), AKEYCODE_SPACE);       // VK_SPACE
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x08), AKEYCODE_DEL);         // VK_BACK
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x2E), AKEYCODE_FORWARD_DEL); // VK_DELETE
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x09), AKEYCODE_TAB);         // VK_TAB

    EXPECT_EQ(Win32VkToAndroidKeyCode(0x25), AKEYCODE_DPAD_LEFT);  // VK_LEFT
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x26), AKEYCODE_DPAD_UP);    // VK_UP
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x27), AKEYCODE_DPAD_RIGHT); // VK_RIGHT
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x28), AKEYCODE_DPAD_DOWN);  // VK_DOWN
}

TEST(KeyCodesTest, MapsModifiersAndFunctionKeys)
{
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x10), AKEYCODE_SHIFT_LEFT); // VK_SHIFT
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x11), AKEYCODE_CTRL_LEFT);  // VK_CONTROL
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x12), AKEYCODE_ALT_LEFT);   // VK_MENU
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x70), AKEYCODE_F1);         // VK_F1
    EXPECT_EQ(Win32VkToAndroidKeyCode(0x7B), AKEYCODE_F12);        // VK_F12
    EXPECT_EQ(Win32VkToAndroidKeyCode(0xFF), AKEYCODE_UNKNOWN);
}
