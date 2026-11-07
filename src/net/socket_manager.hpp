#pragma once

#include "socket_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <afunix.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace portage::net {

class SocketManager {
  public:
    static SocketManager &Instance() noexcept;

    bool Initialize() noexcept;
    void Shutdown() noexcept;

    SOCKET CreateSocket(int domain, int type, int protocol, int &out_flags);

    int64_t Bind(SOCKET s, const LinuxSockaddr *addr, socklen_t len);
    int64_t Connect(SOCKET s, const LinuxSockaddr *addr, socklen_t len);
    int64_t Listen(SOCKET s, int backlog);
    SOCKET Accept(SOCKET s, LinuxSockaddr *addr, socklen_t *len, int flags);

    int64_t SendTo(SOCKET s, const void *buf, size_t len, int flags, const LinuxSockaddr *addr,
                   socklen_t addrlen);
    int64_t RecvFrom(SOCKET s, void *buf, size_t len, int flags, LinuxSockaddr *addr,
                     socklen_t *addrlen);

    int64_t GetSockName(SOCKET s, LinuxSockaddr *addr, socklen_t *len);
    int64_t GetPeerName(SOCKET s, LinuxSockaddr *addr, socklen_t *len);

    int64_t GetSockOpt(SOCKET s, int level, int optname, void *optval, socklen_t *optlen);
    int64_t SetSockOpt(SOCKET s, int level, int optname, const void *optval, socklen_t optlen);

    int64_t ShutdownSocket(SOCKET s, int how);

    static bool LinuxToWinAddr(const LinuxSockaddr *laddr, socklen_t llen, SOCKADDR_STORAGE *waddr,
                               int *wlen) noexcept;
    static bool WinToLinuxAddr(const SOCKADDR_STORAGE *waddr, int wlen, LinuxSockaddr *laddr,
                               socklen_t *llen) noexcept;
    static bool LinuxToWinSockOpt(int llevel, int lopt, int *wlevel, int *wopt) noexcept;
    static int64_t TranslateWsaError(int err) noexcept;

  private:
    SocketManager();
    ~SocketManager();

    std::mutex m_mutex{};
    bool m_initialized{false};
};

} // namespace portage::net
