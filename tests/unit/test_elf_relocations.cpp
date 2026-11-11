#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "elf/link_map.hpp"
#include "mem/address_space.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace portage::elf;

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

TEST(ElfRelocationsTest, GnuHashComputation)
{
    EXPECT_EQ(ElfGnuHash(""), 5381U);
    EXPECT_EQ(ElfGnuHash("printf"), 0x156b2bb8U);
    EXPECT_EQ(ElfGnuHash("exit"), 0x7c967e3fU);
}

TEST(ElfRelocationsTest, GnuHashSymbolExtraction)
{
    SyntheticElfBuilder builder(0x2000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t strtab_off = 0x400;
    constexpr size_t symtab_off = 0x500;
    constexpr size_t gnuhash_off = 0x600;

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
    ph_load.p_flags = PF_R | PF_W;
    ph_load.p_offset = 0;
    ph_load.p_vaddr = 0;
    ph_load.p_paddr = 0;
    ph_load.p_filesz = 0x2000;
    ph_load.p_memsz = 0x2000;
    ph_load.p_align = 0x1000;
    builder.WriteAt(phdr_off, ph_load);

    Elf64_Dyn dyns[4]{};
    dyns[0] = {DT_STRTAB, {strtab_off}};
    dyns[1] = {DT_SYMTAB, {symtab_off}};
    dyns[2] = {DT_GNU_HASH, {gnuhash_off}};
    dyns[3] = {DT_NULL, {0}};
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

    const char strtab[] = "\0foo\0bar\0";
    builder.WriteBytes(strtab_off, strtab, sizeof(strtab));

    Elf64_Sym syms[3]{};
    syms[1].st_name = 1; // "foo"
    syms[1].st_shndx = 1;
    syms[1].st_value = 0x1000;
    syms[2].st_name = 5; // "bar"
    syms[2].st_shndx = 1;
    syms[2].st_value = 0x1200;
    builder.WriteBytes(symtab_off, syms, sizeof(syms));

    Elf64_GnuHashHeader hdr{};
    hdr.nbuckets = 1;
    hdr.symoffset = 1;
    hdr.bloom_size = 1;
    hdr.bloom_shift = 0;
    builder.WriteAt(gnuhash_off, hdr);

    uint64_t bloom = ~0ULL;
    builder.WriteAt(gnuhash_off + sizeof(hdr), bloom);

    uint32_t bucket = 1;
    builder.WriteAt(gnuhash_off + sizeof(hdr) + sizeof(uint64_t), bucket);

    uint32_t chains[2] = {
        ElfGnuHash("foo") & ~1U,
        ElfGnuHash("bar") | 1U,
    };
    builder.WriteBytes(gnuhash_off + sizeof(hdr) + sizeof(uint64_t) + sizeof(uint32_t), chains,
                       sizeof(chains));

    portage::mem::AddressSpace as;
    ElfLoader loader(&as);
    auto result = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(result));

    const auto &elf = std::get<LoadedElf>(result);
    EXPECT_EQ(elf.FindSymbol("foo"), elf.base_address + 0x1000);
    EXPECT_EQ(elf.FindSymbol("bar"), elf.base_address + 0x1200);
}

TEST(ElfRelocationsTest, IfuncResolution)
{
    SyntheticElfBuilder builder(0x2000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t code_off = 0x400;
    constexpr size_t got_off = 0x500;

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
    ph_load.p_filesz = 0x2000;
    ph_load.p_memsz = 0x2000;
    ph_load.p_align = 0x1000;
    builder.WriteAt(phdr_off, ph_load);

    Elf64_Rela rel{};
    rel.r_offset = got_off;
    rel.r_info = R_X86_64_IRELATIVE;
    rel.r_addend = code_off;
    builder.WriteAt(rela_off, rel);

    // mov $0x12345678, %eax; ret
    const uint8_t resolver_code[] = {0xB8, 0x78, 0x56, 0x34, 0x12, 0xC3};
    builder.WriteBytes(code_off, resolver_code, sizeof(resolver_code));

    Elf64_Dyn dyns[3]{};
    dyns[0] = {DT_RELA, {rela_off}};
    dyns[1] = {DT_RELASZ, {sizeof(Elf64_Rela)}};
    dyns[2] = {DT_NULL, {0}};
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
    auto result = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(result));

    const auto &elf = std::get<LoadedElf>(result);
    uintptr_t patched_val = *reinterpret_cast<const uintptr_t *>(elf.base_address + got_off);
    EXPECT_EQ(patched_val, 0x12345678ULL);
}

TEST(ElfRelocationsTest, TlsRelocations)
{
    SyntheticElfBuilder builder(0x2000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t symtab_off = 0x400;
    constexpr size_t got_off = 0x500;

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
    ph_load.p_flags = PF_R | PF_W;
    ph_load.p_offset = 0;
    ph_load.p_vaddr = 0;
    ph_load.p_paddr = 0;
    ph_load.p_filesz = 0x2000;
    ph_load.p_memsz = 0x2000;
    ph_load.p_align = 0x1000;
    builder.WriteAt(phdr_off, ph_load);

    Elf64_Sym syms[2]{};
    syms[1].st_value = 0x10;
    builder.WriteBytes(symtab_off, syms, sizeof(syms));

    Elf64_Rela relas[3]{};
    relas[0].r_offset = got_off;
    relas[0].r_info = (1ULL << 32) | R_X86_64_TPOFF64;
    relas[0].r_addend = -0x20;

    relas[1].r_offset = got_off + 8;
    relas[1].r_info = (1ULL << 32) | R_X86_64_DTPOFF64;
    relas[1].r_addend = 8;

    relas[2].r_offset = got_off + 16;
    relas[2].r_info = (1ULL << 32) | R_X86_64_DTPMOD64;
    relas[2].r_addend = 0;
    builder.WriteBytes(rela_off, relas, sizeof(relas));

    Elf64_Dyn dyns[4]{};
    dyns[0] = {DT_SYMTAB, {symtab_off}};
    dyns[1] = {DT_RELA, {rela_off}};
    dyns[2] = {DT_RELASZ, {sizeof(relas)}};
    dyns[3] = {DT_NULL, {0}};
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
    auto result = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(result));

    const auto &elf = std::get<LoadedElf>(result);
    const auto *got = reinterpret_cast<const uintptr_t *>(elf.base_address + got_off);
    EXPECT_EQ(got[0], static_cast<uintptr_t>(-0x10));
    EXPECT_EQ(got[1], 0x18ULL);
    EXPECT_EQ(got[2], 1ULL);
}

