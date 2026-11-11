#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "elf/link_map.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/atrace.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "property/property_manager.hpp"
#include "thread/thread_manager.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <windows.h>

namespace stdfs = std::filesystem;
using namespace portage;
using namespace portage::elf;

namespace {

struct SyntheticElfBuilder {
    std::vector<uint8_t> data;

    explicit SyntheticElfBuilder(size_t size = 0x4000) : data(size, 0) {}

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

std::vector<int> g_atexit_log;
void *g_reentrant_resolved_sym = nullptr;

__attribute__((sysv_abi)) void RecordAtExitValue(void *arg)
{
    g_atexit_log.push_back(static_cast<int>(reinterpret_cast<intptr_t>(arg)));
}

__attribute__((sysv_abi)) void PlainAtExitCallback() { g_atexit_log.push_back(999); }

__attribute__((sysv_abi)) void ReentrantInitCallback()
{
    void *self_handle = dlopen(nullptr, RTLD_NOW);
    if (self_handle != nullptr) {
        g_reentrant_resolved_sym = dlsym(RTLD_DEFAULT, "reentrant_exported_var");
    }
}

__attribute__((sysv_abi)) int CollectPhdrCallback(dl_phdr_info *info, size_t size, void *data)
{
    EXPECT_GE(size, sizeof(dl_phdr_info));
    auto *vec = static_cast<std::vector<dl_phdr_info> *>(data);
    vec->push_back(*info);
    return 0;
}

__attribute__((sysv_abi)) int EarlyExitPhdrCallback(dl_phdr_info *info, size_t size, void *data)
{
    (void)info;
    (void)size;
    int *count = static_cast<int *>(data);
    (*count)++;
    return 77;
}

} // namespace

class LinkerHardeningTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
        LinkMap::Instance().Reset();
        LinkMap::Instance().SetAddressSpace(address_space_.get());
        LinkMap::Instance().SetVfs(vfs_.get());
        g_atexit_log.clear();
        g_reentrant_resolved_sym = nullptr;
    }

    void TearDown() override
    {
        LinkMap::Instance().Reset();
        vfs_.reset();
        address_space_.reset();
    }

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
};

TEST_F(LinkerHardeningTest, DlIteratePhdrEnumeratesModulesAndRespectsEarlyExit)
{
    auto &lm = LinkMap::Instance();
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    stdfs::path plugin_path = FindGuestBinary("libgame_plugin.so");
    ASSERT_TRUE(stdfs::exists(math_path));
    ASSERT_TRUE(stdfs::exists(plugin_path));

    lm.AddSearchPath(math_path.parent_path().string());

    void *h1 = dlopen("libgame_math.so", RTLD_NOW);
    void *h2 = dlopen("libgame_plugin.so", RTLD_NOW);
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h2, nullptr);

    std::vector<dl_phdr_info> infos;
    int rc = dl_iterate_phdr(CollectPhdrCallback, &infos);
    EXPECT_EQ(rc, 0);
    ASSERT_EQ(infos.size(), 2U);

    for (const auto &info : infos) {
        EXPECT_NE(info.dlpi_addr, 0ULL);
        EXPECT_NE(info.dlpi_name, nullptr);
        EXPECT_NE(info.dlpi_phdr, nullptr);
        EXPECT_GT(info.dlpi_phnum, 0U);
        EXPECT_EQ(info.dlpi_adds, 2ULL);
        EXPECT_EQ(info.dlpi_subs, 0ULL);
    }

    int count = 0;
    int early_rc = dl_iterate_phdr(EarlyExitPhdrCallback, &count);
    EXPECT_EQ(early_rc, 77);
    EXPECT_EQ(count, 1);

    EXPECT_EQ(dlclose(h2), 0);
    EXPECT_EQ(dlclose(h1), 0);
}

