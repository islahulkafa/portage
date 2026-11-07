#include "fs/proc_fs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace portage::fs {

bool ProcFs::IsProcPath(std::string_view path) noexcept
{
    return path == "/proc" || path.starts_with("/proc/");
}

std::string ProcFs::GenerateCpuInfo()
{
    char vendor[13] = {0};
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    *reinterpret_cast<int *>(vendor) = cpu_info[1];
    *reinterpret_cast<int *>(vendor + 4) = cpu_info[3];
    *reinterpret_cast<int *>(vendor + 8) = cpu_info[2];

    char brand[49] = {0};
    __cpuid(cpu_info, 0x80000000);
    unsigned int max_ex = static_cast<unsigned int>(cpu_info[0]);
    if (max_ex >= 0x80000004) {
        __cpuid(reinterpret_cast<int *>(brand), 0x80000002);
        __cpuid(reinterpret_cast<int *>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int *>(brand + 32), 0x80000004);
    } else {
        std::snprintf(brand, sizeof(brand), "Portage Virtual Processor");
    }

    __cpuid(cpu_info, 1);
    int stepping = cpu_info[0] & 0xf;
    int model = (cpu_info[0] >> 4) & 0xf;
    int family = (cpu_info[0] >> 8) & 0xf;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

    std::ostringstream ss;
    for (DWORD i = 0; i < count; ++i) {
        ss << "processor\t: " << i << "\n"
           << "vendor_id\t: " << vendor << "\n"
           << "cpu family\t: " << family << "\n"
           << "model\t\t: " << model << "\n"
           << "model name\t: " << brand << "\n"
           << "stepping\t: " << stepping << "\n"
           << "cpu MHz\t\t: 2400.000\n"
           << "cache size\t: 16384 KB\n"
           << "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 "
              "clflush mmx fxsr sse sse2 ss ht syscall nx lm constant_tsc\n"
           << "bogomips\t: 4800.00\n"
           << "clflush size\t: 64\n"
           << "cache_alignment\t: 64\n"
           << "address sizes\t: 39 bits physical, 48 bits virtual\n\n";
    }
    return ss.str();
}

std::string ProcFs::GenerateMemInfo()
{
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    uint64_t total_kb = ms.ullTotalPhys / 1024;
    uint64_t free_kb = ms.ullAvailPhys / 1024;
    uint64_t avail_kb = free_kb;
    uint64_t swap_total = (ms.ullTotalPageFile > ms.ullTotalPhys)
                              ? (ms.ullTotalPageFile - ms.ullTotalPhys) / 1024
                              : 0;
    uint64_t swap_free = (ms.ullAvailPageFile > ms.ullAvailPhys)
                             ? (ms.ullAvailPageFile - ms.ullAvailPhys) / 1024
                             : 0;

    std::ostringstream ss;
    ss << "MemTotal:       " << std::setw(8) << total_kb << " kB\n"
       << "MemFree:        " << std::setw(8) << free_kb << " kB\n"
       << "MemAvailable:   " << std::setw(8) << avail_kb << " kB\n"
       << "Buffers:            1024 kB\n"
       << "Cached:            65536 kB\n"
       << "SwapCached:            0 kB\n"
       << "Active:            32768 kB\n"
       << "Inactive:          32768 kB\n"
       << "SwapTotal:      " << std::setw(8) << swap_total << " kB\n"
       << "SwapFree:       " << std::setw(8) << swap_free << " kB\n";
    return ss.str();
}

std::string ProcFs::GenerateVersion()
{
    return "Linux version 6.1.0-android14-x86_64 (portage@win11) (clang 23.1.4) #1 SMP PREEMPT "
           "Portage\n";
}

std::string ProcFs::GenerateMaps(mem::AddressSpace *as)
{
    (void)as;
    std::ostringstream ss;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0x10000;

    while (addr < 0x7FFFFFFE0000ULL) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (mbi.State == MEM_COMMIT) {
            uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            uintptr_t end = start + mbi.RegionSize;

            char perms[5] = "---p";
            if (mbi.Protect & PAGE_READONLY) {
                perms[0] = 'r';
            } else if (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) {
                perms[0] = 'r';
                perms[1] = 'w';
            } else if (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE)) {
                perms[0] = 'r';
                perms[2] = 'x';
            } else if (mbi.Protect & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) {
                perms[0] = 'r';
                perms[1] = 'w';
                perms[2] = 'x';
            }

            ss << std::hex << std::setfill('0') << std::setw(12) << start << "-" << std::setw(12)
               << end << " " << perms << " 00000000 00:00 0 [anon]\n"
               << std::dec;
        }

        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) {
            break;
        }
        addr = next;
    }
    return ss.str();
}

std::string ProcFs::GenerateStatus()
{
    DWORD pid = GetCurrentProcessId();
    std::ostringstream ss;
    ss << "Name:\tportage\n"
       << "State:\tR (running)\n"
       << "Tgid:\t" << pid << "\n"
       << "Pid:\t" << pid << "\n"
       << "PPid:\t1\n"
       << "Threads:\t1\n"
       << "VmSize:\t  262144 kB\n"
       << "VmRSS:\t   65536 kB\n";
    return ss.str();
}

std::string ProcFs::GenerateCmdline(const std::vector<std::string> &cmdline)
{
    std::string res;
    if (cmdline.empty()) {
        res.push_back('\0');
        return res;
    }
    for (const auto &arg : cmdline) {
        res.append(arg);
        res.push_back('\0');
    }
    return res;
}

std::string ProcFs::Generate(std::string_view proc_path, mem::AddressSpace *as,
                             const std::vector<std::string> &cmdline)
{
    if (proc_path == "/proc/cpuinfo") {
        return GenerateCpuInfo();
    }
    if (proc_path == "/proc/meminfo") {
        return GenerateMemInfo();
    }
    if (proc_path == "/proc/version") {
        return GenerateVersion();
    }
    if (proc_path == "/proc/self/maps") {
        return GenerateMaps(as);
    }
    if (proc_path == "/proc/self/status") {
        return GenerateStatus();
    }
    if (proc_path == "/proc/self/cmdline") {
        return GenerateCmdline(cmdline);
    }
    return "";
}

std::vector<DirEntry> ProcFs::ListDir(std::string_view proc_path)
{
    std::vector<DirEntry> entries;
    entries.push_back({1, LINUX_DT_DIR, "."});
    entries.push_back({2, LINUX_DT_DIR, ".."});

    if (proc_path == "/proc" || proc_path == "/proc/") {
        entries.push_back({10, LINUX_DT_REG, "cpuinfo"});
        entries.push_back({11, LINUX_DT_REG, "meminfo"});
        entries.push_back({12, LINUX_DT_REG, "version"});
        entries.push_back({13, LINUX_DT_DIR, "self"});
    } else if (proc_path == "/proc/self" || proc_path == "/proc/self/") {
        entries.push_back({20, LINUX_DT_LNK, "exe"});
        entries.push_back({21, LINUX_DT_REG, "maps"});
        entries.push_back({22, LINUX_DT_REG, "status"});
        entries.push_back({23, LINUX_DT_REG, "cmdline"});
    }
    return entries;
}

} // namespace portage::fs
