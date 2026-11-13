#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "elf/link_map.hpp"
#include "mem/address_space.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace portage::elf;

TEST(ElfVersioningIfuncTest, TypesAndParsing)
{
    EXPECT_EQ(STT_GNU_IFUNC, 10);
    EXPECT_EQ(STV_DEFAULT, 0);
    EXPECT_EQ(STV_INTERNAL, 1);
    EXPECT_EQ(STV_HIDDEN, 2);
    EXPECT_EQ(STV_PROTECTED, 3);
    EXPECT_EQ(ELF64_ST_VISIBILITY(3), STV_PROTECTED);

    EXPECT_EQ(DT_VERSYM, 0x6ffffff0);
    EXPECT_EQ(DT_VERDEF, 0x6ffffffc);
    EXPECT_EQ(DT_VERDEFNUM, 0x6ffffffd);
    EXPECT_EQ(DT_VERNEED, 0x6ffffffe);
    EXPECT_EQ(DT_VERNEEDNUM, 0x6fffffff);

    EXPECT_EQ(VERSYM_VERSION, 0x7fff);
    EXPECT_EQ(VERSYM_HIDDEN, 0x8000);

    Elf64_Verdef verdef{};
    verdef.vd_version = 1;
    verdef.vd_flags = 0;
    verdef.vd_ndx = 2;
    verdef.vd_cnt = 1;
    verdef.vd_hash = 0x1234;
    verdef.vd_aux = sizeof(Elf64_Verdef);
    verdef.vd_next = 0;
    EXPECT_EQ(verdef.vd_ndx, 2);

    Elf64_Verdaux verdaux{};
    verdaux.vda_name = 10;
    verdaux.vda_next = 0;
    EXPECT_EQ(verdaux.vda_name, 10);

    Elf64_Verneed verneed{};
    verneed.vn_version = 1;
    verneed.vn_cnt = 1;
    verneed.vn_file = 20;
    verneed.vn_aux = sizeof(Elf64_Verneed);
    verneed.vn_next = 0;
    EXPECT_EQ(verneed.vn_file, 20);

    Elf64_Vernaux vernaux{};
    vernaux.vna_hash = 0x5678;
    vernaux.vna_flags = 0;
    vernaux.vna_other = 3;
    vernaux.vna_name = 30;
    vernaux.vna_next = 0;
    EXPECT_EQ(vernaux.vna_other, 3);

    LoadedElf elf{};
    elf.ifunc_symbols.insert("my_ifunc");
    elf.versioned_symbols["func_v1"].push_back({"LIBC_N", false});
    elf.versioned_symbols["func_v2"].push_back({"LIBC_O", true});

    EXPECT_TRUE(elf.ifunc_symbols.contains("my_ifunc"));
    EXPECT_EQ(elf.versioned_symbols["func_v1"].size(), 1u);
    EXPECT_FALSE(elf.versioned_symbols["func_v1"][0].is_hidden);
    EXPECT_TRUE(elf.versioned_symbols["func_v2"][0].is_hidden);
}

namespace {
__attribute__((sysv_abi)) int target_add(int a, int b) { return a + b; }

static int s_ifunc_calls = 0;
__attribute__((sysv_abi)) uintptr_t counting_ifunc_resolver()
{
    s_ifunc_calls++;
    return reinterpret_cast<uintptr_t>(&target_add);
}
} // namespace

