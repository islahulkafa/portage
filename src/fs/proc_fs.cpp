#include "fs/proc_fs.hpp"
#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "mem/address_space.hpp"
#include "process/namespace_manager.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <intrin.h>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

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
        if (!first_part.empty() && std::all_of(first_part.begin(), first_part.end(),
                                               [](unsigned char c) { return std::isdigit(c); })) {
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

std::string ProcFs::GenerateSmaps(mem::AddressSpace *as, bool rollup_only)
{
    std::ostringstream ss;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0x10000;

    size_t total_rss_kb = 0;
    size_t total_pss_kb = 0;
    size_t total_priv_clean_kb = 0;
    size_t total_priv_dirty_kb = 0;
    size_t total_anon_kb = 0;

    while (addr < 0x7FFFFFFE0000ULL) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (mbi.State == MEM_COMMIT) {
            uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            uintptr_t end = start + mbi.RegionSize;
            size_t size_kb = mbi.RegionSize / 1024;
            if (size_kb == 0) {
                size_kb = 4;
            }

            char r =
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
                    ? 'r'
                    : '-';
            char w = (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                                     PAGE_EXECUTE_WRITECOPY))
                         ? 'w'
                         : '-';
            char x = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                     PAGE_EXECUTE_WRITECOPY))
                         ? 'x'
                         : '-';
            char p = (mbi.Type == MEM_MAPPED) ? 's' : 'p';

            bool is_private_anon = (mbi.Type == MEM_PRIVATE);
            size_t priv_dirty = (w == 'w' && is_private_anon) ? size_kb : 0;
            size_t priv_clean = (priv_dirty == 0) ? size_kb : 0;
            size_t anon_kb = is_private_anon ? size_kb : 0;

            total_rss_kb += size_kb;
            total_pss_kb += size_kb;
            total_priv_clean_kb += priv_clean;
            total_priv_dirty_kb += priv_dirty;
            total_anon_kb += anon_kb;

            if (!rollup_only) {
                std::string map_name;
                if (as && as->GetBrkStart() != 0 && start >= as->GetBrkStart() &&
                    start < as->GetBrkCurrent()) {
                    map_name = "[heap]";
                }
                if (map_name.empty()) {
                    elf::Dl_info dli{};
                    if (elf::LinkMap::Instance().DlAddr(reinterpret_cast<const void *>(start),
                                                        &dli) != 0 &&
                        dli.dli_fname) {
                        map_name = dli.dli_fname;
                    }
                }

                ss << std::hex << std::setw(8) << std::setfill('0') << start << "-" << std::setw(8)
                   << std::setfill('0') << end << " " << r << w << x << p << " 00000000 00:00 0";
                if (!map_name.empty()) {
                    ss << "                          " << map_name;
                }
                ss << std::dec << "\n"
                   << "Size:           " << std::setw(8) << std::setfill(' ') << size_kb << " kB\n"
                   << "KernelPageSize:        4 kB\n"
                   << "MMUPageSize:           4 kB\n"
                   << "Rss:            " << std::setw(8) << size_kb << " kB\n"
                   << "Pss:            " << std::setw(8) << size_kb << " kB\n"
                   << "Shared_Clean:          0 kB\n"
                   << "Shared_Dirty:          0 kB\n"
                   << "Private_Clean:  " << std::setw(8) << priv_clean << " kB\n"
                   << "Private_Dirty:  " << std::setw(8) << priv_dirty << " kB\n"
                   << "Referenced:     " << std::setw(8) << size_kb << " kB\n"
                   << "Anonymous:      " << std::setw(8) << anon_kb << " kB\n"
                   << "LazyFree:              0 kB\n"
                   << "AnonHugePages:         0 kB\n"
                   << "ShmemPmdMapped:        0 kB\n"
                   << "FilePmdMapped:         0 kB\n"
                   << "Shared_Hugetlb:        0 kB\n"
                   << "Private_Hugetlb:       0 kB\n"
                   << "Swap:                  0 kB\n"
                   << "SwapPss:               0 kB\n"
                   << "Locked:                0 kB\n"
                   << "THPeligible:           0\n"
                   << "VmFlags: " << (r == 'r' ? "rd " : "") << (w == 'w' ? "wr " : "")
                   << (x == 'x' ? "ex " : "") << "mr mw me" << (p == 's' ? " sh" : " ac") << "\n";
            }
        }

        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) {
            break;
        }
        addr = next;
    }

    if (rollup_only) {
        ss << "00000000-00000000 ---p 00000000 00:00 0                          [rollup]\n"
           << "Rss:            " << std::setw(8) << std::setfill(' ') << total_rss_kb << " kB\n"
           << "Pss:            " << std::setw(8) << total_pss_kb << " kB\n"
           << "Shared_Clean:          0 kB\n"
           << "Shared_Dirty:          0 kB\n"
           << "Private_Clean:  " << std::setw(8) << total_priv_clean_kb << " kB\n"
           << "Private_Dirty:  " << std::setw(8) << total_priv_dirty_kb << " kB\n"
           << "Referenced:     " << std::setw(8) << total_rss_kb << " kB\n"
           << "Anonymous:      " << std::setw(8) << total_anon_kb << " kB\n"
           << "Swap:                  0 kB\n"
           << "SwapPss:               0 kB\n"
           << "Locked:                0 kB\n";
    }

    return ss.str();
}

