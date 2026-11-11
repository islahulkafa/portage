#pragma once

#include "arch/guest_context.hpp"
#include "elf_types.hpp"
#include "mem/address_space.hpp"
#include "rewrite/syscall_rewriter.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace portage::fs {
class Vfs;
}

namespace portage::elf {

enum class LoaderError {
    FileNotFound,
    FileTooSmall,
    InvalidMagic,
    UnsupportedClass,
    UnsupportedMachine,
    UnsupportedType,
    MemoryAllocationFailed,
    TrampolineAllocationFailed,
    NoLoadSegments,
    InterpreterNotFound,
};

struct LoadedSegment {
    uintptr_t vaddr{0};
    size_t mem_size{0};
    uint32_t flags{0};
};

struct LoadedElf {
    uintptr_t base_address{0};
    uintptr_t load_bias{0};
    uintptr_t entry_point{0};
    uintptr_t app_entry_point{0};
    uintptr_t interpreter_base{0};
    std::string interpreter_path{};
    uintptr_t phdr_address{0};
    uintptr_t dynamic_vaddr{0};
    uint16_t phnum{0};
    uint16_t phentsize{0};
    bool is_pie{false};
    void *trampoline_island{nullptr};
    size_t trampoline_size{0};
    uintptr_t tls_image_addr{0};
    size_t tls_filesz{0};
    size_t tls_memsz{0};
    size_t tls_align{16};
    size_t tls_modid{0};
    uintptr_t relro_vaddr{0};
    size_t relro_memsz{0};
    std::vector<Elf64_Phdr> phdrs;
    std::vector<LoadedSegment> segments;
    std::vector<arch::AuxvEntry> auxv_entries;
    std::unordered_map<std::string, uintptr_t> symbols;
    std::unordered_map<std::string, size_t> symbol_sizes;
    std::unordered_map<std::string, uintptr_t> tls_symbols;
    std::string soname{};
    std::string filepath{};
    std::vector<std::string> needed_libraries{};
    std::vector<std::string> rpath_entries{};
    std::vector<std::string> runpath_entries{};
    std::vector<uintptr_t> init_functions{};
    std::vector<uintptr_t> fini_functions{};
    std::vector<std::shared_ptr<tlsdesc_dynamic_arg>> tlsdesc_args{};

    [[nodiscard]] uintptr_t FindSymbol(std::string_view name) const;
};

struct LoadOptions {
    bool lazy_binding{false};
    void *reserved_addr{nullptr};
    size_t reserved_size{0};
};

class ElfLoader {
  public:
    using SymbolResolver = std::function<uintptr_t(std::string_view name)>;
    using DependencyLoader = std::function<void(const std::vector<std::string> &needed)>;

    explicit ElfLoader(mem::AddressSpace *as) noexcept;

    [[nodiscard]] std::variant<LoadedElf, LoaderError>
    LoadFromFile(const std::string &path, const portage::fs::Vfs *vfs = nullptr,
                 SymbolResolver resolver = nullptr, DependencyLoader dep_loader = nullptr,
                 LoadOptions options = {}) noexcept;
    [[nodiscard]] std::variant<LoadedElf, LoaderError>
    LoadFromMemory(std::span<const uint8_t> buffer, const portage::fs::Vfs *vfs = nullptr,
                   SymbolResolver resolver = nullptr, DependencyLoader dep_loader = nullptr,
                   std::string_view origin_dir = "", LoadOptions options = {}) noexcept;

  private:
    mem::AddressSpace *address_space_{nullptr};
};

} // namespace portage::elf