TEST(ElfVersioningIfuncTest, IfuncResolutionAndCaching)
{
    s_ifunc_calls = 0;
    auto &lm = LinkMap::Instance();
    lm.Reset();

    LoadedElf elf{};
    elf.symbols["calc_sum"] = reinterpret_cast<uintptr_t>(&counting_ifunc_resolver);
    elf.ifunc_symbols.insert("calc_sum");

    auto entry = lm.RegisterLoadedElf(std::move(elf), "libifunc_provider.so");
    ASSERT_NE(entry, nullptr);

    // Initial resolution should invoke the resolver once
    uintptr_t resolved_addr = lm.ResolveGlobalSymbol("calc_sum");
    EXPECT_EQ(s_ifunc_calls, 1);
    ASSERT_EQ(resolved_addr, reinterpret_cast<uintptr_t>(&target_add));

    auto fn = reinterpret_cast<__attribute__((sysv_abi)) int (*)(int, int)>(resolved_addr);
    EXPECT_EQ(fn(10, 32), 42);

    // Second resolution must hit cache and NOT invoke resolver again
    uintptr_t cached_addr = lm.ResolveGlobalSymbol("calc_sum");
    EXPECT_EQ(s_ifunc_calls, 1);
    EXPECT_EQ(cached_addr, resolved_addr);

    // Lookup via dl_sym must also return cached target without extra invocations
    void *sym_from_dlsym = lm.DlSym(entry->handle, "calc_sum");
    EXPECT_EQ(s_ifunc_calls, 1);
    EXPECT_EQ(sym_from_dlsym, reinterpret_cast<void *>(&target_add));

    lm.Reset();
}

extern "C" __attribute__((sysv_abi)) void *dlvsym(void *handle, const char *symbol,
                                                  const char *version);

TEST(ElfVersioningIfuncTest, SymbolVersioning)
{
    auto &lm = LinkMap::Instance();
    lm.Reset();

    LoadedElf elf{};
    elf.symbols["versioned_func"] = 0x1000;
    elf.symbols["hidden_func"] = 0x2000;

    // "versioned_func" has default unhidden version "LIBC_N"
    elf.versioned_symbols["versioned_func"].push_back({"LIBC_N", false});
    // "hidden_func" has hidden version "LIBC_PRIVATE"
    elf.versioned_symbols["hidden_func"].push_back({"LIBC_PRIVATE", true});

    auto entry = lm.RegisterLoadedElf(std::move(elf), "libversioned.so");
    ASSERT_NE(entry, nullptr);

    // Exact matching version lookup
    uintptr_t addr_exact = lm.ResolveGlobalSymbolVersioned("versioned_func", "LIBC_N");
    EXPECT_EQ(addr_exact, 0x1000u);

    // Unversioned lookup should fall back to unhidden default
    uintptr_t addr_unversioned = lm.ResolveGlobalSymbolVersioned("versioned_func", "");
    EXPECT_EQ(addr_unversioned, 0x1000u);

    // Mismatched version should fail
    uintptr_t addr_mismatch = lm.ResolveGlobalSymbolVersioned("versioned_func", "LIBC_OLD");
    EXPECT_EQ(addr_mismatch, 0u);

    // Unversioned lookup for hidden version should fail
    uintptr_t addr_hidden_unver = lm.ResolveGlobalSymbolVersioned("hidden_func", "");
    EXPECT_EQ(addr_hidden_unver, 0u);

    // Exact match for hidden version should succeed
    uintptr_t addr_hidden_exact = lm.ResolveGlobalSymbolVersioned("hidden_func", "LIBC_PRIVATE");
    EXPECT_EQ(addr_hidden_exact, 0x2000u);

    // dlvsym API testing
    void *sym_exact = dlvsym(entry->handle, "versioned_func", "LIBC_N");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(sym_exact), 0x1000u);

    void *sym_unver = dlvsym(entry->handle, "versioned_func", nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(sym_unver), 0x1000u);

    void *sym_mismatch = dlvsym(entry->handle, "versioned_func", "LIBC_UNKNOWN");
    EXPECT_EQ(sym_mismatch, nullptr);

    void *sym_hidden_no = dlvsym(entry->handle, "hidden_func", nullptr);
    EXPECT_EQ(sym_hidden_no, nullptr);

    void *sym_hidden_yes = dlvsym(entry->handle, "hidden_func", "LIBC_PRIVATE");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(sym_hidden_yes), 0x2000u);

    // dlvsym with RTLD_DEFAULT
    void *sym_rtld_default = dlvsym(RTLD_DEFAULT, "versioned_func", "LIBC_N");
    EXPECT_EQ(reinterpret_cast<uintptr_t>(sym_rtld_default), 0x1000u);

    lm.Reset();
}

namespace {

struct SyntheticElfBuilder {
    std::vector<uint8_t> data;

    explicit SyntheticElfBuilder(size_t size = 0x2000) : data(size, 0) {}

