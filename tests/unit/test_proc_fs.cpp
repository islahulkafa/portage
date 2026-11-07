#include "fs/proc_fs.hpp"
#include <gtest/gtest.h>

using namespace portage::fs;

TEST(ProcFsTest, IdentifiesProcPaths)
{
    EXPECT_TRUE(ProcFs::IsProcPath("/proc"));
    EXPECT_TRUE(ProcFs::IsProcPath("/proc/cpuinfo"));
    EXPECT_TRUE(ProcFs::IsProcPath("/proc/meminfo"));
    EXPECT_TRUE(ProcFs::IsProcPath("/proc/self/maps"));
    EXPECT_FALSE(ProcFs::IsProcPath("/system/bin/sh"));
    EXPECT_FALSE(ProcFs::IsProcPath("/dev/null"));
}

TEST(ProcFsTest, GeneratesCpuInfoWithStandardLinuxFields)
{
    std::string cpuinfo = ProcFs::Generate("/proc/cpuinfo");
    EXPECT_FALSE(cpuinfo.empty());
    EXPECT_NE(cpuinfo.find("processor"), std::string::npos);
    EXPECT_NE(cpuinfo.find("vendor_id"), std::string::npos);
    EXPECT_NE(cpuinfo.find("model name"), std::string::npos);
    EXPECT_NE(cpuinfo.find("flags"), std::string::npos);
}

TEST(ProcFsTest, GeneratesMemInfoWithAccurateUnits)
{
    std::string meminfo = ProcFs::Generate("/proc/meminfo");
    EXPECT_FALSE(meminfo.empty());
    EXPECT_NE(meminfo.find("MemTotal:"), std::string::npos);
    EXPECT_NE(meminfo.find("MemFree:"), std::string::npos);
    EXPECT_NE(meminfo.find("MemAvailable:"), std::string::npos);
    EXPECT_NE(meminfo.find("kB"), std::string::npos);
}

TEST(ProcFsTest, GeneratesVersionString)
{
    std::string version = ProcFs::Generate("/proc/version");
    EXPECT_NE(version.find("Linux version"), std::string::npos);
    EXPECT_NE(version.find("Portage"), std::string::npos);
}

TEST(ProcFsTest, ListsProcRootAndSelfDirectories)
{
    auto root_entries = ProcFs::ListDir("/proc");
    EXPECT_GE(root_entries.size(), 4u);
    bool found_cpuinfo = false;
    bool found_self = false;
    for (const auto &e : root_entries) {
        if (e.name == "cpuinfo")
            found_cpuinfo = true;
        if (e.name == "self")
            found_self = true;
    }
    EXPECT_TRUE(found_cpuinfo);
    EXPECT_TRUE(found_self);

    auto self_entries = ProcFs::ListDir("/proc/self");
    EXPECT_GE(self_entries.size(), 4u);
    bool found_maps = false;
    bool found_status = false;
    for (const auto &e : self_entries) {
        if (e.name == "maps")
            found_maps = true;
        if (e.name == "status")
            found_status = true;
    }
    EXPECT_TRUE(found_maps);
    EXPECT_TRUE(found_status);
}
