#include "ndk/configuration.hpp"
#include <gtest/gtest.h>

TEST(ConfigurationTest, AllocatesAndModifiesConfiguration)
{
    AConfiguration *config = AConfiguration_new();
    ASSERT_NE(config, nullptr);

    AConfiguration_setOrientation(config, ACONFIGURATION_ORIENTATION_LAND);
    EXPECT_EQ(AConfiguration_getOrientation(config), ACONFIGURATION_ORIENTATION_LAND);

    AConfiguration_setDensity(config, ACONFIGURATION_DENSITY_XXHIGH);
    EXPECT_EQ(AConfiguration_getDensity(config), ACONFIGURATION_DENSITY_XXHIGH);

    AConfiguration_setScreenSize(config, ACONFIGURATION_SCREENSIZE_LARGE);
    EXPECT_EQ(AConfiguration_getScreenSize(config), ACONFIGURATION_SCREENSIZE_LARGE);

    AConfiguration_setTouchscreen(config, ACONFIGURATION_TOUCHSCREEN_FINGER);
    EXPECT_EQ(AConfiguration_getTouchscreen(config), ACONFIGURATION_TOUCHSCREEN_FINGER);

    AConfiguration_setKeyboard(config, ACONFIGURATION_KEYBOARD_QWERTY);
    EXPECT_EQ(AConfiguration_getKeyboard(config), ACONFIGURATION_KEYBOARD_QWERTY);

    AConfiguration_setNavigation(config, ACONFIGURATION_NAVIGATION_NONAV);
    EXPECT_EQ(AConfiguration_getNavigation(config), ACONFIGURATION_NAVIGATION_NONAV);

    AConfiguration_setKeysHidden(config, 1);
    EXPECT_EQ(AConfiguration_getKeysHidden(config), 1);

    AConfiguration_setNavHidden(config, 2);
    EXPECT_EQ(AConfiguration_getNavHidden(config), 2);

    AConfiguration_setSdkVersion(config, 34);
    EXPECT_EQ(AConfiguration_getSdkVersion(config), 34);

    AConfiguration_setScreenLong(config, ACONFIGURATION_SCREENLONG_YES);
    EXPECT_EQ(AConfiguration_getScreenLong(config), ACONFIGURATION_SCREENLONG_YES);

    AConfiguration_setUiModeType(config, ACONFIGURATION_UI_MODE_TYPE_NORMAL);
    EXPECT_EQ(AConfiguration_getUiModeType(config), ACONFIGURATION_UI_MODE_TYPE_NORMAL);

    AConfiguration_setUiModeNight(config, ACONFIGURATION_UI_MODE_NIGHT_YES);
    EXPECT_EQ(AConfiguration_getUiModeNight(config), ACONFIGURATION_UI_MODE_NIGHT_YES);

    AConfiguration_setScreenWidthDp(config, 1920);
    EXPECT_EQ(AConfiguration_getScreenWidthDp(config), 1920);

    AConfiguration_setScreenHeightDp(config, 1080);
    EXPECT_EQ(AConfiguration_getScreenHeightDp(config), 1080);

    AConfiguration_setSmallestScreenWidthDp(config, 720);
    EXPECT_EQ(AConfiguration_getSmallestScreenWidthDp(config), 720);

    AConfiguration *copy = AConfiguration_new();
    AConfiguration_copy(copy, config);

    EXPECT_EQ(AConfiguration_getOrientation(copy), ACONFIGURATION_ORIENTATION_LAND);
    EXPECT_EQ(AConfiguration_getDensity(copy), ACONFIGURATION_DENSITY_XXHIGH);
    EXPECT_EQ(AConfiguration_getScreenWidthDp(copy), 1920);

    AConfiguration_delete(copy);
    AConfiguration_delete(config);
}