std::string ProcFs::GenerateStatus()
{
    DWORD pid = GetCurrentProcessId();
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                         sizeof(pmc));

    size_t vm_rss_kb = pmc.WorkingSetSize > 0 ? (pmc.WorkingSetSize / 1024) : 65536;
    size_t vm_hwm_kb = pmc.PeakWorkingSetSize > 0 ? (pmc.PeakWorkingSetSize / 1024) : vm_rss_kb;
    size_t vm_size_kb =
        pmc.PrivateUsage > pmc.WorkingSetSize ? (pmc.PrivateUsage / 1024) : (vm_rss_kb * 2);
    size_t vm_peak_kb = pmc.PeakPagefileUsage > pmc.PeakWorkingSetSize
                            ? (pmc.PeakPagefileUsage / 1024)
                            : vm_size_kb;
    size_t rss_anon_kb =
        pmc.PrivateUsage > 0 ? std::min(vm_rss_kb, pmc.PrivateUsage / 1024) : (vm_rss_kb * 3 / 4);
    size_t rss_file_kb = vm_rss_kb > rss_anon_kb ? (vm_rss_kb - rss_anon_kb) : 0;
    size_t threads = std::max<size_t>(1, thread::ThreadManager::Instance().GetActiveTids().size());

    std::ostringstream ss;
    ss << "Name:\tportage\n"
       << "Umask:\t0022\n"
       << "State:\tR (running)\n"
       << "Tgid:\t" << pid << "\n"
       << "Ngid:\t0\n"
       << "Pid:\t" << pid << "\n"
       << "PPid:\t1\n"
       << "TracerPid:\t0\n"
       << "Uid:\t1000\t1000\t1000\t1000\n"
       << "Gid:\t1000\t1000\t1000\t1000\n"
       << "Threads:\t" << threads << "\n"
       << "VmPeak:\t" << std::setw(8) << vm_peak_kb << " kB\n"
       << "VmSize:\t" << std::setw(8) << vm_size_kb << " kB\n"
       << "VmLck:\t       0 kB\n"
       << "VmPin:\t       0 kB\n"
       << "VmHWM:\t" << std::setw(8) << vm_hwm_kb << " kB\n"
       << "VmRSS:\t" << std::setw(8) << vm_rss_kb << " kB\n"
       << "RssAnon:\t" << std::setw(8) << rss_anon_kb << " kB\n"
       << "RssFile:\t" << std::setw(8) << rss_file_kb << " kB\n"
       << "RssShmem:\t       0 kB\n"
       << "VmData:\t" << std::setw(8) << rss_anon_kb << " kB\n"
       << "VmStk:\t    8192 kB\n"
       << "VmExe:\t    4096 kB\n"
       << "VmLib:\t   16384 kB\n"
       << "VmPTE:\t     256 kB\n"
       << "VmSwap:\t       0 kB\n";
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

std::string ProcFs::GenerateStatm() { return "65536 16384 4096 8192 0 8192 0\n"; }

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

