#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 00
#define O_DIRECTORY 0200000
#define AT_FDCWD -100

#define DT_DIR 4

#pragma pack(push, 1)
struct LinuxDirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[1];
};
#pragma pack(pop)

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(mode)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_getdents64(int fd, void *dirp, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(217), "D"(fd), "S"(dirp), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_mkdirat(int dfd, const char *path, int mode)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(258), "D"(dfd), "S"(path), "d"(mode)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_unlinkat(int dfd, const char *path, int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(263), "D"(dfd), "S"(path), "d"(flags)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_faccessat(int dfd, const char *path, int mode, int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(269), "D"(dfd), "S"(path), "d"(mode), "r"(flags)
                     : "rcx", "r11", "memory");
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

static int str_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    int dfd = sys_openat(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY, 0);
    if (dfd < 0) {
        sys_exit(1);
    }

    uint8_t buf[2048];
    int64_t nread = sys_getdents64(dfd, buf, sizeof(buf));
    if (nread <= 0) {
        sys_exit(2);
    }

    int found_dot = 0;
    int found_dotdot = 0;

    size_t pos = 0;
    while (pos < (size_t)nread) {
        struct LinuxDirent64 *d = (struct LinuxDirent64 *)(buf + pos);
        if (str_equal(d->d_name, "."))
            found_dot = 1;
        if (str_equal(d->d_name, ".."))
            found_dotdot = 1;
        pos += d->d_reclen;
    }

    sys_close(dfd);

    if (!found_dot || !found_dotdot) {
        sys_exit(3);
    }

    // Test mkdirat, faccessat, and unlinkat
    const char dir_name[] = "portage_tmp_dir";
    if (sys_mkdirat(AT_FDCWD, dir_name, 0755) != 0) {
        sys_exit(4);
    }

    if (sys_faccessat(AT_FDCWD, dir_name, 0, 0) != 0) {
        sys_exit(5);
    }

    if (sys_unlinkat(AT_FDCWD, dir_name, 0) != 0) {
        sys_exit(6);
    }

    const char pass_msg[] = "[dirent] directory listing and metadata operations verified\n";
    sys_write(1, pass_msg, sizeof(pass_msg) - 1);
    sys_exit(0);
}
