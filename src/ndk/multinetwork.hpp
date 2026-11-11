#pragma once

#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using net_handle_t = uint64_t;

struct LinuxAddrInfo {
    int ai_flags{0};
    int ai_family{0};
    int ai_socktype{0};
    int ai_protocol{0};
    uint32_t ai_addrlen{0};
    uint32_t __pad{0};
    void *ai_addr{nullptr};
    char *ai_canonname{nullptr};
    LinuxAddrInfo *ai_next{nullptr};
};

namespace portage::ndk {

inline constexpr net_handle_t NETWORK_UNSPECIFIED = 0ULL;
inline constexpr uint32_t NET_ID_DEFAULT = 100;
inline constexpr uint32_t NET_ID_WIFI = 101;
inline constexpr uint32_t NET_ID_CELLULAR = 102;

inline constexpr net_handle_t NetHandleFromId(uint32_t netid) noexcept
{
    if (netid == 0) {
        return NETWORK_UNSPECIFIED;
    }
    return (static_cast<uint64_t>(netid) << 32) | 0xfacadeULL;
}

inline constexpr uint32_t NetIdFromHandle(net_handle_t handle) noexcept
{
    return static_cast<uint32_t>(handle >> 32);
}

inline constexpr net_handle_t NET_HANDLE_DEFAULT = NetHandleFromId(NET_ID_DEFAULT);
inline constexpr net_handle_t NET_HANDLE_WIFI = NetHandleFromId(NET_ID_WIFI);
inline constexpr net_handle_t NET_HANDLE_CELLULAR = NetHandleFromId(NET_ID_CELLULAR);

struct PendingDnsQuery {
    int read_fd{-1};
    int write_fd{-1};
    std::atomic<bool> cancelled{false};
    std::thread worker{};
    std::vector<uint8_t> response_payload{};
    int rcode{0};
};

class MultinetworkManager {
  public:
    static MultinetworkManager &Instance() noexcept;

    void Reset() noexcept;

    int SetProcNetwork(net_handle_t network) noexcept;
    int GetProcNetwork(net_handle_t *network) noexcept;

    int SetSocketNetwork(net_handle_t network, int fd) noexcept;
    net_handle_t GetSocketNetwork(int fd) noexcept;

    int GetAddrInfoForNetwork(net_handle_t network, const char *node, const char *service,
                              const LinuxAddrInfo *hints, LinuxAddrInfo **res);
    void FreeLinuxAddrInfo(LinuxAddrInfo *ai) noexcept;

    int ResNQuery(net_handle_t network, const char *dname, int ns_class, int ns_type,
                  uint32_t flags);
    int ResNSearch(net_handle_t network, const char *dname, int ns_class, int ns_type,
                   uint32_t flags);
    int ResNSend(net_handle_t network, const uint8_t *msg, size_t msglen, uint32_t flags);
    int ResNResult(int fd, int *rcode);
    void ResCancel(int nsend_fd);

    static std::vector<uint8_t> BuildRfc1035DnsResponse(uint16_t id, std::string_view domain_name,
                                                        uint16_t qtype, uint16_t qclass,
                                                        const std::vector<uint32_t> &ipv4_addrs,
                                                        int rcode = 0);

  private:
    MultinetworkManager() = default;
    ~MultinetworkManager();

    MultinetworkManager(const MultinetworkManager &) = delete;
    MultinetworkManager &operator=(const MultinetworkManager &) = delete;

    std::atomic<net_handle_t> proc_network_{NET_HANDLE_DEFAULT};
    std::mutex socket_mutex_{};
    std::unordered_map<int, net_handle_t> socket_networks_{};

    std::mutex query_mutex_{};
    std::unordered_map<int, std::shared_ptr<PendingDnsQuery>> pending_queries_{};
};

} // namespace portage::ndk

using portage::ndk::NET_HANDLE_CELLULAR;
using portage::ndk::NET_HANDLE_DEFAULT;
using portage::ndk::NET_HANDLE_WIFI;
using portage::ndk::NETWORK_UNSPECIFIED;

extern "C" {
__attribute__((sysv_abi)) int android_setsocknetwork(net_handle_t network, int fd);
__attribute__((sysv_abi)) int android_setprocnetwork(net_handle_t network);
__attribute__((sysv_abi)) int android_getprocnetwork(net_handle_t *network);
__attribute__((sysv_abi)) int android_getaddrinfofornetwork(net_handle_t network, const char *node,
                                                            const char *service,
                                                            const struct LinuxAddrInfo *hints,
                                                            struct LinuxAddrInfo **res);
__attribute__((sysv_abi)) void android_freeaddrinfofornetwork(struct LinuxAddrInfo *ai);
__attribute__((sysv_abi)) int android_res_nquery(net_handle_t network, const char *dname,
                                                 int ns_class, int ns_type, uint32_t flags);
__attribute__((sysv_abi)) int android_res_nsearch(net_handle_t network, const char *dname,
                                                  int ns_class, int ns_type, uint32_t flags);
__attribute__((sysv_abi)) int android_res_nsend(net_handle_t network, const uint8_t *msg,
                                                size_t msglen, uint32_t flags);
__attribute__((sysv_abi)) int android_res_nresult(int fd, int *rcode);
__attribute__((sysv_abi)) void android_res_cancel(int nsend_fd);
__attribute__((sysv_abi)) void android_freeaddrinfo(struct LinuxAddrInfo *ai);
}
