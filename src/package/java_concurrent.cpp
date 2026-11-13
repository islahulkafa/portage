#include "package/java_runtime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace portage::package {

namespace {

std::atomic<int64_t> g_next_thread_id{10};

template <typename T>
std::shared_ptr<T> GetOrCreatePayload(jobject obj, std::string_view fallback_class)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return nullptr;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name.empty()) {
        syn->class_name = std::string(fallback_class);
    }
    if (!syn->runtime_payload) {
        syn->runtime_payload = std::make_shared<T>();
    }
    return std::static_pointer_cast<T>(syn->runtime_payload);
}

template <typename T> std::shared_ptr<T> GetPayload(jobject obj)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return nullptr;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    return std::static_pointer_cast<T>(syn->runtime_payload);
}

bool ReadJavaString(jobject obj, std::string &out)
{
    if (!obj) {
        return false;
    }
    if (const char *s = JniEnvironment::Instance().GetStringChars(obj)) {
        out = s;
        return true;
    }
    return false;
}

jobject MakeSyntheticObj(std::string_view cls, std::shared_ptr<void> payload)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = std::string(cls);
    syn->runtime_payload = std::move(payload);
    return JniEnvironment::Instance().RegisterObject(syn);
}

DexValue HandleObjectMonitors(DexVm &vm, const DexMethodRef &ref,
                              std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                              jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "wait") {
        int64_t ms = 0;
        if (arg_regs.size() >= 3 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            ms = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
            if (ms < 0) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "timeout value is negative");
                handled = true;
                return {0, 'V'};
            }
            if (arg_regs.size() >= 4 && ref.param_types.size() >= 2 && ref.param_types[1] == "I") {
                int32_t nanos = static_cast<int32_t>(arg_regs[3]);
                if (nanos < 0 || nanos > 999999) {
                    out_exception = JavaRuntime::CreateException(
                        "java/lang/IllegalArgumentException", "nanosecond timeout out of range");
                    handled = true;
                    return {0, 'V'};
                }
                if (ms == 0 && nanos > 0) {
                    ms = 1;
                }
            }
        }
        handled = true;
        JavaRuntime::Instance().ObjectWait(self, ms, out_exception);
        return {0, 'V'};
    }
    if (ref.name == "notify") {
        handled = true;
        JavaRuntime::Instance().ObjectNotify(self, false, out_exception);
        return {0, 'V'};
    }
    if (ref.name == "notifyAll") {
        handled = true;
        JavaRuntime::Instance().ObjectNotify(self, true, out_exception);
        return {0, 'V'};
    }
    return {0, 'V'};
}

DexValue HandleThread(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    auto &rt = JavaRuntime::Instance();

    if (is_static) {
        if (ref.name == "currentThread") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(rt.GetCurrentJavaThread()), 'L'};
        }
        if (ref.name == "sleep" && !arg_regs.empty()) {
            handled = true;
            int64_t ms = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0));
            if (arg_regs.size() >= 3 && ref.param_types.size() >= 2 && ref.param_types[1] == "I") {
                int32_t nanos = static_cast<int32_t>(arg_regs[2]);
                if (nanos < 0 || nanos > 999999) {
                    out_exception = JavaRuntime::CreateException(
                        "java/lang/IllegalArgumentException", "nanosecond timeout out of range");
                    return {0, 'V'};
                }
                if (ms == 0 && nanos > 0) {
                    ms = 1;
                }
            }
            rt.SleepInterruptibly(ms, out_exception);
            return {0, 'V'};
        }
        if (ref.name == "interrupted") {
            handled = true;
            return {rt.CheckAndConsumeInterrupt() ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "yield") {
            handled = true;
            std::this_thread::yield();
            return {0, 'V'};
        }
        return {0, 'V'};
    }

    if (arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "<init>") {
        handled = true;
        jobject runnable = nullptr;
        std::string thread_name;
        size_t idx = 1;
        for (const auto &pt : ref.param_types) {
            if (idx >= arg_regs.size()) {
                break;
            }
            if (pt == "Ljava/lang/Runnable;") {
                runnable = reinterpret_cast<jobject>(arg_regs[idx]);
            } else if (pt == "Ljava/lang/String;") {
                ReadJavaString(reinterpret_cast<jobject>(arg_regs[idx]), thread_name);
            }
            ++idx;
        }
        int64_t tid = g_next_thread_id.fetch_add(1);
        if (thread_name.empty()) {
            thread_name = "Thread-" + std::to_string(tid);
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (syn->class_name.empty()) {
            syn->class_name = "java/lang/Thread";
        }
        if (runnable) {
            syn->object_fields["target"] = runnable;
        }
        syn->string_fields["name"] = std::move(thread_name);
        syn->long_fields["tid"] = tid;
        if (!syn->int_fields.count("priority")) {
            syn->int_fields["priority"] = 5;
        }
        return {0, 'V'};
    }

    if (ref.name == "start") {
        handled = true;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (syn->bool_fields["started"] != 0) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/IllegalThreadStateException", "Thread already started");
                return {0, 'V'};
            }
            syn->bool_fields["started"] = 1;
        }
        rt.PrepareManagedThread(self);
        std::thread worker([&vm, self, env]() {
            JavaRuntime::Instance().SetCurrentJavaThread(self);
            jobject thread_ex = nullptr;
            bool found = false;
            JavaRuntime::CallVirtual(vm, self, "run", "()V", {}, env, thread_ex, &found);
            JniEnvironment::Instance().ClearPendingException();
            JavaRuntime::Instance().MarkThreadFinished(self);
        });
        rt.RegisterManagedThread(self, std::move(worker));
        return {0, 'V'};
    }

    if (ref.name == "run") {
        handled = true;
        jobject target = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            target = syn->object_fields["target"];
        }
        if (target) {
            bool found = false;
            JavaRuntime::CallVirtual(vm, target, "run", "()V", {}, env, out_exception, &found);
        }
        return {0, 'V'};
    }

    if (ref.name == "join") {
        handled = true;
        int64_t ms = 0;
        if (arg_regs.size() >= 3 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            ms = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
            if (ms < 0) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "timeout value is negative");
                return {0, 'V'};
            }
        }
        rt.JoinThreadObject(self, ms, out_exception);
        return {0, 'V'};
    }

    if (ref.name == "interrupt") {
        handled = true;
        rt.InterruptThreadObject(self);
        return {0, 'V'};
    }
    if (ref.name == "isInterrupted") {
        handled = true;
        return {rt.IsThreadInterrupted(self) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isAlive") {
        handled = true;
        return {rt.IsThreadAlive(self) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "getName") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        auto it = syn->string_fields.find("name");
        std::string name =
            (it != syn->string_fields.end() && !it->second.empty()) ? it->second : "Thread";
        return {reinterpret_cast<uintptr_t>(jni.CreateString(name)), 'L'};
    }
    if (ref.name == "setName" && arg_regs.size() >= 2) {
        std::string name;
        ReadJavaString(reinterpret_cast<jobject>(arg_regs[1]), name);
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->string_fields["name"] = std::move(name);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getId") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (syn->long_fields["tid"] == 0) {
            syn->long_fields["tid"] = g_next_thread_id.fetch_add(1);
        }
        return {static_cast<uint64_t>(syn->long_fields["tid"]), 'J'};
    }
    if (ref.name == "getPriority") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        auto it = syn->int_fields.find("priority");
        return {static_cast<uint64_t>(it != syn->int_fields.end() ? it->second : 5), 'I'};
    }
    if (ref.name == "setPriority" && arg_regs.size() >= 2) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->int_fields["priority"] = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "setDaemon" && arg_regs.size() >= 2) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->bool_fields["daemon"] = arg_regs[1] != 0 ? 1 : 0;
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "isDaemon") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {syn->bool_fields["daemon"] != 0 ? 1ULL : 0ULL, 'Z'};
    }
    return {0, 'V'};
}

