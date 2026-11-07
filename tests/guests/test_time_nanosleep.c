#include <stddef.h>
#include <stdint.h>

#define CLOCK_MONOTONIC 1

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline int64_t sys_clock_gettime(int clockid, struct timespec *tp)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(228), "D"(clockid), "S"(tp)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_clock_getres(int clockid, struct timespec *tp)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(229), "D"(clockid), "S"(tp)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(35), "D"(req), "S"(rem) : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

void _start(void)
{
    struct timespec res = {0};
    if (sys_clock_getres(CLOCK_MONOTONIC, &res) != 0) {
        sys_exit(1);
    }

    struct timespec t1 = {0};
    if (sys_clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        sys_exit(2);
    }

    struct timespec req = {0};
    req.tv_sec = 0;
    req.tv_nsec = 50000000; // 50 milliseconds

    if (sys_nanosleep(&req, NULL) != 0) {
        sys_exit(3);
    }

    struct timespec t2 = {0};
    if (sys_clock_gettime(CLOCK_MONOTONIC, &t2) != 0) {
        sys_exit(4);
    }

    int64_t elapsed_ms = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_nsec - t1.tv_nsec) / 1000000;
    if (elapsed_ms < 30 || elapsed_ms > 2000) {
        sys_exit(5);
    }

    sys_exit(0);
}
