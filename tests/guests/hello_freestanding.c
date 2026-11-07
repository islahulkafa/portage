// Minimal freestanding Android x86_64 executable
void _start()
{
    const char *msg = "Hello from Android x86_64 on Windows 11!\n";
    __asm__ volatile("movl $1, %%eax\n\t"  // sys_write
                     "movl $1, %%edi\n\t"  // stdout
                     "movq %0, %%rsi\n\t"  // buf
                     "movl $41, %%edx\n\t" // len
                     "syscall\n\t"
                     "movl $231, %%eax\n\t" // sys_exit_group
                     "movl $42, %%edi\n\t"  // exit_code 42
                     "syscall\n\t"
                     :
                     : "r"(msg)
                     : "rax", "rdi", "rsi", "rdx", "rcx", "r11");
}
