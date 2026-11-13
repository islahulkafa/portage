#include "net/netlink_socket.hpp"
#include "net/netlink_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace portage::net {

namespace {

constexpr int LINUX_ARPHRD_ETHER = 1;
constexpr int LINUX_ARPHRD_LOOPBACK = 772;

inline bool IsIpv6LinkLocal(const void *addr_bytes)
{
    const auto *b = reinterpret_cast<const uint8_t *>(addr_bytes);
    return (b[0] == 0xfe) && ((b[1] & 0xc0) == 0x80);
}

inline bool IsIpv6Unspecified(const void *addr_bytes)
{
    const auto *b = reinterpret_cast<const uint8_t *>(addr_bytes);
    for (int i = 0; i < 16; ++i) {
        if (b[i] != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

NetlinkSocketState::NetlinkSocketState()
{
    wait_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

NetlinkSocketState::~NetlinkSocketState()
{
    if (wait_event_ != INVALID_HANDLE_VALUE && wait_event_ != nullptr) {
        CloseHandle(wait_event_);
        wait_event_ = INVALID_HANDLE_VALUE;
    }
}

int NetlinkSocketState::Bind(const sockaddr_nl *addr)
{
    std::lock_guard lock(mutex_);
    if (addr == nullptr) {
        return -22; // -EINVAL
    }
    local_addr_ = *addr;
    if (local_addr_.nl_pid == 0) {
        local_addr_.nl_pid = GetCurrentProcessId();
    }
    bound_ = true;
    return 0;
}

int NetlinkSocketState::GetSockName(sockaddr_nl *addr) const
{
    std::lock_guard lock(mutex_);
    if (addr == nullptr) {
        return -22; // -EINVAL
    }
    *addr = local_addr_;
    return 0;
}

bool NetlinkSocketState::HasData() const noexcept
{
    std::lock_guard lock(mutex_);
    return rx_offset_ < rx_buffer_.size();
}

HANDLE NetlinkSocketState::GetWaitEvent() const noexcept { return wait_event_; }

void NetlinkSocketState::Reset()
{
    std::lock_guard lock(mutex_);
    rx_buffer_.clear();
    rx_offset_ = 0;
    if (wait_event_ != INVALID_HANDLE_VALUE && wait_event_ != nullptr) {
        ResetEvent(wait_event_);
    }
}

void NetlinkSocketState::AppendAttr(std::vector<uint8_t> &buf, unsigned short type,
                                    const void *data, unsigned short len)
{
    size_t rta_len = RTA_LENGTH(len);
    size_t aligned_len = RTA_ALIGN(rta_len);
    size_t old_size = buf.size();
    buf.resize(old_size + aligned_len, 0);

    auto *rta = reinterpret_cast<rtattr *>(buf.data() + old_size);
    rta->rta_type = type;
    rta->rta_len = static_cast<unsigned short>(rta_len);

    if (data && len > 0) {
        std::memcpy(RTA_DATA(rta), data, len);
    }
}

void NetlinkSocketState::AppendAttrU32(std::vector<uint8_t> &buf, unsigned short type, uint32_t val)
{
    AppendAttr(buf, type, &val, sizeof(val));
}

void NetlinkSocketState::AppendAttrU8(std::vector<uint8_t> &buf, unsigned short type, uint8_t val)
{
    AppendAttr(buf, type, &val, sizeof(val));
}

void NetlinkSocketState::AppendAttrStr(std::vector<uint8_t> &buf, unsigned short type,
                                       const char *str)
{
    if (str) {
        AppendAttr(buf, type, str, static_cast<unsigned short>(std::strlen(str) + 1));
    }
}

void NetlinkSocketState::AppendDone(uint32_t seq, uint32_t pid)
{
    size_t old_size = rx_buffer_.size();
    size_t msg_len = NLMSG_SPACE(0);
    rx_buffer_.resize(old_size + msg_len, 0);

    auto *nlh = reinterpret_cast<nlmsghdr *>(rx_buffer_.data() + old_size);
    nlh->nlmsg_len = static_cast<uint32_t>(sizeof(nlmsghdr));
    nlh->nlmsg_type = NLMSG_DONE;
    nlh->nlmsg_flags = NLM_F_MULTI;
    nlh->nlmsg_seq = seq;
    nlh->nlmsg_pid = pid;
}

void NetlinkSocketState::AppendError(const nlmsghdr &req, int error_code)
{
    size_t old_size = rx_buffer_.size();
    size_t payload_len = sizeof(nlmsgerr);
    size_t msg_len = NLMSG_SPACE(payload_len);
    rx_buffer_.resize(old_size + msg_len, 0);

    auto *nlh = reinterpret_cast<nlmsghdr *>(rx_buffer_.data() + old_size);
    nlh->nlmsg_len = static_cast<uint32_t>(NLMSG_LENGTH(payload_len));
    nlh->nlmsg_type = NLMSG_ERROR;
    nlh->nlmsg_flags = 0;
    nlh->nlmsg_seq = req.nlmsg_seq;
    nlh->nlmsg_pid = req.nlmsg_pid;

    auto *err = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(nlh));
    err->error = error_code;
    err->msg = req;
}

void NetlinkSocketState::HandleGetLink(const nlmsghdr &req)
{
    uint32_t seq = req.nlmsg_seq;
    uint32_t pid = req.nlmsg_pid ? req.nlmsg_pid : local_addr_.nl_pid;

    auto append_link = [&](int ifindex, unsigned short if_type, unsigned int flags,
                           const char *name, uint32_t mtu, unsigned char operstate,
                           const uint8_t *mac, size_t mac_len) {
        std::vector<uint8_t> attrs;
        AppendAttrStr(attrs, IFLA_IFNAME, name);
        AppendAttrU32(attrs, IFLA_MTU, mtu);
        AppendAttrU8(attrs, IFLA_OPERSTATE, operstate);
        if (mac && mac_len > 0) {
            AppendAttr(attrs, IFLA_ADDRESS, mac, static_cast<unsigned short>(mac_len));
        }

        size_t payload_sz = sizeof(ifinfomsg) + attrs.size();
        size_t total_sz = NLMSG_SPACE(payload_sz);
        size_t old_size = rx_buffer_.size();
        rx_buffer_.resize(old_size + total_sz, 0);

        auto *nlh = reinterpret_cast<nlmsghdr *>(rx_buffer_.data() + old_size);
        nlh->nlmsg_len = static_cast<uint32_t>(NLMSG_LENGTH(payload_sz));
        nlh->nlmsg_type = RTM_NEWLINK;
        nlh->nlmsg_flags = NLM_F_MULTI;
        nlh->nlmsg_seq = seq;
        nlh->nlmsg_pid = pid;

        auto *ifi = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(nlh));
        ifi->ifi_family = 0; // AF_UNSPEC
        ifi->ifi_type = if_type;
        ifi->ifi_index = ifindex;
        ifi->ifi_flags = flags;
        ifi->ifi_change = 0xFFFFFFFF;

        if (!attrs.empty()) {
            std::memcpy(reinterpret_cast<uint8_t *>(ifi) + sizeof(ifinfomsg), attrs.data(),
                        attrs.size());
        }
    };

    // Unconditionally append loopback interface (lo, index 1)
    const uint8_t loopback_mac[6] = {0, 0, 0, 0, 0, 0};
    append_link(1, LINUX_ARPHRD_LOOPBACK, LINUX_IFF_UP | LINUX_IFF_LOOPBACK | LINUX_IFF_RUNNING,
                "lo", 65536, IF_OPER_UP, loopback_mac, 6);

    // Query Windows adapters via IP Helper
    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            int ifindex = static_cast<int>(curr->IfIndex > 1 ? curr->IfIndex : 2);

            char ifname[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(ifname, sizeof(ifname), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(ifname, sizeof(ifname), "eth%d", next_eth_idx++);
            }

            unsigned int flags = LINUX_IFF_BROADCAST | LINUX_IFF_MULTICAST;
            if (curr->OperStatus == IfOperStatusUp) {
                flags |= LINUX_IFF_UP | LINUX_IFF_RUNNING;
            }

            unsigned char operstate =
                (curr->OperStatus == IfOperStatusUp) ? IF_OPER_UP : IF_OPER_DOWN;

            append_link(ifindex, LINUX_ARPHRD_ETHER, flags, ifname, curr->Mtu, operstate,
                        curr->PhysicalAddress, curr->PhysicalAddressLength);
        }
    }

    AppendDone(seq, pid);
}

void NetlinkSocketState::HandleGetAddr(const nlmsghdr &req)
{
    uint32_t seq = req.nlmsg_seq;
    uint32_t pid = req.nlmsg_pid ? req.nlmsg_pid : local_addr_.nl_pid;

    auto append_addr = [&](unsigned char family, unsigned char prefixlen, unsigned char scope,
                           int ifindex, const void *addr_bytes, size_t addr_len,
                           const void *bcast_bytes, size_t bcast_len, const char *label) {
        std::vector<uint8_t> attrs;
        if (addr_bytes && addr_len > 0) {
            AppendAttr(attrs, IFA_ADDRESS, addr_bytes, static_cast<unsigned short>(addr_len));
            AppendAttr(attrs, IFA_LOCAL, addr_bytes, static_cast<unsigned short>(addr_len));
        }
        if (bcast_bytes && bcast_len > 0) {
            AppendAttr(attrs, IFA_BROADCAST, bcast_bytes, static_cast<unsigned short>(bcast_len));
        }
        if (label) {
            AppendAttrStr(attrs, IFA_LABEL, label);
        }

        size_t payload_sz = sizeof(ifaddrmsg) + attrs.size();
        size_t total_sz = NLMSG_SPACE(payload_sz);
        size_t old_size = rx_buffer_.size();
        rx_buffer_.resize(old_size + total_sz, 0);

        auto *nlh = reinterpret_cast<nlmsghdr *>(rx_buffer_.data() + old_size);
        nlh->nlmsg_len = static_cast<uint32_t>(NLMSG_LENGTH(payload_sz));
        nlh->nlmsg_type = RTM_NEWADDR;
        nlh->nlmsg_flags = NLM_F_MULTI;
        nlh->nlmsg_seq = seq;
        nlh->nlmsg_pid = pid;

        auto *ifa = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(nlh));
        ifa->ifa_family = family;
        ifa->ifa_prefixlen = prefixlen;
        ifa->ifa_flags = 0;
        ifa->ifa_scope = scope;
        ifa->ifa_index = static_cast<uint32_t>(ifindex);

        if (!attrs.empty()) {
            std::memcpy(reinterpret_cast<uint8_t *>(ifa) + sizeof(ifaddrmsg), attrs.data(),
                        attrs.size());
        }
    };

    // Unconditionally append loopback addresses:
    // IPv4 127.0.0.1/8
    const uint32_t lo_ipv4 = htonl(0x7F000001);
    append_addr(LINUX_AF_INET, 8, RT_SCOPE_HOST, 1, &lo_ipv4, 4, nullptr, 0, "lo");

    // IPv6 ::1/128
    uint8_t lo_ipv6[16]{};
    lo_ipv6[15] = 1;
    append_addr(LINUX_AF_INET6, 128, RT_SCOPE_HOST, 1, lo_ipv6, 16, nullptr, 0, "lo");

    // Query Windows adapters via IP Helper
    ULONG out_buf_len = 16384;
    std::vector<uint8_t> adapter_buf(out_buf_len, 0);
    auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());

