#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace portage::process {

class ProcessManager {
  public:
    static ProcessManager &Instance() noexcept;

    int Spawn(const std::string &host_portage_exe, const std::string &host_elf_path,
              const std::vector<std::string> &args, const std::vector<std::string> &env);

    int64_t Wait4(int pid, int *wstatus, int options, void *rusage);

  private:
    ProcessManager() = default;
    std::mutex m_mutex{};
    std::unordered_map<int, HANDLE> m_children{};
    int m_next_pid{1000};
};

} // namespace portage::process
