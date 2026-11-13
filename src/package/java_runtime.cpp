#include "package/java_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <unordered_set>

namespace portage::package {

struct JavaRuntime::ThreadRecord {
    std::thread worker;
    std::atomic<bool> alive{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> daemon{false};
    std::atomic<bool> interrupted{false};
    std::mutex mu;
    std::condition_variable cv;
    std::weak_ptr<JniObjectMonitor> waiting_monitor;
};

namespace {

thread_local jobject g_current_java_thread = nullptr;
thread_local uint64_t g_current_java_thread_epoch = 0;
thread_local std::weak_ptr<JavaRuntime::ThreadRecord> g_current_thread_record;

std::string NormalizeSlash(std::string_view name)
{
    if (name.empty()) {
        return {};
    }
    if (name.front() == 'L' && name.back() == ';') {
        return DexFile::DescriptorToSlashName(name);
    }
    return std::string(name);
}

} // namespace

JavaRuntime &JavaRuntime::Instance() noexcept
{
    static JavaRuntime inst;
    return inst;
}

JavaRuntime::JavaRuntime()
{
    RegisterStringIntrinsics(*this);
    RegisterBoxingIntrinsics(*this);
    RegisterUtilIntrinsics(*this);
    RegisterConcurrentIntrinsics(*this);
    RegisterReflectIntrinsics(*this);
    RegisterNioIntrinsics(*this);
    RegisterIoRegexIntrinsics(*this);
}

JavaRuntime::~JavaRuntime() { Reset(); }

void JavaRuntime::RegisterClass(std::string_view class_slash, IntrinsicFn fn,
                                std::initializer_list<std::string_view> parents)
{
    if (fn) {
        handlers_[class_slash] = fn;
    }
    if (parents.size() > 0) {
        auto &vec = parents_[class_slash];
        vec.insert(vec.end(), parents.begin(), parents.end());
    }
}

void JavaRuntime::RegisterHierarchy(std::string_view class_slash,
                                    std::initializer_list<std::string_view> parents)
{
    auto &vec = parents_[class_slash];
    vec.insert(vec.end(), parents.begin(), parents.end());
}

void JavaRuntime::RegisterStaticField(std::string_view class_slash, std::string_view field_name,
                                      StaticFieldFn fn)
{
    std::string key(class_slash);
    key.push_back(':');
    key.append(field_name);
    static_fields_[std::move(key)] = fn;
}

DexValue JavaRuntime::Invoke(DexVm &vm, std::string_view runtime_class_slash,
                             const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception,
                             bool &handled) const
{
    handled = false;
    std::string start = NormalizeSlash(runtime_class_slash);
    if (start.empty()) {
        start = DexFile::DescriptorToSlashName(ref.class_descriptor);
    }

    std::deque<std::string_view> queue;
    std::unordered_set<std::string_view> visited;
    queue.push_back(start);

    while (!queue.empty()) {
        std::string_view cur = queue.front();
        queue.pop_front();
        if (!visited.insert(cur).second) {
            continue;
        }
        if (auto it = handlers_.find(cur); it != handlers_.end() && it->second) {
            DexValue res = it->second(vm, ref, arg_regs, is_static, env, out_exception, handled);
            if (handled) {
                return res;
            }
        }
        if (auto pit = parents_.find(cur); pit != parents_.end()) {
            for (std::string_view p : pit->second) {
                if (!visited.contains(p)) {
                    queue.push_back(p);
                }
            }
        }
    }
    return {0, 'V'};
}

bool JavaRuntime::HasHostClass(std::string_view class_slash) const
{
    std::string s = NormalizeSlash(class_slash);
    return handlers_.contains(s) || parents_.contains(s);
}

bool JavaRuntime::IsAssignableFrom(std::string_view sub_slash, std::string_view target_slash) const
{
    std::string sub = NormalizeSlash(sub_slash);
    std::string target = NormalizeSlash(target_slash);
    if (target == "java/lang/Object" || sub == target) {
        return true;
    }

    std::deque<std::string_view> queue;
    std::unordered_set<std::string_view> visited;
    queue.push_back(sub);
    while (!queue.empty()) {
        std::string_view cur = queue.front();
        queue.pop_front();
        if (cur == target) {
            return true;
        }
        if (!visited.insert(cur).second) {
            continue;
        }
        if (auto pit = parents_.find(cur); pit != parents_.end()) {
            for (std::string_view p : pit->second) {
                queue.push_back(p);
            }
        }
    }
    return false;
}

bool JavaRuntime::TryGetStaticField(std::string_view class_desc, std::string_view field_name,
                                    uint64_t &out_val) const
{
    std::string key = NormalizeSlash(class_desc);
    key.push_back(':');
    key.append(field_name);
    if (auto it = static_fields_.find(key); it != static_fields_.end() && it->second) {
        out_val = it->second();
        return true;
    }
    return false;
}

void JavaRuntime::MonitorEnter(jobject obj)
{
    if (!obj) {
        return;
    }
    auto mon = JniEnvironment::Instance().GetMonitor(obj);
    if (!mon) {
        return;
    }
    const auto self_tid = std::this_thread::get_id();
    std::unique_lock<std::mutex> lk(mon->mu);
    mon->lock_cv.wait(lk, [&]() { return mon->recursion == 0 || mon->owner == self_tid; });
    mon->owner = self_tid;
    ++mon->recursion;
}

void JavaRuntime::MonitorExit(jobject obj)
{
    if (!obj) {
        return;
    }
    auto mon = JniEnvironment::Instance().GetMonitor(obj);
    if (!mon) {
        return;
    }
    const auto self_tid = std::this_thread::get_id();
    std::lock_guard<std::mutex> lk(mon->mu);
    if (mon->owner == self_tid && mon->recursion > 0) {
        if (--mon->recursion == 0) {
            mon->owner = std::thread::id{};
            mon->lock_cv.notify_one();
        }
    }
}

void JavaRuntime::ObjectWait(jobject obj, int64_t timeout_ms, jobject &out_exception)
{
    auto mon = JniEnvironment::Instance().GetMonitor(obj);
    if (!mon) {
        out_exception = CreateException("java/lang/NullPointerException", "wait on null");
        return;
    }
    const auto self_tid = std::this_thread::get_id();
    std::shared_ptr<ThreadRecord> rec = g_current_thread_record.lock();
    std::unique_lock<std::mutex> lk(mon->mu);
    if (mon->owner != self_tid || mon->recursion == 0) {
        out_exception =
            CreateException("java/lang/IllegalMonitorStateException", "current thread not owner");
        return;
    }
    if (rec && rec->interrupted.exchange(false)) {
        out_exception = CreateException("java/lang/InterruptedException", "interrupted");
        return;
    }

    uint32_t saved_depth = mon->recursion;
    uint64_t start_all_seq = mon->notify_all_seq;
    mon->recursion = 0;
    mon->owner = std::thread::id{};
    mon->lock_cv.notify_one();

    if (rec) {
        std::lock_guard<std::mutex> rlk(rec->mu);
        rec->waiting_monitor = mon;
    }

    auto wait_pred = [&]() {
        if (rec && rec->interrupted.load()) {
            return true;
        }
        if (mon->notify_all_seq != start_all_seq) {
            return true;
        }
        if (mon->notify_permits > 0) {
            --mon->notify_permits;
            return true;
        }
        return false;
    };

    if (timeout_ms <= 0) {
        mon->wait_cv.wait(lk, wait_pred);
    } else {
        mon->wait_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), wait_pred);
    }

