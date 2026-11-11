#include "elf_loader.hpp"
#include "elf/link_map.hpp"
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

std::vector<std::string> ParseAndExpandSearchPaths(std::string_view raw, std::string_view origin)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t sep = raw.find(':', pos);
        std::string_view token =
            (sep == std::string_view::npos) ? raw.substr(pos) : raw.substr(pos, sep - pos);
        if (!token.empty()) {
            std::string expanded(token);
            auto replace_all = [&](std::string_view needle) {
                size_t idx = 0;
                while ((idx = expanded.find(needle, idx)) != std::string::npos) {
                    expanded.replace(idx, needle.size(), origin);
                    idx += origin.size();
                }
            };
            replace_all("${ORIGIN}");
            replace_all("$ORIGIN");
            out.push_back(std::move(expanded));
        }
        if (sep == std::string_view::npos) {
            break;
        }
        pos = sep + 1;
    }
    return out;
}

} // namespace

ElfLoader::ElfLoader(mem::AddressSpace *as) noexcept : address_space_(as) {}

std::variant<LoadedElf, LoaderError> ElfLoader::LoadFromFile(const std::string &path,
                                                             const portage::fs::Vfs *vfs,
                                                             SymbolResolver resolver,
                                                             DependencyLoader dep_loader,
                                                             LoadOptions options) noexcept
{
    std::filesystem::path host_path = path;
    if (vfs) {
        auto resolved = vfs->Resolve(path);
        host_path = resolved.host_path;
    }

    std::ifstream file(host_path, std::ios::binary | std::ios::ate);
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

    std::string origin_dir = host_path.parent_path().string();
    auto result = LoadFromMemory(buffer, vfs, resolver, dep_loader, origin_dir, options);
    if (std::holds_alternative<LoadedElf>(result)) {
        std::get<LoadedElf>(result).filepath = path;
    }
    return result;
}

