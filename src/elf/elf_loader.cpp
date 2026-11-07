#include "elf_loader.hpp"
#include "fs/vfs.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <windows.h>

namespace portage::elf {

namespace {

uintptr_t AlignDown(uintptr_t val, uintptr_t align) noexcept { return val & ~(align - 1); }

uintptr_t AlignUp(uintptr_t val, uintptr_t align) noexcept
{
    return (val + align - 1) & ~(align - 1);
}

} // namespace

ElfLoader::ElfLoader(mem::AddressSpace *as) noexcept : address_space_(as) {}

std::variant<LoadedElf, LoaderError> ElfLoader::LoadFromFile(const std::string &path,
                                                             const portage::fs::Vfs *vfs) noexcept
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return LoaderError::FileNotFound;
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(sizeof(Elf64_Ehdr))) {
        return LoaderError::FileTooSmall;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(buffer.data()), file_size);

    return LoadFromMemory(buffer, vfs);
}

std::variant<LoadedElf, LoaderError> ElfLoader::LoadFromMemory(std::span<const uint8_t> buffer,
                                                               const portage::fs::Vfs *vfs) noexcept
{
    if (buffer.size() < sizeof(Elf64_Ehdr)) {
        return LoaderError::FileTooSmall;
    }

    const auto *ehdr = reinterpret_cast<const Elf64_Ehdr *>(buffer.data());

    // 1. Validate magic and architecture
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return LoaderError::InvalidMagic;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return LoaderError::UnsupportedClass;
    }

    if (ehdr->e_machine != EM_X86_64) {
        return LoaderError::UnsupportedMachine;
    }

    const bool is_pie = (ehdr->e_type == ET_DYN);
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return LoaderError::UnsupportedType;
    }

    // 2. Scan PT_LOAD headers to determine address span
    if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr) > buffer.size()) {
        return LoaderError::FileTooSmall;
    }

    const auto *phdrs = reinterpret_cast<const Elf64_Phdr *>(buffer.data() + ehdr->e_phoff);
    uintptr_t min_vaddr = UINT64_MAX;
    uintptr_t max_vaddr = 0;
    size_t load_count = 0;
    std::string interp_path{};

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            min_vaddr = std::min<uintptr_t>(min_vaddr, phdrs[i].p_vaddr);
            max_vaddr = std::max<uintptr_t>(max_vaddr, phdrs[i].p_vaddr + phdrs[i].p_memsz);
            load_count++;
        } else if (phdrs[i].p_type == PT_INTERP) {
            if (phdrs[i].p_offset + phdrs[i].p_filesz <= buffer.size() && phdrs[i].p_filesz > 1) {
                const char *interp_str =
                    reinterpret_cast<const char *>(buffer.data() + phdrs[i].p_offset);
                size_t len = phdrs[i].p_filesz;
                if (interp_str[len - 1] == '\0') {
                    len--;
                }
                interp_path.assign(interp_str, len);
            }
        }
    }

    if (load_count == 0) {
        return LoaderError::NoLoadSegments;
    }

    // 3. Compute load bias and reserve virtual address space
    min_vaddr = AlignDown(min_vaddr, mem::PAGE_SIZE_4KB);
    max_vaddr = AlignUp(max_vaddr, mem::PAGE_SIZE_4KB);
    const size_t total_span = max_vaddr - min_vaddr;

    uintptr_t load_bias = 0;
    void *reserved_base = nullptr;

    if (is_pie) {
        reserved_base = address_space_->Reserve(nullptr, total_span);
        if (!reserved_base) {
            std::cerr << "[ElfLoader] PIE Reserve failed, err=" << GetLastError() << "\n";
            return LoaderError::MemoryAllocationFailed;
        }
        load_bias = reinterpret_cast<uintptr_t>(reserved_base) - min_vaddr;
    } else {
        reserved_base = address_space_->Reserve(reinterpret_cast<void *>(min_vaddr), total_span);
        if (reserved_base != reinterpret_cast<void *>(min_vaddr)) {
            std::cerr << "[ElfLoader] EXEC Reserve failed for 0x" << std::hex << min_vaddr
                      << " got " << reserved_base << " err=" << std::dec << GetLastError() << "\n";
            return LoaderError::MemoryAllocationFailed;
        }
        load_bias = 0;
    }

    // 4. Allocate adjacent trampoline island within +/- 2GB
    constexpr size_t TRAMPOLINE_ISLAND_SIZE = 65536;
    uintptr_t entry_point = load_bias + ehdr->e_entry;
    void *island =
        address_space_->AllocateNear(reinterpret_cast<void *>(entry_point), TRAMPOLINE_ISLAND_SIZE);
    if (!island) {
        std::cerr << "[ElfLoader] Trampoline island allocation failed, err=" << GetLastError()
                  << "\n";
        return LoaderError::TrampolineAllocationFailed;
    }

    rewrite::SyscallRewriter rewriter(island, TRAMPOLINE_ISLAND_SIZE);
    LoadedElf loaded_elf{};
    loaded_elf.base_address = reinterpret_cast<uintptr_t>(reserved_base);
    loaded_elf.entry_point = entry_point;
    loaded_elf.app_entry_point = entry_point;
    loaded_elf.interpreter_path = interp_path;
    loaded_elf.is_pie = is_pie;
    loaded_elf.phnum = ehdr->e_phnum;
    loaded_elf.phentsize = ehdr->e_phentsize;
    loaded_elf.trampoline_island = island;
    loaded_elf.trampoline_size = TRAMPOLINE_ISLAND_SIZE;

    // 5. Commit and map each PT_LOAD segment
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const auto &phdr = phdrs[i];
        if (phdr.p_type == PT_LOAD) {
            uintptr_t seg_vaddr = load_bias + phdr.p_vaddr;
            // Commit with read/write/execute during loading and patching
            if (!address_space_->Commit(reinterpret_cast<void *>(seg_vaddr), phdr.p_memsz,
                                        PF_R | PF_W | PF_X)) {
                std::cerr << "[ElfLoader] Commit failed for 0x" << std::hex << seg_vaddr
                          << " size=0x" << phdr.p_memsz << " err=" << std::dec << GetLastError()
                          << "\n";
                return LoaderError::MemoryAllocationFailed;
            }

            if (phdr.p_filesz > 0 && phdr.p_offset + phdr.p_filesz <= buffer.size()) {
                std::memcpy(reinterpret_cast<void *>(seg_vaddr), buffer.data() + phdr.p_offset,
                            phdr.p_filesz);
            }

            if (phdr.p_memsz > phdr.p_filesz) {
                std::memset(reinterpret_cast<void *>(seg_vaddr + phdr.p_filesz), 0,
                            phdr.p_memsz - phdr.p_filesz);
            }

            if ((phdr.p_flags & PF_X) != 0 && phdr.p_filesz > 0) {
                std::span<uint8_t> code(reinterpret_cast<uint8_t *>(seg_vaddr), phdr.p_filesz);
                [[maybe_unused]] size_t patched = rewriter.RewriteSegment(code, seg_vaddr);
            }

            // Apply final protection flags
            (void)address_space_->Commit(reinterpret_cast<void *>(seg_vaddr), phdr.p_memsz,
                                         phdr.p_flags);

            loaded_elf.segments.push_back(
                {.vaddr = seg_vaddr, .mem_size = phdr.p_memsz, .flags = phdr.p_flags});
        } else if (phdr.p_type == PT_PHDR) {
            loaded_elf.phdr_address = load_bias + phdr.p_vaddr;
        }
    }

    if (loaded_elf.phdr_address == 0) {
        loaded_elf.phdr_address = load_bias + ehdr->e_phoff;
    }

    // 6. If PT_INTERP is present and VFS is available, load the interpreter
    if (vfs != nullptr && !interp_path.empty()) {
        auto resolved = vfs->Resolve(interp_path);
        std::filesystem::path interp_host_path = resolved.host_path;

        std::ifstream interp_file(interp_host_path, std::ios::binary | std::ios::ate);
        if (!interp_file.is_open()) {
            std::cerr << "[ElfLoader] Cannot open interpreter: " << interp_host_path << "\n";
            return LoaderError::InterpreterNotFound;
        }

        const auto interp_file_size = interp_file.tellg();
        if (interp_file_size < static_cast<std::streampos>(sizeof(Elf64_Ehdr))) {
            return LoaderError::FileTooSmall;
        }

        std::vector<uint8_t> interp_buffer(static_cast<size_t>(interp_file_size));
        interp_file.seekg(0, std::ios::beg);
        interp_file.read(reinterpret_cast<char *>(interp_buffer.data()), interp_file_size);

        const auto *interp_ehdr = reinterpret_cast<const Elf64_Ehdr *>(interp_buffer.data());
        if (interp_ehdr->e_ident[EI_MAG0] != ELFMAG0 || interp_ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
            interp_ehdr->e_ident[EI_MAG2] != ELFMAG2 || interp_ehdr->e_ident[EI_MAG3] != ELFMAG3 ||
            interp_ehdr->e_machine != EM_X86_64 || interp_ehdr->e_type != ET_DYN) {
            return LoaderError::UnsupportedType;
        }

        const auto *interp_phdrs =
            reinterpret_cast<const Elf64_Phdr *>(interp_buffer.data() + interp_ehdr->e_phoff);
        uintptr_t interp_min_vaddr = UINT64_MAX;
        uintptr_t interp_max_vaddr = 0;

        for (uint16_t i = 0; i < interp_ehdr->e_phnum; i++) {
            if (interp_phdrs[i].p_type == PT_LOAD) {
                interp_min_vaddr = std::min<uintptr_t>(interp_min_vaddr, interp_phdrs[i].p_vaddr);
                interp_max_vaddr = std::max<uintptr_t>(
                    interp_max_vaddr, interp_phdrs[i].p_vaddr + interp_phdrs[i].p_memsz);
            }
        }

        interp_min_vaddr = AlignDown(interp_min_vaddr, mem::PAGE_SIZE_4KB);
        interp_max_vaddr = AlignUp(interp_max_vaddr, mem::PAGE_SIZE_4KB);
        const size_t interp_span = interp_max_vaddr - interp_min_vaddr;

        void *interp_reserved = address_space_->Reserve(nullptr, interp_span);
        if (!interp_reserved) {
            return LoaderError::MemoryAllocationFailed;
        }

        uintptr_t interp_bias = reinterpret_cast<uintptr_t>(interp_reserved) - interp_min_vaddr;
        loaded_elf.interpreter_base = reinterpret_cast<uintptr_t>(interp_reserved);
        loaded_elf.entry_point = interp_bias + interp_ehdr->e_entry;

        for (uint16_t i = 0; i < interp_ehdr->e_phnum; i++) {
            const auto &phdr = interp_phdrs[i];
            if (phdr.p_type == PT_LOAD) {
                uintptr_t seg_vaddr = interp_bias + phdr.p_vaddr;
                if (!address_space_->Commit(reinterpret_cast<void *>(seg_vaddr), phdr.p_memsz,
                                            PF_R | PF_W | PF_X)) {
                    return LoaderError::MemoryAllocationFailed;
                }

                if (phdr.p_filesz > 0 && phdr.p_offset + phdr.p_filesz <= interp_buffer.size()) {
                    std::memcpy(reinterpret_cast<void *>(seg_vaddr),
                                interp_buffer.data() + phdr.p_offset, phdr.p_filesz);
                }

                if (phdr.p_memsz > phdr.p_filesz) {
                    std::memset(reinterpret_cast<void *>(seg_vaddr + phdr.p_filesz), 0,
                                phdr.p_memsz - phdr.p_filesz);
                }

                if ((phdr.p_flags & PF_X) != 0 && phdr.p_filesz > 0) {
                    std::span<uint8_t> code(reinterpret_cast<uint8_t *>(seg_vaddr), phdr.p_filesz);
                    [[maybe_unused]] size_t patched = rewriter.RewriteSegment(code, seg_vaddr);
                }

                (void)address_space_->Commit(reinterpret_cast<void *>(seg_vaddr), phdr.p_memsz,
                                             phdr.p_flags);

                loaded_elf.segments.push_back(
                    {.vaddr = seg_vaddr, .mem_size = phdr.p_memsz, .flags = phdr.p_flags});
            }
        }
    }

    // 7. Initialize heap break
    address_space_->InitBrk(load_bias + max_vaddr);

    // 8. Populate auxv entries
    loaded_elf.auxv_entries = {
        {AT_PHDR, loaded_elf.phdr_address},
        {AT_PHENT, ehdr->e_phentsize},
        {AT_PHNUM, ehdr->e_phnum},
        {AT_PAGESZ, mem::PAGE_SIZE_4KB},
        {AT_ENTRY, loaded_elf.app_entry_point},
        {AT_BASE, loaded_elf.interpreter_base},
        {AT_FLAGS, 0},
        {AT_HWCAP, 0},
        {AT_CLKTCK, 100},
        {AT_SECURE, 0},
    };

    // 9. Populate symbols from section headers (DYNSYM and SYMTAB)
    if (ehdr->e_shoff != 0 && ehdr->e_shnum > 0 &&
        ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) <= buffer.size()) {
        const auto *shdrs = reinterpret_cast<const Elf64_Shdr *>(buffer.data() + ehdr->e_shoff);
        for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
            const auto &sh = shdrs[i];
            if ((sh.sh_type == SHT_DYNSYM || sh.sh_type == SHT_SYMTAB) &&
                sh.sh_entsize == sizeof(Elf64_Sym)) {
                if (sh.sh_link < ehdr->e_shnum && sh.sh_offset + sh.sh_size <= buffer.size()) {
                    const auto &str_shdr = shdrs[sh.sh_link];
                    if (str_shdr.sh_offset + str_shdr.sh_size <= buffer.size()) {
                        const char *strtab =
                            reinterpret_cast<const char *>(buffer.data() + str_shdr.sh_offset);
                        size_t num_syms = sh.sh_size / sizeof(Elf64_Sym);
                        const auto *syms =
                            reinterpret_cast<const Elf64_Sym *>(buffer.data() + sh.sh_offset);
                        for (size_t s = 0; s < num_syms; s++) {
                            const auto &sym = syms[s];
                            if (sym.st_name < str_shdr.sh_size && sym.st_value != 0) {
                                const char *name = strtab + sym.st_name;
                                if (*name) {
                                    uintptr_t sym_addr =
                                        is_pie ? (load_bias + sym.st_value) : sym.st_value;
                                    loaded_elf.symbols[name] = sym_addr;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 8. Process dynamic relocations if PT_DYNAMIC is present
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            uintptr_t dyn_addr = load_bias + phdrs[i].p_vaddr;
            const auto *dyn = reinterpret_cast<const Elf64_Dyn *>(dyn_addr);

            uintptr_t rela_addr = 0;
            size_t rela_sz = 0;
            uintptr_t jmprel_addr = 0;
            size_t jmprel_sz = 0;
            uintptr_t symtab_addr = 0;
            uintptr_t strtab_addr = 0;

            for (size_t d = 0; dyn[d].d_tag != DT_NULL; d++) {
                switch (dyn[d].d_tag) {
                case DT_RELA:
                    rela_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_RELASZ:
                    rela_sz = dyn[d].d_un.d_val;
                    break;
                case DT_JMPREL:
                    jmprel_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_PLTRELSZ:
                    jmprel_sz = dyn[d].d_un.d_val;
                    break;
                case DT_SYMTAB:
                    symtab_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_STRTAB:
                    strtab_addr = dyn[d].d_un.d_ptr;
                    break;
                default:
                    break;
                }
            }

            auto resolve_ptr = [&](uintptr_t ptr) -> uintptr_t {
                if (ptr == 0)
                    return 0;
                if (is_pie && ptr < load_bias)
                    return load_bias + ptr;
                return ptr;
            };

            rela_addr = resolve_ptr(rela_addr);
            jmprel_addr = resolve_ptr(jmprel_addr);
            symtab_addr = resolve_ptr(symtab_addr);
            strtab_addr = resolve_ptr(strtab_addr);

            const auto *symtab = reinterpret_cast<const Elf64_Sym *>(symtab_addr);
            const char *strtab = reinterpret_cast<const char *>(strtab_addr);

            auto apply_relas = [&](uintptr_t table_addr, size_t total_sz) {
                if (!table_addr || !total_sz)
                    return;
                size_t count = total_sz / sizeof(Elf64_Rela);
                const auto *relas = reinterpret_cast<const Elf64_Rela *>(table_addr);
                for (size_t r = 0; r < count; r++) {
                    const auto &rel = relas[r];
                    uintptr_t patch_addr = load_bias + rel.r_offset;
                    uint32_t type = ELF64_R_TYPE(rel.r_info);
                    uint32_t sym_idx = ELF64_R_SYM(rel.r_info);

                    if (type == R_X86_64_RELATIVE) {
                        *reinterpret_cast<uintptr_t *>(patch_addr) = load_bias + rel.r_addend;
                    } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT ||
                               type == R_X86_64_64) {
                        uintptr_t sym_addr = 0;
                        const char *sym_name = "unknown";
                        if (sym_idx > 0 && symtab && strtab) {
                            const auto &s = symtab[sym_idx];
                            sym_name = strtab + s.st_name;
                            if (s.st_value != 0) {
                                sym_addr = is_pie ? (load_bias + s.st_value) : s.st_value;
                            } else {
                                sym_addr = loaded_elf.FindSymbol(sym_name);
                            }
                        }

                        if (sym_addr != 0) {
                            if (type == R_X86_64_64) {
                                *reinterpret_cast<uintptr_t *>(patch_addr) =
                                    sym_addr + rel.r_addend;
                            } else {
                                *reinterpret_cast<uintptr_t *>(patch_addr) = sym_addr;
                            }
                        }
                    }
                }
            };

            apply_relas(rela_addr, rela_sz);
            apply_relas(jmprel_addr, jmprel_sz);
            break;
        }
    }

    return loaded_elf;
}

uintptr_t LoadedElf::FindSymbol(std::string_view name) const
{
    auto it = symbols.find(std::string(name));
    if (it != symbols.end()) {
        return it->second;
    }
    return ndk::NdkSymbolRegistry::Instance().Lookup(name);
}

} // namespace portage::elf
