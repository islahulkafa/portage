#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include "vulkan/vulkan_bridge.hpp"
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

class VulkanExecutionFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
        vulkan::VulkanBridge::Instance().Initialize();
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("rootfs");
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);
        thread::ThreadManager::Instance().RegisterMainThread();
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

    int ExecuteGuest(const stdfs::path &elf_path)
    {
        auto load_res = loader_->LoadFromFile(elf_path.string());
        if (!std::holds_alternative<elf::LoadedElf>(load_res)) {
            auto err = std::get<elf::LoaderError>(load_res);
            std::cerr << "[ExecuteGuest] Failed to load ELF: " << elf_path
                      << " code=" << static_cast<int>(err) << "\n";
            return -999;
        }

        const auto &loaded = std::get<elf::LoadedElf>(load_res);
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

TEST_F(VulkanExecutionFixture, RunsVulkanDeviceAndBufferOperations)
{
    stdfs::path guest_path = FindGuestBinary("test_vulkan_device.elf");
    ASSERT_TRUE(stdfs::exists(guest_path)) << "Guest binary not found at: " << guest_path;
    int exit_code = ExecuteGuest(guest_path);
    EXPECT_EQ(exit_code, 0);
}

TEST_F(VulkanExecutionFixture, RunsGuestVulkanComputePipelineAndFence)
{
    stdfs::path guest_path = FindGuestBinary("test_vulkan_compute_guest.elf");
    ASSERT_TRUE(stdfs::exists(guest_path)) << "Guest binary not found at: " << guest_path;
    int exit_code = ExecuteGuest(guest_path);
    EXPECT_EQ(exit_code, 0);
}
