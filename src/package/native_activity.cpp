#include "native_activity.hpp"
#include "input/gamepad_manager.hpp"
#include "input/key_codes.hpp"
#include "ndk/libandroid_thunks.hpp"

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
    activity_.clazz = JniEnvironment::Instance().GetContextObject();

    activity_.internalDataPath = internal_path_.c_str();
    activity_.externalDataPath = external_path_.c_str();

    activity_.sdkVersion =
        (pkg.manifest.target_sdk_version > 0) ? pkg.manifest.target_sdk_version : 34;
    activity_.instance = nullptr;
    activity_.assetManager = asset_manager_;
    activity_.obbPath = "";

    current_window_ = window;
    is_destroyed_.store(false);
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

void NativeActivityRunner::ConnectInputQueueToWindow()
{
    wsi::WindowManager::Instance().SetMouseCallback(
        [this](int32_t x, int32_t y, int32_t w, int32_t h, int32_t action) {
            float norm_x = static_cast<float>(x);
            float norm_y = static_cast<float>(y);
            if (current_window_ && w > 0 && h > 0) {
                norm_x = (static_cast<float>(x) / static_cast<float>(w)) *
                         static_cast<float>(current_window_->width);
                norm_y = (static_cast<float>(y) / static_cast<float>(h)) *
                         static_cast<float>(current_window_->height);
            }
            std::vector<ndk::InputPointer> ptrs = {{.id = 0,
                                                    .x = norm_x,
                                                    .y = norm_y,
                                                    .raw_x = norm_x,
                                                    .raw_y = norm_y,
                                                    .pressure = 1.0f,
                                                    .size = 1.0f}};
            input_queue_.EnqueueMotionEvent(action, ptrs);
        });

    wsi::WindowManager::Instance().SetMultiTouchCallback(
        [this](int32_t action, const std::vector<ndk::InputPointer> &ptrs) {
            std::vector<ndk::InputPointer> scaled_ptrs = ptrs;
            float win_w = static_cast<float>(wsi::WindowManager::Instance().GetWidth());
            float win_h = static_cast<float>(wsi::WindowManager::Instance().GetHeight());
            if (current_window_ && win_w > 0.0f && win_h > 0.0f) {
                for (auto &p : scaled_ptrs) {
                    p.x = (p.x / win_w) * static_cast<float>(current_window_->width);
                    p.y = (p.y / win_h) * static_cast<float>(current_window_->height);
                    p.raw_x = p.x;
                    p.raw_y = p.y;
                }
            }
            input_queue_.EnqueueMotionEvent(action, scaled_ptrs);
        });

    input::GamepadManager::Instance().SetInputQueue(&input_queue_);

    wsi::WindowManager::Instance().SetKeyCallback([this](uint32_t vk_code, bool pressed) {
        int32_t akey = input::Win32VkToAndroidKeyCode(vk_code);
        if (akey != input::AKEYCODE_UNKNOWN) {
            input_queue_.EnqueueKeyEvent(pressed ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP,
                                         akey);
        }
    });

    wsi::WindowManager::Instance().SetResizeCallback([this](uint32_t width, uint32_t height) {
        if (current_window_) {
            current_window_->width = static_cast<int32_t>(width);
            current_window_->height = static_cast<int32_t>(height);
            DispatchWindowResized(current_window_);
        }
    });

    wsi::WindowManager::Instance().SetCloseCallback([this]() {
        input::GamepadManager::Instance().SetInputQueue(nullptr);
        if (!is_destroyed_.exchange(true)) {
            DispatchPause();
            DispatchStop();
            if (current_window_) {
                DispatchWindowDestroyed(current_window_);
            }
            DispatchInputQueueDestroyed(reinterpret_cast<AInputQueue *>(&input_queue_));
            DispatchDestroy();
        }
    });
}

} // namespace portage::package
