#include "fs/vfs.hpp"
#include <gtest/gtest.h>

using namespace portage::fs;

TEST(VfsTest, ResolvesSyntheticDevices)
{
    Vfs vfs;
    auto null_dev = vfs.Resolve("/dev/null");
    EXPECT_EQ(null_dev.dev_type, DeviceType::DevNull);

    auto zero_dev = vfs.Resolve("/dev/zero");
    EXPECT_EQ(zero_dev.dev_type, DeviceType::DevZero);

    auto urandom_dev = vfs.Resolve("/dev/urandom");
    EXPECT_EQ(urandom_dev.dev_type, DeviceType::DevUrandom);

    auto random_dev = vfs.Resolve("/dev/random");
    EXPECT_EQ(random_dev.dev_type, DeviceType::DevUrandom);

    auto binder_dev = vfs.Resolve("/dev/binder");
    EXPECT_EQ(binder_dev.dev_type, DeviceType::Binder);

    auto hwbinder_dev = vfs.Resolve("/dev/hwbinder");
    EXPECT_EQ(hwbinder_dev.dev_type, DeviceType::HwBinder);

    auto vulkan_dev = vfs.Resolve("/dev/vulkan");
    EXPECT_EQ(vulkan_dev.dev_type, DeviceType::VulkanDevice);
}

TEST(VfsTest, ResolvesProcSelfExe)
{
    Vfs vfs;
    std::filesystem::path expected_exe = "C:/test/app.elf";
    vfs.SetExecutablePath(expected_exe);

    auto res = vfs.Resolve("/proc/self/exe");
    EXPECT_EQ(res.dev_type, DeviceType::Regular);
    EXPECT_EQ(res.host_path, expected_exe);
}

TEST(VfsTest, ResolvesRootfsPrefixedPaths)
{
    Vfs vfs("C:/AndroidRootfs");
    auto res = vfs.Resolve("/system/bin/linker64");
    EXPECT_EQ(res.dev_type, DeviceType::Regular);

    std::filesystem::path expected =
        std::filesystem::path("C:/AndroidRootfs") / "system/bin/linker64";
    EXPECT_EQ(res.host_path, expected);
}

TEST(VfsTest, VirtualSymlinkCreationAndReadlink)
{
    Vfs vfs("C:/AndroidRootfs");
    EXPECT_TRUE(vfs.CreateSymlink("/system/bin/app_process64", "/system/bin/app_process"));
    EXPECT_FALSE(vfs.CreateSymlink("/system/bin/other", "/system/bin/app_process"));

    EXPECT_TRUE(vfs.IsSymlink("/system/bin/app_process"));
    auto target = vfs.Readlink("/system/bin/app_process");
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, "/system/bin/app_process64");

    auto no_follow = vfs.Resolve("/system/bin/app_process", false);
    EXPECT_EQ(no_follow.status, 0);
    EXPECT_TRUE(no_follow.is_symlink);
    EXPECT_EQ(no_follow.symlink_target, "/system/bin/app_process64");

    auto followed = vfs.Resolve("/system/bin/app_process", true);
    EXPECT_EQ(followed.status, 0);
    EXPECT_FALSE(followed.is_symlink);
    EXPECT_EQ(followed.host_path,
              std::filesystem::path("C:/AndroidRootfs") / "system/bin/app_process64");
}

TEST(VfsTest, SymlinkRecursiveResolution)
{
    Vfs vfs("C:/AndroidRootfs");
    EXPECT_TRUE(vfs.CreateSymlink("/data/target.txt", "/data/link2"));
    EXPECT_TRUE(vfs.CreateSymlink("/data/link2", "/data/link1"));

    auto res = vfs.Resolve("/data/link1");
    EXPECT_EQ(res.status, 0);
    EXPECT_FALSE(res.is_symlink);
    EXPECT_TRUE(res.host_path.string().ends_with("target.txt"));
}

TEST(VfsTest, SymlinkLoopDetection)
{
    Vfs vfs("C:/AndroidRootfs");
    EXPECT_TRUE(vfs.CreateSymlink("/data/loop2", "/data/loop1"));
    EXPECT_TRUE(vfs.CreateSymlink("/data/loop1", "/data/loop2"));

    auto res = vfs.Resolve("/data/loop1");
    EXPECT_EQ(res.status, -40);
}

TEST(VfsTest, RelativeSymlinkResolution)
{
    Vfs vfs("C:/AndroidRootfs");
    EXPECT_TRUE(vfs.CreateSymlink("../real/file.txt", "/system/sub/rel_link"));

    auto res = vfs.Resolve("/system/sub/rel_link");
    EXPECT_EQ(res.status, 0);
    std::filesystem::path expected =
        std::filesystem::path("C:/AndroidRootfs") / "system/real/file.txt";
    EXPECT_EQ(res.host_path, expected);
}
