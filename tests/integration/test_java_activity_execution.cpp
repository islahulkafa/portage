#include <windows.h>

#include "package/dex_types.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>

namespace stdfs = std::filesystem;
using namespace portage::package;

namespace {

inline void AppendU16(std::vector<uint8_t> &buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

inline void AppendU32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

inline void AppendUleb128(std::vector<uint8_t> &buf, uint32_t val)
{
    do {
        uint8_t b = static_cast<uint8_t>(val & 0x7Fu);
        val >>= 7;
        if (val != 0) {
            b |= 0x80u;
        }
        buf.push_back(b);
    } while (val != 0);
}

inline void Align4(std::vector<uint8_t> &buf)
{
    while ((buf.size() & 3u) != 0) {
        buf.push_back(0);
    }
}

stdfs::path FindArtifact(const std::string &rel)
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path(rel),
        stdfs::path("..") / rel,
        stdfs::current_path() / rel,
        stdfs::current_path() / "build" / "debug" / rel,
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return {};
}

struct ProcessResult {
    DWORD exit_code{1};
    std::string output{};
};

ProcessResult RunPortageCommand(const std::string &args)
{
    ProcessResult res{};
    auto portage_exe = FindArtifact("src/portage.exe");
    if (portage_exe.empty()) {
        return res;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return res;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + portage_exe.string() + "\" " + args;
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return res;
    }

    char buf[4096];
    DWORD read_bytes = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &read_bytes, nullptr) && read_bytes > 0) {
        res.output.append(buf, read_bytes);
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &res.exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

class IntegDexBuilder {
  public:
    struct MethodSpec {
        uint32_t class_idx{0};
        uint32_t method_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
        uint16_t registers_size{0};
        uint16_t ins_size{0};
        uint16_t outs_size{0};
        std::vector<uint16_t> insns;
    };

    struct ClassSpec {
        uint32_t class_idx{0};
        uint32_t superclass_idx{DEX_NO_INDEX};
        uint32_t access_flags{ACC_PUBLIC};
        std::vector<MethodSpec> direct_methods;
        std::vector<MethodSpec> virtual_methods;
    };

    uint32_t AddString(std::string s)
    {
        auto it = string_map_.find(s);
        if (it != string_map_.end()) {
            return it->second;
        }
        uint32_t idx = static_cast<uint32_t>(strings_.size());
        strings_.push_back(s);
        string_map_.emplace(std::move(s), idx);
        return idx;
    }

    uint32_t AddType(const std::string &desc)
    {
        auto it = type_map_.find(desc);
        if (it != type_map_.end()) {
            return it->second;
        }
        uint32_t sid = AddString(desc);
        uint32_t idx = static_cast<uint32_t>(types_.size());
        types_.push_back(sid);
        type_map_.emplace(desc, idx);
        return idx;
    }

    uint32_t AddProto(const std::string &ret_desc, const std::vector<std::string> &params)
    {
        std::string key = "(";
        for (const auto &p : params) {
            key += p;
        }
        key += ")" + ret_desc;
        auto it = proto_map_.find(key);
        if (it != proto_map_.end()) {
            return it->second;
        }
        std::string shorty;
        shorty.push_back(ret_desc.front() == 'L' || ret_desc.front() == '[' ? 'L'
                                                                            : ret_desc.front());
        std::vector<uint32_t> pt;
        for (const auto &p : params) {
            shorty.push_back(p.front() == 'L' || p.front() == '[' ? 'L' : p.front());
            pt.push_back(AddType(p));
        }
        uint32_t shorty_idx = AddString(shorty);
        uint32_t ret_idx = AddType(ret_desc);
        uint32_t idx = static_cast<uint32_t>(protos_.size());
        protos_.push_back({shorty_idx, ret_idx, std::move(pt)});
        proto_map_.emplace(std::move(key), idx);
        return idx;
    }

    uint32_t AddMethod(const std::string &cls, const std::string &name, const std::string &ret_desc,
                       const std::vector<std::string> &params)
    {
        std::string sig = "(";
        for (const auto &p : params) {
            sig += p;
        }
        sig += ")" + ret_desc;
        std::string key = cls + "->" + name + sig;
        auto it = method_map_.find(key);
        if (it != method_map_.end()) {
            return it->second;
        }
        uint32_t c = AddType(cls);
        uint32_t p = AddProto(ret_desc, params);
        uint32_t n = AddString(name);
        uint32_t idx = static_cast<uint32_t>(methods_.size());
        methods_.push_back({static_cast<uint16_t>(c), static_cast<uint16_t>(p), n});
        method_map_.emplace(std::move(key), idx);
        return idx;
    }

    ClassSpec &AddClass(const std::string &desc, const std::string &super_desc)
    {
        ClassSpec spec;
        spec.class_idx = AddType(desc);
        spec.superclass_idx = AddType(super_desc);
        classes_.push_back(std::move(spec));
        return classes_.back();
    }

    std::vector<uint8_t> Build()
    {
        uint32_t header_sz = sizeof(DexHeader);
        uint32_t str_off = header_sz;
        uint32_t type_off = str_off + static_cast<uint32_t>(strings_.size() * 4);
        uint32_t proto_off = type_off + static_cast<uint32_t>(types_.size() * 4);
        uint32_t method_off =
            proto_off + static_cast<uint32_t>(protos_.size() * sizeof(DexProtoId));
        uint32_t class_off =
            method_off + static_cast<uint32_t>(methods_.size() * sizeof(DexMethodId));
        uint32_t data_off =
            class_off + static_cast<uint32_t>(classes_.size() * sizeof(DexClassDef));

        std::vector<uint8_t> data_sec;
        std::vector<uint32_t> str_offs(strings_.size(), 0);
        for (size_t i = 0; i < strings_.size(); ++i) {
            str_offs[i] = static_cast<uint32_t>(data_sec.size());
            AppendUleb128(data_sec, static_cast<uint32_t>(strings_[i].size()));
            data_sec.insert(data_sec.end(), strings_[i].begin(), strings_[i].end());
            data_sec.push_back(0);
        }
        Align4(data_sec);

        std::vector<uint32_t> proto_param_offs(protos_.size(), 0);
        for (size_t i = 0; i < protos_.size(); ++i) {
            if (!protos_[i].params.empty()) {
                proto_param_offs[i] = static_cast<uint32_t>(data_sec.size());
                AppendU32(data_sec, static_cast<uint32_t>(protos_[i].params.size()));
                for (uint32_t pt : protos_[i].params) {
                    AppendU16(data_sec, static_cast<uint16_t>(pt));
                }
                Align4(data_sec);
            }
        }

        auto emit_code_item = [&](const MethodSpec &m) -> uint32_t {
            if (m.insns.empty()) {
                return 0;
            }
            Align4(data_sec);
            uint32_t off = static_cast<uint32_t>(data_sec.size());
            AppendU16(data_sec, m.registers_size);
            AppendU16(data_sec, m.ins_size);
            AppendU16(data_sec, m.outs_size);
            AppendU16(data_sec, 0);
            AppendU32(data_sec, 0);
            AppendU32(data_sec, static_cast<uint32_t>(m.insns.size()));
            for (uint16_t w : m.insns) {
                AppendU16(data_sec, w);
            }
            return off;
        };

        std::vector<uint32_t> class_data_offs(classes_.size(), 0);
        for (size_t c = 0; c < classes_.size(); ++c) {
            auto &cls = classes_[c];
            std::vector<uint32_t> direct_offs;
            for (const auto &m : cls.direct_methods) {
                direct_offs.push_back(emit_code_item(m));
            }
            std::vector<uint32_t> virtual_offs;
            for (const auto &m : cls.virtual_methods) {
                virtual_offs.push_back(emit_code_item(m));
            }
            class_data_offs[c] = static_cast<uint32_t>(data_sec.size());
            AppendUleb128(data_sec, 0);
            AppendUleb128(data_sec, 0);
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.direct_methods.size()));
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.virtual_methods.size()));

            uint32_t prev = 0;
            for (size_t i = 0; i < cls.direct_methods.size(); ++i) {
                AppendUleb128(data_sec, cls.direct_methods[i].method_idx - prev);
                AppendUleb128(data_sec, cls.direct_methods[i].access_flags);
                AppendUleb128(data_sec, direct_offs[i] ? (data_off + direct_offs[i]) : 0);
                prev = cls.direct_methods[i].method_idx;
            }
            prev = 0;
            for (size_t i = 0; i < cls.virtual_methods.size(); ++i) {
                AppendUleb128(data_sec, cls.virtual_methods[i].method_idx - prev);
                AppendUleb128(data_sec, cls.virtual_methods[i].access_flags);
                AppendUleb128(data_sec, virtual_offs[i] ? (data_off + virtual_offs[i]) : 0);
                prev = cls.virtual_methods[i].method_idx;
            }
        }
        Align4(data_sec);

        uint32_t total_sz = data_off + static_cast<uint32_t>(data_sec.size());

        std::vector<uint8_t> out(total_sz, 0);
        DexHeader hdr{};
        std::memcpy(hdr.magic, "dex\n035\0", 8);
        hdr.file_size = total_sz;
        hdr.header_size = header_sz;
        hdr.endian_tag = DEX_ENDIAN_CONSTANT;
        hdr.string_ids_size = static_cast<uint32_t>(strings_.size());
        hdr.string_ids_off = strings_.empty() ? 0 : str_off;
        hdr.type_ids_size = static_cast<uint32_t>(types_.size());
        hdr.type_ids_off = types_.empty() ? 0 : type_off;
        hdr.proto_ids_size = static_cast<uint32_t>(protos_.size());
        hdr.proto_ids_off = protos_.empty() ? 0 : proto_off;
        hdr.method_ids_size = static_cast<uint32_t>(methods_.size());
        hdr.method_ids_off = methods_.empty() ? 0 : method_off;
        hdr.class_defs_size = static_cast<uint32_t>(classes_.size());
        hdr.class_defs_off = classes_.empty() ? 0 : class_off;
        hdr.data_size = static_cast<uint32_t>(data_sec.size());
        hdr.data_off = data_off;
        std::memcpy(out.data(), &hdr, sizeof(DexHeader));

        for (size_t i = 0; i < strings_.size(); ++i) {
            uint32_t v = data_off + str_offs[i];
            std::memcpy(out.data() + str_off + i * 4, &v, 4);
        }
        for (size_t i = 0; i < types_.size(); ++i) {
            uint32_t v = types_[i];
            std::memcpy(out.data() + type_off + i * 4, &v, 4);
        }
        for (size_t i = 0; i < protos_.size(); ++i) {
            DexProtoId p{};
            p.shorty_idx = protos_[i].shorty_idx;
            p.return_type_idx = protos_[i].ret_idx;
            p.parameters_off = proto_param_offs[i] ? (data_off + proto_param_offs[i]) : 0;
            std::memcpy(out.data() + proto_off + i * sizeof(DexProtoId), &p, sizeof(DexProtoId));
        }
        for (size_t i = 0; i < methods_.size(); ++i) {
            std::memcpy(out.data() + method_off + i * sizeof(DexMethodId), &methods_[i],
                        sizeof(DexMethodId));
        }
        for (size_t i = 0; i < classes_.size(); ++i) {
            DexClassDef cd{};
            cd.class_idx = classes_[i].class_idx;
            cd.access_flags = classes_[i].access_flags;
            cd.superclass_idx = classes_[i].superclass_idx;
            cd.class_data_off = data_off + class_data_offs[i];
            std::memcpy(out.data() + class_off + i * sizeof(DexClassDef), &cd, sizeof(DexClassDef));
        }
        std::memcpy(out.data() + data_off, data_sec.data(), data_sec.size());
        uint32_t adler = static_cast<uint32_t>(adler32(1, out.data() + 12, total_sz - 12));
        std::memcpy(out.data() + 8, &adler, 4);
        return out;
    }

  private:
    struct ProtoSpec {
        uint32_t shorty_idx{0};
        uint32_t ret_idx{0};
        std::vector<uint32_t> params;
    };

    std::vector<std::string> strings_;
    std::map<std::string, uint32_t> string_map_;
    std::vector<uint32_t> types_;
    std::map<std::string, uint32_t> type_map_;
    std::vector<ProtoSpec> protos_;
    std::map<std::string, uint32_t> proto_map_;
    std::vector<DexMethodId> methods_;
    std::map<std::string, uint32_t> method_map_;
    std::vector<ClassSpec> classes_;
};

