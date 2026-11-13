#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "net/netlink_types.hpp"
#include "poll/epoll_manager.hpp"
#include "poll/poll_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace portage::fs;
using namespace portage::net;
using namespace portage::poll;
using namespace portage::syscall;

class NetlinkTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        portage::thread::ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override { SyscallDispatcher::Shutdown(); }

    portage::mem::AddressSpace as_;
    Vfs vfs_;
    FileDescriptorTable fd_table_;
};

TEST_F(NetlinkTest, NetlinkSocketLifecycleAndBinding)
{
    // sys_socket(AF_NETLINK = 16, SOCK_RAW = 3, NETLINK_ROUTE = 0)
    int64_t fd = SyscallDispatcher::Dispatch(41 /* SYS_socket */, LINUX_AF_NETLINK, 3,
                                             NETLINK_ROUTE, 0, 0, 0);
    ASSERT_GE(fd, 0);

    sockaddr_nl sa{};
    sa.nl_family = LINUX_AF_NETLINK;
    sa.nl_pid = 12345;
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;

    // sys_bind
    int64_t bind_res =
        SyscallDispatcher::Dispatch(49 /* SYS_bind */, static_cast<uint64_t>(fd),
                                    reinterpret_cast<uint64_t>(&sa), sizeof(sa), 0, 0, 0);
    EXPECT_EQ(bind_res, 0);

    // sys_getsockname
    sockaddr_nl bound_sa{};
    socklen_t addrlen = sizeof(bound_sa);
    int64_t gsn_res = SyscallDispatcher::Dispatch(
        51 /* SYS_getsockname */, static_cast<uint64_t>(fd), reinterpret_cast<uint64_t>(&bound_sa),
        reinterpret_cast<uint64_t>(&addrlen), 0, 0, 0);
    EXPECT_EQ(gsn_res, 0);
    EXPECT_EQ(bound_sa.nl_family, LINUX_AF_NETLINK);
    EXPECT_EQ(bound_sa.nl_pid, 12345u);
    EXPECT_EQ(bound_sa.nl_groups, (RTMGRP_LINK | RTMGRP_IPV4_IFADDR));

    // sys_close
    int64_t close_res =
        SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);
    EXPECT_EQ(close_res, 0);
}

