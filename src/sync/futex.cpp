#include "sync/futex.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>

namespace portage::sync {

namespace {

uint32_t GetCurrentFutexTid() noexcept
{
    uint32_t tid = static_cast<uint32_t>(thread::ThreadManager::Instance().GetCurrentTid());
    if (tid == 0) {
        tid = static_cast<uint32_t>(::GetCurrentThreadId());
    }
    return tid;
}

bool ValidateTimespec(const LinuxTimespec *ts) noexcept
{
    if (!ts) {
        return true;
    }
    return ts->tv_sec >= 0 && ts->tv_nsec >= 0 && ts->tv_nsec < 1'000'000'000;
}

} // namespace

size_t FutexManager::GetWaiterCount(const uint32_t *uaddr) const
{
    if (!uaddr) {
        return 0;
    }
    size_t b_idx = BucketIndex(uaddr);
    std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
    size_t count = 0;
    for (const auto &w : buckets_[b_idx].waiters) {
        if (w->uaddr == uaddr) {
            count++;
        }
    }
    return count;
}

int64_t FutexManager::WaitImpl(uint32_t *uaddr, uint32_t val, const LinuxTimespec *timeout,
                               uint32_t bitset, bool is_absolute, bool use_realtime)
{
    if (bitset == 0) {
        return -22; // -EINVAL
    }
    if (!ValidateTimespec(timeout)) {
        return -22; // -EINVAL
    }

    size_t b_idx = BucketIndex(uaddr);
    auto waiter = std::make_shared<FutexWaiter>();
    waiter->uaddr = uaddr;
    waiter->bitset = bitset;
    waiter->is_pi = false;
    waiter->tid = GetCurrentFutexTid();
    waiter->woken = false;

    {
        std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
        uint32_t current_val = __atomic_load_n(uaddr, __ATOMIC_SEQ_CST);
        if (current_val != val) {
            return -11; // -EAGAIN
        }
        buckets_[b_idx].waiters.push_back(waiter);
    }

    std::unique_lock<std::mutex> wait_lock(waiter->wait_mutex);
    if (!timeout) {
        waiter->cv.wait(wait_lock, [&waiter]() { return waiter->woken; });
    } else if (!is_absolute) {
        auto duration =
            std::chrono::seconds(timeout->tv_sec) + std::chrono::nanoseconds(timeout->tv_nsec);
        waiter->cv.wait_for(wait_lock, duration, [&waiter]() { return waiter->woken; });
    } else if (use_realtime) {
        auto deadline = std::chrono::system_clock::time_point(
            std::chrono::seconds(timeout->tv_sec) + std::chrono::nanoseconds(timeout->tv_nsec));
        waiter->cv.wait_until(wait_lock, deadline, [&waiter]() { return waiter->woken; });
    } else {
        auto deadline = std::chrono::steady_clock::time_point(
            std::chrono::seconds(timeout->tv_sec) + std::chrono::nanoseconds(timeout->tv_nsec));
        waiter->cv.wait_until(wait_lock, deadline, [&waiter]() { return waiter->woken; });
    }

    if (waiter->woken) {
        return 0;
    }

    while (!waiter->woken) {
        uint32_t *cur_uaddr = waiter->uaddr;
        size_t cur_b = BucketIndex(cur_uaddr);
        std::lock_guard<std::mutex> b_lock(buckets_[cur_b].mutex);
        if (waiter->uaddr != cur_uaddr) {
            continue;
        }
        if (waiter->woken) {
            return 0;
        }
        auto &vec = buckets_[cur_b].waiters;
        vec.erase(std::remove(vec.begin(), vec.end(), waiter), vec.end());
        return -110; // -ETIMEDOUT
    }

    return 0;
}

int64_t FutexManager::WakeImpl(uint32_t *uaddr, uint32_t max_wake, uint32_t bitset)
{
    if (bitset == 0) {
        return -22; // -EINVAL
    }
    if (max_wake == 0) {
        return 0;
    }

    size_t b_idx = BucketIndex(uaddr);
    int64_t woken_count = 0;

    std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
    auto &vec = buckets_[b_idx].waiters;
    auto it = vec.begin();
    while (it != vec.end() && static_cast<uint32_t>(woken_count) < max_wake) {
        auto waiter = *it;
        if (waiter->uaddr == uaddr && (waiter->bitset & bitset) != 0) {
            waiter->woken = true;
            waiter->cv.notify_one();
            it = vec.erase(it);
            woken_count++;
        } else {
            ++it;
        }
    }

    return woken_count;
}

int64_t FutexManager::RequeueImpl(uint32_t *uaddr, uint32_t *uaddr2, uint32_t val, uint32_t val2,
                                  uint32_t val3, bool has_cmp)
{
    if (!uaddr || (reinterpret_cast<uintptr_t>(uaddr) % 4 != 0) || !uaddr2 ||
        (reinterpret_cast<uintptr_t>(uaddr2) % 4 != 0) || uaddr == uaddr2) {
        return -22; // -EINVAL
    }

    size_t b1 = BucketIndex(uaddr);
    size_t b2 = BucketIndex(uaddr2);

    std::unique_lock<std::mutex> lock1(buckets_[std::min(b1, b2)].mutex, std::defer_lock);
    std::unique_lock<std::mutex> lock2(buckets_[std::max(b1, b2)].mutex, std::defer_lock);
    if (b1 == b2) {
        lock1.lock();
    } else {
        lock1.lock();
        lock2.lock();
    }

    if (has_cmp) {
        uint32_t current_val = __atomic_load_n(uaddr, __ATOMIC_SEQ_CST);
        if (current_val != val3) {
            return -11; // -EAGAIN
        }
    }

    int64_t woken_count = 0;
    int64_t requeued_count = 0;

    auto &src_vec = buckets_[b1].waiters;
    auto it = src_vec.begin();
    while (it != src_vec.end()) {
        auto waiter = *it;
        if (waiter->uaddr == uaddr) {
            if (static_cast<uint32_t>(woken_count) < val) {
                waiter->woken = true;
                waiter->cv.notify_one();
                it = src_vec.erase(it);
                woken_count++;
                continue;
            } else if (static_cast<uint32_t>(requeued_count) < val2) {
                it = src_vec.erase(it);
                waiter->uaddr = uaddr2;
                buckets_[b2].waiters.push_back(waiter);
                requeued_count++;
                continue;
            }
        }
        ++it;
    }

    return has_cmp ? (woken_count + requeued_count) : woken_count;
}

int64_t FutexManager::WakeOpImpl(uint32_t *uaddr, uint32_t *uaddr2, uint32_t val, uint32_t val2,
                                 uint32_t val3)
{
    if (!uaddr || (reinterpret_cast<uintptr_t>(uaddr) % 4 != 0) || !uaddr2 ||
        (reinterpret_cast<uintptr_t>(uaddr2) % 4 != 0)) {
        return -22; // -EINVAL
    }

    int op = (val3 >> 28) & 0xf;
    int cmp = (val3 >> 24) & 0xf;
    int oparg = (val3 >> 12) & 0xfff;
    int cmparg = val3 & 0xfff;

    if (op & LINUX_FUTEX_OP_OPARG_SHIFT) {
        oparg = 1 << oparg;
        op &= ~LINUX_FUTEX_OP_OPARG_SHIFT;
    }

    size_t b1 = BucketIndex(uaddr);
    size_t b2 = BucketIndex(uaddr2);

    std::unique_lock<std::mutex> lock1(buckets_[std::min(b1, b2)].mutex, std::defer_lock);
    std::unique_lock<std::mutex> lock2(buckets_[std::max(b1, b2)].mutex, std::defer_lock);
    if (b1 == b2) {
        lock1.lock();
    } else {
        lock1.lock();
        lock2.lock();
    }

    uint32_t oldval = __atomic_load_n(uaddr2, __ATOMIC_SEQ_CST);
    uint32_t newval = oldval;

    switch (op) {
    case LINUX_FUTEX_OP_SET:
        newval = static_cast<uint32_t>(oparg);
        break;
    case LINUX_FUTEX_OP_ADD:
        newval = oldval + static_cast<uint32_t>(oparg);
        break;
    case LINUX_FUTEX_OP_OR:
        newval = oldval | static_cast<uint32_t>(oparg);
        break;
    case LINUX_FUTEX_OP_ANDN:
        newval = oldval & ~static_cast<uint32_t>(oparg);
        break;
    case LINUX_FUTEX_OP_XOR:
        newval = oldval ^ static_cast<uint32_t>(oparg);
        break;
    default:
        return -22; // -EINVAL
    }

    __atomic_store_n(uaddr2, newval, __ATOMIC_SEQ_CST);

    bool cmp_result = false;
    switch (cmp) {
    case LINUX_FUTEX_OP_CMP_EQ:
        cmp_result = (oldval == static_cast<uint32_t>(cmparg));
        break;
    case LINUX_FUTEX_OP_CMP_NE:
        cmp_result = (oldval != static_cast<uint32_t>(cmparg));
        break;
    case LINUX_FUTEX_OP_CMP_LT:
        cmp_result = (oldval < static_cast<uint32_t>(cmparg));
        break;
    case LINUX_FUTEX_OP_CMP_LE:
        cmp_result = (oldval <= static_cast<uint32_t>(cmparg));
        break;
    case LINUX_FUTEX_OP_CMP_GT:
        cmp_result = (oldval > static_cast<uint32_t>(cmparg));
        break;
    case LINUX_FUTEX_OP_CMP_GE:
        cmp_result = (oldval >= static_cast<uint32_t>(cmparg));
        break;
    default:
        return -22; // -EINVAL
    }

    int64_t woken1 = 0;
    auto &vec1 = buckets_[b1].waiters;
    auto it1 = vec1.begin();
    while (it1 != vec1.end() && static_cast<uint32_t>(woken1) < val) {
        if ((*it1)->uaddr == uaddr) {
            (*it1)->woken = true;
            (*it1)->cv.notify_one();
            it1 = vec1.erase(it1);
            woken1++;
        } else {
            ++it1;
        }
    }

    int64_t woken2 = 0;
    if (cmp_result && val2 > 0) {
        auto &vec2 = buckets_[b2].waiters;
        auto it2 = vec2.begin();
        while (it2 != vec2.end() && static_cast<uint32_t>(woken2) < val2) {
            if ((*it2)->uaddr == uaddr2) {
                (*it2)->woken = true;
                (*it2)->cv.notify_one();
                it2 = vec2.erase(it2);
                woken2++;
            } else {
                ++it2;
            }
        }
    }

    return woken1 + woken2;
}

int64_t FutexManager::LockPiImpl(uint32_t *uaddr, const LinuxTimespec *timeout, bool trylock)
{
    if (!ValidateTimespec(timeout)) {
        return -22; // -EINVAL
    }

    uint32_t current_tid = GetCurrentFutexTid();
    size_t b_idx = BucketIndex(uaddr);

    if (trylock) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(uaddr, &expected, current_tid, false, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST)) {
            return 0;
        }
        if ((expected & LINUX_FUTEX_TID_MASK) == current_tid) {
            return -35; // -EDEADLK
        }
        return -11; // -EAGAIN
    }

    while (true) {
        auto waiter = std::make_shared<FutexWaiter>();
        waiter->uaddr = uaddr;
        waiter->bitset = LINUX_FUTEX_BITSET_MATCH_ANY;
        waiter->is_pi = true;
        waiter->tid = current_tid;
        waiter->woken = false;

        {
            std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
            uint32_t expected = 0;
            if (__atomic_compare_exchange_n(uaddr, &expected, current_tid, false, __ATOMIC_SEQ_CST,
                                            __ATOMIC_SEQ_CST)) {
                return 0;
            }
            if ((expected & LINUX_FUTEX_TID_MASK) == current_tid) {
                return -35; // -EDEADLK
            }

            // Set WAITERS bit on futex word
            uint32_t cur = expected;
            while (!(cur & LINUX_FUTEX_WAITERS)) {
                if (__atomic_compare_exchange_n(uaddr, &cur, cur | LINUX_FUTEX_WAITERS, false,
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                    break;
                }
            }

            buckets_[b_idx].waiters.push_back(waiter);
        }

        std::unique_lock<std::mutex> wait_lock(waiter->wait_mutex);
        if (!timeout) {
            waiter->cv.wait(wait_lock, [&waiter]() { return waiter->woken; });
        } else {
            auto deadline = std::chrono::steady_clock::time_point(
                std::chrono::seconds(timeout->tv_sec) + std::chrono::nanoseconds(timeout->tv_nsec));
            waiter->cv.wait_until(wait_lock, deadline, [&waiter]() { return waiter->woken; });
        }

        if (waiter->woken) {
            return 0;
        }

        {
            std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
            if (waiter->woken) {
                return 0;
            }
            auto &vec = buckets_[b_idx].waiters;
            vec.erase(std::remove(vec.begin(), vec.end(), waiter), vec.end());
            return -110; // -ETIMEDOUT
        }
    }
}

