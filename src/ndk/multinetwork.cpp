#include "ndk/multinetwork.hpp"

#include "fs/fd_table.hpp"
#include "net/socket_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace portage::ndk {

MultinetworkManager &MultinetworkManager::Instance() noexcept
{
    static MultinetworkManager instance;
    return instance;
}

MultinetworkManager::~MultinetworkManager()
{
    Reset();
}

void MultinetworkManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(query_mutex_);
    for (auto &[fd, query] : pending_queries_) {
        if (query) {
            query->cancelled.store(true, std::memory_order_release);
            if (query->worker.joinable()) {
                query->worker.join();
            }
            if (auto *table = syscall::SyscallDispatcher::GetFdTable()) {
                if (query->write_fd >= 0) {
                    table->Close(query->write_fd);
                    query->write_fd = -1;
                }
                if (query->read_fd >= 0) {
                    table->Close(query->read_fd);
                    query->read_fd = -1;
                }
            }
        }
    }
    pending_queries_.clear();

    std::lock_guard<std::mutex> sock_lock(socket_mutex_);
    socket_networks_.clear();
    proc_network_.store(NET_HANDLE_DEFAULT, std::memory_order_release);
}

int MultinetworkManager::SetProcNetwork(net_handle_t network) noexcept
{
    if (network != NETWORK_UNSPECIFIED && network != NET_HANDLE_DEFAULT &&
        network != NET_HANDLE_WIFI && network != NET_HANDLE_CELLULAR) {
        if ((network & 0xffffffffULL) != 0xfacadeULL && network != 100 && network != 101 &&
            network != 102) {
            return -22; // -EINVAL
        }
    }
    proc_network_.store(network, std::memory_order_release);
    return 0;
}

int MultinetworkManager::GetProcNetwork(net_handle_t *network) noexcept
{
    if (!network) {
        return -22; // -EINVAL
    }
    *network = proc_network_.load(std::memory_order_acquire);
    return 0;
}

int MultinetworkManager::SetSocketNetwork(net_handle_t network, int fd) noexcept
{
    auto *table = syscall::SyscallDispatcher::GetFdTable();
    if (!table) {
        return -9; // -EBADF
    }
    auto desc = table->Get(fd);
    if (!desc || desc->dev != fs::DeviceType::Socket) {
        return -9; // -EBADF
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    socket_networks_[fd] = network;
    return 0;
}

net_handle_t MultinetworkManager::GetSocketNetwork(int fd) noexcept
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    auto it = socket_networks_.find(fd);
    if (it != socket_networks_.end()) {
        return it->second;
    }
    return proc_network_.load(std::memory_order_acquire);
}

int MultinetworkManager::GetAddrInfoForNetwork(net_handle_t network, const char *node,
                                              const char *service, const LinuxAddrInfo *hints,
                                              LinuxAddrInfo **res)
{
    (void)network;
    if (!res) {
        return -22; // -EINVAL
    }
    *res = nullptr;

    net::SocketManager::Instance().Initialize();

    ADDRINFOA win_hints{};
    if (hints) {
        win_hints.ai_flags = hints->ai_flags;
        win_hints.ai_family = hints->ai_family;
        win_hints.ai_socktype = hints->ai_socktype;
        win_hints.ai_protocol = hints->ai_protocol;
    }

    ADDRINFOA *win_res = nullptr;
    int err = getaddrinfo(node, service, hints ? &win_hints : nullptr, &win_res);
    if (err != 0 || !win_res) {
        return err;
    }

    LinuxAddrInfo *head = nullptr;
    LinuxAddrInfo *tail = nullptr;

    for (ADDRINFOA *cur = win_res; cur != nullptr; cur = cur->ai_next) {
        auto *item = static_cast<LinuxAddrInfo *>(std::calloc(1, sizeof(LinuxAddrInfo)));
        if (!item) {
            break;
        }

        item->ai_flags = cur->ai_flags;
        item->ai_family = cur->ai_family;
        item->ai_socktype = cur->ai_socktype;
        item->ai_protocol = cur->ai_protocol;
        item->ai_addrlen = static_cast<uint32_t>(cur->ai_addrlen);

        if (cur->ai_addr && cur->ai_addrlen > 0) {
            item->ai_addr = std::malloc(cur->ai_addrlen);
            if (item->ai_addr) {
                std::memcpy(item->ai_addr, cur->ai_addr, cur->ai_addrlen);
            }
        }

        if (cur->ai_canonname) {
            size_t clen = std::strlen(cur->ai_canonname);
            item->ai_canonname = static_cast<char *>(std::malloc(clen + 1));
            if (item->ai_canonname) {
                std::memcpy(item->ai_canonname, cur->ai_canonname, clen + 1);
            }
        }

        if (!head) {
            head = item;
            tail = item;
        } else {
            tail->ai_next = item;
            tail = item;
        }
    }

    freeaddrinfo(win_res);

    if (!head) {
        return -12; // -ENOMEM
    }

    *res = head;
    return 0;
}