template <typename T> struct AtomicNumberPayload {
    std::atomic<T> value{0};
};

template <typename T, char Kind>
DexValue HandleAtomicNumber(std::string_view cls_name, const DexMethodRef &ref,
                            std::span<const uint64_t> arg_regs, bool is_static, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto payload = GetOrCreatePayload<AtomicNumberPayload<T>>(self, cls_name);
    if (!payload) {
        return {0, 'V'};
    }

    constexpr size_t step = (Kind == 'J') ? 2 : 1;
    auto read_val = [&](size_t idx) -> T {
        if constexpr (Kind == 'J') {
            return static_cast<T>(java_rt::ReadWideArg(arg_regs, idx));
        } else {
            return static_cast<T>(arg_regs[idx]);
        }
    };
    auto to_raw = [](T v) -> uint64_t {
        if constexpr (Kind == 'J') {
            return static_cast<uint64_t>(v);
        } else {
            return static_cast<uint64_t>(static_cast<uint32_t>(v));
        }
    };

    if (ref.name == "<init>") {
        payload->value.store(arg_regs.size() > step ? read_val(1) : 0);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "get") {
        handled = true;
        return {to_raw(payload->value.load()), Kind};
    }
    if (ref.name == "set" || ref.name == "lazySet") {
        if (arg_regs.size() > step) {
            payload->value.store(read_val(1));
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getAndSet" && arg_regs.size() > step) {
        handled = true;
        return {to_raw(payload->value.exchange(read_val(1))), Kind};
    }
    if ((ref.name == "compareAndSet" || ref.name == "weakCompareAndSet") &&
        arg_regs.size() >= 1 + 2 * step) {
        T expected = read_val(1);
        T update = read_val(1 + step);
        handled = true;
        return {payload->value.compare_exchange_strong(expected, update) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "incrementAndGet") {
        handled = true;
        return {to_raw(payload->value.fetch_add(1) + 1), Kind};
    }
    if (ref.name == "getAndIncrement") {
        handled = true;
        return {to_raw(payload->value.fetch_add(1)), Kind};
    }
    if (ref.name == "decrementAndGet") {
        handled = true;
        return {to_raw(payload->value.fetch_sub(1) - 1), Kind};
    }
    if (ref.name == "getAndDecrement") {
        handled = true;
        return {to_raw(payload->value.fetch_sub(1)), Kind};
    }
    if (ref.name == "addAndGet" && arg_regs.size() > step) {
        T delta = read_val(1);
        handled = true;
        return {to_raw(payload->value.fetch_add(delta) + delta), Kind};
    }
    if (ref.name == "getAndAdd" && arg_regs.size() > step) {
        T delta = read_val(1);
        handled = true;
        return {to_raw(payload->value.fetch_add(delta)), Kind};
    }
    if (ref.name == "intValue") {
        handled = true;
        return {static_cast<uint64_t>(
                    static_cast<uint32_t>(static_cast<int32_t>(payload->value.load()))),
                'I'};
    }
    if (ref.name == "longValue") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<int64_t>(payload->value.load())), 'J'};
    }
    if (ref.name == "toString") {
        handled = true;
        jstring s = JniEnvironment::Instance().CreateString(std::to_string(payload->value.load()));
        return {reinterpret_cast<uintptr_t>(s), 'L'};
    }
    return {0, 'V'};
}

struct AtomicBoolPayload {
    std::atomic<bool> value{false};
};

