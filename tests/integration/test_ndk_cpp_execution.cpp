#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <windows.h>

#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

stdfs::path FindArtifact(const std::string &rel)
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path(rel),
        stdfs::path("..") / rel,
        stdfs::current_path() / rel,
        stdfs::current_path() / "build" / "debug" / rel,
        stdfs::current_path() / "guests" / rel,
        stdfs::current_path() / "tests" / "guests" / rel,
        stdfs::current_path() / "build" / "debug" / "tests" / "guests" / rel,
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return {};
}

struct ProcessResult {
    DWORD exit_code{1};
    std::string output{};
};

ProcessResult RunPortageCommand(const std::string &args)
{
    ProcessResult res{};
    auto portage_exe = FindArtifact("src/portage.exe");
    if (portage_exe.empty()) {
        return res;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return res;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + portage_exe.string() + "\" " + args;
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                             &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return res;
    }

    char buf[1024];
    DWORD nread = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
        res.output.append(buf, nread);
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &res.exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

class NdkCppExecutionGuestTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(address_space_.get(), vfs_.get(), fd_table_.get(),
                                               false);

        auto &lm = elf::LinkMap::Instance();
        lm.Reset();
        lm.SetAddressSpace(address_space_.get());
        lm.SetVfs(vfs_.get());

        thread::ThreadManager::Instance().RegisterMainThread();
        (void)ndk::NdkSymbolRegistry::Instance();

        loader_ = std::make_unique<elf::ElfLoader>(address_space_.get());
        guest_context_ = std::make_unique<arch::GuestContext>();
        ASSERT_TRUE(guest_context_->InitializeStack());
    }

    void TearDown() override
    {
        elf::LinkMap::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        guest_context_.reset();
        loader_.reset();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();
    }

    int ExecuteGuestElf(const stdfs::path &elf_path)
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
            std::cerr << "[ExecuteGuestElf] Failed to load ELF: " << elf_path
                      << " error code: " << static_cast<int>(err) << "\n";
            return -999;
        }

        const auto &loaded = std::get<elf::LoadedElf>(load_res);
        elf::LinkMap::Instance().RegisterLoadedElf(loaded, elf_path.string());

        std::vector<std::string> full_args = {elf_path.string()};
        std::vector<std::string> env = {"PATH=/system/bin", "ANDROID_DATA=/data"};

        void *sp = guest_context_->SetupStack(full_args, env, loaded.auxv_entries);
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

} // namespace

TEST_F(NdkCppExecutionGuestTest, BionicStdioPosixLocaleAndCppFallbackGuestElf)
{
    stdfs::path guest_path = FindArtifact("test_cpp_runtime_guest.elf");
    ASSERT_FALSE(guest_path.empty())
        << "Guest binary test_cpp_runtime_guest.elf not found in candidates";

    stdfs::path rootfs_path = guest_path.parent_path() / "rootfs";
    vfs_->SetRootfs(rootfs_path);

    auto &lm = elf::LinkMap::Instance();
    lm.AddSearchPath("/system/lib64");
    lm.AddSearchPath((rootfs_path / "system" / "lib64").string());

    int exit_code = ExecuteGuestElf(guest_path);
    const char *err = lm.DlError();
    EXPECT_EQ(exit_code, 0) << "dlerror: " << (err ? err : "(none)");
}

TEST(NdkCppExecutionTest, RealNdkCxxSharedStlAndExceptionsApk)
{
    auto apk = FindArtifact("ndk_cpp_showcase.apk");
    if (apk.empty()) {
        GTEST_SKIP()
            << "ndk_cpp_showcase.apk not built (Android NDK was not available at configure time)";
    }

    auto empty_rootfs = stdfs::temp_directory_path() / "portage_empty_rootfs_cpp_check";
    std::error_code ec;
    stdfs::remove_all(empty_rootfs, ec);
    stdfs::create_directories(empty_rootfs, ec);

    auto check_res = RunPortageCommand("--rootfs \"" + empty_rootfs.string() + "\" --check-apk \"" +
                                       apk.string() + "\"");
    stdfs::remove_all(empty_rootfs, ec);

    EXPECT_EQ(check_res.exit_code, 0) << check_res.output;
    EXPECT_NE(check_res.output.find("Unresolved strong symbols: 0"), std::string::npos)
        << check_res.output;

    auto run_rootfs = stdfs::temp_directory_path() / "portage_empty_rootfs_cpp_run";
    stdfs::remove_all(run_rootfs, ec);
    stdfs::create_directories(run_rootfs, ec);

    auto run_res = RunPortageCommand("--headless --rootfs \"" + run_rootfs.string() + "\" run \"" +
                                     apk.string() + "\"");
    stdfs::remove_all(run_rootfs, ec);

    EXPECT_EQ(run_res.exit_code, 0) << run_res.output;
    EXPECT_NE(run_res.output.find("[ndk-cpp-showcase] ALL CHECKS PASSED"), std::string::npos)
        << run_res.output;
}