    ULONG ret = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
        nullptr, adapters, &out_buf_len);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_buf.resize(out_buf_len, 0);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(adapter_buf.data());
        ret = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, adapters, &out_buf_len);
    }

    if (ret == NO_ERROR) {
        int next_eth_idx = 0;
        int next_wlan_idx = 0;

        for (auto *curr = adapters; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            int ifindex = static_cast<int>(curr->IfIndex > 1 ? curr->IfIndex : 2);
            char ifname[64]{};
            if (curr->IfType == IF_TYPE_IEEE80211) {
                std::snprintf(ifname, sizeof(ifname), "wlan%d", next_wlan_idx++);
            } else {
                std::snprintf(ifname, sizeof(ifname), "eth%d", next_eth_idx++);
            }

            for (auto *uni = curr->FirstUnicastAddress; uni != nullptr; uni = uni->Next) {
                if (uni->Address.lpSockaddr == nullptr) {
                    continue;
                }

                auto family = uni->Address.lpSockaddr->sa_family;
                if (family == AF_INET) {
                    auto *sin = reinterpret_cast<sockaddr_in *>(uni->Address.lpSockaddr);
                    uint32_t addr_val = sin->sin_addr.s_addr;
                    uint8_t prefix = uni->OnLinkPrefixLength;
                    uint32_t mask =
                        (prefix >= 32) ? 0xFFFFFFFF : htonl(~((1ULL << (32 - prefix)) - 1));
                    uint32_t bcast = addr_val | ~mask;

                    append_addr(LINUX_AF_INET, prefix, RT_SCOPE_UNIVERSE, ifindex, &addr_val, 4,
                                &bcast, 4, ifname);
                } else if (family == AF_INET6) {
                    auto *sin6 = reinterpret_cast<sockaddr_in6 *>(uni->Address.lpSockaddr);
                    uint8_t prefix = uni->OnLinkPrefixLength;
                    unsigned char scope =
                        IsIpv6LinkLocal(&sin6->sin6_addr) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;

                    append_addr(LINUX_AF_INET6, prefix, scope, ifindex, &sin6->sin6_addr, 16,
                                nullptr, 0, ifname);
                }
            }
        }
    }

    AppendDone(seq, pid);
}