void MultinetworkManager::FreeLinuxAddrInfo(LinuxAddrInfo *ai) noexcept
{
    while (ai) {
        LinuxAddrInfo *next = ai->ai_next;
        if (ai->ai_addr) {
            std::free(ai->ai_addr);
        }
        if (ai->ai_canonname) {
            std::free(ai->ai_canonname);
        }
        std::free(ai);
        ai = next;
    }
}

std::vector<uint8_t>
MultinetworkManager::BuildRfc1035DnsResponse(uint16_t id, std::string_view domain_name,
                                             uint16_t qtype, uint16_t qclass,
                                             const std::vector<uint32_t> &ipv4_addrs, int rcode)
{
    std::vector<uint8_t> packet;
    packet.reserve(512);

    // DNS Header (12 bytes)
    packet.push_back(static_cast<uint8_t>(id >> 8));
    packet.push_back(static_cast<uint8_t>(id & 0xFF));

    // Flags: QR=1 (response), RD=1, RA=1, RCODE
    uint8_t byte2 = 0x81;
    uint8_t byte3 = 0x80 | static_cast<uint8_t>(rcode & 0x0F);
    packet.push_back(byte2);
    packet.push_back(byte3);

    // QDCOUNT = 1
    packet.push_back(0x00);
    packet.push_back(0x01);

    // ANCOUNT
    uint16_t ancount = static_cast<uint16_t>(ipv4_addrs.size());
    packet.push_back(static_cast<uint8_t>(ancount >> 8));
    packet.push_back(static_cast<uint8_t>(ancount & 0xFF));

    // NSCOUNT = 0
    packet.push_back(0x00);
    packet.push_back(0x00);

    // ARCOUNT = 0
    packet.push_back(0x00);
    packet.push_back(0x00);

    // Question Section
    size_t qname_offset = packet.size();
    size_t start = 0;
    while (start < domain_name.size()) {
        size_t dot = domain_name.find('.', start);
        if (dot == std::string_view::npos) {
            dot = domain_name.size();
        }
        size_t len = dot - start;
        if (len > 0) {
            packet.push_back(static_cast<uint8_t>(len));
            for (size_t i = 0; i < len; ++i) {
                packet.push_back(static_cast<uint8_t>(domain_name[start + i]));
            }
        }
        start = dot + 1;
    }
    packet.push_back(0x00); // Terminal null label

    // QTYPE & QCLASS
    packet.push_back(static_cast<uint8_t>(qtype >> 8));
    packet.push_back(static_cast<uint8_t>(qtype & 0xFF));
    packet.push_back(static_cast<uint8_t>(qclass >> 8));
    packet.push_back(static_cast<uint8_t>(qclass & 0xFF));

    // Answer Records
    for (uint32_t ip : ipv4_addrs) {
        // Name compression pointer to question name
        uint16_t ptr = 0xC000 | static_cast<uint16_t>(qname_offset & 0x3FFF);
        packet.push_back(static_cast<uint8_t>(ptr >> 8));
        packet.push_back(static_cast<uint8_t>(ptr & 0xFF));

        // TYPE = A (1), CLASS = IN (1)
        packet.push_back(0x00);
        packet.push_back(0x01);
        packet.push_back(0x00);
        packet.push_back(0x01);

        // TTL = 60s
        packet.push_back(0x00);
        packet.push_back(0x00);
        packet.push_back(0x00);
        packet.push_back(0x3C);

        // RDLENGTH = 4
        packet.push_back(0x00);
        packet.push_back(0x04);

        // RDATA (IPv4 address in network order)
        packet.push_back(static_cast<uint8_t>(ip & 0xFF));
        packet.push_back(static_cast<uint8_t>((ip >> 8) & 0xFF));
        packet.push_back(static_cast<uint8_t>((ip >> 16) & 0xFF));
        packet.push_back(static_cast<uint8_t>((ip >> 24) & 0xFF));
    }

    return packet;
}

