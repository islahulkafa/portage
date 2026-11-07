#pragma once

#include "fs/dirent.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace portage::mem {
class AddressSpace;
}

namespace portage::fs {

class ProcFs {
  public:
    static bool IsProcPath(std::string_view path) noexcept;
    static std::string Generate(std::string_view proc_path, mem::AddressSpace *as = nullptr,
                                const std::vector<std::string> &cmdline = {});
    static std::vector<DirEntry> ListDir(std::string_view proc_path);

  private:
    static std::string GenerateCpuInfo();
    static std::string GenerateMemInfo();
    static std::string GenerateVersion();
    static std::string GenerateMaps(mem::AddressSpace *as);
    static std::string GenerateStatus();
    static std::string GenerateCmdline(const std::vector<std::string> &cmdline);
};

} // namespace portage::fs
