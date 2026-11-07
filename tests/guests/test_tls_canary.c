// Freestanding Android test for arch_prctl and %fs:0x28 stack canary
typedef unsigned long uint64_t;

struct BionicTcb {
    void *slots[16];
};

static inline void sys_exit_group(int code)
{
    __asm__ volatile("movl $231, %%eax\n\t"
                     "movl %0, %%edi\n\t"
                     "syscall\n\t"
                     :
                     : "r"(code)
                     : "rax", "rdi", "rcx", "r11");
}

void _start()
{
    struct BionicTcb tcb;
    tcb.slots[0] = &tcb.slots[0];
    tcb.slots[1] = (void *)0x12345678ULL;
    tcb.slots[5] = (void *)0xDEADBEEFCAFE0001ULL; // canary at fs:0x28

    // sys_arch_prctl(ARCH_SET_FS = 0x1002, &tcb)
    long ret = 0;
    __asm__ volatile("movl $158, %%eax\n\t"
                     "movl $0x1002, %%edi\n\t"
                     "movq %1, %%rsi\n\t"
                     "syscall\n\t"
                     "movq %%rax, %0\n\t"
                     : "=r"(ret)
                     : "r"(&tcb)
                     : "rax", "rdi", "rsi", "rdx", "rcx", "r11");

    if (ret != 0) {
        sys_exit_group(1);
    }

    // Read canary through %fs:0x28
    uint64_t read_canary = 0;
    __asm__ volatile("movq %%fs:0x28, %0\n\t" : "=r"(read_canary));

    if (read_canary != 0xDEADBEEFCAFE0001ULL) {
        sys_exit_group(2);
    }

    sys_exit_group(0);
}