std::vector<uint8_t> BuildJavaManifest(const std::string &pkg_name, const std::string &act_name)
{
    std::vector<std::string> pool = {
        "manifest",
        "package",
        pkg_name,
        "application",
        "activity",
        "name",
        act_name,
        "intent-filter",
        "action",
        "android.intent.action.MAIN",
        "category",
        "android.intent.category.LAUNCHER",
        "http://schemas.android.com/apk/res/android",
    };
    auto find_idx = [&](const std::string &s) -> uint32_t {
        for (uint32_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == s) {
                return i;
            }
        }
        return 0xFFFFFFFFu;
    };

    std::vector<uint8_t> sp_strings;
    std::vector<uint32_t> sp_offs;
    for (const auto &s : pool) {
        sp_offs.push_back(static_cast<uint32_t>(sp_strings.size()));
        AppendU16(sp_strings, static_cast<uint16_t>(s.size()));
        for (char c : s) {
            AppendU16(sp_strings, static_cast<uint8_t>(c));
        }
        AppendU16(sp_strings, 0);
    }
    Align4(sp_strings);

    std::vector<uint8_t> sp_chunk;
    uint32_t sp_size =
        28 + static_cast<uint32_t>(pool.size() * 4) + static_cast<uint32_t>(sp_strings.size());
    AppendU16(sp_chunk, 0x0001);
    AppendU16(sp_chunk, 28);
    AppendU32(sp_chunk, sp_size);
    AppendU32(sp_chunk, static_cast<uint32_t>(pool.size()));
    AppendU32(sp_chunk, 0);
    AppendU32(sp_chunk, 0);
    AppendU32(sp_chunk, 28 + static_cast<uint32_t>(pool.size() * 4));
    AppendU32(sp_chunk, 0);
    for (uint32_t o : sp_offs) {
        AppendU32(sp_chunk, o);
    }
    sp_chunk.insert(sp_chunk.end(), sp_strings.begin(), sp_strings.end());

    std::vector<uint8_t> body;
    auto push_start = [&](const std::string &tag,
                          const std::vector<std::pair<std::string, std::string>> &attrs) {
        uint32_t sz = 36 + static_cast<uint32_t>(attrs.size() * 20);
        AppendU16(body, 0x0102);
        AppendU16(body, 16);
        AppendU32(body, sz);
        AppendU32(body, 1);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, find_idx(tag));
        AppendU16(body, 20);
        AppendU16(body, 20);
        AppendU16(body, static_cast<uint16_t>(attrs.size()));
        AppendU16(body, 0);
        AppendU16(body, 0);
        AppendU16(body, 0);
        for (const auto &[k, v] : attrs) {
            AppendU32(body, find_idx("http://schemas.android.com/apk/res/android"));
            AppendU32(body, find_idx(k));
            uint32_t vi = find_idx(v);
            AppendU32(body, vi);
            AppendU16(body, 8);
            body.push_back(0);
            body.push_back(0x03);
            AppendU32(body, vi);
        }
    };
    auto push_end = [&](const std::string &tag) {
        AppendU16(body, 0x0103);
        AppendU16(body, 16);
        AppendU32(body, 24);
        AppendU32(body, 1);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, 0xFFFFFFFFu);
        AppendU32(body, find_idx(tag));
    };

    push_start("manifest", {{"package", pkg_name}});
    push_start("application", {});
    push_start("activity", {{"name", act_name}});
    push_start("intent-filter", {});
    push_start("action", {{"name", "android.intent.action.MAIN"}});
    push_end("action");
    push_start("category", {{"name", "android.intent.category.LAUNCHER"}});
    push_end("category");
    push_end("intent-filter");
    push_end("activity");
    push_end("application");
    push_end("manifest");

    std::vector<uint8_t> doc;
    uint32_t total = 8 + static_cast<uint32_t>(sp_chunk.size() + body.size());
    AppendU16(doc, 0x0003);
    AppendU16(doc, 8);
    AppendU32(doc, total);
    doc.insert(doc.end(), sp_chunk.begin(), sp_chunk.end());
    doc.insert(doc.end(), body.begin(), body.end());
    return doc;
}

