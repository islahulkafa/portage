#include <windows.h>

#include "package/dex_file.hpp"
#include "package/dex_types.hpp"
#include "package/dex_vm.hpp"
#include "package/java_runtime.hpp"
#include "package/jni_env.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <set>
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

class RuntimeIntegDexBuilder {
  public:
    struct MethodSpec {
        uint32_t class_idx{0};
        uint32_t method_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
        uint16_t registers_size{2};
        uint16_t ins_size{1};
        uint16_t outs_size{2};
        std::vector<uint16_t> insns{};
    };

    struct FieldSpec {
        uint32_t field_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
    };

    struct ClassSpec {
        uint32_t class_idx{0};
        uint32_t super_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
        std::vector<uint32_t> interfaces{};
        std::vector<FieldSpec> instance_fields{};
        std::vector<MethodSpec> direct_methods{};
        std::vector<MethodSpec> virtual_methods{};
    };

    uint32_t AddString(const std::string &s)
    {
        for (uint32_t i = 0; i < strings_.size(); ++i) {
            if (strings_[i] == s) {
                return i;
            }
        }
        strings_.push_back(s);
        return static_cast<uint32_t>(strings_.size() - 1);
    }

    uint32_t AddType(const std::string &desc)
    {
        uint32_t str_idx = AddString(desc);
        for (uint32_t i = 0; i < types_.size(); ++i) {
            if (types_[i] == str_idx) {
                return i;
            }
        }
        types_.push_back(str_idx);
        return static_cast<uint32_t>(types_.size() - 1);
    }

    uint32_t AddField(const std::string &cls, const std::string &name, const std::string &type_desc)
    {
        uint16_t ci = static_cast<uint16_t>(AddType(cls));
        uint16_t ti = static_cast<uint16_t>(AddType(type_desc));
        uint32_t ni = AddString(name);
        fields_.push_back({ci, ti, ni});
        return static_cast<uint32_t>(fields_.size() - 1);
    }

    uint32_t AddProto(const std::string &ret, const std::vector<std::string> &params)
    {
        std::string shorty;
        shorty.push_back(ret.size() == 1 ? ret[0] : 'L');
        for (const auto &p : params) {
            shorty.push_back(p.size() == 1 ? p[0] : 'L');
        }
        uint32_t shorty_idx = AddString(shorty);
        uint32_t ret_idx = AddType(ret);
        std::vector<uint16_t> plist;
        for (const auto &p : params) {
            plist.push_back(static_cast<uint16_t>(AddType(p)));
        }
        for (uint32_t i = 0; i < protos_.size(); ++i) {
            if (protos_[i].shorty_idx == shorty_idx && protos_[i].ret_idx == ret_idx &&
                protos_[i].params == plist) {
                return i;
            }
        }
        protos_.push_back({shorty_idx, ret_idx, plist});
        return static_cast<uint32_t>(protos_.size() - 1);
    }

    uint32_t AddMethod(const std::string &cls, const std::string &name, const std::string &ret,
                       const std::vector<std::string> &params)
    {
        uint16_t ci = static_cast<uint16_t>(AddType(cls));
        uint16_t pi = static_cast<uint16_t>(AddProto(ret, params));
        uint32_t ni = AddString(name);
        methods_.push_back({ci, pi, ni});
        return static_cast<uint32_t>(methods_.size() - 1);
    }

    ClassSpec &AddClass(const std::string &cls, const std::string &super_cls,
                        const std::vector<std::string> &interfaces = {})
    {
        ClassSpec cs{};
        cs.class_idx = AddType(cls);
        cs.super_idx = AddType(super_cls);
        for (const auto &iface : interfaces) {
            cs.interfaces.push_back(AddType(iface));
        }
        classes_.push_back(std::move(cs));
        return classes_.back();
    }

    static uint16_t Op11x(uint8_t op, uint8_t reg)
    {
        return static_cast<uint16_t>(op | (static_cast<uint16_t>(reg) << 8));
    }

    static uint16_t Op22cReg(uint8_t dst, uint8_t obj)
    {
        return static_cast<uint16_t>((dst & 0xFu) | ((obj & 0xFu) << 4));
    }

    static void Emit21c(std::vector<uint16_t> &c, uint8_t op, uint8_t reg, uint16_t idx)
    {
        c.push_back(static_cast<uint16_t>(op | (static_cast<uint16_t>(reg) << 8)));
        c.push_back(idx);
    }

    static void Emit21s(std::vector<uint16_t> &c, uint8_t op, uint8_t reg, int16_t lit)
    {
        c.push_back(static_cast<uint16_t>(op | (static_cast<uint16_t>(reg) << 8)));
        c.push_back(static_cast<uint16_t>(lit));
    }

