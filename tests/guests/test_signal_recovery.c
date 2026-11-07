#include <stddef.h>
#include <stdint.h>

#define SIGSEGV 11
#define SA_SIGINFO 4

struct LinuxSigaction {
    uintptr_t sa_handler;
    uint64_t sa_flags;
    uintptr_t sa_restorer;
    uint64_t sa_mask;
};

struct LinuxSiginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t __pad0;
    uint64_t si_addr;
};

struct LinuxMcontext {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
};

struct LinuxUcontext {
    uint64_t uc_flags;
    void *uc_link;
    uint8_t uc_stack[24];
    struct LinuxMcontext uc_mcontext;
};

static inline int64_t sys_rt_sigaction(int sig, const struct LinuxSigaction *act,
                                       struct LinuxSigaction *oact, size_t sigsetsize)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(13), "D"(sig), "S"(act), "d"(oact), "r"(sigsetsize)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_rt_sigreturn(void)
{
    __asm__ volatile("mov $15, %%rax\n\t"
                     "syscall\n\t"
                     :
                     :
                     : "memory");
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static inline void sys_write(int fd, const void *buf, size_t count)
{
    __asm__ volatile("syscall" : : "a"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
}

static volatile int g_signal_caught = 0;

__attribute__((force_align_arg_pointer)) void sigsegv_handler(int sig, struct LinuxSiginfo *info,
                                                              void *ucontext_raw)
{
    (void)info;
    if (sig == SIGSEGV) {
        g_signal_caught = 1;
        const char msg[] = "[signal] SIGSEGV caught in guest handler\n";
        sys_write(1, msg, sizeof(msg) - 1);

        struct LinuxUcontext *uc = (struct LinuxUcontext *)ucontext_raw;
        // Advance RIP past the 6-byte faulting instruction: movl $42, (%rax) -> c7 00 2a 00 00 00
        uc->uc_mcontext.rip += 6;
    }
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    struct LinuxSigaction act;
    __builtin_memset(&act, 0, sizeof(act));
    act.sa_handler = (uintptr_t)sigsegv_handler;
    act.sa_flags = SA_SIGINFO;

    int64_t res = sys_rt_sigaction(SIGSEGV, &act, NULL, 8);
    if (res != 0) {
        sys_exit(2);
    }

    // Trigger intentional SEGV on unmapped address
    uintptr_t bad_addr = 0x55550000;
    __asm__ volatile("movq %0, %%rax\n\t"
                     // 6-byte instruction: c7 00 2a 00 00 00 (movl $42, (%rax))
                     ".byte 0xc7, 0x00, 0x2a, 0x00, 0x00, 0x00\n\t"
                     :
                     : "r"(bad_addr)
                     : "rax", "memory");

    if (g_signal_caught == 1) {
        const char ok_msg[] = "[signal] execution resumed past fault\n";
        sys_write(1, ok_msg, sizeof(ok_msg) - 1);
        sys_exit(0);
    }

    sys_exit(1);
}
