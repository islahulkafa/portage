#include "arch/guest_context.hpp"
#include "elf/elf_loader.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/android_log.hpp"
#include "ndk/input_queue.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"
#include "package/native_activity.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

namespace stdfs = std::filesystem;
using namespace portage;

namespace {

inline void WriteU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

inline void WriteU16(std::vector<uint8_t> &buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void WriteU32(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

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

static stdfs::path FindPortageExecutable()
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

std::vector<uint8_t> BuildEngineManifest(const std::string &pkg_name, const std::string &lib_name)
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
        "Portage Engine App",         // 11
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
        WriteU16(doc, 20);                                  // attr_start
        WriteU16(doc, 20);                                  // attr_size
        WriteU16(doc, static_cast<uint16_t>(attrs.size())); // attr_count
        WriteU16(doc, 0);
        WriteU16(doc, 0);
        WriteU16(doc, 0);

        for (const auto &[attr_name, attr_val] : attrs) {
            WriteU32(doc, 0xFFFFFFFF);
            WriteU32(doc, attr_name);
            WriteU32(doc, attr_val);
            WriteU16(doc, 0x0008);
            WriteU8(doc, 0);
            WriteU8(doc, 0x03); // TYPE_STRING
            WriteU32(doc, attr_val);
        }
    };

    emit_element(0, {{1, 2}, {3, 5}, {4, 5}});
    emit_element(6, {{7, 5}, {8, 5}});
    emit_element(9, {{10, 11}, {12, 5}});
    emit_element(13, {{14, 15}});
    emit_element(16, {{14, 18}, {17, 19}});

    for (int i = 0; i < 5; ++i) {
        WriteU16(doc, 0x0103); // END_ELEMENT
        WriteU16(doc, 0x0010);
        WriteU32(doc, 24);
        WriteU32(doc, 1);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0xFFFFFFFF);
        WriteU32(doc, 0);
    }

    uint32_t total_len = static_cast<uint32_t>(doc.size());
    doc[4] = static_cast<uint8_t>(total_len & 0xFF);
    doc[5] = static_cast<uint8_t>((total_len >> 8) & 0xFF);
    doc[6] = static_cast<uint8_t>((total_len >> 16) & 0xFF);
    doc[7] = static_cast<uint8_t>((total_len >> 24) & 0xFF);

    return doc;
}

std::vector<uint8_t> AssembleEngineApk(const std::vector<uint8_t> &manifest,
                                       const std::string &lib_filename,
                                       const std::vector<uint8_t> &lib_bytes)
{
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };

    std::vector<FileEntry> entries = {{"AndroidManifest.xml", manifest, 0, 0},
                                      {"lib/x86_64/" + lib_filename, lib_bytes, 0, 0},
                                      {"assets/engine.json", {'{', '}'}, 0, 0}};

    std::vector<uint8_t> zip;
    for (auto &e : entries) {
        e.crc = static_cast<uint32_t>(crc32(0L, e.data.data(), static_cast<uInt>(e.data.size())));
        e.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0); // Stored
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

class GameEngineExecutionFixture : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = stdfs::temp_directory_path() / "portage_game_engine_test";
        sandbox_dir_ = test_dir_ / "sandbox";
        stdfs::create_directories(test_dir_);

        stdfs::path guest_so_path = FindGuestBinary("libtest_game_engine.so");
        ASSERT_TRUE(stdfs::exists(guest_so_path))
            << "Guest shared library not found at: " << guest_so_path;

        std::ifstream so_in(guest_so_path, std::ios::binary);
        std::vector<uint8_t> so_bytes((std::istreambuf_iterator<char>(so_in)),
                                      std::istreambuf_iterator<char>());
        ASSERT_FALSE(so_bytes.empty());

        auto manifest_bytes = BuildEngineManifest("com.portage.gameengine", "test_game_engine");
        auto apk_bytes = AssembleEngineApk(manifest_bytes, "libtest_game_engine.so", so_bytes);

        apk_path_ = test_dir_ / "game_engine.apk";
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

