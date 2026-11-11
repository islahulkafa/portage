#include <windows.h>

#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace stdfs = std::filesystem;

namespace {

stdfs::path FindArtifact(const std::string &rel)
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path(rel),
        stdfs::path("..") / rel,
        stdfs::current_path() / rel,
        stdfs::current_path() / "build" / "debug" / rel,
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

} // namespace

TEST(NdkCompatExecutionTest, CheckApkReportsZeroMissingSymbolsForShowcase)
{
    auto apk = FindArtifact("ndk_showcase.apk");
    ASSERT_FALSE(apk.empty()) << "Missing build/debug/ndk_showcase.apk";

    auto empty_rootfs = stdfs::temp_directory_path() / "portage_empty_rootfs_check";
    std::error_code ec;
    stdfs::remove_all(empty_rootfs, ec);
    stdfs::create_directories(empty_rootfs, ec);

    auto res = RunPortageCommand("--rootfs \"" + empty_rootfs.string() + "\" --check-apk \"" +
                                 apk.string() + "\"");
    stdfs::remove_all(empty_rootfs, ec);

    EXPECT_EQ(res.exit_code, 0) << res.output;
    EXPECT_NE(res.output.find("Unresolved strong symbols: 0"), std::string::npos) << res.output;
}

TEST(NdkCompatExecutionTest, ShowcaseApkRunsStandaloneWithoutRootfs)
{
    auto apk = FindArtifact("ndk_showcase.apk");
    ASSERT_FALSE(apk.empty()) << "Missing build/debug/ndk_showcase.apk";

    auto empty_rootfs = stdfs::temp_directory_path() / "portage_empty_rootfs_run";
    std::error_code ec;
    stdfs::remove_all(empty_rootfs, ec);
    stdfs::create_directories(empty_rootfs, ec);

    auto res = RunPortageCommand("--headless --rootfs \"" + empty_rootfs.string() + "\" run \"" +
                                 apk.string() + "\"");
    stdfs::remove_all(empty_rootfs, ec);

    EXPECT_EQ(res.exit_code, 0) << res.output;
    EXPECT_NE(res.output.find("[ndk-showcase] ALL CHECKS PASSED"), std::string::npos) << res.output;
}