int64_t FutexManager::UnlockPiImpl(uint32_t *uaddr)
{
    uint32_t current_tid = GetCurrentFutexTid();
    size_t b_idx = BucketIndex(uaddr);

    std::lock_guard<std::mutex> lock(buckets_[b_idx].mutex);
    uint32_t cur = __atomic_load_n(uaddr, __ATOMIC_SEQ_CST);
    if ((cur & LINUX_FUTEX_TID_MASK) != current_tid) {
        return -1; // -EPERM
    }

    auto &vec = buckets_[b_idx].waiters;
    auto it = std::find_if(vec.begin(), vec.end(),
                           [uaddr](const std::shared_ptr<FutexWaiter> &w) {
                               return w->uaddr == uaddr && w->is_pi;
                           });

    if (it != vec.end()) {
        auto next_waiter = *it;
        vec.erase(it);

        size_t remaining_pi_waiters = 0;
        for (const auto &w : vec) {
            if (w->uaddr == uaddr && w->is_pi) {
                remaining_pi_waiters++;
            }
        }

        uint32_t new_val = next_waiter->tid;
        if (remaining_pi_waiters > 0) {
            new_val |= LINUX_FUTEX_WAITERS;
        }

        __atomic_store_n(uaddr, new_val, __ATOMIC_SEQ_CST);
        next_waiter->woken = true;
        next_waiter->cv.notify_one();
    } else {
        __atomic_store_n(uaddr, 0, __ATOMIC_SEQ_CST);
    }

    return 0;
}

