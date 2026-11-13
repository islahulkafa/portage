#include "package/java_activity.hpp"

#include "package/asset_manager.hpp"
#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "wsi/window_manager.hpp"

#include <array>

namespace portage::package {

namespace {

std::string NormalizeDotToSlash(std::string_view name)
{
    std::string out(name);
    for (char &c : out) {
        if (c == '.') {
            c = '/';
        }
    }
    return out;
}

} // namespace

JavaActivityRunner &JavaActivityRunner::Instance() noexcept
{
    static JavaActivityRunner instance;
    return instance;
}

bool JavaActivityRunner::Setup(const PackageInfo &pkg, ANativeWindow *window)
{
    pkg_ = pkg;
    window_ = window;
    app_obj_ = nullptr;
    act_obj_ = nullptr;
    intent_obj_ = nullptr;

    std::string internal_path = "/data/data/" + pkg.manifest.package_name + "/files";
    std::string cache_path = "/data/data/" + pkg.manifest.package_name + "/cache";

    auto &jni = JniEnvironment::Instance();
    jni.Initialize(pkg.manifest.package_name, internal_path, cache_path);
    jni.SetActiveNativeWindow(window_);
    (void)AssetManagerBridge::Instance().CreateManager(pkg.archive);

    auto intent = std::make_shared<JniSyntheticObject>();
    intent->type = JniObjectType::Generic;
    intent->class_name = "android/content/Intent";
    intent->string_fields["action"] = "android.intent.action.MAIN";

    auto extras = std::make_shared<JniSyntheticObject>();
    extras->type = JniObjectType::Bundle;
    extras->class_name = "android/os/Bundle";
    intent->object_fields["extras"] = jni.RegisterObject(extras);

    intent_obj_ = jni.RegisterObject(intent);
    jni.SetIntentObject(intent_obj_);

    app_class_ = NormalizeDotToSlash(pkg.manifest.application_name);
    act_class_ = NormalizeDotToSlash(pkg.manifest.launcher_activity_name.empty()
                                         ? pkg.manifest.main_activity_name
                                         : pkg.manifest.launcher_activity_name);
    return !act_class_.empty();
}

void JavaActivityRunner::InvokeActivityLifecycle(std::string_view method_name, std::string_view sig,
                                                 jobject arg0)
{
    if (!act_obj_ || act_class_.empty()) {
        return;
    }
    auto &vm = DexVm::Instance();
    auto resolved = vm.ResolveMethod(act_class_, method_name, sig, true);
    if (!resolved.has_value()) {
        return;
    }
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    if (sig == "()V") {
        std::array<uint64_t, 1> args = {reinterpret_cast<uintptr_t>(act_obj_)};
        (void)vm.ExecuteMethod(resolved->dex, resolved->method, args, env);
    } else {
        std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(act_obj_),
                                        reinterpret_cast<uintptr_t>(arg0)};
        (void)vm.ExecuteMethod(resolved->dex, resolved->method, args, env);
    }
}

void JavaActivityRunner::DispatchSurfaceCallbacks(bool created)
{
    if (!act_obj_) {
        return;
    }
    auto &jni = JniEnvironment::Instance();
    auto &vm = DexVm::Instance();
    JNIEnv *env = jni.GetJNIEnv();

    auto act_syn = jni.LookupObject(act_obj_);
    if (!act_syn) {
        return;
    }

    jobject content_view = nullptr;
    if (auto it = act_syn->object_fields.find("contentView"); it != act_syn->object_fields.end()) {
        content_view = it->second;
    }
    if (!content_view) {
        return;
    }

    auto view_syn = jni.LookupObject(content_view);
    if (!view_syn) {
        return;
    }

    jobject holder_obj = nullptr;
    if (auto it = view_syn->object_fields.find("holder"); it != view_syn->object_fields.end()) {
        holder_obj = it->second;
    }
    if (!holder_obj) {
        return;
    }

    auto holder_syn = jni.LookupObject(holder_obj);
    if (!holder_syn) {
        return;
    }

    int32_t width = window_ && window_->width > 0 ? window_->width : jni.GetDisplayWidth();
    int32_t height = window_ && window_->height > 0 ? window_->height : jni.GetDisplayHeight();
    int32_t format = window_ && window_->format > 0 ? window_->format : 1;

    for (jobject cb : holder_syn->object_data) {
        auto cb_syn = jni.LookupObject(cb);
        if (!cb_syn || cb_syn->class_name.empty()) {
            continue;
        }
        if (created) {
            if (auto m = vm.ResolveMethod(cb_syn->class_name, "surfaceCreated",
                                          "(Landroid/view/SurfaceHolder;)V", true)) {
                std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(cb),
                                                reinterpret_cast<uintptr_t>(holder_obj)};
                (void)vm.ExecuteMethod(m->dex, m->method, args, env);
            }
            if (auto m = vm.ResolveMethod(cb_syn->class_name, "surfaceChanged",
                                          "(Landroid/view/SurfaceHolder;III)V", true)) {
                std::array<uint64_t, 5> args = {
                    reinterpret_cast<uintptr_t>(cb), reinterpret_cast<uintptr_t>(holder_obj),
                    static_cast<uint64_t>(format), static_cast<uint64_t>(width),
                    static_cast<uint64_t>(height)};
                (void)vm.ExecuteMethod(m->dex, m->method, args, env);
            }
        } else {
            if (auto m = vm.ResolveMethod(cb_syn->class_name, "surfaceDestroyed",
                                          "(Landroid/view/SurfaceHolder;)V", true)) {
                std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(cb),
                                                reinterpret_cast<uintptr_t>(holder_obj)};
                (void)vm.ExecuteMethod(m->dex, m->method, args, env);
            }
        }
    }

    if (created) {
        if (auto rit = view_syn->object_fields.find("renderer");
            rit != view_syn->object_fields.end() && rit->second != nullptr) {
            jobject renderer = rit->second;
            auto ren_syn = jni.LookupObject(renderer);
            if (ren_syn && !ren_syn->class_name.empty()) {
                if (auto m = vm.ResolveMethod(ren_syn->class_name, "onSurfaceCreated",
                                              "(Ljavax/microedition/khronos/opengles/GL10;"
                                              "Ljavax/microedition/khronos/egl/EGLConfig;)V",
                                              true)) {
                    std::array<uint64_t, 3> args = {reinterpret_cast<uintptr_t>(renderer), 0, 0};
                    (void)vm.ExecuteMethod(m->dex, m->method, args, env);
                }
                if (auto m =
                        vm.ResolveMethod(ren_syn->class_name, "onSurfaceChanged",
                                         "(Ljavax/microedition/khronos/opengles/GL10;II)V", true)) {
                    std::array<uint64_t, 4> args = {reinterpret_cast<uintptr_t>(renderer), 0,
                                                    static_cast<uint64_t>(width),
                                                    static_cast<uint64_t>(height)};
                    (void)vm.ExecuteMethod(m->dex, m->method, args, env);
                }
                if (auto m =
                        vm.ResolveMethod(ren_syn->class_name, "onDrawFrame",
                                         "(Ljavax/microedition/khronos/opengles/GL10;)V", true)) {
                    std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(renderer), 0};
                    (void)vm.ExecuteMethod(m->dex, m->method, args, env);
                }
            }
        }
    }
}

