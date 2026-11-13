#include "package/dex_file.hpp"
#include "package/dex_vm.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
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

class DexBuilder {
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

    uint32_t AddType(std::string desc)
    {
        auto it = type_map_.find(desc);
        if (it != type_map_.end()) {
            return it->second;
        }
        uint32_t sidx = AddString(desc);
        uint32_t idx = static_cast<uint32_t>(types_.size());
        types_.push_back(sidx);
        type_map_.emplace(std::move(desc), idx);
        return idx;
    }

    uint32_t AddProto(std::string shorty, std::string ret_type,
                      std::vector<std::string> param_types)
    {
        std::string key = shorty + "|" + ret_type + "|";
        for (const auto &p : param_types) {
            key += p;
        }
        auto it = proto_map_.find(key);
        if (it != proto_map_.end()) {
            return it->second;
        }
        uint32_t sidx = AddString(std::move(shorty));
        uint32_t ridx = AddType(std::move(ret_type));
        std::vector<uint16_t> pidxs;
        for (auto &p : param_types) {
            pidxs.push_back(static_cast<uint16_t>(AddType(std::move(p))));
        }
        uint32_t idx = static_cast<uint32_t>(protos_.size());
        protos_.push_back({sidx, ridx, std::move(pidxs)});
        proto_map_.emplace(std::move(key), idx);
        return idx;
    }

    uint32_t AddField(std::string cls, std::string name, std::string type_desc)
    {
        std::string key = cls + ":" + name + ":" + type_desc;
        auto it = field_map_.find(key);
        if (it != field_map_.end()) {
            return it->second;
        }
        uint16_t cidx = static_cast<uint16_t>(AddType(std::move(cls)));
        uint16_t tidx = static_cast<uint16_t>(AddType(std::move(type_desc)));
        uint32_t nidx = AddString(std::move(name));
        uint32_t idx = static_cast<uint32_t>(fields_.size());
        fields_.push_back({cidx, tidx, nidx});
        field_map_.emplace(std::move(key), idx);
        return idx;
    }

    uint32_t AddMethod(std::string cls, std::string name, std::string shorty, std::string ret_type,
                       std::vector<std::string> param_types)
    {
        std::string key = cls + ":" + name + ":" + ret_type + ":";
        for (const auto &p : param_types) {
            key += p;
        }
        auto it = method_map_.find(key);
        if (it != method_map_.end()) {
            return it->second;
        }
        uint16_t cidx = static_cast<uint16_t>(AddType(std::move(cls)));
        uint16_t pidx = static_cast<uint16_t>(
            AddProto(std::move(shorty), std::move(ret_type), std::move(param_types)));
        uint32_t nidx = AddString(std::move(name));
        uint32_t idx = static_cast<uint32_t>(methods_.size());
        methods_.push_back({cidx, pidx, nidx});
        method_map_.emplace(std::move(key), idx);
        return idx;
    }

    ClassSpec &AddClass(std::string desc, std::string super_desc = "Ljava/lang/Object;",
                        uint32_t flags = ACC_PUBLIC)
    {
        ClassSpec c;
        c.class_idx = AddType(std::move(desc));
        c.superclass_idx = super_desc.empty() ? DEX_NO_INDEX : AddType(std::move(super_desc));
        c.access_flags = flags;
        classes_.push_back(std::move(c));
        return classes_.back();
    }

