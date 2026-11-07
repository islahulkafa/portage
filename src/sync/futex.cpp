#include "sync/futex.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <synchapi.h>
#include <windows.h>

namespace portage::sync {

int64_t FutexManager::Futex(uint32_t *uaddr, int futex_op, uint32_t val,
                            const LinuxTimespec *timeout, [[maybe_unused]] uint32_t *uaddr2,
                            [[maybe_unused]] uint32_t val3)
{
    if (!uaddr || (reinterpret_cast<uintptr_t>(uaddr) % 4 != 0)) {
        return -22; // -EINVAL
    }

    int cmd = futex_op & LINUX_FUTEX_CMD_MASK;

    switch (cmd) {
    case LINUX_FUTEX_WAIT:
    case LINUX_FUTEX_WAIT_BITSET: {
        if (cmd == LINUX_FUTEX_WAIT_BITSET && val3 == 0) {
            return -22; // -EINVAL
        }

        DWORD timeout_ms = INFINITE;
        if (timeout) {
            if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000) {
                return -22; // -EINVAL
            }
            uint64_t ms = static_cast<uint64_t>(timeout->tv_sec) * 1000 +
                          (static_cast<uint64_t>(timeout->tv_nsec) + 999999) / 1000000;
            if (ms > MAXDWORD) {
                timeout_ms = INFINITE;
            } else {
                timeout_ms = static_cast<DWORD>(ms);
            }
        }

        // In Linux semantics, before sleeping, check if *uaddr == val.
        // If *uaddr != val, return -EAGAIN immediately.
        uint32_t current_val = *uaddr;
        if (current_val != val) {
            return -11; // -EAGAIN
        }

        BOOL ok = WaitOnAddress(uaddr, &val, sizeof(uint32_t), timeout_ms);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_TIMEOUT) {
                return -110; // -ETIMEDOUT
            }
        }
        return 0;
    }

    case LINUX_FUTEX_WAKE:
    case LINUX_FUTEX_WAKE_BITSET: {
        if (cmd == LINUX_FUTEX_WAKE_BITSET && val3 == 0) {
            return -22; // -EINVAL
        }

        if (val <= 0) {
            return 0;
        }

        if (val == 1) {
            WakeByAddressSingle(uaddr);
            return 1;
        }

        WakeByAddressAll(uaddr);
        return static_cast<int64_t>(val);
    }

    default:
        return -38; // -ENOSYS
    }
}

} // namespace portage::sync
