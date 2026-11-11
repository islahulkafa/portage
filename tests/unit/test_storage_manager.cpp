#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "ndk/storage_manager.hpp"
#include "package/jni_env.hpp"
#include "package/package_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <zlib.h>

namespace portage::ndk {

namespace {

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

inline void WriteU64(std::vector<uint8_t> &buf, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

std::vector<uint8_t>
CreateTestObbArchive(const std::vector<std::pair<std::string, std::string>> &files,
                     const std::string &package_name, uint32_t version = 1,
                     uint32_t marker = 0x01059983)
{
    std::vector<uint8_t> zip;

    struct EntryMeta {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc{0};
        uint32_t offset{0};
    };
    std::vector<EntryMeta> entries;

    for (const auto &[name, content] : files) {
        EntryMeta meta;
        meta.name = name;
        meta.data.assign(content.begin(), content.end());
        meta.crc = static_cast<uint32_t>(crc32(0L, reinterpret_cast<const Bytef *>(content.data()),
                                               static_cast<uInt>(content.size())));
        meta.offset = static_cast<uint32_t>(zip.size());

        WriteU32(zip, 0x04034b50);
        WriteU16(zip, 20);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU16(zip, 0);
        WriteU32(zip, meta.crc);
        WriteU32(zip, static_cast<uint32_t>(meta.data.size()));
        WriteU32(zip, static_cast<uint32_t>(meta.data.size()));
        WriteU16(zip, static_cast<uint16_t>(meta.name.size()));
        WriteU16(zip, 0);
        zip.insert(zip.end(), meta.name.begin(), meta.name.end());
        zip.insert(zip.end(), meta.data.begin(), meta.data.end());

        entries.push_back(std::move(meta));
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

    if (!package_name.empty()) {
        uint32_t footer_size = static_cast<uint32_t>(32 + package_name.size());
        WriteU32(zip, 1);
        WriteU32(zip, version);
        WriteU32(zip, 0);
        WriteU64(zip, 0x1234567890ABCDEFULL);
        WriteU32(zip, static_cast<uint32_t>(package_name.size()));
        zip.insert(zip.end(), package_name.begin(), package_name.end());
        WriteU32(zip, footer_size);
        WriteU32(zip, marker);
    }

    return zip;
}

struct CallbackState {
    std::mutex mtx;
    std::condition_variable cv;
    int32_t state{0};
    std::string filename{};
};

void __attribute__((sysv_abi)) TestCallback(const char *filename, const int32_t state, void *data)
{
    auto *s = reinterpret_cast<CallbackState *>(data);
    std::lock_guard<std::mutex> lock(s->mtx);
    s->state = state;
    s->filename = filename ? filename : "";
    s->cv.notify_one();
}

} // namespace

class StorageManagerTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_dir_ = std::filesystem::temp_directory_path() / "portage_storage_mgr_test";
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
        std::filesystem::create_directories(test_dir_ / "rootfs" / "sdcard", ec);

        vfs_ = std::make_unique<fs::Vfs>(test_dir_ / "rootfs");
        vfs_->AddMount("/sdcard", test_dir_ / "rootfs" / "sdcard");

        as_ = std::make_unique<mem::AddressSpace>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);

        package::PackageManager::Instance().Initialize(test_dir_ / "sandbox");
    }

