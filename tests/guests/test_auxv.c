// Freestanding Android test verifying argc, argv, envp, and auxv on the stack
typedef unsigned long uint64_t;
typedef unsigned long size_t;

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
    // SysV AMD64 entry: %rsp points to argc
    uint64_t *sp;
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));

    uint64_t argc = *sp;
    if (argc < 1) {
        sys_exit_group(1);
    }

    uint64_t *argv = sp + 1;
    if (argv[argc] != 0) {
        sys_exit_group(2);
    }

    uint64_t *envp = argv + argc + 1;
    size_t env_count = 0;
    while (envp[env_count] != 0) {
        env_count++;
    }

    uint64_t *auxv = envp + env_count + 1;

    int found_random = 0;
    int found_pagesz = 0;

    for (size_t i = 0; auxv[i] != 0; i += 2) {
        uint64_t tag = auxv[i];
        uint64_t val = auxv[i + 1];
        if (tag == 6 && val == 4096) {
            found_pagesz = 1;
        } else if (tag == 25 && val != 0) {
            found_random = 1;
        }
    }

    if (!found_pagesz || !found_random) {
        sys_exit_group(3);
    }

    sys_exit_group(0);
}