    template <typename T> void WriteAt(size_t offset, const T &val)
    {
        std::memcpy(data.data() + offset, &val, sizeof(T));
    }

    void WriteBytes(size_t offset, const void *src, size_t len)
    {
        std::memcpy(data.data() + offset, src, len);
    }
};

} // namespace

TEST(ElfVersioningIfuncTest, ProtectedVisibility)
{
    SyntheticElfBuilder builder(0x3000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t symtab_off = 0x400;
    constexpr size_t strtab_off = 0x500;
    constexpr size_t got_off = 0x600;

    Elf64_Ehdr ehdr{};
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = 1;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_phoff = phdr_off;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 2;
    builder.WriteAt(0, ehdr);

    Elf64_Phdr ph_load{};
    ph_load.p_type = PT_LOAD;
    ph_load.p_flags = PF_R | PF_W | PF_X;
    ph_load.p_offset = 0;
    ph_load.p_vaddr = 0;
    ph_load.p_paddr = 0;
    ph_load.p_filesz = 0x3000;
    ph_load.p_memsz = 0x3000;
    ph_load.p_align = 0x1000;
    builder.WriteAt(phdr_off, ph_load);

    const char strtab[] = "\0protected_func\0default_func\0";
    builder.WriteBytes(strtab_off, strtab, sizeof(strtab));

    Elf64_Sym syms[3]{};
    syms[1].st_name = 1;
    syms[1].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[1].st_other = STV_PROTECTED;
    syms[1].st_shndx = 1;
    syms[1].st_value = 0x1111;

    syms[2].st_name = 16;
    syms[2].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[2].st_other = STV_DEFAULT;
    syms[2].st_shndx = 1;
    syms[2].st_value = 0x2222;
    builder.WriteBytes(symtab_off, syms, sizeof(syms));

    Elf64_Rela relas[2]{};
    relas[0].r_offset = got_off;
    relas[0].r_info = (1ULL << 32) | R_X86_64_GLOB_DAT;
    relas[0].r_addend = 0;

    relas[1].r_offset = got_off + 8;
    relas[1].r_info = (2ULL << 32) | R_X86_64_GLOB_DAT;
    relas[1].r_addend = 0;
    builder.WriteBytes(rela_off, relas, sizeof(relas));

    Elf64_Dyn dyns[5]{};
    dyns[0] = {DT_STRTAB, {strtab_off}};
    dyns[1] = {DT_SYMTAB, {symtab_off}};
    dyns[2] = {DT_RELA, {rela_off}};
    dyns[3] = {DT_RELASZ, {sizeof(relas)}};
    dyns[4] = {DT_NULL, {0}};
    builder.WriteBytes(dyn_off, dyns, sizeof(dyns));

    Elf64_Phdr ph_dyn{};
    ph_dyn.p_type = PT_DYNAMIC;
    ph_dyn.p_flags = PF_R | PF_W;
    ph_dyn.p_offset = dyn_off;
    ph_dyn.p_vaddr = dyn_off;
    ph_dyn.p_paddr = dyn_off;
    ph_dyn.p_filesz = sizeof(dyns);
    ph_dyn.p_memsz = sizeof(dyns);
    ph_dyn.p_align = 8;
    builder.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);

    portage::mem::AddressSpace as;
    ElfLoader loader(&as);

    ElfLoader::SymbolResolver preemption_resolver = [](std::string_view name) -> uintptr_t {
        if (name == "protected_func") {
            return 0x999999ULL;
        }
        if (name == "default_func") {
            return 0x888888ULL;
        }
        return 0;
    };

    auto result = loader.LoadFromMemory(builder.data, nullptr, preemption_resolver);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(result));

    const auto &elf = std::get<LoadedElf>(result);
    uintptr_t patched_protected = *reinterpret_cast<const uintptr_t *>(elf.base_address + got_off);
    uintptr_t patched_default =
        *reinterpret_cast<const uintptr_t *>(elf.base_address + got_off + 8);

    EXPECT_EQ(patched_protected, elf.load_bias + 0x1111);
    EXPECT_EQ(patched_default, 0x888888ULL);
}
