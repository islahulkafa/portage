#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::net {

constexpr int LINUX_AF_UNSPEC = 0;
constexpr int LINUX_AF_UNIX = 1;
constexpr int LINUX_AF_LOCAL = 1;
constexpr int LINUX_AF_INET = 2;
constexpr int LINUX_AF_INET6 = 10;
constexpr int LINUX_AF_NETLINK = 16;

constexpr int LINUX_SOCK_STREAM = 1;
constexpr int LINUX_SOCK_DGRAM = 2;
constexpr int LINUX_SOCK_RAW = 3;
constexpr int LINUX_SOCK_NONBLOCK = 00004000;
constexpr int LINUX_SOCK_CLOEXEC = 02000000;

constexpr int LINUX_SOL_SOCKET = 1;
constexpr int LINUX_SO_DEBUG = 1;
constexpr int LINUX_SO_REUSEADDR = 2;
constexpr int LINUX_SO_TYPE = 3;
constexpr int LINUX_SO_ERROR = 4;
constexpr int LINUX_SO_DONTROUTE = 5;
constexpr int LINUX_SO_BROADCAST = 6;
constexpr int LINUX_SO_SNDBUF = 7;
constexpr int LINUX_SO_RCVBUF = 8;
constexpr int LINUX_SO_KEEPALIVE = 9;
constexpr int LINUX_SO_OOBINLINE = 10;
constexpr int LINUX_SO_LINGER = 13;
constexpr int LINUX_SO_REUSEPORT = 15;
constexpr int LINUX_SO_RCVTIMEO = 20;
constexpr int LINUX_SO_SNDTIMEO = 21;

constexpr int LINUX_IPPROTO_IP = 0;
constexpr int LINUX_IPPROTO_TCP = 6;
constexpr int LINUX_IPPROTO_UDP = 17;
constexpr int LINUX_TCP_NODELAY = 1;

constexpr int LINUX_SHUT_RD = 0;
constexpr int LINUX_SHUT_WR = 1;
constexpr int LINUX_SHUT_RDWR = 2;

struct LinuxSockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct LinuxInAddr {
    uint32_t addr;
};

struct LinuxSockaddrIn {
    uint16_t sin_family;
    uint16_t sin_port;
    LinuxInAddr sin_addr;
    uint8_t sin_zero[8];
};

struct LinuxIn6Addr {
    uint8_t s6_addr[16];
};

struct LinuxSockaddrIn6 {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    LinuxIn6Addr sin6_addr;
    uint32_t sin6_scope_id;
};

struct LinuxSockaddrUn {
    uint16_t sun_family;
    char sun_path[108];
};

} // namespace portage::net