std::string ProcFs::GenerateCgroups()
{
    return "#subsys_name\thierarchy\tnum_cgroups\tenabled\n"
           "cpuset\t0\t1\t1\n"
           "cpu\t0\t1\t1\n"
           "cpuacct\t0\t1\t1\n"
           "memory\t0\t1\t1\n"
           "devices\t0\t1\t1\n"
           "freezer\t0\t1\t1\n"
           "net_cls\t0\t1\t1\n"
           "pids\t0\t1\t1\n";
}

std::string ProcFs::GenerateSelfCgroup() { return "0::/uid_1000/top-app\n"; }

std::string ProcFs::GenerateCgroupFile(std::string_view path)
{
    size_t last_slash = path.rfind('/');
    std::string_view leaf =
        (last_slash != std::string_view::npos) ? path.substr(last_slash + 1) : path;

    if (leaf == "cgroup.controllers" || leaf == "cgroup.subtree_control") {
        return "cpu memory cpuset pids io\n";
    }
    if (leaf == "cgroup.procs") {
        return std::to_string(GetCurrentProcessId()) + "\n";
    }
    if (leaf == "cgroup.threads") {
        auto tids = thread::ThreadManager::Instance().GetActiveTids();
        if (tids.empty()) {
            return std::to_string(GetCurrentProcessId()) + "\n";
        }
        std::ostringstream ss;
        for (int tid : tids) {
            ss << tid << "\n";
        }
        return ss.str();
    }
    if (leaf == "cgroup.max.descendants" || leaf == "cgroup.max.depth" || leaf == "pids.max") {
        return "max\n";
    }
    if (leaf == "pids.current") {
        auto tids = thread::ThreadManager::Instance().GetActiveTids();
        return std::to_string(std::max<size_t>(1, tids.size())) + "\n";
    }
    if (leaf == "cgroup.stat") {
        return "nr_descendants 8\nnr_dying_descendants 0\n";
    }
    if (leaf == "memory.current" || leaf == "memory.max" || leaf == "memory.high" ||
        leaf == "memory.stat") {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                             sizeof(pmc));
        uint64_t current_bytes =
            pmc.WorkingSetSize > 0 ? pmc.WorkingSetSize : (64ULL * 1024 * 1024);

        MEMORYSTATUSEX mem_status{};
        mem_status.dwLength = sizeof(mem_status);
        GlobalMemoryStatusEx(&mem_status);
        uint64_t total_phys =
            mem_status.ullTotalPhys > 0 ? mem_status.ullTotalPhys : (8ULL * 1024 * 1024 * 1024);

        if (leaf == "memory.current") {
            return std::to_string(current_bytes) + "\n";
        }
        if (leaf == "memory.max") {
            return std::to_string(total_phys) + "\n";
        }
        if (leaf == "memory.high") {
            return std::to_string((total_phys * 9) / 10) + "\n";
        }

        uint64_t anon = pmc.PrivateUsage > 0 ? pmc.PrivateUsage : (current_bytes * 3 / 4);
        uint64_t file_bytes = current_bytes > anon ? (current_bytes - anon) : 0;
        std::ostringstream ss;
        ss << "anon " << anon << "\n"
           << "file " << file_bytes << "\n"
           << "kernel_stack 16384\n"
           << "pagetables 65536\n"
           << "shmem 0\n"
           << "inactive_anon " << (anon / 2) << "\n"
           << "active_anon " << (anon - anon / 2) << "\n";
        return ss.str();
    }
    if (leaf == "memory.min" || leaf == "memory.low") {
        return "0\n";
    }
    if (leaf == "memory.pressure" || leaf == "cpu.pressure" || leaf == "io.pressure") {
        return "some avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
               "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n";
    }
    if (leaf == "memory.events") {
        return "low 0\nhigh 0\nmax 0\noom 0\noom_kill 0\n";
    }
    if (leaf == "cpu.weight") {
        return "100\n";
    }
    if (leaf == "cpu.max") {
        return "max 100000\n";
    }
    if (leaf == "cpu.stat") {
        FILETIME creation{}, exit{}, kernel{}, user{};
        uint64_t user_usec = 100000;
        uint64_t sys_usec = 50000;
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            ULARGE_INTEGER k{}, u{};
            k.LowPart = kernel.dwLowDateTime;
            k.HighPart = kernel.dwHighDateTime;
            u.LowPart = user.dwLowDateTime;
            u.HighPart = user.dwHighDateTime;
            sys_usec = k.QuadPart / 10;
            user_usec = u.QuadPart / 10;
        }
        std::ostringstream ss;
        ss << "usage_usec " << (user_usec + sys_usec) << "\n"
           << "user_usec " << user_usec << "\n"
           << "system_usec " << sys_usec << "\n"
           << "nr_periods 0\n"
           << "nr_throttled 0\n"
           << "throttled_usec 0\n";
        return ss.str();
    }
    if (leaf == "cpuset.cpus" || leaf == "cpuset.cpus.effective") {
        return GenerateCpuList();
    }
    if (leaf == "cpuset.mems" || leaf == "cpuset.mems.effective") {
        return "0\n";
    }
    return "";
}