TEST_F(NetlinkTest, RtmGetLinkDumpReturnsLoopbackAndAdapters)
{
    int64_t fd = SyscallDispatcher::Dispatch(41 /* SYS_socket */, LINUX_AF_NETLINK, 3,
                                             NETLINK_ROUTE, 0, 0, 0);
    ASSERT_GE(fd, 0);

    // Prepare RTM_GETLINK dump request
    struct Request {
        nlmsghdr nlh;
        ifinfomsg ifi;
    } req{};

    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 101;
    req.nlh.nlmsg_pid = 0;
    req.ifi.ifi_family = 0; // AF_UNSPEC

    int64_t send_res =
        SyscallDispatcher::Dispatch(44 /* SYS_sendto */, static_cast<uint64_t>(fd),
                                    reinterpret_cast<uint64_t>(&req), sizeof(req), 0, 0, 0);
    EXPECT_EQ(send_res, static_cast<int64_t>(sizeof(req)));

    // Read responses
    std::vector<uint8_t> recv_buf(16384, 0);
    bool found_loopback = false;
    bool saw_done = false;

    while (!saw_done) {
        sockaddr_nl src_addr{};
        socklen_t src_len = sizeof(src_addr);
        int64_t nread = SyscallDispatcher::Dispatch(
            45 /* SYS_recvfrom */, static_cast<uint64_t>(fd),
            reinterpret_cast<uint64_t>(recv_buf.data()), recv_buf.size(), 0,
            reinterpret_cast<uint64_t>(&src_addr), reinterpret_cast<uint64_t>(&src_len));
        ASSERT_GT(nread, 0);

        size_t offset = 0;
        while (offset + sizeof(nlmsghdr) <= static_cast<size_t>(nread)) {
            const auto *hdr = reinterpret_cast<const nlmsghdr *>(recv_buf.data() + offset);
            if (hdr->nlmsg_len < sizeof(nlmsghdr) ||
                offset + hdr->nlmsg_len > static_cast<size_t>(nread)) {
                break;
            }

            if (hdr->nlmsg_type == NLMSG_DONE) {
                saw_done = true;
                break;
            }

            if (hdr->nlmsg_type == RTM_NEWLINK) {
                const auto *ifi = reinterpret_cast<const ifinfomsg *>(NLMSG_DATA(hdr));
                if (ifi->ifi_index == 1) {
                    EXPECT_EQ(ifi->ifi_type, 772 /* ARPHRD_LOOPBACK */);
                    EXPECT_TRUE((ifi->ifi_flags & LINUX_IFF_LOOPBACK) != 0);
                    EXPECT_TRUE((ifi->ifi_flags & LINUX_IFF_UP) != 0);

                    // Parse attributes
                    size_t rta_offset = NLMSG_LENGTH(sizeof(ifinfomsg));
                    while (rta_offset + sizeof(rtattr) <= hdr->nlmsg_len) {
                        const auto *rta = reinterpret_cast<const rtattr *>(
                            reinterpret_cast<const uint8_t *>(hdr) + rta_offset);
                        if (rta->rta_len < sizeof(rtattr) ||
                            rta_offset + rta->rta_len > hdr->nlmsg_len) {
                            break;
                        }
                        if (rta->rta_type == IFLA_IFNAME) {
                            const char *ifname = reinterpret_cast<const char *>(RTA_DATA(rta));
                            EXPECT_STREQ(ifname, "lo");
                            found_loopback = true;
                        } else if (rta->rta_type == IFLA_MTU) {
                            uint32_t mtu = *reinterpret_cast<const uint32_t *>(RTA_DATA(rta));
                            EXPECT_EQ(mtu, 65536u);
                        }
                        rta_offset += RTA_ALIGN(rta->rta_len);
                    }
                }
            }

            offset += NLMSG_ALIGN(hdr->nlmsg_len);
        }
    }

    EXPECT_TRUE(found_loopback);
    EXPECT_TRUE(saw_done);

    SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);
}

