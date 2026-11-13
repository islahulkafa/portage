#include "fs/proc_fs.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <windows.h>

using namespace portage::fs;
using namespace portage::mem;
using namespace portage::ndk;
using namespace portage::syscall;
using namespace portage::thread;

TEST(CgroupsCpuTopologyTest, CgroupV2RootControllersAndTelemetry)
{
    std::string controllers = ProcFs::Generate("/sys/fs/cgroup/cgroup.controllers");
    EXPECT_NE(controllers.find("cpu"), std::string::npos);
    EXPECT_NE(controllers.find("memory"), std::string::npos);
    EXPECT_NE(controllers.find("cpuset"), std::string::npos);
    EXPECT_NE(controllers.find("pids"), std::string::npos);
    EXPECT_NE(controllers.find("io"), std::string::npos);

    EXPECT_EQ(ProcFs::Generate("/sys/fs/cgroup/cpu.max"), "max 100000\n");

    std::string cpu_stat = ProcFs::Generate("/sys/fs/cgroup/cpu.stat");
    EXPECT_NE(cpu_stat.find("usage_usec "), std::string::npos);
    EXPECT_NE(cpu_stat.find("user_usec "), std::string::npos);
    EXPECT_NE(cpu_stat.find("system_usec "), std::string::npos);

    std::string mem_cur = ProcFs::Generate("/sys/fs/cgroup/memory.current");
    EXPECT_FALSE(mem_cur.empty());
    EXPECT_GT(std::stoull(mem_cur), 0ULL);

    std::string mem_max = ProcFs::Generate("/sys/fs/cgroup/memory.max");
    EXPECT_FALSE(mem_max.empty());
    EXPECT_GT(std::stoull(mem_max), 0ULL);

    std::string pressure = ProcFs::Generate("/sys/fs/cgroup/memory.pressure");
    EXPECT_NE(pressure.find("some avg10=0.00 avg60=0.00 avg300=0.00 total=0"), std::string::npos);
    EXPECT_NE(pressure.find("full avg10=0.00 avg60=0.00 avg300=0.00 total=0"), std::string::npos);

    std::string pids_cur = ProcFs::Generate("/sys/fs/cgroup/pids.current");
    EXPECT_GE(std::stoull(pids_cur), 1ULL);
    EXPECT_EQ(ProcFs::Generate("/sys/fs/cgroup/pids.max"), "max\n");

    auto entries = ProcFs::ListDir("/sys/fs/cgroup");
    bool found_controllers = false;
    bool found_top_app = false;
    for (const auto &e : entries) {
        if (e.name == "cgroup.controllers") {
            found_controllers = true;
        }
        if (e.name == "top-app") {
            found_top_app = true;
        }
    }
    EXPECT_TRUE(found_controllers);
    EXPECT_TRUE(found_top_app);
}

TEST(CgroupsCpuTopologyTest, CgroupV2AndroidSlicesAndProcCgroups)
{
    std::string proc_cgroups = ProcFs::Generate("/proc/cgroups");
    EXPECT_NE(proc_cgroups.find("#subsys_name"), std::string::npos);
    EXPECT_NE(proc_cgroups.find("cpu\t"), std::string::npos);
    EXPECT_NE(proc_cgroups.find("memory\t"), std::string::npos);

    std::string self_cgroup = ProcFs::Generate("/proc/self/cgroup");
    EXPECT_EQ(self_cgroup, "0::/uid_1000/top-app\n");

    std::string top_app_procs = ProcFs::Generate("/sys/fs/cgroup/top-app/cgroup.procs");
    EXPECT_FALSE(top_app_procs.empty());
    EXPECT_EQ(std::stoul(top_app_procs), GetCurrentProcessId());

    std::string uid_top_app = ProcFs::Generate("/sys/fs/cgroup/uid_1000/top-app/cgroup.procs");
    EXPECT_FALSE(uid_top_app.empty());

    Vfs vfs;
    auto root_res = vfs.Resolve("/sys/fs/cgroup");
    EXPECT_EQ(root_res.dev_type, DeviceType::Directory);
    auto slice_res = vfs.Resolve("/sys/fs/cgroup/top-app");
    EXPECT_EQ(slice_res.dev_type, DeviceType::Directory);
}

TEST(CgroupsCpuTopologyTest, CpuTopologyCoreAndSiblings)
{
    std::string kernel_max = ProcFs::Generate("/sys/devices/system/cpu/kernel_max");
    EXPECT_FALSE(kernel_max.empty());
    EXPECT_GE(std::stoi(kernel_max), 0);

    std::string core_id = ProcFs::Generate("/sys/devices/system/cpu/cpu0/topology/core_id");
    EXPECT_FALSE(core_id.empty());
    EXPECT_GE(std::stoi(core_id), 0);

    std::string pkg_id =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/topology/physical_package_id");
    EXPECT_FALSE(pkg_id.empty());
    EXPECT_GE(std::stoi(pkg_id), 0);

    std::string thread_siblings =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list");
    EXPECT_NE(thread_siblings.find('0'), std::string::npos);

    std::string core_siblings =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/topology/core_siblings_list");
    EXPECT_FALSE(core_siblings.empty());

    auto entries = ProcFs::ListDir("/sys/devices/system/cpu/cpu0/topology");
    EXPECT_EQ(entries.size(), 6u);

    Vfs vfs;
    EXPECT_EQ(vfs.Resolve("/sys/devices/system/cpu/cpu0/topology").dev_type, DeviceType::Directory);
}

