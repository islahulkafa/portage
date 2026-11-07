#include <stddef.h>
#include <stdint.h>

static inline void sys_write(int fd, const void *buf, size_t count)
{
    __asm__ volatile("syscall" : : "a"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    const char msg[] = "[app] hello from dynamic app!\n";
    sys_write(1, msg, sizeof(msg) - 1);
    sys_exit(0);
}
