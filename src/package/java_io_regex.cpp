#include "fs/vfs.hpp"
#include "ndk/android_log.hpp"
#include "package/dex_vm.hpp"
#include "package/java_runtime.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifdef RegisterClass
#undef RegisterClass
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;

namespace portage::package {

namespace {

enum class IoStreamKind {
    ByteArrayIn,
    ByteArrayOut,
    FileIn,
    FileOut,
    BufferedIn,
    BufferedOut,
    DataIn,
    DataOut,
    Reader,
    Writer,
    Print,
};

struct IoStreamPayload {
    IoStreamKind kind{IoStreamKind::ByteArrayIn};
    jobject inner{nullptr};
    std::vector<uint8_t> buffer{};
    size_t pos{0};
    size_t mark_pos{0};
    std::string text_buf{};
    std::string charset{"UTF-8"};
    std::ifstream ifs{};
    std::ofstream ofs{};
    bool closed{false};
    bool is_stdout{false};
    bool is_stderr{false};
    int32_t data_written{0};
    std::string line_accum{};
};

std::string NormalizeJavaPath(std::string_view raw)
{
    std::string p(raw);
    for (char &ch : p) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

stdfs::path ResolveJavaIoPath(std::string_view guest_or_host)
{
    std::string norm = NormalizeJavaPath(guest_or_host);
    if (norm.empty()) {
        return stdfs::current_path();
    }
    if (auto *vfs = syscall::SyscallDispatcher::GetVfs()) {
        auto res = vfs->Resolve(norm);
        if (!res.host_path.empty()) {
            return res.host_path;
        }
    }
    return stdfs::path(norm);
}

std::string ReadFileArgPath(jobject file_or_str)
{
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(file_or_str))) {
        return s;
    }
    if (auto syn = jni.LookupObject(file_or_str)) {
        if (auto it = syn->string_fields.find("path"); it != syn->string_fields.end()) {
            return it->second;
        }
    }
    return {};
}

jobject MakeFileObj(std::string_view path)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = "java/io/File";
    syn->string_fields["path"] = NormalizeJavaPath(path);
    return JniEnvironment::Instance().RegisterObject(syn);
}

jobject MakeIoPayloadObj(std::string_view cls, std::shared_ptr<IoStreamPayload> p)
{
    auto syn = std::make_shared<JniSyntheticObject>();
    syn->type = JniObjectType::Generic;
    syn->class_name = std::string(cls);
    syn->runtime_payload = std::move(p);
    return JniEnvironment::Instance().RegisterObject(syn);
}

std::shared_ptr<IoStreamPayload> GetIoPayload(jobject obj)
{
    auto syn = JniEnvironment::Instance().LookupObject(obj);
    if (!syn || !syn->runtime_payload) {
        return nullptr;
    }
    return std::static_pointer_cast<IoStreamPayload>(syn->runtime_payload);
}

int32_t ReadOneByteFromInput(DexVm &vm, jobject stream_obj, JNIEnv *env, jobject &out_exception)
{
    if (auto p = GetIoPayload(stream_obj)) {
        if (p->closed) {
            out_exception = JavaRuntime::CreateException("java/io/IOException", "Stream closed");
            return -1;
        }
        if (p->kind == IoStreamKind::ByteArrayIn) {
            if (p->pos >= p->buffer.size()) {
                return -1;
            }
            return p->buffer[p->pos++];
        }
        if (p->kind == IoStreamKind::FileIn) {
            int ch = p->ifs.get();
            return (ch == std::char_traits<char>::eof()) ? -1 : (ch & 0xFF);
        }
        if (p->inner) {
            return ReadOneByteFromInput(vm, p->inner, env, out_exception);
        }
    }
    auto syn = JniEnvironment::Instance().LookupObject(stream_obj);
    if (syn && syn->class_name == "java/io/ByteArrayInputStream") {
        int32_t pos = syn->int_fields["pos"];
        int32_t cnt = syn->int_fields["count"];
        if (pos < 0 || pos >= cnt || static_cast<size_t>(pos) >= syn->byte_data.size()) {
            return -1;
        }
        uint8_t b = static_cast<uint8_t>(syn->byte_data[static_cast<size_t>(pos)]);
        syn->int_fields["pos"] = pos + 1;
        return b;
    }
    bool found = false;
    DexValue v =
        JavaRuntime::CallVirtual(vm, stream_obj, "read", "()I", {}, env, out_exception, &found);
    if (found && !out_exception) {
        return static_cast<int32_t>(v.raw);
    }
    return -1;
}

int32_t ReadBytesFromInput(DexVm &vm, jobject stream_obj, uint8_t *dst, size_t max_len, JNIEnv *env,
                           jobject &out_exception)
{
    if (max_len == 0) {
        return 0;
    }
    if (auto p = GetIoPayload(stream_obj)) {
        if (p->closed) {
            out_exception = JavaRuntime::CreateException("java/io/IOException", "Stream closed");
            return -1;
        }
        if (p->kind == IoStreamKind::ByteArrayIn) {
            if (p->pos >= p->buffer.size()) {
                return -1;
            }
            size_t avail = std::min(max_len, p->buffer.size() - p->pos);
            std::memcpy(dst, p->buffer.data() + p->pos, avail);
            p->pos += avail;
            return static_cast<int32_t>(avail);
        }
        if (p->kind == IoStreamKind::FileIn) {
            if (!p->ifs.good() && p->ifs.eof()) {
                return -1;
            }
            p->ifs.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(max_len));
            std::streamsize got = p->ifs.gcount();
            return got > 0 ? static_cast<int32_t>(got) : -1;
        }
        if (p->inner) {
            return ReadBytesFromInput(vm, p->inner, dst, max_len, env, out_exception);
        }
    }
    auto syn = JniEnvironment::Instance().LookupObject(stream_obj);
    if (syn && syn->class_name == "java/io/ByteArrayInputStream") {
        int32_t pos = syn->int_fields["pos"];
        int32_t cnt = syn->int_fields["count"];
        if (pos < 0 || pos >= cnt || static_cast<size_t>(pos) >= syn->byte_data.size()) {
            return -1;
        }
        size_t avail = std::min(max_len, static_cast<size_t>(cnt - pos));
        std::memcpy(dst, syn->byte_data.data() + pos, avail);
        syn->int_fields["pos"] = pos + static_cast<int32_t>(avail);
        return static_cast<int32_t>(avail);
    }
    int b0 = ReadOneByteFromInput(vm, stream_obj, env, out_exception);
    if (b0 < 0 || out_exception) {
        return -1;
    }
    dst[0] = static_cast<uint8_t>(b0);
    return 1;
}

