#pragma once

#include "package/dex_file.hpp"
#include "package/dex_types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <zlib.h>

namespace portage::package::test_util {

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

class RuntimeDexBuilder {
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

    struct EncodedValueSpec {
        enum class Kind { Int, Long, String, Type, MethodType, MethodHandle, Boolean, Null };
        Kind kind{Kind::Null};
        uint64_t uval{0};
    };

    struct ClassSpec {
        uint32_t class_idx{0};
        uint32_t superclass_idx{DEX_NO_INDEX};
        uint32_t access_flags{ACC_PUBLIC};
        std::vector<uint32_t> interfaces;
        std::vector<FieldSpec> static_fields;
        std::vector<FieldSpec> instance_fields;
        std::vector<MethodSpec> direct_methods;
        std::vector<MethodSpec> virtual_methods;
        std::vector<EncodedValueSpec> static_values;
    };

    void SetDexVersion(std::string ver) { dex_version_ = std::move(ver); }

    uint32_t AddMethodHandle(DexMethodHandleType type, uint16_t field_or_method_id)
    {
        uint32_t idx = static_cast<uint32_t>(method_handles_.size());
        DexMethodHandleItem item{};
        item.method_handle_type = static_cast<uint16_t>(type);
        item.field_or_method_id = field_or_method_id;
        method_handles_.push_back(item);
        return idx;
    }

