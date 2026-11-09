#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 00000000
#define O_NONBLOCK 00004000

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03

#define SYN_REPORT 0
#define KEY_A 30
#define BTN_TOUCH 0x14a

#define ABS_MT_SLOT 0x2f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39

#define EPOLL_CTL_ADD 1
#define EPOLLIN 0x00000001

#define LINUX_IOC_NRBITS 8
#define LINUX_IOC_TYPEBITS 8
#define LINUX_IOC_SIZEBITS 14
#define LINUX_IOC_DIRBITS 2

#define LINUX_IOC_NRSHIFT 0
#define LINUX_IOC_TYPESHIFT (LINUX_IOC_NRSHIFT + LINUX_IOC_NRBITS)
#define LINUX_IOC_SIZESHIFT (LINUX_IOC_TYPESHIFT + LINUX_IOC_TYPEBITS)
#define LINUX_IOC_DIRSHIFT (LINUX_IOC_SIZESHIFT + LINUX_IOC_SIZEBITS)
#define LINUX_IOC_READ 2u

#define LINUX_IOC(dir, type, nr, size)                                                             \
    (((uint32_t)(dir) << LINUX_IOC_DIRSHIFT) | ((uint32_t)(type) << LINUX_IOC_TYPESHIFT) |         \
     ((uint32_t)(nr) << LINUX_IOC_NRSHIFT) | ((uint32_t)(size) << LINUX_IOC_SIZESHIFT))

#define LINUX_IOR(type, nr, size) LINUX_IOC(LINUX_IOC_READ, type, nr, size)

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct input_id {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

struct input_absinfo {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((__packed__));

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(mode)
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

static inline int64_t sys_ioctl(int fd, uint32_t cmd, void *arg)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(16), "D"(fd), "S"(cmd), "d"(arg)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_epoll_create1(int flags)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(291), "D"(flags) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    int64_t ret;
    register struct epoll_event *r10 __asm__("r10") = event;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(233), "D"(epfd), "S"(op), "d"(fd), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
                                     int timeout)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = timeout;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(232), "D"(epfd), "S"(events), "d"(maxevents), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

static size_t str_len(const char *s) { return strlen(s); }

static int str_contains(const char *haystack, const char *needle)
{
    size_t h_len = str_len(haystack);
    size_t n_len = str_len(needle);
    if (n_len > h_len)
        return 0;
    for (size_t i = 0; i <= h_len - n_len; ++i) {
        int match = 1;
        for (size_t j = 0; j < n_len; ++j) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    return 0;
}

void *memset(void *dest, int val, size_t len)
{
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < len; ++i) {
        p[i] = (uint8_t)val;
    }
    return dest;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    // Open /dev/input/event0 (keyboard) and /dev/input/event1 (touchscreen)
    const char dev0_path[] = "/dev/input/event0";
    const char dev1_path[] = "/dev/input/event1";

    int fd0 = (int)sys_openat(-100, dev0_path, O_RDONLY | O_NONBLOCK, 0);
    if (fd0 < 0) {
        sys_exit(1);
    }

    int fd1 = (int)sys_openat(-100, dev1_path, O_RDONLY | O_NONBLOCK, 0);
    if (fd1 < 0) {
        sys_exit(2);
    }

    // Query evdev version on event0
    int version = 0;
    uint32_t cmd_version = LINUX_IOR('E', 0x01, sizeof(int));
    if (sys_ioctl(fd0, cmd_version, &version) != 0 || version != 0x010001) {
        sys_exit(3);
    }

    // Query input_id on event0
    struct input_id id0;
    memset(&id0, 0, sizeof(id0));
    uint32_t cmd_id = LINUX_IOR('E', 0x02, sizeof(struct input_id));
    if (sys_ioctl(fd0, cmd_id, &id0) != 0 || id0.vendor != 0x18d1 || id0.product != 0x0001) {
        sys_exit(4);
    }

    // Query device names
    char name0[64];
    memset(name0, 0, sizeof(name0));
    uint32_t cmd_name0 = LINUX_IOC(LINUX_IOC_READ, 'E', 0x06, sizeof(name0));
    if (sys_ioctl(fd0, cmd_name0, name0) <= 0 || !str_contains(name0, "Keyboard")) {
        sys_exit(5);
    }

    char name1[64];
    memset(name1, 0, sizeof(name1));
    uint32_t cmd_name1 = LINUX_IOC(LINUX_IOC_READ, 'E', 0x06, sizeof(name1));
    if (sys_ioctl(fd1, cmd_name1, name1) <= 0 || !str_contains(name1, "Touchscreen")) {
        sys_exit(6);
    }

    // Query absinfo for ABS_MT_POSITION_X on event1
    struct input_absinfo abs_x;
    memset(&abs_x, 0, sizeof(abs_x));
    uint32_t cmd_abs_x = LINUX_IOR('E', 0x40 + ABS_MT_POSITION_X, sizeof(struct input_absinfo));
    if (sys_ioctl(fd1, cmd_abs_x, &abs_x) != 0 || abs_x.maximum != 32767) {
        sys_exit(7);
    }

    // Setup epoll to wait for input on event0
    int epfd = (int)sys_epoll_create1(0);
    if (epfd < 0) {
        sys_exit(8);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data = 1001;
    if (sys_epoll_ctl(epfd, EPOLL_CTL_ADD, fd0, &ev) != 0) {
        sys_exit(9);
    }

    // Wait up to 3000 ms for event0 readiness
    struct epoll_event ready_events[4];
    int n = (int)sys_epoll_wait(epfd, ready_events, 4, 3000);
    if (n <= 0) {
        sys_exit(10);
    }

    // Read events from event0
    struct input_event key_events[8];
    int64_t bytes = sys_read(fd0, key_events, sizeof(key_events));
    if (bytes < (int64_t)sizeof(struct input_event)) {
        sys_exit(11);
    }

    int received_key_a = 0;
    size_t event_count = (size_t)bytes / sizeof(struct input_event);
    for (size_t i = 0; i < event_count; ++i) {
        if (key_events[i].type == EV_KEY && key_events[i].code == KEY_A &&
            key_events[i].value == 1) {
            received_key_a = 1;
            break;
        }
    }
    if (!received_key_a) {
        sys_exit(12);
    }

    // Read pointer events from event1
    struct input_event touch_events[16];
    int64_t t_bytes = sys_read(fd1, touch_events, sizeof(touch_events));
    if (t_bytes < (int64_t)sizeof(struct input_event)) {
        sys_exit(13);
    }

    int received_touch = 0;
    size_t t_count = (size_t)t_bytes / sizeof(struct input_event);
    for (size_t i = 0; i < t_count; ++i) {
        if (touch_events[i].type == EV_KEY && touch_events[i].code == BTN_TOUCH &&
            touch_events[i].value == 1) {
            received_touch = 1;
            break;
        }
    }
    if (!received_touch) {
        sys_exit(14);
    }

    sys_close(epfd);
    sys_close(fd0);
    sys_close(fd1);

    sys_exit(0);
}