int MultinetworkManager::ResNQuery(net_handle_t network, const char *dname, int ns_class,
                                   int ns_type, uint32_t flags)
{
    (void)network;
    (void)flags;
    if (!dname) {
        return -22; // -EINVAL
    }

    auto *table = syscall::SyscallDispatcher::GetFdTable();
    if (!table) {
        return -1;
    }

    int pipefds[2]{-1, -1};
    if (table->CreatePipe(pipefds, 0) != 0) {
        return -24; // -EMFILE
    }

    int read_fd = pipefds[0];
    int write_fd = pipefds[1];

    auto query = std::make_shared<PendingDnsQuery>();
    query->read_fd = read_fd;
    query->write_fd = write_fd;

    std::string domain(dname);
    uint16_t qtype = static_cast<uint16_t>(ns_type);
    uint16_t qclass = static_cast<uint16_t>(ns_class);

    query->worker = std::thread([query, domain, qtype, qclass]() {
        net::SocketManager::Instance().Initialize();

        std::vector<uint32_t> addrs;
        int rcode = 0;

        ADDRINFOA hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        ADDRINFOA *res = nullptr;
        if (getaddrinfo(domain.c_str(), nullptr, &hints, &res) == 0 && res) {
            for (ADDRINFOA *cur = res; cur != nullptr; cur = cur->ai_next) {
                if (cur->ai_addr && cur->ai_family == AF_INET) {
                    auto *sin = reinterpret_cast<sockaddr_in *>(cur->ai_addr);
                    addrs.push_back(sin->sin_addr.s_addr);
                }
            }
            freeaddrinfo(res);
        } else {
            if (domain == "localhost") {
                addrs.push_back(0x0100007F); // 127.0.0.1
            } else {
                rcode = 3; // NXDOMAIN
            }
        }

        query->rcode = rcode;
        query->response_payload =
            BuildRfc1035DnsResponse(0x1234, domain, qtype, qclass, addrs, rcode);

        if (!query->cancelled.load(std::memory_order_acquire)) {
            auto *tbl = syscall::SyscallDispatcher::GetFdTable();
            if (tbl && query->write_fd >= 0) {
                auto desc = tbl->Get(query->write_fd);
                if (desc && desc->handle != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(desc->handle, query->response_payload.data(),
                              static_cast<DWORD>(query->response_payload.size()), &written, nullptr);
                }
                tbl->Close(query->write_fd);
                query->write_fd = -1;
            }
        }
    });

    {
        std::lock_guard<std::mutex> lock(query_mutex_);
        pending_queries_[read_fd] = query;
    }

    return read_fd;
}

int MultinetworkManager::ResNSearch(net_handle_t network, const char *dname, int ns_class,
                                    int ns_type, uint32_t flags)
{
    return ResNQuery(network, dname, ns_class, ns_type, flags);
}

int MultinetworkManager::ResNSend(net_handle_t network, const uint8_t *msg, size_t msglen,
                                  uint32_t flags)
{
    (void)network;
    (void)flags;
    if (!msg || msglen < 12) {
        return -22; // -EINVAL
    }

    uint16_t id = (static_cast<uint16_t>(msg[0]) << 8) | static_cast<uint16_t>(msg[1]);

    // Parse question domain name
    std::string domain;
    size_t pos = 12;
    while (pos < msglen && msg[pos] != 0) {
        uint8_t len = msg[pos++];
        if (pos + len > msglen) {
            break;
        }
        if (!domain.empty()) {
            domain.push_back('.');
        }
        domain.append(reinterpret_cast<const char *>(msg + pos), len);
        pos += len;
    }

    auto *table = syscall::SyscallDispatcher::GetFdTable();
    if (!table) {
        return -1;
    }

    int pipefds[2]{-1, -1};
    if (table->CreatePipe(pipefds, 0) != 0) {
        return -24; // -EMFILE
    }

    int read_fd = pipefds[0];
    int write_fd = pipefds[1];

    auto query = std::make_shared<PendingDnsQuery>();
    query->read_fd = read_fd;
    query->write_fd = write_fd;

    query->worker = std::thread([query, domain, id]() {
        net::SocketManager::Instance().Initialize();

        std::vector<uint32_t> addrs;
        int rcode = 0;

        ADDRINFOA hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        ADDRINFOA *res = nullptr;
        if (getaddrinfo(domain.c_str(), nullptr, &hints, &res) == 0 && res) {
            for (ADDRINFOA *cur = res; cur != nullptr; cur = cur->ai_next) {
                if (cur->ai_addr && cur->ai_family == AF_INET) {
                    auto *sin = reinterpret_cast<sockaddr_in *>(cur->ai_addr);
                    addrs.push_back(sin->sin_addr.s_addr);
                }
            }
            freeaddrinfo(res);
        } else {
            if (domain == "localhost" || domain.empty()) {
                addrs.push_back(0x0100007F); // 127.0.0.1
            } else {
                rcode = 3; // NXDOMAIN
            }
        }

        query->rcode = rcode;
        query->response_payload = BuildRfc1035DnsResponse(id, domain, 1, 1, addrs, rcode);

        if (!query->cancelled.load(std::memory_order_acquire)) {
            auto *tbl = syscall::SyscallDispatcher::GetFdTable();
            if (tbl && query->write_fd >= 0) {
                auto desc = tbl->Get(query->write_fd);
                if (desc && desc->handle != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(desc->handle, query->response_payload.data(),
                              static_cast<DWORD>(query->response_payload.size()), &written, nullptr);
                }
                tbl->Close(query->write_fd);
                query->write_fd = -1;
            }
        }
    });

    {
        std::lock_guard<std::mutex> lock(query_mutex_);
        pending_queries_[read_fd] = query;
    }

    return read_fd;
}

