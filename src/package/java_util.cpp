#include "package/java_runtime.hpp"

#include <algorithm>
#include <bit>
#include <deque>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace portage::package {

namespace {

enum class ListKind { Array, Linked };
enum class MapKind { Hash, Linked, Tree };
enum class SetViewKind { DirectMap, Keys, Values, Entries };

struct ListPayload {
    std::vector<jobject> items;
    uint64_t mod_count{0};
    bool unmodifiable{false};
    ListKind kind{ListKind::Array};
};

struct DequePayload {
    std::deque<jobject> items;
    uint64_t mod_count{0};
};

struct MapPayload {
    MapKind kind{MapKind::Hash};
    jobject comparator{nullptr};
    std::vector<std::pair<jobject, jobject>> entries;
    uint64_t mod_count{0};
    bool unmodifiable{false};
    bool access_order{false};
};

struct SetPayload {
    SetViewKind view{SetViewKind::DirectMap};
    jobject backing_obj{nullptr};
    std::shared_ptr<MapPayload> owned_map;
    bool unmodifiable{false};
};

struct IteratorPayload {
    enum class SourceType { List, Deque, Set, Map };
    SourceType source_type{SourceType::List};
    SetViewKind view_kind{SetViewKind::Keys};
    jobject owner_obj{nullptr};
    uint64_t expected_mod{0};
    std::vector<jobject> snapshot;
    size_t cursor{0};
    int64_t last_ret{-1};
};

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

jobject MakeSyntheticObj(std::string_view class_slash, std::shared_ptr<void> payload)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = std::string(class_slash);
    syn->runtime_payload = std::move(payload);
    return JniEnvironment::Instance().RegisterObject(syn);
}

int32_t CompareObjectsNatural(DexVm &vm, jobject a, jobject b, JNIEnv *env, jobject &out_exception)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *sa = jni.GetStringChars(a)) {
        if (const char *sb = jni.GetStringChars(b)) {
            std::u16string ua = java_rt::Utf8ToUtf16(sa);
            std::u16string ub = java_rt::Utf8ToUtf16(sb);
            if (ua == ub) {
                return 0;
            }
            return ua < ub ? -1 : 1;
        }
    }
    bool found = false;
    uint64_t arg_b = reinterpret_cast<uintptr_t>(b);
    DexValue r =
        JavaRuntime::CallVirtual(vm, a, "compareTo", "(Ljava/lang/Object;)I",
                                 std::span<const uint64_t>(&arg_b, 1), env, out_exception, &found);
    if (out_exception || !found) {
        uintptr_t pa = reinterpret_cast<uintptr_t>(a);
        uintptr_t pb = reinterpret_cast<uintptr_t>(b);
        return (pa < pb) ? -1 : (pa > pb ? 1 : 0);
    }
    return static_cast<int32_t>(r.raw);
}

int32_t CompareKeys(DexVm &vm, jobject comparator, jobject a, jobject b, JNIEnv *env,
                    jobject &out_exception)
{
    if (comparator) {
        return JavaRuntime::ComputeObjectCompare(vm, comparator, a, b, env, out_exception);
    }
    return CompareObjectsNatural(vm, a, b, env, out_exception);
}

std::vector<jobject> SnapshotCollection(DexVm &vm, jobject coll, JNIEnv *env,
                                        jobject &out_exception)
{
    std::vector<jobject> out;
    if (!coll) {
        return out;
    }
    auto syn = JniEnvironment::Instance().LookupObject(coll);
    if (!syn) {
        return out;
    }
    if (syn->class_name == "java/util/ArrayList" || syn->class_name == "java/util/LinkedList" ||
        syn->class_name == "java/util/List" || syn->class_name == "java/util/Collection") {
        if (auto lp = GetPayload<ListPayload>(coll)) {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            return lp->items;
        }
    }
    if (syn->class_name == "java/util/ArrayDeque") {
        if (auto dp = GetPayload<DequePayload>(coll)) {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            return {dp->items.begin(), dp->items.end()};
        }
    }
    bool found = false;
    DexValue arr_val = JavaRuntime::CallVirtual(vm, coll, "toArray", "()[Ljava/lang/Object;", {},
                                                env, out_exception, &found);
    if (!out_exception && found && arr_val.raw != 0) {
        auto arr = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arr_val.raw));
        if (arr && arr->type == JniObjectType::ObjectArray) {
            return arr->object_data;
        }
    }
    return out;
}

