#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::poll {

constexpr int16_t LINUX_POLLIN = 0x0001;
constexpr int16_t LINUX_POLLPRI = 0x0002;
constexpr int16_t LINUX_POLLOUT = 0x0004;
constexpr int16_t LINUX_POLLERR = 0x0008;
constexpr int16_t LINUX_POLLHUP = 0x0010;
constexpr int16_t LINUX_POLLNVAL = 0x0020;
constexpr int16_t LINUX_POLLRDNORM = 0x0040;
constexpr int16_t LINUX_POLLRDBAND = 0x0080;
constexpr int16_t LINUX_POLLWRNORM = 0x0100;
constexpr int16_t LINUX_POLLRDHUP = 0x2000;

struct LinuxPollFd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

constexpr int LINUX_EPOLL_CTL_ADD = 1;
constexpr int LINUX_EPOLL_CTL_DEL = 2;
constexpr int LINUX_EPOLL_CTL_MOD = 3;

constexpr uint32_t LINUX_EPOLLIN = 0x00000001;
constexpr uint32_t LINUX_EPOLLPRI = 0x00000002;
constexpr uint32_t LINUX_EPOLLOUT = 0x00000004;
constexpr uint32_t LINUX_EPOLLERR = 0x00000008;
constexpr uint32_t LINUX_EPOLLHUP = 0x00000010;
constexpr uint32_t LINUX_EPOLLRDHUP = 0x00002000;
constexpr uint32_t LINUX_EPOLLONESHOT = 0x40000000;
constexpr uint32_t LINUX_EPOLLET = 0x80000000;

#pragma pack(push, 1)
struct LinuxEpollEvent {
    uint32_t events;
    uint64_t data;
};
#pragma pack(pop)

static_assert(sizeof(LinuxEpollEvent) == 12);
static_assert(offsetof(LinuxEpollEvent, events) == 0);
static_assert(offsetof(LinuxEpollEvent, data) == 4);

} // namespace portage::poll