void WriteBytesToOutput(DexVm &vm, jobject stream_obj, const uint8_t *src, size_t len, JNIEnv *env,
                        jobject &out_exception)
{
    if (auto p = GetIoPayload(stream_obj)) {
        if (p->closed) {
            out_exception = JavaRuntime::CreateException("java/io/IOException", "Stream closed");
            return;
        }
        if (p->kind == IoStreamKind::ByteArrayOut) {
            p->buffer.insert(p->buffer.end(), src, src + len);
            return;
        }
        if (p->kind == IoStreamKind::FileOut) {
            p->ofs.write(reinterpret_cast<const char *>(src), static_cast<std::streamsize>(len));
            if (!p->ofs.good()) {
                out_exception = JavaRuntime::CreateException("java/io/IOException", "Write failed");
            }
            return;
        }
        if (p->is_stdout || p->is_stderr) {
            std::string_view sv(reinterpret_cast<const char *>(src), len);
            if (p->is_stderr) {
                std::cerr.write(sv.data(), static_cast<std::streamsize>(sv.size()));
            } else {
                std::cout.write(sv.data(), static_cast<std::streamsize>(sv.size()));
            }
            p->line_accum.append(sv);
            size_t nl = 0;
            while ((nl = p->line_accum.find('\n')) != std::string::npos) {
                std::string line = p->line_accum.substr(0, nl);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                ndk::LogcatBuffer::Instance().Append(
                    p->is_stderr ? 6 : 4, p->is_stderr ? "System.err" : "System.out", line);
                p->line_accum.erase(0, nl + 1);
            }
            return;
        }
        if (p->inner) {
            WriteBytesToOutput(vm, p->inner, src, len, env, out_exception);
            return;
        }
    }
    auto arr = std::make_shared<JniSyntheticObject>();
    arr->type = JniObjectType::ByteArray;
    arr->class_name = "[B";
    arr->byte_data.assign(reinterpret_cast<const int8_t *>(src),
                          reinterpret_cast<const int8_t *>(src + len));
    uint64_t a = reinterpret_cast<uintptr_t>(JniEnvironment::Instance().RegisterObject(arr));
    JavaRuntime::CallVirtual(vm, stream_obj, "write", "([B)V", std::span<const uint64_t>(&a, 1),
                             env, out_exception);
}

DexValue HandleFile(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                    bool is_static, JNIEnv *, jobject &, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);

    if (ref.name == "<init>") {
        std::string path;
        if (arg_regs.size() == 2) {
            path = ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1]));
        } else if (arg_regs.size() >= 3) {
            std::string parent = ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1]));
            std::string child = ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[2]));
            if (parent.empty()) {
                path = child;
            } else if (child.empty()) {
                path = parent;
            } else if (parent.back() == '/') {
                path = parent + child;
            } else {
                path = parent + "/" + child;
            }
        }
        syn->string_fields["path"] = NormalizeJavaPath(path);
        handled = true;
        return {0, 'V'};
    }

    std::string path = syn->string_fields["path"];
    stdfs::path host = ResolveJavaIoPath(path);
    std::error_code ec;

    if (ref.name == "getPath" || ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(path)), 'L'};
    }
    if (ref.name == "getAbsolutePath" || ref.name == "getCanonicalPath") {
        std::string out = (!path.empty() && path.front() == '/')
                              ? path
                              : NormalizeJavaPath(stdfs::absolute(host, ec).string());
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(out)), 'L'};
    }
    if (ref.name == "getName") {
        auto pos = path.find_last_of('/');
        std::string nm = (pos == std::string::npos) ? path : path.substr(pos + 1);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(nm)), 'L'};
    }
    if (ref.name == "getParent" || ref.name == "getParentFile") {
        auto pos = path.find_last_of('/');
        if (pos == std::string::npos || pos == 0) {
            handled = true;
            return {0, 'L'};
        }
        std::string parent = path.substr(0, pos);
        handled = true;
        return {reinterpret_cast<uintptr_t>(ref.name == "getParent"
                                                ? static_cast<jobject>(jni.CreateString(parent))
                                                : MakeFileObj(parent)),
                'L'};
    }
    if (ref.name == "exists") {
        handled = true;
        return {static_cast<uint64_t>(stdfs::exists(host, ec) ? 1 : 0), 'Z'};
    }
    if (ref.name == "isFile") {
        handled = true;
        return {static_cast<uint64_t>(stdfs::is_regular_file(host, ec) ? 1 : 0), 'Z'};
    }
    if (ref.name == "isDirectory") {
        handled = true;
        return {static_cast<uint64_t>(stdfs::is_directory(host, ec) ? 1 : 0), 'Z'};
    }
    if (ref.name == "length") {
        uint64_t sz = stdfs::is_regular_file(host, ec) ? stdfs::file_size(host, ec) : 0;
        handled = true;
        return {sz, 'J'};
    }
    if (ref.name == "lastModified") {
        auto ftime = stdfs::last_write_time(host, ec);
        int64_t ms =
            ec ? 0
               : std::chrono::duration_cast<std::chrono::milliseconds>(ftime.time_since_epoch())
                     .count();
        handled = true;
        return {static_cast<uint64_t>(ms), 'J'};
    }
    if (ref.name == "mkdir") {
        handled = true;
        return {static_cast<uint64_t>(stdfs::create_directory(host, ec) ? 1 : 0), 'Z'};
    }
    if (ref.name == "mkdirs") {
        bool ok = stdfs::is_directory(host, ec) || stdfs::create_directories(host, ec);
        handled = true;
        return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
    }
    if (ref.name == "createNewFile") {
        if (stdfs::exists(host, ec)) {
            handled = true;
            return {0, 'Z'};
        }
        if (host.has_parent_path()) {
            stdfs::create_directories(host.parent_path(), ec);
        }
        std::ofstream ofs(host, std::ios::binary | std::ios::trunc);
        handled = true;
        return {static_cast<uint64_t>(ofs.is_open() ? 1 : 0), 'Z'};
    }
    if (ref.name == "delete") {
        handled = true;
        return {static_cast<uint64_t>(stdfs::remove(host, ec) ? 1 : 0), 'Z'};
    }
    if (ref.name == "renameTo" && arg_regs.size() >= 2) {
        stdfs::path dst =
            ResolveJavaIoPath(ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1])));
        stdfs::rename(host, dst, ec);
        handled = true;
        return {static_cast<uint64_t>(!ec ? 1 : 0), 'Z'};
    }
    if (ref.name == "list" || ref.name == "listFiles") {
        if (!stdfs::is_directory(host, ec)) {
            handled = true;
            return {0, 'L'};
        }
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ObjectArray;
        arr->class_name = (ref.name == "list") ? "java/lang/String" : "java/io/File";
        for (const auto &entry : stdfs::directory_iterator(host, ec)) {
            std::string nm = entry.path().filename().string();
            if (ref.name == "list") {
                arr->object_data.push_back(jni.CreateString(nm));
            } else {
                arr->object_data.push_back(MakeFileObj(path.empty() ? nm : (path + "/" + nm)));
            }
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    if (ref.name == "equals" && arg_regs.size() >= 2) {
        std::string other = ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1]));
        handled = true;
        return {static_cast<uint64_t>(path == NormalizeJavaPath(other) ? 1 : 0), 'Z'};
    }
    if (ref.name == "hashCode") {
        handled = true;
        return {static_cast<uint64_t>(static_cast<uint32_t>(java_rt::JavaStringHashCode(path))),
                'I'};
    }
    return {0, 'V'};
}