DexValue HandleAtomicBoolean(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    (void)out_exception;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto payload =
        GetOrCreatePayload<AtomicBoolPayload>(self, "java/util/concurrent/atomic/AtomicBoolean");
    if (!payload) {
        return {0, 'V'};
    }
    if (ref.name == "<init>") {
        payload->value.store(arg_regs.size() >= 2 && arg_regs[1] != 0);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "get") {
        handled = true;
        return {payload->value.load() ? 1ULL : 0ULL, 'Z'};
    }
    if ((ref.name == "set" || ref.name == "lazySet") && arg_regs.size() >= 2) {
        payload->value.store(arg_regs[1] != 0);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getAndSet" && arg_regs.size() >= 2) {
        handled = true;
        return {payload->value.exchange(arg_regs[1] != 0) ? 1ULL : 0ULL, 'Z'};
    }
    if ((ref.name == "compareAndSet" || ref.name == "weakCompareAndSet") && arg_regs.size() >= 3) {
        bool exp = arg_regs[1] != 0;
        bool upd = arg_regs[2] != 0;
        handled = true;
        return {payload->value.compare_exchange_strong(exp, upd) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        jstring s =
            JniEnvironment::Instance().CreateString(payload->value.load() ? "true" : "false");
        return {reinterpret_cast<uintptr_t>(s), 'L'};
    }
    return {0, 'V'};
}

struct AtomicRefPayload {
    std::atomic<jobject> value{nullptr};
};

DexValue HandleAtomicReference(DexVm &vm, const DexMethodRef &ref,
                               std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                               jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto payload =
        GetOrCreatePayload<AtomicRefPayload>(self, "java/util/concurrent/atomic/AtomicReference");
    if (!payload) {
        return {0, 'V'};
    }
    if (ref.name == "<init>") {
        payload->value.store(arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1])
                                                  : nullptr);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "get") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(payload->value.load()), 'L'};
    }
    if ((ref.name == "set" || ref.name == "lazySet") && arg_regs.size() >= 2) {
        payload->value.store(reinterpret_cast<jobject>(arg_regs[1]));
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getAndSet" && arg_regs.size() >= 2) {
        handled = true;
        return {reinterpret_cast<uintptr_t>(
                    payload->value.exchange(reinterpret_cast<jobject>(arg_regs[1]))),
                'L'};
    }
    if ((ref.name == "compareAndSet" || ref.name == "weakCompareAndSet") && arg_regs.size() >= 3) {
        jobject exp = reinterpret_cast<jobject>(arg_regs[1]);
        jobject upd = reinterpret_cast<jobject>(arg_regs[2]);
        handled = true;
        return {payload->value.compare_exchange_strong(exp, upd) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        std::string s = JavaRuntime::ObjectToString(vm, payload->value.load(), env, out_exception);
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(s)), 'L'};
    }
    return {0, 'V'};
}

int64_t TimeUnitFactorNanos(std::string_view unit_name)
{
    if (unit_name == "NANOSECONDS") {
        return 1LL;
    }
    if (unit_name == "MICROSECONDS") {
        return 1000LL;
    }
    if (unit_name == "MILLISECONDS") {
        return 1000000LL;
    }
    if (unit_name == "SECONDS") {
        return 1000000000LL;
    }
    if (unit_name == "MINUTES") {
        return 60LL * 1000000000LL;
    }
    if (unit_name == "HOURS") {
        return 3600LL * 1000000000LL;
    }
    if (unit_name == "DAYS") {
        return 86400LL * 1000000000LL;
    }
    return 1000000LL;
}

std::string ReadTimeUnitName(jobject unit_obj)
{
    if (!unit_obj) {
        return "MILLISECONDS";
    }
    if (auto syn = JniEnvironment::Instance().LookupObject(unit_obj)) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        auto it = syn->string_fields.find("unit");
        if (it != syn->string_fields.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return "MILLISECONDS";
}

int64_t ToMillisFromUnit(int64_t amount, jobject unit_obj)
{
    if (amount <= 0) {
        return 0;
    }
    int64_t factor = TimeUnitFactorNanos(ReadTimeUnitName(unit_obj));
    if (factor >= 1000000LL) {
        return amount * (factor / 1000000LL);
    }
    int64_t ns = amount * factor;
    return std::max<int64_t>(1, (ns + 999999LL) / 1000000LL);
}

std::mutex g_tu_mu;
uint64_t g_tu_epoch = 0;
std::unordered_map<std::string, jobject> g_tu_cache;

jobject GetTimeUnitSingleton(std::string_view unit)
{
    std::lock_guard<std::mutex> lk(g_tu_mu);
    uint64_t cur = JniEnvironment::Instance().GetResetEpoch();
    if (g_tu_epoch != cur) {
        g_tu_cache.clear();
        g_tu_epoch = cur;
    }
    std::string key(unit);
    if (auto it = g_tu_cache.find(key); it != g_tu_cache.end()) {
        return it->second;
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = "java/util/concurrent/TimeUnit";
    syn->string_fields["unit"] = key;
    jobject obj = JniEnvironment::Instance().RegisterObject(syn);
    g_tu_cache[key] = obj;
    return obj;
}

DexValue HandleTimeUnit(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                        bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    std::string my_unit = ReadTimeUnitName(self);
    int64_t my_factor = TimeUnitFactorNanos(my_unit);
    if (ref.name == "toNanos" && arg_regs.size() >= 3) {
        int64_t d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        handled = true;
        return {static_cast<uint64_t>(d * my_factor), 'J'};
    }
    if (ref.name == "toMicros" && arg_regs.size() >= 3) {
        int64_t d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        handled = true;
        return {static_cast<uint64_t>((d * my_factor) / 1000LL), 'J'};
    }
    if (ref.name == "toMillis" && arg_regs.size() >= 3) {
        int64_t d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        handled = true;
        return {static_cast<uint64_t>((d * my_factor) / 1000000LL), 'J'};
    }
    if (ref.name == "toSeconds" && arg_regs.size() >= 3) {
        int64_t d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        handled = true;
        return {static_cast<uint64_t>((d * my_factor) / 1000000000LL), 'J'};
    }
    if (ref.name == "convert" && arg_regs.size() >= 4) {
        int64_t src_d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        int64_t src_factor =
            TimeUnitFactorNanos(ReadTimeUnitName(reinterpret_cast<jobject>(arg_regs[3])));
        handled = true;
        return {static_cast<uint64_t>((src_d * src_factor) / my_factor), 'J'};
    }
    if (ref.name == "sleep" && arg_regs.size() >= 3) {
        int64_t d = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        handled = true;
        if (d > 0) {
            JavaRuntime::Instance().SleepInterruptibly(ToMillisFromUnit(d, self), out_exception);
        }
        return {0, 'V'};
    }
    if (ref.name == "name" || ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(my_unit)), 'L'};
    }
    return {0, 'V'};
}