TEST_F(LinkerHardeningTest, PtTlsAndTlsGetAddrIsolatePerThreadBlocks)
{
    SyntheticElfBuilder builder(0x3000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t tls_data_off = 0x800;

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

    const uint64_t tdata_init[2] = {0xCAFEBABE11223344ULL, 0xDEADBEEF55667788ULL};
    builder.WriteBytes(tls_data_off, tdata_init, sizeof(tdata_init));

    Elf64_Phdr ph_tls{};
    ph_tls.p_type = PT_TLS;
    ph_tls.p_flags = PF_R;
    ph_tls.p_offset = tls_data_off;
    ph_tls.p_vaddr = tls_data_off;
    ph_tls.p_paddr = tls_data_off;
    ph_tls.p_filesz = sizeof(tdata_init);
    ph_tls.p_memsz = 32; // 16 bytes .tdata + 16 bytes .tbss
    ph_tls.p_align = 16;
    builder.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_tls);

    ElfLoader loader(address_space_.get());
    auto res = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(res));
    auto loaded = std::get<LoadedElf>(res);
    EXPECT_GT(loaded.tls_modid, 0U);
    LinkMap::Instance().RegisterLoadedElf(loaded, "libtls_test.so");

    TlsIndex ti_tdata{loaded.tls_modid, 0};
    TlsIndex ti_tbss{loaded.tls_modid, 16};

    auto *main_tdata = static_cast<uint64_t *>(__tls_get_addr(&ti_tdata));
    auto *main_tbss = static_cast<uint64_t *>(__tls_get_addr(&ti_tbss));
    ASSERT_NE(main_tdata, nullptr);
    ASSERT_NE(main_tbss, nullptr);
    EXPECT_EQ(main_tdata[0], 0xCAFEBABE11223344ULL);
    EXPECT_EQ(main_tdata[1], 0xDEADBEEF55667788ULL);
    EXPECT_EQ(main_tbss[0], 0ULL);
    EXPECT_EQ(main_tbss[1], 0ULL);

    main_tdata[0] = 0x1111111111111111ULL;
    main_tbss[0] = 0x2222222222222222ULL;

    uint64_t worker_initial_tdata = 0;
    uint64_t worker_initial_tbss = 99;
    std::thread worker([&]() {
        auto *w_tdata = static_cast<uint64_t *>(__tls_get_addr(&ti_tdata));
        auto *w_tbss = static_cast<uint64_t *>(__tls_get_addr(&ti_tbss));
        worker_initial_tdata = w_tdata[0];
        worker_initial_tbss = w_tbss[0];
        w_tdata[0] = 0x9999999999999999ULL;
        LinkMap::Instance().CleanupCurrentThreadTls();
    });
    worker.join();

    EXPECT_EQ(worker_initial_tdata, 0xCAFEBABE11223344ULL);
    EXPECT_EQ(worker_initial_tbss, 0ULL);
    EXPECT_EQ(main_tdata[0], 0x1111111111111111ULL);
    EXPECT_EQ(main_tbss[0], 0x2222222222222222ULL);
}

