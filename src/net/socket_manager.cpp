#include "net/socket_manager.hpp"

#include <algorithm>
#include <cstring>

namespace portage::net {

SocketManager::SocketManager() { Initialize(); }

SocketManager::~SocketManager() { Shutdown(); }

SocketManager &SocketManager::Instance() noexcept
{
    static SocketManager instance;
    return instance;
}

bool SocketManager::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }
    WSADATA wsa_data{};
    int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (res == 0) {
        m_initialized = true;
        return true;
    }
    return false;
}

void SocketManager::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        WSACleanup();
        m_initialized = false;
    }
}

SOCKET SocketManager::CreateSocket(int domain, int type, int protocol, int &out_flags)
{
    if (!Initialize()) {
        return INVALID_SOCKET;
    }

    out_flags = type & (LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC);
    int real_type = type & ~(LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC);

    int win_domain = AF_UNSPEC;
    if (domain == LINUX_AF_INET) {
        win_domain = AF_INET;
    } else if (domain == LINUX_AF_INET6) {
        win_domain = AF_INET6;
    } else if (domain == LINUX_AF_UNIX) {
        win_domain = AF_UNIX;
    } else {
        return INVALID_SOCKET;
    }

    int win_type = 0;
    if (real_type == LINUX_SOCK_STREAM) {
        win_type = SOCK_STREAM;
    } else if (real_type == LINUX_SOCK_DGRAM) {
        win_type = SOCK_DGRAM;
    } else if (real_type == LINUX_SOCK_RAW) {
        win_type = SOCK_RAW;
    } else {
        return INVALID_SOCKET;
    }

    int win_proto = 0;
    if (protocol == LINUX_IPPROTO_TCP) {
        win_proto = IPPROTO_TCP;
    } else if (protocol == LINUX_IPPROTO_UDP) {
        win_proto = IPPROTO_UDP;
    }

    SOCKET s = ::socket(win_domain, win_type, win_proto);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (out_flags & LINUX_SOCK_NONBLOCK) {
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);
    }

    return s;
}

bool SocketManager::LinuxToWinAddr(const LinuxSockaddr *laddr, socklen_t llen,
                                   SOCKADDR_STORAGE *waddr, int *wlen) noexcept
{
    if (!laddr || !waddr || !wlen || static_cast<size_t>(llen) < sizeof(uint16_t)) {
        return false;
    }

    std::memset(waddr, 0, sizeof(SOCKADDR_STORAGE));

    if (laddr->sa_family == LINUX_AF_INET) {
        if (static_cast<size_t>(llen) < sizeof(LinuxSockaddrIn)) {
            return false;
        }
        const auto *lin = reinterpret_cast<const LinuxSockaddrIn *>(laddr);
        auto *win = reinterpret_cast<sockaddr_in *>(waddr);
        win->sin_family = AF_INET;
        win->sin_port = lin->sin_port;
        win->sin_addr.S_un.S_addr = lin->sin_addr.addr;
        *wlen = sizeof(sockaddr_in);
        return true;
    }

    if (laddr->sa_family == LINUX_AF_INET6) {
        if (static_cast<size_t>(llen) < sizeof(LinuxSockaddrIn6)) {
            return false;
        }
        const auto *lin6 = reinterpret_cast<const LinuxSockaddrIn6 *>(laddr);
        auto *win6 = reinterpret_cast<sockaddr_in6 *>(waddr);
        win6->sin6_family = AF_INET6;
        win6->sin6_port = lin6->sin6_port;
        win6->sin6_flowinfo = lin6->sin6_flowinfo;
        std::memcpy(&win6->sin6_addr, &lin6->sin6_addr, 16);
        win6->sin6_scope_id = lin6->sin6_scope_id;
        *wlen = sizeof(sockaddr_in6);
        return true;
    }

    if (laddr->sa_family == LINUX_AF_UNIX) {
        const auto *lun = reinterpret_cast<const LinuxSockaddrUn *>(laddr);
        auto *wun = reinterpret_cast<sockaddr_un *>(waddr);
        wun->sun_family = AF_UNIX;
        std::strncpy(wun->sun_path, lun->sun_path, sizeof(wun->sun_path) - 1);
        *wlen = sizeof(sockaddr_un);
        return true;
    }

    return false;
}