DexValue HandleByteStreams(DexVm &vm, std::string_view cls, const DexMethodRef &ref,
                           std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                           jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);

    if (ref.name == "<init>") {
        auto p = std::make_shared<IoStreamPayload>();
        if (cls == "java/io/ByteArrayInputStream") {
            p->kind = IoStreamKind::ByteArrayIn;
            if (arg_regs.size() >= 2) {
                auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
                if (arr && arr->type == JniObjectType::ByteArray) {
                    size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
                    size_t len = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3])
                                                        : arr->byte_data.size();
                    if (off <= arr->byte_data.size()) {
                        len = std::min(len, arr->byte_data.size() - off);
                        p->buffer.assign(
                            reinterpret_cast<const uint8_t *>(arr->byte_data.data() + off),
                            reinterpret_cast<const uint8_t *>(arr->byte_data.data() + off + len));
                    }
                }
            }
        } else if (cls == "java/io/ByteArrayOutputStream") {
            p->kind = IoStreamKind::ByteArrayOut;
        } else if (cls == "java/io/FileInputStream") {
            p->kind = IoStreamKind::FileIn;
            std::string path =
                arg_regs.size() >= 2 ? ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1])) : "";
            stdfs::path host = ResolveJavaIoPath(path);
            p->ifs.open(host, std::ios::binary);
            if (!p->ifs.is_open()) {
                out_exception = JavaRuntime::CreateException("java/io/FileNotFoundException", path);
            }
        } else if (cls == "java/io/FileOutputStream") {
            p->kind = IoStreamKind::FileOut;
            std::string path =
                arg_regs.size() >= 2 ? ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1])) : "";
            bool append = (arg_regs.size() >= 3 && arg_regs[2] != 0);
            stdfs::path host = ResolveJavaIoPath(path);
            std::error_code ec;
            if (host.has_parent_path()) {
                stdfs::create_directories(host.parent_path(), ec);
            }
            p->ofs.open(host, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!p->ofs.is_open()) {
                out_exception = JavaRuntime::CreateException("java/io/FileNotFoundException", path);
            }
        } else if (cls == "java/io/BufferedInputStream") {
            p->kind = IoStreamKind::BufferedIn;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        } else if (cls == "java/io/BufferedOutputStream") {
            p->kind = IoStreamKind::BufferedOut;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        } else if (cls == "java/io/DataInputStream") {
            p->kind = IoStreamKind::DataIn;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        } else if (cls == "java/io/DataOutputStream") {
            p->kind = IoStreamKind::DataOut;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        }
        syn->runtime_payload = std::move(p);
        handled = true;
        return {0, 'V'};
    }

    if (!syn->runtime_payload && syn->class_name == "java/io/ByteArrayInputStream") {
        auto p = std::make_shared<IoStreamPayload>();
        p->kind = IoStreamKind::ByteArrayIn;
        p->buffer.assign(
            reinterpret_cast<const uint8_t *>(syn->byte_data.data()),
            reinterpret_cast<const uint8_t *>(syn->byte_data.data() + syn->byte_data.size()));
        p->pos = static_cast<size_t>(std::max(0, syn->int_fields["pos"]));
        syn->runtime_payload = std::move(p);
    }

    auto p = GetIoPayload(self);
    if (!p) {
        return {0, 'V'};
    }

    if (ref.name == "close") {
        p->closed = true;
        if (p->ifs.is_open()) {
            p->ifs.close();
        }
        if (p->ofs.is_open()) {
            p->ofs.flush();
            p->ofs.close();
        }
        if (p->inner) {
            JavaRuntime::CallVirtual(vm, p->inner, "close", "()V", {}, env, out_exception);
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "flush") {
        if (p->ofs.is_open()) {
            p->ofs.flush();
        }
        if (p->inner) {
            JavaRuntime::CallVirtual(vm, p->inner, "flush", "()V", {}, env, out_exception);
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "available") {
        int32_t avail = 0;
        if (p->kind == IoStreamKind::ByteArrayIn) {
            avail = static_cast<int32_t>(p->buffer.size() > p->pos ? p->buffer.size() - p->pos : 0);
        }
        handled = true;
        return {static_cast<uint64_t>(avail), 'I'};
    }
    if (ref.name == "skip" && arg_regs.size() >= 2) {
        int64_t n = static_cast<int64_t>(arg_regs[1]);
        if (arg_regs.size() >= 3 && !ref.param_types.empty() && ref.param_types[0] == "J") {
            n = static_cast<int64_t>((arg_regs[1] & 0xFFFFFFFFULL) |
                                     ((arg_regs[2] & 0xFFFFFFFFULL) << 32));
        }
        int64_t skipped = 0;
        if (n > 0 && p->kind == IoStreamKind::ByteArrayIn) {
            size_t avail = p->buffer.size() > p->pos ? p->buffer.size() - p->pos : 0;
            size_t adv = std::min(avail, static_cast<size_t>(n));
            p->pos += adv;
            skipped = static_cast<int64_t>(adv);
        }
        handled = true;
        return {static_cast<uint64_t>(skipped), 'J'};
    }
    if (ref.name == "markSupported") {
        handled = true;
        return {static_cast<uint64_t>(p->kind == IoStreamKind::ByteArrayIn ? 1 : 0), 'Z'};
    }
    if (ref.name == "mark") {
        p->mark_pos = p->pos;
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "reset") {
        if (p->kind == IoStreamKind::ByteArrayOut) {
            p->buffer.clear();
        } else {
            p->pos = p->mark_pos;
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "read") {
        handled = true;
        if (ref.param_types.empty()) {
            int32_t b = ReadOneByteFromInput(vm, self, env, out_exception);
            return {static_cast<uint64_t>(static_cast<uint32_t>(b)), 'I'};
        }
        if (arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            if (!arr || arr->type != JniObjectType::ByteArray) {
                return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
            }
            size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
            size_t len =
                (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3]) : arr->byte_data.size();
            if (off > arr->byte_data.size() || len > arr->byte_data.size() - off) {
                out_exception =
                    JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "");
                return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
            }
            int32_t n = ReadBytesFromInput(vm, self,
                                           reinterpret_cast<uint8_t *>(arr->byte_data.data() + off),
                                           len, env, out_exception);
            return {static_cast<uint64_t>(static_cast<uint32_t>(n)), 'I'};
        }
    }
    if (ref.name == "readAllBytes") {
        std::vector<uint8_t> all;
        std::array<uint8_t, 4096> chunk{};
        while (!out_exception) {
            int32_t n =
                ReadBytesFromInput(vm, self, chunk.data(), chunk.size(), env, out_exception);
            if (n <= 0) {
                break;
            }
            all.insert(all.end(), chunk.data(), chunk.data() + n);
        }
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ByteArray;
        arr->class_name = "[B";
        arr->byte_data.assign(reinterpret_cast<const int8_t *>(all.data()),
                              reinterpret_cast<const int8_t *>(all.data() + all.size()));
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    if (ref.name == "write") {
        handled = true;
        if (!ref.param_types.empty() && ref.param_types[0] == "I" && arg_regs.size() >= 2) {
            uint8_t b = static_cast<uint8_t>(arg_regs[1] & 0xFFu);
            WriteBytesToOutput(vm, self, &b, 1, env, out_exception);
            p->data_written += 1;
            return {0, 'V'};
        }
        if (arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            if (arr && arr->type == JniObjectType::ByteArray) {
                size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
                size_t len = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3])
                                                    : arr->byte_data.size();
                if (off <= arr->byte_data.size() && len <= arr->byte_data.size() - off) {
                    WriteBytesToOutput(
                        vm, self, reinterpret_cast<const uint8_t *>(arr->byte_data.data() + off),
                        len, env, out_exception);
                    p->data_written += static_cast<int32_t>(len);
                }
            }
            return {0, 'V'};
        }
    }
    if (ref.name == "size") {
        handled = true;
        if (p->kind == IoStreamKind::ByteArrayOut) {
            return {static_cast<uint64_t>(p->buffer.size()), 'I'};
        }
        return {static_cast<uint64_t>(p->data_written), 'I'};
    }
    if (ref.name == "toByteArray") {
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ByteArray;
        arr->class_name = "[B";
        arr->byte_data.assign(
            reinterpret_cast<const int8_t *>(p->buffer.data()),
            reinterpret_cast<const int8_t *>(p->buffer.data() + p->buffer.size()));
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    if (ref.name == "toString" && p->kind == IoStreamKind::ByteArrayOut) {
        std::string cs = "UTF-8";
        if (arg_regs.size() >= 2) {
            jobject cs_arg = reinterpret_cast<jobject>(arg_regs[1]);
            if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(cs_arg))) {
                cs = java_rt::NormalizeCharsetName(s);
            } else if (auto csyn = jni.LookupObject(cs_arg)) {
                cs = csyn->string_fields["name"];
            }
        }
        std::string text = java_rt::DecodeBytesWithCharset(p->buffer, cs);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(text)), 'L'};
    }
    if (ref.name == "writeTo" && arg_regs.size() >= 2 && p->kind == IoStreamKind::ByteArrayOut) {
        WriteBytesToOutput(vm, reinterpret_cast<jobject>(arg_regs[1]), p->buffer.data(),
                           p->buffer.size(), env, out_exception);
        handled = true;
        return {0, 'V'};
    }

    auto read_be = [&](size_t n) -> uint64_t {
        uint64_t val = 0;
        for (size_t i = 0; i < n; ++i) {
            int32_t b = ReadOneByteFromInput(vm, p->inner ? p->inner : self, env, out_exception);
            if (b < 0 || out_exception) {
                if (!out_exception) {
                    out_exception = JavaRuntime::CreateException("java/io/EOFException", "");
                }
                return 0;
            }
            val = (val << 8u) | static_cast<uint64_t>(b & 0xFF);
        }
        return val;
    };
    auto write_be = [&](uint64_t val, size_t n) {
        uint8_t buf[8]{};
        for (size_t i = 0; i < n; ++i) {
            buf[n - 1 - i] = static_cast<uint8_t>((val >> (i * 8u)) & 0xFFu);
        }
        WriteBytesToOutput(vm, p->inner ? p->inner : self, buf, n, env, out_exception);
        p->data_written += static_cast<int32_t>(n);
    };

    if (ref.name == "readBoolean" || ref.name == "readByte" || ref.name == "readUnsignedByte") {
        handled = true;
        uint64_t v = read_be(1);
        if (ref.name == "readByte") {
            return {static_cast<uint64_t>(static_cast<int32_t>(static_cast<int8_t>(v))), 'B'};
        }
        return {v, ref.name == "readBoolean" ? 'Z' : 'I'};
    }
    if (ref.name == "readShort" || ref.name == "readUnsignedShort") {
        handled = true;
        uint64_t v = read_be(2);
        if (ref.name == "readShort") {
            return {static_cast<uint64_t>(static_cast<int32_t>(static_cast<int16_t>(v))), 'S'};
        }
        return {v, 'I'};
    }
    if (ref.name == "readInt" || ref.name == "readFloat") {
        handled = true;
        uint64_t v = read_be(4);
        return {v, ref.name == "readFloat" ? 'F' : 'I'};
    }
    if (ref.name == "readLong" || ref.name == "readDouble") {
        handled = true;
        uint64_t v = read_be(8);
        return {v, ref.name == "readDouble" ? 'D' : 'J'};
    }
    if (ref.name == "readUTF") {
        handled = true;
        uint16_t len = static_cast<uint16_t>(read_be(2));
        std::string s(len, '\0');
        for (uint16_t i = 0; i < len && !out_exception; ++i) {
            int32_t b = ReadOneByteFromInput(vm, p->inner ? p->inner : self, env, out_exception);
            if (b < 0) {
                break;
            }
            s[i] = static_cast<char>(b);
        }
        return {reinterpret_cast<uintptr_t>(jni.CreateString(s)), 'L'};
    }
    if (ref.name == "readFully" && arg_regs.size() >= 2) {
        handled = true;
        auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        if (arr && arr->type == JniObjectType::ByteArray) {
            size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
            size_t len =
                (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3]) : arr->byte_data.size();
            for (size_t i = 0; i < len && off + i < arr->byte_data.size() && !out_exception; ++i) {
                arr->byte_data[off + i] = static_cast<int8_t>(read_be(1));
            }
        }
        return {0, 'V'};
    }
    if ((ref.name == "writeBoolean" || ref.name == "writeByte") && arg_regs.size() >= 2) {
        handled = true;
        write_be(arg_regs[1], 1);
        return {0, 'V'};
    }
    if (ref.name == "writeShort" && arg_regs.size() >= 2) {
        handled = true;
        write_be(arg_regs[1], 2);
        return {0, 'V'};
    }
    if ((ref.name == "writeInt" || ref.name == "writeFloat") && arg_regs.size() >= 2) {
        handled = true;
        write_be(arg_regs[1], 4);
        return {0, 'V'};
    }
    if ((ref.name == "writeLong" || ref.name == "writeDouble") && arg_regs.size() >= 3) {
        handled = true;
        uint64_t wide = (arg_regs[1] & 0xFFFFFFFFULL) | ((arg_regs[2] & 0xFFFFFFFFULL) << 32);
        write_be(wide, 8);
        return {0, 'V'};
    }
    if (ref.name == "writeUTF" && arg_regs.size() >= 2) {
        handled = true;
        const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        std::string_view sv = s ? s : "";
        write_be(static_cast<uint64_t>(sv.size()), 2);
        WriteBytesToOutput(vm, p->inner ? p->inner : self,
                           reinterpret_cast<const uint8_t *>(sv.data()), sv.size(), env,
                           out_exception);
        p->data_written += static_cast<int32_t>(sv.size());
        return {0, 'V'};
    }
    return {0, 'V'};
}