TEST(ElfRelocationsTest, VirtualSystemLibraryFallbackWithoutRootfs)
{
    LinkMap::Instance().Reset();
    portage::mem::AddressSpace as;
    LinkMap::Instance().SetAddressSpace(&as);

    EXPECT_TRUE(LinkMap::IsBuiltinSystemLibrary("libz.so"));
    EXPECT_TRUE(LinkMap::IsBuiltinSystemLibrary("/system/lib64/libc++_shared.so"));
    EXPECT_FALSE(LinkMap::IsBuiltinSystemLibrary("libcustom_unknown_xyz.so"));

    void *handle = LinkMap::Instance().DlOpen("libandroid.so", RTLD_NOW);
    ASSERT_NE(handle, nullptr);

    void *sym = LinkMap::Instance().DlSym(handle, "ALooper_forThread");
    EXPECT_NE(sym, nullptr);
    EXPECT_EQ(LinkMap::Instance().DlClose(handle), 0);
    LinkMap::Instance().Reset();
}

TEST(ElfRelocationsTest, WeakUnresolvedRemainsNullWhileStrongIsRecordedAndTrapped)
{
    LinkMap::Instance().Reset();

    SyntheticElfBuilder builder(0x2000);
    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t strtab_off = 0x400;
    constexpr size_t symtab_off = 0x500;
    constexpr size_t rela_off = 0x600;
    constexpr size_t got_off = 0x700;

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
    ph_load.p_flags = PF_R | PF_W;
    ph_load.p_offset = 0;
    ph_load.p_vaddr = 0;
    ph_load.p_paddr = 0;
    ph_load.p_filesz = 0x2000;
    ph_load.p_memsz = 0x2000;
    ph_load.p_align = 0x1000;
    builder.WriteAt(phdr_off, ph_load);

    const char strtab[] = "\0missing_strong_fn\0missing_weak_fn";
    builder.WriteBytes(strtab_off, strtab, sizeof(strtab));

    Elf64_Sym syms[3]{};
    syms[1].st_name = 1;
    syms[1].st_info = (STB_GLOBAL << 4) | STT_FUNC;
    syms[2].st_name = 19;
    syms[2].st_info = (STB_WEAK << 4) | STT_FUNC;
    builder.WriteBytes(symtab_off, syms, sizeof(syms));

    Elf64_Rela relas[2]{};
    relas[0].r_offset = got_off;
    relas[0].r_info = (1ULL << 32) | R_X86_64_JUMP_SLOT;
    relas[1].r_offset = got_off + 8;
    relas[1].r_info = (2ULL << 32) | R_X86_64_GLOB_DAT;
    builder.WriteBytes(rela_off, relas, sizeof(relas));

    Elf64_Dyn dyns[6]{};
    dyns[0] = {DT_STRTAB, {strtab_off}};
    dyns[1] = {DT_SYMTAB, {symtab_off}};
    dyns[2] = {DT_JMPREL, {rela_off}};
    dyns[3] = {DT_PLTRELSZ, {sizeof(relas)}};
    dyns[4] = {DT_STRSZ, {sizeof(strtab)}};
    dyns[5] = {DT_NULL, {0}};
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
    auto result = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(result));

    const auto &elf = std::get<LoadedElf>(result);
    ASSERT_EQ(elf.unresolved_symbols.size(), 1U);
    EXPECT_EQ(elf.unresolved_symbols[0], "missing_strong_fn");

    const auto *got = reinterpret_cast<const uintptr_t *>(elf.base_address + got_off);
    EXPECT_NE(got[0], 0ULL);
    EXPECT_EQ(got[1], 0ULL);

    using TrapFn = __attribute__((sysv_abi)) int64_t (*)(void);
    auto fn = reinterpret_cast<TrapFn>(got[0]);
    EXPECT_EQ(fn(), -1);

    LinkMap::Instance().Reset();
}

TEST(ElfRelocationsTest, LoadedCxxSharedOverridesFallbackWhileVirtualUsesFallback)
{
    LinkMap::Instance().Reset();

    uintptr_t initial_resolve = LinkMap::Instance().ResolveGlobalSymbol("_Znwm");
    EXPECT_NE(initial_resolve, 0ULL);

    constexpr uintptr_t kCustomCxxNewAddr = 0x55556666ULL;
    LoadedElf cxx_elf{};
    cxx_elf.soname = "libc++_shared.so";
    cxx_elf.symbols["_Znwm"] = kCustomCxxNewAddr;
    cxx_elf.base_address = 0x55550000ULL;

    LinkMap::Instance().RegisterLoadedElf(std::move(cxx_elf), "libc++_shared.so");

    uintptr_t resolved_override = LinkMap::Instance().ResolveGlobalSymbol("_Znwm");
    EXPECT_EQ(resolved_override, kCustomCxxNewAddr);

    LinkMap::Instance().Reset();
}