    if (rec) {
        std::lock_guard<std::mutex> rlk(rec->mu);
        rec->waiting_monitor.reset();
    }

    mon->lock_cv.wait(lk, [&]() { return mon->recursion == 0; });
    mon->owner = self_tid;
    mon->recursion = saved_depth;

    if (rec && rec->interrupted.exchange(false)) {
        out_exception = CreateException("java/lang/InterruptedException", "interrupted");
    }
}

void JavaRuntime::ObjectNotify(jobject obj, bool all, jobject &out_exception)
{
    auto mon = JniEnvironment::Instance().GetMonitor(obj);
    if (!mon) {
        out_exception = CreateException("java/lang/NullPointerException", "notify on null");
        return;
    }
    const auto self_tid = std::this_thread::get_id();
    std::lock_guard<std::mutex> lk(mon->mu);
    if (mon->owner != self_tid || mon->recursion == 0) {
        out_exception =
            CreateException("java/lang/IllegalMonitorStateException", "current thread not owner");
        return;
    }
    if (all) {
        ++mon->notify_all_seq;
        mon->wait_cv.notify_all();
    } else {
        ++mon->notify_permits;
        mon->wait_cv.notify_one();
    }
}

void JavaRuntime::PrepareManagedThread(jobject thread_obj)
{
    auto syn = JniEnvironment::Instance().LookupObject(thread_obj);
    auto rec = std::make_shared<ThreadRecord>();
    rec->alive.store(true);
    rec->finished.store(false);
    rec->daemon.store(syn && syn->bool_fields["daemon"] != 0);
    std::shared_ptr<ThreadRecord> old;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        auto it = threads_.find(thread_obj);
        if (it != threads_.end()) {
            old = std::move(it->second);
        }
        threads_[thread_obj] = rec;
    }
    if (old && old->worker.joinable() && old->worker.get_id() != std::this_thread::get_id()) {
        old->worker.join();
    }
}

