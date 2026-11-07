#include "process/process_manager.hpp"

#include <sstream>

namespace portage::process {

ProcessManager &ProcessManager::Instance() noexcept
{
    static ProcessManager instance;
    return instance;
}

int ProcessManager::Spawn(const std::string &host_portage_exe, const std::string &host_elf_path,
                          const std::vector<std::string> &args, const std::vector<std::string> &env)
{
    (void)env;
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream cmd;
    cmd << "\"" << host_portage_exe << "\" \"" << host_elf_path << "\"";
    for (size_t i = 1; i < args.size(); ++i) {
        cmd << " \"" << args[i] << "\"";
    }

    std::string cmd_str = cmd.str();
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessA(nullptr, cmd_str.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                             &si, &pi);
    if (!ok) {
        return -11; // -EAGAIN
    }

    CloseHandle(pi.hThread);
    int assigned_pid = m_next_pid++;
    m_children[assigned_pid] = pi.hProcess;
    return assigned_pid;
}

int64_t ProcessManager::Wait4(int pid, int *wstatus, int options, void *rusage)
{
    (void)rusage;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (pid > 0) {
        auto it = m_children.find(pid);
        if (it == m_children.end()) {
            return -10; // -ECHILD
        }

        HANDLE h = it->second;
        DWORD wait_ms = (options & 1) ? 0 : INFINITE; // WNOHANG = 1
        DWORD res = WaitForSingleObject(h, wait_ms);

        if (res == WAIT_TIMEOUT) {
            return 0;
        }

        if (res == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeProcess(h, &exit_code);
            CloseHandle(h);
            m_children.erase(it);

            if (wstatus) {
                *wstatus = static_cast<int>((exit_code & 0xFF) << 8);
            }
            return static_cast<int64_t>(pid);
        }

        return -10; // -ECHILD
    }

    if (pid == -1) {
        if (m_children.empty()) {
            return -10; // -ECHILD
        }

        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            int child_pid = it->first;
            HANDLE h = it->second;
            DWORD wait_ms = (options & 1) ? 0 : 50;
            DWORD res = WaitForSingleObject(h, wait_ms);

            if (res == WAIT_OBJECT_0) {
                DWORD exit_code = 0;
                GetExitCodeProcess(h, &exit_code);
                CloseHandle(h);
                m_children.erase(it);

                if (wstatus) {
                    *wstatus = static_cast<int>((exit_code & 0xFF) << 8);
                }
                return static_cast<int64_t>(child_pid);
            }
        }

        if (options & 1) { // WNOHANG
            return 0;
        }
    }

    return -10; // -ECHILD
}

} // namespace portage::process
