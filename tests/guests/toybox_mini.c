#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 00
#define O_DIRECTORY 0200000
#define AT_FDCWD -100

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

static inline int64_t sys_getdents64(int fd, void *dirp, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(217), "D"(fd), "S"(dirp), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

size_t strlen(const char *s)
{
    size_t l = 0;
    while (s[l])
        l++;
    return l;
}

static size_t str_len(const char *s) { return strlen(s); }

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

static void do_echo(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        sys_write(1, argv[i], str_len(argv[i]));
        if (i + 1 < argc) {
            sys_write(1, " ", 1);
        }
    }
    sys_write(1, "\n", 1);
    sys_exit(0);
}

static void do_cat(const char *path)
{
    int fd = sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        sys_exit(1);
    }
    char buf[512];
    int64_t n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        sys_write(1, buf, (size_t)n);
    }
    sys_close(fd);
    sys_exit(0);
}

static void do_ls(const char *path)
{
    int fd = sys_openat(AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        sys_exit(2);
    }
    uint8_t buf[2048];
    int64_t n;
    while ((n = sys_getdents64(fd, buf, sizeof(buf))) > 0) {
        size_t pos = 0;
        while (pos < (size_t)n) {
            struct LinuxDirent64 *d = (struct LinuxDirent64 *)(buf + pos);
            sys_write(1, d->d_name, str_len(d->d_name));
            sys_write(1, "\n", 1);
            pos += d->d_reclen;
        }
    }
    sys_close(fd);
    sys_exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        const char usage[] = "toybox_mini: usage: toybox_mini [echo|cat|ls] [args...]\n";
        sys_write(2, usage, sizeof(usage) - 1);
        return 1;
    }

    if (str_equal(argv[1], "echo")) {
        do_echo(argc, argv);
    } else if (str_equal(argv[1], "cat")) {
        if (argc < 3)
            return 1;
        do_cat(argv[2]);
    } else if (str_equal(argv[1], "ls")) {
        const char *p = (argc >= 3) ? argv[2] : "/";
        do_ls(p);
    }

    return 1;
}

__attribute__((naked)) void _start(void)
{
    __asm__ volatile("movq (%rsp), %rdi\n\t"
                     "leaq 8(%rsp), %rsi\n\t"
                     "andq $-16, %rsp\n\t"
                     "call main\n\t"
                     "movl %eax, %edi\n\t"
                     "movl $60, %eax\n\t"
                     "syscall\n\t");
}
