#include "package/java_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>

namespace portage::package {

namespace java_rt {

std::u16string Utf8ToUtf16(std::string_view utf8)
{
    std::u16string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        uint8_t b0 = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        if (b0 < 0x80u) {
            cp = b0;
            i += 1;
        } else if ((b0 & 0xE0u) == 0xC0u && i + 1 < utf8.size()) {
            cp = ((b0 & 0x1Fu) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((b0 & 0xF0u) == 0xE0u && i + 2 < utf8.size()) {
            cp = ((b0 & 0x0Fu) << 12) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((b0 & 0xF8u) == 0xF0u && i + 3 < utf8.size()) {
            cp = ((b0 & 0x07u) << 18) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<uint8_t>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            cp = 0xFFFDu;
            i += 1;
        }
        if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

std::string Utf16ToUtf8(std::u16string_view utf16)
{
    std::string out;
    out.reserve(utf16.size());
    for (size_t i = 0; i < utf16.size(); ++i) {
        uint32_t c = utf16[i];
        if (c >= 0xD800u && c <= 0xDBFFu && i + 1 < utf16.size()) {
            uint32_t lo = utf16[i + 1];
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                c = 0x10000u + (((c - 0xD800u) << 10) | (lo - 0xDC00u));
                ++i;
            }
        }
        if (c < 0x80u) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (c >> 6)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        } else if (c < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (c >> 12)));
            out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (c >> 18)));
            out.push_back(static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        }
    }
    return out;
}

int32_t JavaStringHashCode(std::string_view utf8)
{
    std::u16string u16 = Utf8ToUtf16(utf8);
    uint32_t h = 0;
    for (char16_t ch : u16) {
        h = h * 31u + static_cast<uint32_t>(ch);
    }
    return static_cast<int32_t>(h);
}

std::string FormatDoubleJava(double v)
{
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return std::signbit(v) ? "-Infinity" : "Infinity";
    }
    if (v == 0.0) {
        return std::signbit(v) ? "-0.0" : "0.0";
    }

    double av = std::abs(v);
    std::ostringstream oss;
    if (av >= 1e7 || av < 1e-3) {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "%.15E", v);
        std::string s(buf);
        auto epos = s.find('E');
        if (epos != std::string::npos) {
            std::string mant = s.substr(0, epos);
            std::string exp = s.substr(epos + 1);
            auto dot = mant.find('.');
            if (dot != std::string::npos) {
                while (mant.size() > dot + 2 && mant.back() == '0') {
                    mant.pop_back();
                }
            }
            bool exp_neg = false;
            int exp_val = std::stoi(exp);
            if (exp_val < 0) {
                exp_neg = true;
                exp_val = -exp_val;
            }
            return mant + "E" + (exp_neg ? "-" : "") + std::to_string(exp_val);
        }
        return s;
    }

    oss << std::setprecision(15) << v;
    std::string s = oss.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

std::string FormatFloatJava(float v)
{
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return std::signbit(v) ? "-Infinity" : "Infinity";
    }
    if (v == 0.0f) {
        return std::signbit(v) ? "-0.0" : "0.0";
    }

    float av = std::abs(v);
    if (av >= 1e7f || av < 1e-3f) {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "%.7E", static_cast<double>(v));
        std::string s(buf);
        auto epos = s.find('E');
        if (epos != std::string::npos) {
            std::string mant = s.substr(0, epos);
            int exp_val = std::stoi(s.substr(epos + 1));
            auto dot = mant.find('.');
            if (dot != std::string::npos) {
                while (mant.size() > dot + 2 && mant.back() == '0') {
                    mant.pop_back();
                }
            }
            return mant + "E" + std::to_string(exp_val);
        }
        return s;
    }

    std::ostringstream oss;
    oss << std::setprecision(7) << v;
    std::string s = oss.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

} // namespace java_rt

namespace {

bool ReadJavaStringUtf8(jobject obj, std::string &out)
{
    if (!obj) {
        return false;
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(obj)) {
        out = s;
        return true;
    }
    if (auto syn = jni.LookupObject(obj)) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        if (syn->class_name == "java/lang/String" || syn->class_name == "java/lang/StringBuilder" ||
            syn->class_name == "java/lang/StringBuffer") {
            out = syn->string_fields["value"];
            return true;
        }
    }
    return false;
}

void WriteStringObjValue(jobject obj, std::string val)
{
    auto &jni = JniEnvironment::Instance();
    if (auto syn = jni.LookupObject(obj)) {
        std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
        syn->string_fields["value"] = std::move(val);
    }
}

