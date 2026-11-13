#include "package/java_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace portage::package {

namespace {

enum class BufferKind { Byte, Short, Int, Long, Float, Double };

struct NioBufferBacking {
    std::vector<uint8_t> data{};
    bool is_direct{false};
};

struct NioBufferPayload {
    std::shared_ptr<NioBufferBacking> backing;
    BufferKind kind{BufferKind::Byte};
    size_t byte_offset{0};
    int32_t capacity{0};
    int32_t limit{0};
    int32_t position{0};
    int32_t mark{-1};
    bool little_endian{false};
    bool read_only{false};
};

size_t ElementByteSize(BufferKind k)
{
    switch (k) {
    case BufferKind::Byte:
        return 1;
    case BufferKind::Short:
        return 2;
    case BufferKind::Int:
    case BufferKind::Float:
        return 4;
    case BufferKind::Long:
    case BufferKind::Double:
        return 8;
    }
    return 1;
}

std::string ClassNameForKind(BufferKind k, bool direct)
{
    switch (k) {
    case BufferKind::Byte:
        return direct ? "java/nio/DirectByteBuffer" : "java/nio/ByteBuffer";
    case BufferKind::Short:
        return "java/nio/ShortBuffer";
    case BufferKind::Int:
        return "java/nio/IntBuffer";
    case BufferKind::Long:
        return "java/nio/LongBuffer";
    case BufferKind::Float:
        return "java/nio/FloatBuffer";
    case BufferKind::Double:
        return "java/nio/DoubleBuffer";
    }
    return "java/nio/ByteBuffer";
}

jobject CreateByteOrderObj(bool little_endian)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/nio/ByteOrder";
    syn->bool_fields["littleEndian"] = little_endian ? 1 : 0;
    syn->string_fields["name"] = little_endian ? "LITTLE_ENDIAN" : "BIG_ENDIAN";
    return JniEnvironment::Instance().RegisterObject(syn);
}

uint64_t GetBigEndianOrder()
{
    static uint64_t epoch = 0;
    static jobject obj = nullptr;
    uint64_t cur = JniEnvironment::Instance().GetResetEpoch();
    if (!obj || epoch != cur) {
        epoch = cur;
        obj = CreateByteOrderObj(false);
    }
    return reinterpret_cast<uintptr_t>(obj);
}

uint64_t GetLittleEndianOrder()
{
    static uint64_t epoch = 0;
    static jobject obj = nullptr;
    uint64_t cur = JniEnvironment::Instance().GetResetEpoch();
    if (!obj || epoch != cur) {
        epoch = cur;
        obj = CreateByteOrderObj(true);
    }
    return reinterpret_cast<uintptr_t>(obj);
}

jobject CreateCharsetObj(std::string_view canonical_name)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/nio/charset/Charset";
    syn->string_fields["name"] = std::string(canonical_name);
    return JniEnvironment::Instance().RegisterObject(syn);
}

uint64_t GetNamedCharset(std::string_view canonical_name)
{
    auto &jni = JniEnvironment::Instance();
    static std::unordered_map<std::string, jobject> cache;
    static uint64_t epoch = 0;
    uint64_t cur = jni.GetResetEpoch();
    if (epoch != cur) {
        cache.clear();
        epoch = cur;
    }
    std::string key(canonical_name);
    if (auto it = cache.find(key); it != cache.end()) {
        return reinterpret_cast<uintptr_t>(it->second);
    }
    jobject obj = CreateCharsetObj(canonical_name);
    cache[std::move(key)] = obj;
    return reinterpret_cast<uintptr_t>(obj);
}

} // namespace