DexValue HandleReadersWritersPrint(DexVm &vm, std::string_view cls, const DexMethodRef &ref,
                                   std::span<const uint64_t> arg_regs, bool is_static, JNIEnv *env,
                                   jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn) {
        return {0, 'V'};
    }
    std::lock_guard<std::recursive_mutex> lk(syn->field_mutex);

    if (ref.name == "<init>") {
        auto p = std::make_shared<IoStreamPayload>();
        if (cls == "java/io/StringReader") {
            p->kind = IoStreamKind::Reader;
            if (arg_regs.size() >= 2) {
                if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]))) {
                    p->text_buf = s;
                }
            }
        } else if (cls == "java/io/StringWriter") {
            p->kind = IoStreamKind::Writer;
        } else if (cls == "java/io/FileReader") {
            p->kind = IoStreamKind::Reader;
            std::string path =
                arg_regs.size() >= 2 ? ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1])) : "";
            std::ifstream ifs(ResolveJavaIoPath(path), std::ios::binary);
            if (!ifs.is_open()) {
                out_exception = JavaRuntime::CreateException("java/io/FileNotFoundException", path);
            } else {
                std::ostringstream oss;
                oss << ifs.rdbuf();
                p->text_buf = oss.str();
            }
        } else if (cls == "java/io/FileWriter") {
            p->kind = IoStreamKind::FileOut;
            std::string path =
                arg_regs.size() >= 2 ? ReadFileArgPath(reinterpret_cast<jobject>(arg_regs[1])) : "";
            bool append = (arg_regs.size() >= 3 && arg_regs[2] != 0);
            stdfs::path host = ResolveJavaIoPath(path);
            std::error_code ec;
            if (host.has_parent_path()) {
                stdfs::create_directories(host.parent_path(), ec);
            }
            p->ofs.open(host, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!p->ofs.is_open()) {
                out_exception = JavaRuntime::CreateException("java/io/FileNotFoundException", path);
            }
        } else if (cls == "java/io/InputStreamReader") {
            p->kind = IoStreamKind::Reader;
            jobject inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
            std::string cs = "UTF-8";
            if (arg_regs.size() >= 3) {
                if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]))) {
                    cs = java_rt::NormalizeCharsetName(s);
                } else if (auto csyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[2]))) {
                    cs = csyn->string_fields["name"];
                }
            }
            std::vector<uint8_t> raw;
            std::array<uint8_t, 4096> chunk{};
            while (inner && !out_exception) {
                int32_t n =
                    ReadBytesFromInput(vm, inner, chunk.data(), chunk.size(), env, out_exception);
                if (n <= 0) {
                    break;
                }
                raw.insert(raw.end(), chunk.data(), chunk.data() + n);
            }
            p->text_buf = java_rt::DecodeBytesWithCharset(raw, cs);
            p->inner = inner;
        } else if (cls == "java/io/OutputStreamWriter") {
            p->kind = IoStreamKind::Writer;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
            if (arg_regs.size() >= 3) {
                if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]))) {
                    p->charset = java_rt::NormalizeCharsetName(s);
                } else if (auto csyn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[2]))) {
                    p->charset = csyn->string_fields["name"];
                }
            }
        } else if (cls == "java/io/BufferedReader") {
            p->kind = IoStreamKind::Reader;
            jobject inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
            if (auto ip = GetIoPayload(inner)) {
                p->text_buf = ip->text_buf;
                p->pos = ip->pos;
            }
            p->inner = inner;
        } else if (cls == "java/io/BufferedWriter") {
            p->kind = IoStreamKind::Writer;
            p->inner = arg_regs.size() >= 2 ? reinterpret_cast<jobject>(arg_regs[1]) : nullptr;
        } else if (cls == "java/io/PrintStream" || cls == "java/io/PrintWriter") {
            p->kind = IoStreamKind::Print;
            if (arg_regs.size() >= 2) {
                jobject target = reinterpret_cast<jobject>(arg_regs[1]);
                if (jni.GetStringChars(reinterpret_cast<jstring>(target)) != nullptr ||
                    (jni.LookupObject(target) &&
                     jni.LookupObject(target)->class_name == "java/io/File")) {
                    std::string path = ReadFileArgPath(target);
                    stdfs::path host = ResolveJavaIoPath(path);
                    std::error_code ec;
                    if (host.has_parent_path()) {
                        stdfs::create_directories(host.parent_path(), ec);
                    }
                    p->kind = IoStreamKind::FileOut;
                    p->ofs.open(host, std::ios::binary | std::ios::trunc);
                } else {
                    p->inner = target;
                }
            }
        }
        syn->runtime_payload = std::move(p);
        handled = true;
        return {0, 'V'};
    }

    auto p = GetIoPayload(self);
    if (!p) {
        return {0, 'V'};
    }

    if (ref.name == "readLine") {
        handled = true;
        if (p->pos >= p->text_buf.size()) {
            return {0, 'L'};
        }
        size_t nl = p->text_buf.find('\n', p->pos);
        std::string line;
        if (nl == std::string::npos) {
            line = p->text_buf.substr(p->pos);
            p->pos = p->text_buf.size();
        } else {
            line = p->text_buf.substr(p->pos, nl - p->pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            p->pos = nl + 1;
        }
        return {reinterpret_cast<uintptr_t>(jni.CreateString(line)), 'L'};
    }
    if (ref.name == "read") {
        handled = true;
        if (ref.param_types.empty()) {
            if (p->pos >= p->text_buf.size()) {
                return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
            }
            return {static_cast<uint64_t>(static_cast<uint8_t>(p->text_buf[p->pos++])), 'I'};
        }
        if (arg_regs.size() >= 2) {
            auto arr = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
            if (!arr || arr->type != JniObjectType::CharArray || p->pos >= p->text_buf.size()) {
                return {static_cast<uint64_t>(static_cast<uint32_t>(-1)), 'I'};
            }
            size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
            size_t len =
                (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3]) : arr->char_data.size();
            size_t avail = std::min(len, p->text_buf.size() - p->pos);
            for (size_t i = 0; i < avail && off + i < arr->char_data.size(); ++i) {
                arr->char_data[off + i] =
                    static_cast<uint16_t>(static_cast<uint8_t>(p->text_buf[p->pos++]));
            }
            return {static_cast<uint64_t>(avail), 'I'};
        }
    }
    if (ref.name == "ready") {
        handled = true;
        return {static_cast<uint64_t>(p->pos < p->text_buf.size() ? 1 : 0), 'Z'};
    }
    auto emit_text = [&](std::string_view text) {
        if (p->kind == IoStreamKind::FileOut) {
            p->ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
            return;
        }
        if (p->inner) {
            if (auto ip = GetIoPayload(p->inner);
                ip && (ip->kind == IoStreamKind::Writer || ip->kind == IoStreamKind::FileOut)) {
                if (ip->kind == IoStreamKind::FileOut) {
                    ip->ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
                } else {
                    ip->text_buf.append(text);
                }
                return;
            }
            std::vector<uint8_t> bytes = java_rt::EncodeStringWithCharset(text, p->charset);
            WriteBytesToOutput(vm, p->inner, bytes.data(), bytes.size(), env, out_exception);
            return;
        }
        if (p->is_stdout || p->is_stderr) {
            WriteBytesToOutput(vm, self, reinterpret_cast<const uint8_t *>(text.data()),
                               text.size(), env, out_exception);
            return;
        }
        p->text_buf.append(text);
    };

    if (ref.name == "write" && arg_regs.size() >= 2) {
        handled = true;
        if (!ref.param_types.empty() && ref.param_types[0] == "Ljava/lang/String;") {
            const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            std::string_view sv = s ? s : "";
            size_t off = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[2]) : 0;
            size_t len = (arg_regs.size() >= 4) ? static_cast<size_t>(arg_regs[3]) : sv.size();
            if (off <= sv.size()) {
                emit_text(sv.substr(off, std::min(len, sv.size() - off)));
            }
        } else if (!ref.param_types.empty() && ref.param_types[0] == "I") {
            char ch = static_cast<char>(arg_regs[1] & 0xFFu);
            emit_text(std::string_view(&ch, 1));
        }
        return {0, 'V'};
    }
    if (ref.name == "newLine") {
        handled = true;
        emit_text("\n");
        return {0, 'V'};
    }
    if (ref.name == "print" || ref.name == "println") {
        handled = true;
        std::string s;
        if (arg_regs.size() >= 2 && !ref.param_types.empty()) {
            const std::string &pt = ref.param_types[0];
            if (pt == "I") {
                s = std::to_string(static_cast<int32_t>(arg_regs[1]));
            } else if (pt == "J") {
                int64_t v = static_cast<int64_t>(
                    (arg_regs[1] & 0xFFFFFFFFULL) |
                    ((arg_regs.size() >= 3 ? arg_regs[2] : 0) & 0xFFFFFFFFULL) << 32);
                s = std::to_string(v);
            } else if (pt == "Z") {
                s = (arg_regs[1] != 0) ? "true" : "false";
            } else if (pt == "C") {
                char16_t c16 = static_cast<char16_t>(arg_regs[1] & 0xFFFFu);
                s = java_rt::Utf16ToUtf8(std::u16string_view(&c16, 1));
            } else if (pt == "F") {
                s = java_rt::FormatFloatJava(java_rt::BitsToFloat(arg_regs[1]));
            } else if (pt == "D") {
                uint64_t wide = (arg_regs[1] & 0xFFFFFFFFULL) |
                                ((arg_regs.size() >= 3 ? arg_regs[2] : 0) & 0xFFFFFFFFULL) << 32;
                s = java_rt::FormatDoubleJava(java_rt::BitsToDouble(wide));
            } else {
                s = JavaRuntime::ObjectToString(vm, reinterpret_cast<jobject>(arg_regs[1]), env,
                                                out_exception);
            }
        }
        if (ref.name == "println") {
            s.push_back('\n');
        }
        emit_text(s);
        return {0, 'V'};
    }
    if (ref.name == "toString" && cls == "java/io/StringWriter") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(p->text_buf)), 'L'};
    }
    if (ref.name == "flush") {
        if (p->ofs.is_open()) {
            p->ofs.flush();
        }
        if (p->inner) {
            JavaRuntime::CallVirtual(vm, p->inner, "flush", "()V", {}, env, out_exception);
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "close") {
        p->closed = true;
        if (p->ofs.is_open()) {
            p->ofs.flush();
            p->ofs.close();
        }
        if (p->inner) {
            JavaRuntime::CallVirtual(vm, p->inner, "close", "()V", {}, env, out_exception);
        }
        handled = true;
        return {0, 'V'};
    }
    if (ref.name == "checkError") {
        handled = true;
        return {0, 'Z'};
    }
    return {0, 'V'};
}