TEST_F(NetlinkTest, RtmGetAddrDumpReturnsIpv4AndIpv6)
{
    int64_t fd = SyscallDispatcher::Dispatch(41 /* SYS_socket */, LINUX_AF_NETLINK, 3,
                                             NETLINK_ROUTE, 0, 0, 0);
    ASSERT_GE(fd, 0);

    struct Request {
        nlmsghdr nlh;
        ifaddrmsg ifa;
    } req{};

    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = RTM_GETADDR;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 102;
    req.nlh.nlmsg_pid = 0;
    req.ifa.ifa_family = 0;

    int64_t send_res =
        SyscallDispatcher::Dispatch(44 /* SYS_sendto */, static_cast<uint64_t>(fd),
                                    reinterpret_cast<uint64_t>(&req), sizeof(req), 0, 0, 0);
    EXPECT_EQ(send_res, static_cast<int64_t>(sizeof(req)));

    std::vector<uint8_t> recv_buf(16384, 0);
    bool found_lo_ipv4 = false;
    bool found_lo_ipv6 = false;
    bool saw_done = false;

    while (!saw_done) {
        int64_t nread = SyscallDispatcher::Dispatch(
            45 /* SYS_recvfrom */, static_cast<uint64_t>(fd),
            reinterpret_cast<uint64_t>(recv_buf.data()), recv_buf.size(), 0, 0, 0);
        ASSERT_GT(nread, 0);

        size_t offset = 0;
        while (offset + sizeof(nlmsghdr) <= static_cast<size_t>(nread)) {
            const auto *hdr = reinterpret_cast<const nlmsghdr *>(recv_buf.data() + offset);
            if (hdr->nlmsg_len < sizeof(nlmsghdr) ||
                offset + hdr->nlmsg_len > static_cast<size_t>(nread)) {
                break;
            }

            if (hdr->nlmsg_type == NLMSG_DONE) {
                saw_done = true;
                break;
            }

            if (hdr->nlmsg_type == RTM_NEWADDR) {
                const auto *ifa = reinterpret_cast<const ifaddrmsg *>(NLMSG_DATA(hdr));
                if (ifa->ifa_index == 1) {
                    if (ifa->ifa_family == LINUX_AF_INET) {
                        EXPECT_EQ(ifa->ifa_prefixlen, 8);
                        size_t rta_offset = NLMSG_LENGTH(sizeof(ifaddrmsg));
                        while (rta_offset + sizeof(rtattr) <= hdr->nlmsg_len) {
                            const auto *rta = reinterpret_cast<const rtattr *>(
                                reinterpret_cast<const uint8_t *>(hdr) + rta_offset);
                            if (rta->rta_len < sizeof(rtattr) ||
                                rta_offset + rta->rta_len > hdr->nlmsg_len) {
                                break;
                            }
                            if (rta->rta_type == IFA_ADDRESS) {
                                uint32_t ip = *reinterpret_cast<const uint32_t *>(RTA_DATA(rta));
                                EXPECT_EQ(ip, htonl(0x7F000001));
                                found_lo_ipv4 = true;
                            }
                            rta_offset += RTA_ALIGN(rta->rta_len);
                        }
                    } else if (ifa->ifa_family == LINUX_AF_INET6) {
                        EXPECT_EQ(ifa->ifa_prefixlen, 128);
                        size_t rta_offset = NLMSG_LENGTH(sizeof(ifaddrmsg));
                        while (rta_offset + sizeof(rtattr) <= hdr->nlmsg_len) {
                            const auto *rta = reinterpret_cast<const rtattr *>(
                                reinterpret_cast<const uint8_t *>(hdr) + rta_offset);
                            if (rta->rta_len < sizeof(rtattr) ||
                                rta_offset + rta->rta_len > hdr->nlmsg_len) {
                                break;
                            }
                            if (rta->rta_type == IFA_ADDRESS) {
                                const auto *b = reinterpret_cast<const uint8_t *>(RTA_DATA(rta));
                                EXPECT_EQ(b[15], 1);
                                found_lo_ipv6 = true;
                            }
                            rta_offset += RTA_ALIGN(rta->rta_len);
                        }
                    }
                }
            }

            offset += NLMSG_ALIGN(hdr->nlmsg_len);
        }
    }

    EXPECT_TRUE(found_lo_ipv4);
    EXPECT_TRUE(found_lo_ipv6);
    EXPECT_TRUE(saw_done);

    SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);
}