    static void Emit22c(std::vector<uint16_t> &c, uint8_t op, uint8_t a, uint8_t b, uint16_t idx)
    {
        c.push_back(static_cast<uint16_t>(
            op | (static_cast<uint16_t>((a & 0xFu) | ((b & 0xFu) << 4)) << 8)));
        c.push_back(idx);
    }

    static void Emit23x(std::vector<uint16_t> &c, uint8_t op, uint8_t a, uint8_t b, uint8_t d)
    {
        c.push_back(static_cast<uint16_t>(op | (static_cast<uint16_t>(a) << 8)));
        c.push_back(static_cast<uint16_t>(b | (static_cast<uint16_t>(d) << 8)));
    }

    static void Emit35c(std::vector<uint16_t> &c, uint8_t op, uint16_t method_idx,
                        const std::vector<uint8_t> &regs)
    {
        uint8_t cnt = static_cast<uint8_t>(regs.size());
        uint8_t g = cnt > 4 ? (regs[4] & 0xFu) : 0;
        c.push_back(static_cast<uint16_t>(op | (static_cast<uint16_t>((cnt << 4) | g) << 8)));
        c.push_back(method_idx);
        uint16_t pack = 0;
        for (size_t i = 0; i < std::min<size_t>(regs.size(), 4); ++i) {
            pack |= static_cast<uint16_t>(regs[i] & 0xFu) << (i * 4);
        }
        c.push_back(pack);
    }

    std::vector<uint8_t> Build()
    {
        std::vector<uint8_t> dex(0x70, 0);
        std::vector<uint32_t> string_offs;
        for (const auto &s : strings_) {
            string_offs.push_back(static_cast<uint32_t>(dex.size()));
            AppendUleb128(dex, static_cast<uint32_t>(s.size()));
            dex.insert(dex.end(), s.begin(), s.end());
            dex.push_back(0);
        }
        Align4(dex);

        uint32_t str_ids_off = static_cast<uint32_t>(dex.size());
        for (uint32_t o : string_offs) {
            AppendU32(dex, o);
        }

        uint32_t type_ids_off = static_cast<uint32_t>(dex.size());
        for (uint32_t t : types_) {
            AppendU32(dex, t);
        }

        std::vector<uint32_t> proto_param_offs(protos_.size(), 0);
        for (size_t i = 0; i < protos_.size(); ++i) {
            if (!protos_[i].params.empty()) {
                Align4(dex);
                proto_param_offs[i] = static_cast<uint32_t>(dex.size());
                AppendU32(dex, static_cast<uint32_t>(protos_[i].params.size()));
                for (uint16_t p : protos_[i].params) {
                    AppendU16(dex, p);
                }
            }
        }
        Align4(dex);
        uint32_t proto_ids_off = static_cast<uint32_t>(dex.size());
        for (size_t i = 0; i < protos_.size(); ++i) {
            AppendU32(dex, protos_[i].shorty_idx);
            AppendU32(dex, protos_[i].ret_idx);
            AppendU32(dex, proto_param_offs[i]);
        }

        uint32_t field_ids_off = static_cast<uint32_t>(dex.size());
        for (const auto &f : fields_) {
            AppendU16(dex, f.class_idx);
            AppendU16(dex, f.type_idx);
            AppendU32(dex, f.name_idx);
        }

        uint32_t method_ids_off = static_cast<uint32_t>(dex.size());
        for (const auto &m : methods_) {
            AppendU16(dex, m.class_idx);
            AppendU16(dex, m.proto_idx);
            AppendU32(dex, m.name_idx);
        }

        std::vector<uint32_t> iface_offs(classes_.size(), 0);
        for (size_t i = 0; i < classes_.size(); ++i) {
            if (!classes_[i].interfaces.empty()) {
                Align4(dex);
                iface_offs[i] = static_cast<uint32_t>(dex.size());
                AppendU32(dex, static_cast<uint32_t>(classes_[i].interfaces.size()));
                for (uint32_t t : classes_[i].interfaces) {
                    AppendU16(dex, static_cast<uint16_t>(t));
                }
            }
        }

        std::vector<uint32_t> class_data_offs(classes_.size(), 0);
        for (size_t ci = 0; ci < classes_.size(); ++ci) {
            const auto &cs = classes_[ci];
            auto emit_methods = [&](const std::vector<MethodSpec> &ms) {
                std::vector<uint32_t> code_offs;
                for (const auto &m : ms) {
                    if (m.insns.empty()) {
                        code_offs.push_back(0);
                        continue;
                    }
                    Align4(dex);
                    code_offs.push_back(static_cast<uint32_t>(dex.size()));
                    AppendU16(dex, m.registers_size);
                    AppendU16(dex, m.ins_size);
                    AppendU16(dex, m.outs_size);
                    AppendU16(dex, 0);
                    AppendU32(dex, 0);
                    AppendU32(dex, static_cast<uint32_t>(m.insns.size()));
                    for (uint16_t w : m.insns) {
                        AppendU16(dex, w);
                    }
                }
                return code_offs;
            };
            auto dm_offs = emit_methods(cs.direct_methods);
            auto vm_offs = emit_methods(cs.virtual_methods);

            class_data_offs[ci] = static_cast<uint32_t>(dex.size());
            AppendUleb128(dex, 0);
            AppendUleb128(dex, static_cast<uint32_t>(cs.instance_fields.size()));
            AppendUleb128(dex, static_cast<uint32_t>(cs.direct_methods.size()));
            AppendUleb128(dex, static_cast<uint32_t>(cs.virtual_methods.size()));

            uint32_t prev_field = 0;
            for (const auto &f : cs.instance_fields) {
                AppendUleb128(dex, f.field_idx - prev_field);
                AppendUleb128(dex, f.access_flags);
                prev_field = f.field_idx;
            }
            auto write_m_list = [&](const std::vector<MethodSpec> &ms,
                                    const std::vector<uint32_t> &co) {
                uint32_t prev = 0;
                for (size_t i = 0; i < ms.size(); ++i) {
                    AppendUleb128(dex, ms[i].method_idx - prev);
                    AppendUleb128(dex, ms[i].access_flags);
                    AppendUleb128(dex, co[i]);
                    prev = ms[i].method_idx;
                }
            };
            write_m_list(cs.direct_methods, dm_offs);
            write_m_list(cs.virtual_methods, vm_offs);
        }

        Align4(dex);
        uint32_t class_defs_off = static_cast<uint32_t>(dex.size());
        for (size_t ci = 0; ci < classes_.size(); ++ci) {
            AppendU32(dex, classes_[ci].class_idx);
            AppendU32(dex, classes_[ci].access_flags);
            AppendU32(dex, classes_[ci].super_idx);
            AppendU32(dex, iface_offs[ci]);
            AppendU32(dex, 0xFFFFFFFFu);
            AppendU32(dex, 0);
            AppendU32(dex, class_data_offs[ci]);
            AppendU32(dex, 0);
        }

        uint32_t file_size = static_cast<uint32_t>(dex.size());
        static constexpr uint8_t kMagic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
        std::memcpy(dex.data(), kMagic, 8);
        auto patch32 = [&](size_t off, uint32_t v) { std::memcpy(dex.data() + off, &v, 4); };
        patch32(0x20, file_size);
        patch32(0x24, 0x70);
        patch32(0x28, 0x12345678u);
        patch32(0x38, static_cast<uint32_t>(strings_.size()));
        patch32(0x3C, str_ids_off);
        patch32(0x40, static_cast<uint32_t>(types_.size()));
        patch32(0x44, type_ids_off);
        patch32(0x48, static_cast<uint32_t>(protos_.size()));
        patch32(0x4C, proto_ids_off);
        patch32(0x50, static_cast<uint32_t>(fields_.size()));
        patch32(0x54, fields_.empty() ? 0 : field_ids_off);
        patch32(0x58, static_cast<uint32_t>(methods_.size()));
        patch32(0x5C, method_ids_off);
        patch32(0x60, static_cast<uint32_t>(classes_.size()));
        patch32(0x64, class_defs_off);
        patch32(0x68, file_size - str_ids_off);
        patch32(0x6C, str_ids_off);
        return dex;
    }