uint64_t GetSystemPrintStream(bool is_err)
{
    static uint64_t epoch = 0;
    static jobject out_obj = nullptr;
    static jobject err_obj = nullptr;
    uint64_t cur = JniEnvironment::Instance().GetResetEpoch();
    if (!out_obj || !err_obj || epoch != cur) {
        epoch = cur;
        auto po = std::make_shared<IoStreamPayload>();
        po->kind = IoStreamKind::Print;
        po->is_stdout = true;
        out_obj = MakeIoPayloadObj("java/io/PrintStream", std::move(po));

        auto pe = std::make_shared<IoStreamPayload>();
        pe->kind = IoStreamKind::Print;
        pe->is_stderr = true;
        err_obj = MakeIoPayloadObj("java/io/PrintStream", std::move(pe));
    }
    return reinterpret_cast<uintptr_t>(is_err ? err_obj : out_obj);
}

std::string PreprocessJavaRegex(std::string_view pat, int32_t flags, int32_t &out_effective_flags)
{
    out_effective_flags = flags;
    std::string raw(pat);
    if (raw.rfind("(?i)", 0) == 0) {
        out_effective_flags |= 0x02;
        raw.erase(0, 4);
    }
    if (flags & 0x10) {
        std::string escaped;
        for (char c : raw) {
            if (std::string_view("\\.^$|()[]{}*+?").find(c) != std::string_view::npos) {
                escaped.push_back('\\');
            }
            escaped.push_back(c);
        }
        return escaped;
    }
    std::string out;
    bool in_class = false;
    bool dotall = (out_effective_flags & 0x20) != 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char next = raw[i + 1];
            if (next == 'A') {
                out.push_back('^');
                ++i;
                continue;
            }
            if (next == 'z' || next == 'Z') {
                out.push_back('$');
                ++i;
                continue;
            }
            out.push_back(c);
            out.push_back(next);
            ++i;
            continue;
        }
        if (c == '[' && !in_class) {
            in_class = true;
        } else if (c == ']' && in_class) {
            in_class = false;
        } else if (c == '.' && !in_class && dotall) {
            out += "[\\s\\S]";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool CompileJavaRegex(std::string_view pat, int32_t flags, std::regex &out_re,
                      jobject &out_exception)
{
    int32_t eff = flags;
    std::string pre = PreprocessJavaRegex(pat, flags, eff);
    std::regex_constants::syntax_option_type opts = std::regex_constants::ECMAScript;
    if (eff & 0x02) {
        opts |= std::regex_constants::icase;
    }
    try {
        out_re = std::regex(pre, opts);
        return true;
    } catch (const std::regex_error &e) {
        out_exception =
            JavaRuntime::CreateException("java/util/regex/PatternSyntaxException", e.what());
        return false;
    }
}

struct PatternPayload {
    std::string raw_pattern{};
    int32_t flags{0};
    std::regex compiled{};
};

struct MatcherPayload {
    std::shared_ptr<PatternPayload> pattern;
    std::string input{};
    size_t search_pos{0};
    size_t append_pos{0};
    bool has_match{false};
    std::vector<std::pair<int32_t, int32_t>> groups{};
    std::vector<std::string> group_strs{};
};

void PopulateMatcherResult(MatcherPayload &m, const std::smatch &sm, size_t base_offset)
{
    m.has_match = true;
    m.groups.clear();
    m.group_strs.clear();
    for (size_t i = 0; i < sm.size(); ++i) {
        if (sm[i].matched) {
            int32_t st = static_cast<int32_t>(base_offset + static_cast<size_t>(sm.position(i)));
            int32_t en = st + static_cast<int32_t>(sm.length(i));
            m.groups.emplace_back(st, en);
            m.group_strs.push_back(sm[i].str());
        } else {
            m.groups.emplace_back(-1, -1);
            m.group_strs.emplace_back();
        }
    }
}

std::string ReadCharSequenceUtf8(jobject obj)
{
    if (!obj) {
        return "";
    }
    auto &jni = JniEnvironment::Instance();
    if (const char *s = jni.GetStringChars(reinterpret_cast<jstring>(obj))) {
        return std::string(s);
    }
    if (auto syn = jni.LookupObject(obj)) {
        auto it = syn->string_fields.find("value");
        if (it != syn->string_fields.end()) {
            return it->second;
        }
    }
    return "";
}

DexValue HandlePattern(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *, jobject &out_exception, bool &handled)
{
    auto &jni = JniEnvironment::Instance();
    if (is_static) {
        if (ref.name == "compile" && !arg_regs.empty()) {
            const char *pstr = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
            int32_t flags = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 0;
            auto pp = std::make_shared<PatternPayload>();
            pp->raw_pattern = pstr ? pstr : "";
            pp->flags = flags;
            if (!CompileJavaRegex(pp->raw_pattern, flags, pp->compiled, out_exception)) {
                handled = true;
                return {0, 'L'};
            }
            auto syn = std::make_shared<JniSyntheticObject>();
            syn->type = JniObjectType::Generic;
            syn->class_name = "java/util/regex/Pattern";
            syn->runtime_payload = std::move(pp);
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.RegisterObject(syn)), 'L'};
        }
        if (ref.name == "matches" && arg_regs.size() >= 2) {
            std::string pstr = ReadCharSequenceUtf8(reinterpret_cast<jobject>(arg_regs[0]));
            std::string in = ReadCharSequenceUtf8(reinterpret_cast<jobject>(arg_regs[1]));
            bool ok = java_rt::RegexMatches(in, pstr, out_exception);
            handled = true;
            return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
        }
        if (ref.name == "quote" && !arg_regs.empty()) {
            const char *s = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[0]));
            std::string quoted = std::string("\\Q") + (s ? s : "") + "\\E";
            handled = true;
            return {reinterpret_cast<uintptr_t>(jni.CreateString(quoted)), 'L'};
        }
        return {0, 'V'};
    }

    if (arg_regs.empty()) {
        return {0, 'V'};
    }
    auto syn = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[0]));
    if (!syn || !syn->runtime_payload) {
        return {0, 'V'};
    }
    auto pp = std::static_pointer_cast<PatternPayload>(syn->runtime_payload);

    if (ref.name == "pattern" || ref.name == "toString") {
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(pp->raw_pattern)), 'L'};
    }
    if (ref.name == "flags") {
        handled = true;
        return {static_cast<uint64_t>(pp->flags), 'I'};
    }
    if (ref.name == "matcher" && arg_regs.size() >= 2) {
        std::string in = ReadCharSequenceUtf8(reinterpret_cast<jobject>(arg_regs[1]));
        auto mp = std::make_shared<MatcherPayload>();
        mp->pattern = pp;
        mp->input = std::move(in);
        auto msyn = std::make_shared<JniSyntheticObject>();
        msyn->type = JniObjectType::Generic;
        msyn->class_name = "java/util/regex/Matcher";
        msyn->runtime_payload = std::move(mp);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(msyn)), 'L'};
    }
    if (ref.name == "split" && arg_regs.size() >= 2) {
        std::string in = ReadCharSequenceUtf8(reinterpret_cast<jobject>(arg_regs[1]));
        int32_t limit = (arg_regs.size() >= 3) ? static_cast<int32_t>(arg_regs[2]) : 0;
        std::vector<std::string> parts =
            java_rt::RegexSplit(in, pp->raw_pattern, limit, out_exception);
        auto arr = std::make_shared<JniSyntheticObject>();
        arr->type = JniObjectType::ObjectArray;
        arr->class_name = "java/lang/String";
        for (const auto &part : parts) {
            arr->object_data.push_back(jni.CreateString(part));
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.RegisterObject(arr)), 'L'};
    }
    return {0, 'V'};
}