DexValue TryHandleCodePointQuery(std::u16string_view u16, const DexMethodRef &ref,
                                 std::span<const uint64_t> arg_regs, jobject &out_exception,
                                 bool &handled)
{
    if (ref.name == "codePointAt" && arg_regs.size() >= 2) {
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx < 0 || static_cast<size_t>(idx) >= u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "String index out of range");
            return {0, 'I'};
        }
        size_t pos = static_cast<size_t>(idx);
        char16_t c1 = u16[pos];
        uint32_t cp = static_cast<uint32_t>(c1);
        if (c1 >= 0xD800u && c1 <= 0xDBFFu && pos + 1 < u16.size()) {
            char16_t c2 = u16[pos + 1];
            if (c2 >= 0xDC00u && c2 <= 0xDFFFu) {
                cp = 0x10000u + (((static_cast<uint32_t>(c1) - 0xD800u) << 10) |
                                 (static_cast<uint32_t>(c2) - 0xDC00u));
            }
        }
        return {static_cast<uint64_t>(cp), 'I'};
    }
    if (ref.name == "codePointBefore" && arg_regs.size() >= 2) {
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx <= 0 || static_cast<size_t>(idx) > u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "String index out of range");
            return {0, 'I'};
        }
        size_t pos = static_cast<size_t>(idx - 1);
        char16_t c2 = u16[pos];
        uint32_t cp = static_cast<uint32_t>(c2);
        if (c2 >= 0xDC00u && c2 <= 0xDFFFu && pos > 0) {
            char16_t c1 = u16[pos - 1];
            if (c1 >= 0xD800u && c1 <= 0xDBFFu) {
                cp = 0x10000u + (((static_cast<uint32_t>(c1) - 0xD800u) << 10) |
                                 (static_cast<uint32_t>(c2) - 0xDC00u));
            }
        }
        return {static_cast<uint64_t>(cp), 'I'};
    }
    if (ref.name == "codePointCount" && arg_regs.size() >= 3) {
        int32_t begin = static_cast<int32_t>(arg_regs[1]);
        int32_t end = static_cast<int32_t>(arg_regs[2]);
        handled = true;
        if (begin < 0 || end < begin || static_cast<size_t>(end) > u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "String index out of range");
            return {0, 'I'};
        }
        uint32_t count = 0;
        for (size_t i = static_cast<size_t>(begin); i < static_cast<size_t>(end); ++i) {
            ++count;
            if (u16[i] >= 0xD800u && u16[i] <= 0xDBFFu && i + 1 < static_cast<size_t>(end) &&
                u16[i + 1] >= 0xDC00u && u16[i + 1] <= 0xDFFFu) {
                ++i;
            }
        }
        return {static_cast<uint64_t>(count), 'I'};
    }
    return {0, 'I'};
}

std::string ValueToStringForAppend(DexVm &vm, std::string_view type_desc,
                                   std::span<const uint64_t> args, size_t arg_idx, JNIEnv *env,
                                   jobject &out_exception)
{
    auto &jni = JniEnvironment::Instance();
    if (type_desc == "Ljava/lang/String;") {
        std::string s;
        return ReadJavaStringUtf8(reinterpret_cast<jobject>(args[arg_idx]), s) ? s : "null";
    }
    if (type_desc == "Ljava/lang/CharSequence;" || type_desc == "Ljava/lang/StringBuffer;" ||
        type_desc == "Ljava/lang/Object;") {
        jobject o = reinterpret_cast<jobject>(args[arg_idx]);
        if (!o) {
            return "null";
        }
        std::string s;
        if (ReadJavaStringUtf8(o, s)) {
            return s;
        }
        return JavaRuntime::ObjectToString(vm, o, env, out_exception);
    }
    if (type_desc == "I") {
        return std::to_string(static_cast<int32_t>(args[arg_idx]));
    }
    if (type_desc == "J") {
        return std::to_string(static_cast<int64_t>(java_rt::ReadWideArg(args, arg_idx)));
    }
    if (type_desc == "F") {
        return java_rt::FormatFloatJava(java_rt::BitsToFloat(args[arg_idx]));
    }
    if (type_desc == "D") {
        return java_rt::FormatDoubleJava(
            java_rt::BitsToDouble(java_rt::ReadWideArg(args, arg_idx)));
    }
    if (type_desc == "Z") {
        return (args[arg_idx] != 0) ? "true" : "false";
    }
    if (type_desc == "C") {
        char16_t ch = static_cast<char16_t>(args[arg_idx] & 0xFFFFu);
        return java_rt::Utf16ToUtf8(std::u16string_view(&ch, 1));
    }
    if (type_desc == "[C") {
        auto arr = jni.LookupObject(reinterpret_cast<jobject>(args[arg_idx]));
        if (arr && arr->type == JniObjectType::CharArray) {
            std::u16string u16(arr->char_data.begin(), arr->char_data.end());
            return java_rt::Utf16ToUtf8(u16);
        }
        return "null";
    }
    return "";
}

