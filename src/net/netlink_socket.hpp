#pragma once

#include "net/netlink_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace portage::net {

class NetlinkSocketState {
  public:
    NetlinkSocketState();
    ~NetlinkSocketState();

    NetlinkSocketState(const NetlinkSocketState &) = delete;
    NetlinkSocketState &operator=(const NetlinkSocketState &) = delete;

    int Bind(const sockaddr_nl *addr);
    int GetSockName(sockaddr_nl *addr) const;

    int64_t Send(const void *buf, size_t len, int flags);
    int64_t Recv(void *buf, size_t len, int flags);

    [[nodiscard]] bool HasData() const noexcept;
    [[nodiscard]] HANDLE GetWaitEvent() const noexcept;

    void Reset();

  private:
    void HandleGetLink(const nlmsghdr &req);
    void HandleGetAddr(const nlmsghdr &req);
    void HandleGetRoute(const nlmsghdr &req);

    void AppendDone(uint32_t seq, uint32_t pid);
    void AppendError(const nlmsghdr &req, int error_code);

    void AppendAttr(std::vector<uint8_t> &buf, unsigned short type, const void *data,
                    unsigned short len);
    void AppendAttrU32(std::vector<uint8_t> &buf, unsigned short type, uint32_t val);
    void AppendAttrU8(std::vector<uint8_t> &buf, unsigned short type, uint8_t val);
    void AppendAttrStr(std::vector<uint8_t> &buf, unsigned short type, const char *str);

    mutable std::mutex mutex_{};
    sockaddr_nl local_addr_{};
    bool bound_{false};

    std::vector<uint8_t> rx_buffer_{};
    size_t rx_offset_{0};
    HANDLE wait_event_{INVALID_HANDLE_VALUE};
};

} // namespace portage::net