TEST_F(GameEngineExecutionFixture, RunsAndroidNativeAppGlueLoop)
{
    package::PackageManager::Instance().Initialize(sandbox_dir_);
    auto pkg_opt = package::PackageManager::Instance().InstallPackage(apk_path_);
    ASSERT_TRUE(pkg_opt.has_value());
    auto pkg = pkg_opt.value();
    EXPECT_EQ(pkg.manifest.package_name, "com.portage.gameengine");

    ANativeWindow native_window{.width = 1280, .height = 720, .format = 1, .ref_count = 1};

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

    // Wait briefly for worker thread to create marker
    stdfs::path ready_marker = pkg.data_dir / "engine_worker_ready.txt";
    for (int i = 0; i < 50 && !stdfs::exists(ready_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(ready_marker));

    // Dispatch lifecycle: Start & Resume
    package::NativeActivityRunner::Instance().DispatchStart();
    package::NativeActivityRunner::Instance().DispatchResume();

    // Dispatch Window creation
    package::NativeActivityRunner::Instance().DispatchWindowCreated(&native_window);
    stdfs::path win_marker = pkg.data_dir / "engine_window_init.txt";
    for (int i = 0; i < 50 && !stdfs::exists(win_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(win_marker));

    // Attach input queue
    portage::ndk::InputQueueImpl input_queue;
    package::NativeActivityRunner::Instance().DispatchInputQueueCreated(
        reinterpret_cast<AInputQueue *>(&input_queue));

    // Dispatch focus
    package::NativeActivityRunner::Instance().DispatchWindowFocus(true);
    stdfs::path focus_marker = pkg.data_dir / "engine_focus_gained.txt";
    for (int i = 0; i < 50 && !stdfs::exists(focus_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(focus_marker));

    // Enqueue motion input
    std::vector<portage::ndk::InputPointer> ptrs = {{.id = 0,
                                                     .x = 400.0f,
                                                     .y = 300.0f,
                                                     .raw_x = 400.0f,
                                                     .raw_y = 300.0f,
                                                     .pressure = 1.0f,
                                                     .size = 1.0f}};
    input_queue.EnqueueMotionEvent(AMOTION_EVENT_ACTION_DOWN, ptrs);

    stdfs::path motion_marker = pkg.data_dir / "engine_motion_received.txt";
    for (int i = 0; i < 50 && !stdfs::exists(motion_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(motion_marker));

    // Enqueue key input
    input_queue.EnqueueKeyEvent(AKEY_EVENT_ACTION_DOWN, 88);

    stdfs::path key_marker = pkg.data_dir / "engine_key_received.txt";
    for (int i = 0; i < 50 && !stdfs::exists(key_marker); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(stdfs::exists(key_marker));

    // Verify logs in LogcatBuffer
    auto log_entries = portage::ndk::LogcatBuffer::Instance().GetEntries();
    bool found_engine_log = false;
    for (const auto &entry : log_entries) {
        if (entry.tag == "PortageEngine") {
            found_engine_log = true;
            break;
        }
    }
    EXPECT_TRUE(found_engine_log);

    // Shutdown activity
    package::NativeActivityRunner::Instance().DispatchWindowFocus(false);
    package::NativeActivityRunner::Instance().DispatchPause();
    package::NativeActivityRunner::Instance().DispatchStop();
    package::NativeActivityRunner::Instance().DispatchInputQueueDestroyed(
        reinterpret_cast<AInputQueue *>(&input_queue));
    package::NativeActivityRunner::Instance().DispatchWindowDestroyed(&native_window);
    package::NativeActivityRunner::Instance().DispatchDestroy();

    stdfs::path destroyed_marker = pkg.data_dir / "engine_destroyed.txt";
    EXPECT_TRUE(stdfs::exists(destroyed_marker));
}

TEST_F(GameEngineExecutionFixture, RunsGameEngineViaLauncherCli)
{
    stdfs::path portage_exe = FindPortageExecutable();
    ASSERT_TRUE(stdfs::exists(portage_exe))
        << "portage.exe not found at current_path=" << stdfs::current_path();

    std::string cmd = "\"\"" + portage_exe.string() + "\" run \"" + apk_path_.string() + "\"\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 0);

    // Also verify `portage logcat` CLI subcommand succeeds
    std::string logcat_cmd = "\"\"" + portage_exe.string() + "\" logcat\"";
    int logcat_ret = std::system(logcat_cmd.c_str());
    EXPECT_EQ(logcat_ret, 0);
}