bool JavaActivityRunner::LaunchAndRun(bool headless)
{
    auto &vm = DexVm::Instance();
    auto &jni = JniEnvironment::Instance();
    JNIEnv *env = jni.GetJNIEnv();

    if (act_class_.empty() || !vm.HasClass(act_class_)) {
        return false;
    }

    if (!app_class_.empty() && vm.HasClass(app_class_)) {
        vm.EnsureClassInitialized(app_class_, env);
        auto ctor = vm.ResolveMethod(app_class_, "<init>", "()V", false);
        app_obj_ = vm.NewObject(app_class_, ctor ? ctor->method : nullptr, {}, env);
        if (auto attach = vm.ResolveMethod(app_class_, "attachBaseContext",
                                           "(Landroid/content/Context;)V", true)) {
            std::array<uint64_t, 2> args = {reinterpret_cast<uintptr_t>(app_obj_),
                                            reinterpret_cast<uintptr_t>(jni.GetContextObject())};
            (void)vm.ExecuteMethod(attach->dex, attach->method, args, env);
        }
        if (auto on_create = vm.ResolveMethod(app_class_, "onCreate", "()V", true)) {
            std::array<uint64_t, 1> args = {reinterpret_cast<uintptr_t>(app_obj_)};
            (void)vm.ExecuteMethod(on_create->dex, on_create->method, args, env);
        }
    } else {
        auto app = std::make_shared<JniSyntheticObject>();
        app->type = JniObjectType::Generic;
        app->class_name = "android/app/Application";
        app_obj_ = jni.RegisterObject(app);
    }
    jni.SetApplicationObject(app_obj_);

    vm.EnsureClassInitialized(act_class_, env);
    auto act_ctor = vm.ResolveMethod(act_class_, "<init>", "()V", false);
    act_obj_ = vm.NewObject(act_class_, act_ctor ? act_ctor->method : nullptr, {}, env);
    if (!act_obj_) {
        return false;
    }
    if (auto syn = jni.LookupObject(act_obj_)) {
        syn->object_fields["intent"] = intent_obj_;
        syn->object_fields["application"] = app_obj_;
    }
    jni.SetActiveActivityObject(act_obj_);

    InvokeActivityLifecycle("onCreate", "(Landroid/os/Bundle;)V", nullptr);
    InvokeActivityLifecycle("onStart", "()V");
    InvokeActivityLifecycle("onResume", "()V");
    DispatchSurfaceCallbacks(true);

    vm.DrainMainLooper(256, env);

    if (!headless && !vm.IsMainLooperQuit()) {
        wsi::WindowManager::Instance().WaitUntilClosed();
        wsi::WindowManager::Instance().DestroyWindow();
        wsi::WindowManager::Instance().ClearCallbacks();
    }

    DispatchSurfaceCallbacks(false);
    InvokeActivityLifecycle("onPause", "()V");
    InvokeActivityLifecycle("onStop", "()V");
    InvokeActivityLifecycle("onDestroy", "()V");
    return true;
}

} // namespace portage::package
