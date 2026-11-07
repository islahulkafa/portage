#include <stddef.h>
#include <stdint.h>

static inline int64_t sys_pipe2(int pipefd[2], int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(293), "D"(pipefd), "S"(flags)
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

static inline int64_t sys_dup3(int oldfd, int newfd, int flags)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(292), "D"(oldfd), "S"(newfd), "d"(flags)
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

__attribute__((force_align_arg_pointer)) void _start(void)
{
    int fds[2] = {-1, -1};
    int64_t prc = sys_pipe2(fds, 0);
    if (prc != 0 || fds[0] < 0 || fds[1] < 0) {
        sys_exit(1);
    }

    const char test_payload[] = "hello_portage_pipe_data";
    int64_t written = sys_write(fds[1], test_payload, sizeof(test_payload));
    if (written != sizeof(test_payload)) {
        sys_exit(2);
    }

    // Duplicate read end to fd 77 via dup3
    int64_t dup_res = sys_dup3(fds[0], 77, 0);
    if (dup_res != 77) {
        sys_exit(3);
    }

    char read_buf[64] = {0};
    int64_t nread = sys_read(77, read_buf, sizeof(read_buf));
    if (nread != sizeof(test_payload)) {
        sys_exit(4);
    }

    for (size_t i = 0; i < sizeof(test_payload); ++i) {
        if (read_buf[i] != test_payload[i]) {
            sys_exit(5);
        }
    }

    sys_close(fds[0]);
    sys_close(fds[1]);
    sys_close(77);

    const char pass_msg[] = "[pipe] anonymous pipe2 and dup3 verified successfully\n";
    sys_write(1, pass_msg, sizeof(pass_msg) - 1);
    sys_exit(0);
}
