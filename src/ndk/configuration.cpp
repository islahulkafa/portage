#include "configuration.hpp"

#include <cstring>

extern "C" {

__attribute__((sysv_abi)) AConfiguration *AConfiguration_new()
{
    auto *cfg = new portage::ndk::ConfigurationData();
    return reinterpret_cast<AConfiguration *>(cfg);
}

__attribute__((sysv_abi)) void AConfiguration_delete(AConfiguration *config)
{
    if (config) {
        delete reinterpret_cast<portage::ndk::ConfigurationData *>(config);
    }
}

__attribute__((sysv_abi)) void AConfiguration_fromAssetManager(AConfiguration *out,
                                                               AAssetManager *am)
{
    (void)am;
    if (out) {
        auto *cfg = reinterpret_cast<portage::ndk::ConfigurationData *>(out);
        *cfg = portage::ndk::ConfigurationData();
    }
}

__attribute__((sysv_abi)) void AConfiguration_copy(AConfiguration *dest, AConfiguration *src)
{
    if (dest && src) {
        auto *d = reinterpret_cast<portage::ndk::ConfigurationData *>(dest);
        const auto *s = reinterpret_cast<const portage::ndk::ConfigurationData *>(src);
        *d = *s;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getOrientation(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->orientation
                  : ACONFIGURATION_ORIENTATION_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setOrientation(AConfiguration *config,
                                                             int32_t orientation)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->orientation = orientation;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getTouchscreen(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->touchscreen
                  : ACONFIGURATION_TOUCHSCREEN_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setTouchscreen(AConfiguration *config,
                                                             int32_t touchscreen)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->touchscreen = touchscreen;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getDensity(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->density
                  : ACONFIGURATION_DENSITY_DEFAULT;
}

__attribute__((sysv_abi)) void AConfiguration_setDensity(AConfiguration *config, int32_t density)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->density = density;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getKeyboard(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->keyboard
                  : ACONFIGURATION_KEYBOARD_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setKeyboard(AConfiguration *config, int32_t keyboard)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->keyboard = keyboard;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getNavigation(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->navigation
                  : ACONFIGURATION_NAVIGATION_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setNavigation(AConfiguration *config,
                                                            int32_t navigation)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->navigation = navigation;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getKeysHidden(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->keys_hidden : 0;
}

__attribute__((sysv_abi)) void AConfiguration_setKeysHidden(AConfiguration *config,
                                                            int32_t keysHidden)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->keys_hidden = keysHidden;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getNavHidden(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->nav_hidden : 0;
}

__attribute__((sysv_abi)) void AConfiguration_setNavHidden(AConfiguration *config,
                                                           int32_t navHidden)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->nav_hidden = navHidden;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getSdkVersion(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->sdk_version : 34;
}

__attribute__((sysv_abi)) void AConfiguration_setSdkVersion(AConfiguration *config,
                                                            int32_t sdkVersion)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->sdk_version = sdkVersion;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenSize(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_size
                  : ACONFIGURATION_SCREENSIZE_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setScreenSize(AConfiguration *config,
                                                            int32_t screenSize)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_size = screenSize;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenLong(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_long
                  : ACONFIGURATION_SCREENLONG_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setScreenLong(AConfiguration *config,
                                                            int32_t screenLong)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_long = screenLong;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getUiModeType(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->ui_mode_type
                  : ACONFIGURATION_UI_MODE_TYPE_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setUiModeType(AConfiguration *config,
                                                            int32_t uiModeType)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->ui_mode_type = uiModeType;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getUiModeNight(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->ui_mode_night
                  : ACONFIGURATION_UI_MODE_NIGHT_ANY;
}

__attribute__((sysv_abi)) void AConfiguration_setUiModeNight(AConfiguration *config,
                                                             int32_t uiModeNight)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->ui_mode_night = uiModeNight;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenWidthDp(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_width_dp
                  : 0;
}

__attribute__((sysv_abi)) void AConfiguration_setScreenWidthDp(AConfiguration *config,
                                                               int32_t value)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_width_dp = value;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getScreenHeightDp(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_height_dp
                  : 0;
}

__attribute__((sysv_abi)) void AConfiguration_setScreenHeightDp(AConfiguration *config,
                                                                int32_t value)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->screen_height_dp = value;
    }
}

__attribute__((sysv_abi)) int32_t AConfiguration_getSmallestScreenWidthDp(AConfiguration *config)
{
    return config ? reinterpret_cast<portage::ndk::ConfigurationData *>(config)
                        ->smallest_screen_width_dp
                  : 0;
}

__attribute__((sysv_abi)) void AConfiguration_setSmallestScreenWidthDp(AConfiguration *config,
                                                                       int32_t value)
{
    if (config) {
        reinterpret_cast<portage::ndk::ConfigurationData *>(config)->smallest_screen_width_dp =
            value;
    }
}

} // extern "C"
