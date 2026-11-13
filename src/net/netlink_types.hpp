#pragma once

#include "net/socket_types.hpp"

#include <cstddef>
#include <cstdint>

namespace portage::net {

constexpr int NETLINK_ROUTE = 0;

constexpr uint16_t NLM_F_REQUEST = 1;
constexpr uint16_t NLM_F_MULTI = 2;
constexpr uint16_t NLM_F_ACK = 4;
constexpr uint16_t NLM_F_ECHO = 8;
constexpr uint16_t NLM_F_DUMP_INTR = 16;
constexpr uint16_t NLM_F_DUMP_FILTERED = 32;

constexpr uint16_t NLM_F_ROOT = 0x100;
constexpr uint16_t NLM_F_MATCH = 0x200;
constexpr uint16_t NLM_F_ATOMIC = 0x400;
constexpr uint16_t NLM_F_DUMP = (NLM_F_ROOT | NLM_F_MATCH);

constexpr uint16_t NLMSG_NOOP = 1;
constexpr uint16_t NLMSG_ERROR = 2;
constexpr uint16_t NLMSG_DONE = 3;
constexpr uint16_t NLMSG_OVERRUN = 4;

constexpr uint16_t RTM_NEWLINK = 16;
constexpr uint16_t RTM_DELLINK = 17;
constexpr uint16_t RTM_GETLINK = 18;
constexpr uint16_t RTM_SETLINK = 19;
constexpr uint16_t RTM_NEWADDR = 20;
constexpr uint16_t RTM_DELADDR = 21;
constexpr uint16_t RTM_GETADDR = 22;
constexpr uint16_t RTM_NEWROUTE = 24;
constexpr uint16_t RTM_DELROUTE = 25;
constexpr uint16_t RTM_GETROUTE = 26;

constexpr uint32_t RTMGRP_LINK = 1;
constexpr uint32_t RTMGRP_NOTIFY = 2;
constexpr uint32_t RTMGRP_NEIGH = 4;
constexpr uint32_t RTMGRP_TC = 8;
constexpr uint32_t RTMGRP_IPV4_IFADDR = 0x10;
constexpr uint32_t RTMGRP_IPV4_MROUTE = 0x20;
constexpr uint32_t RTMGRP_IPV4_ROUTE = 0x40;
constexpr uint32_t RTMGRP_IPV4_RULE = 0x80;
constexpr uint32_t RTMGRP_IPV6_IFADDR = 0x100;
constexpr uint32_t RTMGRP_IPV6_MROUTE = 0x200;
constexpr uint32_t RTMGRP_IPV6_ROUTE = 0x400;
constexpr uint32_t RTMGRP_IPV6_IFINFO = 0x800;

#pragma pack(push, 4)

struct nlmsghdr {
    uint32_t nlmsg_len;
    uint16_t nlmsg_type;
    uint16_t nlmsg_flags;
    uint32_t nlmsg_seq;
    uint32_t nlmsg_pid;
};

struct nlmsgerr {
    int32_t error;
    nlmsghdr msg;
};

struct sockaddr_nl {
    uint16_t nl_family;
    uint16_t nl_pad;
    uint32_t nl_pid;
    uint32_t nl_groups;
};

struct ifinfomsg {
    unsigned char ifi_family;
    unsigned char __ifi_pad;
    unsigned short ifi_type;
    int ifi_index;
    unsigned int ifi_flags;
    unsigned int ifi_change;
};

struct ifaddrmsg {
    unsigned char ifa_family;
    unsigned char ifa_prefixlen;
    unsigned char ifa_flags;
    unsigned char ifa_scope;
    uint32_t ifa_index;
};

struct rtmsg {
    unsigned char rtm_family;
    unsigned char rtm_dst_len;
    unsigned char rtm_src_len;
    unsigned char rtm_tos;
    unsigned char rtm_table;
    unsigned char rtm_protocol;
    unsigned char rtm_scope;
    unsigned char rtm_type;
    unsigned int rtm_flags;
};

struct rtattr {
    unsigned short rta_len;
    unsigned short rta_type;
};

#pragma pack(pop)

constexpr unsigned short IFLA_UNSPEC = 0;
constexpr unsigned short IFLA_ADDRESS = 1;
constexpr unsigned short IFLA_BROADCAST = 2;
constexpr unsigned short IFLA_IFNAME = 3;
constexpr unsigned short IFLA_MTU = 4;
constexpr unsigned short IFLA_LINK = 5;
constexpr unsigned short IFLA_QDISC = 6;
constexpr unsigned short IFLA_STATS = 7;
constexpr unsigned short IFLA_OPERSTATE = 16;
constexpr unsigned short IFLA_LINKMODE = 17;
constexpr unsigned short IFLA_LINKINFO = 18;
constexpr unsigned short IFLA_GROUP = 27;
constexpr unsigned short IFLA_PROMISCUITY = 30;
constexpr unsigned short IFLA_NUM_TX_QUEUES = 31;
constexpr unsigned short IFLA_NUM_RX_QUEUES = 32;
constexpr unsigned short IFLA_CARRIER = 33;

constexpr unsigned char IF_OPER_UNKNOWN = 0;
constexpr unsigned char IF_OPER_NOTPRESENT = 1;
constexpr unsigned char IF_OPER_DOWN = 2;
constexpr unsigned char IF_OPER_LOWERLAYERDOWN = 3;
constexpr unsigned char IF_OPER_TESTING = 4;
constexpr unsigned char IF_OPER_DORMANT = 5;
constexpr unsigned char IF_OPER_UP = 6;

constexpr unsigned int LINUX_IFF_UP = 0x1;
constexpr unsigned int LINUX_IFF_BROADCAST = 0x2;
constexpr unsigned int LINUX_IFF_DEBUG = 0x4;
constexpr unsigned int LINUX_IFF_LOOPBACK = 0x8;
constexpr unsigned int LINUX_IFF_POINTOPOINT = 0x10;
constexpr unsigned int LINUX_IFF_NOTRAILERS = 0x20;
constexpr unsigned int LINUX_IFF_RUNNING = 0x40;
constexpr unsigned int LINUX_IFF_NOARP = 0x80;
constexpr unsigned int LINUX_IFF_PROMISC = 0x100;
constexpr unsigned int LINUX_IFF_ALLMULTI = 0x200;
constexpr unsigned int LINUX_IFF_MULTICAST = 0x1000;

constexpr unsigned short IFA_UNSPEC = 0;
constexpr unsigned short IFA_ADDRESS = 1;
constexpr unsigned short IFA_LOCAL = 2;
constexpr unsigned short IFA_LABEL = 3;
constexpr unsigned short IFA_BROADCAST = 4;
constexpr unsigned short IFA_ANYCAST = 5;
constexpr unsigned short IFA_CACHEINFO = 6;
constexpr unsigned short IFA_MULTICAST = 7;
constexpr unsigned short IFA_FLAGS = 8;

constexpr unsigned short RTA_UNSPEC = 0;
constexpr unsigned short RTA_DST = 1;
constexpr unsigned short RTA_SRC = 2;
constexpr unsigned short RTA_IIF = 3;
constexpr unsigned short RTA_OIF = 4;
constexpr unsigned short RTA_GATEWAY = 5;
constexpr unsigned short RTA_PRIORITY = 6;
constexpr unsigned short RTA_PREFSRC = 7;
constexpr unsigned short RTA_METRICS = 8;
constexpr unsigned short RTA_MULTIPATH = 9;
constexpr unsigned short RTA_TABLE = 15;

constexpr unsigned char RT_TABLE_MAIN = 254;
constexpr unsigned char RT_TABLE_LOCAL = 255;
constexpr unsigned char RTPROT_UNSPEC = 0;
constexpr unsigned char RTPROT_BOOT = 3;
constexpr unsigned char RTPROT_STATIC = 4;
constexpr unsigned char RT_SCOPE_UNIVERSE = 0;
constexpr unsigned char RT_SCOPE_SITE = 200;
constexpr unsigned char RT_SCOPE_LINK = 253;
constexpr unsigned char RT_SCOPE_HOST = 254;
constexpr unsigned char RT_SCOPE_NOWHERE = 255;
constexpr unsigned char RTN_UNSPEC = 0;
constexpr unsigned char RTN_UNICAST = 1;
constexpr unsigned char RTN_LOCAL = 2;

inline constexpr size_t NLMSG_ALIGN(size_t len) noexcept { return (len + 3) & ~size_t{3}; }

inline constexpr size_t NLMSG_HDRLEN = NLMSG_ALIGN(sizeof(nlmsghdr));

inline constexpr size_t NLMSG_LENGTH(size_t len) noexcept { return len + NLMSG_HDRLEN; }

inline constexpr size_t NLMSG_SPACE(size_t len) noexcept { return NLMSG_ALIGN(NLMSG_LENGTH(len)); }

inline void *NLMSG_DATA(nlmsghdr *nlh) noexcept
{
    return reinterpret_cast<uint8_t *>(nlh) + NLMSG_HDRLEN;
}

inline const void *NLMSG_DATA(const nlmsghdr *nlh) noexcept
{
    return reinterpret_cast<const uint8_t *>(nlh) + NLMSG_HDRLEN;
}

inline constexpr size_t RTA_ALIGN(size_t len) noexcept { return (len + 3) & ~size_t{3}; }

inline constexpr size_t RTA_LENGTH(size_t len) noexcept { return RTA_ALIGN(sizeof(rtattr)) + len; }

inline constexpr size_t RTA_SPACE(size_t len) noexcept { return RTA_ALIGN(RTA_LENGTH(len)); }

inline void *RTA_DATA(rtattr *rta) noexcept
{
    return reinterpret_cast<uint8_t *>(rta) + RTA_ALIGN(sizeof(rtattr));
}

inline const void *RTA_DATA(const rtattr *rta) noexcept
{
    return reinterpret_cast<const uint8_t *>(rta) + RTA_ALIGN(sizeof(rtattr));
}

} // namespace portage::net
