#include "fs/fd_table.hpp"
#include "net/socket_manager.hpp"
#include "net/socket_types.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <thread>

using namespace portage::net;
using namespace portage::fs;

TEST(SocketManagerTest, TranslatesIPv4AddressBidirectionally)
{
    LinuxSockaddrIn lin{};
    lin.sin_family = LINUX_AF_INET;
    lin.sin_port = htons(9090);
    lin.sin_addr.addr = htonl(0x7F000001); // 127.0.0.1

    SOCKADDR_STORAGE win_storage{};
    int win_len = 0;
    bool ok = SocketManager::LinuxToWinAddr(reinterpret_cast<const LinuxSockaddr *>(&lin),
                                            sizeof(lin), &win_storage, &win_len);
    EXPECT_TRUE(ok);
    EXPECT_EQ(win_storage.ss_family, AF_INET);
    EXPECT_EQ(win_len, sizeof(sockaddr_in));

    const auto *win = reinterpret_cast<const sockaddr_in *>(&win_storage);
    EXPECT_EQ(win->sin_port, htons(9090));
    EXPECT_EQ(win->sin_addr.S_un.S_addr, htonl(0x7F000001));

    LinuxSockaddrIn back_lin{};
    socklen_t back_len = sizeof(back_lin);
    ok = SocketManager::WinToLinuxAddr(&win_storage, win_len,
                                       reinterpret_cast<LinuxSockaddr *>(&back_lin), &back_len);
    EXPECT_TRUE(ok);
    EXPECT_EQ(back_lin.sin_family, LINUX_AF_INET);
    EXPECT_EQ(back_lin.sin_port, htons(9090));
    EXPECT_EQ(back_lin.sin_addr.addr, htonl(0x7F000001));
}

TEST(SocketManagerTest, TranslatesWsaErrorsToLinuxErrno)
{
    EXPECT_EQ(SocketManager::TranslateWsaError(WSAEWOULDBLOCK), -11);   // -EAGAIN
    EXPECT_EQ(SocketManager::TranslateWsaError(WSAECONNREFUSED), -111); // -ECONNREFUSED
    EXPECT_EQ(SocketManager::TranslateWsaError(WSAETIMEDOUT), -110);    // -ETIMEDOUT
    EXPECT_EQ(SocketManager::TranslateWsaError(WSAEADDRINUSE), -98);    // -EADDRINUSE
    EXPECT_EQ(SocketManager::TranslateWsaError(WSAEINTR), -4);          // -EINTR
}

TEST(SocketManagerTest, EstablishesTcpLoopbackAndTransfersData)
{
    auto &sm = SocketManager::Instance();
    EXPECT_TRUE(sm.Initialize());

    int srv_flags = 0;
    SOCKET srv = sm.CreateSocket(LINUX_AF_INET, LINUX_SOCK_STREAM, 0, srv_flags);
    ASSERT_NE(srv, INVALID_SOCKET);

    LinuxSockaddrIn srv_addr{};
    srv_addr.sin_family = LINUX_AF_INET;
    srv_addr.sin_port = 0;                      // Ephemeral port
    srv_addr.sin_addr.addr = htonl(0x7F000001); // 127.0.0.1

    EXPECT_EQ(sm.Bind(srv, reinterpret_cast<const LinuxSockaddr *>(&srv_addr), sizeof(srv_addr)),
              0);
    EXPECT_EQ(sm.Listen(srv, 5), 0);

    LinuxSockaddrIn bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    EXPECT_EQ(sm.GetSockName(srv, reinterpret_cast<LinuxSockaddr *>(&bound_addr), &bound_len), 0);
    EXPECT_NE(bound_addr.sin_port, 0);

    // Connect client
    int cli_flags = 0;
    SOCKET cli = sm.CreateSocket(LINUX_AF_INET, LINUX_SOCK_STREAM, 0, cli_flags);
    ASSERT_NE(cli, INVALID_SOCKET);

    EXPECT_EQ(
        sm.Connect(cli, reinterpret_cast<const LinuxSockaddr *>(&bound_addr), sizeof(bound_addr)),
        0);

    // Accept on server
    LinuxSockaddrIn peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    SOCKET accepted = sm.Accept(srv, reinterpret_cast<LinuxSockaddr *>(&peer_addr), &peer_len, 0);
    ASSERT_NE(accepted, INVALID_SOCKET);

    // Transfer message
    const char msg[] = "ping from client";
    int64_t sent = sm.SendTo(cli, msg, sizeof(msg), 0, nullptr, 0);
    EXPECT_EQ(sent, static_cast<int64_t>(sizeof(msg)));

    char recv_buf[64] = {0};
    int64_t recvd = sm.RecvFrom(accepted, recv_buf, sizeof(recv_buf), 0, nullptr, nullptr);
    EXPECT_EQ(recvd, static_cast<int64_t>(sizeof(msg)));
    EXPECT_STREQ(recv_buf, msg);

    sm.ShutdownSocket(cli, LINUX_SHUT_RDWR);
    closesocket(cli);
    closesocket(accepted);
    closesocket(srv);
}