struct ReentrantLockPayload {
    std::mutex mu;
    std::condition_variable cv;
    std::thread::id owner{};
    uint32_t holds{0};
};

struct ConditionPayload {
    std::shared_ptr<ReentrantLockPayload> lock_p;
    std::condition_variable cv;
    uint64_t signal_epoch{0};
    uint32_t signal_permits{0};
};

DexValue HandleReentrantLock(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "<init>") {
        (void)GetOrCreatePayload<ReentrantLockPayload>(self,
                                                       "java/util/concurrent/locks/ReentrantLock");
        handled = true;
        return {0, 'V'};
    }
    auto lp =
        GetOrCreatePayload<ReentrantLockPayload>(self, "java/util/concurrent/locks/ReentrantLock");
    if (!lp) {
        return {0, 'V'};
    }
    const auto self_tid = std::this_thread::get_id();

    if (ref.name == "lock") {
        handled = true;
        std::unique_lock<std::mutex> lk(lp->mu);
        lp->cv.wait(lk, [&]() { return lp->holds == 0 || lp->owner == self_tid; });
        lp->owner = self_tid;
        ++lp->holds;
        return {0, 'V'};
    }
    if (ref.name == "lockInterruptibly") {
        handled = true;
        if (JavaRuntime::Instance().CheckAndConsumeInterrupt()) {
            out_exception =
                JavaRuntime::CreateException("java/lang/InterruptedException", "interrupted");
            return {0, 'V'};
        }
        std::unique_lock<std::mutex> lk(lp->mu);
        lp->cv.wait(lk, [&]() { return lp->holds == 0 || lp->owner == self_tid; });
        lp->owner = self_tid;
        ++lp->holds;
        return {0, 'V'};
    }
    if (ref.name == "tryLock") {
        handled = true;
        if (arg_regs.size() >= 4 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            int64_t ms = ToMillisFromUnit(static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1)),
                                          reinterpret_cast<jobject>(arg_regs[3]));
            std::unique_lock<std::mutex> lk(lp->mu);
            bool ok = lp->cv.wait_for(lk, std::chrono::milliseconds(ms),
                                      [&]() { return lp->holds == 0 || lp->owner == self_tid; });
            if (ok) {
                lp->owner = self_tid;
                ++lp->holds;
            }
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        std::lock_guard<std::mutex> lk(lp->mu);
        if (lp->holds == 0 || lp->owner == self_tid) {
            lp->owner = self_tid;
            ++lp->holds;
            return {1ULL, 'Z'};
        }
        return {0ULL, 'Z'};
    }
    if (ref.name == "unlock") {
        handled = true;
        std::lock_guard<std::mutex> lk(lp->mu);
        if (lp->owner != self_tid || lp->holds == 0) {
            out_exception = JavaRuntime::CreateException("java/lang/IllegalMonitorStateException",
                                                         "Unlock by non-owner");
            return {0, 'V'};
        }
        if (--lp->holds == 0) {
            lp->owner = std::thread::id{};
            lp->cv.notify_one();
        }
        return {0, 'V'};
    }
    if (ref.name == "isHeldByCurrentThread") {
        std::lock_guard<std::mutex> lk(lp->mu);
        handled = true;
        return {(lp->holds > 0 && lp->owner == self_tid) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "getHoldCount") {
        std::lock_guard<std::mutex> lk(lp->mu);
        handled = true;
        return {static_cast<uint64_t>(lp->owner == self_tid ? lp->holds : 0), 'I'};
    }
    if (ref.name == "isLocked") {
        std::lock_guard<std::mutex> lk(lp->mu);
        handled = true;
        return {lp->holds > 0 ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "newCondition") {
        handled = true;
        auto cp = std::make_shared<ConditionPayload>();
        cp->lock_p = lp;
        return {reinterpret_cast<uintptr_t>(
                    MakeSyntheticObj("java/util/concurrent/locks/Condition", cp)),
                'L'};
    }
    return {0, 'V'};
}

DexValue HandleCondition(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                         bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto cp = GetPayload<ConditionPayload>(reinterpret_cast<jobject>(arg_regs[0]));
    if (!cp || !cp->lock_p) {
        return {0, 'V'};
    }
    auto &lp = *cp->lock_p;
    const auto self_tid = std::this_thread::get_id();

    if (ref.name == "signal" || ref.name == "signalAll") {
        handled = true;
        std::lock_guard<std::mutex> lk(lp.mu);
        if (lp.owner != self_tid || lp.holds == 0) {
            out_exception = JavaRuntime::CreateException("java/lang/IllegalMonitorStateException",
                                                         "Condition not held");
            return {0, 'V'};
        }
        if (ref.name == "signalAll") {
            ++cp->signal_epoch;
            cp->cv.notify_all();
        } else {
            ++cp->signal_permits;
            cp->cv.notify_one();
        }
        return {0, 'V'};
    }
    if (ref.name == "await" || ref.name == "awaitUninterruptibly") {
        handled = true;
        bool timed = (ref.name == "await" && arg_regs.size() >= 4 && !ref.param_types.empty() &&
                      ref.param_types[0] == "J");
        int64_t ms = timed
                         ? ToMillisFromUnit(static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1)),
                                            reinterpret_cast<jobject>(arg_regs[3]))
                         : 0;
        std::unique_lock<std::mutex> lk(lp.mu);
        if (lp.owner != self_tid || lp.holds == 0) {
            out_exception = JavaRuntime::CreateException("java/lang/IllegalMonitorStateException",
                                                         "Condition not held");
            return {0, timed ? 'Z' : 'V'};
        }
        uint32_t saved_holds = lp.holds;
        uint64_t start_epoch = cp->signal_epoch;
        lp.holds = 0;
        lp.owner = std::thread::id{};
        lp.cv.notify_one();

        auto pred = [&]() {
            if (cp->signal_epoch != start_epoch) {
                return true;
            }
            if (cp->signal_permits > 0) {
                --cp->signal_permits;
                return true;
            }
            return false;
        };
        bool woken = true;
        if (timed) {
            woken = cp->cv.wait_for(lk, std::chrono::milliseconds(ms), pred);
        } else {
            cp->cv.wait(lk, pred);
        }
        lp.cv.wait(lk, [&]() { return lp.holds == 0; });
        lp.owner = self_tid;
        lp.holds = saved_holds;
        if (timed) {
            return {woken ? 1ULL : 0ULL, 'Z'};
        }
        return {0, 'V'};
    }
    return {0, 'V'};
}