    std::vector<uint8_t> Build()
    {
        for (auto &cls : classes_) {
            for (auto &dm : cls.direct_methods) {
                for (auto &tr : dm.tries) {
                    for (auto &h : tr.handlers) {
                        AddType(h.first);
                    }
                }
            }
            for (auto &vm : cls.virtual_methods) {
                for (auto &tr : vm.tries) {
                    for (auto &h : tr.handlers) {
                        AddType(h.first);
                    }
                }
            }
        }

        uint32_t off = sizeof(DexHeader);
        uint32_t str_ids_off = off;
        off += static_cast<uint32_t>(strings_.size()) * 4;
        uint32_t type_ids_off = off;
        off += static_cast<uint32_t>(types_.size()) * 4;
        uint32_t proto_ids_off = off;
        off += static_cast<uint32_t>(protos_.size()) * 12;
        uint32_t field_ids_off = off;
        off += static_cast<uint32_t>(fields_.size()) * 8;
        uint32_t method_ids_off = off;
        off += static_cast<uint32_t>(methods_.size()) * 8;
        uint32_t class_defs_off = off;
        off += static_cast<uint32_t>(classes_.size()) * 32;

        std::vector<uint8_t> buf(off, 0);

        std::vector<uint32_t> str_data_offs(strings_.size(), 0);
        for (size_t i = 0; i < strings_.size(); ++i) {
            str_data_offs[i] = static_cast<uint32_t>(buf.size());
            AppendUleb128(buf, static_cast<uint32_t>(strings_[i].size()));
            for (char ch : strings_[i]) {
                buf.push_back(static_cast<uint8_t>(ch));
            }
            buf.push_back(0);
        }
        Align4(buf);

        std::vector<uint32_t> proto_param_offs(protos_.size(), 0);
        for (size_t i = 0; i < protos_.size(); ++i) {
            if (!protos_[i].params.empty()) {
                Align4(buf);
                proto_param_offs[i] = static_cast<uint32_t>(buf.size());
                AppendU32(buf, static_cast<uint32_t>(protos_[i].params.size()));
                for (uint16_t tidx : protos_[i].params) {
                    AppendU16(buf, tidx);
                }
            }
        }
        Align4(buf);

        auto emit_code_item = [&](const MethodSpec &ms) -> uint32_t {
            if (ms.insns.empty()) {
                return 0;
            }
            Align4(buf);
            uint32_t code_off = static_cast<uint32_t>(buf.size());
            AppendU16(buf, ms.registers_size);
            AppendU16(buf, ms.ins_size);
            AppendU16(buf, ms.outs_size);
            AppendU16(buf, static_cast<uint16_t>(ms.tries.size()));
            AppendU32(buf, 0);
            AppendU32(buf, static_cast<uint32_t>(ms.insns.size()));
            for (uint16_t insn : ms.insns) {
                AppendU16(buf, insn);
            }
            if (!ms.tries.empty()) {
                if ((ms.insns.size() & 1u) != 0) {
                    AppendU16(buf, 0);
                }
                std::vector<uint8_t> encoded_handlers;
                AppendUleb128(encoded_handlers, static_cast<uint32_t>(ms.tries.size()));
                std::vector<uint16_t> handler_offs;
                for (const auto &tr : ms.tries) {
                    handler_offs.push_back(static_cast<uint16_t>(encoded_handlers.size()));
                    int32_t hcount = static_cast<int32_t>(tr.handlers.size());
                    AppendSleb128(encoded_handlers,
                                  tr.catch_all_addr != DEX_NO_INDEX ? -hcount : hcount);
                    for (const auto &pair : tr.handlers) {
                        uint32_t tidx = type_map_.at(pair.first);
                        AppendUleb128(encoded_handlers, tidx);
                        AppendUleb128(encoded_handlers, pair.second);
                    }
                    if (tr.catch_all_addr != DEX_NO_INDEX) {
                        AppendUleb128(encoded_handlers, tr.catch_all_addr);
                    }
                }
                for (size_t i = 0; i < ms.tries.size(); ++i) {
                    AppendU32(buf, ms.tries[i].start_addr);
                    AppendU16(buf, ms.tries[i].insn_count);
                    AppendU16(buf, handler_offs[i]);
                }
                buf.insert(buf.end(), encoded_handlers.begin(), encoded_handlers.end());
            }
            return code_off;
        };

        std::vector<uint32_t> class_data_offs(classes_.size(), 0);
        for (size_t ci = 0; ci < classes_.size(); ++ci) {
            auto &cls = classes_[ci];
            std::sort(
                cls.static_fields.begin(), cls.static_fields.end(),
                [](const FieldSpec &a, const FieldSpec &b) { return a.field_idx < b.field_idx; });
            std::sort(
                cls.instance_fields.begin(), cls.instance_fields.end(),
                [](const FieldSpec &a, const FieldSpec &b) { return a.field_idx < b.field_idx; });
            std::sort(cls.direct_methods.begin(), cls.direct_methods.end(),
                      [](const MethodSpec &a, const MethodSpec &b) {
                          return a.method_idx < b.method_idx;
                      });
            std::sort(cls.virtual_methods.begin(), cls.virtual_methods.end(),
                      [](const MethodSpec &a, const MethodSpec &b) {
                          return a.method_idx < b.method_idx;
                      });

            std::vector<uint32_t> dm_code_offs;
            for (const auto &dm : cls.direct_methods) {
                dm_code_offs.push_back(emit_code_item(dm));
            }
            std::vector<uint32_t> vm_code_offs;
            for (const auto &vm : cls.virtual_methods) {
                vm_code_offs.push_back(emit_code_item(vm));
            }

            class_data_offs[ci] = static_cast<uint32_t>(buf.size());
            AppendUleb128(buf, static_cast<uint32_t>(cls.static_fields.size()));
            AppendUleb128(buf, static_cast<uint32_t>(cls.instance_fields.size()));
            AppendUleb128(buf, static_cast<uint32_t>(cls.direct_methods.size()));
            AppendUleb128(buf, static_cast<uint32_t>(cls.virtual_methods.size()));

            auto write_fields = [&](const std::vector<FieldSpec> &fs) {
                uint32_t prev = 0;
                for (const auto &f : fs) {
                    AppendUleb128(buf, f.field_idx - prev);
                    AppendUleb128(buf, f.access_flags);
                    prev = f.field_idx;
                }
            };
            write_fields(cls.static_fields);
            write_fields(cls.instance_fields);

            auto write_methods = [&](const std::vector<MethodSpec> &ms,
                                     const std::vector<uint32_t> &coffs) {
                uint32_t prev = 0;
                for (size_t i = 0; i < ms.size(); ++i) {
                    AppendUleb128(buf, ms[i].method_idx - prev);
                    AppendUleb128(buf, ms[i].access_flags);
                    AppendUleb128(buf, coffs[i]);
                    prev = ms[i].method_idx;
                }
            };
            write_methods(cls.direct_methods, dm_code_offs);
            write_methods(cls.virtual_methods, vm_code_offs);
        }
        Align4(buf);

        for (size_t i = 0; i < str_data_offs.size(); ++i) {
            std::memcpy(buf.data() + str_ids_off + i * 4, &str_data_offs[i], 4);
        }
        for (size_t i = 0; i < types_.size(); ++i) {
            std::memcpy(buf.data() + type_ids_off + i * 4, &types_[i], 4);
        }
        for (size_t i = 0; i < protos_.size(); ++i) {
            DexProtoId p{protos_[i].shorty_idx, protos_[i].return_type_idx, proto_param_offs[i]};
            std::memcpy(buf.data() + proto_ids_off + i * 12, &p, sizeof(p));
        }
        for (size_t i = 0; i < fields_.size(); ++i) {
            std::memcpy(buf.data() + field_ids_off + i * 8, &fields_[i], sizeof(DexFieldId));
        }
        for (size_t i = 0; i < methods_.size(); ++i) {
            std::memcpy(buf.data() + method_ids_off + i * 8, &methods_[i], sizeof(DexMethodId));
        }
        for (size_t i = 0; i < classes_.size(); ++i) {
            DexClassDef cd{};
            cd.class_idx = classes_[i].class_idx;
            cd.access_flags = classes_[i].access_flags;
            cd.superclass_idx = classes_[i].superclass_idx;
            cd.source_file_idx = DEX_NO_INDEX;
            cd.class_data_off = class_data_offs[i];
            std::memcpy(buf.data() + class_defs_off + i * 32, &cd, sizeof(cd));
        }

        DexHeader hdr{};
        std::memcpy(hdr.magic, "dex\n035\0", 8);
        hdr.file_size = static_cast<uint32_t>(buf.size());
        hdr.header_size = sizeof(DexHeader);
        hdr.endian_tag = DEX_ENDIAN_CONSTANT;
        hdr.string_ids_size = static_cast<uint32_t>(strings_.size());
        hdr.string_ids_off = strings_.empty() ? 0 : str_ids_off;
        hdr.type_ids_size = static_cast<uint32_t>(types_.size());
        hdr.type_ids_off = types_.empty() ? 0 : type_ids_off;
        hdr.proto_ids_size = static_cast<uint32_t>(protos_.size());
        hdr.proto_ids_off = protos_.empty() ? 0 : proto_ids_off;
        hdr.field_ids_size = static_cast<uint32_t>(fields_.size());
        hdr.field_ids_off = fields_.empty() ? 0 : field_ids_off;
        hdr.method_ids_size = static_cast<uint32_t>(methods_.size());
        hdr.method_ids_off = methods_.empty() ? 0 : method_ids_off;
        hdr.class_defs_size = static_cast<uint32_t>(classes_.size());
        hdr.class_defs_off = classes_.empty() ? 0 : class_defs_off;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        return buf;
    }