jobject PackObjectArray(const std::vector<jobject> &items, jobject target_arr = nullptr)
{
    auto &jni = JniEnvironment::Instance();
    if (target_arr) {
        auto syn = jni.LookupObject(target_arr);
        if (syn && syn->type == JniObjectType::ObjectArray) {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (syn->object_data.size() >= items.size()) {
                for (size_t i = 0; i < items.size(); ++i) {
                    syn->object_data[i] = items[i];
                }
                if (syn->object_data.size() > items.size()) {
                    syn->object_data[items.size()] = nullptr;
                }
                return target_arr;
            }
        }
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::ObjectArray;
    syn->class_name = "[Ljava/lang/Object;";
    syn->object_data = items;
    return jni.RegisterObject(syn);
}

ptrdiff_t FindMapEntryIndex(DexVm &vm, const std::shared_ptr<MapPayload> &mp, jobject key,
                            jobject map_obj, JNIEnv *env, jobject &out_exception)
{
    std::vector<jobject> keys;
    jobject comp = nullptr;
    MapKind kind = MapKind::Hash;
    {
        auto syn = map_obj ? JniEnvironment::Instance().LookupObject(map_obj) : nullptr;
        std::unique_lock<std::recursive_mutex> lk;
        if (syn) {
            lk = std::unique_lock<std::recursive_mutex>(syn->field_mutex);
        }
        kind = mp->kind;
        comp = mp->comparator;
        keys.reserve(mp->entries.size());
        for (const auto &kv : mp->entries) {
            keys.push_back(kv.first);
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (kind == MapKind::Tree) {
            int cmp = CompareKeys(vm, comp, keys[i], key, env, out_exception);
            if (out_exception) {
                return -1;
            }
            if (cmp == 0) {
                return static_cast<ptrdiff_t>(i);
            }
        } else {
            bool eq = JavaRuntime::ComputeObjectEquals(vm, keys[i], key, env, out_exception);
            if (out_exception) {
                return -1;
            }
            if (eq) {
                return static_cast<ptrdiff_t>(i);
            }
        }
    }
    return -1;
}

jobject MakeSimpleEntry(jobject k, jobject v)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/util/AbstractMap$SimpleEntry";
    syn->object_fields["key"] = k;
    syn->object_fields["value"] = v;
    return JniEnvironment::Instance().RegisterObject(syn);
}

jobject CreateListIterator(jobject owner_obj, const std::shared_ptr<ListPayload> &lp,
                           size_t start_cursor)
{
    auto it = std::make_shared<IteratorPayload>();
    it->source_type = IteratorPayload::SourceType::List;
    it->owner_obj = owner_obj;
    it->expected_mod = lp->mod_count;
    it->snapshot = lp->items;
    it->cursor = std::min(start_cursor, it->snapshot.size());
    return MakeSyntheticObj("java/util/ListIterator", it);
}

jobject CreateMapView(jobject map_obj, SetViewKind view)
{
    auto sp = std::make_shared<SetPayload>();
    sp->view = view;
    sp->backing_obj = map_obj;
    sp->unmodifiable = true;
    const char *cls =
        (view == SetViewKind::Values) ? "java/util/AbstractCollection" : "java/util/HashSet";
    return MakeSyntheticObj(cls, sp);
}

jobject CreateMapIterator(jobject owner_obj, SetViewKind view,
                          const std::shared_ptr<MapPayload> &mp)
{
    auto it = std::make_shared<IteratorPayload>();
    it->source_type = (view == SetViewKind::DirectMap) ? IteratorPayload::SourceType::Set
                                                       : IteratorPayload::SourceType::Map;
    it->view_kind = view;
    it->owner_obj = owner_obj;
    it->expected_mod = mp->mod_count;
    it->snapshot.reserve(mp->entries.size());
    for (const auto &kv : mp->entries) {
        if (view == SetViewKind::Keys || view == SetViewKind::DirectMap) {
            it->snapshot.push_back(kv.first);
        } else if (view == SetViewKind::Values) {
            it->snapshot.push_back(kv.second);
        } else {
            it->snapshot.push_back(MakeSimpleEntry(kv.first, kv.second));
        }
    }
    return MakeSyntheticObj("java/util/Iterator", it);
}

DexValue HandleList(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                    bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto &jni = JniEnvironment::Instance();
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "<init>") {
        auto lp = GetOrCreatePayload<ListPayload>(self, "java/util/ArrayList");
        std::vector<jobject> init_items;
        if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
            ref.param_types[0] == "Ljava/util/Collection;") {
            init_items =
                SnapshotCollection(vm, reinterpret_cast<jobject>(arg_regs[1]), env, out_exception);
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        lp->items = std::move(init_items);
        if (syn->class_name == "java/util/LinkedList") {
            lp->kind = ListKind::Linked;
        }
        handled = true;
        return {0, 'V'};
    }

    auto lp = GetOrCreatePayload<ListPayload>(self, "java/util/ArrayList");
    if (!lp) {
        return {0, 'V'};
    }

    if (ref.name == "size") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {static_cast<uint64_t>(lp->items.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {lp->items.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, 'V'};
        }
        lp->items.clear();
        lp->mod_count++;
        return {0, 'V'};
    }
    if (ref.name == "add") {
        handled = true;
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, ref.return_type == "Z" ? 'Z' : 'V'};
        }
        if (ref.param_types.size() == 2 && ref.param_types[0] == "I" && arg_regs.size() >= 3) {
            int32_t idx = static_cast<int32_t>(arg_regs[1]);
            if (idx < 0 || static_cast<size_t>(idx) > lp->items.size()) {
                out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                             "Index out of bounds");
                return {0, 'V'};
            }
            lp->items.insert(lp->items.begin() + idx, reinterpret_cast<jobject>(arg_regs[2]));
            lp->mod_count++;
            return {0, 'V'};
        }
        if (arg_regs.size() >= 2) {
            lp->items.push_back(reinterpret_cast<jobject>(arg_regs[1]));
            lp->mod_count++;
            return {1ULL, 'Z'};
        }
    }
    if (ref.name == "addFirst" || ref.name == "push" || ref.name == "offerFirst") {
        if (arg_regs.size() >= 2) {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            handled = true;
            if (lp->unmodifiable) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/UnsupportedOperationException", "Unmodifiable list");
                return {0, ref.return_type == "Z" ? 'Z' : 'V'};
            }
            lp->items.insert(lp->items.begin(), reinterpret_cast<jobject>(arg_regs[1]));
            lp->mod_count++;
            return {ref.return_type == "Z" ? 1ULL : 0ULL, ref.return_type == "Z" ? 'Z' : 'V'};
        }
    }
    if (ref.name == "addLast" || ref.name == "offer" || ref.name == "offerLast") {
        if (arg_regs.size() >= 2) {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            handled = true;
            if (lp->unmodifiable) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/UnsupportedOperationException", "Unmodifiable list");
                return {0, ref.return_type == "Z" ? 'Z' : 'V'};
            }
            lp->items.push_back(reinterpret_cast<jobject>(arg_regs[1]));
            lp->mod_count++;
            return {ref.return_type == "Z" ? 1ULL : 0ULL, ref.return_type == "Z" ? 'Z' : 'V'};
        }
    }
    if (ref.name == "get" && arg_regs.size() >= 2) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        if (idx < 0 || static_cast<size_t>(idx) >= lp->items.size()) {
            out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                         "Index out of bounds");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(lp->items[static_cast<size_t>(idx)]), 'L'};
    }
    if (ref.name == "getFirst" || ref.name == "element" || ref.name == "peek" ||
        ref.name == "peekFirst") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->items.empty()) {
            if (ref.name == "peek" || ref.name == "peekFirst") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "List is empty");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(lp->items.front()), 'L'};
    }
    if (ref.name == "getLast" || ref.name == "peekLast") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->items.empty()) {
            if (ref.name == "peekLast") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "List is empty");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(lp->items.back()), 'L'};
    }
    if ((ref.name == "removeFirst" || ref.name == "pop" || ref.name == "poll" ||
         ref.name == "pollFirst") ||
        (ref.name == "remove" && ref.param_types.empty())) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->items.empty()) {
            if (ref.name == "poll" || ref.name == "pollFirst") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "List is empty");
            return {0, 'L'};
        }
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, 'L'};
        }
        jobject v = lp->items.front();
        lp->items.erase(lp->items.begin());
        lp->mod_count++;
        return {reinterpret_cast<uintptr_t>(v), 'L'};
    }
    if (ref.name == "removeLast" || ref.name == "pollLast") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->items.empty()) {
            if (ref.name == "pollLast") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "List is empty");
            return {0, 'L'};
        }
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, 'L'};
        }
        jobject v = lp->items.back();
        lp->items.pop_back();
        lp->mod_count++;
        return {reinterpret_cast<uintptr_t>(v), 'L'};
    }
    if (ref.name == "set" && arg_regs.size() >= 3) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, 'L'};
        }
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        if (idx < 0 || static_cast<size_t>(idx) >= lp->items.size()) {
            out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                         "Index out of bounds");
            return {0, 'L'};
        }
        jobject old = lp->items[static_cast<size_t>(idx)];
        lp->items[static_cast<size_t>(idx)] = reinterpret_cast<jobject>(arg_regs[2]);
        return {reinterpret_cast<uintptr_t>(old), 'L'};
    }
    if (ref.name == "remove" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        handled = true;
        if (ref.param_types[0] == "I") {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (lp->unmodifiable) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/UnsupportedOperationException", "Unmodifiable list");
                return {0, 'L'};
            }
            int32_t idx = static_cast<int32_t>(arg_regs[1]);
            if (idx < 0 || static_cast<size_t>(idx) >= lp->items.size()) {
                out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                             "Index out of bounds");
                return {0, 'L'};
            }
            jobject old = lp->items[static_cast<size_t>(idx)];
            lp->items.erase(lp->items.begin() + idx);
            lp->mod_count++;
            return {reinterpret_cast<uintptr_t>(old), 'L'};
        }
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            snap = lp->items;
        }
        jobject target = reinterpret_cast<jobject>(arg_regs[1]);
        for (size_t i = 0; i < snap.size(); ++i) {
            bool eq = JavaRuntime::ComputeObjectEquals(vm, snap[i], target, env, out_exception);
            if (out_exception) {
                return {0, 'Z'};
            }
            if (eq) {
                std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
                if (lp->unmodifiable) {
                    out_exception = JavaRuntime::CreateException(
                        "java/lang/UnsupportedOperationException", "Unmodifiable list");
                    return {0, 'Z'};
                }
                if (i < lp->items.size()) {
                    lp->items.erase(lp->items.begin() + static_cast<ptrdiff_t>(i));
                    lp->mod_count++;
                    return {1ULL, 'Z'};
                }
            }
        }
        return {0ULL, 'Z'};
    }
    if (ref.name == "addAll" && arg_regs.size() >= 2) {
        jobject coll = reinterpret_cast<jobject>(
            (ref.param_types.size() == 2 && arg_regs.size() >= 3) ? arg_regs[2] : arg_regs[1]);
        auto extra = SnapshotCollection(vm, coll, env, out_exception);
        handled = true;
        if (out_exception) {
            return {0, 'Z'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (lp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable list");
            return {0, 'Z'};
        }
        if (extra.empty()) {
            return {0ULL, 'Z'};
        }
        if (ref.param_types.size() == 2 && ref.param_types[0] == "I") {
            int32_t idx = static_cast<int32_t>(arg_regs[1]);
            if (idx < 0 || static_cast<size_t>(idx) > lp->items.size()) {
                out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                             "Index out of bounds");
                return {0, 'Z'};
            }
            lp->items.insert(lp->items.begin() + idx, extra.begin(), extra.end());
        } else {
            lp->items.insert(lp->items.end(), extra.begin(), extra.end());
        }
        lp->mod_count++;
        return {1ULL, 'Z'};
    }
    if ((ref.name == "indexOf" || ref.name == "lastIndexOf" || ref.name == "contains") &&
        arg_regs.size() >= 2) {
        handled = true;
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            snap = lp->items;
        }
        jobject target = reinterpret_cast<jobject>(arg_regs[1]);
        if (ref.name == "lastIndexOf") {
            for (size_t idx = snap.size(); idx > 0; --idx) {
                bool eq =
                    JavaRuntime::ComputeObjectEquals(vm, snap[idx - 1], target, env, out_exception);
                if (out_exception) {
                    return {0, 'I'};
                }
                if (eq) {
                    return {static_cast<uint64_t>(static_cast<uint32_t>(idx - 1)), 'I'};
                }
            }
            return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
        }
        for (size_t i = 0; i < snap.size(); ++i) {
            bool eq = JavaRuntime::ComputeObjectEquals(vm, snap[i], target, env, out_exception);
            if (out_exception) {
                return {0, ref.name == "contains" ? 'Z' : 'I'};
            }
            if (eq) {
                if (ref.name == "contains") {
                    return {1ULL, 'Z'};
                }
                return {static_cast<uint64_t>(static_cast<uint32_t>(i)), 'I'};
            }
        }
        if (ref.name == "contains") {
            return {0ULL, 'Z'};
        }
        return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
    }
    if (ref.name == "containsAll" && arg_regs.size() >= 2) {
        handled = true;
        auto need =
            SnapshotCollection(vm, reinterpret_cast<jobject>(arg_regs[1]), env, out_exception);
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            snap = lp->items;
        }
        for (jobject q : need) {
            bool found = false;
            for (jobject item : snap) {
                if (JavaRuntime::ComputeObjectEquals(vm, item, q, env, out_exception)) {
                    found = true;
                    break;
                }
                if (out_exception) {
                    return {0, 'Z'};
                }
            }
            if (!found) {
                return {0ULL, 'Z'};
            }
        }
        return {1ULL, 'Z'};
    }
    if (ref.name == "subList" && arg_regs.size() >= 3) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        int32_t from = static_cast<int32_t>(arg_regs[1]);
        int32_t to = static_cast<int32_t>(arg_regs[2]);
        if (from < 0 || to < from || static_cast<size_t>(to) > lp->items.size()) {
            out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                         "subList range invalid");
            return {0, 'L'};
        }
        auto sub = std::make_shared<ListPayload>();
        sub->items.assign(lp->items.begin() + from, lp->items.begin() + to);
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/ArrayList", sub)), 'L'};
    }
    if (ref.name == "sort" && arg_regs.size() >= 2) {
        handled = true;
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (lp->unmodifiable) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/UnsupportedOperationException", "Unmodifiable list");
                return {0, 'V'};
            }
            snap = lp->items;
        }
        jobject comp = reinterpret_cast<jobject>(arg_regs[1]);
        std::stable_sort(snap.begin(), snap.end(), [&](jobject a, jobject b) {
            if (out_exception) {
                return false;
            }
            return CompareKeys(vm, comp, a, b, env, out_exception) < 0;
        });
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        lp->items = std::move(snap);
        lp->mod_count++;
        return {0, 'V'};
    }
    if (ref.name == "toArray") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        jobject target = (arg_regs.size() >= 2) ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        return {reinterpret_cast<uintptr_t>(PackObjectArray(lp->items, target)), 'L'};
    }
    if (ref.name == "iterator" || ref.name == "listIterator") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        size_t cursor = (ref.name == "listIterator" && arg_regs.size() >= 2)
                            ? static_cast<size_t>(std::max(0, static_cast<int32_t>(arg_regs[1])))
                            : 0;
        return {reinterpret_cast<uintptr_t>(CreateListIterator(self, lp, cursor)), 'L'};
    }
    if (ref.name == "toString") {
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            snap = lp->items;
        }
        std::string out = "[";
        for (size_t i = 0; i < snap.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += JavaRuntime::ObjectToString(vm, snap[i], env, out_exception);
            if (out_exception) {
                break;
            }
        }
        out.push_back(']');
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleArrayDeque(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                          bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = JniEnvironment::Instance().LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "<init>") {
        auto dp = GetOrCreatePayload<DequePayload>(self, "java/util/ArrayDeque");
        std::vector<jobject> init_items;
        if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
            ref.param_types[0] == "Ljava/util/Collection;") {
            init_items =
                SnapshotCollection(vm, reinterpret_cast<jobject>(arg_regs[1]), env, out_exception);
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        dp->items.assign(init_items.begin(), init_items.end());
        handled = true;
        return {0, 'V'};
    }

    auto dp = GetOrCreatePayload<DequePayload>(self, "java/util/ArrayDeque");
    if (!dp) {
        return {0, 'V'};
    }

    if (ref.name == "size") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {static_cast<uint64_t>(dp->items.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {dp->items.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        dp->items.clear();
        dp->mod_count++;
        handled = true;
        return {0, 'V'};
    }
    if ((ref.name == "addFirst" || ref.name == "offerFirst" || ref.name == "push") &&
        arg_regs.size() >= 2) {
        jobject v = reinterpret_cast<jobject>(arg_regs[1]);
        handled = true;
        if (!v) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "ArrayDeque does not allow null");
            return {0, ref.return_type == "Z" ? 'Z' : 'V'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        dp->items.push_front(v);
        dp->mod_count++;
        return {ref.return_type == "Z" ? 1ULL : 0ULL, ref.return_type == "Z" ? 'Z' : 'V'};
    }
    if ((ref.name == "add" || ref.name == "addLast" || ref.name == "offer" ||
         ref.name == "offerLast") &&
        arg_regs.size() >= 2) {
        jobject v = reinterpret_cast<jobject>(arg_regs[1]);
        handled = true;
        if (!v) {
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                         "ArrayDeque does not allow null");
            return {0, ref.return_type == "Z" ? 'Z' : 'V'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        dp->items.push_back(v);
        dp->mod_count++;
        return {ref.return_type == "Z" ? 1ULL : 0ULL, ref.return_type == "Z" ? 'Z' : 'V'};
    }
    if (ref.name == "peek" || ref.name == "peekFirst" || ref.name == "getFirst" ||
        ref.name == "element") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (dp->items.empty()) {
            if (ref.name == "peek" || ref.name == "peekFirst") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Deque is empty");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(dp->items.front()), 'L'};
    }
    if (ref.name == "peekLast" || ref.name == "getLast") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (dp->items.empty()) {
            if (ref.name == "peekLast") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Deque is empty");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(dp->items.back()), 'L'};
    }
    if (ref.name == "poll" || ref.name == "pollFirst" || ref.name == "pop" ||
        ref.name == "removeFirst" || (ref.name == "remove" && ref.param_types.empty())) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (dp->items.empty()) {
            if (ref.name == "poll" || ref.name == "pollFirst") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Deque is empty");
            return {0, 'L'};
        }
        jobject v = dp->items.front();
        dp->items.pop_front();
        dp->mod_count++;
        return {reinterpret_cast<uintptr_t>(v), 'L'};
    }
    if (ref.name == "pollLast" || ref.name == "removeLast") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (dp->items.empty()) {
            if (ref.name == "pollLast") {
                return {0, 'L'};
            }
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Deque is empty");
            return {0, 'L'};
        }
        jobject v = dp->items.back();
        dp->items.pop_back();
        dp->mod_count++;
        return {reinterpret_cast<uintptr_t>(v), 'L'};
    }
    if (ref.name == "iterator") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        auto it = std::make_shared<IteratorPayload>();
        it->source_type = IteratorPayload::SourceType::Deque;
        it->owner_obj = self;
        it->expected_mod = dp->mod_count;
        it->snapshot.assign(dp->items.begin(), dp->items.end());
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/Iterator", it)), 'L'};
    }
    if (ref.name == "toArray") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        std::vector<jobject> v(dp->items.begin(), dp->items.end());
        jobject target = (arg_regs.size() >= 2) ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        return {reinterpret_cast<uintptr_t>(PackObjectArray(v, target)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleMap(DexVm &vm, std::string_view class_slash, const DexMethodRef &ref,
                   std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                   jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto &jni = JniEnvironment::Instance();
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "<init>") {
        auto mp = GetOrCreatePayload<MapPayload>(self, class_slash);
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (class_slash == "java/util/LinkedHashMap") {
                mp->kind = MapKind::Linked;
                if (arg_regs.size() >= 4 && ref.param_types.size() == 3 &&
                    ref.param_types[2] == "Z") {
                    mp->access_order = (arg_regs[3] != 0);
                }
            } else if (class_slash == "java/util/TreeMap") {
                mp->kind = MapKind::Tree;
                if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
                    ref.param_types[0] == "Ljava/util/Comparator;") {
                    mp->comparator = reinterpret_cast<jobject>(arg_regs[1]);
                }
            } else {
                mp->kind = MapKind::Hash;
            }
        }
        if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
            ref.param_types[0] == "Ljava/util/Map;") {
            jobject other_map = reinterpret_cast<jobject>(arg_regs[1]);
            if (auto omp = GetPayload<MapPayload>(other_map)) {
                auto osyn = jni.LookupObject(other_map);
                std::lock_guard<std::recursive_mutex> olk(osyn->field_mutex);
                for (const auto &kv : omp->entries) {
                    DexMethodRef put_ref{};
                    put_ref.name = "put";
                    put_ref.return_type = "Ljava/lang/Object;";
                    uint64_t pargs[3] = {reinterpret_cast<uintptr_t>(self),
                                         reinterpret_cast<uintptr_t>(kv.first),
                                         reinterpret_cast<uintptr_t>(kv.second)};
                    HandleMap(vm, class_slash, put_ref, pargs, false, env, out_exception, handled);
                }
            }
        }
        handled = true;
        return {0, 'V'};
    }

    auto mp = GetOrCreatePayload<MapPayload>(self, class_slash);
    if (!mp) {
        return {0, 'V'};
    }

    if (ref.name == "size") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {static_cast<uint64_t>(mp->entries.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {mp->entries.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable map");
            return {0, 'V'};
        }
        mp->entries.clear();
        mp->mod_count++;
        return {0, 'V'};
    }
    if ((ref.name == "put" || ref.name == "putIfAbsent") && arg_regs.size() >= 3) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        jobject v = reinterpret_cast<jobject>(arg_regs[2]);
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, k, self, env, out_exception);
        if (out_exception) {
            return {0, 'L'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable map");
            return {0, 'L'};
        }
        if (idx >= 0 && static_cast<size_t>(idx) < mp->entries.size()) {
            jobject old = mp->entries[static_cast<size_t>(idx)].second;
            if (ref.name == "put" || old == nullptr) {
                mp->entries[static_cast<size_t>(idx)].second = v;
            }
            if (mp->kind == MapKind::Linked && mp->access_order) {
                auto entry = mp->entries[static_cast<size_t>(idx)];
                mp->entries.erase(mp->entries.begin() + idx);
                mp->entries.push_back(entry);
            }
            return {reinterpret_cast<uintptr_t>(old), 'L'};
        }
        if (mp->kind == MapKind::Tree) {
            size_t pos = 0;
            jobject comp = mp->comparator;
            while (pos < mp->entries.size()) {
                int cmp = CompareKeys(vm, comp, mp->entries[pos].first, k, env, out_exception);
                if (out_exception) {
                    return {0, 'L'};
                }
                if (cmp > 0) {
                    break;
                }
                ++pos;
            }
            mp->entries.insert(mp->entries.begin() + static_cast<ptrdiff_t>(pos), {k, v});
        } else {
            mp->entries.emplace_back(k, v);
        }
        mp->mod_count++;
        return {0, 'L'};
    }
    if ((ref.name == "get" || ref.name == "getOrDefault") && arg_regs.size() >= 2) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        jobject def_val = (ref.name == "getOrDefault" && arg_regs.size() >= 3)
                              ? reinterpret_cast<jobject>(arg_regs[2])
                              : nullptr;
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, k, self, env, out_exception);
        if (out_exception || idx < 0) {
            return {reinterpret_cast<uintptr_t>(def_val), 'L'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (static_cast<size_t>(idx) < mp->entries.size()) {
            jobject val = mp->entries[static_cast<size_t>(idx)].second;
            if (mp->kind == MapKind::Linked && mp->access_order) {
                auto entry = mp->entries[static_cast<size_t>(idx)];
                mp->entries.erase(mp->entries.begin() + idx);
                mp->entries.push_back(entry);
            }
            return {reinterpret_cast<uintptr_t>(val), 'L'};
        }
        return {reinterpret_cast<uintptr_t>(def_val), 'L'};
    }
    if (ref.name == "containsKey" && arg_regs.size() >= 2) {
        handled = true;
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, reinterpret_cast<jobject>(arg_regs[1]), self, env,
                                          out_exception);
        return {(!out_exception && idx >= 0) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "containsValue" && arg_regs.size() >= 2) {
        handled = true;
        std::vector<jobject> vals;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            for (const auto &kv : mp->entries) {
                vals.push_back(kv.second);
            }
        }
        jobject target = reinterpret_cast<jobject>(arg_regs[1]);
        for (jobject v : vals) {
            if (JavaRuntime::ComputeObjectEquals(vm, v, target, env, out_exception)) {
                return {1ULL, 'Z'};
            }
            if (out_exception) {
                return {0, 'Z'};
            }
        }
        return {0ULL, 'Z'};
    }
    if (ref.name == "remove" && arg_regs.size() >= 2) {
        handled = true;
        jobject k = reinterpret_cast<jobject>(arg_regs[1]);
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, k, self, env, out_exception);
        if (out_exception || idx < 0) {
            return {0, 'L'};
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable map");
            return {0, 'L'};
        }
        if (static_cast<size_t>(idx) < mp->entries.size()) {
            jobject old = mp->entries[static_cast<size_t>(idx)].second;
            mp->entries.erase(mp->entries.begin() + idx);
            mp->mod_count++;
            return {reinterpret_cast<uintptr_t>(old), 'L'};
        }
        return {0, 'L'};
    }
    if (ref.name == "putAll" && arg_regs.size() >= 2) {
        handled = true;
        if (auto omp = GetPayload<MapPayload>(reinterpret_cast<jobject>(arg_regs[1]))) {
            auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            std::vector<std::pair<jobject, jobject>> items;
            {
                std::lock_guard<std::recursive_mutex> olk(osyn->field_mutex);
                items = omp->entries;
            }
            DexMethodRef put_ref{};
            put_ref.name = "put";
            put_ref.return_type = "Ljava/lang/Object;";
            for (const auto &kv : items) {
                uint64_t pargs[3] = {reinterpret_cast<uintptr_t>(self),
                                     reinterpret_cast<uintptr_t>(kv.first),
                                     reinterpret_cast<uintptr_t>(kv.second)};
                bool ph = false;
                HandleMap(vm, class_slash, put_ref, pargs, false, env, out_exception, ph);
                if (out_exception) {
                    break;
                }
            }
        }
        return {0, 'V'};
    }
    if (ref.name == "firstKey" || ref.name == "lastKey") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        if (mp->entries.empty()) {
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Map is empty");
            return {0, 'L'};
        }
        jobject k = (ref.name == "firstKey") ? mp->entries.front().first : mp->entries.back().first;
        return {reinterpret_cast<uintptr_t>(k), 'L'};
    }
    if (ref.name == "comparator") {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        handled = true;
        return {reinterpret_cast<uintptr_t>(mp->comparator), 'L'};
    }
    if (ref.name == "keySet") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateMapView(self, SetViewKind::Keys)), 'L'};
    }
    if (ref.name == "values") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateMapView(self, SetViewKind::Values)), 'L'};
    }
    if (ref.name == "entrySet") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateMapView(self, SetViewKind::Entries)), 'L'};
    }
    if (ref.name == "toString") {
        std::vector<std::pair<jobject, jobject>> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            snap = mp->entries;
        }
        std::string out = "{";
        for (size_t i = 0; i < snap.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += JavaRuntime::ObjectToString(vm, snap[i].first, env, out_exception);
            out.push_back('=');
            out += JavaRuntime::ObjectToString(vm, snap[i].second, env, out_exception);
        }
        out.push_back('}');
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleSet(DexVm &vm, std::string_view class_slash, const DexMethodRef &ref,
                   std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                   jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto &jni = JniEnvironment::Instance();
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }

    if (ref.name == "<init>") {
        auto sp = GetOrCreatePayload<SetPayload>(self, class_slash);
        auto mp = std::make_shared<MapPayload>();
        if (class_slash == "java/util/LinkedHashSet") {
            mp->kind = MapKind::Linked;
        } else if (class_slash == "java/util/TreeSet") {
            mp->kind = MapKind::Tree;
            if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
                ref.param_types[0] == "Ljava/util/Comparator;") {
                mp->comparator = reinterpret_cast<jobject>(arg_regs[1]);
            }
        }
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            sp->view = SetViewKind::DirectMap;
            sp->backing_obj = self;
            sp->owned_map = mp;
        }
        if (arg_regs.size() >= 2 && !ref.param_types.empty() &&
            ref.param_types[0] == "Ljava/util/Collection;") {
            auto items =
                SnapshotCollection(vm, reinterpret_cast<jobject>(arg_regs[1]), env, out_exception);
            DexMethodRef add_ref{};
            add_ref.name = "add";
            add_ref.return_type = "Z";
            for (jobject item : items) {
                uint64_t pargs[2] = {reinterpret_cast<uintptr_t>(self),
                                     reinterpret_cast<uintptr_t>(item)};
                bool ah = false;
                HandleSet(vm, class_slash, add_ref, pargs, false, env, out_exception, ah);
                if (out_exception) {
                    break;
                }
            }
        }
        handled = true;
        return {0, 'V'};
    }

    auto sp = GetOrCreatePayload<SetPayload>(self, class_slash);
    if (!sp) {
        return {0, 'V'};
    }
    jobject map_obj = self;
    std::shared_ptr<MapPayload> mp;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (sp->view == SetViewKind::DirectMap) {
            if (!sp->owned_map) {
                sp->owned_map = std::make_shared<MapPayload>();
            }
            mp = sp->owned_map;
            map_obj = self;
        } else {
            map_obj = sp->backing_obj;
            mp = GetPayload<MapPayload>(map_obj);
        }
    }
    if (!mp) {
        return {0, 'V'};
    }
    auto map_syn = jni.LookupObject(map_obj);

    if (ref.name == "size") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        return {static_cast<uint64_t>(mp->entries.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        return {mp->entries.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "clear") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        if (sp->unmodifiable || mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable set");
            return {0, 'V'};
        }
        mp->entries.clear();
        mp->mod_count++;
        return {0, 'V'};
    }
    if (ref.name == "add" && arg_regs.size() >= 2) {
        handled = true;
        if (sp->unmodifiable || mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable set");
            return {0, 'Z'};
        }
        jobject elem = reinterpret_cast<jobject>(arg_regs[1]);
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, elem, map_obj, env, out_exception);
        if (out_exception || idx >= 0) {
            return {0ULL, 'Z'};
        }
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        if (mp->kind == MapKind::Tree) {
            size_t pos = 0;
            jobject comp = mp->comparator;
            while (pos < mp->entries.size()) {
                int cmp = CompareKeys(vm, comp, mp->entries[pos].first, elem, env, out_exception);
                if (out_exception) {
                    return {0, 'Z'};
                }
                if (cmp > 0) {
                    break;
                }
                ++pos;
            }
            mp->entries.insert(mp->entries.begin() + static_cast<ptrdiff_t>(pos),
                               {elem, java_rt::BoxBool(true)});
        } else {
            mp->entries.emplace_back(elem, java_rt::BoxBool(true));
        }
        mp->mod_count++;
        return {1ULL, 'Z'};
    }
    if (ref.name == "contains" && arg_regs.size() >= 2) {
        handled = true;
        jobject elem = reinterpret_cast<jobject>(arg_regs[1]);
        if (sp->view == SetViewKind::Values) {
            DexMethodRef cv_ref = ref;
            cv_ref.name = "containsValue";
            uint64_t cargs[2] = {reinterpret_cast<uintptr_t>(map_obj),
                                 reinterpret_cast<uintptr_t>(elem)};
            return HandleMap(vm, "java/util/HashMap", cv_ref, cargs, false, env, out_exception,
                             handled);
        }
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, elem, map_obj, env, out_exception);
        return {(!out_exception && idx >= 0) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "remove" && arg_regs.size() >= 2) {
        handled = true;
        if (sp->unmodifiable || mp->unmodifiable) {
            out_exception = JavaRuntime::CreateException("java/lang/UnsupportedOperationException",
                                                         "Unmodifiable set");
            return {0, 'Z'};
        }
        jobject elem = reinterpret_cast<jobject>(arg_regs[1]);
        ptrdiff_t idx = FindMapEntryIndex(vm, mp, elem, map_obj, env, out_exception);
        if (out_exception || idx < 0) {
            return {0ULL, 'Z'};
        }
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        if (static_cast<size_t>(idx) < mp->entries.size()) {
            mp->entries.erase(mp->entries.begin() + idx);
            mp->mod_count++;
            return {1ULL, 'Z'};
        }
        return {0ULL, 'Z'};
    }
    if (ref.name == "first" || ref.name == "last") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        if (mp->entries.empty()) {
            out_exception =
                JavaRuntime::CreateException("java/util/NoSuchElementException", "Set is empty");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(ref.name == "first" ? mp->entries.front().first
                                                                : mp->entries.back().first),
                'L'};
    }
    if (ref.name == "iterator") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        return {reinterpret_cast<uintptr_t>(CreateMapIterator(map_obj, sp->view, mp)), 'L'};
    }
    if (ref.name == "toArray") {
        std::lock_guard<std::recursive_mutex> lk(map_syn->field_mutex);
        handled = true;
        std::vector<jobject> items;
        items.reserve(mp->entries.size());
        for (const auto &kv : mp->entries) {
            if (sp->view == SetViewKind::Values) {
                items.push_back(kv.second);
            } else if (sp->view == SetViewKind::Entries) {
                items.push_back(MakeSimpleEntry(kv.first, kv.second));
            } else {
                items.push_back(kv.first);
            }
        }
        jobject target = (arg_regs.size() >= 2) ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        return {reinterpret_cast<uintptr_t>(PackObjectArray(items, target)), 'L'};
    }
    return {0, 'V'};
}