    uint32_t AddCallSite(std::vector<EncodedValueSpec> values)
    {
        uint32_t idx = static_cast<uint32_t>(call_sites_.size());
        call_sites_.push_back(std::move(values));
        return idx;
    }

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
        for (auto &cls : classes_) {
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

        std::vector<uint32_t> class_iface_offs(classes_.size(), 0);
        for (size_t i = 0; i < classes_.size(); ++i) {
            if (!classes_[i].interfaces.empty()) {
                Align4(data_sec);
                class_iface_offs[i] = static_cast<uint32_t>(data_sec.size());
                AppendU32(data_sec, static_cast<uint32_t>(classes_[i].interfaces.size()));
                for (uint32_t tid : classes_[i].interfaces) {
                    AppendU16(data_sec, static_cast<uint16_t>(tid));
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

        auto emit_encoded_array = [&](const std::vector<EncodedValueSpec> &vals) -> uint32_t {
            if (vals.empty()) {
                return 0;
            }
            uint32_t off = static_cast<uint32_t>(data_sec.size());
            AppendUleb128(data_sec, static_cast<uint32_t>(vals.size()));
            for (const auto &v : vals) {
                switch (v.kind) {
                case EncodedValueSpec::Kind::Null:
                    data_sec.push_back(0x1Eu);
                    break;
                case EncodedValueSpec::Kind::Boolean:
                    data_sec.push_back(static_cast<uint8_t>(0x1Fu | ((v.uval ? 1u : 0u) << 5)));
                    break;
                case EncodedValueSpec::Kind::Int: {
                    uint32_t iv = static_cast<uint32_t>(v.uval);
                    data_sec.push_back(static_cast<uint8_t>(0x04u | (3u << 5)));
                    AppendU32(data_sec, iv);
                    break;
                }
                case EncodedValueSpec::Kind::Long: {
                    uint64_t lv = v.uval;
                    data_sec.push_back(static_cast<uint8_t>(0x06u | (7u << 5)));
                    AppendU32(data_sec, static_cast<uint32_t>(lv & 0xFFFFFFFFu));
                    AppendU32(data_sec, static_cast<uint32_t>(lv >> 32));
                    break;
                }
                case EncodedValueSpec::Kind::String: {
                    data_sec.push_back(static_cast<uint8_t>(0x17u | (3u << 5)));
                    AppendU32(data_sec, static_cast<uint32_t>(v.uval));
                    break;
                }
                case EncodedValueSpec::Kind::Type: {
                    data_sec.push_back(static_cast<uint8_t>(0x18u | (3u << 5)));
                    AppendU32(data_sec, static_cast<uint32_t>(v.uval));
                    break;
                }
                case EncodedValueSpec::Kind::MethodType: {
                    data_sec.push_back(static_cast<uint8_t>(0x15u | (3u << 5)));
                    AppendU32(data_sec, static_cast<uint32_t>(v.uval));
                    break;
                }
                case EncodedValueSpec::Kind::MethodHandle: {
                    data_sec.push_back(static_cast<uint8_t>(0x16u | (3u << 5)));
                    AppendU32(data_sec, static_cast<uint32_t>(v.uval));
                    break;
                }
                }
            }
            return off;
        };

        std::vector<uint32_t> class_static_val_offs(classes_.size(), 0);
        for (size_t c = 0; c < classes_.size(); ++c) {
            class_static_val_offs[c] = emit_encoded_array(classes_[c].static_values);
        }

        std::vector<uint32_t> call_site_array_offs;
        for (const auto &cs : call_sites_) {
            call_site_array_offs.push_back(emit_encoded_array(cs));
        }

        uint32_t call_site_ids_off = 0;
        if (!call_sites_.empty()) {
            Align4(data_sec);
            call_site_ids_off = static_cast<uint32_t>(data_sec.size());
            for (uint32_t arr_off : call_site_array_offs) {
                AppendU32(data_sec, data_off + arr_off);
            }
        }

        uint32_t method_handles_off = 0;
        if (!method_handles_.empty()) {
            Align4(data_sec);
            method_handles_off = static_cast<uint32_t>(data_sec.size());
            for (const auto &mh : method_handles_) {
                AppendU16(data_sec, mh.method_handle_type);
                AppendU16(data_sec, mh.unused1);
                AppendU16(data_sec, mh.field_or_method_id);
                AppendU16(data_sec, mh.unused2);
            }
        }

        uint32_t map_off_rel = 0;
        if (!call_sites_.empty() || !method_handles_.empty()) {
            Align4(data_sec);
            map_off_rel = static_cast<uint32_t>(data_sec.size());
            std::vector<DexMapItem> map_items;
            if (!call_sites_.empty()) {
                DexMapItem item{};
                item.type = kDexTypeCallSiteIdItem;
                item.size = static_cast<uint32_t>(call_sites_.size());
                item.offset = data_off + call_site_ids_off;
                map_items.push_back(item);
            }
            if (!method_handles_.empty()) {
                DexMapItem item{};
                item.type = kDexTypeMethodHandleItem;
                item.size = static_cast<uint32_t>(method_handles_.size());
                item.offset = data_off + method_handles_off;
                map_items.push_back(item);
            }
            AppendU32(data_sec, static_cast<uint32_t>(map_items.size()));
            for (const auto &mi : map_items) {
                AppendU16(data_sec, mi.type);
                AppendU16(data_sec, mi.unused);
                AppendU32(data_sec, mi.size);
                AppendU32(data_sec, mi.offset);
            }
        }
        Align4(data_sec);

        uint32_t total_sz = data_off + static_cast<uint32_t>(data_sec.size());

        std::vector<uint8_t> out(total_sz, 0);
        DexHeader hdr{};
        std::memcpy(hdr.magic, "dex\n035\0", 8);
        if (dex_version_.size() == 3) {
            std::memcpy(hdr.magic + 4, dex_version_.data(), 3);
        }
        hdr.file_size = total_sz;
        hdr.header_size = header_sz;
        hdr.endian_tag = DEX_ENDIAN_CONSTANT;
        hdr.map_off = map_off_rel ? (data_off + map_off_rel) : 0;
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
        if (!fields_.empty()) {
            std::memcpy(out.data() + field_off, fields_.data(),
                        fields_.size() * sizeof(DexFieldId));
        }
        if (!methods_.empty()) {
            std::memcpy(out.data() + method_off, methods_.data(),
                        methods_.size() * sizeof(DexMethodId));
        }
        for (size_t i = 0; i < classes_.size(); ++i) {
            DexClassDef cd{};
            cd.class_idx = classes_[i].class_idx;
            cd.access_flags = classes_[i].access_flags;
            cd.superclass_idx = classes_[i].superclass_idx;
            cd.interfaces_off = class_iface_offs[i] ? (data_off + class_iface_offs[i]) : 0;
            cd.source_file_idx = DEX_NO_INDEX;
            cd.class_data_off = data_off + class_data_offs[i];
            cd.static_values_off =
                class_static_val_offs[i] ? (data_off + class_static_val_offs[i]) : 0;
            std::memcpy(out.data() + class_off + i * sizeof(DexClassDef), &cd, sizeof(DexClassDef));
        }
        std::memcpy(out.data() + data_off, data_sec.data(), data_sec.size());

        uint32_t adler = adler32(0L, Z_NULL, 0);
        adler = adler32(adler, out.data() + 12, total_sz - 12);
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
    std::unordered_map<std::string, uint32_t> string_map_;
    std::vector<uint32_t> types_;
    std::unordered_map<std::string, uint32_t> type_map_;
    std::vector<ProtoSpec> protos_;
    std::unordered_map<std::string, uint32_t> proto_map_;
    std::vector<DexFieldId> fields_;
    std::unordered_map<std::string, uint32_t> field_map_;
    std::vector<DexMethodId> methods_;
    std::unordered_map<std::string, uint32_t> method_map_;
    std::vector<ClassSpec> classes_;
    std::string dex_version_{"035"};
    std::vector<DexMethodHandleItem> method_handles_;
    std::vector<std::vector<EncodedValueSpec>> call_sites_;
};

inline uint16_t Encode11n(uint8_t op, uint8_t reg, int8_t val4)
{
    return static_cast<uint16_t>(op | ((reg & 0xFu) << 8) |
                                 ((static_cast<uint8_t>(val4) & 0xFu) << 12));
}

inline uint16_t Encode10x(uint8_t op) { return static_cast<uint16_t>(op); }

inline uint16_t Encode11x(uint8_t op, uint8_t reg)
{
    return static_cast<uint16_t>(op | (reg << 8));
}

inline uint16_t Encode12x(uint8_t op, uint8_t a, uint8_t b)
{
    return static_cast<uint16_t>(op | ((a & 0xFu) << 8) | ((b & 0xFu) << 12));
}

inline void Emit21c(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, uint16_t idx)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(idx);
}

inline void Emit21s(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, int16_t val)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(static_cast<uint16_t>(val));
}

inline void Emit31i(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, uint32_t val)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(static_cast<uint16_t>(val & 0xFFFFu));
    insns.push_back(static_cast<uint16_t>((val >> 16) & 0xFFFFu));
}

inline void Emit51l(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, uint64_t val)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(static_cast<uint16_t>(val & 0xFFFFu));
    insns.push_back(static_cast<uint16_t>((val >> 16) & 0xFFFFu));
    insns.push_back(static_cast<uint16_t>((val >> 32) & 0xFFFFu));
    insns.push_back(static_cast<uint16_t>((val >> 48) & 0xFFFFu));
}

inline void Emit22c(std::vector<uint16_t> &insns, uint8_t op, uint8_t a, uint8_t b, uint16_t idx)
{
    insns.push_back(static_cast<uint16_t>(op | ((a & 0xFu) << 8) | ((b & 0xFu) << 12)));
    insns.push_back(idx);
}

inline void Emit22b(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, uint8_t bb, int8_t cc)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(static_cast<uint16_t>(bb | (static_cast<uint8_t>(cc) << 8)));
}

