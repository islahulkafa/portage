#include <stddef.h>
#include <stdint.h>

#define EPOLLIN 0x001
#define EPOLL_CTL_ADD 1

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((__packed__));

static inline int64_t sys_epoll_create1(int flags)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(291), "D"(flags) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_pipe2(int pipefd[2], int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(293), "D"(pipefd), "S"(flags)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_epoll_ctl(int epfd, int op, int fd, const struct epoll_event *event)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(233), "D"(epfd), "S"(op), "d"(fd), "r"(event)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
                                     int timeout)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(232), "D"(epfd), "S"(events), "d"(maxevents), "r"(timeout)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_read(int fd, void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(0), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_write(int fd, const void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

void _start(void)
{
    int epfd = sys_epoll_create1(0);
    if (epfd < 0) {
        sys_exit(1);
    }

    int pfd[2] = {-1, -1};
    if (sys_pipe2(pfd, 0) < 0) {
        sys_exit(2);
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data = 0xABCD1234ULL;

    if (sys_epoll_ctl(epfd, EPOLL_CTL_ADD, pfd[0], &ev) < 0) {
        sys_exit(3);
    }

    const char msg[] = "HELLO";
    if (sys_write(pfd[1], msg, 5) != 5) {
        sys_exit(4);
    }

    struct epoll_event out_events[4] = {{0}};
    int ready = sys_epoll_wait(epfd, out_events, 4, 1000);
    if (ready != 1) {
        sys_exit(5);
    }

    if (!(out_events[0].events & EPOLLIN)) {
        sys_exit(6);
    }

    if (out_events[0].data != 0xABCD1234ULL) {
        sys_exit(7);
    }

    char buf[8] = {0};
    if (sys_read(pfd[0], buf, 5) != 5) {
        sys_exit(8);
    }

    if (buf[0] != 'H' || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'L' || buf[4] != 'O') {
        sys_exit(9);
    }

    sys_close(pfd[0]);
    sys_close(pfd[1]);
    sys_close(epfd);
    sys_exit(0);
}