TEST_F(LinkerHardeningTest, CrossModuleTlsRelocationsResolveViaLinkMap)
{
    // Module A exports STT_TLS symbol "shared_tls_var" at offset 24
    SyntheticElfBuilder mod_a(0x2000);
    {
        constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t strtab_off = 0x400;
        constexpr size_t symtab_off = 0x500;
        constexpr size_t hash_off = 0x600;
        constexpr size_t tls_off = 0x800;

        Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
        ehdr.e_ident[EI_CLASS] = ELFCLASS64;
        ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
        ehdr.e_ident[EI_VERSION] = 1;
        ehdr.e_type = ET_DYN;
        ehdr.e_machine = EM_X86_64;
        ehdr.e_version = 1;
        ehdr.e_phoff = phdr_off;
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = 3;
        mod_a.WriteAt(0, ehdr);

        Elf64_Phdr ph_load{PT_LOAD, PF_R | PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
        mod_a.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0shared_tls_var\0";
        mod_a.WriteBytes(strtab_off, strtab, sizeof(strtab));

        Elf64_Sym syms[2]{};
        syms[1].st_name = 1;
        syms[1].st_info = (1 << 4) | STT_TLS; // STB_GLOBAL | STT_TLS
        syms[1].st_shndx = 1;
        syms[1].st_value = 24;
        syms[1].st_size = 8;
        mod_a.WriteBytes(symtab_off, syms, sizeof(syms));

        uint32_t sysv_hash[6] = {1, 2, 1, 0, 0, 0};
        mod_a.WriteBytes(hash_off, sysv_hash, sizeof(sysv_hash));

        Elf64_Dyn dyns[5]{};
        dyns[0] = {DT_STRTAB, {strtab_off}};
        dyns[1] = {DT_SYMTAB, {symtab_off}};
        dyns[2] = {DT_HASH, {hash_off}};
        dyns[3] = {DT_NULL, {0}};
        mod_a.WriteBytes(dyn_off, dyns, sizeof(dyns));

        Elf64_Phdr ph_dyn{PT_DYNAMIC, PF_R | PF_W,  dyn_off,      dyn_off,
                          dyn_off,    sizeof(dyns), sizeof(dyns), 8};
        mod_a.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);

        Elf64_Phdr ph_tls{PT_TLS, PF_R, tls_off, tls_off, tls_off, 16, 64, 16};
        mod_a.WriteAt(phdr_off + 2 * sizeof(Elf64_Phdr), ph_tls);
    }

    ElfLoader loader(address_space_.get());
    auto res_a = loader.LoadFromMemory(mod_a.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(res_a));
    auto elf_a = std::get<LoadedElf>(res_a);
    LinkMap::Instance().RegisterLoadedElf(elf_a, "libmod_a.so");

    // Module B has undefined STT_TLS reference to "shared_tls_var" via R_X86_64_DTPMOD64 +
    // R_X86_64_DTPOFF64
    SyntheticElfBuilder mod_b(0x2000);
    constexpr size_t got_off = 0x700;
    {
        constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t rela_off = 0x300;
        constexpr size_t strtab_off = 0x400;
        constexpr size_t symtab_off = 0x500;

        Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
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
        mod_b.WriteAt(0, ehdr);

        Elf64_Phdr ph_load{PT_LOAD, PF_R | PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
        mod_b.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0shared_tls_var\0";
        mod_b.WriteBytes(strtab_off, strtab, sizeof(strtab));

        Elf64_Sym syms[2]{};
        syms[1].st_name = 1;
        syms[1].st_info = (1 << 4) | STT_TLS;
        syms[1].st_shndx = SHN_UNDEF;
        syms[1].st_value = 0;
        mod_b.WriteBytes(symtab_off, syms, sizeof(syms));

        Elf64_Rela relas[2]{};
        relas[0].r_offset = got_off;
        relas[0].r_info = (1ULL << 32) | R_X86_64_DTPMOD64;
        relas[0].r_addend = 0;
        relas[1].r_offset = got_off + 8;
        relas[1].r_info = (1ULL << 32) | R_X86_64_DTPOFF64;
        relas[1].r_addend = 4;
        mod_b.WriteBytes(rela_off, relas, sizeof(relas));

        Elf64_Dyn dyns[5]{};
        dyns[0] = {DT_STRTAB, {strtab_off}};
        dyns[1] = {DT_SYMTAB, {symtab_off}};
        dyns[2] = {DT_RELA, {rela_off}};
        dyns[3] = {DT_RELASZ, {sizeof(relas)}};
        dyns[4] = {DT_NULL, {0}};
        mod_b.WriteBytes(dyn_off, dyns, sizeof(dyns));

        Elf64_Phdr ph_dyn{PT_DYNAMIC, PF_R | PF_W,  dyn_off,      dyn_off,
                          dyn_off,    sizeof(dyns), sizeof(dyns), 8};
        mod_b.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);
    }

    auto res_b = loader.LoadFromMemory(mod_b.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(res_b));
    auto elf_b = std::get<LoadedElf>(res_b);

    const auto *got = reinterpret_cast<const uint64_t *>(elf_b.base_address + got_off);
    EXPECT_EQ(got[0], elf_a.tls_modid);
    EXPECT_EQ(got[1], 28ULL); // 24 + addend 4
}