struct CountDownLatchPayload {
    std::mutex mu;
    std::condition_variable cv;
    int64_t count{0};
};

DexValue HandleCountDownLatch(DexVm &vm, const DexMethodRef &ref,
                              std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                              jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "<init>") {
        handled = true;
        int32_t count = arg_regs.size() >= 2 ? static_cast<int32_t>(arg_regs[1]) : 0;
        if (count < 0) {
            out_exception =
                JavaRuntime::CreateException("java/lang/IllegalArgumentException", "count < 0");
            return {0, 'V'};
        }
        auto p =
            GetOrCreatePayload<CountDownLatchPayload>(self, "java/util/concurrent/CountDownLatch");
        if (p) {
            std::lock_guard<std::mutex> lk(p->mu);
            p->count = count;
        }
        return {0, 'V'};
    }
    auto p = GetPayload<CountDownLatchPayload>(self);
    if (!p) {
        return {0, 'V'};
    }
    if (ref.name == "countDown") {
        handled = true;
        std::lock_guard<std::mutex> lk(p->mu);
        if (p->count > 0 && --p->count == 0) {
            p->cv.notify_all();
        }
        return {0, 'V'};
    }
    if (ref.name == "getCount") {
        handled = true;
        std::lock_guard<std::mutex> lk(p->mu);
        return {static_cast<uint64_t>(p->count), 'J'};
    }
    if (ref.name == "await") {
        handled = true;
        if (JavaRuntime::Instance().CheckAndConsumeInterrupt()) {
            out_exception =
                JavaRuntime::CreateException("java/lang/InterruptedException", "interrupted");
            return {0, !ref.param_types.empty() ? 'Z' : 'V'};
        }
        if (arg_regs.size() >= 4 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            int64_t ms = ToMillisFromUnit(static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1)),
                                          reinterpret_cast<jobject>(arg_regs[3]));
            std::unique_lock<std::mutex> lk(p->mu);
            bool ok =
                p->cv.wait_for(lk, std::chrono::milliseconds(ms), [&]() { return p->count == 0; });
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        std::unique_lock<std::mutex> lk(p->mu);
        p->cv.wait(lk, [&]() { return p->count == 0; });
        return {0, 'V'};
    }
    return {0, 'V'};
}

struct ConcMapPayload {
    std::mutex mu;
    std::vector<std::pair<jobject, jobject>> entries;
};