void JavaRuntime::MarkThreadFinished(jobject thread_obj)
{
    std::shared_ptr<ThreadRecord> rec;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        auto it = threads_.find(thread_obj);
        if (it != threads_.end()) {
            rec = it->second;
        }
    }
    if (rec) {
        rec->alive.store(false);
        rec->finished.store(true);
        rec->cv.notify_all();
    }
}

void JavaRuntime::RegisterManagedThread(jobject thread_obj, std::thread t)
{
    std::lock_guard<std::mutex> lk(thread_mutex_);
    auto it = threads_.find(thread_obj);
    if (it != threads_.end() && it->second) {
        it->second->worker = std::move(t);
        return;
    }
    auto syn = JniEnvironment::Instance().LookupObject(thread_obj);
    auto rec = std::make_shared<ThreadRecord>();
    rec->alive.store(true);
    rec->daemon.store(syn && syn->bool_fields["daemon"] != 0);
    rec->worker = std::move(t);
    threads_[thread_obj] = rec;
}

void JavaRuntime::JoinThreadObject(jobject thread_obj, int64_t timeout_ms, jobject &out_exception)
{
    if (CheckAndConsumeInterrupt()) {
        out_exception = CreateException("java/lang/InterruptedException", "interrupted");
        return;
    }
    std::shared_ptr<ThreadRecord> rec;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        auto it = threads_.find(thread_obj);
        if (it != threads_.end()) {
            rec = it->second;
        }
    }
    if (!rec) {
        return;
    }
    if (timeout_ms <= 0) {
        if (rec->worker.joinable() && rec->worker.get_id() != std::this_thread::get_id()) {
            rec->worker.join();
        } else {
            std::unique_lock<std::mutex> lk(rec->mu);
            rec->cv.wait(lk, [&]() { return rec->finished.load(); });
        }
    } else {
        std::unique_lock<std::mutex> lk(rec->mu);
        rec->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                         [&]() { return rec->finished.load(); });
        if (rec->finished.load() && rec->worker.joinable() &&
            rec->worker.get_id() != std::this_thread::get_id()) {
            lk.unlock();
            rec->worker.join();
        }
    }
}