DexValue HandleMatcher(DexVm &, const DexMethodRef &ref, std::span<const uint64_t> arg_regs,
                       bool is_static, JNIEnv *, jobject &out_exception, bool &handled)
{
    if (is_static || arg_regs.empty()) {
        return {0, 'V'};
    }
    auto &jni = JniEnvironment::Instance();
    jobject self = reinterpret_cast<jobject>(arg_regs[0]);
    auto syn = jni.LookupObject(self);
    if (!syn || !syn->runtime_payload) {
        return {0, 'V'};
    }
    auto m = std::static_pointer_cast<MatcherPayload>(syn->runtime_payload);

    if (ref.name == "matches") {
        std::smatch sm;
        bool ok = std::regex_match(m->input, sm, m->pattern->compiled);
        if (ok) {
            PopulateMatcherResult(*m, sm, 0);
            m->search_pos = m->input.size();
        } else {
            m->has_match = false;
        }
        handled = true;
        return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
    }
    if (ref.name == "lookingAt") {
        std::smatch sm;
        bool ok = std::regex_search(m->input, sm, m->pattern->compiled,
                                    std::regex_constants::match_continuous);
        if (ok) {
            PopulateMatcherResult(*m, sm, 0);
            m->search_pos = static_cast<size_t>(m->groups[0].second);
        } else {
            m->has_match = false;
        }
        handled = true;
        return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
    }
    if (ref.name == "find") {
        if (arg_regs.size() >= 2) {
            m->search_pos =
                static_cast<size_t>(std::max<int32_t>(0, static_cast<int32_t>(arg_regs[1])));
        }
        if (m->search_pos > m->input.size()) {
            m->has_match = false;
            handled = true;
            return {0, 'Z'};
        }
        std::smatch sm;
        auto first = m->input.cbegin() + static_cast<ptrdiff_t>(m->search_pos);
        bool ok = std::regex_search(first, m->input.cend(), sm, m->pattern->compiled);
        if (ok) {
            PopulateMatcherResult(*m, sm, m->search_pos);
            size_t end_pos = static_cast<size_t>(m->groups[0].second);
            m->search_pos = (end_pos == m->search_pos) ? (end_pos + 1) : end_pos;
        } else {
            m->has_match = false;
        }
        handled = true;
        return {static_cast<uint64_t>(ok ? 1 : 0), 'Z'};
    }
    if (ref.name == "groupCount") {
        handled = true;
        return {static_cast<uint64_t>(m->pattern->compiled.mark_count()), 'I'};
    }
    if (ref.name == "group" || ref.name == "start" || ref.name == "end") {
        if (!m->has_match) {
            out_exception =
                JavaRuntime::CreateException("java/lang/IllegalStateException", "No match found");
            handled = true;
            return {0, ref.name == "group" ? 'L' : 'I'};
        }
        int32_t gidx = (arg_regs.size() >= 2) ? static_cast<int32_t>(arg_regs[1]) : 0;
        if (gidx < 0 || static_cast<size_t>(gidx) >= m->groups.size()) {
            out_exception =
                JavaRuntime::CreateException("java/lang/IndexOutOfBoundsException", "Bad group");
            handled = true;
            return {0, ref.name == "group" ? 'L' : 'I'};
        }
        handled = true;
        if (ref.name == "group") {
            if (m->groups[static_cast<size_t>(gidx)].first < 0) {
                return {0, 'L'};
            }
            return {reinterpret_cast<uintptr_t>(
                        jni.CreateString(m->group_strs[static_cast<size_t>(gidx)])),
                    'L'};
        }
        int32_t pos = (ref.name == "start") ? m->groups[static_cast<size_t>(gidx)].first
                                            : m->groups[static_cast<size_t>(gidx)].second;
        return {static_cast<uint64_t>(static_cast<uint32_t>(pos)), 'I'};
    }
    if ((ref.name == "replaceAll" || ref.name == "replaceFirst") && arg_regs.size() >= 2) {
        const char *rep = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
        std::string res = java_rt::RegexReplace(m->input, m->pattern->raw_pattern, rep ? rep : "",
                                                ref.name == "replaceFirst", out_exception);
        handled = true;
        return {reinterpret_cast<uintptr_t>(jni.CreateString(res)), 'L'};
    }
    if (ref.name == "appendReplacement" && arg_regs.size() >= 3) {
        if (!m->has_match) {
            out_exception =
                JavaRuntime::CreateException("java/lang/IllegalStateException", "No match");
            handled = true;
            return {reinterpret_cast<uintptr_t>(self), 'L'};
        }
        auto sb = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        const char *rep = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[2]));
        if (sb) {
            size_t st = static_cast<size_t>(m->groups[0].first);
            size_t en = static_cast<size_t>(m->groups[0].second);
            sb->string_fields["value"].append(m->input, m->append_pos, st - m->append_pos);
            sb->string_fields["value"].append(rep ? rep : "");
            m->append_pos = en;
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    if (ref.name == "appendTail" && arg_regs.size() >= 2) {
        auto sb = jni.LookupObject(reinterpret_cast<jobject>(arg_regs[1]));
        if (sb && m->append_pos <= m->input.size()) {
            sb->string_fields["value"].append(m->input, m->append_pos,
                                              m->input.size() - m->append_pos);
        }
        handled = true;
        return {reinterpret_cast<uintptr_t>(arg_regs[1]), 'L'};
    }
    if (ref.name == "reset") {
        if (arg_regs.size() >= 2) {
            const char *in = jni.GetStringChars(reinterpret_cast<jstring>(arg_regs[1]));
            m->input = in ? in : "";
        }
        m->search_pos = 0;
        m->append_pos = 0;
        m->has_match = false;
        m->groups.clear();
        m->group_strs.clear();
        handled = true;
        return {reinterpret_cast<uintptr_t>(self), 'L'};
    }
    return {0, 'V'};
}

} // namespace

