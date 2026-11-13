#include "package/java_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>

namespace portage::package {

namespace {

std::mutex g_box_cache_mu;
uint64_t g_box_cache_epoch = 0;
std::array<jobject, 256> g_int_cache{};
std::array<jobject, 256> g_long_cache{};
std::array<jobject, 128> g_char_cache{};
jobject g_bool_true = nullptr;
jobject g_bool_false = nullptr;

void EnsureCacheEpochLocked(uint64_t cur_epoch)
{
    if (g_box_cache_epoch != cur_epoch) {
        g_int_cache.fill(nullptr);
        g_long_cache.fill(nullptr);
        g_char_cache.fill(nullptr);
        g_bool_true = nullptr;
        g_bool_false = nullptr;
        g_box_cache_epoch = cur_epoch;
    }
}

bool ReadStringArg(jobject obj, std::string &out)
{
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(obj)) {
        out = s;
        return true;
    }
    if (auto syn = jni.LookupObject(obj)) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (syn->class_name == "java/lang/String") {
            out = syn->string_fields["value"];
            return true;
        }
    }
    return false;
}

std::string IntToRadixString(uint64_t mag, bool neg, int radix)
{
    if (radix < 2 || radix > 36) {
        radix = 10;
    }
    if (mag == 0) {
        return "0";
    }
    std::string s;
    uint64_t r = static_cast<uint64_t>(radix);
    while (mag > 0) {
        uint64_t rem = mag % r;
        s.push_back(static_cast<char>(rem < 10 ? ('0' + rem) : ('a' + (rem - 10))));
        mag /= r;
    }
    if (neg) {
        s.push_back('-');
    }
    std::reverse(s.begin(), s.end());
    return s;
}

std::string UnsignedToRadixString(uint64_t val, int shift_or_bits, int radix)
{
    (void)shift_or_bits;
    return IntToRadixString(val, false, radix);
}

bool ParseSignedInt64(std::string_view text, int radix, int64_t min_val, int64_t max_val,
                      int64_t &out)
{
    if (text.empty() || radix < 2 || radix > 36) {
        return false;
    }
    bool neg = false;
    size_t i = 0;
    if (text[0] == '-') {
        neg = true;
        i = 1;
    } else if (text[0] == '+') {
        i = 1;
    }
    if (i >= text.size()) {
        return false;
    }
    uint64_t limit =
        neg ? static_cast<uint64_t>(-(min_val + 1)) + 1ULL : static_cast<uint64_t>(max_val);
    uint64_t acc = 0;
    for (; i < text.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(text[i]);
        int digit = -1;
        if (uc >= '0' && uc <= '9') {
            digit = uc - '0';
        } else if (uc >= 'a' && uc <= 'z') {
            digit = uc - 'a' + 10;
        } else if (uc >= 'A' && uc <= 'Z') {
            digit = uc - 'A' + 10;
        }
        if (digit < 0 || digit >= radix) {
            return false;
        }
        uint64_t udig = static_cast<uint64_t>(digit);
        uint64_t urad = static_cast<uint64_t>(radix);
        if (acc > (limit - udig) / urad) {
            return false;
        }
        acc = acc * urad + udig;
    }
    out = neg ? -static_cast<int64_t>(acc) : static_cast<int64_t>(acc);
    return true;
}

bool ParseDoubleStr(std::string_view text, double &out)
{
    size_t b = 0;
    size_t e = text.size();
    while (b < e && static_cast<unsigned char>(text[b]) <= 0x20u) {
        ++b;
    }
    while (e > b && static_cast<unsigned char>(text[e - 1]) <= 0x20u) {
        --e;
    }
    if (b >= e) {
        return false;
    }
    std::string trimmed(text.substr(b, e - b));
    if (trimmed == "NaN") {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (trimmed == "Infinity" || trimmed == "+Infinity") {
        out = std::numeric_limits<double>::infinity();
        return true;
    }
    if (trimmed == "-Infinity") {
        out = -std::numeric_limits<double>::infinity();
        return true;
    }
    char *endptr = nullptr;
    double val = std::strtod(trimmed.c_str(), &endptr);
    if (endptr != trimmed.c_str() + trimmed.size()) {
        return false;
    }
    out = val;
    return true;
}

DexValue HandleNumber(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    (void)out_exception;
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    if (ref.name == "<init>") {
        handled = true;
        return {0, 'V'};
    }
    int64_t lv = 0;
    double dv = 0.0;
    bool have_long = java_rt::TryUnwrapLong(self, lv);
    bool have_double = java_rt::TryUnwrapDouble(self, dv);
    if (!have_long && !have_double) {
        return {0, 'V'};
    }
    if (ref.name == "intValue") {
        handled = true;
        int32_t v = have_double ? static_cast<int32_t>(dv) : static_cast<int32_t>(lv);
        return {static_cast<uint64_t>(static_cast<uint32_t>(v)), 'I'};
    }
    if (ref.name == "longValue") {
        handled = true;
        int64_t v = have_double ? static_cast<int64_t>(dv) : lv;
        return {static_cast<uint64_t>(v), 'J'};
    }
    if (ref.name == "floatValue") {
        handled = true;
        float v = have_double ? static_cast<float>(dv) : static_cast<float>(lv);
        return {java_rt::FloatToBits(v), 'F'};
    }
    if (ref.name == "doubleValue") {
        handled = true;
        double v = have_double ? dv : static_cast<double>(lv);
        return {java_rt::DoubleToBits(v), 'D'};
    }
    if (ref.name == "shortValue") {
        handled = true;
        int16_t v =
            have_double ? static_cast<int16_t>(static_cast<int32_t>(dv)) : static_cast<int16_t>(lv);
        return {static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(v))), 'S'};
    }
    if (ref.name == "byteValue") {
        handled = true;
        int8_t v =
            have_double ? static_cast<int8_t>(static_cast<int32_t>(dv)) : static_cast<int8_t>(lv);
        return {static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(v))), 'B'};
    }
    return {0, 'V'};
}

