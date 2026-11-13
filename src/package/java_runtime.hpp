#pragma once

#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "package/jni_env.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portage::package {

namespace java_rt {

inline uint64_t ReadWideArg(std::span<const uint64_t> regs, size_t idx) noexcept
{
    if (idx + 1 >= regs.size()) {
        return 0;
    }
    uint64_t lo = regs[idx] & 0xFFFFFFFFULL;
    uint64_t hi = regs[idx + 1] & 0xFFFFFFFFULL;
    return lo | (hi << 32);
}

inline float BitsToFloat(uint64_t raw) noexcept
{
    uint32_t bits = static_cast<uint32_t>(raw);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(float));
    return f;
}

inline uint64_t FloatToBits(float f) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(float));
    return static_cast<uint64_t>(bits);
}

inline double BitsToDouble(uint64_t raw) noexcept
{
    double d = 0.0;
    std::memcpy(&d, &raw, sizeof(double));
    return d;
}

inline uint64_t DoubleToBits(double d) noexcept
{
    uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof(double));
    return bits;
}

std::u16string Utf8ToUtf16(std::string_view utf8);
std::string Utf16ToUtf8(std::u16string_view utf16);
int32_t JavaStringHashCode(std::string_view utf8);
std::string FormatDoubleJava(double v);
std::string FormatFloatJava(float v);
DexValue HandleStringInvocation(DexVm &vm, const DexMethodRef &ref,
                                std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                                jobject &out_exception, bool &handled);

bool TryUnwrapInt(jobject obj, int32_t &out);
bool TryUnwrapLong(jobject obj, int64_t &out);
bool TryUnwrapFloat(jobject obj, float &out);
bool TryUnwrapDouble(jobject obj, double &out);
bool TryUnwrapBool(jobject obj, bool &out);
bool TryUnwrapChar(jobject obj, uint16_t &out);
jobject BoxInt(int32_t v);
jobject BoxLong(int64_t v);
jobject BoxDouble(double v);
jobject BoxFloat(float v);
jobject BoxBool(bool v);

std::string NormalizeCharsetName(std::string_view raw);
std::vector<uint8_t> EncodeStringWithCharset(std::string_view utf8, std::string_view charset);
std::string DecodeBytesWithCharset(std::span<const uint8_t> bytes, std::string_view charset);

bool RegexMatches(std::string_view input, std::string_view pattern, jobject &out_exception);
std::string RegexReplace(std::string_view input, std::string_view pattern,
                         std::string_view replacement, bool first_only, jobject &out_exception);
std::vector<std::string> RegexSplit(std::string_view input, std::string_view pattern, int32_t limit,
                                    jobject &out_exception);

} // namespace java_rt

class JavaRuntime {
  public:
    using IntrinsicFn = DexValue (*)(DexVm &vm, const DexMethodRef &ref,
                                     std::span<const uint64_t> arg_regs, bool is_static,
                                     JNIEnv *env, jobject &out_exception, bool &handled);
    using StaticFieldFn = uint64_t (*)();

    struct ThreadRecord;

    static JavaRuntime &Instance() noexcept;

    void RegisterClass(std::string_view class_slash, IntrinsicFn fn,
                       std::initializer_list<std::string_view> parents = {});
    void RegisterHierarchy(std::string_view class_slash,
                           std::initializer_list<std::string_view> parents);
    void RegisterStaticField(std::string_view class_slash, std::string_view field_name,
                             StaticFieldFn fn);

    DexValue Invoke(DexVm &vm, std::string_view runtime_class_slash, const DexMethodRef &ref,
                    std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                    jobject &out_exception, bool &handled) const;

    [[nodiscard]] bool HasHostClass(std::string_view class_slash) const;
    [[nodiscard]] bool IsAssignableFrom(std::string_view sub_slash,
                                        std::string_view target_slash) const;
    [[nodiscard]] bool TryGetStaticField(std::string_view class_desc, std::string_view field_name,
                                         uint64_t &out_val) const;

    void MonitorEnter(jobject obj);
    void MonitorExit(jobject obj);
    void ObjectWait(jobject obj, int64_t timeout_ms, jobject &out_exception);
    void ObjectNotify(jobject obj, bool all, jobject &out_exception);

