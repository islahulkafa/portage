#pragma once

#include <cstdint>

struct AConfiguration;
struct AAssetManager;

enum {
    ACONFIGURATION_ORIENTATION_ANY = 0x0000,
    ACONFIGURATION_ORIENTATION_PORT = 0x0001,
    ACONFIGURATION_ORIENTATION_LAND = 0x0002,
    ACONFIGURATION_ORIENTATION_SQUARE = 0x0003,

    ACONFIGURATION_TOUCHSCREEN_ANY = 0x0000,
    ACONFIGURATION_TOUCHSCREEN_NOTOUCH = 0x0001,
    ACONFIGURATION_TOUCHSCREEN_STYLUS = 0x0002,
    ACONFIGURATION_TOUCHSCREEN_FINGER = 0x0003,

    ACONFIGURATION_DENSITY_DEFAULT = 0,
    ACONFIGURATION_DENSITY_LOW = 120,
    ACONFIGURATION_DENSITY_MEDIUM = 160,
    ACONFIGURATION_DENSITY_TV = 213,
    ACONFIGURATION_DENSITY_HIGH = 240,
    ACONFIGURATION_DENSITY_XHIGH = 320,
    ACONFIGURATION_DENSITY_XXHIGH = 480,
    ACONFIGURATION_DENSITY_XXXHIGH = 640,
    ACONFIGURATION_DENSITY_ANY = 0xfffe,
    ACONFIGURATION_DENSITY_NONE = 0xffff,

    ACONFIGURATION_KEYBOARD_ANY = 0x0000,
    ACONFIGURATION_KEYBOARD_NOKEYS = 0x0001,
    ACONFIGURATION_KEYBOARD_QWERTY = 0x0002,
    ACONFIGURATION_KEYBOARD_12KEY = 0x0003,

    ACONFIGURATION_NAVIGATION_ANY = 0x0000,
    ACONFIGURATION_NAVIGATION_NONAV = 0x0001,
    ACONFIGURATION_NAVIGATION_DPAD = 0x0002,
    ACONFIGURATION_NAVIGATION_TRACKBALL = 0x0003,
    ACONFIGURATION_NAVIGATION_WHEEL = 0x0004,

    ACONFIGURATION_SCREENSIZE_ANY = 0x00,
    ACONFIGURATION_SCREENSIZE_SMALL = 0x01,
    ACONFIGURATION_SCREENSIZE_NORMAL = 0x02,
    ACONFIGURATION_SCREENSIZE_LARGE = 0x03,
    ACONFIGURATION_SCREENSIZE_XLARGE = 0x04,

    ACONFIGURATION_SCREENLONG_ANY = 0x00,
    ACONFIGURATION_SCREENLONG_NO = 0x01,
    ACONFIGURATION_SCREENLONG_YES = 0x02,

    ACONFIGURATION_UI_MODE_TYPE_ANY = 0x00,
    ACONFIGURATION_UI_MODE_TYPE_NORMAL = 0x01,

    ACONFIGURATION_UI_MODE_NIGHT_ANY = 0x00,
    ACONFIGURATION_UI_MODE_NIGHT_NO = 0x01,
    ACONFIGURATION_UI_MODE_NIGHT_YES = 0x02,
};

namespace portage::ndk {

struct ConfigurationData {
    int32_t orientation{ACONFIGURATION_ORIENTATION_LAND};
    int32_t touchscreen{ACONFIGURATION_TOUCHSCREEN_FINGER};
    int32_t density{ACONFIGURATION_DENSITY_XHIGH};
    int32_t keyboard{ACONFIGURATION_KEYBOARD_QWERTY};
    int32_t navigation{ACONFIGURATION_NAVIGATION_NONAV};
    int32_t keys_hidden{1};
    int32_t nav_hidden{1};
    int32_t sdk_version{34};
    int32_t screen_size{ACONFIGURATION_SCREENSIZE_NORMAL};
    int32_t screen_long{ACONFIGURATION_SCREENLONG_NO};
    int32_t ui_mode_type{ACONFIGURATION_UI_MODE_TYPE_NORMAL};
    int32_t ui_mode_night{ACONFIGURATION_UI_MODE_NIGHT_NO};
    int32_t screen_width_dp{1280};
    int32_t screen_height_dp{720};
    int32_t smallest_screen_width_dp{720};
    char mcc[4]{0};
    char mnc[4]{0};
    char language[4]{'e', 'n', 0, 0};
    char country[4]{'U', 'S', 0, 0};
};

} // namespace portage::ndk

extern "C" {
__attribute__((sysv_abi)) AConfiguration *AConfiguration_new();
__attribute__((sysv_abi)) void AConfiguration_delete(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_fromAssetManager(AConfiguration *out,
                                                               AAssetManager *am);
__attribute__((sysv_abi)) void AConfiguration_copy(AConfiguration *dest, AConfiguration *src);

__attribute__((sysv_abi)) int32_t AConfiguration_getOrientation(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setOrientation(AConfiguration *config,
                                                             int32_t orientation);

__attribute__((sysv_abi)) int32_t AConfiguration_getTouchscreen(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setTouchscreen(AConfiguration *config,
                                                             int32_t touchscreen);

__attribute__((sysv_abi)) int32_t AConfiguration_getDensity(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setDensity(AConfiguration *config, int32_t density);

__attribute__((sysv_abi)) int32_t AConfiguration_getKeyboard(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setKeyboard(AConfiguration *config, int32_t keyboard);

__attribute__((sysv_abi)) int32_t AConfiguration_getNavigation(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setNavigation(AConfiguration *config,
                                                            int32_t navigation);

__attribute__((sysv_abi)) int32_t AConfiguration_getKeysHidden(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setKeysHidden(AConfiguration *config,
                                                            int32_t keysHidden);

__attribute__((sysv_abi)) int32_t AConfiguration_getNavHidden(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setNavHidden(AConfiguration *config,
                                                           int32_t navHidden);

__attribute__((sysv_abi)) int32_t AConfiguration_getSdkVersion(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setSdkVersion(AConfiguration *config,
                                                            int32_t sdkVersion);

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenSize(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setScreenSize(AConfiguration *config,
                                                            int32_t screenSize);

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenLong(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setScreenLong(AConfiguration *config,
                                                            int32_t screenLong);

__attribute__((sysv_abi)) int32_t AConfiguration_getUiModeType(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setUiModeType(AConfiguration *config,
                                                            int32_t uiModeType);

__attribute__((sysv_abi)) int32_t AConfiguration_getUiModeNight(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setUiModeNight(AConfiguration *config,
                                                             int32_t uiModeNight);

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenWidthDp(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setScreenWidthDp(AConfiguration *config,
                                                               int32_t value);

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenHeightDp(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setScreenHeightDp(AConfiguration *config,
                                                                int32_t value);

__attribute__((sysv_abi)) int32_t AConfiguration_getSmallestScreenWidthDp(AConfiguration *config);
__attribute__((sysv_abi)) void AConfiguration_setSmallestScreenWidthDp(AConfiguration *config,
                                                                       int32_t value);
}