TEST_F(NetlinkTest, RtmGetRouteDumpReturnsDefaultRoutes)
{
    int64_t fd = SyscallDispatcher::Dispatch(41 /* SYS_socket */, LINUX_AF_NETLINK, 3,
                                             NETLINK_ROUTE, 0, 0, 0);
    ASSERT_GE(fd, 0);

    struct Request {
        nlmsghdr nlh;
        rtmsg rtm;
    } req{};

    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 103;
    req.nlh.nlmsg_pid = 0;
    req.rtm.rtm_family = 0;

    int64_t send_res =
        SyscallDispatcher::Dispatch(44 /* SYS_sendto */, static_cast<uint64_t>(fd),
                                    reinterpret_cast<uint64_t>(&req), sizeof(req), 0, 0, 0);
    EXPECT_EQ(send_res, static_cast<int64_t>(sizeof(req)));

    std::vector<uint8_t> recv_buf(16384, 0);
    bool found_lo_route = false;
    bool saw_done = false;

    while (!saw_done) {
        int64_t nread = SyscallDispatcher::Dispatch(
            45 /* SYS_recvfrom */, static_cast<uint64_t>(fd),
            reinterpret_cast<uint64_t>(recv_buf.data()), recv_buf.size(), 0, 0, 0);
        ASSERT_GT(nread, 0);

        size_t offset = 0;
        while (offset + sizeof(nlmsghdr) <= static_cast<size_t>(nread)) {
            const auto *hdr = reinterpret_cast<const nlmsghdr *>(recv_buf.data() + offset);
            if (hdr->nlmsg_len < sizeof(nlmsghdr) ||
                offset + hdr->nlmsg_len > static_cast<size_t>(nread)) {
                break;
            }

            if (hdr->nlmsg_type == NLMSG_DONE) {
                saw_done = true;
                break;
            }

            if (hdr->nlmsg_type == RTM_NEWROUTE) {
                const auto *rtm = reinterpret_cast<const rtmsg *>(NLMSG_DATA(hdr));
                if (rtm->rtm_family == LINUX_AF_INET && rtm->rtm_dst_len == 8) {
                    size_t rta_offset = NLMSG_LENGTH(sizeof(rtmsg));
                    bool matches_lo_dst = false;
                    bool matches_lo_oif = false;

                    while (rta_offset + sizeof(rtattr) <= hdr->nlmsg_len) {
                        const auto *rta = reinterpret_cast<const rtattr *>(
                            reinterpret_cast<const uint8_t *>(hdr) + rta_offset);
                        if (rta->rta_len < sizeof(rtattr) ||
                            rta_offset + rta->rta_len > hdr->nlmsg_len) {
                            break;
                        }
                        if (rta->rta_type == RTA_DST) {
                            uint32_t dst = *reinterpret_cast<const uint32_t *>(RTA_DATA(rta));
                            if (dst == htonl(0x7F000000)) {
                                matches_lo_dst = true;
                            }
                        } else if (rta->rta_type == RTA_OIF) {
                            uint32_t oif = *reinterpret_cast<const uint32_t *>(RTA_DATA(rta));
                            if (oif == 1) {
                                matches_lo_oif = true;
                            }
                        }
                        rta_offset += RTA_ALIGN(rta->rta_len);
                    }

                    if (matches_lo_dst && matches_lo_oif) {
                        found_lo_route = true;
                    }
                }
            }

            offset += NLMSG_ALIGN(hdr->nlmsg_len);
        }
    }

    EXPECT_TRUE(found_lo_route);
    EXPECT_TRUE(saw_done);

    SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(fd), 0, 0, 0, 0, 0);
}