void NetlinkSocketState::HandleGetRoute(const nlmsghdr &req)
{
    uint32_t seq = req.nlmsg_seq;
    uint32_t pid = req.nlmsg_pid ? req.nlmsg_pid : local_addr_.nl_pid;

    auto append_route = [&](unsigned char family, unsigned char dst_len, unsigned char scope,
                            unsigned char table, const void *dst_bytes, size_t dst_sz,
                            const void *gw_bytes, size_t gw_sz, uint32_t oif, uint32_t priority) {
        std::vector<uint8_t> attrs;
        if (dst_bytes && dst_sz > 0) {
            AppendAttr(attrs, RTA_DST, dst_bytes, static_cast<unsigned short>(dst_sz));
        }
        if (gw_bytes && gw_sz > 0) {
            AppendAttr(attrs, RTA_GATEWAY, gw_bytes, static_cast<unsigned short>(gw_sz));
        }
        if (oif > 0) {
            AppendAttrU32(attrs, RTA_OIF, oif);
        }
        if (priority > 0) {
            AppendAttrU32(attrs, RTA_PRIORITY, priority);
        }
        AppendAttrU32(attrs, RTA_TABLE, table);

        size_t payload_sz = sizeof(rtmsg) + attrs.size();
        size_t total_sz = NLMSG_SPACE(payload_sz);
        size_t old_size = rx_buffer_.size();
        rx_buffer_.resize(old_size + total_sz, 0);

        auto *nlh = reinterpret_cast<nlmsghdr *>(rx_buffer_.data() + old_size);
        nlh->nlmsg_len = static_cast<uint32_t>(NLMSG_LENGTH(payload_sz));
        nlh->nlmsg_type = RTM_NEWROUTE;
        nlh->nlmsg_flags = NLM_F_MULTI;
        nlh->nlmsg_seq = seq;
        nlh->nlmsg_pid = pid;

        auto *rtm = reinterpret_cast<rtmsg *>(NLMSG_DATA(nlh));
        rtm->rtm_family = family;
        rtm->rtm_dst_len = dst_len;
        rtm->rtm_src_len = 0;
        rtm->rtm_tos = 0;
        rtm->rtm_table = table;
        rtm->rtm_protocol = RTPROT_BOOT;
        rtm->rtm_scope = scope;
        rtm->rtm_type = RTN_UNICAST;
        rtm->rtm_flags = 0;

        if (!attrs.empty()) {
            std::memcpy(reinterpret_cast<uint8_t *>(rtm) + sizeof(rtmsg), attrs.data(),
                        attrs.size());
        }
    };

    // Unconditionally append loopback route: 127.0.0.0/8 on lo (oif 1)
    const uint32_t lo_dst = htonl(0x7F000000);
    append_route(LINUX_AF_INET, 8, RT_SCOPE_HOST, RT_TABLE_LOCAL, &lo_dst, 4, nullptr, 0, 1, 0);

    // Query Windows routing table via GetIpForwardTable2
    PMIB_IPFORWARD_TABLE2 routes = nullptr;
    if (GetIpForwardTable2(AF_UNSPEC, &routes) == NO_ERROR && routes != nullptr) {
        for (ULONG i = 0; i < routes->NumEntries; i++) {
            const auto &row = routes->Table[i];
            auto family = row.DestinationPrefix.Prefix.si_family;
            uint32_t oif = row.InterfaceIndex > 1 ? row.InterfaceIndex : 2;
            uint32_t metric = row.Metric;

            if (family == AF_INET) {
                const auto &dest_sin = row.DestinationPrefix.Prefix.Ipv4;
                const auto &next_sin = row.NextHop.Ipv4;
                uint8_t prefix_len = row.DestinationPrefix.PrefixLength;

                uint32_t dst_addr = dest_sin.sin_addr.s_addr;
                uint32_t gw_addr = next_sin.sin_addr.s_addr;

                bool is_default = (prefix_len == 0);
                unsigned char scope =
                    (gw_addr != 0 && !is_default) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;

                append_route(LINUX_AF_INET, prefix_len, scope, RT_TABLE_MAIN,
                             prefix_len > 0 ? &dst_addr : nullptr, prefix_len > 0 ? 4 : 0,
                             gw_addr != 0 ? &gw_addr : nullptr, gw_addr != 0 ? 4 : 0, oif, metric);
            } else if (family == AF_INET6) {
                const auto &dest_sin6 = row.DestinationPrefix.Prefix.Ipv6;
                const auto &next_sin6 = row.NextHop.Ipv6;
                uint8_t prefix_len = row.DestinationPrefix.PrefixLength;

                bool is_default = (prefix_len == 0);
                bool has_gw = !IsIpv6Unspecified(&next_sin6.sin6_addr);
                unsigned char scope = (has_gw && !is_default) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;

                append_route(LINUX_AF_INET6, prefix_len, scope, RT_TABLE_MAIN,
                             prefix_len > 0 ? &dest_sin6.sin6_addr : nullptr,
                             prefix_len > 0 ? 16 : 0, has_gw ? &next_sin6.sin6_addr : nullptr,
                             has_gw ? 16 : 0, oif, metric);
            }
        }
        FreeMibTable(routes);
    }

    AppendDone(seq, pid);
}

