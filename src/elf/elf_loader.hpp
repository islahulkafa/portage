#pragma once

#include "arch/guest_context.hpp"
#include "elf_types.hpp"
#include "mem/address_space.hpp"
#include "rewrite/syscall_rewriter.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
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
    uintptr_t entry_point{0};
    uintptr_t app_entry_point{0};
    uintptr_t interpreter_base{0};
    std::string interpreter_path{};
    uintptr_t phdr_address{0};
    uint16_t phnum{0};
    uint16_t phentsize{0};
    bool is_pie{false};
    void *trampoline_island{nullptr};
    size_t trampoline_size{0};
    std::vector<LoadedSegment> segments;
    std::vector<arch::AuxvEntry> auxv_entries;
    std::unordered_map<std::string, uintptr_t> symbols;
    std::string soname{};
    std::string filepath{};
    std::vector<std::string> needed_libraries{};
    std::vector<uintptr_t> init_functions{};
    std::vector<uintptr_t> fini_functions{};

    [[nodiscard]] uintptr_t FindSymbol(std::string_view name) const;
};

class ElfLoader {
  public:
    using SymbolResolver = std::function<uintptr_t(std::string_view name)>;
    using DependencyLoader = std::function<void(const std::vector<std::string> &needed)>;

    explicit ElfLoader(mem::AddressSpace *as) noexcept;

    [[nodiscard]] std::variant<LoadedElf, LoaderError>
    LoadFromFile(const std::string &path, const portage::fs::Vfs *vfs = nullptr,
                 SymbolResolver resolver = nullptr, DependencyLoader dep_loader = nullptr) noexcept;
    [[nodiscard]] std::variant<LoadedElf, LoaderError>
    LoadFromMemory(std::span<const uint8_t> buffer, const portage::fs::Vfs *vfs = nullptr,
                   SymbolResolver resolver = nullptr,
                   DependencyLoader dep_loader = nullptr) noexcept;

  private:
    mem::AddressSpace *address_space_{nullptr};
};

} // namespace portage::elf