DexValue HandleConcurrentHashMap(DexVm &vm, const DexMethodRef &ref,
                                 std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                                 jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "<init>") {
        (void)GetOrCreatePayload<ConcMapPayload>(self, "java/util/concurrent/ConcurrentHashMap");
        handled = true;
        return {0, 'V'};
    }
    auto p = GetOrCreatePayload<ConcMapPayload>(self, "java/util/concurrent/ConcurrentHashMap");
    if (!p) {
        return {0, 'V'};
    }

    auto find_idx_snapshot = [&](jobject key) -> ptrdiff_t {
        std::vector<jobject> keys;
        {
            std::lock_guard<std::mutex> lk(p->mu);
            keys.reserve(p->entries.size());
            for (const auto &kv : p->entries) {
                keys.push_back(kv.first);
            }
        }
        for (size_t i = 0; i < keys.size(); ++i) {
            if (JavaRuntime::ComputeObjectEquals(vm, keys[i], key, env, out_exception)) {
                return static_cast<ptrdiff_t>(i);
            }
            if (out_exception) {
                return -1;
            }
        }
        return -1;
    };

    if (ref.name == "size") {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        return {static_cast<uint64_t>(p->entries.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        return {p->entries.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::mutex> lk(p->mu);
        p->entries.clear();
        handled = true;
        return {0, 'V'};
    }
    if ((ref.name == "put" || ref.name == "putIfAbsent") && arg_regs.size() >= 3) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        jobject v = reinterpret_cast<jobject>(arg_regs[2]);
        if (!k || !v) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "ConcurrentHashMap null key/value");
            return {0, 'L'};
        }
        std::lock_guard<std::mutex> lk(p->mu);
        for (auto &kv : p->entries) {
            if (JavaRuntime::ComputeObjectEquals(vm, kv.first, k, env, out_exception)) {
                jobject old = kv.second;
                if (ref.name == "put") {
                    kv.second = v;
                }
                return {reinterpret_cast<uintptr_t>(old), 'L'};
            }
            if (out_exception) {
                return {0, 'L'};
            }
        }
        p->entries.emplace_back(k, v);
        return {0, 'L'};
    }
    if ((ref.name == "get" || ref.name == "getOrDefault") && arg_regs.size() >= 2) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        jobject def_val = (ref.name == "getOrDefault" && arg_regs.size() >= 3)
                              ? reinterpret_cast<jobject>(arg_regs[2])
                              : nullptr;
        if (!k) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "ConcurrentHashMap null key");
            return {0, 'L'};
        }
        std::vector<std::pair<jobject, jobject>> snap;
        {
            std::lock_guard<std::mutex> lk(p->mu);
            snap = p->entries;
        }
        for (const auto &kv : snap) {
            if (JavaRuntime::ComputeObjectEquals(vm, kv.first, k, env, out_exception)) {
                return {reinterpret_cast<uintptr_t>(kv.second), 'L'};
            }
            if (out_exception) {
                return {0, 'L'};
            }
        }
        return {reinterpret_cast<uintptr_t>(def_val), 'L'};
    }
    if (ref.name == "containsKey" && arg_regs.size() >= 2) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        ptrdiff_t idx = k ? find_idx_snapshot(k) : -1;
        return {(!out_exception && idx >= 0) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "remove" && arg_regs.size() >= 2) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        std::lock_guard<std::mutex> lk(p->mu);
        for (size_t i = 0; i < p->entries.size(); ++i) {
            if (JavaRuntime::ComputeObjectEquals(vm, p->entries[i].first, k, env, out_exception)) {
                jobject old = p->entries[i].second;
                p->entries.erase(p->entries.begin() + static_cast<ptrdiff_t>(i));
                return {reinterpret_cast<uintptr_t>(old), 'L'};
            }
        }
        return {0, 'L'};
    }
    if (ref.name == "keySet" || ref.name == "values") {
        handled = true;
        std::vector<std::pair<jobject, jobject>> snap;
        {
            std::lock_guard<std::mutex> lk(p->mu);
            snap = p->entries;
        }
        jobject obj = MakeSyntheticObj(
            ref.name == "keySet" ? "java/util/HashSet" : "java/util/ArrayList", nullptr);
        DexMethodRef add_ref{};
        add_ref.name = "add";
        add_ref.return_type = "Z";
        add_ref.param_types = {"Ljava/lang/Object;"};
        for (const auto &kv : snap) {
            jobject item = (ref.name == "keySet") ? kv.first : kv.second;
            uint64_t a[2] = {reinterpret_cast<uintptr_t>(obj), reinterpret_cast<uintptr_t>(item)};
            bool ah = false;
            JavaRuntime::Instance().Invoke(
                vm, ref.name == "keySet" ? "java/util/HashSet" : "java/util/ArrayList", add_ref, a,
                false, env, out_exception, ah);
        }
        return {reinterpret_cast<uintptr_t>(obj), 'L'};
    }
    return {0, 'V'};
}

struct CowListPayload {
    std::mutex mu;
    std::vector<jobject> items;
};

DexValue HandleCopyOnWriteArrayList(DexVm &vm, const DexMethodRef &ref,
                                    std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                                    jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "<init>") {
        (void)GetOrCreatePayload<CowListPayload>(self, "java/util/concurrent/CopyOnWriteArrayList");
        handled = true;
        return {0, 'V'};
    }
    auto p = GetOrCreatePayload<CowListPayload>(self, "java/util/concurrent/CopyOnWriteArrayList");
    if (!p) {
        return {0, 'V'};
    }
    if (ref.name == "size") {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        return {static_cast<uint64_t>(p->items.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        return {p->items.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "add" && arg_regs.size() >= 2) {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        p->items.push_back(reinterpret_cast<jobject>(arg_regs[1]));
        return {1ULL, 'Z'};
    }
    if (ref.name == "get" && arg_regs.size() >= 2) {
        std::lock_guard<std::mutex> lk(p->mu);
        handled = true;
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        if (idx < 0 || static_cast<size_t>(idx) >= p->items.size()) {
            out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                         "Index out of bounds");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(p->items[static_cast<size_t>(idx)]), 'L'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::mutex> lk(p->mu);
        p->items.clear();
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "iterator") {
        std::vector<jobject> snap;
        {
            std::lock_guard<std::mutex> lk(p->mu);
            snap = p->items;
        }
        handled = true;
        jobject al = MakeSyntheticObj("java/util/ArrayList", nullptr);
        DexMethodRef add_ref{};
        add_ref.name = "add";
        add_ref.return_type = "Z";
        add_ref.param_types = {"Ljava/lang/Object;"};
        for (jobject item : snap) {
            uint64_t a[2] = {reinterpret_cast<uintptr_t>(al), reinterpret_cast<uintptr_t>(item)};
            bool ah = false;
            JavaRuntime::Instance().Invoke(vm, "java/util/ArrayList", add_ref, a, false, env,
                                           out_exception, ah);
        }
        DexMethodRef it_ref{};
        it_ref.name = "iterator";
        it_ref.return_type = "Ljava/util/Iterator;";
        uint64_t a[1] = {reinterpret_cast<uintptr_t>(al)};
        bool ah = false;
        return JavaRuntime::Instance().Invoke(vm, "java/util/ArrayList", it_ref, a, false, env,
                                              out_exception, ah);
    }
    return {0, 'V'};
}

struct FuturePayload {
    std::mutex mu;
    std::condition_variable cv;
    bool done{false};
    bool cancelled{false};
    jobject result{nullptr};
    jobject exception{nullptr};
};

struct ExecutorPayload {
    std::mutex mu;
    std::condition_variable work_cv;
    std::condition_variable term_cv;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> workers;
    size_t active_tasks{0};
    bool shutdown{false};

    void ShutdownAndJoin()
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            shutdown = true;
        }
        work_cv.notify_all();
        for (auto &w : workers) {
            if (w.joinable() && w.get_id() != std::this_thread::get_id()) {
                w.join();
            }
        }
    }
};