TEST_F(NetlinkTest, EpollReadinessNotification)
{
    // Create epoll instance
    int64_t epfd = SyscallDispatcher::Dispatch(291 /* SYS_epoll_create1 */, 0, 0, 0, 0, 0, 0);
    ASSERT_GE(epfd, 0);

    // Create netlink socket
    int64_t nlfd = SyscallDispatcher::Dispatch(41 /* SYS_socket */, LINUX_AF_NETLINK, 3,
                                               NETLINK_ROUTE, 0, 0, 0);
    ASSERT_GE(nlfd, 0);

    // Register netlink socket in epoll
    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = static_cast<uint64_t>(nlfd);

    int64_t ctl_res = SyscallDispatcher::Dispatch(
        233 /* SYS_epoll_ctl */, static_cast<uint64_t>(epfd), 1 /* EPOLL_CTL_ADD */,
        static_cast<uint64_t>(nlfd), reinterpret_cast<uint64_t>(&ev), 0, 0);
    EXPECT_EQ(ctl_res, 0);

    // Non-blocking wait before sending request: should return 0 (not ready)
    LinuxEpollEvent ready_events[4]{};
    int64_t wait_empty = SyscallDispatcher::Dispatch(
        232 /* SYS_epoll_wait */, static_cast<uint64_t>(epfd),
        reinterpret_cast<uint64_t>(ready_events), 4, 0 /* 0 timeout */, 0, 0);
    EXPECT_EQ(wait_empty, 0);

    // Send RTM_GETLINK request to populate response packets
    struct Request {
        nlmsghdr nlh;
        ifinfomsg ifi;
    } req{};
    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 200;

    int64_t send_res =
        SyscallDispatcher::Dispatch(44 /* SYS_sendto */, static_cast<uint64_t>(nlfd),
                                    reinterpret_cast<uint64_t>(&req), sizeof(req), 0, 0, 0);
    EXPECT_EQ(send_res, static_cast<int64_t>(sizeof(req)));

    // Now epoll_wait should wake up with EPOLLIN
    int64_t wait_ready = SyscallDispatcher::Dispatch(
        232 /* SYS_epoll_wait */, static_cast<uint64_t>(epfd),
        reinterpret_cast<uint64_t>(ready_events), 4, 1000 /* 1000ms */, 0, 0);
    EXPECT_GE(wait_ready, 1);
    EXPECT_EQ(ready_events[0].data, static_cast<uint64_t>(nlfd));
    EXPECT_TRUE((ready_events[0].events & LINUX_EPOLLIN) != 0);

    // Drain all packets
    std::vector<uint8_t> drain_buf(16384, 0);
    bool saw_done = false;
    while (!saw_done) {
        int64_t n = SyscallDispatcher::Dispatch(45 /* SYS_recvfrom */, static_cast<uint64_t>(nlfd),
                                                reinterpret_cast<uint64_t>(drain_buf.data()),
                                                drain_buf.size(), 0, 0, 0);
        if (n <= 0)
            break;

        size_t off = 0;
        while (off + sizeof(nlmsghdr) <= static_cast<size_t>(n)) {
            const auto *h = reinterpret_cast<const nlmsghdr *>(drain_buf.data() + off);
            if (h->nlmsg_len < sizeof(nlmsghdr))
                break;
            if (h->nlmsg_type == NLMSG_DONE) {
                saw_done = true;
                break;
            }
            off += NLMSG_ALIGN(h->nlmsg_len);
        }
    }
    EXPECT_TRUE(saw_done);

    // After draining, epoll_wait should again report 0 events
    int64_t wait_drained =
        SyscallDispatcher::Dispatch(232 /* SYS_epoll_wait */, static_cast<uint64_t>(epfd),
                                    reinterpret_cast<uint64_t>(ready_events), 4, 0, 0, 0);
    EXPECT_EQ(wait_drained, 0);

    SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(nlfd), 0, 0, 0, 0, 0);
    SyscallDispatcher::Dispatch(3 /* SYS_close */, static_cast<uint64_t>(epfd), 0, 0, 0, 0, 0);
}

TEST_F(NetlinkTest, BionicGetifaddrsAndIfNameindex)
{
    // Test portage_getifaddrs
    struct ifaddrs *ifap = nullptr;
    int res = portage_getifaddrs(&ifap);
    EXPECT_EQ(res, 0);
    ASSERT_NE(ifap, nullptr);

    bool found_lo = false;
    for (struct ifaddrs *curr = ifap; curr != nullptr; curr = curr->ifa_next) {
        if (curr->ifa_name && std::strcmp(curr->ifa_name, "lo") == 0) {
            found_lo = true;
            EXPECT_TRUE((curr->ifa_flags & LINUX_IFF_LOOPBACK) != 0);
            EXPECT_TRUE((curr->ifa_flags & LINUX_IFF_UP) != 0);
            EXPECT_NE(curr->ifa_addr, nullptr);
            EXPECT_NE(curr->ifa_netmask, nullptr);
        }
    }
    EXPECT_TRUE(found_lo);

    // Test portage_freeifaddrs
    portage_freeifaddrs(ifap);

    // Test if_nametoindex and if_indextoname
    unsigned int idx = portage_if_nametoindex("lo");
    EXPECT_EQ(idx, 1u);

    char name_buf[16]{};
    char *name_res = portage_if_indextoname(1, name_buf);
    EXPECT_EQ(name_res, name_buf);
    EXPECT_STREQ(name_buf, "lo");

    // Test portage_if_nameindex
    struct if_nameindex *idx_list = portage_if_nameindex();
    ASSERT_NE(idx_list, nullptr);
    EXPECT_EQ(idx_list[0].if_index, 1u);
    ASSERT_NE(idx_list[0].if_name, nullptr);
    EXPECT_STREQ(idx_list[0].if_name, "lo");

    // Free if_nameindex
    portage_if_freenameindex(idx_list);
}