DexValue HandleInteger(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();

    if (is_static) {
        if (ref.name == "valueOf" && !arg_regs.empty() && !ref.param_types.empty()) {
            handled = true;
            if (ref.param_types[0] == "I") {
                return {
                    reinterpret_cast<uintptr_t>(java_rt::BoxInt(static_cast<int32_t>(arg_regs[0]))),
                    'L'};
            }
            std::string s;
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseSignedInt64(s, radix, std::numeric_limits<int32_t>::min(),
                                  std::numeric_limits<int32_t>::max(), parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'L'};
            }
            return {reinterpret_cast<uintptr_t>(java_rt::BoxInt(static_cast<int32_t>(parsed))),
                    'L'};
        }
        if (ref.name == "parseInt" && !arg_regs.empty()) {
            handled = true;
            std::string s;
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseSignedInt64(s, radix, std::numeric_limits<int32_t>::min(),
                                  std::numeric_limits<int32_t>::max(), parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'I'};
            }
            return {static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(parsed))),
                    'I'};
        }
        if (ref.name == "toString" && !arg_regs.empty()) {
            handled = true;
            int32_t v = static_cast<int32_t>(arg_regs[0]);
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            bool neg = v < 0;
            uint64_t mag =
                neg ? static_cast<uint64_t>(-static_cast<int64_t>(v)) : static_cast<uint64_t>(v);
            return {
                reinterpret_cast<uintptr_t>(jni.CreateString(IntToRadixString(mag, neg, radix))),
                'L'};
        }
        if ((ref.name == "toHexString" || ref.name == "toBinaryString" ||
             ref.name == "toOctalString") &&
            !arg_regs.empty()) {
            handled = true;
            uint32_t uv = static_cast<uint32_t>(arg_regs[0]);
            int radix = (ref.name == "toHexString") ? 16 : (ref.name == "toBinaryString" ? 2 : 8);
            return {
                reinterpret_cast<uintptr_t>(jni.CreateString(UnsignedToRadixString(uv, 32, radix))),
                'L'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 2) {
            handled = true;
            int32_t a = static_cast<int32_t>(arg_regs[0]);
            int32_t b = static_cast<int32_t>(arg_regs[1]);
            int32_t cmp = (a < b) ? -1 : (a > b ? 1 : 0);
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
        }
        if (ref.name == "hashCode" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>(static_cast<uint32_t>(arg_regs[0])), 'I'};
        }
        if (ref.name == "bitCount" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>(std::popcount(static_cast<uint32_t>(arg_regs[0]))), 'I'};
        }
        if (ref.name == "highestOneBit" && !arg_regs.empty()) {
            handled = true;
            uint32_t v = static_cast<uint32_t>(arg_regs[0]);
            uint32_t res = v ? (1u << (31u - std::countl_zero(v))) : 0u;
            return {res, 'I'};
        }
        if (ref.name == "numberOfLeadingZeros" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>(std::countl_zero(static_cast<uint32_t>(arg_regs[0]))),
                    'I'};
        }
        if (ref.name == "numberOfTrailingZeros" && !arg_regs.empty()) {
            handled = true;
            return {static_cast<uint64_t>(std::countr_zero(static_cast<uint32_t>(arg_regs[0]))),
                    'I'};
        }
        if (ref.name == "sum" && arg_regs.size() >= 2) {
            handled = true;
            int32_t s = static_cast<int32_t>(arg_regs[0]) + static_cast<int32_t>(arg_regs[1]);
            return {static_cast<uint64_t>(static_cast<uint32_t>(s)), 'I'};
        }
        if ((ref.name == "min" || ref.name == "max") && arg_regs.size() >= 2) {
            handled = true;
            int32_t a = static_cast<int32_t>(arg_regs[0]);
            int32_t b = static_cast<int32_t>(arg_regs[1]);
            int32_t r = (ref.name == "min") ? std::min(a, b) : std::max(a, b);
            return {static_cast<uint64_t>(static_cast<uint32_t>(r)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        handled = true;
        int32_t val = 0;
        if (ref.param_types[0] == "I") {
            val = static_cast<int32_t>(arg_regs[1]);
        } else {
            std::string s;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[1]), s) ||
                !ParseSignedInt64(s, 10, std::numeric_limits<int32_t>::min(),
                                  std::numeric_limits<int32_t>::max(), parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'V'};
            }
            val = static_cast<int32_t>(parsed);
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->int_fields["value"] = val;
        return {0, 'V'};
    }

    int32_t self_val = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        self_val = syn->int_fields["value"];
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(std::to_string(self_val))), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(self_val)), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        int32_t other = 0;
        bool eq = false;
        auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        if (osyn && osyn->class_name == "java/lang/Integer") {
            std::lock_guard<std::recursive_mutex> lk(osyn->field_mutex);
            other = osyn->int_fields["value"];
            eq = (self_val == other);
        }
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        int32_t other = 0;
        java_rt::TryUnwrapInt(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = (self_val < other) ? -1 : (self_val > other ? 1 : 0);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return HandleNumber(vm, ref, arg_regs, is_static, env, out_exception, handled);
}

