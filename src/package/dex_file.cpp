#include "package/dex_file.hpp"

#include <cstring>

namespace portage::package {

namespace {

class DexCursor {
  public:
    explicit DexCursor(std::span<const uint8_t> data, size_t offset = 0) noexcept
        : data_(data), pos_(offset)
    {
    }

    [[nodiscard]] bool CanRead(size_t n) const noexcept
    {
        return pos_ <= data_.size() && n <= data_.size() - pos_;
    }

    [[nodiscard]] size_t Tell() const noexcept { return pos_; }
    void Seek(size_t off) noexcept { pos_ = off; }

    template <typename T> [[nodiscard]] bool Read(T &out) noexcept
    {
        if (!CanRead(sizeof(T))) {
            return false;
        }
        std::memcpy(&out, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    [[nodiscard]] bool ReadUleb128(uint32_t &out) noexcept
    {
        uint32_t result = 0;
        int shift = 0;
        for (int i = 0; i < 5; ++i) {
            uint8_t byte = 0;
            if (!Read(byte)) {
                return false;
            }
            result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0) {
                out = result;
                return true;
            }
            shift += 7;
        }
        return false;
    }

    [[nodiscard]] bool ReadSleb128(int32_t &out) noexcept
    {
        uint32_t result = 0;
        int shift = 0;
        uint8_t byte = 0;
        for (int i = 0; i < 5; ++i) {
            if (!Read(byte)) {
                return false;
            }
            result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
            shift += 7;
            if ((byte & 0x80u) == 0) {
                if (shift < 32 && (byte & 0x40u) != 0) {
                    result |= (~0u) << shift;
                }
                out = static_cast<int32_t>(result);
                return true;
            }
        }
        return false;
    }

  private:
    std::span<const uint8_t> data_;
    size_t pos_{0};
};

std::string DecodeMutf8(std::span<const uint8_t> data, size_t offset)
{
    if (offset >= data.size()) {
        return {};
    }
    DexCursor cur(data, offset);
    uint32_t utf16_len = 0;
    if (!cur.ReadUleb128(utf16_len)) {
        return {};
    }
    (void)utf16_len;
    std::string out;
    size_t pos = cur.Tell();
    while (pos < data.size() && data[pos] != 0) {
        uint8_t c = data[pos];
        if (c == 0xC0u && pos + 1 < data.size() && data[pos + 1] == 0x80u) {
            out.push_back('\0');
            pos += 2;
        } else {
            out.push_back(static_cast<char>(c));
            ++pos;
        }
    }
    return out;
}

} // namespace

std::string DexFile::NormalizeDescriptor(std::string_view name)
{
    if (name.empty()) {
        return {};
    }
    if (name.front() == '[' || (name.front() == 'L' && name.back() == ';')) {
        std::string s(name);
        for (char &ch : s) {
            if (ch == '.') {
                ch = '/';
            }
        }
        return s;
    }
    std::string s;
    s.reserve(name.size() + 2);
    s.push_back('L');
    for (char ch : name) {
        s.push_back(ch == '.' ? '/' : ch);
    }
    s.push_back(';');
    return s;
}

std::string DexFile::DescriptorToSlashName(std::string_view descriptor)
{
    if (descriptor.size() >= 2 && descriptor.front() == 'L' && descriptor.back() == ';') {
        return std::string(descriptor.substr(1, descriptor.size() - 2));
    }
    return std::string(descriptor);
}

std::shared_ptr<DexFile> DexFile::Parse(std::span<const uint8_t> data)
{
    if (data.size() < sizeof(DexHeader)) {
        return nullptr;
    }
    DexHeader hdr{};
    std::memcpy(&hdr, data.data(), sizeof(DexHeader));
    if (std::memcmp(hdr.magic, "dex\n", 4) != 0 || hdr.magic[7] != '\0') {
        return nullptr;
    }
    if (hdr.endian_tag != DEX_ENDIAN_CONSTANT || hdr.file_size > data.size() ||
        hdr.header_size < sizeof(DexHeader)) {
        return nullptr;
    }

    auto dex = std::make_shared<DexFile>();

    if (hdr.string_ids_size > 0) {
        DexCursor cur(data, hdr.string_ids_off);
        dex->strings_.reserve(hdr.string_ids_size);
        for (uint32_t i = 0; i < hdr.string_ids_size; ++i) {
            DexStringId sid{};
            if (!cur.Read(sid)) {
                return nullptr;
            }
            dex->strings_.push_back(DecodeMutf8(data, sid.string_data_off));
        }
    }

    if (hdr.type_ids_size > 0) {
        DexCursor cur(data, hdr.type_ids_off);
        dex->types_.reserve(hdr.type_ids_size);
        for (uint32_t i = 0; i < hdr.type_ids_size; ++i) {
            DexTypeId tid{};
            if (!cur.Read(tid)) {
                return nullptr;
            }
            dex->types_.emplace_back(dex->GetString(tid.descriptor_idx));
        }
    }

    struct ProtoDecoded {
        std::string shorty;
        std::string descriptor;
        std::string return_type;
        std::vector<std::string> param_types;
    };
    std::vector<ProtoDecoded> protos;
    if (hdr.proto_ids_size > 0) {
        DexCursor cur(data, hdr.proto_ids_off);
        protos.reserve(hdr.proto_ids_size);
        for (uint32_t i = 0; i < hdr.proto_ids_size; ++i) {
            DexProtoId pid{};
            if (!cur.Read(pid)) {
                return nullptr;
            }
            ProtoDecoded p;
            p.shorty = std::string(dex->GetString(pid.shorty_idx));
            p.return_type = std::string(dex->GetType(pid.return_type_idx));
            std::string sig = "(";
            if (pid.parameters_off != 0) {
                DexCursor pcur(data, pid.parameters_off);
                uint32_t count = 0;
                if (pcur.Read(count)) {
                    for (uint32_t k = 0; k < count; ++k) {
                        uint16_t type_idx = 0;
                        if (!pcur.Read(type_idx)) {
                            break;
                        }
                        std::string t = std::string(dex->GetType(type_idx));
                        sig += t;
                        p.param_types.push_back(std::move(t));
                    }
                }
            }
            sig += ")";
            sig += p.return_type;
            p.descriptor = std::move(sig);
            protos.push_back(std::move(p));
        }
    }

    if (hdr.field_ids_size > 0) {
        DexCursor cur(data, hdr.field_ids_off);
        dex->field_refs_.reserve(hdr.field_ids_size);
        for (uint32_t i = 0; i < hdr.field_ids_size; ++i) {
            DexFieldId fid{};
            if (!cur.Read(fid)) {
                return nullptr;
            }
            DexFieldRef fref;
            fref.class_descriptor = std::string(dex->GetType(fid.class_idx));
            fref.type_descriptor = std::string(dex->GetType(fid.type_idx));
            fref.name = std::string(dex->GetString(fid.name_idx));
            dex->field_refs_.push_back(std::move(fref));
        }
    }

    if (hdr.method_ids_size > 0) {
        DexCursor cur(data, hdr.method_ids_off);
        dex->method_refs_.reserve(hdr.method_ids_size);
        for (uint32_t i = 0; i < hdr.method_ids_size; ++i) {
            DexMethodId mid{};
            if (!cur.Read(mid)) {
                return nullptr;
            }
            DexMethodRef mref;
            mref.class_descriptor = std::string(dex->GetType(mid.class_idx));
            mref.name = std::string(dex->GetString(mid.name_idx));
            if (mid.proto_idx < protos.size()) {
                mref.shorty = protos[mid.proto_idx].shorty;
                mref.descriptor = protos[mid.proto_idx].descriptor;
                mref.return_type = protos[mid.proto_idx].return_type;
                mref.param_types = protos[mid.proto_idx].param_types;
            }
            dex->method_refs_.push_back(std::move(mref));
        }
    }

    auto parse_code_item = [&](uint32_t code_off, DexMethodDef &mdef) {
        if (code_off == 0) {
            return;
        }
        DexCursor ccur(data, code_off);
        DexCodeItemHeader ch{};
        if (!ccur.Read(ch)) {
            return;
        }
        mdef.registers_size = ch.registers_size;
        mdef.ins_size = ch.ins_size;
        mdef.outs_size = ch.outs_size;
        mdef.insns.resize(ch.insns_size);
        for (uint32_t i = 0; i < ch.insns_size; ++i) {
            if (!ccur.Read(mdef.insns[i])) {
                mdef.insns.clear();
                return;
            }
        }
        if (ch.tries_size > 0) {
            if ((ch.insns_size & 1u) != 0) {
                uint16_t pad = 0;
                if (!ccur.Read(pad)) {
                    return;
                }
            }
            std::vector<DexTryItem> raw_tries(ch.tries_size);
            for (uint16_t i = 0; i < ch.tries_size; ++i) {
                if (!ccur.Read(raw_tries[i])) {
                    return;
                }
            }
            size_t handlers_base = ccur.Tell();
            for (const auto &rt : raw_tries) {
                DexDecodedTry dtry;
                dtry.start_addr = rt.start_addr;
                dtry.end_addr = rt.start_addr + rt.insn_count;
                DexCursor hcur(data, handlers_base + rt.handler_off);
                int32_t size = 0;
                if (hcur.ReadSleb128(size)) {
                    uint32_t count = static_cast<uint32_t>(size < 0 ? -size : size);
                    for (uint32_t k = 0; k < count; ++k) {
                        uint32_t type_idx = 0;
                        uint32_t addr = 0;
                        if (!hcur.ReadUleb128(type_idx) || !hcur.ReadUleb128(addr)) {
                            break;
                        }
                        dtry.handlers.push_back({std::string(dex->GetType(type_idx)), addr});
                    }
                    if (size <= 0) {
                        uint32_t catch_all = DEX_NO_INDEX;
                        if (hcur.ReadUleb128(catch_all)) {
                            dtry.catch_all_addr = catch_all;
                        }
                    }
                }
                mdef.tries.push_back(std::move(dtry));
            }
        }
    };

    if (hdr.class_defs_size > 0) {
        DexCursor cur(data, hdr.class_defs_off);
        dex->classes_.reserve(hdr.class_defs_size);
        for (uint32_t i = 0; i < hdr.class_defs_size; ++i) {
            DexClassDef cdef{};
            if (!cur.Read(cdef)) {
                return nullptr;
            }
            DexClassDefResolved cls;
            cls.descriptor = std::string(dex->GetType(cdef.class_idx));
            if (cdef.superclass_idx != DEX_NO_INDEX) {
                cls.superclass_descriptor = std::string(dex->GetType(cdef.superclass_idx));
            }
            cls.access_flags = cdef.access_flags;

            if (cdef.interfaces_off != 0) {
                DexCursor icur(data, cdef.interfaces_off);
                uint32_t icount = 0;
                if (icur.Read(icount)) {
                    for (uint32_t k = 0; k < icount; ++k) {
                        uint16_t tidx = 0;
                        if (!icur.Read(tidx)) {
                            break;
                        }
                        cls.interfaces.emplace_back(dex->GetType(tidx));
                    }
                }
            }

            if (cdef.class_data_off != 0) {
                DexCursor dcur(data, cdef.class_data_off);
                uint32_t sf_count = 0, if_count = 0, dm_count = 0, vm_count = 0;
                if (dcur.ReadUleb128(sf_count) && dcur.ReadUleb128(if_count) &&
                    dcur.ReadUleb128(dm_count) && dcur.ReadUleb128(vm_count)) {
                    auto read_fields = [&](uint32_t count, bool is_static,
                                           std::vector<DexFieldDef> &out) {
                        uint32_t field_idx = 0;
                        for (uint32_t k = 0; k < count; ++k) {
                            uint32_t diff = 0, flags = 0;
                            if (!dcur.ReadUleb128(diff) || !dcur.ReadUleb128(flags)) {
                                break;
                            }
                            field_idx += diff;
                            DexFieldDef f;
                            f.field_idx = field_idx;
                            f.access_flags = flags;
                            f.is_static = is_static;
                            if (const auto *ref = dex->GetFieldRef(field_idx)) {
                                f.class_descriptor = ref->class_descriptor;
                                f.name = ref->name;
                                f.type_descriptor = ref->type_descriptor;
                            }
                            out.push_back(std::move(f));
                        }
                    };
                    read_fields(sf_count, true, cls.static_fields);
                    read_fields(if_count, false, cls.instance_fields);

                    auto read_methods = [&](uint32_t count, std::vector<DexMethodDef> &out) {
                        uint32_t method_idx = 0;
                        for (uint32_t k = 0; k < count; ++k) {
                            uint32_t diff = 0, flags = 0, code_off = 0;
                            if (!dcur.ReadUleb128(diff) || !dcur.ReadUleb128(flags) ||
                                !dcur.ReadUleb128(code_off)) {
                                break;
                            }
                            method_idx += diff;
                            DexMethodDef m;
                            m.method_idx = method_idx;
                            m.access_flags = flags;
                            if (const auto *ref = dex->GetMethodRef(method_idx)) {
                                m.class_descriptor = ref->class_descriptor;
                                m.name = ref->name;
                                m.shorty = ref->shorty;
                                m.descriptor = ref->descriptor;
                                m.return_type = ref->return_type;
                                m.param_types = ref->param_types;
                            }
                            parse_code_item(code_off, m);
                            out.push_back(std::move(m));
                        }
                    };
                    read_methods(dm_count, cls.direct_methods);
                    read_methods(vm_count, cls.virtual_methods);
                }
            }

            dex->class_by_descriptor_[cls.descriptor] = dex->classes_.size();
            dex->classes_.push_back(std::move(cls));
        }
    }

    return dex;
}

const DexClassDefResolved *DexFile::FindClass(std::string_view name) const
{
    std::string norm = NormalizeDescriptor(name);
    auto it = class_by_descriptor_.find(norm);
    if (it == class_by_descriptor_.end()) {
        return nullptr;
    }
    return &classes_[it->second];
}

const DexMethodDef *DexFile::FindMethod(std::string_view class_name, std::string_view method_name,
                                        std::string_view signature) const
{
    const DexClassDefResolved *cls = FindClass(class_name);
    if (!cls) {
        return nullptr;
    }
    auto match = [&](const std::vector<DexMethodDef> &list) -> const DexMethodDef * {
        for (const auto &m : list) {
            if (m.name == method_name && (signature.empty() || m.descriptor == signature)) {
                return &m;
            }
        }
        return nullptr;
    };
    if (const auto *m = match(cls->virtual_methods)) {
        return m;
    }
    return match(cls->direct_methods);
}

std::string_view DexFile::GetString(uint32_t idx) const noexcept
{
    if (idx >= strings_.size()) {
        return {};
    }
    return strings_[idx];
}

std::string_view DexFile::GetType(uint32_t idx) const noexcept
{
    if (idx >= types_.size()) {
        return {};
    }
    return types_[idx];
}

const DexFieldRef *DexFile::GetFieldRef(uint32_t idx) const noexcept
{
    if (idx >= field_refs_.size()) {
        return nullptr;
    }
    return &field_refs_[idx];
}

const DexMethodRef *DexFile::GetMethodRef(uint32_t idx) const noexcept
{
    if (idx >= method_refs_.size()) {
        return nullptr;
    }
    return &method_refs_[idx];
}

} // namespace portage::package
