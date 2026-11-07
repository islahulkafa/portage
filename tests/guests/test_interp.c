#include <stddef.h>
#include <stdint.h>

#define AT_NULL 0
#define AT_PHDR 3
#define AT_ENTRY 9
#define AT_BASE 7

static inline int64_t sys_write(int fd, const void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_openat(int dfd, const char *path, int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(0)
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

uintptr_t c_interp_main(uint64_t *sp);

__attribute__((naked)) void interp_entry(void)
{
    __asm__ volatile("movq %rsp, %rdi\n\t"
                     "call c_interp_main\n\t"
                     // Jump to returned app entry point
                     "jmpq *%rax\n\t");
}

uintptr_t c_interp_main(uint64_t *sp)
{
    const char msg1[] = "[interp] dynamic linker loaded\n";
    sys_write(1, msg1, sizeof(msg1) - 1);

    uint64_t argc = *sp;
    sp++; // past argc

    // Skip argv
    while (*sp != 0) {
        sp++;
    }
    sp++; // skip NULL

    // Skip envp
    while (*sp != 0) {
        sp++;
    }
    sp++; // skip NULL

    // auxv pairs
    uintptr_t app_entry = 0;
    while (*sp != AT_NULL) {
        uint64_t key = *sp++;
        uint64_t val = *sp++;
        if (key == AT_ENTRY) {
            app_entry = val;
        }
    }

    // Verify VFS openat and read of shared library
    int64_t fd = sys_openat(-100, "/system/lib64/test_lib.so", 0);
    if (fd >= 0) {
        char elf_header[4];
        if (sys_read(fd, elf_header, 4) == 4) {
            if (elf_header[0] == 0x7f && elf_header[1] == 'E' && elf_header[2] == 'L' &&
                elf_header[3] == 'F') {
                const char msg2[] = "[interp] /system/lib64/test_lib.so verified\n";
                sys_write(1, msg2, sizeof(msg2) - 1);
            }
        }
        sys_close(fd);
    }

    if (app_entry == 0) {
        sys_exit(99);
    }

    const char msg3[] = "[interp] handing over to application\n";
    sys_write(1, msg3, sizeof(msg3) - 1);

    return app_entry;
}
