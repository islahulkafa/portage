#include "elf/elf_loader.hpp"
#include "elf/link_map.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <vector>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

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

} // namespace

class LinkMapTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
        elf::LinkMap::Instance().Reset();
        elf::LinkMap::Instance().SetAddressSpace(address_space_.get());
        elf::LinkMap::Instance().SetVfs(vfs_.get());
    }

    void TearDown() override
    {
        elf::LinkMap::Instance().Reset();
        vfs_.reset();
        address_space_.reset();
    }

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
};

TEST_F(LinkMapTest, ManagesSearchPathsCorrectly)
{
    auto &lm = elf::LinkMap::Instance();
    EXPECT_EQ(lm.GetSearchPaths().size(), 0U);

    lm.AddSearchPath("/system/lib64");
    lm.AddSearchPath("/data/app/lib");
    lm.AddSearchPath("/system/lib64"); // Duplicate

    const auto &paths = lm.GetSearchPaths();
    ASSERT_EQ(paths.size(), 2U);
    EXPECT_EQ(paths[0], "/system/lib64");
    EXPECT_EQ(paths[1], "/data/app/lib");
}

TEST_F(LinkMapTest, LoadsLibraryAndExecutesConstructors)
{
    auto &lm = elf::LinkMap::Instance();
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    ASSERT_TRUE(stdfs::exists(math_path)) << "libgame_math.so not found at: " << math_path;

    lm.AddSearchPath(math_path.parent_path().string());

    void *handle = lm.DlOpen("libgame_math.so", elf::RTLD_NOW);
    ASSERT_NE(handle, nullptr) << (lm.DlError() ? lm.DlError() : "unknown error");

    // Symbol lookup for constructor verification
    void *init_status_sym = lm.DlSym(handle, "math_get_init_status");
    ASSERT_NE(init_status_sym, nullptr);

    using StatusFn = __attribute__((sysv_abi)) int (*)();
    auto status_fn = reinterpret_cast<StatusFn>(init_status_sym);
    EXPECT_EQ(status_fn(), 12345);

    // Call math_add
    void *add_sym = lm.DlSym(handle, "math_add");
    ASSERT_NE(add_sym, nullptr);
    using AddFn = __attribute__((sysv_abi)) int (*)(int, int);
    auto add_fn = reinterpret_cast<AddFn>(add_sym);
    EXPECT_EQ(add_fn(17, 25), 42);

    // dlclose
    EXPECT_EQ(lm.DlClose(handle), 0);

    // Destructor should have run
    void *fini_status_sym = lm.DlSym(handle, "math_get_fini_status");
    ASSERT_NE(fini_status_sym, nullptr);
    auto fini_fn = reinterpret_cast<StatusFn>(fini_status_sym);
    EXPECT_EQ(fini_fn(), 54321);
}

TEST_F(LinkMapTest, PerformsRecursiveDtNeededResolution)
{
    auto &lm = elf::LinkMap::Instance();
    stdfs::path engine_path = FindGuestBinary("libtest_game_engine.so");
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    ASSERT_TRUE(stdfs::exists(engine_path))
        << "libtest_game_engine.so not found at: " << engine_path;
    ASSERT_TRUE(stdfs::exists(math_path)) << "libgame_math.so not found at: " << math_path;

    lm.AddSearchPath(engine_path.parent_path().string());
    lm.AddSearchPath(math_path.parent_path().string());

    void *handle = lm.DlOpen("libtest_game_engine.so", elf::RTLD_NOW);
    ASSERT_NE(handle, nullptr) << (lm.DlError() ? lm.DlError() : "unknown error");

    // libtest_game_engine.so declares DT_NEEDED libgame_math.so
    // Verify libgame_math.so is present in LinkMap
    auto math_mod = lm.FindModule("libgame_math.so");
    ASSERT_NE(math_mod, nullptr);
    EXPECT_TRUE(math_mod->init_called);

    // Query symbol through dependent handle
    void *math_add_sym = lm.DlSym(handle, "math_add");
    ASSERT_NE(math_add_sym, nullptr);

    using AddFn = __attribute__((sysv_abi)) int (*)(int, int);
    auto add_fn = reinterpret_cast<AddFn>(math_add_sym);
    EXPECT_EQ(add_fn(100, 23), 123);

    EXPECT_EQ(lm.DlClose(handle), 0);
}

