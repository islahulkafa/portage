#pragma once

#include "fs/fd_table.hpp"
#include "poll_types.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace portage::poll {

struct EpollItem {
    int fd{0};
    uint32_t events{0};
    uint64_t data{0};
};

struct EpollInstance {
    int epfd{0};
    std::unordered_map<int, EpollItem> items{};
};

class EpollManager {
  public:
    static EpollManager &Instance() noexcept;

    int CreateInstance() noexcept;
    int CloseInstance(int epfd) noexcept;

    int Ctl(int epfd, int op, int fd, const LinuxEpollEvent *event) noexcept;
    int Wait(int epfd, LinuxEpollEvent *events, int maxevents, int timeout_ms,
             fs::FileDescriptorTable *fd_table) noexcept;

    int Poll(LinuxPollFd *fds, size_t nfds, int timeout_ms,
             fs::FileDescriptorTable *fd_table) noexcept;

  private:
    EpollManager() = default;
    std::mutex m_mutex{};
    std::unordered_map<int, EpollInstance> m_instances{};
    int m_next_epfd{10000};
};

} // namespace portage::poll
