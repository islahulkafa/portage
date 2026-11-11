#include "fs/proc_fs.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "mem/address_space.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace portage::fs {

bool ProcFs::IsProcPath(std::string_view path) noexcept
{
    return path == "/proc" || path.starts_with("/proc/") || path == "/sys" ||
           path.starts_with("/sys/");
}

std::string ProcFs::CanonicalizePath(std::string_view path)
{
    if (path.empty()) {
        return "";
    }

    std::string s(path);
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }

    if (s == "/proc/thread-self" || s.starts_with("/proc/thread-self/")) {
        return "/proc/self" + s.substr(17);
    }

    if (s.starts_with("/proc/")) {
        size_t next_slash = s.find('/', 6);
        std::string first_part =
            (next_slash == std::string::npos) ? s.substr(6) : s.substr(6, next_slash - 6);
        if (!first_part.empty() &&
            std::all_of(first_part.begin(), first_part.end(), [](unsigned char c) {
                return std::isdigit(c);
            })) {
            if (next_slash == std::string::npos) {
                return "/proc/self";
            }
            return "/proc/self" + s.substr(next_slash);
        }
    }

    return s;
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

            std::string pathname = "[anon]";
            uint64_t offset = 0;
            auto mod = elf::LinkMap::Instance().FindModuleByAddress(start);
            if (mod) {
                pathname = !mod->filepath.empty() ? mod->filepath : mod->name;
                offset = (start >= mod->elf.load_bias) ? (start - mod->elf.load_bias) : 0;
            } else if (as && start >= as->GetBrkStart() && start < as->GetBrkLimit()) {
                pathname = "[heap]";
                offset = 0;
            }

            ss << std::hex << std::setfill('0') << std::setw(12) << start << "-" << std::setw(12)
               << end << " " << perms << " " << std::setw(8) << offset << " 00:00 0";
            if (!pathname.empty()) {
                ss << " " << pathname;
            }
            ss << "\n" << std::dec;
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

std::string ProcFs::GenerateStat()
{
    DWORD pid = GetCurrentProcessId();
    std::ostringstream ss;
    ss << pid
       << " (portage) R 1 1 1 0 -1 4194304 0 0 0 0 10 5 0 0 20 0 1 0 1000 268435456 16384 "
          "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
    return ss.str();
}

std::string ProcFs::GenerateStatm()
{
    return "65536 16384 4096 8192 0 8192 0\n";
}

std::string ProcFs::GenerateComm(int tid)
{
    if (tid > 0) {
        auto th = thread::ThreadManager::Instance().GetThread(tid);
        if (th && !th->name.empty()) {
            return th->name + "\n";
        }
    } else {
        auto th = thread::ThreadManager::Instance().GetCurrentGuestThread();
        if (th && !th->name.empty()) {
            return th->name + "\n";
        }
    }
    return "portage\n";
}

std::string ProcFs::GenerateAuxv()
{
    struct Auxv64 {
        uint64_t a_type;
        uint64_t a_val;
    };
    std::vector<Auxv64> entries = {
        {6, 4096},               // AT_PAGESZ
        {17, 100},               // AT_CLKTCK
        {7, 0x7ffff7000000ULL},  // AT_BASE
        {8, 0},                  // AT_FLAGS
        {9, 0x400000ULL},        // AT_ENTRY
        {11, 1000},              // AT_UID
        {12, 1000},              // AT_EUID
        {13, 1000},              // AT_GID
        {14, 1000},              // AT_EGID
        {23, 0},                 // AT_SECURE
        {25, 0x7ffff7fff000ULL}, // AT_RANDOM
        {0, 0}                   // AT_NULL
    };
    return std::string(reinterpret_cast<const char *>(entries.data()),
                       entries.size() * sizeof(Auxv64));
}

std::string ProcFs::GenerateProcStat()
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

    std::ostringstream ss;
    ss << "cpu  2255 34 2290 22625563 6290 127 456 0 0 0\n";
    for (DWORD i = 0; i < count; ++i) {
        ss << "cpu" << i << " 1132 17 1145 11312781 3145 63 228 0 0 0\n";
    }
    ss << "intr 11493054 38 10 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0\n"
       << "ctxt 110815410\n"
       << "btime 1700000000\n"
       << "processes 12345\n"
       << "procs_running 1\n"
       << "procs_blocked 0\n";
    return ss.str();
}

std::string ProcFs::GenerateUptime()
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

    double up = static_cast<double>(GetTickCount64()) / 1000.0;
    double idle = up * count;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << up << " " << idle << "\n";
    return ss.str();
}

