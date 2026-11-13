#include "elf/link_map.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "package/java_activity.hpp"
#include "package/jni_env.hpp"
#include "package/manifest_parser.hpp"
#include "package/package_manager.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>

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

inline void AppendSleb128(std::vector<uint8_t> &buf, int32_t val)
{
    bool more = true;
    while (more) {
        uint8_t b = static_cast<uint8_t>(val & 0x7Fu);
        val >>= 7;
        bool sign_bit = (b & 0x40u) != 0;
        if ((val == 0 && !sign_bit) || (val == -1 && sign_bit)) {
            more = false;
        } else {
            b |= 0x80u;
        }
        buf.push_back(b);
    }
}

inline void Align4(std::vector<uint8_t> &buf)
{
    while ((buf.size() & 3u) != 0) {
        buf.push_back(0);
    }
}

class MiniDexBuilder {
  public:
    struct TrySpec {
        uint32_t start_addr{0};
        uint16_t insn_count{0};
        std::vector<std::pair<std::string, uint32_t>> handlers;
        uint32_t catch_all_addr{DEX_NO_INDEX};
    };

    struct MethodSpec {
        uint32_t class_idx{0};
        uint32_t method_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
        uint16_t registers_size{0};
        uint16_t ins_size{0};
        uint16_t outs_size{0};
        std::vector<uint16_t> insns;
        std::vector<TrySpec> tries;
    };

    struct FieldSpec {
        uint32_t field_idx{0};
        uint32_t access_flags{ACC_PUBLIC};
    };

    struct ClassSpec {
        uint32_t class_idx{0};
        uint32_t superclass_idx{DEX_NO_INDEX};
        uint32_t access_flags{ACC_PUBLIC};
        std::vector<FieldSpec> static_fields;
        std::vector<FieldSpec> instance_fields;
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

