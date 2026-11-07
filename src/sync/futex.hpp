#pragma once

#include <cstdint>

namespace portage::sync {

constexpr int LINUX_FUTEX_WAIT = 0;
constexpr int LINUX_FUTEX_WAKE = 1;
constexpr int LINUX_FUTEX_FD = 2;
constexpr int LINUX_FUTEX_REQUEUE = 3;
constexpr int LINUX_FUTEX_CMP_REQUEUE = 4;
constexpr int LINUX_FUTEX_WAKE_OP = 5;
constexpr int LINUX_FUTEX_LOCK_PI = 6;
constexpr int LINUX_FUTEX_UNLOCK_PI = 7;
constexpr int LINUX_FUTEX_TRYLOCK_PI = 8;
constexpr int LINUX_FUTEX_WAIT_BITSET = 9;
constexpr int LINUX_FUTEX_WAKE_BITSET = 10;
constexpr int LINUX_FUTEX_WAIT_REQUEUE_PI = 11;
constexpr int LINUX_FUTEX_CMP_REQUEUE_PI = 12;

constexpr int LINUX_FUTEX_PRIVATE_FLAG = 128;
constexpr int LINUX_FUTEX_CLOCK_REALTIME = 256;
constexpr int LINUX_FUTEX_CMD_MASK = ~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME);

constexpr uint32_t LINUX_FUTEX_BITSET_MATCH_ANY = 0xffffffff;

struct LinuxTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

class FutexManager {
  public:
    FutexManager() = default;
    ~FutexManager() = default;

    int64_t Futex(uint32_t *uaddr, int futex_op, uint32_t val, const LinuxTimespec *timeout,
                  uint32_t *uaddr2, uint32_t val3);
};

} // namespace portage::sync
