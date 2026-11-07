#include <stddef.h>
#include <stdint.h>

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_CHILD_CLEARTID 0x00200000

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static inline int64_t sys_clone(uint64_t flags, void *child_stack, int *ptid, int *ctid,
                                unsigned long tls)
{
    int64_t ret;
    register int *r10 __asm__("r10") = ctid;
    register unsigned long r8 __asm__("r8") = tls;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(56), "D"(flags), "S"(child_stack), "d"(ptid), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_futex(int *uaddr, int op, int val, const void *timeout)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(202), "D"(uaddr), "S"(op), "d"(val), "r"(timeout), "r"(0), "r"(0)
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

static volatile int g_thread_done = 0;
static uint8_t g_child_stack[65536] __attribute__((aligned(16)));

__attribute__((force_align_arg_pointer)) void _start(void)
{
    int child_tid = 9999;
    uint64_t clone_flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_CHILD_CLEARTID;

    // Stack grows downwards, top is at end of array
    void *stack_top = g_child_stack + sizeof(g_child_stack);

    int64_t ret = sys_clone(clone_flags, stack_top, NULL, &child_tid, 0);

    if (ret == 0) {
        // Child thread execution path
        const char msg[] = "[thread] child thread running\n";
        sys_write(1, msg, sizeof(msg) - 1);
        g_thread_done = 1;
        sys_exit(0);
    }

    if (ret < 0) {
        // Clone failed
        sys_exit(1);
    }

    // Parent thread execution path: wait for child thread termination
    const char pmsg[] = "[thread] parent waiting on futex\n";
    sys_write(1, pmsg, sizeof(pmsg) - 1);

    while (child_tid != 0) {
        sys_futex(&child_tid, FUTEX_WAIT, child_tid, NULL);
    }

    if (g_thread_done == 1) {
        const char donemsg[] = "[thread] thread joined successfully\n";
        sys_write(1, donemsg, sizeof(donemsg) - 1);
        sys_exit(0);
    }

    sys_exit(2);
}