    uint32_t AddField(const std::string &cls, const std::string &name, const std::string &type)
    {
        std::string key = cls + "->" + name + ":" + type;
        auto it = field_map_.find(key);
        if (it != field_map_.end()) {
            return it->second;
        }
        uint32_t c = AddType(cls);
        uint32_t t = AddType(type);
        uint32_t n = AddString(name);
        uint32_t idx = static_cast<uint32_t>(fields_.size());
        fields_.push_back({static_cast<uint16_t>(c), static_cast<uint16_t>(t), n});
        field_map_.emplace(std::move(key), idx);
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

    ClassSpec &AddClass(const std::string &desc,
                        const std::string &super_desc = "Ljava/lang/Object;")
    {
        ClassSpec spec;
        spec.class_idx = AddType(desc);
        if (!super_desc.empty()) {
            spec.superclass_idx = AddType(super_desc);
        }
        classes_.push_back(std::move(spec));
        return classes_.back();
    }

    std::vector<uint8_t> Build()
    {
        for (const auto &cls : classes_) {
            for (const auto &m : cls.direct_methods) {
                for (const auto &t : m.tries) {
                    for (const auto &[ex_desc, addr] : t.handlers) {
                        (void)addr;
                        (void)AddType(ex_desc);
                    }
                }
            }
            for (const auto &m : cls.virtual_methods) {
                for (const auto &t : m.tries) {
                    for (const auto &[ex_desc, addr] : t.handlers) {
                        (void)addr;
                        (void)AddType(ex_desc);
                    }
                }
            }
        }

        uint32_t header_sz = sizeof(DexHeader);
        uint32_t str_off = header_sz;
        uint32_t type_off = str_off + static_cast<uint32_t>(strings_.size() * 4);
        uint32_t proto_off = type_off + static_cast<uint32_t>(types_.size() * 4);
        uint32_t field_off = proto_off + static_cast<uint32_t>(protos_.size() * sizeof(DexProtoId));
        uint32_t method_off =
            field_off + static_cast<uint32_t>(fields_.size() * sizeof(DexFieldId));
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
            AppendU16(data_sec, static_cast<uint16_t>(m.tries.size()));
            AppendU32(data_sec, 0);
            AppendU32(data_sec, static_cast<uint32_t>(m.insns.size()));
            for (uint16_t w : m.insns) {
                AppendU16(data_sec, w);
            }
            if (!m.tries.empty()) {
                if ((m.insns.size() & 1u) != 0) {
                    AppendU16(data_sec, 0);
                }
                std::vector<uint8_t> handler_sec;
                AppendUleb128(handler_sec, static_cast<uint32_t>(m.tries.size()));
                for (const auto &t : m.tries) {
                    AppendU32(data_sec, t.start_addr);
                    AppendU16(data_sec, t.insn_count);
                    uint16_t h_off = static_cast<uint16_t>(handler_sec.size());
                    AppendU16(data_sec, h_off);
                    int32_t size_s = static_cast<int32_t>(t.handlers.size());
                    if (t.catch_all_addr != DEX_NO_INDEX) {
                        size_s = -size_s;
                    }
                    AppendSleb128(handler_sec, size_s);
                    for (const auto &[ex_desc, addr] : t.handlers) {
                        AppendUleb128(handler_sec, AddType(ex_desc));
                        AppendUleb128(handler_sec, addr);
                    }
                    if (t.catch_all_addr != DEX_NO_INDEX) {
                        AppendUleb128(handler_sec, t.catch_all_addr);
                    }
                }
                data_sec.insert(data_sec.end(), handler_sec.begin(), handler_sec.end());
            }
            return off;
        };

        std::vector<uint32_t> class_data_offs(classes_.size(), 0);
        for (size_t c = 0; c < classes_.size(); ++c) {
            auto &cls = classes_[c];
            std::vector<uint32_t> direct_code_offs;
            for (const auto &m : cls.direct_methods) {
                direct_code_offs.push_back(emit_code_item(m));
            }
            std::vector<uint32_t> virtual_code_offs;
            for (const auto &m : cls.virtual_methods) {
                virtual_code_offs.push_back(emit_code_item(m));
            }
            class_data_offs[c] = static_cast<uint32_t>(data_sec.size());
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.static_fields.size()));
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.instance_fields.size()));
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.direct_methods.size()));
            AppendUleb128(data_sec, static_cast<uint32_t>(cls.virtual_methods.size()));

            uint32_t prev = 0;
            for (const auto &f : cls.static_fields) {
                AppendUleb128(data_sec, f.field_idx - prev);
                AppendUleb128(data_sec, f.access_flags);
                prev = f.field_idx;
            }
            prev = 0;
            for (const auto &f : cls.instance_fields) {
                AppendUleb128(data_sec, f.field_idx - prev);
                AppendUleb128(data_sec, f.access_flags);
                prev = f.field_idx;
            }
            prev = 0;
            for (size_t i = 0; i < cls.direct_methods.size(); ++i) {
                AppendUleb128(data_sec, cls.direct_methods[i].method_idx - prev);
                AppendUleb128(data_sec, cls.direct_methods[i].access_flags);
                AppendUleb128(data_sec, direct_code_offs[i] ? (data_off + direct_code_offs[i]) : 0);
                prev = cls.direct_methods[i].method_idx;
            }
            prev = 0;
            for (size_t i = 0; i < cls.virtual_methods.size(); ++i) {
                AppendUleb128(data_sec, cls.virtual_methods[i].method_idx - prev);
                AppendUleb128(data_sec, cls.virtual_methods[i].access_flags);
                AppendUleb128(data_sec,
                              virtual_code_offs[i] ? (data_off + virtual_code_offs[i]) : 0);
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
        hdr.field_ids_size = static_cast<uint32_t>(fields_.size());
        hdr.field_ids_off = fields_.empty() ? 0 : field_off;
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
        for (size_t i = 0; i < fields_.size(); ++i) {
            std::memcpy(out.data() + field_off + i * sizeof(DexFieldId), &fields_[i],
                        sizeof(DexFieldId));
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
    std::vector<DexFieldId> fields_;
    std::map<std::string, uint32_t> field_map_;
    std::vector<DexMethodId> methods_;
    std::map<std::string, uint32_t> method_map_;
    std::vector<ClassSpec> classes_;
};

std::vector<uint8_t> BuildBinaryManifestWithIntentFilter(const std::string &pkg_name,
                                                         const std::string &app_name,
                                                         const std::string &first_act,
                                                         const std::string &launcher_act)
{
    std::vector<std::string> pool = {
        "manifest",
        "package",
        pkg_name,
        "uses-sdk",
        "minSdkVersion",
        "targetSdkVersion",
        "application",
        "name",
        app_name,
        "activity",
        first_act,
        launcher_act,
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
    push_start("application", {{"name", app_name}});
    push_start("activity", {{"name", first_act}});
    push_end("activity");
    push_start("activity", {{"name", launcher_act}});
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

class JavaActivityTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        DexVm::Instance().Reset();
        JniEnvironment::Instance().Reset();
    }
    void TearDown() override
    {
        DexVm::Instance().Reset();
        JniEnvironment::Instance().Reset();
    }
};

} // namespace