int MultinetworkManager::ResNResult(int fd, int *rcode)
{
    std::shared_ptr<PendingDnsQuery> query;
    {
        std::lock_guard<std::mutex> lock(query_mutex_);
        auto it = pending_queries_.find(fd);
        if (it != pending_queries_.end()) {
            query = it->second;
        }
    }

    if (!query) {
        return -9; // -EBADF
    }

    if (query->worker.joinable()) {
        query->worker.join();
    }

    if (rcode) {
        *rcode = query->rcode;
    }

    return static_cast<int>(query->response_payload.size());
}

void MultinetworkManager::ResCancel(int nsend_fd)
{
    std::shared_ptr<PendingDnsQuery> query;
    {
        std::lock_guard<std::mutex> lock(query_mutex_);
        auto it = pending_queries_.find(nsend_fd);
        if (it != pending_queries_.end()) {
            query = it->second;
            pending_queries_.erase(it);
        }
    }

    if (query) {
        query->cancelled.store(true, std::memory_order_release);
        if (query->worker.joinable()) {
            query->worker.join();
        }
        if (auto *tbl = syscall::SyscallDispatcher::GetFdTable()) {
            if (query->write_fd >= 0) {
                tbl->Close(query->write_fd);
                query->write_fd = -1;
            }
            if (query->read_fd >= 0) {
                tbl->Close(query->read_fd);
                query->read_fd = -1;
            }
        }
    }
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int android_setsocknetwork(net_handle_t network, int fd)
{
    return portage::ndk::MultinetworkManager::Instance().SetSocketNetwork(network, fd);
}

__attribute__((sysv_abi)) int android_setprocnetwork(net_handle_t network)
{
    return portage::ndk::MultinetworkManager::Instance().SetProcNetwork(network);
}

__attribute__((sysv_abi)) int android_getprocnetwork(net_handle_t *network)
{
    return portage::ndk::MultinetworkManager::Instance().GetProcNetwork(network);
}

__attribute__((sysv_abi)) int android_getaddrinfofornetwork(net_handle_t network, const char *node,
                                                           const char *service,
                                                           const struct LinuxAddrInfo *hints,
                                                           struct LinuxAddrInfo **res)
{
    return portage::ndk::MultinetworkManager::Instance().GetAddrInfoForNetwork(
        network, node, service, hints, res);
}

__attribute__((sysv_abi)) void android_freeaddrinfofornetwork(struct LinuxAddrInfo *ai)
{
    portage::ndk::MultinetworkManager::Instance().FreeLinuxAddrInfo(ai);
}

__attribute__((sysv_abi)) int android_res_nquery(net_handle_t network, const char *dname,
                                                int ns_class, int ns_type, uint32_t flags)
{
    return portage::ndk::MultinetworkManager::Instance().ResNQuery(network, dname, ns_class,
                                                                   ns_type, flags);
}

__attribute__((sysv_abi)) int android_res_nsearch(net_handle_t network, const char *dname,
                                                 int ns_class, int ns_type, uint32_t flags)
{
    return portage::ndk::MultinetworkManager::Instance().ResNSearch(network, dname, ns_class,
                                                                    ns_type, flags);
}

__attribute__((sysv_abi)) int android_res_nsend(net_handle_t network, const uint8_t *msg,
                                               size_t msglen, uint32_t flags)
{
    return portage::ndk::MultinetworkManager::Instance().ResNSend(network, msg, msglen, flags);
}

__attribute__((sysv_abi)) int android_res_nresult(int fd, int *rcode)
{
    return portage::ndk::MultinetworkManager::Instance().ResNResult(fd, rcode);
}

__attribute__((sysv_abi)) void android_res_cancel(int nsend_fd)
{
    portage::ndk::MultinetworkManager::Instance().ResCancel(nsend_fd);
}

__attribute__((sysv_abi)) void android_freeaddrinfo(struct LinuxAddrInfo *ai)
{
    portage::ndk::MultinetworkManager::Instance().FreeLinuxAddrInfo(ai);
}

} // extern "C"