uint64_t CurrentOwnerModCount(jobject owner, IteratorPayload::SourceType st)
{
    if (!owner) {
        return 0;
    }
    auto syn = JniEnvironment::Instance().LookupObject(owner);
    if (!syn) {
        return 0;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (st == IteratorPayload::SourceType::List) {
        if (auto lp = std::static_pointer_cast<ListPayload>(syn->runtime_payload)) {
            return lp->mod_count;
        }
    } else if (st == IteratorPayload::SourceType::Deque) {
        if (auto dp = std::static_pointer_cast<DequePayload>(syn->runtime_payload)) {
            return dp->mod_count;
        }
    } else if (st == IteratorPayload::SourceType::Set) {
        if (auto sp = std::static_pointer_cast<SetPayload>(syn->runtime_payload)) {
            if (sp->owned_map) {
                return sp->owned_map->mod_count;
            }
        }
    } else if (st == IteratorPayload::SourceType::Map) {
        if (auto mp = std::static_pointer_cast<MapPayload>(syn->runtime_payload)) {
            return mp->mod_count;
        }
    }
    return 0;
}

DexValue HandleIterator(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                        bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = JniEnvironment::Instance().LookupObject(self);
    auto it = GetPayload<IteratorPayload>(self);
    if (!syn || !it) {
        return {0, 'V'};
    }

    auto check_comod = [&]() -> bool {
        if (CurrentOwnerModCount(it->owner_obj, it->source_type) != it->expected_mod) {
            out_exception = JavaRuntime::CreateException(
                "java/util/ConcurrentModificationException", "Concurrent modification detected");
            return false;
        }
        return true;
    };

    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (ref.name == "hasNext") {
        handled = true;
        return {it->cursor < it->snapshot.size() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "next") {
        handled = true;
        if (!check_comod()) {
            return {0, 'L'};
        }
        if (it->cursor >= it->snapshot.size()) {
            out_exception = JavaRuntime::CreateException("java/util/NoSuchElementException",
                                                         "Iterator exhausted");
            return {0, 'L'};
        }
        it->last_ret = static_cast<int64_t>(it->cursor);
        return {reinterpret_cast<uintptr_t>(it->snapshot[it->cursor++]), 'L'};
    }
    if (ref.name == "hasPrevious") {
        handled = true;
        return {it->cursor > 0 ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "previous") {
        handled = true;
        if (!check_comod()) {
            return {0, 'L'};
        }
        if (it->cursor == 0) {
            out_exception = JavaRuntime::CreateException("java/util/NoSuchElementException",
                                                         "Iterator at start");
            return {0, 'L'};
        }
        --it->cursor;
        it->last_ret = static_cast<int64_t>(it->cursor);
        return {reinterpret_cast<uintptr_t>(it->snapshot[it->cursor]), 'L'};
    }
    if (ref.name == "nextIndex") {
        handled = true;
        return {static_cast<uint64_t>(it->cursor), 'I'};
    }
    if (ref.name == "previousIndex") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<int64_t>(it->cursor) - 1), 'I'};
    }
    if (ref.name == "remove") {
        handled = true;
        if (!check_comod()) {
            return {0, 'V'};
        }
        if (it->last_ret < 0 || static_cast<size_t>(it->last_ret) >= it->snapshot.size()) {
            out_exception = JavaRuntime::CreateException("java/lang/IllegalStateException",
                                                         "No current element to remove");
            return {0, 'V'};
        }
        size_t idx = static_cast<size_t>(it->last_ret);
        it->snapshot.erase(it->snapshot.begin() + static_cast<ptrdiff_t>(idx));
        if (idx < it->cursor) {
            --it->cursor;
        }
        it->last_ret = -1;
        if (it->source_type == IteratorPayload::SourceType::List && it->owner_obj) {
            if (auto osyn = JniEnvironment::Instance().LookupObject(it->owner_obj)) {
                std::lock_guard<std::recursive_mutex> olk(osyn->field_mutex);
                if (auto lp = std::static_pointer_cast<ListPayload>(osyn->runtime_payload)) {
                    if (lp->unmodifiable) {
                        out_exception = JavaRuntime::CreateException(
                            "java/lang/UnsupportedOperationException", "Unmodifiable list");
                        return {0, 'V'};
                    }
                    if (idx < lp->items.size()) {
                        lp->items.erase(lp->items.begin() + static_cast<ptrdiff_t>(idx));
                        lp->mod_count++;
                        it->expected_mod = lp->mod_count;
                    }
                }
            }
        }
        return {0, 'V'};
    }
    return {0, 'V'};
}