bool SocketManager::WinToLinuxAddr(const SOCKADDR_STORAGE *waddr, int wlen, LinuxSockaddr *laddr,
                                   socklen_t *llen) noexcept
{
    (void)wlen;
    if (!waddr || !laddr || !llen || static_cast<size_t>(*llen) < sizeof(uint16_t)) {
        return false;
    }

    if (waddr->ss_family == AF_INET) {
        if (static_cast<size_t>(*llen) < sizeof(LinuxSockaddrIn)) {
            return false;
        }
        const auto *win = reinterpret_cast<const sockaddr_in *>(waddr);
        auto *lin = reinterpret_cast<LinuxSockaddrIn *>(laddr);
        lin->sin_family = LINUX_AF_INET;
        lin->sin_port = win->sin_port;
        lin->sin_addr.addr = win->sin_addr.S_un.S_addr;
        std::memset(lin->sin_zero, 0, sizeof(lin->sin_zero));
        *llen = sizeof(LinuxSockaddrIn);
        return true;
    }

    if (waddr->ss_family == AF_INET6) {
        if (static_cast<size_t>(*llen) < sizeof(LinuxSockaddrIn6)) {
            return false;
        }
        const auto *win6 = reinterpret_cast<const sockaddr_in6 *>(waddr);
        auto *lin6 = reinterpret_cast<LinuxSockaddrIn6 *>(laddr);
        lin6->sin6_family = LINUX_AF_INET6;
        lin6->sin6_port = win6->sin6_port;
        lin6->sin6_flowinfo = win6->sin6_flowinfo;
        std::memcpy(&lin6->sin6_addr, &win6->sin6_addr, 16);
        lin6->sin6_scope_id = win6->sin6_scope_id;
        *llen = sizeof(LinuxSockaddrIn6);
        return true;
    }

    if (waddr->ss_family == AF_UNIX) {
        if (static_cast<size_t>(*llen) < sizeof(LinuxSockaddrUn)) {
            return false;
        }
        const auto *wun = reinterpret_cast<const sockaddr_un *>(waddr);
        auto *lun = reinterpret_cast<LinuxSockaddrUn *>(laddr);
        lun->sun_family = LINUX_AF_UNIX;
        std::strncpy(lun->sun_path, wun->sun_path, sizeof(lun->sun_path) - 1);
        *llen = sizeof(LinuxSockaddrUn);
        return true;
    }

    return false;
}

bool SocketManager::LinuxToWinSockOpt(int llevel, int lopt, int *wlevel, int *wopt) noexcept
{
    if (!wlevel || !wopt) {
        return false;
    }

    if (llevel == LINUX_SOL_SOCKET) {
        *wlevel = SOL_SOCKET;
        switch (lopt) {
        case LINUX_SO_REUSEADDR:
            *wopt = SO_REUSEADDR;
            return true;
        case LINUX_SO_ERROR:
            *wopt = SO_ERROR;
            return true;
        case LINUX_SO_BROADCAST:
            *wopt = SO_BROADCAST;
            return true;
        case LINUX_SO_SNDBUF:
            *wopt = SO_SNDBUF;
            return true;
        case LINUX_SO_RCVBUF:
            *wopt = SO_RCVBUF;
            return true;
        case LINUX_SO_KEEPALIVE:
            *wopt = SO_KEEPALIVE;
            return true;
        case LINUX_SO_OOBINLINE:
            *wopt = SO_OOBINLINE;
            return true;
        case LINUX_SO_LINGER:
            *wopt = SO_LINGER;
            return true;
        case LINUX_SO_RCVTIMEO:
            *wopt = SO_RCVTIMEO;
            return true;
        case LINUX_SO_SNDTIMEO:
            *wopt = SO_SNDTIMEO;
            return true;
        case LINUX_SO_DONTROUTE:
            *wopt = SO_DONTROUTE;
            return true;
        case LINUX_SO_TYPE:
            *wopt = SO_TYPE;
            return true;
        default:
            break;
        }
    } else if (llevel == LINUX_IPPROTO_TCP) {
        *wlevel = IPPROTO_TCP;
        if (lopt == LINUX_TCP_NODELAY) {
            *wopt = TCP_NODELAY;
            return true;
        }
    }

    return false;
}

