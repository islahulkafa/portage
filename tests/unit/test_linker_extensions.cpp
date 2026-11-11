#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <gtest/gtest.h>
#include <windows.h>
#include <immintrin.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

struct SyntheticElfBuilder {
    std::vector<uint8_t> data;

    explicit SyntheticElfBuilder(size_t size = 0x3000) : data(size, 0) {}

    template <typename T> void WriteAt(size_t offset, const T &val)
    {
        std::memcpy(data.data() + offset, &val, sizeof(T));
    }

    void WriteBytes(size_t offset, const void *src, size_t len)
    {
        std::memcpy(data.data() + offset, src, len);
    }
};

stdfs::path FindGuestBinary(const std::string &filename)
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("guests") / filename,
        stdfs::path("tests/guests") / filename,
        stdfs::current_path() / "guests" / filename,
        stdfs::current_path() / "tests" / "guests" / filename,
        stdfs::current_path() / "build" / "debug" / "tests" / "guests" / filename,
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return stdfs::path("guests") / filename;
}

int64_t InvokeTlsdescAndCheckRegs(const elf::tlsdesc *desc, uint64_t *regs_out)
{
    int64_t offset_ret = 0;
    uint64_t fn = reinterpret_cast<uint64_t>(desc->entry);
    uint64_t arg_addr = reinterpret_cast<uint64_t>(desc);
    uint64_t *out_ptr = regs_out;

    asm volatile("movq $0x1111111111111111, %%rcx\n\t"
                 "movq $0x2222222222222222, %%rdx\n\t"
                 "movq $0x3333333333333333, %%rsi\n\t"
                 "movq $0x4444444444444444, %%rdi\n\t"
                 "movq $0x5555555555555555, %%r8\n\t"
                 "movq $0x6666666666666666, %%r9\n\t"
                 "movq $0x7777777777777777, %%r10\n\t"
                 "movq $0x8888888888888888, %%r11\n\t"
                 "movq %[arg], %%rax\n\t"
                 "call *%[fn]\n\t"
                 "movq %%rcx, 0(%%rbx)\n\t"
                 "movq %%rdx, 8(%%rbx)\n\t"
                 "movq %%rsi, 16(%%rbx)\n\t"
                 "movq %%rdi, 24(%%rbx)\n\t"
                 "movq %%r8, 32(%%rbx)\n\t"
                 "movq %%r9, 40(%%rbx)\n\t"
                 "movq %%r10, 48(%%rbx)\n\t"
                 "movq %%r11, 56(%%rbx)\n\t"
                 : "=a"(offset_ret)
                 : [arg] "m"(arg_addr), [fn] "m"(fn), [out] "b"(out_ptr)
                 : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory", "cc");

    return offset_ret;
}

class ScopedFsBase {
  public:
    explicit ScopedFsBase(uint64_t new_fs) : old_fs_(_readfsbase_u64())
    {
        _writefsbase_u64(new_fs);
    }
    ~ScopedFsBase() { _writefsbase_u64(old_fs_); }
    ScopedFsBase(const ScopedFsBase &) = delete;
    ScopedFsBase &operator=(const ScopedFsBase &) = delete;

  private:
    uint64_t old_fs_;
};

} // namespace

class LinkerExtensionsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);
        elf::LinkMap::Instance().Reset();
        elf::LinkMap::Instance().SetAddressSpace(address_space_.get());
        elf::LinkMap::Instance().SetVfs(vfs_.get());
        thread::ThreadManager::Instance().RegisterMainThread();
        (void)ndk::NdkSymbolRegistry::Instance();
    }

    void TearDown() override
    {
        elf::LinkMap::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();
    }

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