int64_t FutexManager::Futex(uint32_t *uaddr, int futex_op, uint32_t val,
                            const LinuxTimespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
    if (!uaddr || (reinterpret_cast<uintptr_t>(uaddr) % 4 != 0)) {
        return -22; // -EINVAL
    }

    int cmd = futex_op & LINUX_FUTEX_CMD_MASK;
    bool use_realtime = (futex_op & LINUX_FUTEX_CLOCK_REALTIME) != 0;

    switch (cmd) {
    case LINUX_FUTEX_WAIT:
        return WaitImpl(uaddr, val, timeout, LINUX_FUTEX_BITSET_MATCH_ANY, false, false);

    case LINUX_FUTEX_WAIT_BITSET:
        return WaitImpl(uaddr, val, timeout, val3, true, use_realtime);

    case LINUX_FUTEX_WAKE:
        return WakeImpl(uaddr, val, LINUX_FUTEX_BITSET_MATCH_ANY);

    case LINUX_FUTEX_WAKE_BITSET:
        return WakeImpl(uaddr, val, val3);

    case LINUX_FUTEX_REQUEUE: {
        uint32_t val2 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(timeout));
        return RequeueImpl(uaddr, uaddr2, val, val2, val3, false);
    }

    case LINUX_FUTEX_CMP_REQUEUE: {
        uint32_t val2 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(timeout));
        return RequeueImpl(uaddr, uaddr2, val, val2, val3, true);
    }

    case LINUX_FUTEX_WAKE_OP: {
        uint32_t val2 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(timeout));
        return WakeOpImpl(uaddr, uaddr2, val, val2, val3);
    }

    case LINUX_FUTEX_LOCK_PI:
        return LockPiImpl(uaddr, timeout, false);

    case LINUX_FUTEX_TRYLOCK_PI:
        return LockPiImpl(uaddr, nullptr, true);

    case LINUX_FUTEX_UNLOCK_PI:
        return UnlockPiImpl(uaddr);

    default:
        return -38; // -ENOSYS
    }
}

} // namespace portage::sync