  private:
    struct ProtoRaw {
        uint32_t shorty_idx{0};
        uint32_t return_type_idx{0};
        std::vector<uint16_t> params;
    };

    std::vector<std::string> strings_;
    std::map<std::string, uint32_t> string_map_;
    std::vector<uint32_t> types_;
    std::map<std::string, uint32_t> type_map_;
    std::vector<ProtoRaw> protos_;
    std::map<std::string, uint32_t> proto_map_;
    std::vector<DexFieldId> fields_;
    std::map<std::string, uint32_t> field_map_;
    std::vector<DexMethodId> methods_;
    std::map<std::string, uint32_t> method_map_;
    std::vector<ClassSpec> classes_;
};

std::vector<uint8_t> BuildMinimalManifest(std::string_view pkg_name)
{
    std::vector<std::string> strings = {
        "manifest",
        "package",
        std::string(pkg_name),
        "application",
    };

    std::vector<uint8_t> sp_strings;
    std::vector<uint32_t> offsets;
    for (const auto &s : strings) {
        offsets.push_back(static_cast<uint32_t>(sp_strings.size()));
        uint16_t len = static_cast<uint16_t>(s.size());
        AppendU16(sp_strings, len);
        for (char c : s) {
            AppendU16(sp_strings, static_cast<uint16_t>(static_cast<uint8_t>(c)));
        }
        AppendU16(sp_strings, 0);
    }
    Align4(sp_strings);

    uint32_t sp_header_size = 28;
    uint32_t offsets_bytes = static_cast<uint32_t>(offsets.size() * 4);
    uint32_t sp_total_size =
        sp_header_size + offsets_bytes + static_cast<uint32_t>(sp_strings.size());

    std::vector<uint8_t> body;
    AppendU16(body, 0x0001);
    AppendU16(body, 0x001C);
    AppendU32(body, sp_total_size);
    AppendU32(body, static_cast<uint32_t>(strings.size()));
    AppendU32(body, 0);
    AppendU32(body, 0);
    AppendU32(body, sp_header_size + offsets_bytes);
    AppendU32(body, 0);
    for (uint32_t off : offsets) {
        AppendU32(body, off);
    }
    body.insert(body.end(), sp_strings.begin(), sp_strings.end());

    AppendU16(body, 0x0102);
    AppendU16(body, 0x0010);
    AppendU32(body, 0x0038);
    AppendU32(body, 1);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0);
    AppendU16(body, 0x0014);
    AppendU16(body, 0x0014);
    AppendU16(body, 1);
    AppendU16(body, 0);
    AppendU16(body, 0);
    AppendU16(body, 0);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 1);
    AppendU32(body, 2);
    AppendU32(body, 2);
    AppendU16(body, 0x0008);
    AppendU16(body, 0x0300);
    AppendU32(body, 2);

    AppendU16(body, 0x0102);
    AppendU16(body, 0x0010);
    AppendU32(body, 0x0024);
    AppendU32(body, 2);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 3);
    AppendU16(body, 0x0014);
    AppendU16(body, 0x0014);
    AppendU16(body, 0);
    AppendU16(body, 0);
    AppendU16(body, 0);
    AppendU16(body, 0);

    AppendU16(body, 0x0103);
    AppendU16(body, 0x0010);
    AppendU32(body, 0x0018);
    AppendU32(body, 2);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 3);

    AppendU16(body, 0x0103);
    AppendU16(body, 0x0010);
    AppendU32(body, 0x0018);
    AppendU32(body, 3);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0xFFFFFFFFu);
    AppendU32(body, 0);

    std::vector<uint8_t> doc;
    AppendU16(doc, 0x0003);
    AppendU16(doc, 0x0008);
    AppendU32(doc, static_cast<uint32_t>(8 + body.size()));
    doc.insert(doc.end(), body.begin(), body.end());
    return doc;
}