jobject CreateThreadPoolExecutor(size_t num_threads)
{
    auto ep = std::make_shared<ExecutorPayload>();
    if (num_threads == 0) {
        num_threads = 1;
    }
    ep->workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        ep->workers.emplace_back([ep, idx = i]() {
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/lang/Thread";
            syn->string_fields["name"] = "pool-thread-" + std::to_string(idx + 1);
            jobject t_obj = JniEnvironment::Instance().RegisterObject(syn);
            JavaRuntime::Instance().SetCurrentJavaThread(t_obj);

            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(ep->mu);
                    ep->work_cv.wait(lk, [&]() { return ep->shutdown || !ep->queue.empty(); });
                    if (ep->queue.empty()) {
                        if (ep->shutdown) {
                            break;
                        }
                        continue;
                    }
                    task = std::move(ep->queue.front());
                    ep->queue.pop_front();
                    ++ep->active_tasks;
                }
                if (task) {
                    task();
                }
                {
                    std::lock_guard<std::mutex> lk(ep->mu);
                    if (ep->active_tasks > 0) {
                        --ep->active_tasks;
                    }
                    if (ep->shutdown && ep->queue.empty() && ep->active_tasks == 0) {
                        ep->term_cv.notify_all();
                    }
                }
            }
        });
    }
    JavaRuntime::Instance().RegisterExecutorCleanup([ep]() { ep->ShutdownAndJoin(); });
    return MakeSyntheticObj("java/util/concurrent/ExecutorService", ep);
}

DexValue HandleExecutors(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                         bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (!is_static) {
        return {0, 'V'};
    }
    if (ref.name == "newSingleThreadExecutor") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateThreadPoolExecutor(1)), 'L'};
    }
    if (ref.name == "newFixedThreadPool") {
        handled = true;
        int32_t n = !arg_regs.empty() ? static_cast<int32_t>(arg_regs[0]) : 1;
        if (n <= 0) {
            out_exception =
                JavaRuntime::CreateException("java/lang/IllegalArgumentException", "nThreads <= 0");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(CreateThreadPoolExecutor(static_cast<size_t>(n))), 'L'};
    }
    if (ref.name == "newCachedThreadPool") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateThreadPoolExecutor(4)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleExecutorService(DexVm &vm, const DexMethodRef &ref,
                               std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                               jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto ep = GetPayload<ExecutorPayload>(reinterpret_cast<jobject>(arg_regs[0]));
    if (!ep) {
        return {0, 'V'};
    }

    auto submit_task = [&](jobject task_obj, bool is_callable, jobject default_ret) -> jobject {
        auto fp = std::make_shared<FuturePayload>();
        jobject fut_obj = MakeSyntheticObj("java/util/concurrent/Future", fp);
        std::lock_guard<std::mutex> lk(ep->mu);
        if (ep->shutdown) {
            out_exception = JavaRuntime::CreateException(
                "java/util/concurrent/RejectedExecutionException", "Executor is shut down");
            return nullptr;
        }
        ep->queue.push_back([&vm, task_obj, is_callable, default_ret, fp, env]() {
            {
                std::lock_guard<std::mutex> flk(fp->mu);
                if (fp->cancelled) {
                    fp->done = true;
                    fp->cv.notify_all();
                    return;
                }
            }
            jobject ex = nullptr;
            bool found = false;
            DexValue res = JavaRuntime::CallVirtual(vm, task_obj, is_callable ? "call" : "run",
                                                    is_callable ? "()Ljava/lang/Object;" : "()V",
                                                    {}, env, ex, &found);
            JniEnvironment::Instance().ClearPendingException();
            std::lock_guard<std::mutex> flk(fp->mu);
            fp->exception = ex;
            fp->result = is_callable ? reinterpret_cast<jobject>(res.raw) : default_ret;
            fp->done = true;
            fp->cv.notify_all();
        });
        ep->work_cv.notify_one();
        return fut_obj;
    };

    if (ref.name == "execute" && arg_regs.size() >= 2) {
        handled = true;
        jobject task_obj = reinterpret_cast<jobject>(arg_regs[1]);
        if (!task_obj) {
            out_exception =
                JavaRuntime::CreateException("java/lang/NullPointerException", "task is null");
            return {0, 'V'};
        }
        (void)submit_task(task_obj, false, nullptr);
        return {0, 'V'};
    }
    if (ref.name == "submit" && arg_regs.size() >= 2) {
        handled = true;
        jobject task_obj = reinterpret_cast<jobject>(arg_regs[1]);
        if (!task_obj) {
            out_exception =
                JavaRuntime::CreateException("java/lang/NullPointerException", "task is null");
            return {0, 'L'};
        }
        bool is_callable =
            !ref.param_types.empty() && ref.param_types[0] == "Ljava/util/concurrent/Callable;";
        jobject def_ret = (arg_regs.size() >= 3) ? reinterpret_cast<jobject>(arg_regs[2]) : nullptr;
        return {reinterpret_cast<uintptr_t>(submit_task(task_obj, is_callable, def_ret)), 'L'};
    }
    if (ref.name == "shutdown") {
        handled = true;
        {
            std::lock_guard<std::mutex> lk(ep->mu);
            ep->shutdown = true;
        }
        ep->work_cv.notify_all();
        ep->term_cv.notify_all();
        return {0, 'V'};
    }
    if (ref.name == "shutdownNow") {
        handled = true;
        std::vector<jobject> unstarted;
        {
            std::lock_guard<std::mutex> lk(ep->mu);
            ep->shutdown = true;
            ep->queue.clear();
        }
        ep->work_cv.notify_all();
        ep->term_cv.notify_all();
        jobject list = MakeSyntheticObj("java/util/ArrayList", nullptr);
        return {reinterpret_cast<uintptr_t>(list), 'L'};
    }
    if (ref.name == "isShutdown") {
        std::lock_guard<std::mutex> lk(ep->mu);
        handled = true;
        return {ep->shutdown ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isTerminated") {
        std::lock_guard<std::mutex> lk(ep->mu);
        handled = true;
        return {(ep->shutdown && ep->queue.empty() && ep->active_tasks == 0) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "awaitTermination" && arg_regs.size() >= 4) {
        handled = true;
        int64_t ms = ToMillisFromUnit(static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1)),
                                      reinterpret_cast<jobject>(arg_regs[3]));
        std::unique_lock<std::mutex> lk(ep->mu);
        bool done = ep->term_cv.wait_for(lk, std::chrono::milliseconds(ms), [&]() {
            return ep->shutdown && ep->queue.empty() && ep->active_tasks == 0;
        });
        return {done ? 1ULL : 0ULL, 'Z'};
    }
    return {0, 'V'};
}