void JavaRuntime::InterruptThreadObject(jobject thread_obj)
{
    std::shared_ptr<ThreadRecord> rec;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        auto it = threads_.find(thread_obj);
        if (it != threads_.end()) {
            rec = it->second;
        }
    }
    if (rec) {
        rec->interrupted.store(true);
        rec->cv.notify_all();
        std::shared_ptr<JniObjectMonitor> mon;
        {
            std::lock_guard<std::mutex> rlk(rec->mu);
            mon = rec->waiting_monitor.lock();
        }
        if (mon) {
            std::lock_guard<std::mutex> mlk(mon->mu);
            mon->wait_cv.notify_all();
        }
    }
}

bool JavaRuntime::CheckAndConsumeInterrupt()
{
    if (auto rec = g_current_thread_record.lock()) {
        return rec->interrupted.exchange(false);
    }
    return false;
}

bool JavaRuntime::IsThreadInterrupted(jobject thread_obj) const
{
    std::lock_guard<std::mutex> lk(thread_mutex_);
    auto it = threads_.find(thread_obj);
    return (it != threads_.end() && it->second) ? it->second->interrupted.load() : false;
}

bool JavaRuntime::IsThreadAlive(jobject thread_obj) const
{
    std::lock_guard<std::mutex> lk(thread_mutex_);
    auto it = threads_.find(thread_obj);
    return (it != threads_.end() && it->second) ? it->second->alive.load() : false;
}

void JavaRuntime::SetCurrentJavaThread(jobject thread_obj)
{
    g_current_java_thread = thread_obj;
    g_current_java_thread_epoch = JniEnvironment::Instance().GetResetEpoch();
    std::lock_guard<std::mutex> lk(thread_mutex_);
    auto it = threads_.find(thread_obj);
    if (it != threads_.end()) {
        g_current_thread_record = it->second;
    } else {
        g_current_thread_record.reset();
    }
}

jobject JavaRuntime::GetCurrentJavaThread()
{
    uint64_t epoch = JniEnvironment::Instance().GetResetEpoch();
    if (!g_current_java_thread || g_current_java_thread_epoch != epoch) {
        auto syn = std::make_shared<JniSyntheticObject>();
        syn->class_name = "java/lang/Thread";
        syn->string_fields["name"] = "main";
        g_current_java_thread = JniEnvironment::Instance().RegisterObject(syn);
        g_current_java_thread_epoch = epoch;
        auto rec = std::make_shared<ThreadRecord>();
        rec->alive.store(true);
        std::lock_guard<std::mutex> lk(thread_mutex_);
        threads_[g_current_java_thread] = rec;
        g_current_thread_record = rec;
    }
    return g_current_java_thread;
}

void JavaRuntime::SleepInterruptibly(int64_t timeout_ms, jobject &out_exception)
{
    if (timeout_ms < 0) {
        out_exception =
            CreateException("java/lang/IllegalArgumentException", "timeout is negative");
        return;
    }
    if (CheckAndConsumeInterrupt()) {
        out_exception = CreateException("java/lang/InterruptedException", "sleep interrupted");
        return;
    }
    if (timeout_ms == 0) {
        std::this_thread::yield();
        return;
    }
    (void)GetCurrentJavaThread();
    if (auto rec = g_current_thread_record.lock()) {
        std::unique_lock<std::mutex> lk(rec->mu);
        bool woken = rec->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                      [&]() { return rec->interrupted.load(); });
        if (woken && rec->interrupted.exchange(false)) {
            out_exception = CreateException("java/lang/InterruptedException", "sleep interrupted");
        }
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

void JavaRuntime::RegisterExecutorCleanup(std::function<void()> cleanup)
{
    std::lock_guard<std::mutex> lk(thread_mutex_);
    executor_cleanups_.push_back(std::move(cleanup));
}

void JavaRuntime::JoinAllNonDaemonThreads()
{
    std::vector<std::shared_ptr<ThreadRecord>> to_join;
    std::vector<std::function<void()>> cleanups;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        cleanups = std::move(executor_cleanups_);
        executor_cleanups_.clear();
        for (auto &[obj, rec] : threads_) {
            (void)obj;
            if (rec && !rec->daemon.load() && rec->worker.joinable() &&
                rec->worker.get_id() != std::this_thread::get_id()) {
                to_join.push_back(rec);
            }
        }
    }
    for (auto &fn : cleanups) {
        if (fn) {
            fn();
        }
    }
    for (auto &rec : to_join) {
        if (rec->worker.joinable()) {
            rec->worker.join();
        }
    }
}