TEST_F(LinkerHardeningTest, CxaAtExitExecutesInLifoOrderOnDlCloseAndFinalize)
{
    void *dso_a = reinterpret_cast<void *>(0xA000);
    void *dso_b = reinterpret_cast<void *>(0xB000);

    EXPECT_EQ(portage_cxa_atexit(RecordAtExitValue, reinterpret_cast<void *>(1), dso_a), 0);
    EXPECT_EQ(portage_cxa_atexit(RecordAtExitValue, reinterpret_cast<void *>(2), dso_b), 0);
    EXPECT_EQ(portage_cxa_atexit(RecordAtExitValue, reinterpret_cast<void *>(3), dso_a), 0);
    EXPECT_EQ(portage_guest_atexit(PlainAtExitCallback), 0);

    portage_cxa_finalize(dso_a);
    ASSERT_EQ(g_atexit_log.size(), 2U);
    EXPECT_EQ(g_atexit_log[0], 3);
    EXPECT_EQ(g_atexit_log[1], 1);

    // Calling again for dso_a should not re-run already finalized entries
    portage_cxa_finalize(dso_a);
    EXPECT_EQ(g_atexit_log.size(), 2U);

    // Finalizing nullptr runs all remaining entries in LIFO order
    portage_cxa_finalize(nullptr);
    ASSERT_EQ(g_atexit_log.size(), 4U);
    EXPECT_EQ(g_atexit_log[2], 999);
    EXPECT_EQ(g_atexit_log[3], 2);
}

TEST_F(LinkerHardeningTest, CxaThreadAtExitRunsOnGuestThreadExit)
{
    std::thread worker([]() {
        int tid = thread::ThreadManager::Instance().GetCurrentTid();
        EXPECT_EQ(portage_cxa_thread_atexit_impl(RecordAtExitValue, reinterpret_cast<void *>(10),
                                                 nullptr),
                  0);
        EXPECT_EQ(portage_cxa_thread_atexit_impl(RecordAtExitValue, reinterpret_cast<void *>(20),
                                                 nullptr),
                  0);
        EXPECT_EQ(portage_cxa_thread_atexit_impl(RecordAtExitValue, reinterpret_cast<void *>(30),
                                                 nullptr),
                  0);
        thread::ThreadManager::Instance().OnThreadExit(tid, 0);
    });
    worker.join();

    ASSERT_EQ(g_atexit_log.size(), 3U);
    EXPECT_EQ(g_atexit_log[0], 30);
    EXPECT_EQ(g_atexit_log[1], 20);
    EXPECT_EQ(g_atexit_log[2], 10);
}

TEST_F(LinkerHardeningTest, ReentrantDlOpenAndDlSymInsideInitArraySucceedWithoutDeadlock)
{
    LoadedElf synthetic{};
    synthetic.base_address = 0x500000;
    synthetic.symbols["reentrant_exported_var"] = 0x500123;
    synthetic.init_functions.push_back(reinterpret_cast<uintptr_t>(&ReentrantInitCallback));

    auto entry = LinkMap::Instance().RegisterLoadedElf(synthetic, "libreentrant.so");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->init_called);
    EXPECT_EQ(g_reentrant_resolved_sym, reinterpret_cast<void *>(0x500123));
}