TEST_F(LinkMapTest, QueriesAddressMetadataViaDlAddr)
{
    auto &lm = elf::LinkMap::Instance();
    stdfs::path plugin_path = FindGuestBinary("libgame_plugin.so");
    ASSERT_TRUE(stdfs::exists(plugin_path)) << "libgame_plugin.so not found at: " << plugin_path;

    lm.AddSearchPath(plugin_path.parent_path().string());

    void *handle = lm.DlOpen("libgame_plugin.so", elf::RTLD_NOW);
    ASSERT_NE(handle, nullptr);

    void *compute_sym = lm.DlSym(handle, "plugin_compute");
    ASSERT_NE(compute_sym, nullptr);

    elf::Dl_info info{};
    int ret = lm.DlAddr(compute_sym, &info);
    EXPECT_EQ(ret, 1);
    EXPECT_NE(info.dli_fname, nullptr);
    EXPECT_NE(info.dli_fbase, nullptr);
    ASSERT_NE(info.dli_sname, nullptr);
    EXPECT_STREQ(info.dli_sname, "plugin_compute");
    EXPECT_EQ(info.dli_saddr, compute_sym);

    // Address outside any module returns 0
    elf::Dl_info outside_info{};
    EXPECT_EQ(lm.DlAddr(reinterpret_cast<const void *>(0x1234), &outside_info), 0);

    EXPECT_EQ(lm.DlClose(handle), 0);
}

TEST_F(LinkMapTest, ReportsAndClearsErrorsViaDlError)
{
    auto &lm = elf::LinkMap::Instance();

    // Initially no error
    EXPECT_EQ(lm.DlError(), nullptr);

    // Attempt to open missing library
    void *h = lm.DlOpen("non_existent_library_12345.so", elf::RTLD_NOW);
    EXPECT_EQ(h, nullptr);

    char *err = lm.DlError();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(std::string(err).find("library not found"), std::string::npos);

    // Subsequent dlerror clears error
    EXPECT_EQ(lm.DlError(), nullptr);

    // Symbol lookup failure
    stdfs::path math_path = FindGuestBinary("libgame_math.so");
    lm.AddSearchPath(math_path.parent_path().string());
    void *valid_h = lm.DlOpen("libgame_math.so", elf::RTLD_NOW);
    ASSERT_NE(valid_h, nullptr);

    void *missing_sym = lm.DlSym(valid_h, "non_existent_symbol");
    EXPECT_EQ(missing_sym, nullptr);
    err = lm.DlError();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(std::string(err).find("symbol not found"), std::string::npos);

    EXPECT_EQ(lm.DlClose(valid_h), 0);
}

TEST_F(LinkMapTest, InvokesSysVThunksSeamlessly)
{
    auto &lm = elf::LinkMap::Instance();
    stdfs::path plugin_path = FindGuestBinary("libgame_plugin.so");
    lm.AddSearchPath(plugin_path.parent_path().string());

    void *h = dlopen("libgame_plugin.so", elf::RTLD_NOW);
    ASSERT_NE(h, nullptr);

    void *sym = dlsym(h, "plugin_compute");
    ASSERT_NE(sym, nullptr);

    using ComputeFn = __attribute__((sysv_abi)) int (*)(int);
    auto compute_fn = reinterpret_cast<ComputeFn>(sym);
    EXPECT_EQ(compute_fn(5), 55);

    elf::Dl_info info{};
    EXPECT_EQ(dladdr(sym, &info), 1);
    EXPECT_STREQ(info.dli_sname, "plugin_compute");

    EXPECT_EQ(dlclose(h), 0);
}