std::string FormatStringImpl(DexVm &vm, std::string_view fmt, const std::vector<jobject> &args,
                             JNIEnv *env, jobject &out_exception)
{
    std::string out;
    size_t arg_idx = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (i + 1 >= fmt.size()) {
            out.push_back('%');
            break;
        }
        size_t j = i + 1;
        bool left_align = false;
        bool zero_pad = false;
        bool plus_sign = false;
        bool group = false;
        while (j < fmt.size()) {
            char f = fmt[j];
            if (f == '-') {
                left_align = true;
            } else if (f == '0') {
                zero_pad = true;
            } else if (f == '+') {
                plus_sign = true;
            } else if (f == ',') {
                group = true;
            } else {
                break;
            }
            ++j;
        }
        (void)group;
        int width = 0;
        while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) {
            width = width * 10 + (fmt[j] - '0');
            ++j;
        }
        int precision = -1;
        if (j < fmt.size() && fmt[j] == '.') {
            ++j;
            precision = 0;
            while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) {
                precision = precision * 10 + (fmt[j] - '0');
                ++j;
            }
        }
        if (j >= fmt.size()) {
            out.push_back('%');
            break;
        }
        char spec = fmt[j];
        i = j;

        if (spec == '%') {
            out.push_back('%');
            continue;
        }
        if (spec == 'n') {
            out.push_back('\n');
            continue;
        }

        jobject aobj = (arg_idx < args.size()) ? args[arg_idx++] : nullptr;
        std::string piece;
        if (spec == 's' || spec == 'S') {
            piece = aobj ? JavaRuntime::ObjectToString(vm, aobj, env, out_exception) : "null";
            if (out_exception) {
                return "";
            }
            if (precision >= 0 && static_cast<size_t>(precision) < piece.size()) {
                piece.resize(static_cast<size_t>(precision));
            }
            if (spec == 'S') {
                std::transform(piece.begin(), piece.end(), piece.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
        } else if (spec == 'd' || spec == 'x' || spec == 'X' || spec == 'o') {
            int64_t val = 0;
            int32_t iv = 0;
            if (java_rt::TryUnwrapLong(aobj, val)) {
            } else if (java_rt::TryUnwrapInt(aobj, iv)) {
                val = iv;
            }
            if (spec == 'd') {
                if (plus_sign && val >= 0) {
                    piece = "+" + std::to_string(val);
                } else {
                    piece = std::to_string(val);
                }
            } else {
                char fbuf[16]{};
                std::snprintf(fbuf, sizeof(fbuf), "%%%sll%c", plus_sign ? "+" : "", spec);
                char rbuf[64]{};
                std::snprintf(rbuf, sizeof(rbuf), fbuf, static_cast<unsigned long long>(val));
                piece = rbuf;
            }
        } else if (spec == 'f' || spec == 'e' || spec == 'E') {
            double dv = 0.0;
            float fv = 0.0f;
            if (java_rt::TryUnwrapDouble(aobj, dv)) {
            } else if (java_rt::TryUnwrapFloat(aobj, fv)) {
                dv = fv;
            }
            int p = (precision >= 0) ? precision : 6;
            std::string cfmt = "%";
            if (plus_sign) {
                cfmt.push_back('+');
            }
            cfmt += "." + std::to_string(p) + spec;
            char rbuf[128]{};
            std::snprintf(rbuf, sizeof(rbuf), cfmt.c_str(), dv);
            piece = rbuf;
        } else if (spec == 'c' || spec == 'C') {
            uint16_t ch = 0;
            int32_t iv = 0;
            if (java_rt::TryUnwrapChar(aobj, ch)) {
            } else if (java_rt::TryUnwrapInt(aobj, iv)) {
                ch = static_cast<uint16_t>(iv);
            }
            char16_t c16 = static_cast<char16_t>(ch);
            piece = java_rt::Utf16ToUtf8(std::u16string_view(&c16, 1));
        } else if (spec == 'b' || spec == 'B') {
            bool bval = false;
            if (aobj) {
                if (!java_rt::TryUnwrapBool(aobj, bval)) {
                    bval = true;
                }
            }
            piece = bval ? (spec == 'B' ? "TRUE" : "true") : (spec == 'B' ? "FALSE" : "false");
        }

        if (width > 0 && piece.size() < static_cast<size_t>(width)) {
            size_t pad = static_cast<size_t>(width) - piece.size();
            char pch = (zero_pad && !left_align &&
                        (spec == 'd' || spec == 'x' || spec == 'X' || spec == 'o' || spec == 'f'))
                           ? '0'
                           : ' ';
            if (left_align) {
                piece.append(pad, ' ');
            } else if (pch == '0' && !piece.empty() && (piece[0] == '-' || piece[0] == '+')) {
                piece.insert(1, pad, '0');
            } else {
                piece.insert(0, pad, pch);
            }
        }
        out += piece;
    }
    return out;
}

std::vector<std::string> SplitJavaString(const std::string &input, const std::string &regex,
                                         int32_t limit)
{
    std::string delim = regex;
    if (delim == "\\.") {
        delim = ".";
    } else if (delim == "\\|") {
        delim = "|";
    } else if (delim == "\\+") {
        delim = "+";
    } else if (delim == "\\*") {
        delim = "*";
    } else if (delim == "\\?") {
        delim = "?";
    } else if (delim == "\\n") {
        delim = "\n";
    } else if (delim == "\\t") {
        delim = "\t";
    }

    std::vector<std::string> parts;
    if (delim.empty()) {
        std::u16string u16 = java_rt::Utf8ToUtf16(input);
        for (char16_t c : u16) {
            parts.push_back(java_rt::Utf16ToUtf8(std::u16string_view(&c, 1)));
        }
    } else if (delim == "\\s" || delim == "\\s+") {
        size_t pos = 0;
        while (pos < input.size()) {
            while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) {
                ++pos;
            }
            if (pos >= input.size()) {
                break;
            }
            size_t end = pos;
            while (end < input.size() && !std::isspace(static_cast<unsigned char>(input[end]))) {
                ++end;
            }
            parts.push_back(input.substr(pos, end - pos));
            pos = end;
        }
    } else {
        size_t pos = 0;
        while (true) {
            if (limit > 0 && static_cast<int32_t>(parts.size()) + 1 == limit) {
                parts.push_back(input.substr(pos));
                break;
            }
            size_t hit = input.find(delim, pos);
            if (hit == std::string::npos) {
                parts.push_back(input.substr(pos));
                break;
            }
            parts.push_back(input.substr(pos, hit - pos));
            pos = hit + delim.size();
        }
    }
    if (limit == 0) {
        while (!parts.empty() && parts.back().empty()) {
            parts.pop_back();
        }
    }
    if (parts.empty()) {
        parts.push_back(input);
    }
    return parts;
}

