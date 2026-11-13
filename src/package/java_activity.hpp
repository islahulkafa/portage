#pragma once

#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"

#include <string>

namespace portage::package {

class JavaActivityRunner {
  public:
    static JavaActivityRunner &Instance() noexcept;

    JavaActivityRunner(const JavaActivityRunner &) = delete;
    JavaActivityRunner &operator=(const JavaActivityRunner &) = delete;

    bool Setup(const PackageInfo &pkg, ANativeWindow *window);
    bool LaunchAndRun(bool headless);

    [[nodiscard]] jobject GetApplicationObject() const noexcept { return app_obj_; }
    [[nodiscard]] jobject GetActivityObject() const noexcept { return act_obj_; }
    [[nodiscard]] ANativeWindow *GetWindow() const noexcept { return window_; }

  private:
    JavaActivityRunner() = default;
    ~JavaActivityRunner() = default;

    void InvokeActivityLifecycle(std::string_view method_name, std::string_view sig,
                                 jobject arg0 = nullptr);
    void DispatchSurfaceCallbacks(bool created);

    PackageInfo pkg_{};
    ANativeWindow *window_{nullptr};
    jobject app_obj_{nullptr};
    jobject act_obj_{nullptr};
    jobject intent_obj_{nullptr};
    std::string app_class_;
    std::string act_class_;
};

} // namespace portage::package
