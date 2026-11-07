#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/native_activity.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

void WriteU8(std::vector<uint8_t> &buf, uint8_t val) { buf.push_back(val); }

void WriteU16(std::vector<uint8_t> &buf, uint16_t val)
{
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void WriteU32(std::vector<uint8_t> &buf, uint32_t val)
{
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

uint32_t CalculateCrc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

stdfs::path FindGuestBinary(const std::string &name)
{
    const std::vector<stdfs::path> search_paths = {
        stdfs::path("guests") / name,
        stdfs::path("tests/guests") / name,
        stdfs::current_path() / "guests" / name,
        stdfs::current_path() / "tests" / "guests" / name,
        stdfs::current_path() / "build" / "debug" / "tests" / "guests" / name,
        stdfs::current_path() / ".." / "build" / "debug" / "tests" / "guests" / name,
    };
    for (const auto &p : search_paths) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return "";
}

stdfs::path FindPortageExecutable()
{
    const std::vector<stdfs::path> candidates = {
        stdfs::path("src") / "portage.exe",
        stdfs::path("../src") / "portage.exe",
        stdfs::path("build/debug/src") / "portage.exe",
        stdfs::current_path() / "src" / "portage.exe",
        stdfs::current_path() / ".." / "src" / "portage.exe",
        stdfs::current_path() / "build" / "debug" / "src" / "portage.exe",
    };
    for (const auto &p : candidates) {
        if (stdfs::exists(p)) {
            return stdfs::canonical(p);
        }
    }
    return "";
}

std::vector<uint8_t> BuildGlesManifest(const std::string &pkg_name, const std::string &lib_name)
{
    std::vector<std::string> string_pool = {
        "manifest",                   // 0
        "package",                    // 1
        pkg_name,                     // 2
        "versionCode",                // 3
        "versionName",                // 4
        "1.0.0",                      // 5
        "uses-sdk",                   // 6
        "minSdkVersion",              // 7
        "targetSdkVersion",           // 8
        "application",                // 9
        "label",                      // 10
        "Portage GLES App",           // 11
        "hasCode",                    // 12
        "activity",                   // 13
        "name",                       // 14
        "android.app.NativeActivity", // 15
        "meta-data",                  // 16
        "value",                      // 17
        "android.app.lib_name",       // 18
        lib_name                      // 19
    };

    std::vector<uint8_t> doc;
    WriteU16(doc, 0x0003); // CHUNK_AXML_ROOT
    WriteU16(doc, 0x0008);
    WriteU32(doc, 0);

    uint32_t str_pool_start = static_cast<uint32_t>(doc.size());
    WriteU16(doc, 0x0001); // CHUNK_STRING_POOL
    WriteU16(doc, 0x001C);
    WriteU32(doc, 0);
    WriteU32(doc, static_cast<uint32_t>(string_pool.size()));
    WriteU32(doc, 0);
    WriteU32(doc, 1 << 8); // UTF-8
    WriteU32(doc, 28 + static_cast<uint32_t>(string_pool.size()) * 4);
    WriteU32(doc, 0);

    std::vector<uint32_t> offsets;
    std::vector<uint8_t> str_data;
    for (const auto &str : string_pool) {
        offsets.push_back(static_cast<uint32_t>(str_data.size()));
        str_data.push_back(static_cast<uint8_t>(str.size()));
        str_data.push_back(static_cast<uint8_t>(str.size()));
        for (char c : str)
            str_data.push_back(static_cast<uint8_t>(c));
        str_data.push_back(0);
    }
    while (str_data.size() % 4 != 0)
        str_data.push_back(0);

    for (uint32_t off : offsets)
        WriteU32(doc, off);
    doc.insert(doc.end(), str_data.begin(), str_data.end());

    uint32_t str_pool_len = static_cast<uint32_t>(doc.size() - str_pool_start);
    doc[str_pool_start + 4] = static_cast<uint8_t>(str_pool_len & 0xFF);
    doc[str_pool_start + 5] = static_cast<uint8_t>((str_pool_len >> 8) & 0xFF);
    doc[str_pool_start + 6] = static_cast<uint8_t>((str_pool_len >> 16) & 0xFF);
    doc[str_pool_start + 7] = static_cast<uint8_t>((str_pool_len >> 24) & 0xFF);

    auto emit_element = [&](uint32_t name_idx,
                            const std::vector<std::pair<uint32_t, uint32_t>> &attrs) {
        WriteU16(doc, 0x0102); // START_ELEMENT
        WriteU16(doc, 0x0010);
        WriteU32(doc, 36 + static_cast<uint32_t>(attrs.size()) * 20);
        WriteU32(doc, 1);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, name_idx);
        WriteU16(doc, 20);
        WriteU16(doc, 20);
        WriteU16(doc, static_cast<uint16_t>(attrs.size()));
        WriteU16(doc, 0);
        WriteU16(doc, 0);
        WriteU16(doc, 0);

        for (const auto &[attr_name, attr_val] : attrs) {
            WriteU32(doc, 0xFFFFFFFF);
            WriteU32(doc, attr_name);
            WriteU32(doc, attr_val);
            WriteU16(doc, 8);
            WriteU8(doc, 0);
            WriteU8(doc, 3);
            WriteU32(doc, attr_val);
        }
    };

    auto emit_end = [&](uint32_t name_idx) {
        WriteU16(doc, 0x0103); // END_ELEMENT
        WriteU16(doc, 0x0010);
        WriteU32(doc, 24);
        WriteU32(doc, 1);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, name_idx);
    };

    emit_element(0, {{1, 2}});         // <manifest package="pkg">
    emit_element(6, {{7, 5}, {8, 5}}); // <uses-sdk>
    emit_end(6);
    emit_element(9, {{10, 11}});            // <application>
    emit_element(13, {{14, 15}});           // <activity>
    emit_element(16, {{14, 18}, {17, 19}}); // <meta-data android.app.lib_name="test_gles_quad">
    emit_end(16);
    emit_end(13);
    emit_end(9);
    emit_end(0);

    uint32_t total_len = static_cast<uint32_t>(doc.size());
    doc[4] = static_cast<uint8_t>(total_len & 0xFF);
    doc[5] = static_cast<uint8_t>((total_len >> 8) & 0xFF);
    doc[6] = static_cast<uint8_t>((total_len >> 16) & 0xFF);
    doc[7] = static_cast<uint8_t>((total_len >> 24) & 0xFF);

    return doc;
}