void JavaRuntime::Reset()
{
    std::vector<std::shared_ptr<ThreadRecord>> all_recs;
    std::vector<std::function<void()>> cleanups;
    {
        std::lock_guard<std::mutex> lk(thread_mutex_);
        cleanups = std::move(executor_cleanups_);
        executor_cleanups_.clear();
        for (auto &[obj, rec] : threads_) {
            (void)obj;
            if (rec) {
                rec->interrupted.store(true);
                rec->cv.notify_all();
                all_recs.push_back(rec);
            }
        }
        threads_.clear();
    }
    for (auto &fn : cleanups) {
        if (fn) {
            fn();
        }
    }
    for (auto &rec : all_recs) {
        if (rec->worker.joinable() && rec->worker.get_id() != std::this_thread::get_id()) {
            rec->worker.join();
        }
    }
    g_current_java_thread = nullptr;
    g_current_java_thread_epoch = 0;
    g_current_thread_record.reset();
    {
        std::lock_guard<std::mutex> clk(class_mutex_);
        class_objects_.clear();
        class_epoch_ = JniEnvironment::Instance().GetResetEpoch();
    }
}

jobject JavaRuntime::GetClassObject(std::string_view class_slash)
{
    std::string norm = NormalizeSlash(class_slash);
    if (norm.empty()) {
        norm = "java/lang/Object";
    }
    auto &jni = JniEnvironment::Instance();
    uint64_t epoch = jni.GetResetEpoch();
    std::lock_guard<std::mutex> lk(class_mutex_);
    if (class_epoch_ != epoch) {
        class_objects_.clear();
        class_epoch_ = epoch;
    }
    if (auto it = class_objects_.find(norm); it != class_objects_.end()) {
        return it->second;
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Class;
    syn->class_name = "java/lang/Class";
    syn->target_class = norm;
    std::string dot_name = norm;
    for (char &ch : dot_name) {
        if (ch == '/') {
            ch = '.';
        }
    }
    syn->string_fields["name"] = std::move(dot_name);
    jobject obj = jni.RegisterObject(syn);
    class_objects_.emplace(std::move(norm), obj);
    return obj;
}

std::string JavaRuntime::ExtractClassSlash(jobject class_or_str)
{
    if (!class_or_str) {
        return {};
    }
    auto &jni = JniEnvironment::Instance();
    if (auto syn = jni.LookupObject(class_or_str)) {
        if (syn->type == JniObjectType::Class && !syn->target_class.empty()) {
            return NormalizeSlash(syn->target_class);
        }
        if (syn->class_name == "java/lang/Class") {
            if (!syn->target_class.empty()) {
                return NormalizeSlash(syn->target_class);
            }
            if (auto it = syn->string_fields.find("name"); it != syn->string_fields.end()) {
                std::string s = it->second;
                for (char &ch : s) {
                    if (ch == '.') {
                        ch = '/';
                    }
                }
                return NormalizeSlash(s);
            }
        }
        return NormalizeSlash(syn->class_name);
    }
    if (const char *s = jni.GetStringChars(class_or_str)) {
        std::string out(s);
        for (char &ch : out) {
            if (ch == '.') {
                ch = '/';
            }
        }
        return NormalizeSlash(out);
    }
    return {};
}

jobject JavaRuntime::CreateException(std::string_view class_slash, std::string_view message)
{
    auto ex = std::make_shared<JniSyntheticObject>();
    ex->type = JniObjectType::Generic;
    ex->class_name = std::string(class_slash);
    ex->string_fields["detailMessage"] = std::string(message);
    return JniEnvironment::Instance().RegisterObject(ex);
}

DexValue JavaRuntime::CallVirtual(DexVm &vm, jobject obj, std::string_view method_name,
                                  std::string_view sig, std::span<const uint64_t> extra_args,
                                  JNIEnv *env, jobject &out_exception, bool *out_found)
{
    if (out_found) {
        *out_found = false;
    }
    if (!obj) {
        return {0, 'V'};
    }
    if (!env) {
        env = JniEnvironment::Instance().GetJNIEnv();
    }

    auto &jni = JniEnvironment::Instance();
    std::string cls_slash;
    if (jni.GetStringChars(obj) != nullptr) {
        cls_slash = "java/lang/String";
    } else if (auto syn = jni.LookupObject(obj)) {
        cls_slash = DexFile::DescriptorToSlashName(syn->class_name);
    }
    if (cls_slash.empty()) {
        return {0, 'V'};
    }

    std::vector<uint64_t> full_args;
    full_args.reserve(1 + extra_args.size());
    full_args.push_back(reinterpret_cast<uintptr_t>(obj));
    full_args.insert(full_args.end(), extra_args.begin(), extra_args.end());

    std::string cls_desc = DexFile::NormalizeDescriptor(cls_slash);
    if (cls_slash == "java/lang/invoke/LambdaProxy") {
        DexMethodRef lambda_ref{};
        lambda_ref.class_descriptor = cls_desc;
        lambda_ref.name = std::string(method_name);
        lambda_ref.descriptor = std::string(sig);
        bool lambda_handled = false;
        DexValue lv = Instance().TryInvokeLambdaProxy(vm, obj, lambda_ref, full_args, env,
                                                      out_exception, lambda_handled);
        if (lambda_handled) {
            if (out_found) {
                *out_found = true;
            }
            return lv;
        }
    }

    if (vm.HasClass(cls_desc)) {
        if (auto resolved = vm.ResolveMethod(cls_desc, method_name, sig, true)) {
            if (out_found) {
                *out_found = true;
            }
            DexValue v = vm.ExecuteMethod(resolved->dex, resolved->method, full_args, env);
            if (jthrowable pending = jni.GetPendingException()) {
                out_exception = pending;
                jni.ClearPendingException();
            }
            return v;
        }
    }

    DexMethodRef ref{};
    ref.class_descriptor = cls_desc;
    ref.name = std::string(method_name);
    ref.descriptor = std::string(sig);
    auto open_p = sig.find('(');
    auto close_p = sig.find(')');
    if (open_p != std::string_view::npos && close_p != std::string_view::npos && close_p > open_p) {
        std::string_view params = sig.substr(open_p + 1, close_p - open_p - 1);
        for (size_t i = 0; i < params.size();) {
            size_t start = i;
            while (i < params.size() && params[i] == '[') {
                ++i;
            }
            if (i < params.size() && params[i] == 'L') {
                size_t semi = params.find(';', i);
                i = (semi == std::string_view::npos) ? params.size() : (semi + 1);
            } else if (i < params.size()) {
                ++i;
            }
            if (i > start) {
                ref.param_types.emplace_back(params.substr(start, i - start));
            }
        }
        ref.return_type = std::string(sig.substr(close_p + 1));
    }

    bool handled = false;
    DexValue hv =
        Instance().Invoke(vm, cls_slash, ref, full_args, false, env, out_exception, handled);
    if (handled) {
        if (out_found) {
            *out_found = true;
        }
        return hv;
    }
    return {0, 'V'};
}

int32_t JavaRuntime::ComputeObjectHashCode(DexVm &vm, jobject obj, JNIEnv *env,
                                           jobject &out_exception)
{
    if (!obj) {
        return 0;
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(obj)) {
        return java_rt::JavaStringHashCode(s);
    }
    bool found = false;
    DexValue res = CallVirtual(vm, obj, "hashCode", "()I", {}, env, out_exception, &found);
    if (out_exception) {
        return 0;
    }
    if (found) {
        return static_cast<int32_t>(res.raw);
    }
    return static_cast<int32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj)));
}

