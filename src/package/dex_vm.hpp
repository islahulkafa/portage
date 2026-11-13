#pragma once

#include "package/dex_file.hpp"
#include "package/jni_env.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace portage::package {

struct DexValue {
    uint64_t raw{0};
    char kind{'V'};
};

struct LooperTask {
    uint64_t execute_at_ms{0};
    jobject target{nullptr};
    jobject runnable{nullptr};
    jobject message{nullptr};
    int32_t what{0};
    uint64_t seq{0};
};

class DexVm {
  public:
    static DexVm &Instance() noexcept;

    bool LoadDex(std::span<const uint8_t> data);
    void Reset();

    [[nodiscard]] bool HasClass(std::string_view class_name) const;
    [[nodiscard]] const DexClassDefResolved *FindClassDef(std::string_view class_name) const;
    [[nodiscard]] bool IsInstanceOf(jobject obj, std::string_view target_class) const;

    struct ResolvedMethod {
        const DexFile *dex{nullptr};
        const DexMethodDef *method{nullptr};
    };

    [[nodiscard]] std::optional<ResolvedMethod> ResolveMethod(std::string_view class_name,
                                                              std::string_view method_name,
                                                              std::string_view signature = "",
                                                              bool search_super = true) const;

    [[nodiscard]] bool HasField(std::string_view class_name, std::string_view field_name) const;

    void EnsureClassInitialized(std::string_view class_descriptor, JNIEnv *env = nullptr);

    jobject NewObject(std::string_view class_name, const DexMethodDef *ctor,
                      std::span<const uint64_t> ctor_args, JNIEnv *env = nullptr);

    DexValue ExecuteMethod(const DexFile *dex, const DexMethodDef *method,
                           std::span<const uint64_t> raw_args, JNIEnv *env = nullptr);

    [[nodiscard]] uint64_t GetStaticField(std::string_view class_name, std::string_view field_name);
    void SetStaticField(std::string_view class_name, std::string_view field_name, uint64_t val);

    [[nodiscard]] bool GetInstanceField(jobject obj, std::string_view field_name,
                                        std::string_view type_desc, uint64_t &out_val) const;
    bool SetInstanceField(jobject obj, std::string_view field_name, std::string_view type_desc,
                          uint64_t val);

    [[nodiscard]] static std::string MangleJniName(std::string_view class_slash,
                                                   std::string_view method_name,
                                                   std::string_view signature = "");

    [[nodiscard]] static std::vector<uint64_t>
    PackJniArgs(const std::vector<std::string> &param_types, std::span<const uint64_t> input_args);

    bool LoadNativeLibrary(std::string_view lib_name, JNIEnv *env = nullptr,
                           std::string *out_error = nullptr);
    void PostToMainLooper(jobject target_handler, jobject runnable, jobject message,
                          uint64_t delay_ms = 0);
    size_t DrainMainLooper(size_t max_iterations = 256, JNIEnv *env = nullptr);
    void QuitMainLooper();
    [[nodiscard]] bool IsMainLooperQuit() const;
    [[nodiscard]] jobject GetMainLooperObject();
    [[nodiscard]] jobject GetMainHandlerObject();

  private:
    DexVm() = default;
    ~DexVm() = default;

    [[nodiscard]] const DexClassDefResolved *
    FindClassUnlocked(std::string_view name, const DexFile **out_dex = nullptr) const;
    [[nodiscard]] std::optional<ResolvedMethod> ResolveMethodUnlocked(std::string_view class_name,
                                                                      std::string_view method_name,
                                                                      std::string_view signature,
                                                                      bool search_super) const;
    [[nodiscard]] bool IsSubclassUnlocked(std::string_view sub_desc,
                                          std::string_view super_desc) const;

    DexValue InvokeTarget(const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                          bool is_static, bool is_super, bool is_direct,
                          std::string_view caller_class, JNIEnv *env, jobject &out_exception);
    DexValue InvokeIntrinsic(const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled);
    DexValue InvokeNativeMethod(const DexMethodDef &method, std::span<const uint64_t> raw_args,
                                JNIEnv *env);

    jobject AllocateArray(std::string_view array_desc, size_t length);

    mutable std::recursive_mutex mutex_;
    std::condition_variable_any init_cv_;
    std::vector<std::shared_ptr<DexFile>> dex_files_;
    std::unordered_set<std::string> initialized_classes_;
    std::unordered_map<std::string, std::thread::id> initializing_classes_;
    std::unordered_map<std::string, uint64_t> static_fields_;
    std::unordered_set<void *> jni_onload_done_;
    jobject main_looper_obj_{nullptr};
    jobject main_handler_obj_{nullptr};
    jobject runtime_obj_{nullptr};
    bool main_looper_quit_{false};
    uint64_t next_looper_seq_{1};
    std::vector<LooperTask> looper_queue_;
};

} // namespace portage::package