    void TearDown() override
    {
        StorageManagerImpl::Reset();
        package::PackageManager::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        as_.reset();
        vfs_.reset();

        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_{};
    std::unique_ptr<fs::Vfs> vfs_{};
    std::unique_ptr<mem::AddressSpace> as_{};
    std::unique_ptr<fs::FileDescriptorTable> fd_table_{};
};

TEST_F(StorageManagerTest, Lifecycle)
{
    AStorageManager *mgr = AStorageManager_new();
    ASSERT_NE(mgr, nullptr);
    AStorageManager_delete(mgr);
}

TEST_F(StorageManagerTest, FooterParsingValidAndAlt)
{
    auto obb_bytes =
        CreateTestObbArchive({{"test.txt", "hello"}}, "com.example.game", 42, 0x01059983);
    auto obb_path = test_dir_ / "main.42.com.example.game.obb";
    {
        std::ofstream out(obb_path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(obb_bytes.data()), obb_bytes.size());
    }

    auto footer = StorageManagerImpl::ParseObbFooter(obb_path);
    ASSERT_TRUE(footer.has_value());
    EXPECT_TRUE(footer->valid);
    EXPECT_EQ(footer->sig_version, 1u);
    EXPECT_EQ(footer->package_version, 42u);
    EXPECT_EQ(footer->package_name, "com.example.game");

    auto alt_bytes =
        CreateTestObbArchive({{"test.txt", "hello"}}, "com.example.alt", 99, 0x01050a4c);
    auto alt_path = test_dir_ / "main.99.com.example.alt.obb";
    {
        std::ofstream out(alt_path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(alt_bytes.data()), alt_bytes.size());
    }

    auto alt_footer = StorageManagerImpl::ParseObbFooter(alt_path);
    ASSERT_TRUE(alt_footer.has_value());
    EXPECT_TRUE(alt_footer->valid);
    EXPECT_EQ(alt_footer->package_version, 99u);
    EXPECT_EQ(alt_footer->package_name, "com.example.alt");
}

TEST_F(StorageManagerTest, MountAndUnmountObb)
{
    auto obb_bytes = CreateTestObbArchive(
        {{"assets/config.json", "{\"key\":\"val\"}"}, {"data/level1.dat", "LEVEL1_DATA"}},
        "com.portage.sample", 1);
    auto host_obb_file = test_dir_ / "rootfs" / "sdcard" / "test.obb";
    {
        std::ofstream out(host_obb_file, std::ios::binary);
        out.write(reinterpret_cast<const char *>(obb_bytes.data()), obb_bytes.size());
    }

    AStorageManager *mgr = AStorageManager_new();
    ASSERT_NE(mgr, nullptr);

    CallbackState cb_state;

    const char *guest_obb_path = "/sdcard/test.obb";
    AStorageManager_mountObb(mgr, guest_obb_path, nullptr, TestCallback, &cb_state);

    {
        std::unique_lock<std::mutex> lock(cb_state.mtx);
        ASSERT_TRUE(cb_state.cv.wait_for(lock, std::chrono::seconds(5),
                                         [&] { return cb_state.state != 0; }));
        EXPECT_EQ(cb_state.state, AOBB_STATE_MOUNTED);
    }

    EXPECT_EQ(AStorageManager_isObbMounted(mgr, guest_obb_path), 1);
    const char *mounted_path = AStorageManager_getMountedObbPath(mgr, guest_obb_path);
    ASSERT_NE(mounted_path, nullptr);
    EXPECT_TRUE(std::string_view(mounted_path).starts_with("/storage/obb/"));

    std::string guest_file_path = std::string(mounted_path) + "/data/level1.dat";
    auto resolved = vfs_->Resolve(guest_file_path);
    EXPECT_TRUE(std::filesystem::exists(resolved.host_path));
    std::ifstream in(resolved.host_path);
    std::string content;
    in >> content;
    EXPECT_EQ(content, "LEVEL1_DATA");

    {
        std::lock_guard<std::mutex> lock(cb_state.mtx);
        cb_state.state = 0;
    }

    AStorageManager_unmountObb(mgr, guest_obb_path, 0, TestCallback, &cb_state);

    {
        std::unique_lock<std::mutex> lock(cb_state.mtx);
        ASSERT_TRUE(cb_state.cv.wait_for(lock, std::chrono::seconds(5),
                                         [&] { return cb_state.state != 0; }));
        EXPECT_EQ(cb_state.state, AOBB_STATE_UNMOUNTED);
    }

    EXPECT_EQ(AStorageManager_isObbMounted(mgr, guest_obb_path), 0);
    EXPECT_EQ(AStorageManager_getMountedObbPath(mgr, guest_obb_path), nullptr);

    AStorageManager_delete(mgr);
}

TEST_F(StorageManagerTest, ErrorHandling)
{
    AStorageManager *mgr = AStorageManager_new();
    ASSERT_NE(mgr, nullptr);

    CallbackState cb_state;

    AStorageManager_mountObb(mgr, "/sdcard/nonexistent.obb", nullptr, TestCallback, &cb_state);
    {
        std::unique_lock<std::mutex> lock(cb_state.mtx);
        ASSERT_TRUE(cb_state.cv.wait_for(lock, std::chrono::seconds(5),
                                         [&] { return cb_state.state != 0; }));
        EXPECT_EQ(cb_state.state, AOBB_STATE_ERROR_COULD_NOT_MOUNT);
    }

    {
        std::lock_guard<std::mutex> lock(cb_state.mtx);
        cb_state.state = 0;
    }
    AStorageManager_unmountObb(mgr, "/sdcard/not_mounted.obb", 0, TestCallback, &cb_state);
    {
        std::unique_lock<std::mutex> lock(cb_state.mtx);
        ASSERT_TRUE(cb_state.cv.wait_for(lock, std::chrono::seconds(5),
                                         [&] { return cb_state.state != 0; }));
        EXPECT_EQ(cb_state.state, AOBB_STATE_ERROR_NOT_MOUNTED);
    }

    AStorageManager_delete(mgr);
}

TEST_F(StorageManagerTest, ScopedStorageVfsResolution)
{
    auto res1 = vfs_->Resolve("/storage/emulated/0/Download");
    auto res2 = vfs_->Resolve("/sdcard/Download");
    EXPECT_EQ(res1.host_path, res2.host_path);

    auto res3 =
        vfs_->Resolve("/storage/emulated/0/Android/obb/com.test.app/main.1.com.test.app.obb");
    auto res4 = vfs_->Resolve("/sdcard/Android/obb/com.test.app/main.1.com.test.app.obb");
    EXPECT_EQ(res3.host_path, res4.host_path);
}

TEST_F(StorageManagerTest, NdkSymbolRegistryLookup)
{
    auto &registry = NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("AStorageManager_new"), 0u);
    EXPECT_NE(registry.Lookup("AStorageManager_delete"), 0u);
    EXPECT_NE(registry.Lookup("AStorageManager_mountObb"), 0u);
    EXPECT_NE(registry.Lookup("AStorageManager_unmountObb"), 0u);
    EXPECT_NE(registry.Lookup("AStorageManager_isObbMounted"), 0u);
    EXPECT_NE(registry.Lookup("AStorageManager_getMountedObbPath"), 0u);
}