  private:
    struct ProtoEntry {
        uint32_t shorty_idx;
        uint32_t ret_idx;
        std::vector<uint16_t> params;
    };
    struct MethodEntry {
        uint16_t class_idx;
        uint16_t proto_idx;
        uint32_t name_idx;
    };
    struct FieldEntry {
        uint16_t class_idx;
        uint16_t type_idx;
        uint32_t name_idx;
    };

    std::vector<std::string> strings_{};
    std::vector<uint32_t> types_{};
    std::vector<ProtoEntry> protos_{};
    std::vector<FieldEntry> fields_{};
    std::vector<MethodEntry> methods_{};
    std::vector<ClassSpec> classes_{};
};

std::vector<uint8_t> BuildJavaManifest(const std::string &pkg, const std::string &activity)
{
    const std::vector<std::string> pool = {
        "package",
        "name",
        "manifest",
        "application",
        "activity",
        "intent-filter",
        "action",
        "category",
        "android.intent.action.MAIN",
        "android.intent.category.LAUNCHER",
        "http://schemas.android.com/apk/res/android",
        pkg,
        activity,
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
                          const std::vector<std::pair<std::string, std::string>> &attrs,
                          bool has_ns) {
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
            AppendU32(body, has_ns ? find_idx("http://schemas.android.com/apk/res/android")
                                   : 0xFFFFFFFFu);
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

    push_start("manifest", {{"package", pkg}}, false);
    push_start("application", {}, true);
    push_start("activity", {{"name", activity}}, true);
    push_start("intent-filter", {}, true);
    push_start("action", {{"name", "android.intent.action.MAIN"}}, true);
    push_end("action");
    push_start("category", {{"name", "android.intent.category.LAUNCHER"}}, true);
    push_end("category");
    push_end("intent-filter");
    push_end("activity");
    push_end("application");
    push_end("manifest");

    std::vector<uint8_t> xml;
    uint32_t total = 8 + static_cast<uint32_t>(sp_chunk.size() + body.size());
    AppendU16(xml, 0x0003);
    AppendU16(xml, 8);
    AppendU32(xml, total);
    xml.insert(xml.end(), sp_chunk.begin(), sp_chunk.end());
    xml.insert(xml.end(), body.begin(), body.end());
    return xml;
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

stdfs::path FindPortageExecutable()
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("src") / "portage.exe",
        stdfs::path("..") / "src" / "portage.exe",
        stdfs::current_path() / "src" / "portage.exe",
        stdfs::current_path() / "build" / "debug" / "src" / "portage.exe",
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
    auto portage_exe = FindPortageExecutable();
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
    if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(hWrite);
        CloseHandle(hRead);
        return res;
    }
    CloseHandle(hWrite);

    std::array<char, 4096> buf{};
    DWORD nread = 0;
    while (ReadFile(hRead, buf.data(), static_cast<DWORD>(buf.size()), &nread, nullptr) &&
           nread > 0) {
        res.output.append(buf.data(), nread);
    }
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &res.exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    return res;
}

} // namespace