namespace java_rt {

bool RegexMatches(std::string_view input, std::string_view pattern, jobject &out_exception)
{
    std::regex re;
    if (!CompileJavaRegex(pattern, 0, re, out_exception)) {
        return false;
    }
    std::string s(input);
    return std::regex_match(s, re);
}

std::string RegexReplace(std::string_view input, std::string_view pattern,
                         std::string_view replacement, bool first_only, jobject &out_exception)
{
    std::regex re;
    if (!CompileJavaRegex(pattern, 0, re, out_exception)) {
        return std::string(input);
    }
    std::string s(input);
    std::string rep(replacement);
    auto flags =
        first_only ? std::regex_constants::format_first_only : std::regex_constants::match_default;
    return std::regex_replace(s, re, rep, flags);
}

std::vector<std::string> RegexSplit(std::string_view input, std::string_view pattern, int32_t limit,
                                    jobject &out_exception)
{
    std::regex re;
    if (!CompileJavaRegex(pattern, 0, re, out_exception)) {
        return {};
    }
    std::string s(input);
    std::vector<std::string> parts;
    std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
    std::sregex_token_iterator end;
    for (; it != end; ++it) {
        if (limit > 0 && static_cast<int32_t>(parts.size()) + 1 >= limit) {
            size_t tail_off = static_cast<size_t>(it->first - s.begin());
            parts.push_back(s.substr(tail_off));
            break;
        }
        parts.push_back(it->str());
    }
    if (parts.empty()) {
        parts.push_back(s);
    }
    if (limit == 0) {
        while (parts.size() > 1 && parts.back().empty()) {
            parts.pop_back();
        }
    }
    return parts;
}

} // namespace java_rt