DexValue HandleString(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                      bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();

    if (!is_static && ref.name == "<init>" && !arg_regs.empty()) {
        jobject self = reinterpret_cast<jobject>(arg_regs[0]);
        std::string init_val;
        if (arg_regs.size() >= 2 && !ref.param_types.empty()) {
            const auto &pt0 = ref.param_types[0];
            if (pt0 == "Ljava/lang/String;" || pt0 == "Ljava/lang/StringBuilder;" ||
                pt0 == "Ljava/lang/StringBuffer;") {
                ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), init_val);
            } else if (pt0 == "[C") {
                auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
                if (arr && arr->type == JniObjectType::CharArray) {
                    size_t off = (arg_regs.size() >= 4)
                                     ? static_cast<size_t>(static_cast<int32_t>(arg_regs[2]))
                                     : 0;
                    size_t cnt = (arg_regs.size() >= 4)
                                     ? static_cast<size_t>(static_cast<int32_t>(arg_regs[3]))
                                     : arr->char_data.size();
                    if (off <= arr->char_data.size() && off + cnt <= arr->char_data.size()) {
                        std::u16string u16(arr->char_data.begin() + static_cast<ptrdiff_t>(off),
                                           arr->char_data.begin() +
                                               static_cast<ptrdiff_t>(off + cnt));
                        init_val = java_rt::Utf16ToUtf8(u16);
                    }
                }
            } else if (pt0 == "[B") {
                auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
                if (arr && arr->type == JniObjectType::ByteArray) {
                    size_t off = 0;
                    size_t cnt = arr->byte_data.size();
                    std::string charset = "UTF-8";
                    if (ref.param_types.size() >= 3 && ref.param_types[1] == "I" &&
                        arg_regs.size() >= 4) {
                        off = static_cast<size_t>(static_cast<int32_t>(arg_regs[2]));
                        cnt = static_cast<size_t>(static_cast<int32_t>(arg_regs[3]));
                        if (ref.param_types.size() >= 4 && arg_regs.size() >= 5) {
                            if (ref.param_types[3] == "Ljava/lang/String;") {
                                std::string cs;
                                ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[4]), cs);
                                charset = java_rt::NormalizeCharsetName(cs);
                            } else if (ref.param_types[3] == "Ljava/nio/charset/Charset;") {
                                auto cs_obj =
                                    jni.LookupObject(reinterpret_cast<jobject>(arg_regs[4]));
                                if (cs_obj) {
                                    auto it = cs_obj->string_fields.find("name");
                                    if (it != cs_obj->string_fields.end()) {
                                        charset = java_rt::NormalizeCharsetName(it->second);
                                    }
                                }
                            }
                        }
                    } else if (ref.param_types.size() >= 2 && arg_regs.size() >= 3) {
                        if (ref.param_types[1] == "Ljava/lang/String;") {
                            std::string cs;
                            ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[2]), cs);
                            charset = java_rt::NormalizeCharsetName(cs);
                        } else if (ref.param_types[1] == "Ljava/nio/charset/Charset;") {
                            auto cs_obj = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[2]));
                            if (cs_obj) {
                                auto it = cs_obj->string_fields.find("name");
                                if (it != cs_obj->string_fields.end()) {
                                    charset = java_rt::NormalizeCharsetName(it->second);
                                }
                            }
                        }
                    }
                    if (off <= arr->byte_data.size() && off + cnt <= arr->byte_data.size()) {
                        std::span<const uint8_t> slice(
                            reinterpret_cast<const uint8_t *>(arr->byte_data.data() + off), cnt);
                        init_val = java_rt::DecodeBytesWithCharset(slice, charset);
                    }
                }
            }
        }
        WriteStringObjValue(self, std::move(init_val));
        handled = true;
        return {0, 'V'};
    }

    if (is_static && ref.name == "valueOf" && !arg_regs.empty() && !ref.param_types.empty()) {
        const auto &pt0 = ref.param_types[0];
        std::string str;
        if (pt0 == "[C" && ref.param_types.size() == 3 && arg_regs.size() >= 3) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            size_t off = static_cast<size_t>(static_cast<int32_t>(arg_regs[1]));
            size_t cnt = static_cast<size_t>(static_cast<int32_t>(arg_regs[2]));
            if (arr && arr->type == JniObjectType::CharArray && off <= arr->char_data.size() &&
                off + cnt <= arr->char_data.size()) {
                std::u16string u16(arr->char_data.begin() + static_cast<ptrdiff_t>(off),
                                   arr->char_data.begin() + static_cast<ptrdiff_t>(off + cnt));
                str = java_rt::Utf16ToUtf8(u16);
            }
        } else {
            str = ValueToStringForAppend(vm, pt0, arg_regs, 0, env, out_exception);
            if (out_exception) {
                handled = true;
                return {0, 'L'};
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(str)), 'L'};
    }

    if (is_static && ref.name == "format" && !arg_regs.empty() && !ref.param_types.empty()) {
        size_t fmt_idx = (ref.param_types[0] == "Ljava/util/Locale;") ? 1 : 0;
        if (fmt_idx + 1 < arg_regs.size()) {
            std::string fmt;
            ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[fmt_idx]), fmt);
            std::vector<jobject> fargs;
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[fmt_idx + 1]));
            if (arr && arr->type == JniObjectType::ObjectArray) {
                fargs = arr->object_data;
            }
            std::string formatted = FormatStringImpl(vm, fmt, fargs, env, out_exception);
            handled = true;
            if (out_exception) {
                return {0, 'L'};
            }
            return {reinterpret_cast<uintptr_t>(jni.CreateString(formatted)), 'L'};
        }
    }

    if (is_static && ref.name == "join" && arg_regs.size() >= 2) {
        std::string delim;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[0]), delim);
        std::string out;
        auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        if (arr && arr->type == JniObjectType::ObjectArray) {
            for (size_t i = 0; i < arr->object_data.size(); ++i) {
                if (i > 0) {
                    out += delim;
                }
                std::string piece;
                ReadJavaStringUtf8(arr->object_data[i], piece);
                out += piece;
            }
        } else if (arr) {
            bool has_next = false;
            DexValue it_val = JavaRuntime::CallVirtual(
                vm, arg_regs[1] ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr, "iterator",
                "()Ljava/util/Iterator;", {}, env, out_exception, &has_next);
            jobject iter = reinterpret_cast<jobject>(it_val.raw);
            bool first = true;
            while (iter && !out_exception) {
                DexValue hn =
                    JavaRuntime::CallVirtual(vm, iter, "hasNext", "()Z", {}, env, out_exception);
                if (out_exception || hn.raw == 0) {
                    break;
                }
                DexValue elem = JavaRuntime::CallVirtual(vm, iter, "next", "()Ljava/lang/Object;",
                                                         {}, env, out_exception);
                if (out_exception) {
                    break;
                }
                if (!first) {
                    out += delim;
                }
                first = false;
                out += JavaRuntime::ObjectToString(vm, reinterpret_cast<jobject>(elem.raw), env,
                                                   out_exception);
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }

    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }

    std::string self_utf8;
    if (!ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[0]), self_utf8)) {
        return {0, 'V'};
    }
    std::u16string u16 = java_rt::Utf8ToUtf16(self_utf8);

    if (ref.name == "length") {
        handled = true;
        return {static_cast<uint64_t>(u16.size()), 'I'};
    }
    if (ref.name == "isEmpty") {
        handled = true;
        return {u16.empty() ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(self_utf8)), 'L'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {
            static_cast<uint64_t>(static_cast<uint32_t>(java_rt::JavaStringHashCode(self_utf8))),
            'I'};
    }
    if (ref.name == "charAt" && arg_regs.size() >= 2) {
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx < 0 || static_cast<size_t>(idx) >= u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "String index out of range");
            return {0, 'C'};
        }
        return {static_cast<uint64_t>(u16[static_cast<size_t>(idx)]), 'C'};
    }
    if (DexValue cpv = TryHandleCodePointQuery(u16, ref, arg_regs, out_exception, handled);
        handled) {
        return cpv;
    }
    if (ref.name == "substring" && arg_regs.size() >= 2) {
        int32_t begin = static_cast<int32_t>(arg_regs[1]);
        int32_t end = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2])
                                             : static_cast<int32_t>(u16.size());
        handled = true;
        if (begin < 0 || end < begin || static_cast<size_t>(end) > u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "String index out of range");
            return {0, 'L'};
        }
        std::string sub = java_rt::Utf16ToUtf8(std::u16string_view(u16).substr(
            static_cast<size_t>(begin), static_cast<size_t>(end - begin)));
        return {reinterpret_cast<uintptr_t>(jni.CreateString(sub)), 'L'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        std::string other;
        bool eq =
            ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), other) && other == self_utf8;
        handled = true;
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "equalsIgnoreCase" && arg_regs.size() >= 2) {
        std::string other;
        bool ok = ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), other);
        bool eq = false;
        if (ok) {
            std::u16string uother = java_rt::Utf8ToUtf16(other);
            if (u16.size() == uother.size()) {
                eq = true;
                for (size_t i = 0; i < u16.size(); ++i) {
                    char16_t a = u16[i];
                    char16_t b = uother[i];
                    if (a < 128 && b < 128) {
                        a = static_cast<char16_t>(std::tolower(static_cast<unsigned char>(a)));
                        b = static_cast<char16_t>(std::tolower(static_cast<unsigned char>(b)));
                    }
                    if (a != b) {
                        eq = false;
                        break;
                    }
                }
            }
        }
        handled = true;
        return {eq ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "compareTo" && arg_regs.size() >= 2) {
        std::string other;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), other);
        std::u16string uother = java_rt::Utf8ToUtf16(other);
        size_t lim = std::min(u16.size(), uother.size());
        int32_t cmp = 0;
        for (size_t i = 0; i < lim; ++i) {
            if (u16[i] != uother[i]) {
                cmp = static_cast<int32_t>(u16[i]) - static_cast<int32_t>(uother[i]);
                break;
            }
        }
        if (cmp == 0 && u16.size() != uother.size()) {
            cmp = static_cast<int32_t>(u16.size()) - static_cast<int32_t>(uother.size());
        }
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    if (ref.name == "compareToIgnoreCase" && arg_regs.size() >= 2) {
        std::string other;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), other);
        std::u16string uother = java_rt::Utf8ToUtf16(other);
        size_t lim = std::min(u16.size(), uother.size());
        int32_t cmp = 0;
        for (size_t i = 0; i < lim; ++i) {
            char16_t a = (u16[i] < 128) ? static_cast<char16_t>(std::tolower(u16[i])) : u16[i];
            char16_t b =
                (uother[i] < 128) ? static_cast<char16_t>(std::tolower(uother[i])) : uother[i];
            if (a != b) {
                cmp = static_cast<int32_t>(a) - static_cast<int32_t>(b);
                break;
            }
        }
        if (cmp == 0 && u16.size() != uother.size()) {
            cmp = static_cast<int32_t>(u16.size()) - static_cast<int32_t>(uother.size());
        }
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(cmp)), 'I'};
    }
    if (ref.name == "indexOf" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        int32_t from = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : 0;
        if (from < 0) {
            from = 0;
        }
        size_t pos = std::u16string::npos;
        if (ref.param_types[0] == "I") {
            char16_t ch = static_cast<char16_t>(arg_regs[1]);
            pos = (static_cast<size_t>(from) <= u16.size())
                      ? u16.find(ch, static_cast<size_t>(from))
                      : std::u16string::npos;
        } else {
            std::string sub;
            if (ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), sub)) {
                std::u16string usub = java_rt::Utf8ToUtf16(sub);
                pos = (static_cast<size_t>(from) <= u16.size())
                          ? u16.find(usub, static_cast<size_t>(from))
                          : std::u16string::npos;
            }
        }
        handled = true;
        int32_t res = (pos == std::u16string::npos) ? -1 : static_cast<int32_t>(pos);
        return {static_cast<uint64_t>(static_cast<uint32_t>(res)), 'I'};
    }
    if (ref.name == "lastIndexOf" && arg_regs.size() >= 2 && !ref.param_types.empty()) {
        int32_t from = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2])
                                              : static_cast<int32_t>(u16.size());
        size_t pos = std::u16string::npos;
        if (from >= 0) {
            if (ref.param_types[0] == "I") {
                pos = u16.rfind(static_cast<char16_t>(arg_regs[1]), static_cast<size_t>(from));
            } else {
                std::string sub;
                if (ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), sub)) {
                    pos = u16.rfind(java_rt::Utf8ToUtf16(sub), static_cast<size_t>(from));
                }
            }
        }
        handled = true;
        int32_t res = (pos == std::u16string::npos) ? -1 : static_cast<int32_t>(pos);
        return {static_cast<uint64_t>(static_cast<uint32_t>(res)), 'I'};
    }
    if (ref.name == "contains" && arg_regs.size() >= 2) {
        std::string sub;
        bool ok = false;
        if (ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), sub)) {
            ok = self_utf8.find(sub) != std::string::npos;
        }
        handled = true;
        return {ok ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "startsWith" && arg_regs.size() >= 2) {
        std::string prefix;
        int32_t toffset = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : 0;
        bool ok = false;
        if (toffset >= 0 && ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), prefix)) {
            std::u16string uprefix = java_rt::Utf8ToUtf16(prefix);
            size_t uoff = static_cast<size_t>(toffset);
            ok = (uoff + uprefix.size() <= u16.size()) &&
                 (u16.compare(uoff, uprefix.size(), uprefix) == 0);
        }
        handled = true;
        return {ok ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "endsWith" && arg_regs.size() >= 2) {
        std::string suffix;
        bool ok = ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), suffix) &&
                  self_utf8.ends_with(suffix);
        handled = true;
        return {ok ? 1ULL : 0ULL, 'Z'};
    }
    if (ref.name == "concat" && arg_regs.size() >= 2) {
        std::string b;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), b);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(self_utf8 + b)), 'L'};
    }
    if (ref.name == "toUpperCase") {
        std::u16string out = u16;
        for (char16_t &c : out) {
            if (c < 128) {
                c = static_cast<char16_t>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::Utf16ToUtf8(out))), 'L'};
    }
    if (ref.name == "toLowerCase") {
        std::u16string out = u16;
        for (char16_t &c : out) {
            if (c < 128) {
                c = static_cast<char16_t>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::Utf16ToUtf8(out))), 'L'};
    }
    if (ref.name == "trim" || ref.name == "strip") {
        size_t start = 0;
        size_t end = u16.size();
        while (start < end && u16[start] <= 0x20u) {
            ++start;
        }
        while (end > start && u16[end - 1] <= 0x20u) {
            --end;
        }
        std::string trimmed =
            java_rt::Utf16ToUtf8(std::u16string_view(u16).substr(start, end - start));
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(trimmed)), 'L'};
    }
    if (ref.name == "replace" && arg_regs.size() >= 3 && ref.param_types.size() >= 2) {
        if (ref.param_types[0] == "C") {
            char16_t old_c = static_cast<char16_t>(arg_regs[1]);
            char16_t new_c = static_cast<char16_t>(arg_regs[2]);
            std::u16string out = u16;
            std::replace(out.begin(), out.end(), old_c, new_c);
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.CreateString(java_rt::Utf16ToUtf8(out))), 'L'};
        }
        std::string target;
        std::string repl;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), target);
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[2]), repl);
        std::string out = self_utf8;
        if (!target.empty()) {
            size_t pos = 0;
            while ((pos = out.find(target, pos)) != std::string::npos) {
                out.replace(pos, target.size(), repl);
                pos += repl.size();
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    if (ref.name == "matches" && arg_regs.size() >= 2) {
        std::string regex;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), regex);
        bool matched = java_rt::RegexMatches(self_utf8, regex, out_exception);
        handled = true;
        return {matched ? 1ULL : 0ULL, 'Z'};
    }
    if ((ref.name == "replaceFirst" || ref.name == "replaceAll") && arg_regs.size() >= 3) {
        std::string regex;
        std::string repl;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), regex);
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[2]), repl);
        std::string out = java_rt::RegexReplace(self_utf8, regex, repl, ref.name == "replaceFirst",
                                                out_exception);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    if (ref.name == "split" && arg_regs.size() >= 2) {
        std::string regex;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), regex);
        int32_t limit = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : 0;
        const bool literal_special =
            regex.empty() ||
            (regex.size() == 1 && (regex[0] == '|' || regex[0] == '.' || regex[0] == '+' ||
                                   regex[0] == '*' || regex[0] == '?'));
        std::vector<std::string> parts =
            literal_special ? SplitJavaString(self_utf8, regex, limit)
                            : java_rt::RegexSplit(self_utf8, regex, limit, out_exception);
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ObjectArray;
        arr->class_name = "[Ljava/lang/String;";
        arr->object_data.reserve(parts.size());
        for (const auto &p : parts) {
            arr->object_data.push_back(jni.CreateString(p));
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    if (ref.name == "toCharArray") {
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::CharArray;
        arr->class_name = "[C";
        arr->char_data.assign(u16.begin(), u16.end());
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    if (ref.name == "getBytes") {
        std::string charset = "UTF-8";
        if (!ref.param_types.empty() && arg_regs.size() >= 2) {
            if (ref.param_types[0] == "Ljava/lang/String;") {
                std::string cs;
                ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), cs);
                charset = java_rt::NormalizeCharsetName(cs);
            } else if (ref.param_types[0] == "Ljava/nio/charset/Charset;") {
                auto cs_obj = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
                if (cs_obj) {
                    auto it = cs_obj->string_fields.find("name");
                    if (it != cs_obj->string_fields.end()) {
                        charset = java_rt::NormalizeCharsetName(it->second);
                    }
                }
            }
        }
        std::vector<uint8_t> encoded = java_rt::EncodeStringWithCharset(self_utf8, charset);
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ByteArray;
        arr->class_name = "[B";
        arr->byte_data.assign(reinterpret_cast<const int8_t *>(encoded.data()),
                              reinterpret_cast<const int8_t *>(encoded.data() + encoded.size()));
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }

    return {0, 'V'};
}