TEST(JavaRuntimeExecution, ApkLifecycleWithExecutorFutureCollectionsAndFormatting)
{
    RuntimeIntegDexBuilder b;

    const uint32_t s_prefix = b.AddString("ok");
    const uint32_t s_colon = b.AddString(":");
    const uint32_t s_key = b.AddString("result");
    const uint32_t s_fmt = b.AddString("item=%s count=%d");
    const uint32_t s_tag = b.AddString("JavaRuntimeE2E");
    const uint32_t s_looper_msg = b.AddString("looper-done");
    const uint32_t s_bg_msg = b.AddString("bg-thread-done");

    const uint32_t t_sb = b.AddType("Ljava/lang/StringBuilder;");
    const uint32_t t_obj_arr = b.AddType("[Ljava/lang/Object;");
    const uint32_t t_map = b.AddType("Ljava/util/HashMap;");
    const uint32_t t_handler = b.AddType("Landroid/os/Handler;");
    const uint32_t t_worker = b.AddType("Lcom/portage/runtimee2e/WorkerCallable;");
    const uint32_t t_ui_run = b.AddType("Lcom/portage/runtimee2e/UiRunnable;");
    const uint32_t t_bg_thr = b.AddType("Lcom/portage/runtimee2e/BgThread;");

    const uint32_t f_wk_prefix =
        b.AddField("Lcom/portage/runtimee2e/WorkerCallable;", "prefix", "Ljava/lang/String;");
    const uint32_t f_wk_num = b.AddField("Lcom/portage/runtimee2e/WorkerCallable;", "num", "I");
    const uint32_t f_ui_msg =
        b.AddField("Lcom/portage/runtimee2e/UiRunnable;", "msg", "Ljava/lang/String;");
    const uint32_t f_bg_msg =
        b.AddField("Lcom/portage/runtimee2e/BgThread;", "msg", "Ljava/lang/String;");

    const uint32_t m_obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    const uint32_t m_act_init = b.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    const uint32_t m_thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {});
    const uint32_t m_thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    const uint32_t m_thr_sleep = b.AddMethod("Ljava/lang/Thread;", "sleep", "V", {"J"});

    const uint32_t m_sb_init =
        b.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", {"Ljava/lang/String;"});
    const uint32_t m_sb_app_s = b.AddMethod("Ljava/lang/StringBuilder;", "append",
                                            "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});
    const uint32_t m_sb_app_i =
        b.AddMethod("Ljava/lang/StringBuilder;", "append", "Ljava/lang/StringBuilder;", {"I"});
    const uint32_t m_sb_str =
        b.AddMethod("Ljava/lang/StringBuilder;", "toString", "Ljava/lang/String;", {});

    const uint32_t m_exec_fixed =
        b.AddMethod("Ljava/util/concurrent/Executors;", "newFixedThreadPool",
                    "Ljava/util/concurrent/ExecutorService;", {"I"});
    const uint32_t m_exec_submit =
        b.AddMethod("Ljava/util/concurrent/ExecutorService;", "submit",
                    "Ljava/util/concurrent/Future;", {"Ljava/util/concurrent/Callable;"});
    const uint32_t m_exec_shutdown =
        b.AddMethod("Ljava/util/concurrent/ExecutorService;", "shutdown", "V", {});
    const uint32_t m_fut_get =
        b.AddMethod("Ljava/util/concurrent/Future;", "get", "Ljava/lang/Object;", {});

    const uint32_t m_map_init = b.AddMethod("Ljava/util/HashMap;", "<init>", "V", {});
    const uint32_t m_map_put = b.AddMethod("Ljava/util/HashMap;", "put", "Ljava/lang/Object;",
                                           {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    const uint32_t m_map_get =
        b.AddMethod("Ljava/util/HashMap;", "get", "Ljava/lang/Object;", {"Ljava/lang/Object;"});

    const uint32_t m_int_val =
        b.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    const uint32_t m_str_fmt = b.AddMethod("Ljava/lang/String;", "format", "Ljava/lang/String;",
                                           {"Ljava/lang/String;", "[Ljava/lang/Object;"});
    const uint32_t m_log_i =
        b.AddMethod("Landroid/util/Log;", "i", "I", {"Ljava/lang/String;", "Ljava/lang/String;"});
    const uint32_t m_looper_main =
        b.AddMethod("Landroid/os/Looper;", "getMainLooper", "Landroid/os/Looper;", {});
    const uint32_t m_handler_init =
        b.AddMethod("Landroid/os/Handler;", "<init>", "V", {"Landroid/os/Looper;"});
    const uint32_t m_handler_post =
        b.AddMethod("Landroid/os/Handler;", "post", "Z", {"Ljava/lang/Runnable;"});

    const uint32_t m_wk_init = b.AddMethod("Lcom/portage/runtimee2e/WorkerCallable;", "<init>", "V",
                                           {"Ljava/lang/String;", "I"});
    const uint32_t m_wk_call =
        b.AddMethod("Lcom/portage/runtimee2e/WorkerCallable;", "call", "Ljava/lang/Object;", {});
    const uint32_t m_ui_init =
        b.AddMethod("Lcom/portage/runtimee2e/UiRunnable;", "<init>", "V", {"Ljava/lang/String;"});
    const uint32_t m_ui_run = b.AddMethod("Lcom/portage/runtimee2e/UiRunnable;", "run", "V", {});
    const uint32_t m_bg_init =
        b.AddMethod("Lcom/portage/runtimee2e/BgThread;", "<init>", "V", {"Ljava/lang/String;"});
    const uint32_t m_bg_run = b.AddMethod("Lcom/portage/runtimee2e/BgThread;", "run", "V", {});
    const uint32_t m_ma_init =
        b.AddMethod("Lcom/portage/runtimee2e/MainActivity;", "<init>", "V", {});
    const uint32_t m_ma_on_create = b.AddMethod("Lcom/portage/runtimee2e/MainActivity;", "onCreate",
                                                "V", {"Landroid/os/Bundle;"});

    {
        auto &wc = b.AddClass("Lcom/portage/runtimee2e/WorkerCallable;", "Ljava/lang/Object;",
                              {"Ljava/util/concurrent/Callable;"});
        wc.instance_fields.push_back({f_wk_prefix, ACC_PUBLIC});
        wc.instance_fields.push_back({f_wk_num, ACC_PUBLIC});

        std::vector<uint16_t> init_code;
        RuntimeIntegDexBuilder::Emit35c(init_code, OP_INVOKE_DIRECT,
                                        static_cast<uint16_t>(m_obj_init), {1});
        RuntimeIntegDexBuilder::Emit22c(init_code, OP_IPUT_OBJECT, 2, 1,
                                        static_cast<uint16_t>(f_wk_prefix));
        RuntimeIntegDexBuilder::Emit22c(init_code, OP_IPUT, 3, 1, static_cast<uint16_t>(f_wk_num));
        init_code.push_back(OP_RETURN_VOID);
        wc.direct_methods.push_back(
            {wc.class_idx, m_wk_init, ACC_PUBLIC | ACC_CONSTRUCTOR, 4, 3, 1, init_code});

        std::vector<uint16_t> call_code;
        RuntimeIntegDexBuilder::Emit21c(call_code, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_sb));
        RuntimeIntegDexBuilder::Emit22c(call_code, OP_IGET_OBJECT, 1, 3,
                                        static_cast<uint16_t>(f_wk_prefix));
        RuntimeIntegDexBuilder::Emit35c(call_code, OP_INVOKE_DIRECT,
                                        static_cast<uint16_t>(m_sb_init), {0, 1});
        RuntimeIntegDexBuilder::Emit21c(call_code, OP_CONST_STRING, 1,
                                        static_cast<uint16_t>(s_colon));
        RuntimeIntegDexBuilder::Emit35c(call_code, OP_INVOKE_VIRTUAL,
                                        static_cast<uint16_t>(m_sb_app_s), {0, 1});
        RuntimeIntegDexBuilder::Emit22c(call_code, OP_IGET, 1, 3, static_cast<uint16_t>(f_wk_num));
        RuntimeIntegDexBuilder::Emit35c(call_code, OP_INVOKE_VIRTUAL,
                                        static_cast<uint16_t>(m_sb_app_i), {0, 1});
        RuntimeIntegDexBuilder::Emit35c(call_code, OP_INVOKE_VIRTUAL,
                                        static_cast<uint16_t>(m_sb_str), {0});
        call_code.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 0));
        call_code.push_back(RuntimeIntegDexBuilder::Op11x(OP_RETURN_OBJECT, 0));
        wc.virtual_methods.push_back({wc.class_idx, m_wk_call, ACC_PUBLIC, 4, 1, 2, call_code});
    }

    {
        auto &ui = b.AddClass("Lcom/portage/runtimee2e/UiRunnable;", "Ljava/lang/Object;",
                              {"Ljava/lang/Runnable;"});
        ui.instance_fields.push_back({f_ui_msg, ACC_PUBLIC});

        std::vector<uint16_t> init_code;
        RuntimeIntegDexBuilder::Emit35c(init_code, OP_INVOKE_DIRECT,
                                        static_cast<uint16_t>(m_obj_init), {1});
        RuntimeIntegDexBuilder::Emit22c(init_code, OP_IPUT_OBJECT, 2, 1,
                                        static_cast<uint16_t>(f_ui_msg));
        init_code.push_back(OP_RETURN_VOID);
        ui.direct_methods.push_back(
            {ui.class_idx, m_ui_init, ACC_PUBLIC | ACC_CONSTRUCTOR, 3, 2, 1, init_code});

        std::vector<uint16_t> run_code;
        RuntimeIntegDexBuilder::Emit21c(run_code, OP_CONST_STRING, 0, static_cast<uint16_t>(s_tag));
        RuntimeIntegDexBuilder::Emit22c(run_code, OP_IGET_OBJECT, 1, 2,
                                        static_cast<uint16_t>(f_ui_msg));
        RuntimeIntegDexBuilder::Emit35c(run_code, OP_INVOKE_STATIC, static_cast<uint16_t>(m_log_i),
                                        {0, 1});
        run_code.push_back(OP_RETURN_VOID);
        ui.virtual_methods.push_back({ui.class_idx, m_ui_run, ACC_PUBLIC, 3, 1, 2, run_code});
    }

    {
        auto &bg = b.AddClass("Lcom/portage/runtimee2e/BgThread;", "Ljava/lang/Thread;");
        bg.instance_fields.push_back({f_bg_msg, ACC_PUBLIC});

        std::vector<uint16_t> init_code;
        RuntimeIntegDexBuilder::Emit35c(init_code, OP_INVOKE_DIRECT,
                                        static_cast<uint16_t>(m_thr_init), {1});
        RuntimeIntegDexBuilder::Emit22c(init_code, OP_IPUT_OBJECT, 2, 1,
                                        static_cast<uint16_t>(f_bg_msg));
        init_code.push_back(OP_RETURN_VOID);
        bg.direct_methods.push_back(
            {bg.class_idx, m_bg_init, ACC_PUBLIC | ACC_CONSTRUCTOR, 3, 2, 1, init_code});

        std::vector<uint16_t> run_code;
        RuntimeIntegDexBuilder::Emit21s(run_code, OP_CONST_WIDE_16, 0, 15);
        RuntimeIntegDexBuilder::Emit35c(run_code, OP_INVOKE_STATIC,
                                        static_cast<uint16_t>(m_thr_sleep), {0, 1});
        RuntimeIntegDexBuilder::Emit21c(run_code, OP_CONST_STRING, 0, static_cast<uint16_t>(s_tag));
        RuntimeIntegDexBuilder::Emit22c(run_code, OP_IGET_OBJECT, 1, 2,
                                        static_cast<uint16_t>(f_bg_msg));
        RuntimeIntegDexBuilder::Emit35c(run_code, OP_INVOKE_STATIC, static_cast<uint16_t>(m_log_i),
                                        {0, 1});
        run_code.push_back(OP_RETURN_VOID);
        bg.virtual_methods.push_back({bg.class_idx, m_bg_run, ACC_PUBLIC, 3, 1, 2, run_code});
    }

    {
        auto &ma = b.AddClass("Lcom/portage/runtimee2e/MainActivity;", "Landroid/app/Activity;");
        std::vector<uint16_t> init_code;
        RuntimeIntegDexBuilder::Emit35c(init_code, OP_INVOKE_DIRECT,
                                        static_cast<uint16_t>(m_act_init), {0});
        init_code.push_back(OP_RETURN_VOID);
        ma.direct_methods.push_back(
            {ma.class_idx, m_ma_init, ACC_PUBLIC | ACC_CONSTRUCTOR, 1, 1, 1, init_code});

        std::vector<uint16_t> oc;
        oc.push_back(static_cast<uint16_t>(OP_CONST_4 | ((1u | (2u << 4)) << 8)));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_STATIC, static_cast<uint16_t>(m_exec_fixed),
                                        {1});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 0));

        RuntimeIntegDexBuilder::Emit21c(oc, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_worker));
        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 2, static_cast<uint16_t>(s_prefix));
        RuntimeIntegDexBuilder::Emit21s(oc, OP_CONST_16, 3, 42);
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_wk_init),
                                        {1, 2, 3});
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_INTERFACE,
                                        static_cast<uint16_t>(m_exec_submit), {0, 1});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_fut_get),
                                        {1});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_INTERFACE,
                                        static_cast<uint16_t>(m_exec_shutdown), {0});

        RuntimeIntegDexBuilder::Emit21c(oc, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_map));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_map_init),
                                        {2});
        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 3, static_cast<uint16_t>(s_key));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_map_put),
                                        {2, 3, 1});
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_map_get),
                                        {2, 3});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));

        oc.push_back(static_cast<uint16_t>(OP_CONST_4 | ((2u | (2u << 4)) << 8)));
        RuntimeIntegDexBuilder::Emit22c(oc, OP_NEW_ARRAY, 4, 2, static_cast<uint16_t>(t_obj_arr));
        oc.push_back(static_cast<uint16_t>(OP_CONST_4 | ((5u | (0u << 4)) << 8)));
        RuntimeIntegDexBuilder::Emit23x(oc, OP_APUT_OBJECT, 1, 4, 5);
        oc.push_back(static_cast<uint16_t>(OP_CONST_4 | ((5u | (7u << 4)) << 8)));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_val),
                                        {5});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
        oc.push_back(static_cast<uint16_t>(OP_CONST_4 | ((5u | (1u << 4)) << 8)));
        RuntimeIntegDexBuilder::Emit23x(oc, OP_APUT_OBJECT, 1, 4, 5);

        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 1, static_cast<uint16_t>(s_fmt));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_STATIC, static_cast<uint16_t>(m_str_fmt),
                                        {1, 4});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 2, static_cast<uint16_t>(s_tag));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_STATIC, static_cast<uint16_t>(m_log_i),
                                        {2, 1});

        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_STATIC, static_cast<uint16_t>(m_looper_main),
                                        {});
        oc.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
        RuntimeIntegDexBuilder::Emit21c(oc, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_handler));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_handler_init),
                                        {2, 1});
        RuntimeIntegDexBuilder::Emit21c(oc, OP_NEW_INSTANCE, 3, static_cast<uint16_t>(t_ui_run));
        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 4,
                                        static_cast<uint16_t>(s_looper_msg));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_ui_init),
                                        {3, 4});
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_VIRTUAL,
                                        static_cast<uint16_t>(m_handler_post), {2, 3});

        RuntimeIntegDexBuilder::Emit21c(oc, OP_NEW_INSTANCE, 1, static_cast<uint16_t>(t_bg_thr));
        RuntimeIntegDexBuilder::Emit21c(oc, OP_CONST_STRING, 2, static_cast<uint16_t>(s_bg_msg));
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_bg_init),
                                        {1, 2});
        RuntimeIntegDexBuilder::Emit35c(oc, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_thr_start),
                                        {1});

        oc.push_back(OP_RETURN_VOID);
        ma.virtual_methods.push_back({ma.class_idx, m_ma_on_create, ACC_PUBLIC, 8, 2, 3, oc});
    }

    const stdfs::path apk_path = stdfs::temp_directory_path() / "portage_java_runtime_e2e.apk";
    auto manifest = BuildJavaManifest("com.portage.runtimee2e", ".MainActivity");
    auto dex = b.Build();
    ASSERT_TRUE(WriteZipApk(apk_path, {{"AndroidManifest.xml", manifest}, {"classes.dex", dex}}));

    ProcessResult check = RunPortageCommand("--check-apk \"" + apk_path.string() + "\"");
    EXPECT_EQ(check.exit_code, 0u) << check.output;

    ProcessResult run = RunPortageCommand("--headless run \"" + apk_path.string() + "\"");
    EXPECT_EQ(run.exit_code, 0u) << run.output;
    EXPECT_NE(run.output.find("item=ok:42 count=7"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("looper-done"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("bg-thread-done"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("APK Java activity execution completed successfully"),
              std::string::npos)
        << run.output;

    std::error_code ec;
    stdfs::remove(apk_path, ec);
}

TEST(JavaRuntimeExecution, InProcessTreeMapSynchronizedWorkerAndCleanup)
{
    JniEnvironment::Instance().Reset();
    DexVm::Instance().Reset();
    JniEnvironment::Instance().Initialize("com.portage.runtimee2e",
                                          "/data/data/com.portage.runtimee2e/files",
                                          "/data/data/com.portage.runtimee2e/cache");

    RuntimeIntegDexBuilder b;
    const uint32_t s_a = b.AddString("beta");
    const uint32_t s_b = b.AddString("alpha");
    const uint32_t s_sep = b.AddString(",");
    const uint32_t t_tree = b.AddType("Ljava/util/TreeMap;");
    const uint32_t t_list = b.AddType("Ljava/util/ArrayList;");

    const uint32_t m_obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    const uint32_t m_tree_init = b.AddMethod("Ljava/util/TreeMap;", "<init>", "V", {});
    const uint32_t m_tree_put = b.AddMethod("Ljava/util/TreeMap;", "put", "Ljava/lang/Object;",
                                            {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    const uint32_t m_tree_keys =
        b.AddMethod("Ljava/util/TreeMap;", "keySet", "Ljava/util/Set;", {});
    const uint32_t m_list_init =
        b.AddMethod("Ljava/util/ArrayList;", "<init>", "V", {"Ljava/util/Collection;"});
    const uint32_t m_str_join = b.AddMethod("Ljava/lang/String;", "join", "Ljava/lang/String;",
                                            {"Ljava/lang/CharSequence;", "Ljava/lang/Iterable;"});
    const uint32_t m_int_val =
        b.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    const uint32_t m_sorter_init =
        b.AddMethod("Lcom/portage/runtimee2e/Sorter;", "<init>", "V", {});
    const uint32_t m_run =
        b.AddMethod("Lcom/portage/runtimee2e/Sorter;", "runOrder", "Ljava/lang/String;", {});

    auto &cls = b.AddClass("Lcom/portage/runtimee2e/Sorter;", "Ljava/lang/Object;");
    cls.direct_methods.push_back({cls.class_idx,
                                  m_sorter_init,
                                  ACC_PUBLIC | ACC_CONSTRUCTOR,
                                  1,
                                  1,
                                  1,
                                  {static_cast<uint16_t>(OP_INVOKE_DIRECT | (0x10u << 8)),
                                   static_cast<uint16_t>(m_obj_init), 0, OP_RETURN_VOID}});

    std::vector<uint16_t> code;
    RuntimeIntegDexBuilder::Emit21c(code, OP_NEW_INSTANCE, 0, static_cast<uint16_t>(t_tree));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_tree_init),
                                    {0});
    RuntimeIntegDexBuilder::Emit21c(code, OP_CONST_STRING, 1, static_cast<uint16_t>(s_a));
    code.push_back(static_cast<uint16_t>(OP_CONST_4 | ((2u | (2u << 4)) << 8)));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_val), {2});
    code.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 2));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_tree_put),
                                    {0, 1, 2});

    RuntimeIntegDexBuilder::Emit21c(code, OP_CONST_STRING, 1, static_cast<uint16_t>(s_b));
    code.push_back(static_cast<uint16_t>(OP_CONST_4 | ((2u | (1u << 4)) << 8)));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_val), {2});
    code.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 2));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_tree_put),
                                    {0, 1, 2});

    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_tree_keys),
                                    {0});
    code.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 1));
    RuntimeIntegDexBuilder::Emit21c(code, OP_NEW_INSTANCE, 2, static_cast<uint16_t>(t_list));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_list_init),
                                    {2, 1});
    RuntimeIntegDexBuilder::Emit21c(code, OP_CONST_STRING, 3, static_cast<uint16_t>(s_sep));
    RuntimeIntegDexBuilder::Emit35c(code, OP_INVOKE_STATIC, static_cast<uint16_t>(m_str_join),
                                    {3, 2});
    code.push_back(RuntimeIntegDexBuilder::Op11x(OP_MOVE_RESULT_OBJECT, 0));
    code.push_back(RuntimeIntegDexBuilder::Op11x(OP_RETURN_OBJECT, 0));
    cls.direct_methods.push_back({cls.class_idx, m_run, ACC_PUBLIC | ACC_STATIC, 5, 0, 3, code});

    auto dex_bytes = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(dex_bytes));
    auto target = DexVm::Instance().ResolveMethod("com/portage/runtimee2e/Sorter", "runOrder",
                                                  "()Ljava/lang/String;", false);
    ASSERT_TRUE(target.has_value());

    DexValue res = DexVm::Instance().ExecuteMethod(target->dex, target->method, {},
                                                   JniEnvironment::Instance().GetJNIEnv());
    const char *utf = JniEnvironment::Instance().GetStringChars(reinterpret_cast<jstring>(res.raw));
    ASSERT_NE(utf, nullptr);
    EXPECT_STREQ(utf, "alpha,beta");

    DexVm::Instance().Reset();
    JniEnvironment::Instance().Reset();
}