void RegisterIoRegexIntrinsics(JavaRuntime &rt)
{
    rt.RegisterClass("java/io/File", HandleFile, {"java/io/Serializable", "java/lang/Comparable"});
    rt.RegisterStaticField("java/io/File", "separator", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString("/"));
    });
    rt.RegisterStaticField("java/io/File", "pathSeparator", []() -> uint64_t {
        return reinterpret_cast<uintptr_t>(JniEnvironment::Instance().CreateString(":"));
    });
    rt.RegisterStaticField("java/io/File", "separatorChar", []() -> uint64_t { return '/'; });
    rt.RegisterStaticField("java/io/File", "pathSeparatorChar", []() -> uint64_t { return ':'; });

    for (std::string_view cls :
         {"java/io/InputStream", "java/io/OutputStream", "java/io/ByteArrayInputStream",
          "java/io/ByteArrayOutputStream", "java/io/FileInputStream", "java/io/FileOutputStream",
          "java/io/BufferedInputStream", "java/io/BufferedOutputStream", "java/io/DataInputStream",
          "java/io/DataOutputStream"}) {
        rt.RegisterClass(cls,
                         [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                            JNIEnv *e, jobject &ex, bool &h) {
                             return HandleByteStreams(
                                 vm, DexFile::DescriptorToSlashName(r.class_descriptor), r, a, s, e,
                                 ex, h);
                         },
                         {"java/io/Closeable", "java/lang/AutoCloseable"});
    }

    for (std::string_view cls :
         {"java/io/Reader", "java/io/Writer", "java/io/InputStreamReader",
          "java/io/OutputStreamWriter", "java/io/FileReader", "java/io/FileWriter",
          "java/io/StringReader", "java/io/StringWriter", "java/io/BufferedReader",
          "java/io/BufferedWriter", "java/io/PrintStream", "java/io/PrintWriter"}) {
        rt.RegisterClass(cls,
                         [](DexVm &vm, const DexMethodRef &r, std::span<const uint64_t> a, bool s,
                            JNIEnv *e, jobject &ex, bool &h) {
                             return HandleReadersWritersPrint(
                                 vm, DexFile::DescriptorToSlashName(r.class_descriptor), r, a, s, e,
                                 ex, h);
                         },
                         {"java/io/Closeable", "java/lang/AutoCloseable"});
    }

    rt.RegisterStaticField("java/lang/System", "out",
                           []() -> uint64_t { return GetSystemPrintStream(false); });
    rt.RegisterStaticField("java/lang/System", "err",
                           []() -> uint64_t { return GetSystemPrintStream(true); });

    rt.RegisterClass("java/util/regex/Pattern", HandlePattern);
    rt.RegisterClass("java/util/regex/Matcher", HandleMatcher);
}

} // namespace portage::package