namespace java_rt {

std::string NormalizeCharsetName(std::string_view raw)
{
    std::string upper;
    for (unsigned char c : raw) {
        if (c != '-' && c != '_') {
            upper.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    if (upper == "UTF8") {
        return "UTF-8";
    }
    if (upper == "UTF16") {
        return "UTF-16";
    }
    if (upper == "UTF16BE") {
        return "UTF-16BE";
    }
    if (upper == "UTF16LE") {
        return "UTF-16LE";
    }
    if (upper == "USASCII" || upper == "ASCII") {
        return "US-ASCII";
    }
    if (upper == "ISO88591" || upper == "LATIN1") {
        return "ISO-8859-1";
    }
    return {};
}

std::vector<uint8_t> EncodeStringWithCharset(std::string_view utf8, std::string_view charset)
{
    if (charset == "UTF-8" || charset.empty()) {
        return std::vector<uint8_t>(utf8.begin(), utf8.end());
    }
    std::u16string u16 = Utf8ToUtf16(utf8);
    std::vector<uint8_t> out;
    if (charset == "UTF-16LE") {
        out.reserve(u16.size() * 2);
        for (char16_t ch : u16) {
            out.push_back(static_cast<uint8_t>(ch & 0xFFu));
            out.push_back(static_cast<uint8_t>((ch >> 8) & 0xFFu));
        }
    } else if (charset == "UTF-16BE" || charset == "UTF-16") {
        out.reserve(u16.size() * 2 + (charset == "UTF-16" ? 2 : 0));
        if (charset == "UTF-16") {
            out.push_back(0xFEu);
            out.push_back(0xFFu);
        }
        for (char16_t ch : u16) {
            out.push_back(static_cast<uint8_t>((ch >> 8) & 0xFFu));
            out.push_back(static_cast<uint8_t>(ch & 0xFFu));
        }
    } else if (charset == "ISO-8859-1") {
        out.reserve(u16.size());
        for (char16_t ch : u16) {
            out.push_back(ch <= 0xFFu ? static_cast<uint8_t>(ch) : static_cast<uint8_t>('?'));
        }
    } else {
        out.reserve(u16.size());
        for (char16_t ch : u16) {
            out.push_back(ch <= 0x7Fu ? static_cast<uint8_t>(ch) : static_cast<uint8_t>('?'));
        }
    }
    return out;
}

std::string DecodeBytesWithCharset(std::span<const uint8_t> bytes, std::string_view charset)
{
    if (charset == "UTF-8" || charset.empty()) {
        return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    if (charset == "UTF-16LE" || charset == "UTF-16BE" || charset == "UTF-16") {
        bool le = (charset == "UTF-16LE");
        size_t start = 0;
        if (charset == "UTF-16" && bytes.size() >= 2) {
            if (bytes[0] == 0xFFu && bytes[1] == 0xFEu) {
                le = true;
                start = 2;
            } else if (bytes[0] == 0xFEu && bytes[1] == 0xFFu) {
                le = false;
                start = 2;
            }
        }
        std::u16string u16;
        for (size_t i = start; i + 1 < bytes.size(); i += 2) {
            char16_t ch = le ? static_cast<char16_t>(bytes[i] | (bytes[i + 1] << 8))
                             : static_cast<char16_t>((bytes[i] << 8) | bytes[i + 1]);
            u16.push_back(ch);
        }
        return Utf16ToUtf8(u16);
    }
    if (charset == "ISO-8859-1") {
        std::u16string u16;
        u16.reserve(bytes.size());
        for (uint8_t b : bytes) {
            u16.push_back(static_cast<char16_t>(b));
        }
        return Utf16ToUtf8(u16);
    }
    std::string out;
    out.reserve(bytes.size());
    for (uint8_t b : bytes) {
        out.push_back(b <= 0x7Fu ? static_cast<char>(b) : '?');
    }
    return out;
}

} // namespace java_rt

namespace {

using java_rt::NormalizeCharsetName;

std::vector<uint8_t> EncodeUtf8ToCharset(std::string_view utf8, std::string_view charset)
{
    return java_rt::EncodeStringWithCharset(utf8, charset);
}

std::string DecodeCharsetToUtf8(std::span<const uint8_t> bytes, std::string_view charset)
{
    return java_rt::DecodeBytesWithCharset(bytes, charset);
}

jobject RegisterNioBuffer(std::shared_ptr<NioBufferPayload> payload)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    bool direct = payload->backing && payload->backing->is_direct;
    if (direct && payload->kind == BufferKind::Byte) {
        syn->type = JniObjectType::DirectBuffer;
        syn->buffer_address = payload->backing->data.data() + payload->byte_offset;
        syn->buffer_capacity = payload->capacity;
    } else {
        syn->type = JniObjectType::Generic;
    }
    syn->class_name = ClassNameForKind(payload->kind, direct);
    syn->runtime_payload = std::move(payload);
    return JniEnvironment::Instance().RegisterObject(syn);
}

std::shared_ptr<NioBufferPayload> GetNioPayload(jobject obj)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn || !syn->runtime_payload) {
        return nullptr;
    }
    return std::static_pointer_cast<NioBufferPayload>(syn->runtime_payload);
}

uint64_t ReadBytesOrdered(const uint8_t *p, size_t n, bool le)
{
    uint64_t val = 0;
    if (le) {
        for (size_t i = 0; i < n; ++i) {
            val |= static_cast<uint64_t>(p[i]) << (i * 8u);
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            val = (val << 8u) | static_cast<uint64_t>(p[i]);
        }
    }
    return val;
}

void WriteBytesOrdered(uint8_t *p, size_t n, uint64_t val, bool le)
{
    if (le) {
        for (size_t i = 0; i < n; ++i) {
            p[i] = static_cast<uint8_t>((val >> (i * 8u)) & 0xFFu);
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            p[n - 1 - i] = static_cast<uint8_t>((val >> (i * 8u)) & 0xFFu);
        }
    }
}

DexValue HandleBufferCommon(NioBufferPayload &buf, jobject self,
                            std::shared_ptr<JniSyntheticObject> syn, const DexMethodRef &ref,
                            std::span<const uint64_t> arg_regs, jobject &out_exception,
                            bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    size_t elem_sz = ElementByteSize(buf.kind);

    if (ref.name == "capacity") {
        handled = true;
        return {static_cast<uint64_t>(buf.capacity), 'I'};
    }
    if (ref.name == "limit") {
        handled = true;
        if (arg_regs.size() >= 2 && !ref.param_types.empty()) {
            int32_t nl = static_cast<int32_t>(arg_regs[1]);
            if (nl < 0 || nl > buf.capacity) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "Invalid limit");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            buf.limit = nl;
            if (buf.position > buf.limit) {
                buf.position = buf.limit;
            }
            if (buf.mark > buf.limit) {
                buf.mark = -1;
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        return {static_cast<uint64_t>(buf.limit), 'I'};
    }
    if (ref.name == "position") {
        handled = true;
        if (arg_regs.size() >= 2 && !ref.param_types.empty()) {
            int32_t np = static_cast<int32_t>(arg_regs[1]);
            if (np < 0 || np > buf.limit) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "Invalid position");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            buf.position = np;
            if (buf.mark > buf.position) {
                buf.mark = -1;
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        return {static_cast<uint64_t>(buf.position), 'I'};
    }
    if (ref.name == "remaining") {
        handled = true;
        return {static_cast<uint64_t>(std::max(0, buf.limit - buf.position)), 'I'};
    }
    if (ref.name == "hasRemaining") {
        handled = true;
        return {static_cast<uint64_t>(buf.position < buf.limit ? 1 : 0), 'Z'};
    }
    if (ref.name == "flip") {
        buf.limit = buf.position;
        buf.position = 0;
        buf.mark = -1;
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "clear") {
        buf.position = 0;
        buf.limit = buf.capacity;
        buf.mark = -1;
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "rewind") {
        buf.position = 0;
        buf.mark = -1;
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "mark") {
        buf.mark = buf.position;
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "reset") {
        if (buf.mark < 0) {
            out_exception =
                JavaRuntime::CreateException("java/nio/InvalidMarkException", "Mark not set");
        } else {
            buf.position = buf.mark;
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "isDirect") {
        handled = true;
        return {static_cast<uint64_t>(buf.backing && buf.backing->is_direct ? 1 : 0), 'Z'};
    }
    if (ref.name == "isReadOnly") {
        handled = true;
        return {static_cast<uint64_t>(buf.read_only ? 1 : 0), 'Z'};
    }
    if (ref.name == "hasArray") {
        handled = true;
        return {static_cast<uint64_t>(
                    !buf.read_only && !(buf.backing && buf.backing->is_direct) ? 1 : 0),
                'Z'};
    }
    if (ref.name == "arrayOffset") {
        handled = true;
        return {static_cast<uint64_t>(buf.byte_offset / elem_sz), 'I'};
    }
    if (ref.name == "order") {
        handled = true;
        if (arg_regs.size() >= 2 && !ref.param_types.empty()) {
            auto osyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            if (osyn) {
                auto it = osyn->bool_fields.find("littleEndian");
                buf.little_endian = (it != osyn->bool_fields.end() && it->second != 0);
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        return {buf.little_endian ? GetLittleEndianOrder() : GetBigEndianOrder(), 'L'};
    }
    if (ref.name == "slice" || ref.name == "duplicate" || ref.name == "asReadOnlyBuffer") {
        auto copy = std::make_shared<NioBufferPayload>();
        copy->backing = buf.backing;
        copy->kind = buf.kind;
        copy->little_endian = buf.little_endian;
        copy->read_only = buf.read_only || (ref.name == "asReadOnlyBuffer");
        if (ref.name == "slice") {
            int32_t rem = std::max(0, buf.limit - buf.position);
            copy->byte_offset = buf.byte_offset + static_cast<size_t>(buf.position) * elem_sz;
            copy->capacity = rem;
            copy->limit = rem;
            copy->position = 0;
        } else {
            copy->byte_offset = buf.byte_offset;
            copy->capacity = buf.capacity;
            copy->limit = buf.limit;
            copy->position = buf.position;
            copy->mark = buf.mark;
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(copy))), 'L'};
    }
    if (ref.name == "compact") {
        if (buf.read_only) {
            out_exception = JavaRuntime::CreateException("java/nio/ReadOnlyBufferException", "");
            handled = true;
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        int32_t rem = std::max(0, buf.limit - buf.position);
        if (rem > 0 && buf.position > 0) {
            std::memmove(buf.backing->data.data() + buf.byte_offset,
                         buf.backing->data.data() + buf.byte_offset +
                             static_cast<size_t>(buf.position) * elem_sz,
                         static_cast<size_t>(rem) * elem_sz);
        }
        buf.position = rem;
        buf.limit = buf.capacity;
        buf.mark = -1;
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "array" && buf.kind == BufferKind::Byte) {
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ByteArray;
        arr->class_name = "[B";
        size_t len = static_cast<size_t>(buf.capacity);
        arr->byte_data.resize(len);
        if (buf.backing && buf.byte_offset + len <= buf.backing->data.size()) {
            std::memcpy(arr->byte_data.data(), buf.backing->data.data() + buf.byte_offset, len);
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    (void)syn;
    return {0, 'V'};
}

DexValue HandleByteBuffer(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                          bool is_static, JNIEnv *, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static) {
        if ((ref.name == "allocate" || ref.name == "allocateDirect") && !arg_regs.empty()) {
            int32_t cap = static_cast<int32_t>(arg_regs[0]);
            if (cap < 0) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "Negative capacity");
                handled = true;
                return {0, 'L'};
            }
            auto backing = std::make_shared<NioBufferBacking>();
            backing->data.assign(static_cast<size_t>(cap), 0);
            backing->is_direct = (ref.name == "allocateDirect");
            auto payload = std::make_shared<NioBufferPayload>();
            payload->backing = std::move(backing);
            payload->kind = BufferKind::Byte;
            payload->capacity = cap;
            payload->limit = cap;
            handled = true;
            return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(payload))), 'L'};
        }
        if (ref.name == "wrap" && !arg_regs.empty()) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            if (!arr || arr->type != JniObjectType::ByteArray) {
                handled = true;
                return {0, 'L'};
            }
            int32_t total = static_cast<int32_t>(arr->byte_data.size());
            int32_t off = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[1]) : 0;
            int32_t len = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : total;
            if (off < 0 || len < 0 || off + len > total) {
                out_exception = JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException",
                                                             "Invalid wrap range");
                handled = true;
                return {0, 'L'};
            }
            auto backing = std::make_shared<NioBufferBacking>();
            backing->data.resize(static_cast<size_t>(total));
            if (total > 0) {
                std::memcpy(backing->data.data(), arr->byte_data.data(),
                            static_cast<size_t>(total));
            }
            auto payload = std::make_shared<NioBufferPayload>();
            payload->backing = std::move(backing);
            payload->kind = BufferKind::Byte;
            payload->capacity = total;
            payload->position = off;
            payload->limit = off + len;
            handled = true;
            return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(payload))), 'L'};
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
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    if (!syn->runtime_payload && syn->type == JniObjectType::DirectBuffer && syn->buffer_address &&
        syn->buffer_capacity >= 0) {
        auto backing = std::make_shared<NioBufferBacking>();
        size_t cap = static_cast<size_t>(syn->buffer_capacity);
        backing->data.resize(cap);
        if (cap > 0) {
            std::memcpy(backing->data.data(), syn->buffer_address, cap);
        }
        backing->is_direct = true;
        auto payload = std::make_shared<NioBufferPayload>();
        payload->backing = std::move(backing);
        payload->kind = BufferKind::Byte;
        payload->capacity = static_cast<int32_t>(cap);
        payload->limit = static_cast<int32_t>(cap);
        syn->runtime_payload = std::move(payload);
    }
    auto buf = GetNioPayload(self);
    if (!buf || !buf->backing) {
        return {0, 'V'};
    }

    DexValue common = HandleBufferCommon(*buf, self, syn, ref, arg_regs, out_exception, handled);
    if (handled) {
        return common;
    }

    auto read_type = [&](size_t byte_width, bool indexed, int32_t idx, uint64_t &out_raw) -> bool {
        int32_t pos = indexed ? idx : buf->position;
        if (pos < 0 || pos + static_cast<int32_t>(byte_width) > buf->limit) {
            out_exception =
                JavaRuntime::CreateException(indexed ? "java/lang/IndexOutOfBoundsException"
                                                     : "java/nio/BufferUnderflowException",
                                             "Buffer read out of bounds");
            return false;
        }
        out_raw = ReadBytesOrdered(buf->backing->data.data() + buf->byte_offset +
                                       static_cast<size_t>(pos),
                                   byte_width, buf->little_endian);
        if (!indexed) {
            buf->position += static_cast<int32_t>(byte_width);
        }
        return true;
    };

    auto write_type = [&](size_t byte_width, bool indexed, int32_t idx, uint64_t val) -> bool {
        if (buf->read_only) {
            out_exception = JavaRuntime::CreateException("java/nio/ReadOnlyBufferException", "");
            return false;
        }
        int32_t pos = indexed ? idx : buf->position;
        if (pos < 0 || pos + static_cast<int32_t>(byte_width) > buf->limit) {
            out_exception =
                JavaRuntime::CreateException(indexed ? "java/lang/IndexOutOfBoundsException"
                                                     : "java/nio/BufferOverflowException",
                                             "Buffer write out of bounds");
            return false;
        }
        WriteBytesOrdered(buf->backing->data.data() + buf->byte_offset + static_cast<size_t>(pos),
                          byte_width, val, buf->little_endian);
        if (!indexed) {
            buf->position += static_cast<int32_t>(byte_width);
        }
        return true;
    };

    if (ref.name == "get") {
        handled = true;
        if (ref.param_types.empty()) {
            uint64_t v = 0;
            if (read_type(1, false, 0, v)) {
                return {static_cast<uint64_t>(static_cast<int32_t>(static_cast<int8_t>(v))), 'B'};
            }
            return {0, 'B'};
        }
        if (ref.param_types[0] == "I" && arg_regs.size() >= 2) {
            uint64_t v = 0;
            if (read_type(1, true, static_cast<int32_t>(arg_regs[1]), v)) {
                return {static_cast<uint64_t>(static_cast<int32_t>(static_cast<int8_t>(v))), 'B'};
            }
            return {0, 'B'};
        }
        if (ref.param_types[0] == "[B" && arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            int32_t off = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[2]) : 0;
            int32_t len = (arg_regs.size() >= 4)
                              ? static_cast<int32_t>(arg_regs[3])
                              : (arr ? static_cast<int32_t>(arr->byte_data.size()) : 0);
            if (!arr || arr->type != JniObjectType::ByteArray || off < 0 || len < 0 ||
                static_cast<size_t>(off + len) > arr->byte_data.size()) {
                out_exception =
                    JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (buf->position + len > buf->limit) {
                out_exception =
                    JavaRuntime::CreateException("java/nio/BufferUnderflowException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (len > 0) {
                std::memcpy(arr->byte_data.data() + off,
                            buf->backing->data.data() + buf->byte_offset +
                                static_cast<size_t>(buf->position),
                            static_cast<size_t>(len));
                buf->position += len;
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
    }

    if (ref.name == "put") {
        handled = true;
        if (ref.param_types.size() == 1 && ref.param_types[0] == "B" && arg_regs.size() >= 2) {
            write_type(1, false, 0, arg_regs[1] & 0xFFu);
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        if (ref.param_types.size() == 2 && ref.param_types[0] == "I" && ref.param_types[1] == "B" &&
            arg_regs.size() >= 3) {
            write_type(1, true, static_cast<int32_t>(arg_regs[1]), arg_regs[2] & 0xFFu);
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        if (!ref.param_types.empty() && ref.param_types[0] == "[B" && arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            int32_t off = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[2]) : 0;
            int32_t len = (arg_regs.size() >= 4)
                              ? static_cast<int32_t>(arg_regs[3])
                              : (arr ? static_cast<int32_t>(arr->byte_data.size()) : 0);
            if (!arr || arr->type != JniObjectType::ByteArray || off < 0 || len < 0 ||
                static_cast<size_t>(off + len) > arr->byte_data.size()) {
                out_exception =
                    JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (buf->position + len > buf->limit) {
                out_exception =
                    JavaRuntime::CreateException("java/nio/BufferOverflowException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (len > 0) {
                std::memcpy(buf->backing->data.data() + buf->byte_offset +
                                static_cast<size_t>(buf->position),
                            arr->byte_data.data() + off, static_cast<size_t>(len));
                buf->position += len;
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        if (!ref.param_types.empty() && ref.param_types[0] == "Ljava/nio/ByteBuffer;" &&
            arg_regs.size() >= 2) {
            auto src = GetNioPayload(reinterpret_cast<jobject>(arg_regs[1]));
            if (src && src->backing) {
                int32_t rem = std::max(0, src->limit - src->position);
                if (buf->position + rem > buf->limit) {
                    out_exception =
                        JavaRuntime::CreateException("java/nio/BufferOverflowException", "");
                    return {reinterpret_cast<uintptr_t>(self), 'L'};
                }
                if (rem > 0) {
                    std::memmove(buf->backing->data.data() + buf->byte_offset +
                                     static_cast<size_t>(buf->position),
                                 src->backing->data.data() + src->byte_offset +
                                     static_cast<size_t>(src->position),
                                 static_cast<size_t>(rem));
                    src->position += rem;
                    buf->position += rem;
                }
            }
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
    }

    auto handle_typed_rw = [&](std::string_view suffix, size_t width, char ret_k) -> bool {
        if (ref.name == std::string("get") + std::string(suffix)) {
            handled = true;
            bool indexed =
                !ref.param_types.empty() && ref.param_types[0] == "I" && arg_regs.size() >= 2;
            uint64_t raw = 0;
            if (read_type(width, indexed, indexed ? static_cast<int32_t>(arg_regs[1]) : 0, raw)) {
                if (ret_k == 'S') {
                    raw = static_cast<uint64_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                } else if (ret_k == 'I' || ret_k == 'F') {
                    raw = static_cast<uint64_t>(static_cast<uint32_t>(raw));
                }
            }
            return true;
        }
        return false;
    };
    (void)handle_typed_rw;

    struct TypedSpec {
        const char *name;
        size_t width;
        char kind;
        bool is_signed_16;
    };
    static constexpr TypedSpec kTypedSpecs[] = {
        {"Short", 2, 'S', true},  {"Char", 2, 'C', false}, {"Int", 4, 'I', false},
        {"Float", 4, 'F', false}, {"Long", 8, 'J', false}, {"Double", 8, 'D', false},
    };
    for (const auto &sp : kTypedSpecs) {
        if (ref.name == std::string("get") + sp.name) {
            handled = true;
            bool indexed =
                !ref.param_types.empty() && ref.param_types[0] == "I" && arg_regs.size() >= 2;
            uint64_t raw = 0;
            if (!read_type(sp.width, indexed, indexed ? static_cast<int32_t>(arg_regs[1]) : 0,
                           raw)) {
                return {0, sp.kind};
            }
            if (sp.is_signed_16) {
                raw = static_cast<uint64_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
            }
            return {raw, sp.kind};
        }
        if (ref.name == std::string("put") + sp.name) {
            handled = true;
            bool indexed = ref.param_types.size() == 2 && ref.param_types[0] == "I";
            int32_t idx = indexed && arg_regs.size() >= 2 ? static_cast<int32_t>(arg_regs[1]) : 0;
            size_t val_reg = indexed ? 2 : 1;
            uint64_t val = 0;
            if (sp.width == 8 && arg_regs.size() > val_reg + 1) {
                val = (arg_regs[val_reg] & 0xFFFFFFFFULL) |
                      ((arg_regs[val_reg + 1] & 0xFFFFFFFFULL) << 32);
            } else if (arg_regs.size() > val_reg) {
                val = arg_regs[val_reg];
            }
            write_type(sp.width, indexed, idx, val);
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
    }

    auto make_view = [&](BufferKind vk) -> jobject {
        size_t vsz = ElementByteSize(vk);
        int32_t rem_bytes = std::max(0, buf->limit - buf->position);
        auto vp = std::make_shared<NioBufferPayload>();
        vp->backing = buf->backing;
        vp->kind = vk;
        vp->byte_offset = buf->byte_offset + static_cast<size_t>(buf->position);
        vp->capacity = rem_bytes / static_cast<int32_t>(vsz);
        vp->limit = vp->capacity;
        vp->little_endian = buf->little_endian;
        vp->read_only = buf->read_only;
        return RegisterNioBuffer(std::move(vp));
    };
    if (ref.name == "asShortBuffer") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(make_view(BufferKind::Short)), 'L'};
    }
    if (ref.name == "asIntBuffer") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(make_view(BufferKind::Int)), 'L'};
    }
    if (ref.name == "asFloatBuffer") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(make_view(BufferKind::Float)), 'L'};
    }
    if (ref.name == "asLongBuffer") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(make_view(BufferKind::Long)), 'L'};
    }
    if (ref.name == "asDoubleBuffer") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(make_view(BufferKind::Double)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleTypedBuffer(BufferKind kind, const DexMethodRef &ref,
                           std::span<const uint64_t> arg_regs, bool is_static,
                           jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    size_t esz = ElementByteSize(kind);
    if (is_static) {
        if (ref.name == "allocate" && !arg_regs.empty()) {
            int32_t cap = static_cast<int32_t>(arg_regs[0]);
            if (cap < 0) {
                out_exception = JavaRuntime::CreateException("java/lang/IllegalArgumentException",
                                                             "Negative capacity");
                handled = true;
                return {0, 'L'};
            }
            auto backing = std::make_shared<NioBufferBacking>();
            backing->data.assign(static_cast<size_t>(cap) * esz, 0);
            auto vp = std::make_shared<NioBufferPayload>();
            vp->backing = std::move(backing);
            vp->kind = kind;
            vp->capacity = cap;
            vp->limit = cap;
            vp->little_endian = true;
            handled = true;
            return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(vp))), 'L'};
        }
        if (ref.name == "wrap" && !arg_regs.empty()) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
            if (!arr) {
                handled = true;
                return {0, 'L'};
            }
            int32_t count = 0;
            if (kind == BufferKind::Int) {
                count = static_cast<int32_t>(arr->int_data.size());
            } else if (kind == BufferKind::Float) {
                count = static_cast<int32_t>(arr->float_data.size());
            } else if (kind == BufferKind::Short) {
                count = static_cast<int32_t>(arr->short_data.size());
            } else if (kind == BufferKind::Long) {
                count = static_cast<int32_t>(arr->long_data.size());
            } else if (kind == BufferKind::Double) {
                count = static_cast<int32_t>(arr->double_data.size());
            }
            auto backing = std::make_shared<NioBufferBacking>();
            backing->data.assign(static_cast<size_t>(count) * esz, 0);
            for (int32_t i = 0; i < count; ++i) {
                uint64_t val = 0;
                if (kind == BufferKind::Int) {
                    val = static_cast<uint32_t>(arr->int_data[static_cast<size_t>(i)]);
                } else if (kind == BufferKind::Float) {
                    uint32_t bits = 0;
                    std::memcpy(&bits, &arr->float_data[static_cast<size_t>(i)], sizeof(bits));
                    val = bits;
                } else if (kind == BufferKind::Short) {
                    val = static_cast<uint16_t>(arr->short_data[static_cast<size_t>(i)]);
                } else if (kind == BufferKind::Long) {
                    val = static_cast<uint64_t>(arr->long_data[static_cast<size_t>(i)]);
                } else if (kind == BufferKind::Double) {
                    std::memcpy(&val, &arr->double_data[static_cast<size_t>(i)], sizeof(val));
                }
                WriteBytesOrdered(backing->data.data() + static_cast<size_t>(i) * esz, esz, val,
                                  true);
            }
            auto vp = std::make_shared<NioBufferPayload>();
            vp->backing = std::move(backing);
            vp->kind = kind;
            vp->capacity = count;
            vp->limit = count;
            vp->little_endian = true;
            handled = true;
            return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(vp))), 'L'};
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
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);
    auto buf = GetNioPayload(self);
    if (!buf || !buf->backing) {
        return {0, 'V'};
    }
    DexValue common = HandleBufferCommon(*buf, self, syn, ref, arg_regs, out_exception, handled);
    if (handled) {
        return common;
    }

    char ret_k = 'I';
    if (kind == BufferKind::Float) {
        ret_k = 'F';
    } else if (kind == BufferKind::Long) {
        ret_k = 'J';
    } else if (kind == BufferKind::Double) {
        ret_k = 'D';
    } else if (kind == BufferKind::Short) {
        ret_k = 'S';
    }

    if (ref.name == "get") {
        handled = true;
        if (!ref.param_types.empty() && ref.param_types[0].front() == '[' && arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            int32_t off = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[2]) : 0;
            int32_t arr_len = 0;
            if (arr) {
                if (kind == BufferKind::Int) {
                    arr_len = static_cast<int32_t>(arr->int_data.size());
                } else if (kind == BufferKind::Float) {
                    arr_len = static_cast<int32_t>(arr->float_data.size());
                } else if (kind == BufferKind::Short) {
                    arr_len = static_cast<int32_t>(arr->short_data.size());
                } else if (kind == BufferKind::Long) {
                    arr_len = static_cast<int32_t>(arr->long_data.size());
                } else if (kind == BufferKind::Double) {
                    arr_len = static_cast<int32_t>(arr->double_data.size());
                }
            }
            int32_t len = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[3]) : arr_len;
            if (!arr || off < 0 || len < 0 || off + len > arr_len) {
                out_exception =
                    JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (buf->position + len > buf->limit) {
                out_exception =
                    JavaRuntime::CreateException("java/nio/BufferUnderflowException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            for (int32_t i = 0; i < len; ++i) {
                uint64_t val = ReadBytesOrdered(buf->backing->data.data() + buf->byte_offset +
                                                    static_cast<size_t>(buf->position + i) * esz,
                                                esz, buf->little_endian);
                size_t dst = static_cast<size_t>(off + i);
                if (kind == BufferKind::Int) {
                    arr->int_data[dst] = static_cast<int32_t>(val);
                } else if (kind == BufferKind::Float) {
                    uint32_t bits = static_cast<uint32_t>(val);
                    std::memcpy(&arr->float_data[dst], &bits, sizeof(bits));
                } else if (kind == BufferKind::Short) {
                    arr->short_data[dst] = static_cast<int16_t>(val);
                } else if (kind == BufferKind::Long) {
                    arr->long_data[dst] = static_cast<int64_t>(val);
                } else if (kind == BufferKind::Double) {
                    std::memcpy(&arr->double_data[dst], &val, sizeof(val));
                }
            }
            buf->position += len;
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        bool indexed =
            !ref.param_types.empty() && ref.param_types[0] == "I" && arg_regs.size() >= 2;
        int32_t idx = indexed ? static_cast<int32_t>(arg_regs[1]) : buf->position;
        if (idx < 0 || idx >= buf->limit) {
            out_exception =
                JavaRuntime::CreateException(indexed ? "java/lang/IndexOutOfBoundsException"
                                                     : "java/nio/BufferUnderflowException",
                                             "");
            return {0, ret_k};
        }
        uint64_t raw = ReadBytesOrdered(buf->backing->data.data() + buf->byte_offset +
                                            static_cast<size_t>(idx) * esz,
                                        esz, buf->little_endian);
        if (!indexed) {
            buf->position++;
        }
        if (kind == BufferKind::Short) {
            raw = static_cast<uint64_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
        }
        return {raw, ret_k};
    }

    if (ref.name == "put") {
        handled = true;
        if (buf->read_only) {
            out_exception = JavaRuntime::CreateException("java/nio/ReadOnlyBufferException", "");
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        if (!ref.param_types.empty() && ref.param_types[0].front() == '[' && arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            int32_t off = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[2]) : 0;
            int32_t arr_len = 0;
            if (arr) {
                if (kind == BufferKind::Int) {
                    arr_len = static_cast<int32_t>(arr->int_data.size());
                } else if (kind == BufferKind::Float) {
                    arr_len = static_cast<int32_t>(arr->float_data.size());
                } else if (kind == BufferKind::Short) {
                    arr_len = static_cast<int32_t>(arr->short_data.size());
                } else if (kind == BufferKind::Long) {
                    arr_len = static_cast<int32_t>(arr->long_data.size());
                } else if (kind == BufferKind::Double) {
                    arr_len = static_cast<int32_t>(arr->double_data.size());
                }
            }
            int32_t len = (arg_regs.size() >= 4) ? static_cast<int32_t>(arg_regs[3]) : arr_len;
            if (!arr || off < 0 || len < 0 || off + len > arr_len) {
                out_exception =
                    JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            if (buf->position + len > buf->limit) {
                out_exception =
                    JavaRuntime::CreateException("java/nio/BufferOverflowException", "");
                return {reinterpret_cast<uintptr_t>(self), 'L'};
            }
            for (int32_t i = 0; i < len; ++i) {
                size_t src = static_cast<size_t>(off + i);
                uint64_t val = 0;
                if (kind == BufferKind::Int) {
                    val = static_cast<uint32_t>(arr->int_data[src]);
                } else if (kind == BufferKind::Float) {
                    uint32_t bits = 0;
                    std::memcpy(&bits, &arr->float_data[src], sizeof(bits));
                    val = bits;
                } else if (kind == BufferKind::Short) {
                    val = static_cast<uint16_t>(arr->short_data[src]);
                } else if (kind == BufferKind::Long) {
                    val = static_cast<uint64_t>(arr->long_data[src]);
                } else if (kind == BufferKind::Double) {
                    std::memcpy(&val, &arr->double_data[src], sizeof(val));
                }
                WriteBytesOrdered(buf->backing->data.data() + buf->byte_offset +
                                      static_cast<size_t>(buf->position + i) * esz,
                                  esz, val, buf->little_endian);
            }
            buf->position += len;
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        bool indexed = ref.param_types.size() == 2 && ref.param_types[0] == "I";
        int32_t idx =
            indexed && arg_regs.size() >= 2 ? static_cast<int32_t>(arg_regs[1]) : buf->position;
        if (idx < 0 || idx >= buf->limit) {
            out_exception =
                JavaRuntime::CreateException(indexed ? "java/lang/IndexOutOfBoundsException"
                                                     : "java/nio/BufferOverflowException",
                                             "");
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        size_t vreg = indexed ? 2 : 1;
        uint64_t val = 0;
        if (esz == 8 && arg_regs.size() > vreg + 1) {
            val = (arg_regs[vreg] & 0xFFFFFFFFULL) | ((arg_regs[vreg + 1] & 0xFFFFFFFFULL) << 32);
        } else if (arg_regs.size() > vreg) {
            val = arg_regs[vreg];
        }
        WriteBytesOrdered(buf->backing->data.data() + buf->byte_offset +
                              static_cast<size_t>(idx) * esz,
                          esz, val, buf->little_endian);
        if (!indexed) {
            buf->position++;
        }
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleByteOrder(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                         bool is_static, JNIEnv *, jobject &, bool &handled)
{
    if (is_static && ref.name == "nativeOrder") {
        handled = true;
        return {GetLittleEndianOrder(), 'L'};
    }
    if (!is_static && ref.name == "toString" && !arg_regs.empty()) {
        auto syn = JniEnvironment::Instance().LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
        std::string nm = syn ? syn->string_fields["name"] : "BIG_ENDIAN";
        handled = true;
        return {reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(nm)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleCharset(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static) {
        if (ref.name == "defaultCharset") {
            handled = true;
            return {GetNamedCharset("UTF-8"), 'L'};
        }
        if (ref.name == "forName" && !arg_regs.empty()) {
            const char *nm = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
            std::string canon = nm ? NormalizeCharsetName(nm) : "";
            if (canon.empty()) {
                out_exception = JavaRuntime::CreateException(
                    "java/nio/charset/UnsupportedCharsetException", nm ? nm : "null");
                handled = true;
                return {0, 'L'};
            }
            handled = true;
            return {GetNamedCharset(canon), 'L'};
        }
        if (ref.name == "isSupported" && !arg_regs.empty()) {
            const char *nm = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
            handled = true;
            return {static_cast<uint64_t>(nm && !NormalizeCharsetName(nm).empty() ? 1 : 0), 'Z'};
        }
        return {0, 'V'};
    }

    if (arg_regs.empty()) {
        return {0, 'V'};
    }
    auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
    std::string cname = syn ? syn->string_fields["name"] : "UTF-8";

    if (ref.name == "name" || ref.name == "displayName" || ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(cname)), 'L'};
    }
    if (ref.name == "encode" && arg_regs.size() >= 2) {
        const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        std::vector<uint8_t> bytes = EncodeUtf8ToCharset(s ? s : "", cname);
        auto backing = std::make_shared<NioBufferBacking>();
        backing->data = std::move(bytes);
        auto payload = std::make_shared<NioBufferPayload>();
        payload->capacity = static_cast<int32_t>(backing->data.size());
        payload->limit = payload->capacity;
        payload->backing = std::move(backing);
        payload->kind = BufferKind::Byte;
        handled = true;
        return {reinterpret_cast<uintptr_t>(RegisterNioBuffer(std::move(payload))), 'L'};
    }
    if (ref.name == "decode" && arg_regs.size() >= 2) {
        auto buf = GetNioPayload(reinterpret_cast<jobject>(arg_regs[1]));
        std::string decoded;
        if (buf && buf->backing && buf->position < buf->limit) {
            size_t len = static_cast<size_t>(buf->limit - buf->position);
            decoded = DecodeCharsetToUtf8(
                std::span<const uint8_t>(buf->backing->data.data() + buf->byte_offset +
                                             static_cast<size_t>(buf->position),
                                         len),
                cname);
            buf->position = buf->limit;
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(decoded)), 'L'};
    }
    return {0, 'V'};
}

} // namespace

void RegisterNioIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/nio/Buffer", HandleByteBuffer);
    rt.RegisterClass("java/nio/ByteBuffer", HandleByteBuffer, {"java/nio/Buffer"});
    rt.RegisterClass("java/nio/DirectByteBuffer", HandleByteBuffer, {"java/nio/ByteBuffer"});
    rt.RegisterClass("java/nio/HeapByteBuffer", HandleByteBuffer, {"java/nio/ByteBuffer"});

    rt.RegisterClass("java/nio/ShortBuffer",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &ex,
                        bool &h) { return HandleTypedBuffer(BufferKind::Short, r, a, s, ex, h); },
                     {"java/nio/Buffer"});
    rt.RegisterClass("java/nio/IntBuffer",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &ex,
                        bool &h) { return HandleTypedBuffer(BufferKind::Int, r, a, s, ex, h); },
                     {"java/nio/Buffer"});
    rt.RegisterClass("java/nio/FloatBuffer",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &ex,
                        bool &h) { return HandleTypedBuffer(BufferKind::Float, r, a, s, ex, h); },
                     {"java/nio/Buffer"});
    rt.RegisterClass("java/nio/LongBuffer",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &ex,
                        bool &h) { return HandleTypedBuffer(BufferKind::Long, r, a, s, ex, h); },
                     {"java/nio/Buffer"});
    rt.RegisterClass("java/nio/DoubleBuffer",
                     [](DexVm &, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                        JNIEnv *, jobject &ex,
                        bool &h) { return HandleTypedBuffer(BufferKind::Double, r, a, s, ex, h); },
                     {"java/nio/Buffer"});

    rt.RegisterClass("java/nio/ByteOrder", HandleByteOrder);
    rt.RegisterStaticField("java/nio/ByteOrder", "BIG_ENDIAN", GetBigEndianOrder);
    rt.RegisterStaticField("java/nio/ByteOrder", "LITTLE_ENDIAN", GetLittleEndianOrder);

    rt.RegisterClass("java/nio/charset/Charset", HandleCharset);
    rt.RegisterClass("java/nio/charset/StandardCharsets", HandleCharset);
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "UTF_8",
                           []() { return GetNamedCharset("UTF-8"); });
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "UTF_16",
                           []() { return GetNamedCharset("UTF-16"); });
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "UTF_16BE",
                           []() { return GetNamedCharset("UTF-16BE"); });
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "UTF_16LE",
                           []() { return GetNamedCharset("UTF-16LE"); });
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "US_ASCII",
                           []() { return GetNamedCharset("US-ASCII"); });
    rt.RegisterStaticField("java/nio/charset/StandardCharsets", "ISO_8859_1",
                           []() { return GetNamedCharset("ISO-8859-1"); });
}

} // namespace portage::package
