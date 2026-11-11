#include "arch/guest_context.hpp"
#include "camera/camera_manager.hpp"
#include "elf/elf_loader.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
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

class CameraExecutionTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        camera::CameraManager::Instance().SetForceSynthetic(true);
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("rootfs");
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);
        thread::ThreadManager::Instance().RegisterMainThread();
        loader_ = std::make_unique<elf::ElfLoader>(address_space_.get());
        guest_context_ = std::make_unique<arch::GuestContext>();
        ASSERT_TRUE(guest_context_->InitializeStack());

        auto &lm = elf::LinkMap::Instance();
        lm.Reset();
        lm.SetAddressSpace(address_space_.get());
        lm.SetVfs(vfs_.get());

        (void)ndk::NdkSymbolRegistry::Instance();
    }

    void TearDown() override
    {
        camera::CameraManager::Instance().SetForceSynthetic(false);
        elf::LinkMap::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        guest_context_.reset();
        loader_.reset();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();
    }

    int ExecuteDynamicGuest(const stdfs::path &elf_path)
    {
        auto resolver = [](std::string_view name) -> uint64_t {
            return elf::LinkMap::Instance().ResolveGlobalSymbol(name);
        };
        auto dep_loader = [](const std::vector<std::string> &needed) {
            for (const auto &soname : needed) {
                elf::LinkMap::Instance().DlOpen(soname.c_str(), elf::RTLD_LAZY);
            }
        };

        elf::LoadOptions opts{};
        opts.lazy_binding = true;

        auto load_res =
            loader_->LoadFromFile(elf_path.string(), vfs_.get(), resolver, dep_loader, opts);
        if (!std::holds_alternative<elf::LoadedElf>(load_res)) {
            auto err = std::get<elf::LoaderError>(load_res);
            std::cerr << "[ExecuteDynamicGuest] Failed to load ELF: " << elf_path
                      << " code=" << static_cast<int>(err) << "\n";
            return -999;
        }

        const auto &loaded = std::get<elf::LoadedElf>(load_res);
        elf::LinkMap::Instance().RegisterLoadedElf(loaded, elf_path.string());

        std::vector<std::string> args = {elf_path.string()};
        std::vector<std::string> env = {"PATH=/system/bin", "ANDROID_DATA=/data"};

        void *sp = guest_context_->SetupStack(args, env, loaded.auxv_entries);
        if (!sp) {
            return -998;
        }

        return guest_context_->Run(reinterpret_cast<void *>(loaded.entry_point), sp);
    }

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
    std::unique_ptr<elf::ElfLoader> loader_;
    std::unique_ptr<arch::GuestContext> guest_context_;
};

TEST_F(CameraExecutionTest, Camera2PipelineAndCallbacksGuestElf)
{
    stdfs::path guest_path = FindGuestBinary("test_camera2_pipeline.elf");
    ASSERT_TRUE(stdfs::exists(guest_path)) << "Guest binary not found at: " << guest_path;

    stdfs::path rootfs_path = guest_path.parent_path() / "rootfs";
    vfs_->SetRootfs(rootfs_path);

    auto &lm = elf::LinkMap::Instance();
    lm.AddSearchPath("/system/lib64");
    lm.AddSearchPath((rootfs_path / "system" / "lib64").string());

    int exit_code = ExecuteDynamicGuest(guest_path);
    const char *err = lm.DlError();
    EXPECT_EQ(exit_code, 0) << "dlerror: " << (err ? err : "(none)");
}