bool JavaRuntime::ComputeObjectEquals(DexVm &vm, jobject a, jobject b, JNIEnv *env,
                                      jobject &out_exception)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *sa = jni.GetStringChars(a)) {
        const char *sb = jni.GetStringChars(b);
        return sb && std::strcmp(sa, sb) == 0;
    }
    std::array<uint64_t, 1> extra = {reinterpret_cast<uintptr_t>(b)};
    bool found = false;
    DexValue res =
        CallVirtual(vm, a, "equals", "(Ljava/lang/Object;)Z", extra, env, out_exception, &found);
    if (out_exception) {
        return false;
    }
    if (found) {
        return res.raw != 0;
    }
    return a == b;
}

int32_t JavaRuntime::ComputeObjectCompare(DexVm &vm, jobject comparator, jobject a, jobject b,
                                          JNIEnv *env, jobject &out_exception)
{
    if (a == b) {
        return 0;
    }
    if (comparator) {
        std::array<uint64_t, 2> extra = {reinterpret_cast<uintptr_t>(a),
                                         reinterpret_cast<uintptr_t>(b)};
        bool found = false;
        DexValue res =
            CallVirtual(vm, comparator, "compare", "(Ljava/lang/Object;Ljava/lang/Object;)I", extra,
                        env, out_exception, &found);
        if (out_exception) {
            return 0;
        }
        if (found) {
            return static_cast<int32_t>(res.raw);
        }
    }

    auto &jni = JniEnvironment::Instance();
    if (const char *sa = jni.GetStringChars(a)) {
        if (const char *sb = jni.GetStringChars(b)) {
            std::u16string ua = java_rt::Utf8ToUtf16(sa);
            std::u16string ub = java_rt::Utf8ToUtf16(sb);
            if (ua == ub) {
                return 0;
            }
            return (ua < ub) ? -1 : 1;
        }
    }

    std::array<uint64_t, 1> extra = {reinterpret_cast<uintptr_t>(b)};
    bool found = false;
    DexValue res =
        CallVirtual(vm, a, "compareTo", "(Ljava/lang/Object;)I", extra, env, out_exception, &found);
    if (out_exception) {
        return 0;
    }
    if (found) {
        return static_cast<int32_t>(res.raw);
    }

    int32_t ia = 0;
    int32_t ib = 0;
    if (java_rt::TryUnwrapInt(a, ia) && java_rt::TryUnwrapInt(b, ib)) {
        return (ia < ib) ? -1 : (ia > ib ? 1 : 0);
    }
    int64_t la = 0;
    int64_t lb = 0;
    if (java_rt::TryUnwrapLong(a, la) && java_rt::TryUnwrapLong(b, lb)) {
        return (la < lb) ? -1 : (la > lb ? 1 : 0);
    }
    double da = 0.0;
    double db = 0.0;
    if (java_rt::TryUnwrapDouble(a, da) && java_rt::TryUnwrapDouble(b, db)) {
        return (da < db) ? -1 : (da > db ? 1 : 0);
    }
    return 0;
}

std::string JavaRuntime::ObjectToString(DexVm &vm, jobject obj, JNIEnv *env, jobject &out_exception)
{
    if (!obj) {
        return "null";
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(obj)) {
        return s;
    }
    bool found = false;
    DexValue res =
        CallVirtual(vm, obj, "toString", "()Ljava/lang/String;", {}, env, out_exception, &found);
    if (out_exception) {
        return "";
    }
    if (found && res.raw != 0) {
        if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(res.raw))) {
            return s;
        }
    }
    if (auto syn = jni.LookupObject(obj)) {
        return syn->class_name;
    }
    return "Object";
}

} // namespace portage::package