std::vector<uint8_t>
BuildZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files)
{
    std::vector<uint8_t> zip;
    struct CentralDirInfo {
        std::string name;
        uint32_t crc{0};
        uint32_t size{0};
        uint32_t local_off{0};
    };
    std::vector<CentralDirInfo> cds;
    for (const auto &[name, data] : files) {
        uint32_t local_off = static_cast<uint32_t>(zip.size());
        uint32_t crc = crc32(0, data.data(), static_cast<uInt>(data.size()));
        AppendU32(zip, 0x04034B50u);
        AppendU16(zip, 20);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU32(zip, crc);
        AppendU32(zip, static_cast<uint32_t>(data.size()));
        AppendU32(zip, static_cast<uint32_t>(data.size()));
        AppendU16(zip, static_cast<uint16_t>(name.size()));
        AppendU16(zip, 0);
        zip.insert(zip.end(), name.begin(), name.end());
        zip.insert(zip.end(), data.begin(), data.end());
        cds.push_back({name, crc, static_cast<uint32_t>(data.size()), local_off});
    }
    uint32_t cd_off = static_cast<uint32_t>(zip.size());
    for (const auto &c : cds) {
        AppendU32(zip, 0x02014B50u);
        AppendU16(zip, 20);
        AppendU16(zip, 20);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU32(zip, c.crc);
        AppendU32(zip, c.size);
        AppendU32(zip, c.size);
        AppendU16(zip, static_cast<uint16_t>(c.name.size()));
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU16(zip, 0);
        AppendU32(zip, 0);
        AppendU32(zip, c.local_off);
        zip.insert(zip.end(), c.name.begin(), c.name.end());
    }
    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_off;
    AppendU32(zip, 0x06054B50u);
    AppendU16(zip, 0);
    AppendU16(zip, 0);
    AppendU16(zip, static_cast<uint16_t>(cds.size()));
    AppendU16(zip, static_cast<uint16_t>(cds.size()));
    AppendU32(zip, cd_size);
    AppendU32(zip, cd_off);
    AppendU16(zip, 0);
    return zip;
}