TEST_F(LinkerHardeningTest, PtGnuRelroMarksRegionReadOnlyAfterRelocation)
{
    SyntheticElfBuilder builder(0x3000);

    constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
    constexpr size_t dyn_off = 0x200;
    constexpr size_t rela_off = 0x300;
    constexpr size_t relro_page_off = 0x1000;

    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident,
                "\x7F"
                "ELF",
                4);
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = 1;
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_phoff = phdr_off;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = 3;
    builder.WriteAt(0, ehdr);

    Elf64_Phdr ph_load{PT_LOAD, PF_R | PF_W, 0, 0, 0, 0x3000, 0x3000, 0x1000};
    builder.WriteAt(phdr_off, ph_load);

    Elf64_Rela rel{};
    rel.r_offset = relro_page_off;
    rel.r_info = R_X86_64_RELATIVE;
    rel.r_addend = 0x777;
    builder.WriteAt(rela_off, rel);

    Elf64_Dyn dyns[3]{};
    dyns[0] = {DT_RELA, {rela_off}};
    dyns[1] = {DT_RELASZ, {sizeof(Elf64_Rela)}};
    dyns[2] = {DT_NULL, {0}};
    builder.WriteBytes(dyn_off, dyns, sizeof(dyns));

    Elf64_Phdr ph_dyn{PT_DYNAMIC, PF_R | PF_W,  dyn_off,      dyn_off,
                      dyn_off,    sizeof(dyns), sizeof(dyns), 8};
    builder.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);

    Elf64_Phdr ph_relro{PT_GNU_RELRO,   PF_R,   relro_page_off, relro_page_off,
                        relro_page_off, 0x1000, 0x1000,         0x1000};
    builder.WriteAt(phdr_off + 2 * sizeof(Elf64_Phdr), ph_relro);

    ElfLoader loader(address_space_.get());
    auto res = loader.LoadFromMemory(builder.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(res));
    auto elf = std::get<LoadedElf>(res);

    uintptr_t relro_addr = elf.base_address + relro_page_off;
    EXPECT_EQ(*reinterpret_cast<const uintptr_t *>(relro_addr), elf.base_address + 0x777);

    MEMORY_BASIC_INFORMATION mbi{};
    ASSERT_NE(VirtualQuery(reinterpret_cast<LPCVOID>(relro_addr), &mbi, sizeof(mbi)), 0U);
    EXPECT_EQ(mbi.Protect, PAGE_READONLY);
}

TEST_F(LinkerHardeningTest, DtRunpathAndOriginResolveNestedDependency)
{
    stdfs::path temp_dir = stdfs::temp_directory_path() / "portage_runpath_test";
    stdfs::path sub_dir = temp_dir / "nested_deps";
    stdfs::create_directories(sub_dir);

    stdfs::path math_src = FindGuestBinary("libgame_math.so");
    ASSERT_TRUE(stdfs::exists(math_src));
    stdfs::path nested_dep = sub_dir / "libnested_dep.so";
    stdfs::copy_file(math_src, nested_dep, stdfs::copy_options::overwrite_existing);

    // Build parent library with DT_RUNPATH = "$ORIGIN/nested_deps" and DT_NEEDED =
    // "libnested_dep.so"
    SyntheticElfBuilder parent(0x2000);
    {
        constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t strtab_off = 0x400;

        Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
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
        parent.WriteAt(0, ehdr);

        Elf64_Phdr ph_load{PT_LOAD, PF_R | PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
        parent.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0$ORIGIN/nested_deps\0libnested_dep.so\0";
        parent.WriteBytes(strtab_off, strtab, sizeof(strtab));

        Elf64_Dyn dyns[4]{};
        dyns[0] = {DT_STRTAB, {strtab_off}};
        dyns[1] = {DT_RUNPATH, {1}};
        dyns[2] = {DT_NEEDED, {21}};
        dyns[3] = {DT_NULL, {0}};
        parent.WriteBytes(dyn_off, dyns, sizeof(dyns));

        Elf64_Phdr ph_dyn{PT_DYNAMIC, PF_R | PF_W,  dyn_off,      dyn_off,
                          dyn_off,    sizeof(dyns), sizeof(dyns), 8};
        parent.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);
    }

    stdfs::path parent_path = temp_dir / "libparent_origin.so";
    {
        std::ofstream out(parent_path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(parent.data.data()),
                  static_cast<std::streamsize>(parent.data.size()));
    }

    void *handle = dlopen(parent_path.string().c_str(), RTLD_NOW);
    ASSERT_NE(handle, nullptr) << (dlerror() ? dlerror() : "unknown error");
    EXPECT_NE(LinkMap::Instance().FindModule("libnested_dep.so"), nullptr);

    EXPECT_EQ(dlclose(handle), 0);
    std::error_code ec;
    stdfs::remove_all(temp_dir, ec);
}