DexValue HandleLong(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                    bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();

    if (is_static) {
        if (ref.name == "valueOf" && !arg_regs.empty() && !ref.param_types.empty()) {
            handled = true;
            if (ref.param_types[0] == "J") {
                int64_t v = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0));
                return {reinterpret_cast<uintptr_t>(java_rt::BoxLong(v)), 'L'};
            }
            std::string s;
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseSignedInt64(s, radix, std::numeric_limits<int64_t>::min(),
                                  std::numeric_limits<int64_t>::max(), parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'L'};
            }
            return {reinterpret_cast<uintptr_t>(java_rt::BoxLong(parsed)), 'L'};
        }
        if (ref.name == "parseLong" && !arg_regs.empty()) {
            handled = true;
            std::string s;
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseSignedInt64(s, radix, std::numeric_limits<int64_t>::min(),
                                  std::numeric_limits<int64_t>::max(), parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'J'};
            }
            return {static_cast<uint64_t>(parsed), 'J'};
        }
        if (ref.name == "toString" && arg_regs.size() >= 2) {
            handled = true;
            int64_t v = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0));
            int radix = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : 10;
            bool neg = v < 0;
            uint64_t mag = neg ? (~static_cast<uint64_t>(v) + 1ULL) : static_cast<uint64_t>(v);
            return {
                reinterpret_cast<uintptr_t>(jni.CreateString(IntToRadixString(mag, neg, radix))),
                'L'};
        }
        if ((ref.name == "toHexString" || ref.name == "toBinaryString" ||
             ref.name == "toOctalString") &&
            arg_regs.size() >= 2) {
            handled = true;
            uint64_t uv = java_rt::ReadWideArg(arg_regs, 0);
            int radix = (ref.name == "toHexString") ? 16 : (ref.name == "toBinaryString" ? 2 : 8);
            return {
                reinterpret_cast<uintptr_t>(jni.CreateString(UnsignedToRadixString(uv, 64, radix))),
                'L'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 4) {
            handled = true;
            int64_t a = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0));
            int64_t b = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 2));
            int32_t cmp = (a < b) ? -1 : (a > b ? 1 : 0);
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
        }
        if (ref.name == "hashCode" && arg_regs.size() >= 2) {
            handled = true;
            uint64_t uv = java_rt::ReadWideArg(arg_regs, 0);
            int32_t h = static_cast<int32_t>(uv ^ (uv >> 32));
            return {static_cast<uint64_t>(static_cast<uint32_t>(h)), 'I'};
        }
        if (ref.name == "bitCount" && arg_regs.size() >= 2) {
            handled = true;
            return {static_cast<uint64_t>(std::popcount(java_rt::ReadWideArg(arg_regs, 0))), 'I'};
        }
        if (ref.name == "numberOfLeadingZeros" && arg_regs.size() >= 2) {
            handled = true;
            return {static_cast<uint64_t>(std::countl_zero(java_rt::ReadWideArg(arg_regs, 0))),
                    'I'};
        }
        if (ref.name == "sum" && arg_regs.size() >= 4) {
            handled = true;
            int64_t s = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0)) +
                        static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 2));
            return {static_cast<uint64_t>(s), 'J'};
        }
        if ((ref.name == "min" || ref.name == "max") && arg_regs.size() >= 4) {
            handled = true;
            int64_t a = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 0));
            int64_t b = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 2));
            return {static_cast<uint64_t>(ref.name == "min" ? std::min(a, b) : std::max(a, b)),
                    'J'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        handled = true;
        int64_t val = 0;
        if (ref.param_types[0] == "J") {
            val = static_cast<int64_t>(java_rt::ReadWideArg(arg_regs, 1));
        } else {
            std::string s;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[1]), s) ||
                !ParseSignedInt64(s, 10, std::numeric_limits<int64_t>::min(),
                                  std::numeric_limits<int64_t>::max(), val)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'V'};
            }
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->long_fields["value"] = val;
        return {0, 'V'};
    }

    int64_t self_val = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        self_val = syn->long_fields["value"];
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(std::to_string(self_val))), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        uint64_t uv = static_cast<uint64_t>(self_val);
        int32_t h = static_cast<int32_t>(uv ^ (uv >> 32));
        return {static_cast<uint64_t>(static_cast<uint32_t>(h)), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        bool eq = false;
        if (osyn && osyn->class_name == "java/lang/Long") {
            std::lock_guard<std::recursive_mutex> lk(osyn->field_mutex);
            eq = (self_val == osyn->long_fields["value"]);
        }
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        int64_t other = 0;
        java_rt::TryUnwrapLong(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = (self_val < other) ? -1 : (self_val > other ? 1 : 0);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return HandleNumber(vm, ref, arg_regs, is_static, env, out_exception, handled);
}

DexValue HandleSmallIntegral(std::string_view cls, DexVm &vm, const DexMethodRef &ref,
                             std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                             jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    bool is_short = (cls == "java/lang/Short");
    int64_t min_v = is_short ? -32768 : -128;
    int64_t max_v = is_short ? 32767 : 127;

    if (is_static) {
        if (ref.name == "valueOf" && !arg_regs.empty() && !ref.param_types.empty()) {
            handled = true;
            int64_t val = 0;
            if (ref.param_types[0] == "S" || ref.param_types[0] == "B" ||
                ref.param_types[0] == "I") {
                val =
                    is_short ? static_cast<int16_t>(arg_regs[0]) : static_cast<int8_t>(arg_regs[0]);
            } else {
                std::string s;
                int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
                if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                    !ParseSignedInt64(s, radix, min_v, max_v, val)) {
                    out_exception =
                        JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                    return {0, 'L'};
                }
            }
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = std::string(cls);
            syn->int_fields["value"] = static_cast<int32_t>(val);
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(syn)), 'L'};
        }
        if ((ref.name == "parseShort" || ref.name == "parseByte") && !arg_regs.empty()) {
            handled = true;
            std::string s;
            int radix = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 10;
            int64_t parsed = 0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseSignedInt64(s, radix, min_v, max_v, parsed)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, is_short ? 'S' : 'B'};
            }
            return {static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(parsed))),
                    is_short ? 'S' : 'B'};
        }
        if (ref.name == "toString" && !arg_regs.empty()) {
            handled = true;
            int32_t v =
                is_short ? static_cast<int16_t>(arg_regs[0]) : static_cast<int8_t>(arg_regs[0]);
            return {reinterpret_cast<uintptr_t>(jni.CreateString(std::to_string(v))), 'L'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 2) {
            handled = true;
            int32_t a =
                is_short ? static_cast<int16_t>(arg_regs[0]) : static_cast<int8_t>(arg_regs[0]);
            int32_t b =
                is_short ? static_cast<int16_t>(arg_regs[1]) : static_cast<int8_t>(arg_regs[1]);
            return {static_cast<uint64_t>(static_cast<uint32_t>(a - b)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2) {
        handled = true;
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->int_fields["value"] =
            is_short ? static_cast<int16_t>(arg_regs[1]) : static_cast<int8_t>(arg_regs[1]);
        return {0, 'V'};
    }
    int32_t self_val = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        self_val = syn->int_fields["value"];
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(std::to_string(self_val))), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(self_val)), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        bool eq = osyn && osyn->class_name == cls && osyn->int_fields["value"] == self_val;
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        int32_t other = 0;
        java_rt::TryUnwrapInt(reinterpret_cast<jobject>(arg_regs[1]), other);
        return {static_cast<uint64_t>(static_cast<uint32_t>(self_val - other)), 'I'};
    }
    return HandleNumber(vm, ref, arg_regs, is_static, env, out_exception, handled);
}