int64_t NetlinkSocketState::Send(const void *buf, size_t len, int)
{
    if (buf == nullptr || len < sizeof(nlmsghdr)) {
        return -22; // -EINVAL
    }

    std::lock_guard lock(mutex_);

    const auto *nlh = reinterpret_cast<const nlmsghdr *>(buf);
    if (nlh->nlmsg_len < sizeof(nlmsghdr) || nlh->nlmsg_len > len) {
        return -22; // -EINVAL
    }

    // Prepare fresh response stream
    rx_buffer_.clear();
    rx_offset_ = 0;

    switch (nlh->nlmsg_type) {
    case RTM_GETLINK:
        HandleGetLink(*nlh);
        break;
    case RTM_GETADDR:
        HandleGetAddr(*nlh);
        break;
    case RTM_GETROUTE:
        HandleGetRoute(*nlh);
        break;
    default:
        // Echo unsupported netlink requests with NLMSG_DONE or ACK
        if (nlh->nlmsg_flags & NLM_F_ACK) {
            AppendError(*nlh, 0); // 0 error indicates success ACK
        } else {
            AppendDone(nlh->nlmsg_seq, nlh->nlmsg_pid ? nlh->nlmsg_pid : local_addr_.nl_pid);
        }
        break;
    }

    if (!rx_buffer_.empty() && wait_event_ != INVALID_HANDLE_VALUE && wait_event_ != nullptr) {
        SetEvent(wait_event_);
    }

    return static_cast<int64_t>(len);
}

int64_t NetlinkSocketState::Recv(void *buf, size_t len, int)
{
    if (buf == nullptr || len == 0) {
        return -22; // -EINVAL
    }

    std::lock_guard lock(mutex_);

    if (rx_offset_ >= rx_buffer_.size()) {
        if (wait_event_ != INVALID_HANDLE_VALUE && wait_event_ != nullptr) {
            ResetEvent(wait_event_);
        }
        return -11; // -EAGAIN / -EWOULDBLOCK
    }

    size_t available = rx_buffer_.size() - rx_offset_;
    size_t to_copy = std::min(available, len);

    std::memcpy(buf, rx_buffer_.data() + rx_offset_, to_copy);
    rx_offset_ += to_copy;

    if (rx_offset_ >= rx_buffer_.size()) {
        if (wait_event_ != INVALID_HANDLE_VALUE && wait_event_ != nullptr) {
            ResetEvent(wait_event_);
        }
    }

    return static_cast<int64_t>(to_copy);
}

} // namespace portage::net