std::variant<LoadedElf, LoaderError>
ElfLoader::LoadFromMemory(std::span<const uint8_t> buffer, const portage::fs::Vfs *vfs,
                          SymbolResolver resolver, DependencyLoader dep_loader,
                          std::string_view origin_dir, LoadOptions options) noexcept
{
    if (buffer.size() < sizeof(Elf64_Ehdr)) {
        return LoaderError::FileTooSmall;
    }

    const auto *ehdr = reinterpret_cast<const Elf64_Ehdr *>(buffer.data());

    // Validate magic and architecture
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

    // Scan PT_LOAD headers to determine address span
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

    // Compute load bias and reserve virtual address space
    min_vaddr = AlignDown(min_vaddr, mem::PAGE_SIZE_4KB);
    max_vaddr = AlignUp(max_vaddr, mem::PAGE_SIZE_4KB);
    const size_t total_span = max_vaddr - min_vaddr;

    uintptr_t load_bias = 0;
    void *reserved_base = nullptr;

    if (is_pie) {
        if (options.reserved_addr != nullptr && options.reserved_size >= total_span) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(options.reserved_addr, &mbi, sizeof(mbi)) != 0 &&
                (mbi.State == MEM_RESERVE || mbi.State == MEM_COMMIT)) {
                reserved_base = options.reserved_addr;
            } else {
                reserved_base = address_space_->Reserve(options.reserved_addr, total_span);
            }
        } else {
            reserved_base = address_space_->Reserve(nullptr, total_span);
        }
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

    // Allocate adjacent trampoline island within +/- 2GB
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
    loaded_elf.load_bias = load_bias;
    loaded_elf.entry_point = entry_point;
    loaded_elf.app_entry_point = entry_point;
    loaded_elf.interpreter_path = interp_path;
    loaded_elf.is_pie = is_pie;
    loaded_elf.phnum = ehdr->e_phnum;
    loaded_elf.phentsize = ehdr->e_phentsize;
    loaded_elf.trampoline_island = island;
    loaded_elf.trampoline_size = TRAMPOLINE_ISLAND_SIZE;
    loaded_elf.phdrs.assign(phdrs, phdrs + ehdr->e_phnum);

    // Commit and map each PT_LOAD segment
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
        } else if (phdr.p_type == PT_TLS) {
            loaded_elf.tls_image_addr = load_bias + phdr.p_vaddr;
            loaded_elf.tls_filesz = phdr.p_filesz;
            loaded_elf.tls_memsz = phdr.p_memsz;
            loaded_elf.tls_align = std::max<size_t>(phdr.p_align, 16);
            loaded_elf.tls_modid = LinkMap::Instance().RegisterTlsModule(
                loaded_elf.tls_image_addr, loaded_elf.tls_filesz, loaded_elf.tls_memsz,
                loaded_elf.tls_align);
        } else if (phdr.p_type == PT_GNU_RELRO) {
            loaded_elf.relro_vaddr = load_bias + phdr.p_vaddr;
            loaded_elf.relro_memsz = phdr.p_memsz;
        }
    }

    if (loaded_elf.phdr_address == 0) {
        loaded_elf.phdr_address = load_bias + ehdr->e_phoff;
    }

    // Load dynamic interpreter if PT_INTERP is present and VFS is available
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

    // Initialize heap break
    address_space_->InitBrk(load_bias + max_vaddr);

    // Populate auxv entries
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

    // Populate symbols from section headers (DYNSYM and SYMTAB)
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
                            if (sym.st_name < str_shdr.sh_size) {
                                const char *name = strtab + sym.st_name;
                                if (*name) {
                                    if (ELF64_ST_TYPE(sym.st_info) == STT_TLS &&
                                        sym.st_shndx != 0) {
                                        loaded_elf.tls_symbols[name] = sym.st_value;
                                    } else if (sym.st_value != 0) {
                                        uintptr_t sym_addr =
                                            is_pie ? (load_bias + sym.st_value) : sym.st_value;
                                        loaded_elf.symbols[name] = sym_addr;
                                        if (sym.st_size > 0) {
                                            loaded_elf.symbol_sizes[name] = sym.st_size;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Process dynamic relocations if PT_DYNAMIC is present
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            uintptr_t dyn_addr = load_bias + phdrs[i].p_vaddr;
            loaded_elf.dynamic_vaddr = dyn_addr;
            const auto *dyn = reinterpret_cast<const Elf64_Dyn *>(dyn_addr);

            uintptr_t rela_addr = 0;
            size_t rela_sz = 0;
            uintptr_t jmprel_addr = 0;
            size_t jmprel_sz = 0;
            uintptr_t symtab_addr = 0;
            uintptr_t strtab_addr = 0;
            uintptr_t hash_addr = 0;
            uintptr_t gnu_hash_addr = 0;
            uintptr_t init_addr = 0;
            uintptr_t fini_addr = 0;
            uintptr_t init_array_addr = 0;
            size_t init_array_sz = 0;
            uintptr_t fini_array_addr = 0;
            size_t fini_array_sz = 0;
            uint64_t dt_flags = 0;
            uint64_t dt_flags_1 = 0;
            std::vector<uint64_t> needed_offsets;
            std::vector<uint64_t> rpath_offsets;
            std::vector<uint64_t> runpath_offsets;
            uint64_t soname_offset = 0;
            bool has_soname = false;

            for (size_t d = 0; dyn[d].d_tag != DT_NULL; d++) {
                switch (dyn[d].d_tag) {
                case DT_NEEDED:
                    needed_offsets.push_back(dyn[d].d_un.d_val);
                    break;
                case DT_RPATH:
                    rpath_offsets.push_back(dyn[d].d_un.d_val);
                    break;
                case DT_RUNPATH:
                    runpath_offsets.push_back(dyn[d].d_un.d_val);
                    break;
                case DT_SONAME:
                    soname_offset = dyn[d].d_un.d_val;
                    has_soname = true;
                    break;
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
                case DT_HASH:
                    hash_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_GNU_HASH:
                    gnu_hash_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_INIT:
                    init_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_FINI:
                    fini_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_INIT_ARRAY:
                    init_array_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_INIT_ARRAYSZ:
                    init_array_sz = dyn[d].d_un.d_val;
                    break;
                case DT_FINI_ARRAY:
                    fini_array_addr = dyn[d].d_un.d_ptr;
                    break;
                case DT_FINI_ARRAYSZ:
                    fini_array_sz = dyn[d].d_un.d_val;
                    break;
                case DT_FLAGS:
                    dt_flags = dyn[d].d_un.d_val;
                    break;
                case DT_FLAGS_1:
                    dt_flags_1 = dyn[d].d_un.d_val;
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
            hash_addr = resolve_ptr(hash_addr);
            gnu_hash_addr = resolve_ptr(gnu_hash_addr);
            init_addr = resolve_ptr(init_addr);
            fini_addr = resolve_ptr(fini_addr);
            init_array_addr = resolve_ptr(init_array_addr);
            fini_array_addr = resolve_ptr(fini_array_addr);

            const auto *symtab = reinterpret_cast<const Elf64_Sym *>(symtab_addr);
            const char *strtab = reinterpret_cast<const char *>(strtab_addr);

            if (loaded_elf.symbols.empty() && symtab != nullptr && strtab != nullptr) {
                if (gnu_hash_addr != 0) {
                    const auto *gnu_hdr =
                        reinterpret_cast<const Elf64_GnuHashHeader *>(gnu_hash_addr);
                    const uint64_t *bloom = reinterpret_cast<const uint64_t *>(gnu_hdr + 1);
                    const uint32_t *buckets =
                        reinterpret_cast<const uint32_t *>(bloom + gnu_hdr->bloom_size);
                    const uint32_t *chains = buckets + gnu_hdr->nbuckets;

                    for (uint32_t b = 0; b < gnu_hdr->nbuckets; ++b) {
                        uint32_t sym_idx = buckets[b];
                        if (sym_idx == 0 || sym_idx < gnu_hdr->symoffset) {
                            continue;
                        }
                        while (true) {
                            const auto &sym = symtab[sym_idx];
                            if (sym.st_name != 0) {
                                const char *name = strtab + sym.st_name;
                                if (*name) {
                                    if (ELF64_ST_TYPE(sym.st_info) == STT_TLS &&
                                        sym.st_shndx != 0) {
                                        loaded_elf.tls_symbols[name] = sym.st_value;
                                    } else if (sym.st_value != 0) {
                                        uintptr_t sym_addr =
                                            is_pie ? (load_bias + sym.st_value) : sym.st_value;
                                        loaded_elf.symbols[name] = sym_addr;
                                        if (sym.st_size > 0) {
                                            loaded_elf.symbol_sizes[name] = sym.st_size;
                                        }
                                    }
                                }
                            }
                            uint32_t chain_val = chains[sym_idx - gnu_hdr->symoffset];
                            if ((chain_val & 1) != 0) {
                                break;
                            }
                            ++sym_idx;
                        }
                    }
                } else if (hash_addr != 0) {
                    const auto *hash_hdr = reinterpret_cast<const uint32_t *>(hash_addr);
                    uint32_t nchain = hash_hdr[1];
                    for (uint32_t s = 0; s < nchain; s++) {
                        const auto &sym = symtab[s];
                        if (sym.st_name != 0) {
                            const char *name = strtab + sym.st_name;
                            if (*name) {
                                if (ELF64_ST_TYPE(sym.st_info) == STT_TLS && sym.st_shndx != 0) {
                                    loaded_elf.tls_symbols[name] = sym.st_value;
                                } else if (sym.st_value != 0) {
                                    uintptr_t sym_addr =
                                        is_pie ? (load_bias + sym.st_value) : sym.st_value;
                                    loaded_elf.symbols[name] = sym_addr;
                                }
                            }
                        }
                    }
                }
            }

            if (strtab) {
                if (has_soname) {
                    loaded_elf.soname = strtab + soname_offset;
                }
                for (uint64_t off : needed_offsets) {
                    loaded_elf.needed_libraries.emplace_back(strtab + off);
                }
                for (uint64_t off : rpath_offsets) {
                    auto parsed = ParseAndExpandSearchPaths(strtab + off, origin_dir);
                    loaded_elf.rpath_entries.insert(loaded_elf.rpath_entries.end(), parsed.begin(),
                                                    parsed.end());
                }
                for (uint64_t off : runpath_offsets) {
                    auto parsed = ParseAndExpandSearchPaths(strtab + off, origin_dir);
                    loaded_elf.runpath_entries.insert(loaded_elf.runpath_entries.end(),
                                                      parsed.begin(), parsed.end());
                }
            }

            if (dep_loader && !loaded_elf.needed_libraries.empty()) {
                bool pushed_runpaths =
                    !loaded_elf.rpath_entries.empty() || !loaded_elf.runpath_entries.empty();
                if (pushed_runpaths) {
                    LinkMap::Instance().PushModuleRunpaths(loaded_elf.rpath_entries,
                                                           loaded_elf.runpath_entries);
                }
                dep_loader(loaded_elf.needed_libraries);
                if (pushed_runpaths) {
                    LinkMap::Instance().PopModuleRunpaths();
                }
            }

            const bool has_bind_now =
                ((dt_flags & DF_BIND_NOW) != 0) || ((dt_flags_1 & DF_1_NOW) != 0);
            const bool lazy_plt = options.lazy_binding && !has_bind_now;

            auto apply_relas = [&](uintptr_t table_addr, size_t total_sz, bool is_jmprel) {
                if (!table_addr || !total_sz)
                    return;
                size_t count = total_sz / sizeof(Elf64_Rela);
                const auto *relas = reinterpret_cast<const Elf64_Rela *>(table_addr);
                for (size_t r = 0; r < count; r++) {
                    const auto &rel = relas[r];
                    uintptr_t patch_addr = load_bias + rel.r_offset;
                    uint32_t type = ELF64_R_TYPE(rel.r_info);
                    uint32_t sym_idx = ELF64_R_SYM(rel.r_info);

                    if (is_jmprel && lazy_plt && type == R_X86_64_JUMP_SLOT && sym_idx > 0 &&
                        symtab && strtab) {
                        const auto &s = symtab[sym_idx];
                        const char *sym_name = strtab + s.st_name;
                        const bool is_weak = (ELF64_ST_BIND(s.st_info) == STB_WEAK);
                        if (is_weak && (resolver == nullptr || resolver(sym_name) == 0)) {
                            *reinterpret_cast<uintptr_t *>(patch_addr) = 0;
                            continue;
                        }
                        uintptr_t stub =
                            LinkMap::Instance().AllocateLazyPltTrampoline(patch_addr, sym_name);
                        if (stub != 0) {
                            *reinterpret_cast<uintptr_t *>(patch_addr) = stub;
                            continue;
                        }
                    }

                    if (type == R_X86_64_RELATIVE) {
                        *reinterpret_cast<uintptr_t *>(patch_addr) = load_bias + rel.r_addend;
                    } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT ||
                               type == R_X86_64_64) {
                        uintptr_t sym_addr = 0;
                        const char *sym_name = "";
                        bool is_weak = false;
                        unsigned char sym_type = STT_NOTYPE;
                        if (sym_idx > 0 && symtab && strtab) {
                            const auto &s = symtab[sym_idx];
                            sym_name = strtab + s.st_name;
                            is_weak = (ELF64_ST_BIND(s.st_info) == STB_WEAK);
                            sym_type = ELF64_ST_TYPE(s.st_info);
                            if (s.st_shndx != 0 && s.st_value != 0) {
                                sym_addr = is_pie ? (load_bias + s.st_value) : s.st_value;
                            } else {
                                if (resolver) {
                                    sym_addr = resolver(sym_name);
                                }
                                if (sym_addr == 0) {
                                    sym_addr = loaded_elf.FindSymbol(sym_name);
                                }
                            }
                        }

                        if (sym_addr == 0 && !is_weak && sym_name[0] != '\0') {
                            if (std::find(loaded_elf.unresolved_symbols.begin(),
                                          loaded_elf.unresolved_symbols.end(),
                                          sym_name) == loaded_elf.unresolved_symbols.end()) {
                                loaded_elf.unresolved_symbols.emplace_back(sym_name);
                            }
                            if (options.trap_unresolved_symbols &&
                                (sym_type == STT_FUNC || sym_type == STT_NOTYPE)) {
                                sym_addr = LinkMap::Instance().AllocateUnresolvedTrap(sym_name);
                            }
                        }

                        if (sym_addr != 0) {
                            if (type == R_X86_64_64) {
                                *reinterpret_cast<uintptr_t *>(patch_addr) =
                                    sym_addr + rel.r_addend;
                            } else {
                                *reinterpret_cast<uintptr_t *>(patch_addr) = sym_addr;
                            }
                        } else if (is_weak) {
                            *reinterpret_cast<uintptr_t *>(patch_addr) = 0;
                        }
                    } else if (type == R_X86_64_COPY) {
                        if (sym_idx > 0 && symtab && strtab) {
                            const auto &s = symtab[sym_idx];
                            const char *sym_name = strtab + s.st_name;
                            uintptr_t src_addr = 0;
                            if (resolver) {
                                src_addr = resolver(sym_name);
                            }
                            if (src_addr == 0) {
                                src_addr = LinkMap::Instance().ResolveGlobalSymbol(sym_name);
                            }
                            if (src_addr != 0 && src_addr != patch_addr && s.st_size > 0) {
                                std::memcpy(reinterpret_cast<void *>(patch_addr),
                                            reinterpret_cast<const void *>(src_addr), s.st_size);
                                loaded_elf.symbols[sym_name] = patch_addr;
                            }
                        }
                    } else if (type == R_X86_64_IRELATIVE) {
                        uintptr_t resolver_addr = load_bias + rel.r_addend;
                        using IfuncResolver = uintptr_t(__attribute__((sysv_abi)) *)(void);
                        auto ifunc_resolver = reinterpret_cast<IfuncResolver>(resolver_addr);
                        *reinterpret_cast<uintptr_t *>(patch_addr) = ifunc_resolver();
                    } else if (type == R_X86_64_TPOFF64 || type == R_X86_64_DTPOFF64) {
                        uintptr_t sym_val = 0;
                        if (sym_idx > 0 && symtab) {
                            const auto &s = symtab[sym_idx];
                            sym_val = s.st_value;
                            if (s.st_shndx == 0 && strtab && s.st_name != 0) {
                                size_t ext_modid = 0;
                                uintptr_t ext_off = 0;
                                if (LinkMap::Instance().ResolveTlsSymbol(strtab + s.st_name,
                                                                         ext_modid, ext_off)) {
                                    sym_val = ext_off;
                                }
                            }
                        }
                        *reinterpret_cast<uintptr_t *>(patch_addr) = sym_val + rel.r_addend;
                    } else if (type == R_X86_64_DTPMOD64) {
                        size_t modid = (loaded_elf.tls_modid > 0) ? loaded_elf.tls_modid : 1;
                        if (sym_idx > 0 && symtab && strtab) {
                            const auto &s = symtab[sym_idx];
                            if (s.st_shndx == 0) {
                                size_t ext_modid = 0;
                                uintptr_t ext_off = 0;
                                if (LinkMap::Instance().ResolveTlsSymbol(strtab + s.st_name,
                                                                         ext_modid, ext_off)) {
                                    modid = ext_modid;
                                }
                            }
                        }
                        *reinterpret_cast<uintptr_t *>(patch_addr) = modid;
                    } else if (type == R_X86_64_TLSDESC) {
                        size_t target_modid = loaded_elf.tls_modid;
                        uintptr_t sym_off = 0;
                        bool resolved_external = false;
                        if (sym_idx > 0 && symtab) {
                            const auto &s = symtab[sym_idx];
                            if (s.st_shndx == 0 && strtab && s.st_name != 0) {
                                if (LinkMap::Instance().ResolveTlsSymbol(strtab + s.st_name,
                                                                         target_modid, sym_off)) {
                                    resolved_external = true;
                                }
                            } else {
                                sym_off = s.st_value;
                            }
                        }
                        uintptr_t final_off = sym_off + static_cast<uintptr_t>(rel.r_addend);
                        auto *desc = reinterpret_cast<tlsdesc *>(patch_addr);
                        if (target_modid > 0 || resolved_external) {
                            auto dyn_arg = std::make_shared<tlsdesc_dynamic_arg>();
                            dyn_arg->gen_count = 1;
                            dyn_arg->ti.ti_module = target_modid;
                            dyn_arg->ti.ti_offset = final_off;
                            desc->entry = reinterpret_cast<uintptr_t (*)(tlsdesc *)>(
                                LinkMap::Instance().GetTlsdescDynamicThunk());
                            desc->arg = dyn_arg.get();
                            loaded_elf.tlsdesc_args.push_back(std::move(dyn_arg));
                        } else {
                            desc->entry = reinterpret_cast<uintptr_t (*)(tlsdesc *)>(
                                LinkMap::Instance().GetTlsdescReturnThunk());
                            desc->arg = reinterpret_cast<void *>(final_off);
                        }
                    }
                }
            };

            apply_relas(rela_addr, rela_sz, false);
            apply_relas(jmprel_addr, jmprel_sz, true);

            if (init_addr != 0) {
                loaded_elf.init_functions.push_back(init_addr);
            }
            if (init_array_addr != 0 && init_array_sz >= sizeof(uintptr_t)) {
                size_t count = init_array_sz / sizeof(uintptr_t);
                const auto *arr = reinterpret_cast<const uintptr_t *>(init_array_addr);
                for (size_t i = 0; i < count; i++) {
                    if (arr[i] != 0 && arr[i] != static_cast<uintptr_t>(-1)) {
                        loaded_elf.init_functions.push_back(resolve_ptr(arr[i]));
                    }
                }
            }

            if (fini_addr != 0) {
                loaded_elf.fini_functions.push_back(fini_addr);
            }
            if (fini_array_addr != 0 && fini_array_sz >= sizeof(uintptr_t)) {
                size_t count = fini_array_sz / sizeof(uintptr_t);
                const auto *arr = reinterpret_cast<const uintptr_t *>(fini_array_addr);
                for (size_t i = 0; i < count; i++) {
                    if (arr[i] != 0 && arr[i] != static_cast<uintptr_t>(-1)) {
                        loaded_elf.fini_functions.push_back(resolve_ptr(arr[i]));
                    }
                }
            }
            break;
        }
    }

    if (loaded_elf.relro_vaddr != 0 && loaded_elf.relro_memsz > 0) {
        uintptr_t relro_start = AlignDown(loaded_elf.relro_vaddr, mem::PAGE_SIZE_4KB);
        uintptr_t relro_end =
            AlignDown(loaded_elf.relro_vaddr + loaded_elf.relro_memsz, mem::PAGE_SIZE_4KB);
        if (relro_end > relro_start) {
            (void)address_space_->Mprotect(reinterpret_cast<void *>(relro_start),
                                           relro_end - relro_start, 1 /* PROT_READ */);
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
    if (uintptr_t primary = ndk::NdkSymbolRegistry::Instance().Lookup(name); primary != 0) {
        return primary;
    }
    return ndk::NdkSymbolRegistry::Instance().LookupFallback(name);
}

} // namespace portage::elf