__attribute__((sysv_abi)) jint NativeMultiplyCallback(JNIEnv *, jobject, jint a, jint b)
{
    return a * b;
}

class DexVmFixture : public ::testing::Test {
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

TEST_F(DexVmFixture, ParsesDexHeaderAndMetadata)
{
    DexBuilder b;
    uint32_t f_id = b.AddField("Lcom/example/Calc;", "sFactor", "I");
    uint32_t m_id = b.AddMethod("Lcom/example/Calc;", "sumLoop", "II", "I", {"I"});
    auto &cls = b.AddClass("Lcom/example/Calc;");
    cls.static_fields.push_back({f_id, ACC_PUBLIC | ACC_STATIC});
    cls.direct_methods.push_back(
        {cls.class_idx, m_id, ACC_PUBLIC | ACC_STATIC, 2, 1, 0, {0x0012, 0x000F}, {}});

    auto bytes = b.Build();
    auto dex = DexFile::Parse(bytes);
    ASSERT_NE(dex, nullptr);

    const auto *parsed_cls = dex->FindClass("com/example/Calc");
    ASSERT_NE(parsed_cls, nullptr);
    EXPECT_EQ(parsed_cls->static_fields.size(), 1u);
    EXPECT_EQ(parsed_cls->static_fields[0].name, "sFactor");

    const auto *parsed_m = dex->FindMethod("com.example.Calc", "sumLoop", "(I)I");
    ASSERT_NE(parsed_m, nullptr);
    EXPECT_EQ(parsed_m->ins_size, 1u);
    EXPECT_EQ(parsed_m->registers_size, 2u);
}

TEST_F(DexVmFixture, ExecutesArithmeticAndControlFlow)
{
    DexBuilder b;
    uint32_t m_id = b.AddMethod("Lcom/example/MathLoop;", "sum1ToN", "II", "I", {"I"});
    auto &cls = b.AddClass("Lcom/example/MathLoop;");
    // v0 = acc (0), v1 = i (1), v2 = n (arg0), v3 = const 1
    // 0: const/4 v0, 0      -> 0x0012
    // 1: const/4 v1, 1      -> 0x1112
    // 2: const/4 v3, 1      -> 0x1312
    // 3: if-gt v1, v2, +3   -> 0x2136, 0x0003 (pc 3 + 3 = 6)
    // 5: add-int/2addr v0, v1 -> 0x10B0
    // 6?: wait compute exact offsets!
    // pc 0: 0x0012 (const/4 v0, 0)
    // pc 1: 0x1112 (const/4 v1, 1)
    // pc 2: if-gt v1, v2, +4 -> 0x2136, 0x0004 (pc 2 + 4 = 6)
    // pc 4: add-int/2addr v0, v1 -> 0x10B0
    // pc 5: add-int/lit8 v1, v1, 1 -> 0x01D8, 0x0101 -> wait pc shifts to 7!
    // Let's write cleanly:
    // pc 0: const/4 v0, 0       (0x0012)
    // pc 1: const/4 v1, 1       (0x1112)
    // pc 2: if-gt v1, v2, +5    (0x2136, 0x0005) -> target = pc 7
    // pc 4: add-int/2addr v0, v1 (0x10B0)
    // pc 5: add-int/lit8 v1, v1, 1 (0x01D8, 0x0101) -> op=D8, aa=01, bb=01, cc=01
    // pc 7: wait goto -5 at pc 7 -> target = pc 2!
    // Let's fix if-gt target to pc 8!
    // pc 2: if-gt v1, v2, +6    (0x2136, 0x0006) -> target = pc 8
    // pc 4: add-int/2addr v0, v1 (0x10B0)
    // pc 5: add-int/lit8 v1, v1, 1 (0x01D8, 0x0101)
    // pc 7: goto -5             (0xFB28) -> pc 7 + (-5) = pc 2
    // pc 8: return v0           (0x000F)
    cls.direct_methods.push_back(
        {cls.class_idx,
         m_id,
         ACC_PUBLIC | ACC_STATIC,
         3,
         1,
         0,
         {0x0012, 0x1112, 0x2136, 0x0006, 0x10B0, 0x01D8, 0x0101, 0xFB28, 0x000F},
         {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    auto resolved = DexVm::Instance().ResolveMethod("com/example/MathLoop", "sum1ToN", "(I)I");
    ASSERT_TRUE(resolved.has_value());

    uint64_t args[1] = {10};
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, args);
    EXPECT_EQ(static_cast<int32_t>(res.raw), 55);
}

TEST_F(DexVmFixture, InstantiatesObjectsAndMutatesFields)
{
    DexBuilder b;
    uint32_t sf = b.AddField("Lcom/example/Box;", "sBias", "I");
    uint32_t inf = b.AddField("Lcom/example/Box;", "value", "I");
    uint32_t clinit_m = b.AddMethod("Lcom/example/Box;", "<clinit>", "V", "V", {});
    uint32_t init_m = b.AddMethod("Lcom/example/Box;", "<init>", "VI", "V", {"I"});
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", "V", {});
    uint32_t get_m = b.AddMethod("Lcom/example/Box;", "getTotal", "I", "I", {});

    auto &cls = b.AddClass("Lcom/example/Box;");
    cls.static_fields.push_back({sf, ACC_PUBLIC | ACC_STATIC});
    cls.instance_fields.push_back({inf, ACC_PUBLIC});

    // <clinit>: const/16 v0, 100; sput v0, sf; return-void
    cls.direct_methods.push_back({cls.class_idx,
                                  clinit_m,
                                  ACC_STATIC | ACC_CONSTRUCTOR,
                                  1,
                                  0,
                                  0,
                                  {0x0013, 100, 0x0067, static_cast<uint16_t>(sf), 0x000E},
                                  {}});
    // <init>(int v2): v1 = this, v2 = arg0
    // invoke-direct {v1}, Object.<init>; iput v2, v1, inf; return-void
    cls.direct_methods.push_back({cls.class_idx,
                                  init_m,
                                  ACC_PUBLIC | ACC_CONSTRUCTOR,
                                  3,
                                  2,
                                  1,
                                  {0x1070, static_cast<uint16_t>(obj_init), 0x0001, 0x1259,
                                   static_cast<uint16_t>(inf), 0x000E},
                                  {}});
    // getTotal(): v2 = this
    // iget v0, v2, inf; sget v1, sf; add-int v0, v0, v1; return v0
    cls.virtual_methods.push_back({cls.class_idx,
                                   get_m,
                                   ACC_PUBLIC,
                                   3,
                                   1,
                                   0,
                                   {0x2052, static_cast<uint16_t>(inf), 0x0160,
                                    static_cast<uint16_t>(sf), 0x0090, 0x0100, 0x000F},
                                   {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));

    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass box_cls = env->functions->FindClass(env, "com/example/Box");
    jmethodID ctor_id = env->functions->GetMethodID(env, box_cls, "<init>", "(I)V");
    jobject obj = env->functions->NewObject(env, box_cls, ctor_id, 23);
    ASSERT_NE(obj, nullptr);

    jmethodID get_id = env->functions->GetMethodID(env, box_cls, "getTotal", "()I");
    EXPECT_EQ(env->functions->CallIntMethod(env, obj, get_id), 123);
    EXPECT_EQ(DexVm::Instance().GetStaticField("com/example/Box", "sBias"), 100ULL);
}

TEST_F(DexVmFixture, ExecutesArraysAndStringBuilderIntrinsics)
{
    DexBuilder b;
    uint32_t sb_init = b.AddMethod("Ljava/lang/StringBuilder;", "<init>", "V", "V", {});
    uint32_t sb_app_s = b.AddMethod("Ljava/lang/StringBuilder;", "append", "LL",
                                    "Ljava/lang/StringBuilder;", {"Ljava/lang/String;"});
    uint32_t sb_app_i = b.AddMethod("Ljava/lang/StringBuilder;", "append", "LI",
                                    "Ljava/lang/StringBuilder;", {"I"});
    uint32_t sb_to_s =
        b.AddMethod("Ljava/lang/StringBuilder;", "toString", "L", "Ljava/lang/String;", {});
    uint32_t build_m =
        b.AddMethod("Lcom/example/TextHelper;", "formatCount", "LI", "Ljava/lang/String;", {"I"});

    uint32_t str_hello = b.AddString("arr=");
    uint32_t sb_type = b.AddType("Ljava/lang/StringBuilder;");
    uint32_t iarr_type = b.AddType("[I");

    auto &cls = b.AddClass("Lcom/example/TextHelper;");
    // regs: v0..v2 scratch, v3 = n (arg0)
    // new-array v0, v3, [I       -> op=23, a=0, b=3 => 0x3023, iarr_type
    // array-length v1, v0        -> op=21, a=1, b=0 => 0x0121
    // new-instance v0, StringBuilder -> op=22, aa=0 => 0x0022, sb_type
    // invoke-direct {v0}, sb_init -> 0x1070, sb_init, 0x0000
    // const-string v2, "arr="    -> 0x021A, str_hello
    // invoke-virtual {v0, v2}, sb_app_s -> 0x206E, sb_app_s, 0x0020
    // invoke-virtual {v0, v1}, sb_app_i -> 0x206E, sb_app_i, 0x0010
    // invoke-virtual {v0}, sb_to_s -> 0x106E, sb_to_s, 0x0000
    // move-result-object v0      -> 0x000C
    // return-object v0           -> 0x0011
    cls.direct_methods.push_back({cls.class_idx,
                                  build_m,
                                  ACC_PUBLIC | ACC_STATIC,
                                  4,
                                  1,
                                  2,
                                  {0x3023,
                                   static_cast<uint16_t>(iarr_type),
                                   0x0121,
                                   0x0022,
                                   static_cast<uint16_t>(sb_type),
                                   0x1070,
                                   static_cast<uint16_t>(sb_init),
                                   0x0000,
                                   0x021A,
                                   static_cast<uint16_t>(str_hello),
                                   0x206E,
                                   static_cast<uint16_t>(sb_app_s),
                                   0x0020,
                                   0x206E,
                                   static_cast<uint16_t>(sb_app_i),
                                   0x0010,
                                   0x106E,
                                   static_cast<uint16_t>(sb_to_s),
                                   0x0000,
                                   0x000C,
                                   0x0011},
                                  {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass hcls = env->functions->FindClass(env, "com/example/TextHelper");
    jmethodID mid =
        env->functions->GetStaticMethodID(env, hcls, "formatCount", "(I)Ljava/lang/String;");
    jobject s = env->functions->CallStaticObjectMethod(env, hcls, mid, 7);
    const char *utf = JniEnvironment::Instance().GetStringChars(s);
    ASSERT_NE(utf, nullptr);
    EXPECT_STREQ(utf, "arr=7");
}

TEST_F(DexVmFixture, HandlesExceptionsAndTryCatchUnwinding)
{
    DexBuilder b;
    uint32_t ex_type = b.AddType("Ljava/lang/IllegalStateException;");
    uint32_t caught_m = b.AddMethod("Lcom/example/ExTest;", "caught", "I", "I", {});
    uint32_t uncaught_m = b.AddMethod("Lcom/example/ExTest;", "uncaught", "V", "V", {});

    auto &cls = b.AddClass("Lcom/example/ExTest;");
    // caught():
    // pc 0: new-instance v0, IllegalStateException (2 units)
    // pc 2: throw v0 (1 unit)   [try covers pc 0..3 -> catch IllegalStateException at pc 3]
    // pc 3: move-exception v0 (1 unit)
    // pc 4: const/16 v0, 77 (2 units)
    // pc 6: return v0 (1 unit)
    DexBuilder::TrySpec try_spec;
    try_spec.start_addr = 0;
    try_spec.insn_count = 3;
    try_spec.handlers.push_back({"Ljava/lang/IllegalStateException;", 3});
    cls.direct_methods.push_back(
        {cls.class_idx,
         caught_m,
         ACC_PUBLIC | ACC_STATIC,
         1,
         0,
         0,
         {0x0022, static_cast<uint16_t>(ex_type), 0x0027, 0x000D, 0x0013, 77, 0x000F},
         {try_spec}});
    cls.direct_methods.push_back({cls.class_idx,
                                  uncaught_m,
                                  ACC_PUBLIC | ACC_STATIC,
                                  1,
                                  0,
                                  0,
                                  {0x0022, static_cast<uint16_t>(ex_type), 0x0027, 0x000E},
                                  {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass ecls = env->functions->FindClass(env, "com/example/ExTest");
    jmethodID cmid = env->functions->GetStaticMethodID(env, ecls, "caught", "()I");
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, ecls, cmid), 77);
    EXPECT_EQ(env->functions->ExceptionCheck(env), JNI_FALSE);

    jmethodID umid = env->functions->GetStaticMethodID(env, ecls, "uncaught", "()V");
    env->functions->CallStaticVoidMethod(env, ecls, umid);
    EXPECT_EQ(env->functions->ExceptionCheck(env), JNI_TRUE);
}

TEST_F(DexVmFixture, InvokesDexMethodsViaJniEnv)
{
    DexBuilder b;
    uint32_t add_m = b.AddMethod("Lcom/example/Bridge;", "addThree", "IIII", "I", {"I", "I", "I"});
    auto &cls = b.AddClass("Lcom/example/Bridge;");
    // v0..v1 scratch, v2=a, v3=b, v4=c
    // add-int v0, v2, v3 -> 0x0090, 0x0302
    // add-int v0, v0, v4 -> 0x0090, 0x0400
    // return v0            -> 0x000F
    cls.direct_methods.push_back({cls.class_idx,
                                  add_m,
                                  ACC_PUBLIC | ACC_STATIC,
                                  5,
                                  3,
                                  0,
                                  {0x0090, 0x0302, 0x0090, 0x0400, 0x000F},
                                  {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass cls_ref = env->functions->FindClass(env, "com/example/Bridge");
    jmethodID mid = env->functions->GetStaticMethodID(env, cls_ref, "addThree", "(III)I");
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, cls_ref, mid, 10, 20, 12), 42);
}

TEST_F(DexVmFixture, RoundTripsNativeToDexToNativeViaRegisterNatives)
{
    DexBuilder b;
    uint32_t nat_m = b.AddMethod("Lcom/example/NativeBridge;", "nativeMul", "III", "I", {"I", "I"});
    uint32_t wrap_m = b.AddMethod("Lcom/example/NativeBridge;", "callMul", "III", "I", {"I", "I"});
    auto &cls = b.AddClass("Lcom/example/NativeBridge;");
    // nativeMul(I, I)I: ACC_NATIVE, no code
    cls.direct_methods.push_back(
        {cls.class_idx, nat_m, ACC_PUBLIC | ACC_STATIC | ACC_NATIVE, 0, 0, 0, {}, {}});
    // callMul(I v1, I v2) -> invoke-static {v1, v2}, nat_m; move-result v0; return v0
    cls.direct_methods.push_back({cls.class_idx,
                                  wrap_m,
                                  ACC_PUBLIC | ACC_STATIC,
                                  3,
                                  2,
                                  0,
                                  {0x2071, static_cast<uint16_t>(nat_m), 0x0021, 0x000A, 0x000F},
                                  {}});

    ASSERT_TRUE(DexVm::Instance().LoadDex(b.Build()));
    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass cls_ref = env->functions->FindClass(env, "com/example/NativeBridge");
    JNINativeMethod methods[1] = {
        {"nativeMul", "(II)I", reinterpret_cast<void *>(&NativeMultiplyCallback)},
    };
    ASSERT_EQ(env->functions->RegisterNatives(env, cls_ref, methods, 1), JNI_OK);

    jmethodID mid = env->functions->GetStaticMethodID(env, cls_ref, "callMul", "(II)I");
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, cls_ref, mid, 6, 9), 54);
}

TEST_F(DexVmFixture, LoadsMultiDexFromApkPackageManager)
{
    DexBuilder b1;
    uint32_t m1 = b1.AddMethod("Lcom/portage/dexpkg/Primary;", "tag", "I", "I", {});
    auto &c1 = b1.AddClass("Lcom/portage/dexpkg/Primary;");
    c1.direct_methods.push_back(
        {c1.class_idx, m1, ACC_PUBLIC | ACC_STATIC, 1, 0, 0, {0x0013, 111, 0x000F}, {}});

    DexBuilder b2;
    uint32_t m2 = b2.AddMethod("Lcom/portage/dexpkg/Secondary;", "tag", "I", "I", {});
    auto &c2 = b2.AddClass("Lcom/portage/dexpkg/Secondary;");
    c2.direct_methods.push_back(
        {c2.class_idx, m2, ACC_PUBLIC | ACC_STATIC, 1, 0, 0, {0x0013, 222, 0x000F}, {}});

    auto apk_bytes = BuildZip({
        {"AndroidManifest.xml", BuildMinimalManifest("com.portage.dexpkg")},
        {"classes.dex", b1.Build()},
        {"classes2.dex", b2.Build()},
    });

    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "portage_dex_pkg_test";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path apk_path = temp_dir / "test_multidex.apk";
    {
        std::ofstream ofs(apk_path, std::ios::binary | std::ios::trunc);
        ofs.write(reinterpret_cast<const char *>(apk_bytes.data()),
                  static_cast<std::streamsize>(apk_bytes.size()));
    }

    ASSERT_TRUE(PackageManager::Instance().Initialize(temp_dir / "data"));
    auto pkg = PackageManager::Instance().InstallPackage(apk_path);
    ASSERT_TRUE(pkg.has_value());

    JNIEnv *env = JniEnvironment::Instance().GetJNIEnv();
    jclass cls1 = env->functions->FindClass(env, "com/portage/dexpkg/Primary");
    jclass cls2 = env->functions->FindClass(env, "com/portage/dexpkg/Secondary");
    jmethodID mid1 = env->functions->GetStaticMethodID(env, cls1, "tag", "()I");
    jmethodID mid2 = env->functions->GetStaticMethodID(env, cls2, "tag", "()I");
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, cls1, mid1), 111);
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, cls2, mid2), 222);

    PackageManager::Instance().Reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}