DexValue HandleShort(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                     bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    return HandleSmallIntegral("java/lang/Short", vm, ref, arg_regs, is_static, env, out_exception,
                               handled);
}

DexValue HandleByte(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                    bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    return HandleSmallIntegral("java/lang/Byte", vm, ref, arg_regs, is_static, env, out_exception,
                               handled);
}

DexValue HandleBoolean(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    (void)out_exception;
    auto &jni = JniEnvironment::Instance();
    if (is_static) {
        if (ref.name == "valueOf" && !arg_regs.empty() && !ref.param_types.empty()) {
            handled = true;
            bool v = false;
            if (ref.param_types[0] == "Z") {
                v = (arg_regs[0] != 0);
            } else {
                std::string s;
                if (ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s)) {
                    for (char &ch : s) {
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    }
                    v = (s == "true");
                }
            }
            return {reinterpret_cast<uintptr_t>(java_rt::BoxBool(v)), 'L'};
        }
        if (ref.name == "parseBoolean" && !arg_regs.empty()) {
            handled = true;
            std::string s;
            bool v = false;
            if (ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s)) {
                for (char &ch : s) {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                v = (s == "true");
            }
            return {v ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "toString" && !arg_regs.empty()) {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.CreateString(arg_regs[0] ? "true" : "false")),
                    'L'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 2) {
            handled = true;
            bool a = (arg_regs[0] != 0);
            bool b = (arg_regs[1] != 0);
            int32_t cmp = (a == b) ? 0 : (a ? 1 : -1);
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2) {
        handled = true;
        bool v = (arg_regs[1] != 0);
        if (!ref.param_types.empty() && ref.param_types[0] == "Ljava/lang/String;") {
            std::string s;
            ReadStringArg(reinterpret_cast<jobject>(arg_regs[1]), s);
            for (char &ch : s) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            v = (s == "true");
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->bool_fields["value"] = v ? 1 : 0;
        return {0, 'V'};
    }
    bool self_val = false;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        self_val = (syn->bool_fields["value"] != 0);
    }
    if (ref.name == "booleanValue") {
        handled = true;
        return {self_val ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(self_val ? "true" : "false")), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {static_cast<uint64_t>(self_val ? 1231 : 1237), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        bool other = false;
        bool eq = java_rt::TryUnwrapBool(reinterpret_cast<jobject>(arg_regs[1]), other) &&
                  (other == self_val);
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        bool other = false;
        java_rt::TryUnwrapBool(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = (self_val == other) ? 0 : (self_val ? 1 : -1);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return {0, 'V'};
}

DexValue HandleCharacter(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                         bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    (void)vm;
    (void)env;
    (void)out_exception;
    auto &jni = JniEnvironment::Instance();
    if (is_static && !arg_regs.empty()) {
        uint32_t cp = static_cast<uint32_t>(arg_regs[0]);
        if (ref.name == "valueOf") {
            handled = true;
            uint16_t ch = static_cast<uint16_t>(cp);
            if (ch < 128) {
                std::lock_guard<std::mutex> clk(g_box_cache_mu);
                EnsureCacheEpochLocked(jni.GetResetEpoch());
                if (!g_char_cache[ch]) {
                    auto syn = std::make_shared<JniSyntheticObject>();
                    syn->class_name = "java/lang/Character";
                    syn->int_fields["value"] = ch;
                    g_char_cache[ch] = jni.RegisterObject(syn);
                }
                return {reinterpret_cast<uintptr_t>(g_char_cache[ch]), 'L'};
            }
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/lang/Character";
            syn->int_fields["value"] = ch;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(syn)), 'L'};
        }
        if (ref.name == "toString") {
            handled = true;
            char16_t c16 = static_cast<char16_t>(cp);
            return {reinterpret_cast<uintptr_t>(
                        jni.CreateString(java_rt::Utf16ToUtf8(std::u16string_view(&c16, 1)))),
                    'L'};
        }
        if (ref.name == "isDigit") {
            handled = true;
            bool ok = (cp < 128 && std::isdigit(static_cast<int>(cp)));
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isLetter") {
            handled = true;
            bool ok = (cp < 128 && std::isalpha(static_cast<int>(cp)));
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isLetterOrDigit") {
            handled = true;
            bool ok = (cp < 128 && std::isalnum(static_cast<int>(cp)));
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isWhitespace" || ref.name == "isSpaceChar") {
            handled = true;
            bool ok = (cp <= 0x20u && std::isspace(static_cast<int>(cp)));
            return {ok ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isUpperCase") {
            handled = true;
            return {(cp < 128 && std::isupper(static_cast<int>(cp))) ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isLowerCase") {
            handled = true;
            return {(cp < 128 && std::islower(static_cast<int>(cp))) ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "toUpperCase") {
            handled = true;
            uint32_t u =
                (cp < 128) ? static_cast<uint32_t>(std::toupper(static_cast<int>(cp))) : cp;
            return {u, ref.return_type.empty() ? 'C' : ref.return_type.front()};
        }
        if (ref.name == "toLowerCase") {
            handled = true;
            uint32_t l =
                (cp < 128) ? static_cast<uint32_t>(std::tolower(static_cast<int>(cp))) : cp;
            return {l, ref.return_type.empty() ? 'C' : ref.return_type.front()};
        }
        if (ref.name == "compare" && arg_regs.size() >= 2) {
            handled = true;
            int32_t cmp = static_cast<int32_t>(static_cast<uint16_t>(arg_regs[0])) -
                          static_cast<int32_t>(static_cast<uint16_t>(arg_regs[1]));
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2) {
        handled = true;
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->int_fields["value"] = static_cast<uint16_t>(arg_regs[1]);
        return {0, 'V'};
    }
    uint16_t ch = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        ch = static_cast<uint16_t>(syn->int_fields["value"]);
    }
    if (ref.name == "charValue") {
        handled = true;
        return {ch, 'C'};
    }
    if (ref.name == "toString") {
        handled = true;
        char16_t c16 = static_cast<char16_t>(ch);
        return {reinterpret_cast<uintptr_t>(
                    jni.CreateString(java_rt::Utf16ToUtf8(std::u16string_view(&c16, 1)))),
                'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {ch, 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        uint16_t other = 0;
        bool eq =
            java_rt::TryUnwrapChar(reinterpret_cast<jobject>(arg_regs[1]), other) && (other == ch);
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        uint16_t other = 0;
        java_rt::TryUnwrapChar(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = static_cast<int32_t>(ch) - static_cast<int32_t>(other);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return {0, 'V'};
}

DexValue HandleFloat(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                     bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static && !arg_regs.empty()) {
        if (ref.name == "valueOf" && !ref.param_types.empty()) {
            handled = true;
            float f = 0.0f;
            if (ref.param_types[0] == "F") {
                f = java_rt::BitsToFloat(arg_regs[0]);
            } else {
                std::string s;
                double dv = 0.0;
                if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                    !ParseDoubleStr(s, dv)) {
                    out_exception =
                        JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                    return {0, 'L'};
                }
                f = static_cast<float>(dv);
            }
            return {reinterpret_cast<uintptr_t>(java_rt::BoxFloat(f)), 'L'};
        }
        if (ref.name == "parseFloat") {
            handled = true;
            std::string s;
            double dv = 0.0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseDoubleStr(s, dv)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'F'};
            }
            return {java_rt::FloatToBits(static_cast<float>(dv)), 'F'};
        }
        if (ref.name == "toString") {
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.CreateString(
                        java_rt::FormatFloatJava(java_rt::BitsToFloat(arg_regs[0])))),
                    'L'};
        }
        if (ref.name == "isNaN") {
            handled = true;
            return {std::isnan(java_rt::BitsToFloat(arg_regs[0])) ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "isInfinite") {
            handled = true;
            return {std::isinf(java_rt::BitsToFloat(arg_regs[0])) ? 1ULL : 0ULL, 'Z'};
        }
        if (ref.name == "floatToIntBits" || ref.name == "floatToRawIntBits" ||
            ref.name == "intBitsToFloat") {
            handled = true;
            return {arg_regs[0] & 0xFFFFFFFFULL, ref.name == "intBitsToFloat" ? 'F' : 'I'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 2) {
            handled = true;
            float a = java_rt::BitsToFloat(arg_regs[0]);
            float b = java_rt::BitsToFloat(arg_regs[1]);
            int32_t cmp = (a < b) ? -1 : (a > b ? 1 : (a == b ? 0 : (std::isnan(a) ? 1 : -1)));
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        handled = true;
        float f = 0.0f;
        if (ref.param_types[0] == "F") {
            f = java_rt::BitsToFloat(arg_regs[1]);
        } else if (ref.param_types[0] == "D") {
            f = static_cast<float>(java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 1)));
        } else {
            std::string s;
            double dv = 0.0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[1]), s) ||
                !ParseDoubleStr(s, dv)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'V'};
            }
            f = static_cast<float>(dv);
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->float_fields["value"] = f;
        syn->double_fields["value"] = static_cast<double>(f);
        return {0, 'V'};
    }
    float fv = 0.0f;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        fv = syn->float_fields["value"];
    }
    if (ref.name == "isNaN") {
        handled = true;
        return {std::isnan(fv) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isInfinite") {
        handled = true;
        return {std::isinf(fv) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::FormatFloatJava(fv))), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {java_rt::FloatToBits(fv), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        bool eq = osyn && osyn->class_name == "java/lang/Float" &&
                  java_rt::FloatToBits(osyn->float_fields["value"]) == java_rt::FloatToBits(fv);
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        float other = 0.0f;
        java_rt::TryUnwrapFloat(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = (fv < other) ? -1 : (fv > other ? 1 : 0);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return HandleNumber(vm, ref, arg_regs, is_static, env, out_exception, handled);
}

DexValue HandleDouble(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static && !arg_regs.empty()) {
        if (ref.name == "valueOf" && !ref.param_types.empty()) {
            handled = true;
            double d = 0.0;
            if (ref.param_types[0] == "D") {
                d = java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 0));
            } else {
                std::string s;
                if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                    !ParseDoubleStr(s, d)) {
                    out_exception =
                        JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                    return {0, 'L'};
                }
            }
            return {reinterpret_cast<uintptr_t>(java_rt::BoxDouble(d)), 'L'};
        }
        if (ref.name == "parseDouble") {
            handled = true;
            std::string s;
            double d = 0.0;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[0]), s) ||
                !ParseDoubleStr(s, d)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'D'};
            }
            return {java_rt::DoubleToBits(d), 'D'};
        }
        if (ref.name == "toString" && arg_regs.size() >= 2) {
            handled = true;
            double d = java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 0));
            return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::FormatDoubleJava(d))),
                    'L'};
        }
        if (ref.name == "isNaN" && arg_regs.size() >= 2) {
            handled = true;
            return {std::isnan(java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 0))) ? 1ULL
                                                                                         : 0ULL,
                    'Z'};
        }
        if (ref.name == "isInfinite" && arg_regs.size() >= 2) {
            handled = true;
            return {std::isinf(java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 0))) ? 1ULL
                                                                                         : 0ULL,
                    'Z'};
        }
        if ((ref.name == "doubleToLongBits" || ref.name == "doubleToRawLongBits" ||
             ref.name == "longBitsToDouble") &&
            arg_regs.size() >= 2) {
            handled = true;
            return {java_rt::ReadWideArg(arg_regs, 0), ref.name == "longBitsToDouble" ? 'D' : 'J'};
        }
        if (ref.name == "compare" && arg_regs.size() >= 4) {
            handled = true;
            double a = java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 0));
            double b = java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 2));
            int32_t cmp = (a < b) ? -1 : (a > b ? 1 : (a == b ? 0 : (std::isnan(a) ? 1 : -1)));
            return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
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
    if (ref.name == "<init>" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        handled = true;
        double d = 0.0;
        if (ref.param_types[0] == "D") {
            d = java_rt::BitsToDouble(java_rt::ReadWideArg(arg_regs, 1));
        } else {
            std::string s;
            if (!ReadStringArg(reinterpret_cast<jobject>(arg_regs[1]), s) ||
                !ParseDoubleStr(s, d)) {
                out_exception = JavaRuntime::CreateException("java/lang/NumberFormatException", s);
                return {0, 'V'};
            }
        }
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->double_fields["value"] = d;
        return {0, 'V'};
    }
    double dv = 0.0;
    {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        dv = syn->double_fields["value"];
    }
    if (ref.name == "isNaN") {
        handled = true;
        return {std::isnan(dv) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "isInfinite") {
        handled = true;
        return {std::isinf(dv) ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::FormatDoubleJava(dv))), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        uint64_t uv = java_rt::DoubleToBits(dv);
        int32_t h = static_cast<int32_t>(uv ^ (uv >> 32));
        return {static_cast<uint64_t>(static_cast<uint32_t>(h)), 'I'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        handled = true;
        auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        bool eq = osyn && osyn->class_name == "java/lang/Double" &&
                  java_rt::DoubleToBits(osyn->double_fields["value"]) == java_rt::DoubleToBits(dv);
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        handled = true;
        double other = 0.0;
        java_rt::TryUnwrapDouble(reinterpret_cast<jobject>(arg_regs[1]), other);
        int32_t cmp = (dv < other) ? -1 : (dv > other ? 1 : 0);
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    return HandleNumber(vm, ref, arg_regs, is_static, env, out_exception, handled);
}

} // namespace