std::string ProcFs::GenerateCpuList()
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
    if (count <= 1) {
        return "0\n";
    }
    return "0-" + std::to_string(count - 1) + "\n";
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
    std::string path = CanonicalizePath(proc_path);

    if (path == "/proc/cpuinfo") {
        return GenerateCpuInfo();
    }
    if (path == "/proc/meminfo") {
        return GenerateMemInfo();
    }
    if (path == "/proc/version") {
        return GenerateVersion();
    }
    if (path == "/proc/self/maps") {
        return GenerateMaps(as);
    }
    if (path == "/proc/self/status") {
        return GenerateStatus();
    }
    if (path == "/proc/self/stat") {
        return GenerateStat();
    }
    if (path == "/proc/self/statm") {
        return GenerateStatm();
    }
    if (path == "/proc/self/comm") {
        return GenerateComm();
    }
    if (path == "/proc/self/auxv") {
        return GenerateAuxv();
    }
    if (path == "/proc/self/cmdline") {
        return GenerateCmdline(cmdline);
    }
    if (path == "/proc/stat") {
        return GenerateProcStat();
    }
    if (path == "/proc/uptime") {
        return GenerateUptime();
    }
    if (path == "/proc/sys/kernel/osrelease") {
        return "6.1.0-android14-x86_64\n";
    }
    if (path == "/proc/sys/kernel/random/boot_id") {
        return "4b827734-d020-410a-b286-63d1a8e10d32\n";
    }
    if (path == "/sys/devices/system/cpu/online" || path == "/sys/devices/system/cpu/possible" ||
        path == "/sys/devices/system/cpu/present") {
        return GenerateCpuList();
    }
    if (path.starts_with("/sys/devices/system/cpu/cpu") && path.ends_with("/online")) {
        return "1\n";
    }
    if (path.starts_with("/proc/self/task/")) {
        size_t last_slash = path.rfind('/');
        std::string op = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : "";
        std::string tid_str =
            path.substr(16, (last_slash > 16) ? (last_slash - 16) : std::string::npos);
        int tid = 0;
        try {
            tid = std::stoi(tid_str);
        } catch (...) {
            tid = 0;
        }
        if (op == "comm") {
            return GenerateComm(tid);
        }
        if (op == "stat") {
            return GenerateStat();
        }
        if (op == "status") {
            return GenerateStatus();
        }
    }
    return "";
}

std::vector<DirEntry> ProcFs::ListDir(std::string_view proc_path, FileDescriptorTable *fd_table)
{
    std::string path = CanonicalizePath(proc_path);
    std::vector<DirEntry> entries;
    entries.push_back({1, LINUX_DT_DIR, "."});
    entries.push_back({2, LINUX_DT_DIR, ".."});

    if (path == "/proc") {
        entries.push_back({10, LINUX_DT_REG, "cpuinfo"});
        entries.push_back({11, LINUX_DT_REG, "meminfo"});
        entries.push_back({12, LINUX_DT_REG, "version"});
        entries.push_back({13, LINUX_DT_REG, "stat"});
        entries.push_back({14, LINUX_DT_REG, "uptime"});
        entries.push_back({15, LINUX_DT_DIR, "self"});
        entries.push_back({16, LINUX_DT_DIR, "sys"});
    } else if (path == "/proc/self") {
        entries.push_back({20, LINUX_DT_LNK, "exe"});
        entries.push_back({21, LINUX_DT_REG, "maps"});
        entries.push_back({22, LINUX_DT_REG, "status"});
        entries.push_back({23, LINUX_DT_REG, "stat"});
        entries.push_back({24, LINUX_DT_REG, "statm"});
        entries.push_back({25, LINUX_DT_REG, "comm"});
        entries.push_back({26, LINUX_DT_REG, "auxv"});
        entries.push_back({27, LINUX_DT_REG, "cmdline"});
        entries.push_back({28, LINUX_DT_DIR, "fd"});
        entries.push_back({29, LINUX_DT_DIR, "task"});
    } else if (path == "/proc/self/fd") {
        if (fd_table) {
            for (int fd : fd_table->GetOpenFds()) {
                entries.push_back(
                    {static_cast<uint64_t>(100 + fd), LINUX_DT_LNK, std::to_string(fd)});
            }
        }
    } else if (path == "/proc/self/task") {
        auto tids = thread::ThreadManager::Instance().GetActiveTids();
        if (tids.empty()) {
            entries.push_back({1001, LINUX_DT_DIR, std::to_string(GetCurrentProcessId())});
        } else {
            for (int tid : tids) {
                entries.push_back(
                    {static_cast<uint64_t>(1000 + tid), LINUX_DT_DIR, std::to_string(tid)});
            }
        }
    } else if (path.starts_with("/proc/self/task/")) {
        entries.push_back({30, LINUX_DT_REG, "comm"});
        entries.push_back({31, LINUX_DT_REG, "stat"});
        entries.push_back({32, LINUX_DT_REG, "status"});
    } else if (path == "/proc/sys") {
        entries.push_back({40, LINUX_DT_DIR, "kernel"});
    } else if (path == "/proc/sys/kernel") {
        entries.push_back({50, LINUX_DT_REG, "osrelease"});
        entries.push_back({51, LINUX_DT_DIR, "random"});
    } else if (path == "/proc/sys/kernel/random") {
        entries.push_back({60, LINUX_DT_REG, "boot_id"});
    } else if (path == "/sys") {
        entries.push_back({70, LINUX_DT_DIR, "devices"});
    } else if (path == "/sys/devices") {
        entries.push_back({80, LINUX_DT_DIR, "system"});
    } else if (path == "/sys/devices/system") {
        entries.push_back({90, LINUX_DT_DIR, "cpu"});
    } else if (path == "/sys/devices/system/cpu") {
        entries.push_back({91, LINUX_DT_REG, "online"});
        entries.push_back({92, LINUX_DT_REG, "possible"});
        entries.push_back({93, LINUX_DT_REG, "present"});
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
        for (DWORD i = 0; i < count; ++i) {
            entries.push_back(
                {static_cast<uint64_t>(500 + i), LINUX_DT_DIR, "cpu" + std::to_string(i)});
        }
    } else if (path.starts_with("/sys/devices/system/cpu/cpu")) {
        entries.push_back({100, LINUX_DT_REG, "online"});
    }
    return entries;
}

} // namespace portage::fs
