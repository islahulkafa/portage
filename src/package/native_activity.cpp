#include "native_activity.hpp"

#include <cstring>

namespace portage::package {

NativeActivityRunner &NativeActivityRunner::Instance() noexcept
{
    static NativeActivityRunner instance;
    return instance;
}

NativeActivityRunner::NativeActivityRunner()
{
    std::memset(&activity_, 0, sizeof(ANativeActivity));
    std::memset(&callbacks_, 0, sizeof(ANativeActivityCallbacks));
    activity_.callbacks = &callbacks_;
}

bool NativeActivityRunner::SetupActivity(const PackageInfo &pkg, ANativeWindow *window)
{
    internal_path_ = "/data/data/" + pkg.manifest.package_name + "/files";
    external_path_ = "/sdcard/Android/data/" + pkg.manifest.package_name + "/files";
    std::string cache_path = "/data/data/" + pkg.manifest.package_name + "/cache";

    JniEnvironment::Instance().Initialize(pkg.manifest.package_name, internal_path_, cache_path);

    asset_manager_ = AssetManagerBridge::Instance().CreateManager(pkg.archive);

    std::memset(&callbacks_, 0, sizeof(ANativeActivityCallbacks));
    std::memset(&activity_, 0, sizeof(ANativeActivity));

    activity_.callbacks = &callbacks_;
    activity_.vm = JniEnvironment::Instance().GetJavaVM();
    activity_.env = JniEnvironment::Instance().GetJNIEnv();
    activity_.clazz = const_cast<char *>(pkg.manifest.package_name.c_str());

    activity_.internalDataPath = internal_path_.c_str();
    activity_.externalDataPath = external_path_.c_str();

    activity_.sdkVersion =
        (pkg.manifest.target_sdk_version > 0) ? pkg.manifest.target_sdk_version : 34;
    activity_.instance = nullptr;
    activity_.assetManager = asset_manager_;
    activity_.obbPath = "";

    current_window_ = window;
    return true;
}

void NativeActivityRunner::DispatchStart()
{
    if (callbacks_.onStart) {
        callbacks_.onStart(&activity_);
    }
}

void NativeActivityRunner::DispatchResume()
{
    if (callbacks_.onResume) {
        callbacks_.onResume(&activity_);
    }
}

void NativeActivityRunner::DispatchWindowCreated(ANativeWindow *window)
{
    current_window_ = window;
    if (callbacks_.onNativeWindowCreated && current_window_) {
        callbacks_.onNativeWindowCreated(&activity_, current_window_);
    }
}

void NativeActivityRunner::DispatchWindowFocus(bool has_focus)
{
    if (callbacks_.onWindowFocusChanged) {
        callbacks_.onWindowFocusChanged(&activity_, has_focus ? 1 : 0);
    }
}

void NativeActivityRunner::DispatchWindowResized(ANativeWindow *window)
{
    current_window_ = window;
    if (callbacks_.onNativeWindowResized && current_window_) {
        callbacks_.onNativeWindowResized(&activity_, current_window_);
    }
}

void NativeActivityRunner::DispatchWindowDestroyed(ANativeWindow *window)
{
    if (callbacks_.onNativeWindowDestroyed && window) {
        callbacks_.onNativeWindowDestroyed(&activity_, window);
    }
    if (current_window_ == window) {
        current_window_ = nullptr;
    }
}

void NativeActivityRunner::DispatchInputQueueCreated(AInputQueue *queue)
{
    if (callbacks_.onInputQueueCreated && queue) {
        callbacks_.onInputQueueCreated(&activity_, queue);
    }
}

void NativeActivityRunner::DispatchInputQueueDestroyed(AInputQueue *queue)
{
    if (callbacks_.onInputQueueDestroyed && queue) {
        callbacks_.onInputQueueDestroyed(&activity_, queue);
    }
}

void NativeActivityRunner::DispatchConfigurationChanged()
{
    if (callbacks_.onConfigurationChanged) {
        callbacks_.onConfigurationChanged(&activity_);
    }
}

void NativeActivityRunner::DispatchLowMemory()
{
    if (callbacks_.onLowMemory) {
        callbacks_.onLowMemory(&activity_);
    }
}

void NativeActivityRunner::DispatchPause()
{
    if (callbacks_.onPause) {
        callbacks_.onPause(&activity_);
    }
}

void NativeActivityRunner::DispatchStop()
{
    if (callbacks_.onStop) {
        callbacks_.onStop(&activity_);
    }
}

void NativeActivityRunner::DispatchDestroy()
{
    if (callbacks_.onDestroy) {
        callbacks_.onDestroy(&activity_);
    }
}

} // namespace portage::package