namespace java_rt {

bool TryUnwrapInt(jobject obj, int32_t &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Integer" || syn->class_name == "java/lang/Short" ||
        syn->class_name == "java/lang/Byte" || syn->class_name == "java/lang/Character") {
        out = syn->int_fields["value"];
        return true;
    }
    if (syn->class_name == "java/lang/Long") {
        out = static_cast<int32_t>(syn->long_fields["value"]);
        return true;
    }
    return false;
}

bool TryUnwrapLong(jobject obj, int64_t &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Long") {
        out = syn->long_fields["value"];
        return true;
    }
    if (syn->class_name == "java/lang/Integer" || syn->class_name == "java/lang/Short" ||
        syn->class_name == "java/lang/Byte") {
        out = syn->int_fields["value"];
        return true;
    }
    return false;
}

bool TryUnwrapFloat(jobject obj, float &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Float") {
        out = syn->float_fields["value"];
        return true;
    }
    if (syn->class_name == "java/lang/Double") {
        out = static_cast<float>(syn->double_fields["value"]);
        return true;
    }
    return false;
}

bool TryUnwrapDouble(jobject obj, double &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Double") {
        out = syn->double_fields["value"];
        return true;
    }
    if (syn->class_name == "java/lang/Float") {
        out = static_cast<double>(syn->float_fields["value"]);
        return true;
    }
    if (syn->class_name == "java/lang/Integer" || syn->class_name == "java/lang/Short" ||
        syn->class_name == "java/lang/Byte") {
        out = static_cast<double>(syn->int_fields["value"]);
        return true;
    }
    if (syn->class_name == "java/lang/Long") {
        out = static_cast<double>(syn->long_fields["value"]);
        return true;
    }
    return false;
}