namespace {

struct CpuTopologyData {
    DWORD logical_count{1};
    DWORD freq_khz{2400000};
    std::unordered_map<DWORD, DWORD> cpu_to_core;
    std::unordered_map<DWORD, std::string> cpu_to_siblings;
};

CpuTopologyData QueryCpuTopology()
{
    CpuTopologyData data{};
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    data.logical_count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

    DWORD mhz = 0;
    DWORD mhz_size = sizeof(mhz);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &mhz_size) == ERROR_SUCCESS &&
        mhz > 0) {
        data.freq_khz = mhz * 1000;
    }

    DWORD buf_len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buf_len);
    if (buf_len > 0) {
        std::vector<uint8_t> buffer(buf_len);
        auto *info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &buf_len)) {
            DWORD offset = 0;
            DWORD core_id = 0;
            while (offset < buf_len) {
                auto *curr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                    buffer.data() + offset);
                if (curr->Relationship == RelationProcessorCore && curr->Processor.GroupCount > 0) {
                    KAFFINITY mask = curr->Processor.GroupMask[0].Mask;
                    std::vector<DWORD> siblings;
                    for (DWORD bit = 0; bit < 64; ++bit) {
                        if (mask & (1ULL << bit)) {
                            siblings.push_back(bit);
                        }
                    }
                    std::string sib_str;
                    for (size_t i = 0; i < siblings.size(); ++i) {
                        if (i > 0) {
                            sib_str.push_back(',');
                        }
                        sib_str += std::to_string(siblings[i]);
                    }
                    for (DWORD cpu_idx : siblings) {
                        data.cpu_to_core[cpu_idx] = core_id;
                        data.cpu_to_siblings[cpu_idx] = sib_str;
                    }
                    core_id++;
                }
                if (curr->Size == 0) {
                    break;
                }
                offset += curr->Size;
            }
        }
    }

    for (DWORD i = 0; i < data.logical_count; ++i) {
        if (!data.cpu_to_core.contains(i)) {
            data.cpu_to_core[i] = i;
        }
        if (!data.cpu_to_siblings.contains(i)) {
            data.cpu_to_siblings[i] = std::to_string(i);
        }
    }

    return data;
}

} // namespace