TEST_F(JavaActivityTest, ResolvesMainLauncherActivityAndApplicationFromManifest)
{
    auto raw = BuildBinaryManifestWithIntentFilter("com.portage.sample", ".MyApplication",
                                                   ".SettingsActivity", ".MainActivity");
    auto parsed = ManifestParser::Parse(raw);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->package_name, "com.portage.sample");
    EXPECT_EQ(parsed->application_name, "com.portage.sample.MyApplication");
    EXPECT_EQ(parsed->main_activity_name, "com.portage.sample.SettingsActivity");
    EXPECT_EQ(parsed->launcher_activity_name, "com.portage.sample.MainActivity");
}

TEST_F(JavaActivityTest, ExecutesApplicationAndActivityLifecycleOrder)
{
    MiniDexBuilder b;
    uint32_t f_trace = b.AddField("Lcom/test/App;", "trace", "I");
    uint32_t m_app_init = b.AddMethod("Lcom/test/App;", "<init>", "V", {});
    uint32_t m_app_super = b.AddMethod("Landroid/app/Application;", "<init>", "V", {});
    uint32_t m_app_on_create = b.AddMethod("Lcom/test/App;", "onCreate", "V", {});

    uint32_t m_act_init = b.AddMethod("Lcom/test/MainActivity;", "<init>", "V", {});
    uint32_t m_act_super = b.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    uint32_t m_act_create =
        b.AddMethod("Lcom/test/MainActivity;", "onCreate", "V", {"Landroid/os/Bundle;"});
    uint32_t m_act_start = b.AddMethod("Lcom/test/MainActivity;", "onStart", "V", {});
    uint32_t m_act_resume = b.AddMethod("Lcom/test/MainActivity;", "onResume", "V", {});
    uint32_t m_act_pause = b.AddMethod("Lcom/test/MainActivity;", "onPause", "V", {});
    uint32_t m_act_stop = b.AddMethod("Lcom/test/MainActivity;", "onStop", "V", {});
    uint32_t m_act_destroy = b.AddMethod("Lcom/test/MainActivity;", "onDestroy", "V", {});

    auto make_step = [&](uint8_t step) {
        return std::vector<uint16_t>{static_cast<uint16_t>(0x0060u),
                                     static_cast<uint16_t>(f_trace),
                                     static_cast<uint16_t>(0x00D8u),
                                     static_cast<uint16_t>((step << 8) | 0u),
                                     static_cast<uint16_t>(0x0067u),
                                     static_cast<uint16_t>(f_trace),
                                     0x000Eu};
    };

    auto &c_app = b.AddClass("Lcom/test/App;", "Landroid/app/Application;");
    c_app.static_fields.push_back({f_trace, ACC_PUBLIC | ACC_STATIC});
    c_app.direct_methods.push_back({c_app.class_idx,
                                    m_app_init,
                                    ACC_PUBLIC | ACC_CONSTRUCTOR,
                                    1,
                                    1,
                                    1,
                                    {0x1070u, static_cast<uint16_t>(m_app_super), 0x0000u, 0x000Eu},
                                    {}});
    c_app.virtual_methods.push_back(
        {c_app.class_idx, m_app_on_create, ACC_PUBLIC, 1, 1, 0, make_step(1), {}});

    auto &c_act = b.AddClass("Lcom/test/MainActivity;", "Landroid/app/Activity;");
    c_act.direct_methods.push_back({c_act.class_idx,
                                    m_act_init,
                                    ACC_PUBLIC | ACC_CONSTRUCTOR,
                                    1,
                                    1,
                                    1,
                                    {0x1070u, static_cast<uint16_t>(m_act_super), 0x0000u, 0x000Eu},
                                    {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_create, ACC_PUBLIC, 2, 2, 0, make_step(2), {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_start, ACC_PUBLIC, 1, 1, 0, make_step(4), {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_resume, ACC_PUBLIC, 1, 1, 0, make_step(8), {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_pause, ACC_PUBLIC, 1, 1, 0, make_step(16), {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_stop, ACC_PUBLIC, 1, 1, 0, make_step(32), {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx, m_act_destroy, ACC_PUBLIC, 1, 1, 0, make_step(64), {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));

    PackageInfo pkg{};
    pkg.manifest.package_name = "com.test";
    pkg.manifest.application_name = "com.test.App";
    pkg.manifest.launcher_activity_name = "com.test.MainActivity";

    ANativeWindow win{800, 600, 1, 1, nullptr};
    ASSERT_TRUE(JavaActivityRunner::Instance().Setup(pkg, &win));
    ASSERT_TRUE(JavaActivityRunner::Instance().LaunchAndRun(true));

    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/App;", "trace"), 127u);
}

TEST_F(JavaActivityTest, SystemLoadLibraryLoadsVirtualAndHandlesMissing)
{
    MiniDexBuilder b;
    uint32_t s_ok = b.AddString("android");
    uint32_t s_missing = b.AddString("nonexistent_portage_lib_xyz");
    uint32_t m_load = b.AddMethod("Ljava/lang/System;", "loadLibrary", "V", {"Ljava/lang/String;"});
    uint32_t m_test = b.AddMethod("Lcom/test/LibTest;", "loadBoth", "I", {});

    auto &cls = b.AddClass("Lcom/test/LibTest;");
    MiniDexBuilder::TrySpec t{};
    t.start_addr = 4;
    t.insn_count = 4;
    t.handlers.emplace_back("Ljava/lang/UnsatisfiedLinkError;", 10);
    cls.direct_methods.push_back(
        {cls.class_idx,
         m_test,
         ACC_PUBLIC | ACC_STATIC,
         2,
         0,
         1,
         {0x001Au, static_cast<uint16_t>(s_ok), 0x1071u, static_cast<uint16_t>(m_load), 0x0000u,
          0x001Au, static_cast<uint16_t>(s_missing), 0x1071u, static_cast<uint16_t>(m_load),
          0x0000u, 0x0012u, 0x010Fu, 0x000Du, 0x7012u, 0x000Fu},
         {t}});
    // Notice insns layout: 0:1a, 1:s_ok, 2:71, 3:m_load, 4:0000, 5:1a... wait: let's keep try
    // exact!
    cls.direct_methods.back().insns = {0x001Au,
                                       static_cast<uint16_t>(s_ok),
                                       0x1071u,
                                       static_cast<uint16_t>(m_load),
                                       0x0000u,
                                       0x001Au,
                                       static_cast<uint16_t>(s_missing),
                                       0x1071u,
                                       static_cast<uint16_t>(m_load),
                                       0x0000u,
                                       0x0012u,
                                       0x000Fu,
                                       0x000Du,
                                       0x7012u,
                                       0x000Fu};
    cls.direct_methods.back().tries[0].start_addr = 5;
    cls.direct_methods.back().tries[0].insn_count = 5;
    cls.direct_methods.back().tries[0].handlers[0].second = 12;

    portage::elf::LinkMap::Instance().Reset();
    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/test/LibTest;", "loadBoth", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 7);
}

TEST_F(JavaActivityTest, HandlerAndLooperDispatchRunnablesAndMessages)
{
    MiniDexBuilder b;
    uint32_t f_count = b.AddField("Lcom/test/Work;", "count", "I");
    uint32_t m_r_init = b.AddMethod("Lcom/test/Work;", "<init>", "V", {});
    uint32_t m_obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t m_r_run = b.AddMethod("Lcom/test/Work;", "run", "V", {});

    uint32_t m_h_init = b.AddMethod("Lcom/test/MyHandler;", "<init>", "V", {"Landroid/os/Looper;"});
    uint32_t m_hs_init =
        b.AddMethod("Landroid/os/Handler;", "<init>", "V", {"Landroid/os/Looper;"});
    uint32_t m_h_handle =
        b.AddMethod("Lcom/test/MyHandler;", "handleMessage", "V", {"Landroid/os/Message;"});

    uint32_t m_get_looper =
        b.AddMethod("Landroid/os/Looper;", "getMainLooper", "Landroid/os/Looper;", {});
    uint32_t m_post = b.AddMethod("Landroid/os/Handler;", "post", "Z", {"Ljava/lang/Runnable;"});
    uint32_t m_post_delayed =
        b.AddMethod("Landroid/os/Handler;", "postDelayed", "Z", {"Ljava/lang/Runnable;", "J"});
    uint32_t m_send_empty = b.AddMethod("Landroid/os/Handler;", "sendEmptyMessage", "Z", {"I"});
    uint32_t m_driver = b.AddMethod("Lcom/test/Work;", "drive", "I", {});

    uint32_t t_work = b.AddType("Lcom/test/Work;");
    uint32_t t_handler = b.AddType("Lcom/test/MyHandler;");

    auto &c_work = b.AddClass("Lcom/test/Work;");
    c_work.static_fields.push_back({f_count, ACC_PUBLIC | ACC_STATIC});
    c_work.direct_methods.push_back({c_work.class_idx,
                                     m_r_init,
                                     ACC_PUBLIC | ACC_CONSTRUCTOR,
                                     1,
                                     1,
                                     1,
                                     {0x1070u, static_cast<uint16_t>(m_obj_init), 0x0000u, 0x000Eu},
                                     {}});
    c_work.direct_methods.push_back({c_work.class_idx,
                                     m_driver,
                                     ACC_PUBLIC | ACC_STATIC,
                                     5,
                                     0,
                                     4,
                                     {0x0071u,
                                      static_cast<uint16_t>(m_get_looper),
                                      0x0000u,
                                      0x000Cu,
                                      0x0122u,
                                      static_cast<uint16_t>(t_handler),
                                      0x2070u,
                                      static_cast<uint16_t>(m_h_init),
                                      0x0001u,
                                      0x0222u,
                                      static_cast<uint16_t>(t_work),
                                      0x1070u,
                                      static_cast<uint16_t>(m_r_init),
                                      0x0002u,
                                      0x206Eu,
                                      static_cast<uint16_t>(m_post),
                                      0x0021u,
                                      0x0313u,
                                      5,
                                      0x0412u,
                                      0x406Eu,
                                      static_cast<uint16_t>(m_post_delayed),
                                      0x4321u,
                                      0x0312u,
                                      0x206Eu,
                                      static_cast<uint16_t>(m_send_empty),
                                      0x0031u,
                                      0x0060u,
                                      static_cast<uint16_t>(f_count),
                                      0x000Fu},
                                     {}});
    c_work.virtual_methods.push_back({c_work.class_idx,
                                      m_r_run,
                                      ACC_PUBLIC,
                                      1,
                                      1,
                                      0,
                                      {0x0060u, static_cast<uint16_t>(f_count), 0x00D8u, 0x0100u,
                                       0x0067u, static_cast<uint16_t>(f_count), 0x000Eu},
                                      {}});

    auto &c_handler = b.AddClass("Lcom/test/MyHandler;", "Landroid/os/Handler;");
    c_handler.direct_methods.push_back(
        {c_handler.class_idx,
         m_h_init,
         ACC_PUBLIC | ACC_CONSTRUCTOR,
         2,
         2,
         2,
         {0x2070u, static_cast<uint16_t>(m_hs_init), 0x0100u, 0x000Eu},
         {}});
    c_handler.virtual_methods.push_back({c_handler.class_idx,
                                         m_h_handle,
                                         ACC_PUBLIC,
                                         2,
                                         2,
                                         0,
                                         {0x0060u, static_cast<uint16_t>(f_count), 0x00D8u, 0x0A00u,
                                          0x0067u, static_cast<uint16_t>(f_count), 0x000Eu},
                                         {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/test/Work;", "drive", "()I");
    ASSERT_TRUE(resolved.has_value());
    (void)DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});

    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/Work;", "count"), 0u);
    size_t drained = DexVm::Instance().DrainMainLooper(16);
    EXPECT_EQ(drained, 3u);
    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/Work;", "count"), 12u);
}

TEST_F(JavaActivityTest, SurfaceViewCallbackAndNativeWindowFromSurface)
{
    MiniDexBuilder b;
    uint32_t f_w = b.AddField("Lcom/test/SurfAct;", "lastW", "I");
    uint32_t f_h = b.AddField("Lcom/test/SurfAct;", "lastH", "I");
    uint32_t f_destroyed = b.AddField("Lcom/test/SurfAct;", "destroyed", "I");

    uint32_t m_init = b.AddMethod("Lcom/test/SurfAct;", "<init>", "V", {});
    uint32_t m_super_init = b.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    uint32_t m_on_create =
        b.AddMethod("Lcom/test/SurfAct;", "onCreate", "V", {"Landroid/os/Bundle;"});
    uint32_t m_sv_init =
        b.AddMethod("Landroid/view/SurfaceView;", "<init>", "V", {"Landroid/content/Context;"});
    uint32_t m_set_cv =
        b.AddMethod("Landroid/app/Activity;", "setContentView", "V", {"Landroid/view/View;"});
    uint32_t m_get_holder =
        b.AddMethod("Landroid/view/SurfaceView;", "getHolder", "Landroid/view/SurfaceHolder;", {});
    uint32_t m_add_cb = b.AddMethod("Landroid/view/SurfaceHolder;", "addCallback", "V",
                                    {"Landroid/view/SurfaceHolder$Callback;"});
    uint32_t m_surf_created =
        b.AddMethod("Lcom/test/SurfAct;", "surfaceCreated", "V", {"Landroid/view/SurfaceHolder;"});
    uint32_t m_surf_changed = b.AddMethod("Lcom/test/SurfAct;", "surfaceChanged", "V",
                                          {"Landroid/view/SurfaceHolder;", "I", "I", "I"});
    uint32_t m_surf_destroyed = b.AddMethod("Lcom/test/SurfAct;", "surfaceDestroyed", "V",
                                            {"Landroid/view/SurfaceHolder;"});
    uint32_t m_get_surf =
        b.AddMethod("Landroid/view/SurfaceHolder;", "getSurface", "Landroid/view/Surface;", {});

    uint32_t t_sv = b.AddType("Landroid/view/SurfaceView;");
    auto &c_act = b.AddClass("Lcom/test/SurfAct;", "Landroid/app/Activity;");
    c_act.static_fields.push_back({f_w, ACC_PUBLIC | ACC_STATIC});
    c_act.static_fields.push_back({f_h, ACC_PUBLIC | ACC_STATIC});
    c_act.static_fields.push_back({f_destroyed, ACC_PUBLIC | ACC_STATIC});

    c_act.direct_methods.push_back(
        {c_act.class_idx,
         m_init,
         ACC_PUBLIC | ACC_CONSTRUCTOR,
         1,
         1,
         1,
         {0x1070u, static_cast<uint16_t>(m_super_init), 0x0000u, 0x000Eu},
         {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx,
         m_on_create,
         ACC_PUBLIC,
         4,
         2,
         2,
         {0x0022u, static_cast<uint16_t>(t_sv), 0x2070u, static_cast<uint16_t>(m_sv_init), 0x0020u,
          0x206Eu, static_cast<uint16_t>(m_set_cv), 0x0002u, 0x106Eu,
          static_cast<uint16_t>(m_get_holder), 0x0000u, 0x010Cu, 0x2072u,
          static_cast<uint16_t>(m_add_cb), 0x0021u, 0x000Eu},
         {}});

    c_act.virtual_methods.push_back(
        {c_act.class_idx,
         m_surf_created,
         ACC_PUBLIC,
         3,
         2,
         1,
         {0x1072u, static_cast<uint16_t>(m_get_surf), 0x0002u, 0x000Cu, 0x000Eu},
         {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx,
         m_surf_changed,
         ACC_PUBLIC,
         5,
         5,
         0,
         {0x0367u, static_cast<uint16_t>(f_w), 0x0467u, static_cast<uint16_t>(f_h), 0x000Eu},
         {}});
    c_act.virtual_methods.push_back(
        {c_act.class_idx,
         m_surf_destroyed,
         ACC_PUBLIC,
         2,
         2,
         0,
         {0x1012u, 0x0067u, static_cast<uint16_t>(f_destroyed), 0x000Eu},
         {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));

    PackageInfo pkg{};
    pkg.manifest.package_name = "com.test";
    pkg.manifest.launcher_activity_name = "com.test.SurfAct";
    ANativeWindow win{1024, 768, 1, 1, nullptr};

    ASSERT_TRUE(JavaActivityRunner::Instance().Setup(pkg, &win));
    ASSERT_TRUE(JavaActivityRunner::Instance().LaunchAndRun(true));

    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/SurfAct;", "lastW"), 1024u);
    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/SurfAct;", "lastH"), 768u);
    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/SurfAct;", "destroyed"), 1u);

    jobject act = JavaActivityRunner::Instance().GetActivityObject();
    auto act_syn = JniEnvironment::Instance().LookupObject(act);
    ASSERT_NE(act_syn, nullptr);
    jobject sv = act_syn->object_fields["contentView"];
    auto sv_syn = JniEnvironment::Instance().LookupObject(sv);
    ASSERT_NE(sv_syn, nullptr);
    jobject holder = sv_syn->object_fields["holder"];
    auto holder_syn = JniEnvironment::Instance().LookupObject(holder);
    ASSERT_NE(holder_syn, nullptr);
    jobject surf = holder_syn->object_fields["surface"];
    ANativeWindow *acquired =
        ANativeWindow_fromSurface(JniEnvironment::Instance().GetJNIEnv(), surf);
    EXPECT_EQ(acquired, &win);
    ANativeWindow_release(acquired);
}

TEST_F(JavaActivityTest, CanvasSoftwareDrawBlitsIntoNativeWindow)
{
    MiniDexBuilder b;
    uint32_t m_draw = b.AddMethod("Lcom/test/CanvasTest;", "paint", "V", {});
    uint32_t m_sv_init =
        b.AddMethod("Landroid/view/SurfaceView;", "<init>", "V", {"Landroid/content/Context;"});
    uint32_t m_get_holder =
        b.AddMethod("Landroid/view/SurfaceView;", "getHolder", "Landroid/view/SurfaceHolder;", {});
    uint32_t m_lock =
        b.AddMethod("Landroid/view/SurfaceHolder;", "lockCanvas", "Landroid/graphics/Canvas;", {});
    uint32_t m_draw_color = b.AddMethod("Landroid/graphics/Canvas;", "drawColor", "V", {"I"});
    uint32_t m_unlock = b.AddMethod("Landroid/view/SurfaceHolder;", "unlockCanvasAndPost", "V",
                                    {"Landroid/graphics/Canvas;"});

    uint32_t t_sv = b.AddType("Landroid/view/SurfaceView;");
    auto &cls = b.AddClass("Lcom/test/CanvasTest;");
    cls.direct_methods.push_back({cls.class_idx,
                                  m_draw,
                                  ACC_PUBLIC | ACC_STATIC,
                                  4,
                                  0,
                                  2,
                                  {0x0022u,
                                   static_cast<uint16_t>(t_sv),
                                   0x1070u,
                                   static_cast<uint16_t>(m_sv_init),
                                   0x0000u,
                                   0x106Eu,
                                   static_cast<uint16_t>(m_get_holder),
                                   0x0000u,
                                   0x000Cu,
                                   0x1072u,
                                   static_cast<uint16_t>(m_lock),
                                   0x0000u,
                                   0x010Cu,
                                   0x0214u,
                                   0xFF00u,
                                   0xFF00u,
                                   0x206Eu,
                                   static_cast<uint16_t>(m_draw_color),
                                   0x0021u,
                                   0x2072u,
                                   static_cast<uint16_t>(m_unlock),
                                   0x0010u,
                                   0x000Eu},
                                  {}});

    ANativeWindow win{64, 64, 1, 1, nullptr};
    JniEnvironment::Instance().SetActiveNativeWindow(&win);
    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/test/CanvasTest;", "paint", "()V");
    ASSERT_TRUE(resolved.has_value());
    (void)DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});

    ASSERT_EQ(win.backbuffer.size(), static_cast<size_t>(64 * 64 * 4));
    EXPECT_EQ(win.backbuffer[0], 0x00);
    EXPECT_EQ(win.backbuffer[1], 0xFF);
    EXPECT_EQ(win.backbuffer[2], 0x00);
    EXPECT_EQ(win.backbuffer[3], 0xFF);
}

TEST_F(JavaActivityTest, IntentAndBundleExtrasRoundTrip)
{
    MiniDexBuilder b;
    uint32_t s_k = b.AddString("level");
    uint32_t s_name_k = b.AddString("tag");
    uint32_t s_name_v = b.AddString("portage");
    uint32_t m_test = b.AddMethod("Lcom/test/IntentTest;", "roundTrip", "I", {});
    uint32_t m_i_init = b.AddMethod("Landroid/content/Intent;", "<init>", "V", {});
    uint32_t m_put_i = b.AddMethod("Landroid/content/Intent;", "putExtra",
                                   "Landroid/content/Intent;", {"Ljava/lang/String;", "I"});
    uint32_t m_put_s =
        b.AddMethod("Landroid/content/Intent;", "putExtra", "Landroid/content/Intent;",
                    {"Ljava/lang/String;", "Ljava/lang/String;"});
    uint32_t m_get_i =
        b.AddMethod("Landroid/content/Intent;", "getIntExtra", "I", {"Ljava/lang/String;", "I"});
    uint32_t m_get_s = b.AddMethod("Landroid/content/Intent;", "getStringExtra",
                                   "Ljava/lang/String;", {"Ljava/lang/String;"});
    uint32_t m_len = b.AddMethod("Ljava/lang/String;", "length", "I", {});

    uint32_t t_intent = b.AddType("Landroid/content/Intent;");
    auto &cls = b.AddClass("Lcom/test/IntentTest;");
    cls.direct_methods.push_back({cls.class_idx,
                                  m_test,
                                  ACC_PUBLIC | ACC_STATIC,
                                  5,
                                  0,
                                  3,
                                  {0x0022u,
                                   static_cast<uint16_t>(t_intent),
                                   0x1070u,
                                   static_cast<uint16_t>(m_i_init),
                                   0x0000u,
                                   0x011Au,
                                   static_cast<uint16_t>(s_k),
                                   0x0213u,
                                   42,
                                   0x306Eu,
                                   static_cast<uint16_t>(m_put_i),
                                   0x0210u,
                                   0x011Au,
                                   static_cast<uint16_t>(s_name_k),
                                   0x021Au,
                                   static_cast<uint16_t>(s_name_v),
                                   0x306Eu,
                                   static_cast<uint16_t>(m_put_s),
                                   0x0210u,
                                   0x011Au,
                                   static_cast<uint16_t>(s_k),
                                   0x0212u,
                                   0x306Eu,
                                   static_cast<uint16_t>(m_get_i),
                                   0x0210u,
                                   0x030Au,
                                   0x011Au,
                                   static_cast<uint16_t>(s_name_k),
                                   0x206Eu,
                                   static_cast<uint16_t>(m_get_s),
                                   0x0010u,
                                   0x040Cu,
                                   0x106Eu,
                                   static_cast<uint16_t>(m_len),
                                   0x0004u,
                                   0x040Au,
                                   0x0403u,
                                   0x90u | 0x03u,
                                   0x030Fu},
                                  {}});
    // Let's fix add-int v3, v3, v4 -> opcode 0x90: (v3<<8)|0x90, (v4<<8)|v3
    cls.direct_methods.back().insns[cls.direct_methods.back().insns.size() - 3] = 0x0390u;
    cls.direct_methods.back().insns[cls.direct_methods.back().insns.size() - 2] = 0x0403u;

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/test/IntentTest;", "roundTrip", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 42 + 7);
}

TEST_F(JavaActivityTest, ActivityFinishTerminatesLooperAndDispatchesDestroy)
{
    MiniDexBuilder b;
    uint32_t f_ui = b.AddField("Lcom/test/UiAct;", "uiRan", "I");
    uint32_t m_init = b.AddMethod("Lcom/test/UiAct;", "<init>", "V", {});
    uint32_t m_super_init = b.AddMethod("Landroid/app/Activity;", "<init>", "V", {});
    uint32_t m_on_create =
        b.AddMethod("Lcom/test/UiAct;", "onCreate", "V", {"Landroid/os/Bundle;"});
    uint32_t m_run_ui =
        b.AddMethod("Landroid/app/Activity;", "runOnUiThread", "V", {"Ljava/lang/Runnable;"});
    uint32_t m_finish = b.AddMethod("Landroid/app/Activity;", "finish", "V", {});
    uint32_t m_is_finishing = b.AddMethod("Landroid/app/Activity;", "isFinishing", "Z", {});
    uint32_t m_run = b.AddMethod("Lcom/test/UiAct;", "run", "V", {});

    auto &cls = b.AddClass("Lcom/test/UiAct;", "Landroid/app/Activity;");
    cls.static_fields.push_back({f_ui, ACC_PUBLIC | ACC_STATIC});
    cls.direct_methods.push_back({cls.class_idx,
                                  m_init,
                                  ACC_PUBLIC | ACC_CONSTRUCTOR,
                                  1,
                                  1,
                                  1,
                                  {0x1070u, static_cast<uint16_t>(m_super_init), 0x0000u, 0x000Eu},
                                  {}});
    cls.virtual_methods.push_back({cls.class_idx,
                                   m_on_create,
                                   ACC_PUBLIC,
                                   2,
                                   2,
                                   2,
                                   {0x206Eu, static_cast<uint16_t>(m_run_ui), 0x0000u, 0x000Eu},
                                   {}});
    cls.virtual_methods.push_back(
        {cls.class_idx,
         m_run,
         ACC_PUBLIC,
         2,
         1,
         1,
         {0x1012u, 0x0067u, static_cast<uint16_t>(f_ui), 0x106Eu, static_cast<uint16_t>(m_finish),
          0x0001u, 0x106Eu, static_cast<uint16_t>(m_is_finishing), 0x0001u, 0x000Au, 0x000Eu},
         {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    PackageInfo pkg{};
    pkg.manifest.package_name = "com.test";
    pkg.manifest.launcher_activity_name = "com.test.UiAct";
    ANativeWindow win{640, 480, 1, 1, nullptr};

    ASSERT_TRUE(JavaActivityRunner::Instance().Setup(pkg, &win));
    ASSERT_TRUE(JavaActivityRunner::Instance().LaunchAndRun(true));

    EXPECT_EQ(DexVm::Instance().GetStaticField("Lcom/test/UiAct;", "uiRan"), 1u);
    EXPECT_TRUE(DexVm::Instance().IsMainLooperQuit());
}