TEST_F(LinkerHardeningTest, CopyRelocationAndRtldNextResolution)
{
    uint64_t source_global = 0x8877665544332211ULL;

    LoadedElf first_mod{};
    first_mod.base_address = 0x100000;
    first_mod.segments.push_back({0x100000, 0x2000, PF_R | PF_X});
    first_mod.symbols["interposed_sym"] = 0x100100;
    first_mod.symbols["shared_global_data"] = reinterpret_cast<uintptr_t>(&source_global);
    first_mod.symbol_sizes["shared_global_data"] = sizeof(source_global);
    LinkMap::Instance().RegisterLoadedElf(first_mod, "libfirst.so");

    LoadedElf second_mod{};
    second_mod.base_address = 0x200000;
    second_mod.segments.push_back({0x200000, 0x2000, PF_R | PF_X});
    second_mod.symbols["interposed_sym"] = 0x200200;
    LinkMap::Instance().RegisterLoadedElf(second_mod, "libsecond.so");

    // Verify RTLD_NEXT from caller inside libfirst.so resolves libsecond.so's symbol
    void *default_sym =
        LinkMap::Instance().DlSymWithCaller(RTLD_DEFAULT, "interposed_sym", 0x100050);
    void *next_sym = LinkMap::Instance().DlSymWithCaller(RTLD_NEXT, "interposed_sym", 0x100050);
    EXPECT_EQ(default_sym, reinterpret_cast<void *>(0x100100));
    EXPECT_EQ(next_sym, reinterpret_cast<void *>(0x200200));

    // Verify R_X86_64_COPY copies 8-byte global value from libfirst.so into target module BSS
    SyntheticElfBuilder copy_mod(0x2000);
    constexpr size_t bss_target_off = 0x900;
    {
        constexpr size_t phdr_off = sizeof(Elf64_Ehdr);
        constexpr size_t dyn_off = 0x200;
        constexpr size_t rela_off = 0x300;
        constexpr size_t strtab_off = 0x400;
        constexpr size_t symtab_off = 0x500;

        Elf64_Ehdr ehdr{};
        std::memcpy(ehdr.e_ident,
                    "\x7F"
                    "ELF",
                    4);
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
        copy_mod.WriteAt(0, ehdr);

        Elf64_Phdr ph_load{PT_LOAD, PF_R | PF_W, 0, 0, 0, 0x2000, 0x2000, 0x1000};
        copy_mod.WriteAt(phdr_off, ph_load);

        const char strtab[] = "\0shared_global_data\0";
        copy_mod.WriteBytes(strtab_off, strtab, sizeof(strtab));

        Elf64_Sym syms[2]{};
        syms[1].st_name = 1;
        syms[1].st_info = (1 << 4) | STT_OBJECT;
        syms[1].st_shndx = 1;
        syms[1].st_value = bss_target_off;
        syms[1].st_size = sizeof(uint64_t);
        copy_mod.WriteBytes(symtab_off, syms, sizeof(syms));

        Elf64_Rela rel{};
        rel.r_offset = bss_target_off;
        rel.r_info = (1ULL << 32) | R_X86_64_COPY;
        rel.r_addend = 0;
        copy_mod.WriteAt(rela_off, rel);

        Elf64_Dyn dyns[5]{};
        dyns[0] = {DT_STRTAB, {strtab_off}};
        dyns[1] = {DT_SYMTAB, {symtab_off}};
        dyns[2] = {DT_RELA, {rela_off}};
        dyns[3] = {DT_RELASZ, {sizeof(Elf64_Rela)}};
        dyns[4] = {DT_NULL, {0}};
        copy_mod.WriteBytes(dyn_off, dyns, sizeof(dyns));

        Elf64_Phdr ph_dyn{PT_DYNAMIC, PF_R | PF_W,  dyn_off,      dyn_off,
                          dyn_off,    sizeof(dyns), sizeof(dyns), 8};
        copy_mod.WriteAt(phdr_off + sizeof(Elf64_Phdr), ph_dyn);
    }

    ElfLoader loader(address_space_.get());
    auto res = loader.LoadFromMemory(copy_mod.data);
    ASSERT_TRUE(std::holds_alternative<LoadedElf>(res));
    auto loaded_copy = std::get<LoadedElf>(res);
    uint64_t copied_val =
        *reinterpret_cast<const uint64_t *>(loaded_copy.base_address + bss_target_off);
    EXPECT_EQ(copied_val, 0x8877665544332211ULL);
}