struct ZipInputEntry {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t crc{0};
    uint32_t offset{0};
};

std::vector<uint8_t> AssembleGlesApk(const std::vector<uint8_t> &manifest_bytes,
                                     const std::string &lib_name,
                                     const std::vector<uint8_t> &lib_bytes)
{
    std::vector<ZipInputEntry> entries = {{"AndroidManifest.xml", manifest_bytes, 0, 0},
                                          {"lib/x86_64/" + lib_name, lib_bytes, 0, 0}};

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.crc = CalculateCrc32(e.data.data(), e.data.size());
        e.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.data.begin(), e.data.end());
    }

    uint32_t cd_offset = static_cast<uint32_t>(zip.size());
    for (const auto &e : entries) {
        WriteU32(zip, 0x02014b50);
        WriteU16(zip, 20);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, e.crc);
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU32(zip, static_cast<uint32_t>(e.data.size()));
        WriteU16(zip, static_cast<uint16_t>(e.name.size()));
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, 0);
        WriteU32(zip, e.offset);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_offset;
    WriteU32(zip, 0x06054b50);
    WriteU16(zip, 0);
    WriteU16(zip, 0);
    WriteU16(zip, static_cast<uint16_t>(entries.size()));
    WriteU16(zip, static_cast<uint16_t>(entries.size()));
    WriteU32(zip, cd_size);
    WriteU32(zip, cd_offset);
    WriteU16(zip, 0);

    return zip;
}

} // namespace

class GlesExecutionFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = stdfs::temp_directory_path() / "portage_gles_test";
        sandbox_dir_ = test_dir_ / "sandbox";
        stdfs::create_directories(test_dir_);

        stdfs::path guest_so_path = FindGuestBinary("libtest_gles_quad.so");
        ASSERT_TRUE(stdfs::exists(guest_so_path))
            << "Guest shared library not found at: " << guest_so_path;

        std::ifstream so_in(guest_so_path, std::ios::binary);
        std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                      std::istreambuf_iterator<char>());
        ASSERT_FALSE(so_bytes.empty());

        auto manifest_bytes = BuildGlesManifest("com.portage.glesapp", "test_gles_quad");
        auto apk_bytes = AssembleGlesApk(manifest_bytes, "libtest_gles_quad.so", so_bytes);

        apk_path_ = test_dir_ / "gles_app.apk";
        std::ofstream apk_out(apk_path_, std::ios::binary);
        apk_out.write(reinterpret_cast<const char *>(apk_bytes.data()),
                      static_cast<std::streamsize>(apk_bytes.size()));
        apk_out.close();

        address_space_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>("");
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
        guest_context_.reset();
        loader_.reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        address_space_.reset();

        std::error_code ec;
        stdfs::remove_all(test_dir_, ec);
    }

    stdfs::path test_dir_;
    stdfs::path sandbox_dir_;
    stdfs::path apk_path_;

    std::unique_ptr<mem::AddressSpace> address_space_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
    std::unique_ptr<elf::ElfLoader> loader_;
    std::unique_ptr<arch::GuestContext> guest_context_;
};

TEST_F(GlesExecutionFixture, RunsEglAndGlesQuadRendering)
{
    package::PackageManager::Instance().Initialize(sandbox_dir_);
    auto pkg_opt = package::PackageManager::Instance().InstallPackage(apk_path_);
    ASSERT_TRUE(pkg_opt.has_value());
    auto pkg = pkg_opt.value();
    EXPECT_EQ(pkg.manifest.package_name, "com.portage.glesapp");

    ANativeWindow native_window{
        .width = 800, .height = 600, .format = 1, .ref_count = 1, .native_handle = nullptr};

    ASSERT_TRUE(package::NativeActivityRunner::Instance().SetupActivity(pkg, &native_window));

    stdfs::path primary_lib = package::PackageManager::Instance().GetPrimaryLibraryPath(pkg);
    ASSERT_TRUE(stdfs::exists(primary_lib)) << "Primary lib not found at: " << primary_lib;

    auto load_res = loader_->LoadFromFile(primary_lib.string(), vfs_.get());
    ASSERT_TRUE(std::holds_alternative<elf::LoadedElf>(load_res));
    const auto &loaded = std::get<elf::LoadedElf>(load_res);

    uintptr_t on_create_addr = loaded.FindSymbol("ANativeActivity_onCreate");
    ASSERT_NE(on_create_addr, 0ULL) << "ANativeActivity_onCreate not found";

    auto on_create = reinterpret_cast<package::ANativeActivity_createFunc>(on_create_addr);
    on_create(package::NativeActivityRunner::Instance().GetActivity(), nullptr, 0);

    // Dispatch lifecycle: Start & Resume
    package::NativeActivityRunner::Instance().DispatchStart();
    package::NativeActivityRunner::Instance().DispatchResume();

    // Dispatch Window creation, which triggers EGL initialization and quad rendering
    package::NativeActivityRunner::Instance().DispatchWindowCreated(&native_window);

    // Verify success marker written by guest program upon pixel readback match
    stdfs::path success_marker = pkg.data_dir / "gles_render_success.txt";
    EXPECT_TRUE(stdfs::exists(success_marker))
        << "GLES rendering marker not found at: " << success_marker;

    if (stdfs::exists(success_marker)) {
        std::ifstream marker_in(success_marker);
        std::string content;
        marker_in >> content;
        EXPECT_EQ(content, "SUCCESS");
    }

    // Teardown activity
    package::NativeActivityRunner::Instance().DispatchPause();
    package::NativeActivityRunner::Instance().DispatchStop();
    package::NativeActivityRunner::Instance().DispatchWindowDestroyed(&native_window);
    package::NativeActivityRunner::Instance().DispatchDestroy();
}

TEST_F(GlesExecutionFixture, RunsGlesAppViaLauncherCli)
{
    stdfs::path portage_exe = FindPortageExecutable();
    ASSERT_TRUE(stdfs::exists(portage_exe))
        << "portage.exe not found at current_path=" << stdfs::current_path();

    std::string cmd = "\"\"" + portage_exe.string() + "\" run \"" + apk_path_.string() + "\"\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 0);
}