bool WriteZipApk(const stdfs::path &dest,
                 const std::vector<std::pair<std::string, std::vector<uint8_t>>> &entries)
{
    std::vector<uint8_t> archive;
    struct CentralRecord {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };
    std::vector<CentralRecord> records;

    for (const auto &[name, data] : entries) {
        uint32_t off = static_cast<uint32_t>(archive.size());
        uint32_t crc = static_cast<uint32_t>(
            crc32(0, data.empty() ? nullptr : data.data(), static_cast<uInt>(data.size())));
        uint32_t sz = static_cast<uint32_t>(data.size());
        records.push_back({name, crc, sz, off});

        AppendU32(archive, 0x04034B50u);
        AppendU16(archive, 20);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, crc);
        AppendU32(archive, sz);
        AppendU32(archive, sz);
        AppendU16(archive, static_cast<uint16_t>(name.size()));
        AppendU16(archive, 0);
        archive.insert(archive.end(), name.begin(), name.end());
        archive.insert(archive.end(), data.begin(), data.end());
    }

    uint32_t cd_off = static_cast<uint32_t>(archive.size());
    for (const auto &r : records) {
        AppendU32(archive, 0x02014B50u);
        AppendU16(archive, 20);
        AppendU16(archive, 20);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, r.crc);
        AppendU32(archive, r.size);
        AppendU32(archive, r.size);
        AppendU16(archive, static_cast<uint16_t>(r.name.size()));
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU16(archive, 0);
        AppendU32(archive, 0);
        AppendU32(archive, r.offset);
        archive.insert(archive.end(), r.name.begin(), r.name.end());
    }
    uint32_t cd_sz = static_cast<uint32_t>(archive.size()) - cd_off;
    AppendU32(archive, 0x06054B50u);
    AppendU16(archive, 0);
    AppendU16(archive, 0);
    AppendU16(archive, static_cast<uint16_t>(records.size()));
    AppendU16(archive, static_cast<uint16_t>(records.size()));
    AppendU32(archive, cd_sz);
    AppendU32(archive, cd_off);
    AppendU16(archive, 0);

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(archive.data()),
              static_cast<std::streamsize>(archive.size()));
    return out.good();
}

} // namespace