DexValue HandleStringBuilder(DexVm &vm, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                             bool is_static, JNIEnv *env, jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);

    if (ref.name == "<init>") {
        std::string init_str;
        if (arg_regs.size() >= 2 && !ref.param_types.empty() && ref.param_types[0] != "I") {
            ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), init_str);
        }
        syn->string_fields["value"] = std::move(init_str);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "append") {
        std::string piece;
        if (!ref.param_types.empty() && ref.param_types[0] == "[C" && ref.param_types.size() == 3 &&
            arg_regs.size() >= 4) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            size_t off = static_cast<size_t>(static_cast<int32_t>(arg_regs[2]));
            size_t cnt = static_cast<size_t>(static_cast<int32_t>(arg_regs[3]));
            if (arr && arr->type == JniObjectType::CharArray && off <= arr->char_data.size() &&
                off + cnt <= arr->char_data.size()) {
                std::u16string u16(arr->char_data.begin() + static_cast<ptrdiff_t>(off),
                                   arr->char_data.begin() + static_cast<ptrdiff_t>(off + cnt));
                piece = java_rt::Utf16ToUtf8(u16);
            }
        } else if (!ref.param_types.empty() && arg_regs.size() >= 2) {
            piece = ValueToStringForAppend(vm, ref.param_types[0], arg_regs, 1, env, out_exception);
            if (out_exception) {
                handled = true;
                return {arg_regs[0], 'L'};
            }
        }
        syn->string_fields["value"] += piece;
        handled = true;
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "insert" && arg_regs.size() >= 3 && ref.param_types.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t offset = static_cast<int32_t>(arg_regs[1]);
        if (offset < 0 || static_cast<size_t>(offset) > u16.size()) {
            handled = true;
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "insert offset out of bounds");
            return {arg_regs[0], 'L'};
        }
        std::string piece =
            ValueToStringForAppend(vm, ref.param_types[1], arg_regs, 2, env, out_exception);
        if (out_exception) {
            handled = true;
            return {arg_regs[0], 'L'};
        }
        u16.insert(static_cast<size_t>(offset), java_rt::Utf8ToUtf16(piece));
        syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        handled = true;
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "reverse") {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        std::reverse(u16.begin(), u16.end());
        syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        handled = true;
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "deleteCharAt" && arg_regs.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx < 0 || static_cast<size_t>(idx) >= u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "index out of bounds");
            return {arg_regs[0], 'L'};
        }
        u16.erase(static_cast<size_t>(idx), 1);
        syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "delete" && arg_regs.size() >= 3) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t start = static_cast<int32_t>(arg_regs[1]);
        int32_t end = std::min(static_cast<int32_t>(arg_regs[2]), static_cast<int32_t>(u16.size()));
        handled = true;
        if (start < 0 || static_cast<size_t>(start) > u16.size() || start > end) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "delete bounds invalid");
            return {arg_regs[0], 'L'};
        }
        u16.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
        syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "replace" && arg_regs.size() >= 4) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t start = static_cast<int32_t>(arg_regs[1]);
        int32_t end = std::min(static_cast<int32_t>(arg_regs[2]), static_cast<int32_t>(u16.size()));
        std::string s;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[3]), s);
        if (start >= 0 && start <= end && static_cast<size_t>(start) <= u16.size()) {
            u16.replace(static_cast<size_t>(start), static_cast<size_t>(end - start),
                        java_rt::Utf8ToUtf16(s));
            syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        }
        handled = true;
        return {arg_regs[0], 'L'};
    }
    if (ref.name == "setLength" && arg_regs.size() >= 2) {
        int32_t new_len = std::max(0, static_cast<int32_t>(arg_regs[1]));
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        u16.resize(static_cast<size_t>(new_len), u'\0');
        syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "charAt" && arg_regs.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx < 0 || static_cast<size_t>(idx) >= u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "index out of bounds");
            return {0, 'C'};
        }
        return {static_cast<uint64_t>(u16[static_cast<size_t>(idx)]), 'C'};
    }
    if (ref.name == "codePointAt" || ref.name == "codePointBefore" ||
        ref.name == "codePointCount") {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        DexValue cpv = TryHandleCodePointQuery(u16, ref, arg_regs, out_exception, handled);
        if (handled) {
            return cpv;
        }
    }
    if (ref.name == "setCharAt" && arg_regs.size() >= 3) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t idx = static_cast<int32_t>(arg_regs[1]);
        handled = true;
        if (idx >= 0 && static_cast<size_t>(idx) < u16.size()) {
            u16[static_cast<size_t>(idx)] = static_cast<char16_t>(arg_regs[2]);
            syn->string_fields["value"] = java_rt::Utf16ToUtf8(u16);
        }
        return {0, 'V'};
    }
    if (ref.name == "indexOf" && arg_regs.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        std::string sub;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), sub);
        size_t from = (arg_regs.size() >= 3)
                          ? static_cast<size_t>(std::max(0, static_cast<int32_t>(arg_regs[2])))
                          : 0;
        size_t pos =
            (from <= u16.size()) ? u16.find(java_rt::Utf8ToUtf16(sub), from) : std::u16string::npos;
        int32_t res = (pos == std::u16string::npos) ? -1 : static_cast<int32_t>(pos);
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(res)), 'I'};
    }
    if (ref.name == "lastIndexOf" && arg_regs.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        std::string sub;
        ReadJavaStringUtf8(reinterpret_cast<jobject>(arg_regs[1]), sub);
        size_t from = (arg_regs.size() >= 3)
                          ? static_cast<size_t>(std::max(0, static_cast<int32_t>(arg_regs[2])))
                          : u16.size();
        size_t pos = u16.rfind(java_rt::Utf8ToUtf16(sub), from);
        int32_t res = (pos == std::u16string::npos) ? -1 : static_cast<int32_t>(pos);
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(res)), 'I'};
    }
    if (ref.name == "substring" && arg_regs.size() >= 2) {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        int32_t begin = static_cast<int32_t>(arg_regs[1]);
        int32_t end = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2])
                                             : static_cast<int32_t>(u16.size());
        handled = true;
        if (begin < 0 || end < begin || static_cast<size_t>(end) > u16.size()) {
            out_exception = JavaRuntime::CreateException(
                "java/lang/StringIndexOutOfBoundsException", "substring out of bounds");
            return {0, 'L'};
        }
        std::string sub = java_rt::Utf16ToUtf8(std::u16string_view(u16).substr(
            static_cast<size_t>(begin), static_cast<size_t>(end - begin)));
        return {reinterpret_cast<uintptr_t>(jni.CreateString(sub)), 'L'};
    }
    if (ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(syn->string_fields["value"])), 'L'};
    }
    if (ref.name == "length") {
        std::u16string u16 = java_rt::Utf8ToUtf16(syn->string_fields["value"]);
        handled = true;
        return {static_cast<uint64_t>(u16.size()), 'I'};
    }
    if (ref.name == "capacity") {
        handled = true;
        return {static_cast<uint64_t>(syn->string_fields["value"].size() + 16), 'I'};
    }
    if (ref.name == "ensureCapacity") {
        handled = true;
        return {0, 'V'};
    }
    return {0, 'V'};
}

} // namespace

void RegisterStringIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/lang/String", &HandleString,
                     {"java/lang/CharSequence", "java/lang/Comparable", "java/io/Serializable",
                      "java/lang/Object"});
    rt.RegisterClass("java/lang/StringBuilder", &HandleStringBuilder,
                     {"java/lang/CharSequence", "java/lang/Appendable", "java/io/Serializable",
                      "java/lang/Object"});
    rt.RegisterClass("java/lang/StringBuffer", &HandleStringBuilder,
                     {"java/lang/CharSequence", "java/lang/Appendable", "java/io/Serializable",
                      "java/lang/Object"});
}

} // namespace portage::package
