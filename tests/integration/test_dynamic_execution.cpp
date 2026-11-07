#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <filesystem>
#include <gtest/gtest.h>
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

stdfs::path FindRootfs()
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("guests/rootfs"),
        stdfs::path("tests/guests/rootfs"),
        stdfs::current_path() / "guests" / "rootfs",
        stdfs::current_path() / "tests" / "guests" / "rootfs",
        stdfs::current_path() / "build" / "debug" / "tests" / "guests" / "rootfs",
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return stdfs::path("rootfs");
}

} // namespace

class DynamicExecutionFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>(FindRootfs());
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);
        loader_ = std::make_unique<elf::ElfLoader>(address_space_.get());
        guest_context_ = std::make_unique<arch::GuestContext>();
        ASSERT_TRUE(guest_context_->InitializeStack());
    }

    void TearDown() override
    {
        syscall::SyscallDispatcher::Shutdown();
        guest_context_.reset();
        loader_.reset();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();
    }

    int ExecuteDynamicGuest(const stdfs::path &elf_path)
    {
        vfs_->SetExecutablePath(elf_path);
        auto load_res = loader_->LoadFromFile(elf_path.string(), vfs_.get());
        if (!std::holds_alternative<elf::LoadedElf>(load_res)) {
            auto err = std::get<elf::LoaderError>(load_res);
            std::cerr << "[ExecuteDynamicGuest] Failed to load ELF: " << elf_path
                      << " code=" << static_cast<int>(err) << "\n";
            return -999;
        }

        const auto &loaded = std::get<elf::LoadedElf>(load_res);
        EXPECT_EQ(loaded.interpreter_path, "/system/bin/linker64");
        EXPECT_NE(loaded.interpreter_base, 0ULL);
        EXPECT_NE(loaded.app_entry_point, 0ULL);
        EXPECT_NE(loaded.entry_point, loaded.app_entry_point);

        std::vector<std::string> args = {elf_path.string()};
        std::vector<std::string> env = {"PATH=/system/bin", "ANDROID_DATA=/data",
                                        "ANDROID_ROOT=/system"};

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

TEST_F(DynamicExecutionFixture, ExecutesDynamicBinaryViaInterpreter)
{
    stdfs::path guest_path = FindGuestBinary("hello_dynamic.elf");
    ASSERT_TRUE(stdfs::exists(guest_path)) << "Dynamic guest binary not found at: " << guest_path;
    int exit_code = ExecuteDynamicGuest(guest_path);
    EXPECT_EQ(exit_code, 0);
}
