// Freestanding Android test for brk, mmap, and munmap
typedef unsigned long uint64_t;

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
    // 1. Query initial brk
    uint64_t initial_brk = 0;
    __asm__ volatile("movl $12, %%eax\n\t" // sys_brk
                     "xorl %%edi, %%edi\n\t"
                     "syscall\n\t"
                     "movq %%rax, %0\n\t"
                     : "=r"(initial_brk)
                     :
                     : "rax", "rdi", "rcx", "r11");

    if (initial_brk == 0) {
        sys_exit_group(1);
    }

    // 2. Expand brk by 4096 bytes
    uint64_t target_brk = initial_brk + 4096;
    uint64_t new_brk = 0;
    __asm__ volatile("movl $12, %%eax\n\t"
                     "movq %1, %%rdi\n\t"
                     "syscall\n\t"
                     "movq %%rax, %0\n\t"
                     : "=r"(new_brk)
                     : "r"(target_brk)
                     : "rax", "rdi", "rcx", "r11");

    if (new_brk != target_brk) {
        sys_exit_group(2);
    }

    // Write into heap memory
    volatile uint64_t *heap_ptr = (volatile uint64_t *)initial_brk;
    *heap_ptr = 0xCAFEBABE12345678ULL;
    if (*heap_ptr != 0xCAFEBABE12345678ULL) {
        sys_exit_group(3);
    }

    // 3. Test mmap anonymous (prot = PROT_READ|PROT_WRITE = 3, flags = MAP_PRIVATE|MAP_ANONYMOUS =
    // 0x22)
    uint64_t mmap_res = 0;
    __asm__ volatile("movl $9, %%eax\n\t"     // sys_mmap
                     "xorl %%edi, %%edi\n\t"  // addr = NULL
                     "movl $4096, %%esi\n\t"  // len = 4096
                     "movl $3, %%edx\n\t"     // prot = PROT_READ|PROT_WRITE
                     "movl $0x22, %%r10d\n\t" // flags = MAP_PRIVATE|MAP_ANON
                     "movl $-1, %%r8d\n\t"    // fd = -1
                     "xorl %%r9d, %%r9d\n\t"  // offset = 0
                     "syscall\n\t"
                     "movq %%rax, %0\n\t"
                     : "=r"(mmap_res)
                     :
                     : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11");

    if ((long)mmap_res < 0) {
        sys_exit_group(4);
    }

    // Write into mmapped memory
    volatile uint64_t *mmap_ptr = (volatile uint64_t *)mmap_res;
    *mmap_ptr = 0x1122334455667788ULL;
    if (*mmap_ptr != 0x1122334455667788ULL) {
        sys_exit_group(5);
    }

    // 4. Test munmap
    long munmap_res = 0;
    __asm__ volatile("movl $11, %%eax\n\t"
                     "movq %1, %%rdi\n\t"
                     "movl $4096, %%esi\n\t"
                     "syscall\n\t"
                     "movq %%rax, %0\n\t"
                     : "=r"(munmap_res)
                     : "r"(mmap_res)
                     : "rax", "rdi", "rsi", "rcx", "r11");

    if (munmap_res != 0) {
        sys_exit_group(6);
    }

    // Success
    sys_exit_group(0);
}