TEST(JavaActivityExecution, RunsHybridJavaActivityApkWithSystemLoadLibrary)
{
    auto so_path = FindArtifact("tests/guests/libjavahybrid.so");
    ASSERT_FALSE(so_path.empty()) << "libjavahybrid.so not found";

    std::ifstream so_in(so_path, std::ios::binary);
    ASSERT_TRUE(so_in.is_open());
    std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                  std::istreambuf_iterator<char>());

    IntegDexBuilder b;
    uint32_t s_lib = b.AddString("javahybrid");
    uint32_t m_clinit = b.AddMethod("Lcom/portage/hybrid/MainActivity;", "<clinit>", "V", {});
    uint32_t m_load = b.AddMethod("Ljava/lang/System;", "loadLibrary", "V", {"Ljava/lang/String;"});
    uint32_t m_init = b.AddMethod("Lcom/portage/hybrid/MainActivity;", "<init>", "V", {});
    uint32_t m_super_init = b.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    uint32_t m_on_create =
        b.AddMethod("Lcom/portage/hybrid/MainActivity;", "onCreate", "V", {"Landroid/os/Bundle;"});
    uint32_t m_sv_init =
        b.AddMethod("Landroid/view/SurfaceView;", "<init>", "V", {"Landroid/content/Context;"});
    uint32_t m_set_cv =
        b.AddMethod("Landroid/app/Activity;", "setContentView", "V", {"Landroid/view/View;"});
    uint32_t m_get_holder =
        b.AddMethod("Landroid/view/SurfaceView;", "getHolder", "Landroid/view/SurfaceHolder;", {});
    uint32_t m_add_cb = b.AddMethod("Landroid/view/SurfaceHolder;", "addCallback", "V",
                                    {"Landroid/view/SurfaceHolder$Callback;"});
    uint32_t m_surf_created = b.AddMethod("Lcom/portage/hybrid/MainActivity;", "surfaceCreated",
                                          "V", {"Landroid/view/SurfaceHolder;"});
    uint32_t m_get_surf =
        b.AddMethod("Landroid/view/SurfaceHolder;", "getSurface", "Landroid/view/Surface;", {});
    uint32_t m_native_ready = b.AddMethod("Lcom/portage/hybrid/MainActivity;",
                                          "nativeOnSurfaceReady", "I", {"Landroid/view/Surface;"});

    uint32_t t_sv = b.AddType("Landroid/view/SurfaceView;");
    auto &cls = b.AddClass("Lcom/portage/hybrid/MainActivity;", "Landroid/app/Activity;");
    cls.direct_methods.push_back({cls.class_idx,
                                  m_clinit,
                                  ACC_PUBLIC | ACC_STATIC | ACC_CONSTRUCTOR,
                                  1,
                                  0,
                                  1,
                                  {0x001Au, static_cast<uint16_t>(s_lib), 0x1071u,
                                   static_cast<uint16_t>(m_load), 0x0000u, 0x000Eu}});
    cls.direct_methods.push_back(
        {cls.class_idx,
         m_init,
         ACC_PUBLIC | ACC_CONSTRUCTOR,
         1,
         1,
         1,
         {0x1070u, static_cast<uint16_t>(m_super_init), 0x0000u, 0x000Eu}});
    cls.virtual_methods.push_back(
        {cls.class_idx,
         m_on_create,
         ACC_PUBLIC,
         4,
         2,
         2,
         {0x0022u, static_cast<uint16_t>(t_sv), 0x2070u, static_cast<uint16_t>(m_sv_init), 0x0020u,
          0x206Eu, static_cast<uint16_t>(m_set_cv), 0x0002u, 0x106Eu,
          static_cast<uint16_t>(m_get_holder), 0x0000u, 0x010Cu, 0x2072u,
          static_cast<uint16_t>(m_add_cb), 0x0021u, 0x000Eu}});
    cls.virtual_methods.push_back(
        {cls.class_idx,
         m_surf_created,
         ACC_PUBLIC,
         3,
         2,
         2,
         {0x1072u, static_cast<uint16_t>(m_get_surf), 0x0002u, 0x000Cu, 0x206Eu,
          static_cast<uint16_t>(m_native_ready), 0x0020u, 0x010Au, 0x000Eu}});
    cls.virtual_methods.push_back(
        {cls.class_idx, m_native_ready, ACC_PUBLIC | ACC_NATIVE, 0, 0, 0, {}});

    stdfs::path apk_path = stdfs::temp_directory_path() / "portage_hybrid_test.apk";
    auto manifest = BuildJavaManifest("com.portage.hybrid", ".MainActivity");
    auto dex = b.Build();

    ASSERT_TRUE(WriteZipApk(apk_path, {{"AndroidManifest.xml", manifest},
                                       {"classes.dex", dex},
                                       {"lib/x86_64/libjavahybrid.so", so_bytes}}));

    ProcessResult check = RunPortageCommand("--check-apk \"" + apk_path.string() + "\"");
    EXPECT_EQ(check.exit_code, 0u) << check.output;

    ProcessResult run = RunPortageCommand("--headless run \"" + apk_path.string() + "\"");
    EXPECT_EQ(run.exit_code, 0u) << run.output;
    EXPECT_NE(run.output.find("[javahybrid] JNI_OnLoad invoked"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("[javahybrid] surface verified"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("APK Java activity execution completed successfully"),
              std::string::npos)
        << run.output;

    std::error_code ec;
    stdfs::remove(apk_path, ec);
}