DexValue HandleFuture(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto fp = GetPayload<FuturePayload>(reinterpret_cast<jobject>(arg_regs[0]));
    if (!fp) {
        return {0, 'V'};
    }
    if (ref.name == "isDone") {
        std::lock_guard<std::mutex> lk(fp->mu);
        handled = true;
        return {fp->done ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isCancelled") {
        std::lock_guard<std::mutex> lk(fp->mu);
        handled = true;
        return {fp->cancelled ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "cancel") {
        std::lock_guard<std::mutex> lk(fp->mu);
        handled = true;
        if (fp->done) {
            return {0ULL, 'Z'};
        }
        fp->cancelled = true;
        fp->done = true;
        fp->cv.notify_all();
        return {1ULL, 'Z'};
    }
    if (ref.name == "get") {
        handled = true;
        std::unique_lock<std::mutex> lk(fp->mu);
        if (arg_regs.size() >= 4 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            int64_t ms = ToMillisFromUnit(static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1)),
                                          reinterpret_cast<jobject>(arg_regs[3]));
            if (!fp->cv.wait_for(lk, std::chrono::milliseconds(ms), [&]() { return fp->done; })) {
                out_exception = JavaRuntime::CreateException(
                    "java/util/concurrent/TimeoutException", "Future get timed out");
                return {0, 'L'};
            }
        } else {
            fp->cv.wait(lk, [&]() { return fp->done; });
        }
        if (fp->cancelled) {
            out_exception = JavaRuntime::CreateException(
                "java/util/concurrent/CancellationException", "Future cancelled");
            return {0, 'L'};
        }
        if (fp->exception) {
            out_exception = JavaRuntime::CreateException("java/util/concurrent/ExecutionException",
                                                         "Task threw exception");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(fp->result), 'L'};
    }
    return {0, 'V'};
}

} // namespace

void RegisterConcurrentIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/lang/Object", HandleObjectMonitors);
    rt.RegisterClass("java/lang/Thread", HandleThread, {"java/lang/Runnable"});
    rt.RegisterClass("java/util/concurrent/atomic/AtomicInteger",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &, bool &h) {
                         return HandleAtomicNumber<int32_t, 'I'>(
                             "java/util/concurrent/atomic/AtomicInteger", r, a, s, h);
                     },
                     {"java/lang/Number"});
    rt.RegisterClass("java/util/concurrent/atomic/AtomicLong",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &, bool &h) {
                         return HandleAtomicNumber<int64_t, 'J'>(
                             "java/util/concurrent/atomic/AtomicLong", r, a, s, h);
                     },
                     {"java/lang/Number"});
    rt.RegisterClass("java/util/concurrent/atomic/AtomicBoolean", HandleAtomicBoolean);
    rt.RegisterClass("java/util/concurrent/atomic/AtomicReference", HandleAtomicReference);
    rt.RegisterClass("java/util/concurrent/locks/ReentrantLock", HandleReentrantLock,
                     {"java/util/concurrent/locks/Lock"});
    rt.RegisterClass("java/util/concurrent/locks/Lock", HandleReentrantLock);
    rt.RegisterClass("java/util/concurrent/locks/Condition", HandleCondition);
    rt.RegisterClass("java/util/concurrent/CountDownLatch", HandleCountDownLatch);
    rt.RegisterClass("java/util/concurrent/ConcurrentHashMap", HandleConcurrentHashMap,
                     {"java/util/concurrent/ConcurrentMap", "java/util/Map"});
    rt.RegisterClass("java/util/concurrent/CopyOnWriteArrayList", HandleCopyOnWriteArrayList,
                     {"java/util/List", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass("java/util/concurrent/TimeUnit", HandleTimeUnit);
    rt.RegisterClass("java/util/concurrent/Executors", HandleExecutors);
    rt.RegisterClass("java/util/concurrent/ExecutorService", HandleExecutorService,
                     {"java/util/concurrent/Executor"});
    rt.RegisterClass("java/util/concurrent/Executor", HandleExecutorService);
    rt.RegisterClass("java/util/concurrent/Future", HandleFuture);

    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "NANOSECONDS", []() {
        return static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("NANOSECONDS")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "MICROSECONDS", []() {
        return static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("MICROSECONDS")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "MILLISECONDS", []() {
        return static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("MILLISECONDS")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "SECONDS", []() {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("SECONDS")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "MINUTES", []() {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("MINUTES")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "HOURS", []() {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("HOURS")));
    });
    rt.RegisterStaticField("java/util/concurrent/TimeUnit", "DAYS", []() {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(GetTimeUnitSingleton("DAYS")));
    });
}

} // namespace portage::package