int64_t SocketManager::TranslateWsaError(int err) noexcept
{
    switch (err) {
    case 0:
        return 0;
    case WSAEINTR:
        return -4; // -EINTR
    case WSAEWOULDBLOCK:
        return -11; // -EAGAIN / -EWOULDBLOCK
    case WSAEINPROGRESS:
        return -115; // -EINPROGRESS
    case WSAEALREADY:
        return -114; // -EALREADY
    case WSAENOTSOCK:
        return -88; // -ENOTSOCK
    case WSAEDESTADDRREQ:
        return -89; // -EDESTADDRREQ
    case WSAEMSGSIZE:
        return -90; // -EMSGSIZE
    case WSAEPROTOTYPE:
        return -91; // -EPROTOTYPE
    case WSAENOPROTOOPT:
        return -92; // -ENOPROTOOPT
    case WSAEPROTONOSUPPORT:
        return -93; // -EPROTONOSUPPORT
    case WSAEAFNOSUPPORT:
        return -97; // -EAFNOSUPPORT
    case WSAEADDRINUSE:
        return -98; // -EADDRINUSE
    case WSAEADDRNOTAVAIL:
        return -99; // -EADDRNOTAVAIL
    case WSAENETDOWN:
        return -100; // -ENETDOWN
    case WSAENETUNREACH:
        return -101; // -ENETUNREACH
    case WSAENETRESET:
        return -102; // -ENETRESET
    case WSAECONNABORTED:
        return -103; // -ECONNABORTED
    case WSAECONNRESET:
        return -104; // -ECONNRESET
    case WSAENOBUFS:
        return -105; // -ENOBUFS
    case WSAEISCONN:
        return -106; // -EISCONN
    case WSAENOTCONN:
        return -107; // -ENOTCONN
    case WSAESHUTDOWN:
        return -108; // -ESHUTDOWN
    case WSAETIMEDOUT:
        return -110; // -ETIMEDOUT
    case WSAECONNREFUSED:
        return -111; // -ECONNREFUSED
    case WSAEHOSTUNREACH:
        return -113; // -EHOSTUNREACH
    case WSAEACCES:
        return -13; // -EACCES
    case WSAEFAULT:
        return -14; // -EFAULT
    case WSAEINVAL:
        return -22; // -EINVAL
    case WSAEMFILE:
        return -24; // -EMFILE
    default:
        return -5; // -EIO
    }
}