TEST(CgroupsCpuTopologyTest, CpuFreqScalingFiles)
{
    std::string max_freq =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    std::string min_freq =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    std::string cur_freq =
        ProcFs::Generate("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    std::string gov = ProcFs::Generate("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    EXPECT_GT(std::stoull(max_freq), 0ULL);
    EXPECT_GT(std::stoull(min_freq), 0ULL);
    EXPECT_LE(std::stoull(min_freq), std::stoull(max_freq));
    EXPECT_GT(std::stoull(cur_freq), 0ULL);
    EXPECT_EQ(gov, "performance\n");

    auto entries = ProcFs::ListDir("/sys/devices/system/cpu/cpu0/cpufreq");
    EXPECT_EQ(entries.size(), 6u);

    Vfs vfs;
    EXPECT_EQ(vfs.Resolve("/sys/devices/system/cpu/cpu0/cpufreq").dev_type, DeviceType::Directory);
}

TEST(CgroupsCpuTopologyTest, SchedAffinityPerThreadRoundTrip)
{
    auto &tm = ThreadManager::Instance();
    int tid = tm.RegisterMainThread();
    auto th = tm.GetThread(tid);
    ASSERT_NE(th, nullptr);
    uint64_t saved_mask = th->affinity_mask;

    uint64_t set_mask = 1ULL;
    EXPECT_EQ(portage_sched_setaffinity(0, sizeof(set_mask), &set_mask), 0);

    uint64_t got_mask = 0;
    EXPECT_EQ(portage_sched_getaffinity(0, sizeof(got_mask), &got_mask), 0);
    EXPECT_EQ(got_mask, 1ULL);

    uint64_t zero_mask = 0;
    EXPECT_EQ(portage_sched_setaffinity(0, sizeof(zero_mask), &zero_mask), -1);
    EXPECT_EQ(portage_sched_getaffinity(0, 2, &got_mask), -1);

    DWORD_PTR proc_mask = 1, sys_mask = 1;
    GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask);
    uint64_t restore = static_cast<uint64_t>(proc_mask);
    portage_sched_setaffinity(0, sizeof(restore), &restore);
    if (th) {
        th->affinity_mask = saved_mask;
    }
}

TEST(CgroupsCpuTopologyTest, ProcSelfSmapsAndSmapsRollup)
{
    AddressSpace as;
    void *page = as.MmapAnonymous(nullptr, 4096 * 4, 0x3, 0x22);
    ASSERT_NE(page, nullptr);
    std::memset(page, 0xAB, 4096);

    std::string smaps = ProcFs::Generate("/proc/self/smaps", &as);
    EXPECT_NE(smaps.find("Size:"), std::string::npos);
    EXPECT_NE(smaps.find("Rss:"), std::string::npos);
    EXPECT_NE(smaps.find("Pss:"), std::string::npos);
    EXPECT_NE(smaps.find("VmFlags:"), std::string::npos);

    std::string rollup = ProcFs::Generate("/proc/self/smaps_rollup", &as);
    EXPECT_NE(rollup.find("[rollup]"), std::string::npos);
    EXPECT_NE(rollup.find("Rss:"), std::string::npos);
    EXPECT_NE(rollup.find("Pss:"), std::string::npos);

    EXPECT_EQ(as.Munmap(page, 4096 * 4), 0);
}

TEST(CgroupsCpuTopologyTest, ProcSelfStatusMemoryAccounting)
{
    std::string status = ProcFs::Generate("/proc/self/status");
    EXPECT_NE(status.find("VmPeak:"), std::string::npos);
    EXPECT_NE(status.find("VmSize:"), std::string::npos);
    EXPECT_NE(status.find("VmHWM:"), std::string::npos);
    EXPECT_NE(status.find("VmRSS:"), std::string::npos);
    EXPECT_NE(status.find("VmData:"), std::string::npos);
    EXPECT_NE(status.find("Threads:"), std::string::npos);
}

TEST(CgroupsCpuTopologyTest, BionicPosixAffinityThunkRegistration)
{
    auto &registry = NdkSymbolRegistry::Instance();
    RegisterBionicPosix(registry);
    EXPECT_NE(registry.Lookup("sched_setaffinity"), 0u);
    EXPECT_NE(registry.Lookup("sched_getaffinity"), 0u);
}
