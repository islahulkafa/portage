#pragma once

#include "fs/dirent.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace portage::mem {
class AddressSpace;
}

namespace portage::fs {

class FileDescriptorTable;

class ProcFs {
  public:
    static bool IsProcPath(std::string_view path) noexcept;
    static std::string CanonicalizePath(std::string_view path);

    static std::string Generate(std::string_view proc_path, mem::AddressSpace *as = nullptr,
                                const std::vector<std::string> &cmdline = {});
    static std::vector<DirEntry> ListDir(std::string_view proc_path,
                                         FileDescriptorTable *fd_table = nullptr);

  private:
    static std::string GenerateCpuInfo();
    static std::string GenerateMemInfo();
    static std::string GenerateVersion();
    static std::string GenerateMaps(mem::AddressSpace *as);
    static std::string GenerateSmaps(mem::AddressSpace *as, bool rollup_only = false);
    static std::string GenerateStatus();
    static std::string GenerateStat();
    static std::string GenerateStatm();
    static std::string GenerateComm(int tid = 0);
    static std::string GenerateAuxv();
    static std::string GenerateProcStat();
    static std::string GenerateUptime();
    static std::string GenerateCpuList();
    static std::string GenerateCmdline(const std::vector<std::string> &cmdline);
    static std::string GenerateCgroups();
    static std::string GenerateSelfCgroup();
    static std::string GenerateCgroupFile(std::string_view path);
    static std::string GenerateCpuTopologyFile(std::string_view path);
};

} // namespace portage::fs
