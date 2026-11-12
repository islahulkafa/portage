#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

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

constexpr uint32_t LINUX_FUTEX_BITSET_MATCH_ANY = 0xffffffffU;

constexpr uint32_t LINUX_FUTEX_WAITERS = 0x80000000U;
constexpr uint32_t LINUX_FUTEX_OWNER_DIED = 0x40000000U;
constexpr uint32_t LINUX_FUTEX_TID_MASK = 0x3fffffffU;

constexpr int LINUX_FUTEX_OP_SET = 0;
constexpr int LINUX_FUTEX_OP_ADD = 1;
constexpr int LINUX_FUTEX_OP_OR = 2;
constexpr int LINUX_FUTEX_OP_ANDN = 3;
constexpr int LINUX_FUTEX_OP_XOR = 4;
constexpr int LINUX_FUTEX_OP_OPARG_SHIFT = 8;

constexpr int LINUX_FUTEX_OP_CMP_EQ = 0;
constexpr int LINUX_FUTEX_OP_CMP_NE = 1;
constexpr int LINUX_FUTEX_OP_CMP_LT = 2;
constexpr int LINUX_FUTEX_OP_CMP_LE = 3;
constexpr int LINUX_FUTEX_OP_CMP_GT = 4;
constexpr int LINUX_FUTEX_OP_CMP_GE = 5;

struct LinuxTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct FutexWaiter {
    std::mutex wait_mutex{};
    std::condition_variable cv{};
    uint32_t *uaddr{nullptr};
    uint32_t bitset{LINUX_FUTEX_BITSET_MATCH_ANY};
    bool is_pi{false};
    uint32_t tid{0};
    bool woken{false};
};

struct FutexBucket {
    std::mutex mutex{};
    std::vector<std::shared_ptr<FutexWaiter>> waiters{};
};

class FutexManager {
  public:
    FutexManager() = default;
    ~FutexManager() = default;

    FutexManager(const FutexManager &) = delete;
    FutexManager &operator=(const FutexManager &) = delete;

    int64_t Futex(uint32_t *uaddr, int futex_op, uint32_t val, const LinuxTimespec *timeout,
                  uint32_t *uaddr2, uint32_t val3);

    [[nodiscard]] size_t GetWaiterCount(const uint32_t *uaddr) const;

  private:
    static constexpr size_t kNumBuckets = 64;

    [[nodiscard]] static size_t BucketIndex(const uint32_t *uaddr) noexcept
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(uaddr);
        return ((addr >> 2) ^ (addr >> 8) ^ (addr >> 16)) % kNumBuckets;
    }

    int64_t WaitImpl(uint32_t *uaddr, uint32_t val, const LinuxTimespec *timeout, uint32_t bitset,
                     bool is_absolute, bool use_realtime);
    int64_t WakeImpl(uint32_t *uaddr, uint32_t max_wake, uint32_t bitset);
    int64_t RequeueImpl(uint32_t *uaddr, uint32_t *uaddr2, uint32_t val, uint32_t val2,
                        uint32_t val3, bool has_cmp);
    int64_t WakeOpImpl(uint32_t *uaddr, uint32_t *uaddr2, uint32_t val, uint32_t val2,
                       uint32_t val3);
    int64_t LockPiImpl(uint32_t *uaddr, const LinuxTimespec *timeout, bool trylock);
    int64_t UnlockPiImpl(uint32_t *uaddr);

    mutable std::array<FutexBucket, kNumBuckets> buckets_{};
};

} // namespace portage::sync
