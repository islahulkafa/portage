#pragma once

#include "asset_manager.hpp"
#include "jni_env.hpp"
#include "ndk/input_queue.hpp"
#include "package_manager.hpp"
#include "wsi/window_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct ANativeWindow;
struct AInputQueue;
struct ARect;

namespace portage::package {

using ::AInputQueue;
using ::ANativeWindow;
using ::ARect;

struct ANativeActivity;

struct ANativeActivityCallbacks {
    void(__attribute__((sysv_abi)) * onStart)(ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onResume)(ANativeActivity *activity);
    void *(__attribute__((sysv_abi)) * onSaveInstanceState)(ANativeActivity *activity,
                                                            size_t *outSize);
    void(__attribute__((sysv_abi)) * onPause)(ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onStop)(ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onDestroy)(ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onWindowFocusChanged)(ANativeActivity *activity, int hasFocus);
    void(__attribute__((sysv_abi)) * onNativeWindowCreated)(ANativeActivity *activity,
                                                            ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowResized)(ANativeActivity *activity,
                                                            ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowRedrawNeeded)(ANativeActivity *activity,
                                                                 ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onNativeWindowDestroyed)(ANativeActivity *activity,
                                                              ANativeWindow *window);
    void(__attribute__((sysv_abi)) * onInputQueueCreated)(ANativeActivity *activity,
                                                          AInputQueue *queue);
    void(__attribute__((sysv_abi)) * onInputQueueDestroyed)(ANativeActivity *activity,
                                                            AInputQueue *queue);
    void(__attribute__((sysv_abi)) * onContentRectChanged)(ANativeActivity *activity,
                                                           const ARect *rect);
    void(__attribute__((sysv_abi)) * onConfigurationChanged)(ANativeActivity *activity);
    void(__attribute__((sysv_abi)) * onLowMemory)(ANativeActivity *activity);
};

struct ANativeActivity {
    ANativeActivityCallbacks *callbacks;
    JavaVM *vm;
    JNIEnv *env;
    jobject clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int32_t sdkVersion;
    void *instance;
    AAssetManager *assetManager;
    const char *obbPath;
};

using ANativeActivity_createFunc = void(__attribute__((sysv_abi)) *)(ANativeActivity *activity,
                                                                     void *savedState,
                                                                     size_t savedStateSize);
using JniOnLoadFunc = jint(__attribute__((sysv_abi)) *)(JavaVM *vm, void *reserved);

class NativeActivityRunner {
  public:
    static NativeActivityRunner &Instance() noexcept;

    bool SetupActivity(const PackageInfo &pkg, ANativeWindow *window = nullptr);

    void DispatchStart();
    void DispatchResume();
    void DispatchWindowCreated(ANativeWindow *window);
    void DispatchWindowFocus(bool has_focus);
    void DispatchWindowResized(ANativeWindow *window);
    void DispatchWindowDestroyed(ANativeWindow *window);
    void DispatchInputQueueCreated(AInputQueue *queue);
    void DispatchInputQueueDestroyed(AInputQueue *queue);
    void DispatchConfigurationChanged();
    void DispatchLowMemory();
    void DispatchPause();
    void DispatchStop();
    void DispatchDestroy();

    void ConnectInputQueueToWindow();

    [[nodiscard]] ANativeActivity *GetActivity() noexcept { return &activity_; }
    [[nodiscard]] ndk::InputQueueImpl &GetInputQueue() noexcept { return input_queue_; }

  private:
    NativeActivityRunner();
    ~NativeActivityRunner() = default;

    ANativeActivity activity_{};
    ANativeActivityCallbacks callbacks_{};

    std::string internal_path_{};
    std::string external_path_{};
    AAssetManager *asset_manager_{nullptr};
    ANativeWindow *current_window_{nullptr};
    ndk::InputQueueImpl input_queue_{};
    std::atomic<bool> is_destroyed_{false};
};

} // namespace portage::package