TEST_F(LinkerExtensionsTest, TlsdescStaticResolverReturnsDirectOffsetAndPreservesRegs)
{
    SyntheticElfBuilder bin(0x2000);
    constexpr size_t phdr_off = sizeof(elf::Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t strtab_off = 0x400;
    constexpr size_t symtab_off = 0x500;
    constexpr size_t got_off = 0x700;

    elf::Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident,
                "\x7F"
                "ELF",
                4);
    ehdr.e_ident[elf::EI_CLASS] = elf::ELFCLASS64;
    ehdr.e_ident[elf::EI_DATA] = elf::ELFDATA2LSB;
    ehdr.e_ident[elf::EI_VERSION] = 1;
    ehdr.e_type = elf::ET_DYN;
    ehdr.e_machine = elf::EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_phoff = phdr_off;
    ehdr.e_ehsize = sizeof(elf::Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(elf::Elf64_Phdr);
    ehdr.e_phnum = 2;
    bin.WriteAt(0, ehdr);

    elf::Elf64_Phdr ph_load{elf::PT_LOAD, elf::PF_R | elf::PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
    bin.WriteAt(phdr_off, ph_load);

    const char strtab[] = "\0";
    bin.WriteBytes(strtab_off, strtab, sizeof(strtab));

    elf::Elf64_Sym syms[1]{};
    bin.WriteBytes(symtab_off, syms, sizeof(syms));

    elf::Elf64_Rela relas[1]{};
    relas[0].r_offset = got_off;
    relas[0].r_info = elf::R_X86_64_TLSDESC;
    relas[0].r_addend = -32;
    bin.WriteBytes(rela_off, relas, sizeof(relas));

    elf::Elf64_Dyn dyns[5]{};
    dyns[0] = {elf::DT_STRTAB, {strtab_off}};
    dyns[1] = {elf::DT_SYMTAB, {symtab_off}};
    dyns[2] = {elf::DT_RELA, {rela_off}};
    dyns[3] = {elf::DT_RELASZ, {sizeof(relas)}};
    dyns[4] = {elf::DT_NULL, {0}};
    bin.WriteBytes(dyn_off, dyns, sizeof(dyns));

    elf::Elf64_Phdr ph_dyn{elf::PT_DYNAMIC, elf::PF_R | elf::PF_W, dyn_off,      dyn_off,
                           dyn_off,         sizeof(dyns),          sizeof(dyns), 8};
    bin.WriteAt(phdr_off + sizeof(elf::Elf64_Phdr), ph_dyn);

    elf::ElfLoader loader(address_space_.get());
    auto res = loader.LoadFromMemory(bin.data);
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(res));
    auto loaded = std::get<elf::LoadedElf>(res);

    const auto *desc = reinterpret_cast<const elf::tlsdesc *>(loaded.base_address + got_off);
    EXPECT_EQ(reinterpret_cast<uint64_t>(desc->entry),
              elf::LinkMap::Instance().GetTlsdescReturnThunk());
    EXPECT_EQ(reinterpret_cast<intptr_t>(desc->arg), -32);

    uint64_t regs_out[8]{};
    int64_t off = InvokeTlsdescAndCheckRegs(desc, regs_out);
    EXPECT_EQ(off, -32);
    EXPECT_EQ(regs_out[0], 0x1111111111111111ULL);
    EXPECT_EQ(regs_out[1], 0x2222222222222222ULL);
    EXPECT_EQ(regs_out[2], 0x3333333333333333ULL);
    EXPECT_EQ(regs_out[3], 0x4444444444444444ULL);
    EXPECT_EQ(regs_out[4], 0x5555555555555555ULL);
    EXPECT_EQ(regs_out[5], 0x6666666666666666ULL);
    EXPECT_EQ(regs_out[6], 0x7777777777777777ULL);
    EXPECT_EQ(regs_out[7], 0x8888888888888888ULL);
}

TEST_F(LinkerExtensionsTest, TlsdescDynamicResolverIsolatesPerThreadAndPreservesRegs)
{
    SyntheticElfBuilder mod(0x2000);
    constexpr size_t phdr_off = sizeof(elf::Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t strtab_off = 0x400;
    constexpr size_t symtab_off = 0x500;
    constexpr size_t got_off = 0x700;
    constexpr size_t tls_off = 0x800;

    elf::Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident,
                "\x7F"
                "ELF",
                4);
    ehdr.e_ident[elf::EI_CLASS] = elf::ELFCLASS64;
    ehdr.e_ident[elf::EI_DATA] = elf::ELFDATA2LSB;
    ehdr.e_ident[elf::EI_VERSION] = 1;
    ehdr.e_type = elf::ET_DYN;
    ehdr.e_machine = elf::EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_phoff = phdr_off;
    ehdr.e_ehsize = sizeof(elf::Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(elf::Elf64_Phdr);
    ehdr.e_phnum = 3;
    mod.WriteAt(0, ehdr);

    elf::Elf64_Phdr ph_load{elf::PT_LOAD, elf::PF_R | elf::PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
    mod.WriteAt(phdr_off, ph_load);

    const char strtab[] = "\0dyn_tls_var\0";
    mod.WriteBytes(strtab_off, strtab, sizeof(strtab));

    elf::Elf64_Sym syms[2]{};
    syms[1].st_name = 1;
    syms[1].st_info = (1 << 4) | elf::STT_TLS;
    syms[1].st_shndx = 1;
    syms[1].st_value = 16;
    syms[1].st_size = 8;
    mod.WriteBytes(symtab_off, syms, sizeof(syms));

    uint64_t tls_init_pattern = 0xCAFEBABEDEADBEEFULL;
    mod.WriteAt(tls_off + 16, tls_init_pattern);

    elf::Elf64_Rela relas[1]{};
    relas[0].r_offset = got_off;
    relas[0].r_info = (1ULL << 32) | elf::R_X86_64_TLSDESC;
    relas[0].r_addend = 0;
    mod.WriteBytes(rela_off, relas, sizeof(relas));

    elf::Elf64_Dyn dyns[5]{};
    dyns[0] = {elf::DT_STRTAB, {strtab_off}};
    dyns[1] = {elf::DT_SYMTAB, {symtab_off}};
    dyns[2] = {elf::DT_RELA, {rela_off}};
    dyns[3] = {elf::DT_RELASZ, {sizeof(relas)}};
    dyns[4] = {elf::DT_NULL, {0}};
    mod.WriteBytes(dyn_off, dyns, sizeof(dyns));

    elf::Elf64_Phdr ph_dyn{elf::PT_DYNAMIC, elf::PF_R | elf::PF_W, dyn_off,      dyn_off,
                           dyn_off,         sizeof(dyns),          sizeof(dyns), 8};
    mod.WriteAt(phdr_off + sizeof(elf::Elf64_Phdr), ph_dyn);

    elf::Elf64_Phdr ph_tls{elf::PT_TLS, elf::PF_R, tls_off, tls_off, tls_off, 32, 64, 16};
    mod.WriteAt(phdr_off + 2 * sizeof(elf::Elf64_Phdr), ph_tls);

    elf::ElfLoader loader(address_space_.get());
    auto res = loader.LoadFromMemory(mod.data);
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(res));
    auto loaded = std::get<elf::LoadedElf>(res);
    elf::LinkMap::Instance().RegisterLoadedElf(loaded, "libdyn_tls_test.so");

    const auto *desc = reinterpret_cast<const elf::tlsdesc *>(loaded.base_address + got_off);
    EXPECT_EQ(reinterpret_cast<uint64_t>(desc->entry),
              elf::LinkMap::Instance().GetTlsdescDynamicThunk());
    ASSERT_NE(desc->arg, nullptr);

    alignas(16) uint8_t main_tcb[64]{};
    ScopedFsBase main_fs_guard(reinterpret_cast<uint64_t>(main_tcb));
    uint64_t regs_main[8]{};
    int64_t off_main = InvokeTlsdescAndCheckRegs(desc, regs_main);
    EXPECT_EQ(regs_main[0], 0x1111111111111111ULL);
    EXPECT_EQ(regs_main[4], 0x5555555555555555ULL);
    EXPECT_EQ(regs_main[7], 0x8888888888888888ULL);

    auto *main_var = reinterpret_cast<uint64_t *>(_readfsbase_u64() + off_main);
    EXPECT_EQ(*main_var, tls_init_pattern);
    *main_var = 0x123456789ABCDEF0ULL;

    uint64_t worker_initial_val = 0;
    uint64_t worker_var_addr = 0;
    std::thread worker([&]() {
        alignas(16) uint8_t worker_tcb[64]{};
        ScopedFsBase worker_fs_guard(reinterpret_cast<uint64_t>(worker_tcb));
        uint64_t regs_worker[8]{};
        int64_t off_worker = InvokeTlsdescAndCheckRegs(desc, regs_worker);
        auto *worker_var = reinterpret_cast<uint64_t *>(_readfsbase_u64() + off_worker);
        worker_var_addr = reinterpret_cast<uint64_t>(worker_var);
        worker_initial_val = *worker_var;
        *worker_var = 0x0FEDCBA987654321ULL;
    });
    worker.join();

    EXPECT_EQ(worker_initial_val, tls_init_pattern);
    EXPECT_NE(worker_var_addr, reinterpret_cast<uint64_t>(main_var));
    EXPECT_EQ(*main_var, 0x123456789ABCDEF0ULL);
}

TEST_F(LinkerExtensionsTest, LazyPltTrampolineResolvesOnceAndPatchesGotSlot)
{
    SyntheticElfBuilder provider(0x2000);
    constexpr size_t code_off = 0x900;
    {
        constexpr size_t phdr_off = sizeof(elf::Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t strtab_off = 0x400;
        constexpr size_t symtab_off = 0x500;
        constexpr size_t hash_off = 0x600;

        elf::Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
        ehdr.e_ident[elf::EI_CLASS] = elf::ELFCLASS64;
        ehdr.e_ident[elf::EI_DATA] = elf::ELFDATA2LSB;
        ehdr.e_ident[elf::EI_VERSION] = 1;
        ehdr.e_type = elf::ET_DYN;
        ehdr.e_machine = elf::EM_X86_64;
        ehdr.e_version = 1;
        ehdr.e_phoff = phdr_off;
        ehdr.e_ehsize = sizeof(elf::Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(elf::Elf64_Phdr);
        ehdr.e_phnum = 2;
        provider.WriteAt(0, ehdr);

        elf::Elf64_Phdr ph_load{
            elf::PT_LOAD, elf::PF_R | elf::PF_W | elf::PF_X, 0, 0, 0, 0x2000, 0x2000, 0x1000};
        provider.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0lazy_target_fn\0";
        provider.WriteBytes(strtab_off, strtab, sizeof(strtab));

        elf::Elf64_Sym syms[2]{};
        syms[1].st_name = 1;
        syms[1].st_info = (1 << 4) | elf::STT_FUNC;
        syms[1].st_shndx = 1;
        syms[1].st_value = code_off;
        syms[1].st_size = 32;
        provider.WriteBytes(symtab_off, syms, sizeof(syms));

        uint32_t sysv_hash[6] = {1, 2, 1, 0, 0, 0};
        provider.WriteBytes(hash_off, sysv_hash, sizeof(sysv_hash));

        elf::Elf64_Dyn dyns[4]{};
        dyns[0] = {elf::DT_STRTAB, {strtab_off}};
        dyns[1] = {elf::DT_SYMTAB, {symtab_off}};
        dyns[2] = {elf::DT_HASH, {hash_off}};
        dyns[3] = {elf::DT_NULL, {0}};
        provider.WriteBytes(dyn_off, dyns, sizeof(dyns));

        elf::Elf64_Phdr ph_dyn{elf::PT_DYNAMIC, elf::PF_R | elf::PF_W, dyn_off,      dyn_off,
                               dyn_off,         sizeof(dyns),          sizeof(dyns), 8};
        provider.WriteAt(phdr_off + sizeof(elf::Elf64_Phdr), ph_dyn);

        const uint8_t fn_bytes[] = {
            0x48, 0x8D, 0x04, 0x37,       // lea (%rdi,%rsi,1), %rax
            0x48, 0x01, 0xD0,             // add %rdx, %rax
            0x48, 0x01, 0xC8,             // add %rcx, %rax
            0x4C, 0x01, 0xC0,             // add %r8, %rax
            0x4C, 0x01, 0xC8,             // add %r9, %rax
            0xF2, 0x48, 0x0F, 0x2A, 0xD0, // cvtsi2sd %rax, %xmm2
            0xF2, 0x0F, 0x58, 0xC1,       // addsd %xmm1, %xmm0
            0xF2, 0x0F, 0x58, 0xC2,       // addsd %xmm2, %xmm0
            0xC3                          // ret
        };
        provider.WriteBytes(code_off, fn_bytes, sizeof(fn_bytes));
    }

    elf::ElfLoader loader(address_space_.get());
    auto prov_res = loader.LoadFromMemory(provider.data);
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(prov_res));
    auto prov_elf = std::get<elf::LoadedElf>(prov_res);
    elf::LinkMap::Instance().RegisterLoadedElf(prov_elf, "liblazy_provider.so");
    uint64_t expected_target = prov_elf.base_address + code_off;

    SyntheticElfBuilder consumer(0x2000);
    constexpr size_t got_slot_off = 0x700;
    {
        constexpr size_t phdr_off = sizeof(elf::Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t jmprel_off = 0x300;
        constexpr size_t strtab_off = 0x400;
        constexpr size_t symtab_off = 0x500;

        elf::Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
        ehdr.e_ident[elf::EI_CLASS] = elf::ELFCLASS64;
        ehdr.e_ident[elf::EI_DATA] = elf::ELFDATA2LSB;
        ehdr.e_ident[elf::EI_VERSION] = 1;
        ehdr.e_type = elf::ET_DYN;
        ehdr.e_machine = elf::EM_X86_64;
        ehdr.e_version = 1;
        ehdr.e_phoff = phdr_off;
        ehdr.e_ehsize = sizeof(elf::Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(elf::Elf64_Phdr);
        ehdr.e_phnum = 2;
        consumer.WriteAt(0, ehdr);

        elf::Elf64_Phdr ph_load{elf::PT_LOAD, elf::PF_R | elf::PF_W, 0, 0, 0, 0x2000, 0x2000,
                                0x1000};
        consumer.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0lazy_target_fn\0";
        consumer.WriteBytes(strtab_off, strtab, sizeof(strtab));

        elf::Elf64_Sym syms[2]{};
        syms[1].st_name = 1;
        syms[1].st_info = (1 << 4) | elf::STT_FUNC;
        syms[1].st_shndx = elf::SHN_UNDEF;
        consumer.WriteBytes(symtab_off, syms, sizeof(syms));

        elf::Elf64_Rela jmprels[1]{};
        jmprels[0].r_offset = got_slot_off;
        jmprels[0].r_info = (1ULL << 32) | elf::R_X86_64_JUMP_SLOT;
        consumer.WriteBytes(jmprel_off, jmprels, sizeof(jmprels));

        elf::Elf64_Dyn dyns[7]{};
        dyns[0] = {elf::DT_STRTAB, {strtab_off}};
        dyns[1] = {elf::DT_SYMTAB, {symtab_off}};
        dyns[2] = {elf::DT_JMPREL, {jmprel_off}};
        dyns[3] = {elf::DT_PLTRELSZ, {sizeof(jmprels)}};
        dyns[4] = {20 /* DT_PLTREL */, {elf::DT_RELA}};
        dyns[5] = {elf::DT_NULL, {0}};
        consumer.WriteBytes(dyn_off, dyns, sizeof(dyns));

        elf::Elf64_Phdr ph_dyn{elf::PT_DYNAMIC, elf::PF_R | elf::PF_W, dyn_off,      dyn_off,
                               dyn_off,         sizeof(dyns),          sizeof(dyns), 8};
        consumer.WriteAt(phdr_off + sizeof(elf::Elf64_Phdr), ph_dyn);
    }

    elf::LoadOptions lazy_opts{};
    lazy_opts.lazy_binding = true;
    auto cons_res =
        loader.LoadFromMemory(consumer.data, vfs_.get(), nullptr, nullptr, "", lazy_opts);
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(cons_res));
    auto cons_elf = std::get<elf::LoadedElf>(cons_res);
    elf::LinkMap::Instance().RegisterLoadedElf(cons_elf, "liblazy_consumer.so");

    auto *got_slot = reinterpret_cast<uint64_t *>(cons_elf.base_address + got_slot_off);
    ASSERT_NE(*got_slot, 0ULL);
    EXPECT_NE(*got_slot, expected_target);

    uint64_t count_before = elf::LinkMap::Instance().GetLazyResolveCount();

    using LazyFn = __attribute__((sysv_abi)) double (*)(int64_t, int64_t, int64_t, int64_t, int64_t,
                                                        int64_t, double, double);
    auto fn1 = reinterpret_cast<LazyFn>(*got_slot);
    double r1 = fn1(1, 2, 3, 4, 5, 6, 1.5, 2.5);
    EXPECT_DOUBLE_EQ(r1, 25.0);
    EXPECT_EQ(elf::LinkMap::Instance().GetLazyResolveCount(), count_before + 1);
    EXPECT_EQ(*got_slot, expected_target);

    auto fn2 = reinterpret_cast<LazyFn>(*got_slot);
    double r2 = fn2(10, 20, 30, 40, 50, 60, 0.25, 0.75);
    EXPECT_DOUBLE_EQ(r2, 211.0);
    EXPECT_EQ(elf::LinkMap::Instance().GetLazyResolveCount(), count_before + 1);
}

TEST_F(LinkerExtensionsTest, AndroidDlOpenExtLoadsFromFdAndOffset)
{
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    ASSERT_TRUE(stdfs::exists(math_path)) << "Missing fixture: " << math_path;

    std::ifstream ifs(math_path, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    std::vector<uint8_t> elf_bytes((std::istreambuf_iterator<char>(ifs)),
                                   std::istreambuf_iterator<char>());
    ASSERT_GT(elf_bytes.size(), 64u);

    constexpr size_t kOffset = 4096;
    stdfs::path packed_path = stdfs::temp_directory_path() / "portage_packed_ext_test.bin";
    {
        std::ofstream ofs(packed_path, std::ios::binary | std::ios::trunc);
        std::vector<uint8_t> pad(kOffset, 0xAA);
        ofs.write(reinterpret_cast<const char *>(pad.data()),
                  static_cast<std::streamsize>(pad.size()));
        ofs.write(reinterpret_cast<const char *>(elf_bytes.data()),
                  static_cast<std::streamsize>(elf_bytes.size()));
    }

    HANDLE h_file = CreateFileW(packed_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(h_file, INVALID_HANDLE_VALUE);
    int guest_fd =
        fd_table_->Allocate(h_file, 0, fs::DeviceType::Regular, packed_path.string(), false);
    ASSERT_GE(guest_fd, 0);

    elf::android_dlextinfo ext{};
    ext.flags = elf::ANDROID_DLEXT_USE_LIBRARY_FD | elf::ANDROID_DLEXT_USE_LIBRARY_FD_OFFSET;
    ext.library_fd = guest_fd;
    ext.library_fd_offset = static_cast<int64_t>(kOffset);

    void *handle = elf::LinkMap::Instance().DlOpenExt("packed_math.so", elf::RTLD_NOW, &ext);
    fd_table_->Close(guest_fd);

    ASSERT_NE(handle, nullptr) << elf::LinkMap::Instance().DlError();

    using MathAddFn = __attribute__((sysv_abi)) int (*)(int, int);
    auto math_add = reinterpret_cast<MathAddFn>(elf::LinkMap::Instance().DlSym(handle, "math_add"));
    ASSERT_NE(math_add, nullptr);
    EXPECT_EQ(math_add(19, 23), 42);

    EXPECT_EQ(elf::LinkMap::Instance().DlClose(handle), 0);
    std::error_code ec;
    stdfs::remove(packed_path, ec);
}

TEST_F(LinkerExtensionsTest, DlInfoQueriesMetadataAndDlCloseUnmapsSegments)
{
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    ASSERT_TRUE(stdfs::exists(math_path)) << "Missing fixture: " << math_path;

    auto &lm = elf::LinkMap::Instance();
    void *h1 = lm.DlOpen(math_path.string().c_str(), elf::RTLD_NOW);
    ASSERT_NE(h1, nullptr) << lm.DlError();
    void *h2 = lm.DlOpen(math_path.string().c_str(), elf::RTLD_NOW);
    ASSERT_EQ(h2, h1);

    elf::link_map_raw *raw_lm = nullptr;
    EXPECT_EQ(lm.DlInfo(h1, elf::RTLD_DI_LINKMAP, &raw_lm), 0);
    ASSERT_NE(raw_lm, nullptr);
    EXPECT_NE(raw_lm->l_addr, 0ULL);
    EXPECT_NE(raw_lm->l_name, nullptr);

    char origin_buf[512]{};
    EXPECT_EQ(lm.DlInfo(h1, elf::RTLD_DI_ORIGIN, origin_buf), 0);
    EXPECT_GT(std::strlen(origin_buf), 0u);

    auto mod = lm.FindModule(math_path.string());
    ASSERT_NE(mod, nullptr);
    ASSERT_FALSE(mod->elf.segments.empty());
    uint64_t seg_vaddr = mod->elf.segments[0].vaddr;

    EXPECT_EQ(lm.DlClose(h2), 0);
    EXPECT_NE(lm.FindModule(math_path.string()), nullptr);
    EXPECT_NE(lm.DlSym(h1, "math_add"), nullptr);

    EXPECT_EQ(lm.DlClose(h1), 0);
    EXPECT_EQ(lm.FindModule(math_path.string()), nullptr);
    EXPECT_EQ(lm.DlSym(h1, "math_add"), nullptr);

    MEMORY_BASIC_INFORMATION mbi{};
    ASSERT_NE(VirtualQuery(reinterpret_cast<void *>(seg_vaddr), &mbi, sizeof(mbi)), 0u);
    EXPECT_NE(mbi.State, static_cast<DWORD>(MEM_COMMIT));
}

TEST_F(LinkerExtensionsTest, PthreadIntrospectionAndAttributes)
{
    auto &reg = ndk::NdkSymbolRegistry::Instance();

    using SelfFn = __attribute__((sysv_abi)) uint64_t (*)();
    using EqualFn = __attribute__((sysv_abi)) int (*)(uint64_t, uint64_t);
    using SetNameFn = __attribute__((sysv_abi)) int (*)(uint64_t, const char *);
    using GetNameFn = __attribute__((sysv_abi)) int (*)(uint64_t, char *, size_t);
    using GetAttrFn = __attribute__((sysv_abi)) int (*)(uint64_t, ::BionicPthreadAttr *);
    using AttrInitFn = __attribute__((sysv_abi)) int (*)(::BionicPthreadAttr *);
    using AttrDestroyFn = __attribute__((sysv_abi)) int (*)(::BionicPthreadAttr *);
    using SetSizeFn = __attribute__((sysv_abi)) int (*)(::BionicPthreadAttr *, size_t);
    using GetSizeFn = __attribute__((sysv_abi)) int (*)(const ::BionicPthreadAttr *, size_t *);
    using SetDetachFn = __attribute__((sysv_abi)) int (*)(::BionicPthreadAttr *, int);
    using GetDetachFn = __attribute__((sysv_abi)) int (*)(const ::BionicPthreadAttr *, int *);

    auto p_self = reinterpret_cast<SelfFn>(reg.Lookup("pthread_self"));
    auto p_equal = reinterpret_cast<EqualFn>(reg.Lookup("pthread_equal"));
    auto p_setname = reinterpret_cast<SetNameFn>(reg.Lookup("pthread_setname_np"));
    auto p_getname = reinterpret_cast<GetNameFn>(reg.Lookup("pthread_getname_np"));
    auto p_getattr = reinterpret_cast<GetAttrFn>(reg.Lookup("pthread_getattr_np"));
    auto p_attr_init = reinterpret_cast<AttrInitFn>(reg.Lookup("pthread_attr_init"));
    auto p_attr_destroy = reinterpret_cast<AttrDestroyFn>(reg.Lookup("pthread_attr_destroy"));
    auto p_set_stacksize = reinterpret_cast<SetSizeFn>(reg.Lookup("pthread_attr_setstacksize"));
    auto p_get_stacksize = reinterpret_cast<GetSizeFn>(reg.Lookup("pthread_attr_getstacksize"));
    auto p_set_guardsize = reinterpret_cast<SetSizeFn>(reg.Lookup("pthread_attr_setguardsize"));
    auto p_get_guardsize = reinterpret_cast<GetSizeFn>(reg.Lookup("pthread_attr_getguardsize"));
    auto p_set_detach = reinterpret_cast<SetDetachFn>(reg.Lookup("pthread_attr_setdetachstate"));
    auto p_get_detach = reinterpret_cast<GetDetachFn>(reg.Lookup("pthread_attr_getdetachstate"));

    ASSERT_NE(p_self, nullptr);
    ASSERT_NE(p_equal, nullptr);
    ASSERT_NE(p_setname, nullptr);
    ASSERT_NE(p_getname, nullptr);
    ASSERT_NE(p_getattr, nullptr);
    ASSERT_NE(p_attr_init, nullptr);
    ASSERT_NE(p_attr_destroy, nullptr);
    ASSERT_NE(p_set_stacksize, nullptr);
    ASSERT_NE(p_get_stacksize, nullptr);
    ASSERT_NE(p_set_guardsize, nullptr);
    ASSERT_NE(p_get_guardsize, nullptr);
    ASSERT_NE(p_set_detach, nullptr);
    ASSERT_NE(p_get_detach, nullptr);

    uint64_t self = p_self();
    EXPECT_NE(self, 0ULL);
    EXPECT_NE(p_equal(self, self), 0);
    EXPECT_EQ(p_equal(self, self + 1), 0);

    EXPECT_EQ(p_setname(self, "unit_worker_1"), 0);
    EXPECT_NE(p_setname(self, "name_longer_than_fifteen_chars"), 0);

    char name[32]{};
    EXPECT_EQ(p_getname(self, name, sizeof(name)), 0);
    EXPECT_STREQ(name, "unit_worker_1");
    EXPECT_NE(p_getname(self, name, 4), 0);

    ::BionicPthreadAttr attr{};
    EXPECT_EQ(p_attr_init(&attr), 0);
    EXPECT_EQ(p_getattr(self, &attr), 0);

    size_t sz = 0;
    EXPECT_EQ(p_set_stacksize(&attr, 2u * 1024u * 1024u), 0);
    EXPECT_EQ(p_get_stacksize(&attr, &sz), 0);
    EXPECT_EQ(sz, 2u * 1024u * 1024u);

    size_t guard = 0;
    EXPECT_EQ(p_set_guardsize(&attr, 8192u), 0);
    EXPECT_EQ(p_get_guardsize(&attr, &guard), 0);
    EXPECT_EQ(guard, 8192u);

    int detach = 0;
    EXPECT_EQ(p_set_detach(&attr, 1), 0);
    EXPECT_EQ(p_get_detach(&attr, &detach), 0);
    EXPECT_EQ(detach, 1);

    EXPECT_EQ(p_attr_destroy(&attr), 0);
}