std::string ProcFs::GenerateCpuTopologyFile(std::string_view path)
{
    auto topo = QueryCpuTopology();
    if (path == "/sys/devices/system/cpu/kernel_max") {
        return std::to_string(topo.logical_count > 0 ? topo.logical_count - 1 : 0) + "\n";
    }

    constexpr std::string_view prefix = "/sys/devices/system/cpu/cpu";
    if (!path.starts_with(prefix)) {
        return "";
    }
    std::string_view rest = path.substr(prefix.size());
    size_t slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return "";
    }
    DWORD cpu_idx = 0;
    try {
        cpu_idx = static_cast<DWORD>(std::stoul(std::string(rest.substr(0, slash))));
    } catch (...) {
        return "";
    }
    std::string_view sub = rest.substr(slash + 1);

    if (sub == "online") {
        return "1\n";
    }
    if (sub == "topology/core_id") {
        return std::to_string(topo.cpu_to_core.contains(cpu_idx) ? topo.cpu_to_core[cpu_idx]
                                                                 : cpu_idx) +
               "\n";
    }
    if (sub == "topology/physical_package_id") {
        return "0\n";
    }
    if (sub == "topology/thread_siblings_list") {
        return (topo.cpu_to_siblings.contains(cpu_idx) ? topo.cpu_to_siblings[cpu_idx]
                                                       : std::to_string(cpu_idx)) +
               "\n";
    }
    if (sub == "topology/core_siblings_list") {
        return GenerateCpuList();
    }
    if (sub == "cpufreq/cpuinfo_max_freq" || sub == "cpufreq/scaling_cur_freq") {
        return std::to_string(topo.freq_khz) + "\n";
    }
    if (sub == "cpufreq/cpuinfo_min_freq") {
        return std::to_string(topo.freq_khz / 3) + "\n";
    }
    if (sub == "cpufreq/scaling_governor") {
        return "performance\n";
    }
    return "";
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
    if (path == "/proc/cgroups") {
        return GenerateCgroups();
    }
    if (path == "/proc/version") {
        return GenerateVersion();
    }
    if (path == "/proc/self/maps") {
        return GenerateMaps(as);
    }
    if (path == "/proc/self/smaps") {
        return GenerateSmaps(as, false);
    }
    if (path == "/proc/self/smaps_rollup") {
        return GenerateSmaps(as, true);
    }
    if (path == "/proc/self/cgroup") {
        return GenerateSelfCgroup();
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
    if (path == "/proc/self/ns/pid") {
        auto t = thread::ThreadManager::Instance().GetCurrentGuestThread();
        uint64_t id = (t && t->pid_ns)
                          ? t->pid_ns->GetId()
                          : process::NamespaceManager::Instance().GetRootPidNamespace()->GetId();
        return "pid:[" + std::to_string(id) + "]";
    }
    if (path == "/proc/self/ns/mnt") {
        auto t = thread::ThreadManager::Instance().GetCurrentGuestThread();
        uint64_t id = (t && t->mnt_ns)
                          ? t->mnt_ns->GetId()
                          : process::NamespaceManager::Instance().GetRootMountNamespace()->GetId();
        return "mnt:[" + std::to_string(id) + "]";
    }
    if (path == "/proc/self/ns/uts") {
        auto t = thread::ThreadManager::Instance().GetCurrentGuestThread();
        uint64_t id = (t && t->uts_ns)
                          ? t->uts_ns->GetId()
                          : process::NamespaceManager::Instance().GetRootUtsNamespace()->GetId();
        return "uts:[" + std::to_string(id) + "]";
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
    if (path == "/sys/devices/system/cpu/kernel_max" ||
        path.starts_with("/sys/devices/system/cpu/cpu")) {
        return GenerateCpuTopologyFile(path);
    }
    if (path.starts_with("/sys/fs/cgroup/")) {
        return GenerateCgroupFile(path);
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
        entries.push_back({17, LINUX_DT_REG, "cgroups"});
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
        entries.push_back({30, LINUX_DT_DIR, "ns"});
        entries.push_back({31, LINUX_DT_REG, "smaps"});
        entries.push_back({32, LINUX_DT_REG, "smaps_rollup"});
        entries.push_back({33, LINUX_DT_REG, "cgroup"});
    } else if (path == "/proc/self/ns") {
        entries.push_back({31, LINUX_DT_LNK, "pid"});
        entries.push_back({32, LINUX_DT_LNK, "mnt"});
        entries.push_back({33, LINUX_DT_LNK, "uts"});
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
        entries.push_back({71, LINUX_DT_DIR, "fs"});
    } else if (path == "/sys/fs") {
        entries.push_back({72, LINUX_DT_DIR, "cgroup"});
    } else if (path == "/sys/fs/cgroup") {
        entries.push_back({110, LINUX_DT_REG, "cgroup.controllers"});
        entries.push_back({111, LINUX_DT_REG, "cgroup.procs"});
        entries.push_back({112, LINUX_DT_REG, "cgroup.threads"});
        entries.push_back({113, LINUX_DT_REG, "cgroup.stat"});
        entries.push_back({114, LINUX_DT_REG, "cpu.max"});
        entries.push_back({115, LINUX_DT_REG, "cpu.stat"});
        entries.push_back({116, LINUX_DT_REG, "cpu.pressure"});
        entries.push_back({117, LINUX_DT_REG, "memory.current"});
        entries.push_back({118, LINUX_DT_REG, "memory.max"});
        entries.push_back({119, LINUX_DT_REG, "memory.high"});
        entries.push_back({120, LINUX_DT_REG, "memory.stat"});
        entries.push_back({121, LINUX_DT_REG, "memory.pressure"});
        entries.push_back({122, LINUX_DT_REG, "cpuset.cpus"});
        entries.push_back({123, LINUX_DT_REG, "cpuset.cpus.effective"});
        entries.push_back({124, LINUX_DT_REG, "pids.current"});
        entries.push_back({125, LINUX_DT_REG, "pids.max"});
        entries.push_back({126, LINUX_DT_REG, "io.stat"});
        entries.push_back({127, LINUX_DT_REG, "io.pressure"});
        entries.push_back({128, LINUX_DT_DIR, "top-app"});
        entries.push_back({129, LINUX_DT_DIR, "foreground"});
        entries.push_back({130, LINUX_DT_DIR, "background"});
        entries.push_back({131, LINUX_DT_DIR, "system"});
        entries.push_back({132, LINUX_DT_DIR, "nnapi-hal"});
        entries.push_back({133, LINUX_DT_DIR, "camera-daemon"});
        entries.push_back({134, LINUX_DT_DIR, "uid_0"});
        entries.push_back({135, LINUX_DT_DIR, "uid_1000"});
    } else if (path.starts_with("/sys/fs/cgroup/")) {
        entries.push_back({140, LINUX_DT_REG, "cgroup.procs"});
        entries.push_back({141, LINUX_DT_REG, "cgroup.threads"});
        entries.push_back({142, LINUX_DT_REG, "cgroup.controllers"});
        entries.push_back({143, LINUX_DT_REG, "cpu.max"});
        entries.push_back({144, LINUX_DT_REG, "cpu.stat"});
        entries.push_back({145, LINUX_DT_REG, "memory.current"});
        entries.push_back({146, LINUX_DT_REG, "memory.max"});
        entries.push_back({147, LINUX_DT_REG, "cpuset.cpus"});
        entries.push_back({148, LINUX_DT_REG, "cpuset.cpus.effective"});
        if (path == "/sys/fs/cgroup/uid_1000") {
            entries.push_back({149, LINUX_DT_DIR, "top-app"});
        }
    } else if (path == "/sys/devices") {
        entries.push_back({80, LINUX_DT_DIR, "system"});
    } else if (path == "/sys/devices/system") {
        entries.push_back({90, LINUX_DT_DIR, "cpu"});
    } else if (path == "/sys/devices/system/cpu") {
        entries.push_back({91, LINUX_DT_REG, "online"});
        entries.push_back({92, LINUX_DT_REG, "possible"});
        entries.push_back({93, LINUX_DT_REG, "present"});
        entries.push_back({94, LINUX_DT_REG, "kernel_max"});
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        DWORD count = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
        for (DWORD i = 0; i < count; ++i) {
            entries.push_back(
                {static_cast<uint64_t>(500 + i), LINUX_DT_DIR, "cpu" + std::to_string(i)});
        }
    } else if (path.starts_with("/sys/devices/system/cpu/cpu")) {
        if (path.ends_with("/topology")) {
            entries.push_back({103, LINUX_DT_REG, "core_id"});
            entries.push_back({104, LINUX_DT_REG, "physical_package_id"});
            entries.push_back({105, LINUX_DT_REG, "thread_siblings_list"});
            entries.push_back({106, LINUX_DT_REG, "core_siblings_list"});
        } else if (path.ends_with("/cpufreq")) {
            entries.push_back({107, LINUX_DT_REG, "cpuinfo_max_freq"});
            entries.push_back({108, LINUX_DT_REG, "cpuinfo_min_freq"});
            entries.push_back({109, LINUX_DT_REG, "scaling_cur_freq"});
            entries.push_back({110, LINUX_DT_REG, "scaling_governor"});
        } else {
            entries.push_back({100, LINUX_DT_REG, "online"});
            entries.push_back({101, LINUX_DT_DIR, "topology"});
            entries.push_back({102, LINUX_DT_DIR, "cpufreq"});
        }
    }
    return entries;
}

} // namespace portage::fs