bool TryUnwrapBool(jobject obj, bool &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Boolean") {
        out = (syn->bool_fields["value"] != 0);
        return true;
    }
    return false;
}

bool TryUnwrapChar(jobject obj, uint16_t &out)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (syn->class_name == "java/lang/Character") {
        out = static_cast<uint16_t>(syn->int_fields["value"]);
        return true;
    }
    return false;
}

jobject BoxInt(int32_t v)
{
    auto &jni = JniEnvironment::Instance();
    if (v >= -128 && v <= 127) {
        size_t slot = static_cast<size_t>(v + 128);
        std::lock_guard<std::mutex> clk(g_box_cache_mu);
        EnsureCacheEpochLocked(jni.GetResetEpoch());
        if (!g_int_cache[slot]) {
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/lang/Integer";
            syn->int_fields["value"] = v;
            syn->long_fields["value"] = v;
            g_int_cache[slot] = jni.RegisterObject(syn);
        }
        return g_int_cache[slot];
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = "java/lang/Integer";
    syn->int_fields["value"] = v;
    syn->long_fields["value"] = v;
    return jni.RegisterObject(syn);
}

jobject BoxLong(int64_t v)
{
    auto &jni = JniEnvironment::Instance();
    if (v >= -128 && v <= 127) {
        size_t slot = static_cast<size_t>(v + 128);
        std::lock_guard<std::mutex> clk(g_box_cache_mu);
        EnsureCacheEpochLocked(jni.GetResetEpoch());
        if (!g_long_cache[slot]) {
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->class_name = "java/lang/Long";
            syn->long_fields["value"] = v;
            g_long_cache[slot] = jni.RegisterObject(syn);
        }
        return g_long_cache[slot];
    }
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = "java/lang/Long";
    syn->long_fields["value"] = v;
    return jni.RegisterObject(syn);
}

jobject BoxDouble(double v)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = "java/lang/Double";
    syn->double_fields["value"] = v;
    return JniEnvironment::Instance().RegisterObject(syn);
}