inline void Emit22t(std::vector<uint16_t> &insns, uint8_t op, uint8_t a, uint8_t b, int16_t off)
{
    insns.push_back(static_cast<uint16_t>(op | ((a & 0xFu) << 8) | ((b & 0xFu) << 12)));
    insns.push_back(static_cast<uint16_t>(off));
}

inline void Emit23x(std::vector<uint8_t> &, uint8_t, uint8_t, uint8_t, uint8_t) = delete;

inline void Emit23x(std::vector<uint16_t> &insns, uint8_t op, uint8_t aa, uint8_t bb, uint8_t cc)
{
    insns.push_back(static_cast<uint16_t>(op | (aa << 8)));
    insns.push_back(static_cast<uint16_t>(bb | (cc << 8)));
}

inline void Emit35c(std::vector<uint16_t> &insns, uint8_t op, uint16_t method_idx,
                    std::vector<uint8_t> regs)
{
    uint8_t count = static_cast<uint8_t>(regs.size());
    while (regs.size() < 5) {
        regs.push_back(0);
    }
    insns.push_back(static_cast<uint16_t>(op | ((regs[4] & 0xFu) << 8) | ((count & 0xFu) << 12)));
    insns.push_back(method_idx);
    insns.push_back(static_cast<uint16_t>((regs[0] & 0xFu) | ((regs[1] & 0xFu) << 4) |
                                          ((regs[2] & 0xFu) << 8) | ((regs[3] & 0xFu) << 12)));
}

inline void Emit45cc(std::vector<uint16_t> &insns, uint8_t op, uint16_t method_idx,
                     uint16_t proto_idx, std::vector<uint8_t> regs)
{
    uint8_t count = static_cast<uint8_t>(regs.size());
    while (regs.size() < 5) {
        regs.push_back(0);
    }
    insns.push_back(static_cast<uint16_t>(op | ((regs[4] & 0xFu) << 8) | ((count & 0xFu) << 12)));
    insns.push_back(method_idx);
    insns.push_back(static_cast<uint16_t>((regs[0] & 0xFu) | ((regs[1] & 0xFu) << 4) |
                                          ((regs[2] & 0xFu) << 8) | ((regs[3] & 0xFu) << 12)));
    insns.push_back(proto_idx);
}

} // namespace portage::package::test_util