TEST_F(StorageManagerTest, JniSyntheticEnvironment)
{
    auto &jni = package::JniEnvironment::Instance();
    jni.Initialize("com.test.app", "/data/data/com.test.app/files",
                   "/data/data/com.test.app/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    package::jclass clazz =
        reinterpret_cast<package::jclass>(const_cast<char *>("android/os/Environment"));
    package::jmethodID mid_dir =
        reinterpret_cast<package::jmethodID>(const_cast<char *>("getExternalStorageDirectory"));
    package::jobject dir_obj = env->functions->CallStaticObjectMethod(env, clazz, mid_dir);
    ASSERT_NE(dir_obj, nullptr);

    package::jmethodID mid_path =
        reinterpret_cast<package::jmethodID>(const_cast<char *>("getAbsolutePath"));
    package::jobject path_str = env->functions->CallObjectMethod(env, dir_obj, mid_path);
    ASSERT_NE(path_str, nullptr);
    const char *path_chars = env->functions->GetStringUTFChars(env, path_str, nullptr);
    EXPECT_STREQ(path_chars, "/storage/emulated/0");
    env->functions->ReleaseStringUTFChars(env, path_str, path_chars);

    package::jmethodID mid_state =
        reinterpret_cast<package::jmethodID>(const_cast<char *>("getExternalStorageState"));
    package::jobject state_str = env->functions->CallStaticObjectMethod(env, clazz, mid_state);
    ASSERT_NE(state_str, nullptr);
    const char *state_chars = env->functions->GetStringUTFChars(env, state_str, nullptr);
    EXPECT_STREQ(state_chars, "mounted");
    env->functions->ReleaseStringUTFChars(env, state_str, state_chars);

    package::jobject dummy_ctx =
        reinterpret_cast<package::jobject>(const_cast<char *>("dummy_context"));
    package::jmethodID mid_obb =
        reinterpret_cast<package::jmethodID>(const_cast<char *>("getObbDir"));
    package::jobject obb_obj = env->functions->CallObjectMethod(env, dummy_ctx, mid_obb);
    ASSERT_NE(obb_obj, nullptr);
    package::jobject obb_path_str = env->functions->CallObjectMethod(env, obb_obj, mid_path);
    ASSERT_NE(obb_path_str, nullptr);
    const char *obb_chars = env->functions->GetStringUTFChars(env, obb_path_str, nullptr);
    EXPECT_STREQ(obb_chars, "/storage/emulated/0/Android/obb/com.test.app");
    env->functions->ReleaseStringUTFChars(env, obb_path_str, obb_chars);

    jni.Reset();
}

} // namespace portage::ndk
