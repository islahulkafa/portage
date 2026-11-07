#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 00
#define AT_FDCWD -100

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

static inline void sys_write(int fd, const void *buf, size_t count)
{
    __asm__ volatile("syscall" : : "a"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
}

static int contains_str(const char *buf, size_t len, const char *sub)
{
    size_t sub_len = 0;
    while (sub[sub_len])
        sub_len++;

    if (sub_len > len)
        return 0;
    for (size_t i = 0; i <= len - sub_len; i++) {
        int match = 1;
        for (size_t j = 0; j < sub_len; j++) {
            if (buf[i + j] != sub[j]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    return 0;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    char buf[512];

    // 1. Read /proc/cpuinfo
    int fd_cpu = sys_openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    if (fd_cpu < 0)
        sys_exit(1);

    int64_t r_cpu = sys_read(fd_cpu, buf, sizeof(buf));
    sys_close(fd_cpu);
    if (r_cpu <= 0 || !contains_str(buf, (size_t)r_cpu, "processor")) {
        sys_exit(2);
    }

    // 2. Read /proc/meminfo
    int fd_mem = sys_openat(AT_FDCWD, "/proc/meminfo", O_RDONLY, 0);
    if (fd_mem < 0)
        sys_exit(3);

    int64_t r_mem = sys_read(fd_mem, buf, sizeof(buf));
    sys_close(fd_mem);
    if (r_mem <= 0 || !contains_str(buf, (size_t)r_mem, "MemTotal:")) {
        sys_exit(4);
    }

    // 3. Read /proc/version
    int fd_ver = sys_openat(AT_FDCWD, "/proc/version", O_RDONLY, 0);
    if (fd_ver < 0)
        sys_exit(5);

    int64_t r_ver = sys_read(fd_ver, buf, sizeof(buf));
    sys_close(fd_ver);
    if (r_ver <= 0 || !contains_str(buf, (size_t)r_ver, "Linux version")) {
        sys_exit(6);
    }

    const char pass_msg[] = "[proc] /proc virtual files read and verified successfully\n";
    sys_write(1, pass_msg, sizeof(pass_msg) - 1);
    sys_exit(0);
}