int64_t SocketManager::Bind(SOCKET s, const LinuxSockaddr *addr, socklen_t len)
{
    SOCKADDR_STORAGE waddr{};
    int wlen = 0;
    if (!LinuxToWinAddr(addr, len, &waddr, &wlen)) {
        return -22; // -EINVAL
    }
    if (::bind(s, reinterpret_cast<const sockaddr *>(&waddr), wlen) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return 0;
}

int64_t SocketManager::Connect(SOCKET s, const LinuxSockaddr *addr, socklen_t len)
{
    SOCKADDR_STORAGE waddr{};
    int wlen = 0;
    if (!LinuxToWinAddr(addr, len, &waddr, &wlen)) {
        return -22; // -EINVAL
    }
    if (::connect(s, reinterpret_cast<const sockaddr *>(&waddr), wlen) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return 0;
}

int64_t SocketManager::Listen(SOCKET s, int backlog)
{
    if (::listen(s, backlog) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return 0;
}

SOCKET SocketManager::Accept(SOCKET s, LinuxSockaddr *addr, socklen_t *len, int flags)
{
    SOCKADDR_STORAGE waddr{};
    int wlen = sizeof(waddr);
    SOCKET client = ::accept(s, reinterpret_cast<sockaddr *>(&waddr), &wlen);
    if (client == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (addr && len) {
        WinToLinuxAddr(&waddr, wlen, addr, len);
    }

    if (flags & LINUX_SOCK_NONBLOCK) {
        u_long mode = 1;
        ioctlsocket(client, FIONBIO, &mode);
    }

    return client;
}

int64_t SocketManager::SendTo(SOCKET s, const void *buf, size_t len, int flags,
                              const LinuxSockaddr *addr, socklen_t addrlen)
{
    const char *data = reinterpret_cast<const char *>(buf);
    int to_send = static_cast<int>(std::min(len, static_cast<size_t>(0x7FFFFFFF)));

    int res = 0;
    if (addr && addrlen > 0) {
        SOCKADDR_STORAGE waddr{};
        int wlen = 0;
        if (!LinuxToWinAddr(addr, addrlen, &waddr, &wlen)) {
            return -22; // -EINVAL
        }
        res = ::sendto(s, data, to_send, flags, reinterpret_cast<const sockaddr *>(&waddr), wlen);
    } else {
        res = ::send(s, data, to_send, flags);
    }

    if (res == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return static_cast<int64_t>(res);
}

int64_t SocketManager::RecvFrom(SOCKET s, void *buf, size_t len, int flags, LinuxSockaddr *addr,
                                socklen_t *addrlen)
{
    char *data = reinterpret_cast<char *>(buf);
    int to_recv = static_cast<int>(std::min(len, static_cast<size_t>(0x7FFFFFFF)));

    int res = 0;
    if (addr && addrlen) {
        SOCKADDR_STORAGE waddr{};
        int wlen = sizeof(waddr);
        res = ::recvfrom(s, data, to_recv, flags, reinterpret_cast<sockaddr *>(&waddr), &wlen);
        if (res != SOCKET_ERROR) {
            WinToLinuxAddr(&waddr, wlen, addr, addrlen);
        }
    } else {
        res = ::recv(s, data, to_recv, flags);
    }

    if (res == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return static_cast<int64_t>(res);
}

int64_t SocketManager::GetSockName(SOCKET s, LinuxSockaddr *addr, socklen_t *len)
{
    SOCKADDR_STORAGE waddr{};
    int wlen = sizeof(waddr);
    if (::getsockname(s, reinterpret_cast<sockaddr *>(&waddr), &wlen) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    if (!WinToLinuxAddr(&waddr, wlen, addr, len)) {
        return -22; // -EINVAL
    }
    return 0;
}

int64_t SocketManager::GetPeerName(SOCKET s, LinuxSockaddr *addr, socklen_t *len)
{
    SOCKADDR_STORAGE waddr{};
    int wlen = sizeof(waddr);
    if (::getpeername(s, reinterpret_cast<sockaddr *>(&waddr), &wlen) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    if (!WinToLinuxAddr(&waddr, wlen, addr, len)) {
        return -22; // -EINVAL
    }
    return 0;
}

int64_t SocketManager::GetSockOpt(SOCKET s, int level, int optname, void *optval, socklen_t *optlen)
{
    int wlevel = 0;
    int wopt = 0;
    if (!LinuxToWinSockOpt(level, optname, &wlevel, &wopt)) {
        return -92; // -ENOPROTOOPT
    }
    int wlen = static_cast<int>(*optlen);
    if (::getsockopt(s, wlevel, wopt, reinterpret_cast<char *>(optval), &wlen) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    *optlen = static_cast<socklen_t>(wlen);
    return 0;
}

int64_t SocketManager::SetSockOpt(SOCKET s, int level, int optname, const void *optval,
                                  socklen_t optlen)
{
    int wlevel = 0;
    int wopt = 0;
    if (!LinuxToWinSockOpt(level, optname, &wlevel, &wopt)) {
        return -92; // -ENOPROTOOPT
    }
    if (::setsockopt(s, wlevel, wopt, reinterpret_cast<const char *>(optval),
                     static_cast<int>(optlen)) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return 0;
}

int64_t SocketManager::ShutdownSocket(SOCKET s, int how)
{
    int win_how = SD_BOTH;
    if (how == LINUX_SHUT_RD) {
        win_how = SD_RECEIVE;
    } else if (how == LINUX_SHUT_WR) {
        win_how = SD_SEND;
    } else if (how == LINUX_SHUT_RDWR) {
        win_how = SD_BOTH;
    } else {
        return -22; // -EINVAL
    }
    if (::shutdown(s, win_how) == SOCKET_ERROR) {
        return TranslateWsaError(WSAGetLastError());
    }
    return 0;
}

} // namespace portage::net
