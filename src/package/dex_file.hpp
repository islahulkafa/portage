#pragma once

#include "package/dex_types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::package {

struct DexCatchPair {
    std::string exception_descriptor;
    uint32_t addr{0};
};

struct DexDecodedTry {
    uint32_t start_addr{0};
    uint32_t end_addr{0};
    std::vector<DexCatchPair> handlers;
    uint32_t catch_all_addr{DEX_NO_INDEX};
};

struct DexFieldRef {
    std::string class_descriptor;
    std::string name;
    std::string type_descriptor;
};

struct DexMethodRef {
    std::string class_descriptor;
    std::string name;
    std::string shorty;
    std::string descriptor;
    std::string return_type;
    std::vector<std::string> param_types;
};

struct DexFieldDef {
    uint32_t field_idx{0};
    std::string class_descriptor;
    std::string name;
    std::string type_descriptor;
    uint32_t access_flags{0};
    bool is_static{false};
};

struct DexMethodDef {
    uint32_t method_idx{0};
    std::string class_descriptor;
    std::string name;
    std::string shorty;
    std::string descriptor;
    std::string return_type;
    std::vector<std::string> param_types;
    uint32_t access_flags{0};
    uint16_t registers_size{0};
    uint16_t ins_size{0};
    uint16_t outs_size{0};
    std::vector<uint16_t> insns;
    std::vector<DexDecodedTry> tries;
};

struct DexProtoRef {
    std::string shorty;
    std::string descriptor;
    std::string return_type;
    std::vector<std::string> param_types;
};

struct DexMethodHandleDef {
    DexMethodHandleType type{DexMethodHandleType::InvokeStatic};
    uint16_t field_or_method_id{0};
};

struct DexEncodedValue {
    enum class Kind : uint8_t {
        Null = 0,
        Boolean,
        Int,
        Long,
        Float,
        Double,
        String,
        Type,
        MethodType,
        MethodHandle,
        Field,
        Method,
        Enum,
        Array,
    };
    Kind kind{Kind::Null};
    uint64_t raw{0};
    uint32_t index{0};
    std::string text{};
    std::vector<DexEncodedValue> elements{};
};

struct DexCallSiteDef {
    uint32_t bootstrap_method_handle_idx{0};
    std::string method_name{};
    uint32_t method_type_proto_idx{0};
    std::vector<DexEncodedValue> extra_args{};
};

struct DexClassDefResolved {
    std::string descriptor;
    std::string superclass_descriptor;
    std::vector<std::string> interfaces;
    uint32_t access_flags{0};
    std::vector<DexFieldDef> static_fields;
    std::vector<DexFieldDef> instance_fields;
    std::vector<DexMethodDef> direct_methods;
    std::vector<DexMethodDef> virtual_methods;
    std::vector<DexEncodedValue> static_values;
};

class DexFile {
  public:
    [[nodiscard]] static std::shared_ptr<DexFile> Parse(std::span<const uint8_t> data);

    [[nodiscard]] static std::string NormalizeDescriptor(std::string_view name);
    [[nodiscard]] static std::string DescriptorToSlashName(std::string_view descriptor);

    [[nodiscard]] const DexClassDefResolved *FindClass(std::string_view name) const;
    [[nodiscard]] const DexMethodDef *FindMethod(std::string_view class_name,
                                                 std::string_view method_name,
                                                 std::string_view signature) const;

    [[nodiscard]] std::string_view GetString(uint32_t idx) const noexcept;
    [[nodiscard]] std::string_view GetType(uint32_t idx) const noexcept;
    [[nodiscard]] const DexProtoRef *GetProtoRef(uint32_t idx) const noexcept;
    [[nodiscard]] const DexFieldRef *GetFieldRef(uint32_t idx) const noexcept;
    [[nodiscard]] const DexMethodRef *GetMethodRef(uint32_t idx) const noexcept;
    [[nodiscard]] const DexMethodHandleDef *GetMethodHandle(uint32_t idx) const noexcept;
    [[nodiscard]] const DexCallSiteDef *GetCallSite(uint32_t idx) const noexcept;

    [[nodiscard]] const std::vector<DexClassDefResolved> &GetClasses() const noexcept
    {
        return classes_;
    }

  private:
    std::vector<std::string> strings_;
    std::vector<std::string> types_;
    std::vector<DexProtoRef> protos_;
    std::vector<DexFieldRef> field_refs_;
    std::vector<DexMethodRef> method_refs_;
    std::vector<DexMethodHandleDef> method_handles_;
    std::vector<DexCallSiteDef> call_sites_;
    std::vector<DexClassDefResolved> classes_;
    std::unordered_map<std::string, size_t> class_by_descriptor_;
};

} // namespace portage::package