TEST_F(LinkerHardeningTest, ATraceAndDeviceApiLevelQueries)
{
    auto &tm = ndk::TraceManager::Instance();
    tm.Clear();
    tm.SetEnabled(true);

    EXPECT_TRUE(ATrace_isEnabled());
    ATrace_beginSection("PhysicsStep");
    ATrace_setCounter("RigidBodies", 128);
    ATrace_beginAsyncSection("AssetLoad", 42);
    ATrace_endAsyncSection("AssetLoad", 42);
    ATrace_endSection();

    auto events = tm.GetEvents();
    ASSERT_EQ(events.size(), 5U);
    EXPECT_EQ(events[0].type, ndk::TraceEvent::Type::Begin);
    EXPECT_EQ(events[0].name, "PhysicsStep");
    EXPECT_EQ(events[1].type, ndk::TraceEvent::Type::Counter);
    EXPECT_EQ(events[1].name, "RigidBodies");
    EXPECT_EQ(events[1].value, 128);
    EXPECT_EQ(events[2].type, ndk::TraceEvent::Type::AsyncBegin);
    EXPECT_EQ(events[2].value, 42);
    EXPECT_EQ(events[3].type, ndk::TraceEvent::Type::AsyncEnd);
    EXPECT_EQ(events[3].value, 42);
    EXPECT_EQ(events[4].type, ndk::TraceEvent::Type::End);

    property::PropertyManager::Instance().Initialize();
    property::PropertyManager::Instance().SetProperty("ro.build.version.sdk", "34");
    EXPECT_EQ(android_get_device_api_level(), 34);
    EXPECT_GE(android_get_application_target_sdk_version(), 21);

    auto &reg = ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("dl_iterate_phdr"), 0ULL);
    EXPECT_NE(reg.Lookup("__tls_get_addr"), 0ULL);
    EXPECT_NE(reg.Lookup("__cxa_atexit"), 0ULL);
    EXPECT_NE(reg.Lookup("__cxa_finalize"), 0ULL);
    EXPECT_NE(reg.Lookup("__cxa_thread_atexit_impl"), 0ULL);
    EXPECT_NE(reg.Lookup("ATrace_beginSection"), 0ULL);
    EXPECT_NE(reg.Lookup("android_get_device_api_level"), 0ULL);
    EXPECT_NE(reg.Lookup("android_get_application_target_sdk_version"), 0ULL);
}