DexValue HandleMapEntry(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                        bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    (void)out_exception;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (ref.name == "<init>" && arg_regs.size() >= 3) {
        syn->object_fields["key"] = reinterpret_cast<jobject>(arg_regs[1]);
        syn->object_fields["value"] = reinterpret_cast<jobject>(arg_regs[2]);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "getKey") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(syn->object_fields["key"]), 'L'};
    }
    if (ref.name == "getValue") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(syn->object_fields["value"]), 'L'};
    }
    if (ref.name == "setValue" && arg_regs.size() >= 2) {
        jobject old = syn->object_fields["value"];
        syn->object_fields["value"] = reinterpret_cast<jobject>(arg_regs[1]);
        handled = true;
        return {reinterpret_cast<uintptr_t>(old), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleArrays(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (!is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    jobject arr_obj = reinterpret_cast<jobject>(arg_regs[0]);
    auto arr = jni.LookupObject(arr_obj);

    if (ref.name == "asList") {
        handled = true;
        auto lp = std::make_shared<ListPayload>();
        if (arr && arr->type == JniObjectType::ObjectArray) {
            std::lock_guard<std::recursive_mutex> lk(arr->field_mutex);
            lp->items = arr->object_data;
        }
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/ArrayList", lp)), 'L'};
    }
    if (!arr) {
        return {0, 'V'};
    }
    if (ref.name == "sort") {
        handled = true;
        std::lock_guard<std::recursive_mutex> lk(arr->field_mutex);
        if (arr->type == JniObjectType::IntArray) {
            std::sort(arr->int_data.begin(), arr->int_data.end());
        } else if (arr->type == JniObjectType::LongArray) {
            std::sort(arr->long_data.begin(), arr->long_data.end());
        } else if (arr->type == JniObjectType::FloatArray) {
            std::sort(arr->float_data.begin(), arr->float_data.end());
        } else if (arr->type == JniObjectType::DoubleArray) {
            std::sort(arr->double_data.begin(), arr->double_data.end());
        } else if (arr->type == JniObjectType::ByteArray) {
            std::sort(arr->byte_data.begin(), arr->byte_data.end());
        } else if (arr->type == JniObjectType::CharArray) {
            std::sort(arr->char_data.begin(), arr->char_data.end());
        } else if (arr->type == JniObjectType::ShortArray) {
            std::sort(arr->short_data.begin(), arr->short_data.end());
        } else if (arr->type == JniObjectType::ObjectArray) {
            jobject comp = (arg_regs.size() >= 2 && ref.param_types.size() == 2 &&
                            ref.param_types[1] == "Ljava/util/Comparator;")
                               ? reinterpret_cast<jobject>(arg_regs[1])
                               : nullptr;
            std::vector<jobject> snap = arr->object_data;
            std::stable_sort(snap.begin(), snap.end(), [&](jobject a, jobject b) {
                if (out_exception) {
                    return false;
                }
                return CompareKeys(vm, comp, a, b, env, out_exception) < 0;
            });
            arr->object_data = std::move(snap);
        }
        return {0, 'V'};
    }
    if (ref.name == "fill" && arg_regs.size() >= 2) {
        handled = true;
        std::lock_guard<std::recursive_mutex> lk(arr->field_mutex);
        if (arr->type == JniObjectType::IntArray) {
            std::fill(arr->int_data.begin(), arr->int_data.end(),
                      static_cast<int32_t>(arg_regs[1]));
        } else if (arr->type == JniObjectType::ByteArray) {
            std::fill(arr->byte_data.begin(), arr->byte_data.end(),
                      static_cast<int8_t>(arg_regs[1]));
        } else if (arr->type == JniObjectType::ObjectArray) {
            std::fill(arr->object_data.begin(), arr->object_data.end(),
                      reinterpret_cast<jobject>(arg_regs[1]));
        }
        return {0, 'V'};
    }
    if (ref.name == "copyOf" && arg_regs.size() >= 2) {
        handled = true;
        size_t new_len = static_cast<size_t>(std::max(0, static_cast<int32_t>(arg_regs[1])));
        auto copy = std::make_shared<JniSyntheticObject>();
        std::lock_guard<std::recursive_mutex> lk(arr->field_mutex);
        copy->type = arr->type;
        copy->class_name = arr->class_name;
        if (arr->type == JniObjectType::IntArray) {
            copy->int_data = arr->int_data;
            copy->int_data.resize(new_len, 0);
        } else if (arr->type == JniObjectType::ByteArray) {
            copy->byte_data = arr->byte_data;
            copy->byte_data.resize(new_len, 0);
        } else if (arr->type == JniObjectType::CharArray) {
            copy->char_data = arr->char_data;
            copy->char_data.resize(new_len, 0);
        } else if (arr->type == JniObjectType::ObjectArray) {
            copy->object_data = arr->object_data;
            copy->object_data.resize(new_len, nullptr);
        }
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(copy)), 'L'};
    }
    if (ref.name == "toString") {
        handled = true;
        std::string out = "[";
        std::lock_guard<std::recursive_mutex> lk(arr->field_mutex);
        if (arr->type == JniObjectType::IntArray) {
            for (size_t i = 0; i < arr->int_data.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += std::to_string(arr->int_data[i]);
            }
        } else if (arr->type == JniObjectType::ObjectArray) {
            for (size_t i = 0; i < arr->object_data.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += JavaRuntime::ObjectToString(vm, arr->object_data[i], env, out_exception);
            }
        }
        out.push_back(']');
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        auto arr2 = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        if (!arr2 || arr->type != arr2->type) {
            return {0ULL, 'Z'};
        }
        if (arr->type == JniObjectType::IntArray) {
            return {arr->int_data == arr2->int_data ? 1ULL : 0ULL, 'Z'};
        }
        if (arr->type == JniObjectType::ByteArray) {
            return {arr->byte_data == arr2->byte_data ? 1ULL : 0ULL, 'Z'};
        }
        return {0ULL, 'Z'};
    }
    return {0, 'V'};
}

DexValue HandleCollections(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                           bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (!is_static) {
        return {0, 'V'};
    }
    if (ref.name == "sort" && !arg_regs.empty()) {
        handled = true;
        jobject list_obj = reinterpret_cast<jobject>(arg_regs[0]);
        auto lp = GetPayload<ListPayload>(list_obj);
        auto syn = JniEnvironment::Instance().LookupObject(list_obj);
        if (!lp || !syn) {
            return {0, 'V'};
        }
        std::vector<jobject> snap;
        {
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            if (lp->unmodifiable) {
                out_exception = JavaRuntime::CreateException(
                    "java/lang/UnsupportedOperationException", "Unmodifiable list");
                return {0, 'V'};
            }
            snap = lp->items;
        }
        jobject comp = (arg_regs.size() >= 2) ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        std::stable_sort(snap.begin(), snap.end(), [&](jobject a, jobject b) {
            if (out_exception) {
                return false;
            }
            return CompareKeys(vm, comp, a, b, env, out_exception) < 0;
        });
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        lp->items = std::move(snap);
        lp->mod_count++;
        return {0, 'V'};
    }
    if (ref.name == "reverse" && !arg_regs.empty()) {
        handled = true;
        jobject list_obj = reinterpret_cast<jobject>(arg_regs[0]);
        if (auto lp = GetPayload<ListPayload>(list_obj)) {
            auto syn = JniEnvironment::Instance().LookupObject(list_obj);
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            std::reverse(lp->items.begin(), lp->items.end());
            lp->mod_count++;
        }
        return {0, 'V'};
    }
    if (ref.name == "emptyList") {
        handled = true;
        auto lp = std::make_shared<ListPayload>();
        lp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/ArrayList", lp)), 'L'};
    }
    if (ref.name == "singletonList" && !arg_regs.empty()) {
        handled = true;
        auto lp = std::make_shared<ListPayload>();
        lp->items.push_back(reinterpret_cast<jobject>(arg_regs[0]));
        lp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/ArrayList", lp)), 'L'};
    }
    if (ref.name == "emptyMap") {
        handled = true;
        auto mp = std::make_shared<MapPayload>();
        mp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/HashMap", mp)), 'L'};
    }
    if (ref.name == "singletonMap" && arg_regs.size() >= 2) {
        handled = true;
        auto mp = std::make_shared<MapPayload>();
        mp->entries.emplace_back(reinterpret_cast<jobject>(arg_regs[0]),
                                 reinterpret_cast<jobject>(arg_regs[1]));
        mp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/HashMap", mp)), 'L'};
    }
    if (ref.name == "emptySet") {
        handled = true;
        auto sp = std::make_shared<SetPayload>();
        sp->owned_map = std::make_shared<MapPayload>();
        sp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/HashSet", sp)), 'L'};
    }
    if (ref.name == "singleton" && !arg_regs.empty()) {
        handled = true;
        auto sp = std::make_shared<SetPayload>();
        sp->owned_map = std::make_shared<MapPayload>();
        sp->owned_map->entries.emplace_back(reinterpret_cast<jobject>(arg_regs[0]),
                                            java_rt::BoxBool(true));
        sp->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/HashSet", sp)), 'L'};
    }
    if (ref.name == "unmodifiableList" && !arg_regs.empty()) {
        handled = true;
        auto copy = std::make_shared<ListPayload>();
        if (auto src = GetPayload<ListPayload>(reinterpret_cast<jobject>(arg_regs[0]))) {
            auto syn =
                JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
            copy->items = src->items;
        }
        copy->unmodifiable = true;
        return {reinterpret_cast<uintptr_t>(MakeSyntheticObj("java/util/ArrayList", copy)), 'L'};
    }
    if ((ref.name == "synchronizedList" || ref.name == "synchronizedMap" ||
         ref.name == "synchronizedSet") &&
        !arg_regs.empty()) {
        handled = true;
        return {arg_regs[0], 'L'};
    }
    return {0, 'V'};
}

DexValue HandleObjects(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (!is_static) {
        return {0, 'V'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        bool eq = JavaRuntime::ComputeObjectEquals(vm, reinterpret_cast<jobject>(arg_regs[0]),
                                                   reinterpret_cast<jobject>(arg_regs[1]), env,
                                                   out_exception);
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "hashCode" && !arg_regs.empty()) {
        handled = true;
        int32_t h = JavaRuntime::ComputeObjectHashCode(vm, reinterpret_cast<jobject>(arg_regs[0]),
                                                       env, out_exception);
        return {static_cast<uint64_t>(static_cast<uint32_t>(h)), 'I'};
    }
    if (ref.name == "requireNonNull" && !arg_regs.empty()) {
        handled = true;
        if (!arg_regs[0]) {
            std::string msg = "null object";
            if (arg_regs.size() >= 2) {
                if (const char *s = JniEnvironment::Instance().GetStringChars(
                        reinterpret_cast<jstring>(arg_regs[1]))) {
                    msg = s;
                }
            }
            out_exception = JavaRuntime::CreateException("java/lang/NullPointerException", msg);
            return {0, 'L'};
        }
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "isNull" && !arg_regs.empty()) {
        handled = true;
        return {arg_regs[0] == 0 ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "nonNull" && !arg_regs.empty()) {
        handled = true;
        return {arg_regs[0] != 0 ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString" && !arg_regs.empty()) {
        handled = true;
        jobject obj = reinterpret_cast<jobject>(arg_regs[0]);
        if (!obj && arg_regs.size() >= 2) {
            return {arg_regs[1], 'L'};
        }
        std::string s = JavaRuntime::ObjectToString(vm, obj, env, out_exception);
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(s)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleOptional(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                        bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    auto &jni = JniEnvironment::Instance();
    if (is_static) {
        if (ref.name == "empty") {
            handled = true;
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/util/Optional";
            syn->object_fields["value"] = nullptr;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(syn)), 'L'};
        }
        if ((ref.name == "of" || ref.name == "ofNullable") && !arg_regs.empty()) {
            handled = true;
            jobject v = reinterpret_cast<jobject>(arg_regs[0]);
            if (ref.name == "of" && !v) {
                out_exception = JavaRuntime::CreateException("java/lang/NullPointerException",
                                                             "Optional.of(null)");
                return {0, 'L'};
            }
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/util/Optional";
            syn->object_fields["value"] = v;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(syn)), 'L'};
        }
        return {0, 'V'};
    }
    if (arg_regs.empty()) {
        return {0, 'V'};
    }
    auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    jobject val = syn->object_fields["value"];
    if (ref.name == "isPresent") {
        handled = true;
        return {val != nullptr ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isEmpty") {
        handled = true;
        return {val == nullptr ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "get") {
        handled = true;
        if (!val) {
            out_exception = JavaRuntime::CreateException("java/util/NoSuchElementException",
                                                         "No value present");
            return {0, 'L'};
        }
        return {reinterpret_cast<uintptr_t>(val), 'L'};
    }
    if (ref.name == "orElse" && arg_regs.size() >= 2) {
        handled = true;
        return {val ? reinterpret_cast<uintptr_t>(val) : arg_regs[1], 'L'};
    }
    return {0, 'V'};
}

} // namespace

void RegisterUtilIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/util/ArrayList", HandleList,
                     {"java/util/AbstractList", "java/util/List", "java/util/Collection",
                      "java/lang/Iterable", "java/util/RandomAccess"});
    rt.RegisterClass("java/util/LinkedList", HandleList,
                     {"java/util/AbstractSequentialList", "java/util/List", "java/util/Deque",
                      "java/util/Queue", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass(
        "java/util/ArrayDeque", HandleArrayDeque,
        {"java/util/Deque", "java/util/Queue", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass("java/util/List", HandleList, {"java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass("java/util/Collection", HandleList, {"java/lang/Iterable"});

    rt.RegisterClass("java/util/HashMap",
                     [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *e, jobject &ex,
                        bool &h) { return HandleMap(vm, "java/util/HashMap", r, a, s, e, ex, h); },
                     {"java/util/AbstractMap", "java/util/Map"});
    rt.RegisterClass("java/util/LinkedHashMap",
                     [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *e, jobject &ex, bool &h) {
                         return HandleMap(vm, "java/util/LinkedHashMap", r, a, s, e, ex, h);
                     },
                     {"java/util/HashMap", "java/util/Map"});
    rt.RegisterClass("java/util/TreeMap",
                     [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *e, jobject &ex,
                        bool &h) { return HandleMap(vm, "java/util/TreeMap", r, a, s, e, ex, h); },
                     {"java/util/SortedMap", "java/util/NavigableMap", "java/util/Map"});
    rt.RegisterClass("java/util/Map",
                     [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *e, jobject &ex,
                        bool &h) { return HandleMap(vm, "java/util/HashMap", r, a, s, e, ex, h); });

    rt.RegisterClass(
        "java/util/HashSet",
        [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s, JNIEnv *e,
           jobject &ex, bool &h) { return HandleSet(vm, "java/util/HashSet", r, a, s, e, ex, h); },
        {"java/util/AbstractSet", "java/util/Set", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass(
        "java/util/LinkedHashSet",
        [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s, JNIEnv *e,
           jobject &ex,
           bool &h) { return HandleSet(vm, "java/util/LinkedHashSet", r, a, s, e, ex, h); },
        {"java/util/HashSet", "java/util/Set", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass(
        "java/util/TreeSet",
        [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s, JNIEnv *e,
           jobject &ex, bool &h) { return HandleSet(vm, "java/util/TreeSet", r, a, s, e, ex, h); },
        {"java/util/SortedSet", "java/util/Set", "java/util/Collection", "java/lang/Iterable"});
    rt.RegisterClass("java/util/Set",
                     [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *e, jobject &ex,
                        bool &h) { return HandleSet(vm, "java/util/HashSet", r, a, s, e, ex, h); },
                     {"java/util/Collection", "java/lang/Iterable"});

    rt.RegisterClass("java/util/Iterator", HandleIterator);
    rt.RegisterClass("java/util/ListIterator", HandleIterator, {"java/util/Iterator"});
    rt.RegisterClass("java/util/Map$Entry", HandleMapEntry);
    rt.RegisterClass("java/util/AbstractMap$SimpleEntry", HandleMapEntry, {"java/util/Map$Entry"});
    rt.RegisterClass("java/util/Arrays", HandleArrays);
    rt.RegisterClass("java/util/Collections", HandleCollections);
    rt.RegisterClass("java/util/Objects", HandleObjects);
    rt.RegisterClass("java/util/Optional", HandleOptional);
}

} // namespace portage::package