jobject BoxFloat(float v)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->class_name = "java/lang/Float";
    syn->float_fields["value"] = v;
    syn->double_fields["value"] = static_cast<double>(v);
    return JniEnvironment::Instance().RegisterObject(syn);
}

jobject BoxBool(bool v)
{
    auto &jni = JniEnvironment::Instance();
    std::lock_guard<std::mutex> clk(g_box_cache_mu);
    EnsureCacheEpochLocked(jni.GetResetEpoch());
    jobject &slot = v ? g_bool_true : g_bool_false;
    if (!slot) {
        auto syn = std::make_shared<JniSyntheticObject>();
        syn->class_name = "java/lang/Boolean";
        syn->bool_fields["value"] = v ? 1 : 0;
        slot = jni.RegisterObject(syn);
    }
    return slot;
}

} // namespace java_rt

void RegisterBoxingIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/lang/Number", &HandleNumber,
                     {"java/io/Serializable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Integer", &HandleInteger,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Long", &HandleLong,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Short", &HandleShort,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Byte", &HandleByte,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Boolean", &HandleBoolean,
                     {"java/io/Serializable", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Character", &HandleCharacter,
                     {"java/io/Serializable", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Float", &HandleFloat,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});
    rt.RegisterClass("java/lang/Double", &HandleDouble,
                     {"java/lang/Number", "java/lang/Comparable", "java/lang/Object"});

    rt.RegisterStaticField("java/lang/Integer", "MAX_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
    });
    rt.RegisterStaticField("java/lang/Integer", "MIN_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::min()));
    });
    rt.RegisterStaticField("java/lang/Integer", "SIZE", []() -> uint64_t { return 32; });
    rt.RegisterStaticField("java/lang/Integer", "BYTES", []() -> uint64_t { return 4; });

    rt.RegisterStaticField("java/lang/Long", "MAX_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    });
    rt.RegisterStaticField("java/lang/Long", "MIN_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(std::numeric_limits<int64_t>::min());
    });
    rt.RegisterStaticField("java/lang/Long", "SIZE", []() -> uint64_t { return 64; });
    rt.RegisterStaticField("java/lang/Long", "BYTES", []() -> uint64_t { return 8; });

    rt.RegisterStaticField("java/lang/Short", "MAX_VALUE", []() -> uint64_t { return 32767; });
    rt.RegisterStaticField("java/lang/Short", "MIN_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(static_cast<uint32_t>(-32768));
    });
    rt.RegisterStaticField("java/lang/Byte", "MAX_VALUE", []() -> uint64_t { return 127; });
    rt.RegisterStaticField("java/lang/Byte", "MIN_VALUE", []() -> uint64_t {
        return static_cast<uint64_t>(static_cast<uint32_t>(-128));
    });
    rt.RegisterStaticField("java/lang/Character", "MIN_VALUE", []() -> uint64_t { return 0; });
    rt.RegisterStaticField("java/lang/Character", "MAX_VALUE",
                           []() -> uint64_t { return 0xFFFFu; });

    rt.RegisterStaticField("java/lang/Boolean", "TRUE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(java_rt::BoxBool(true));
    });
    rt.RegisterStaticField("java/lang/Boolean", "FALSE", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(java_rt::BoxBool(false));
    });

    rt.RegisterStaticField("java/lang/Double", "NaN", []() -> uint64_t {
        return java_rt::DoubleToBits(std::numeric_limits<double>::quiet_NaN());
    });
    rt.RegisterStaticField("java/lang/Double", "POSITIVE_INFINITY", []() -> uint64_t {
        return java_rt::DoubleToBits(std::numeric_limits<double>::infinity());
    });
    rt.RegisterStaticField("java/lang/Double", "NEGATIVE_INFINITY", []() -> uint64_t {
        return java_rt::DoubleToBits(-std::numeric_limits<double>::infinity());
    });
    rt.RegisterStaticField("java/lang/Double", "MAX_VALUE", []() -> uint64_t {
        return java_rt::DoubleToBits(std::numeric_limits<double>::max());
    });
    rt.RegisterStaticField("java/lang/Double", "MIN_VALUE", []() -> uint64_t {
        return java_rt::DoubleToBits(std::numeric_limits<double>::denorm_min());
    });

    rt.RegisterStaticField("java/lang/Float", "NaN", []() -> uint64_t {
        return java_rt::FloatToBits(std::numeric_limits<float>::quiet_NaN());
    });
    rt.RegisterStaticField("java/lang/Float", "POSITIVE_INFINITY", []() -> uint64_t {
        return java_rt::FloatToBits(std::numeric_limits<float>::infinity());
    });
    rt.RegisterStaticField("java/lang/Float", "NEGATIVE_INFINITY", []() -> uint64_t {
        return java_rt::FloatToBits(-std::numeric_limits<float>::infinity());
    });
}

} // namespace portage::package