    void RegisterManagedThread(jobject thread_obj, std::thread t);
    void PrepareManagedThread(jobject thread_obj);
    void MarkThreadFinished(jobject thread_obj);
    void JoinThreadObject(jobject thread_obj, int64_t timeout_ms, jobject &out_exception);
    void InterruptThreadObject(jobject thread_obj);
    [[nodiscard]] bool CheckAndConsumeInterrupt();
    [[nodiscard]] bool IsThreadInterrupted(jobject thread_obj) const;
    [[nodiscard]] bool IsThreadAlive(jobject thread_obj) const;
    void SetCurrentJavaThread(jobject thread_obj);
    [[nodiscard]] jobject GetCurrentJavaThread();
    void SleepInterruptibly(int64_t timeout_ms, jobject &out_exception);
    void RegisterExecutorCleanup(std::function<void()> cleanup);
    void JoinAllNonDaemonThreads();
    void Reset();

    jobject GetClassObject(std::string_view class_slash);
    [[nodiscard]] static std::string ExtractClassSlash(jobject class_or_str);

    jobject CreateMethodHandleObject(const DexFile *dex, const DexMethodHandleDef &mh);
    jobject CreateMethodTypeObject(const DexProtoRef &proto);
    DexValue InvokeCustomCallSite(DexVm &vm, const DexFile *dex, uint32_t call_site_idx,
                                  std::span<const uint64_t> arg_regs, JNIEnv *env,
                                  jobject &out_exception);
    DexValue InvokePolymorphic(DexVm &vm, jobject mh_obj, const DexProtoRef &call_proto,
                               std::span<const uint64_t> arg_regs, JNIEnv *env,
                               jobject &out_exception);
    DexValue TryInvokeLambdaProxy(DexVm &vm, jobject proxy_obj, const DexMethodRef &ref,
                                  std::span<const uint64_t> arg_regs, JNIEnv *env,
                                  jobject &out_exception, bool &handled);
    [[nodiscard]] bool IsLambdaProxyInstanceOf(jobject obj, std::string_view target_slash) const;

    static DexValue CallVirtual(DexVm &vm, jobject obj, std::string_view method_name,
                                std::string_view sig, std::span<const uint64_t> extra_args,
                                JNIEnv *env, jobject &out_exception, bool *out_found = nullptr);
    static jobject CreateException(std::string_view class_slash, std::string_view message);
    static int32_t ComputeObjectHashCode(DexVm &vm, jobject obj, JNIEnv *env,
                                         jobject &out_exception);
    static bool ComputeObjectEquals(DexVm &vm, jobject a, jobject b, JNIEnv *env,
                                    jobject &out_exception);
    static int32_t ComputeObjectCompare(DexVm &vm, jobject comparator, jobject a, jobject b,
                                        JNIEnv *env, jobject &out_exception);
    static std::string ObjectToString(DexVm &vm, jobject obj, JNIEnv *env, jobject &out_exception);

  private:
    JavaRuntime();
    ~JavaRuntime();

    std::unordered_map<std::string_view, IntrinsicFn> handlers_;
    std::unordered_map<std::string_view, std::vector<std::string_view>> parents_;
    std::unordered_map<std::string, StaticFieldFn> static_fields_;

    mutable std::mutex thread_mutex_;
    std::unordered_map<jobject, std::shared_ptr<ThreadRecord>> threads_;
    std::vector<std::function<void()>> executor_cleanups_;

    mutable std::mutex class_mutex_;
    std::unordered_map<std::string, jobject> class_objects_;
    uint64_t class_epoch_{0};
};

void RegisterStringIntrinsics(JavaRuntime &rt);
void RegisterBoxingIntrinsics(JavaRuntime &rt);
void RegisterUtilIntrinsics(JavaRuntime &rt);
void RegisterConcurrentIntrinsics(JavaRuntime &rt);
void RegisterReflectIntrinsics(JavaRuntime &rt);
void RegisterNioIntrinsics(JavaRuntime &rt);
void RegisterIoRegexIntrinsics(JavaRuntime &rt);

} // namespace portage::package
