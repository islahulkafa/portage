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
