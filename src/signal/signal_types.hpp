#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::signal {

constexpr int LINUX_SIGHUP = 1;
constexpr int LINUX_SIGINT = 2;
constexpr int LINUX_SIGQUIT = 3;
constexpr int LINUX_SIGILL = 4;
constexpr int LINUX_SIGTRAP = 5;
constexpr int LINUX_SIGABRT = 6;
constexpr int LINUX_SIGBUS = 7;
constexpr int LINUX_SIGFPE = 8;
constexpr int LINUX_SIGKILL = 9;
constexpr int LINUX_SIGUSR1 = 10;
constexpr int LINUX_SIGSEGV = 11;
constexpr int LINUX_SIGUSR2 = 12;
constexpr int LINUX_SIGPIPE = 13;
constexpr int LINUX_SIGALRM = 14;
constexpr int LINUX_SIGTERM = 15;
constexpr int LINUX_SIGCHLD = 17;
constexpr int LINUX_SIGCONT = 18;
constexpr int LINUX_SIGSTOP = 19;
constexpr int LINUX_SIGTSTP = 20;
constexpr int LINUX_SIGRTMIN = 32;
constexpr int LINUX_SIGRTMAX = 64;

constexpr int LINUX_SFD_CLOEXEC = 02000000;
constexpr int LINUX_SFD_NONBLOCK = 04000;

constexpr int LINUX_SI_USER = 0;
constexpr int LINUX_SI_QUEUE = -1;
constexpr int LINUX_SI_TIMER = -2;

constexpr uint64_t LINUX_SA_NOCLDSTOP = 0x00000001;
constexpr uint64_t LINUX_SA_NOCLDWAIT = 0x00000002;
constexpr uint64_t LINUX_SA_SIGINFO = 0x00000004;
constexpr uint64_t LINUX_SA_ONSTACK = 0x08000000;
constexpr uint64_t LINUX_SA_RESTART = 0x10000000;
constexpr uint64_t LINUX_SA_NODEFER = 0x40000000;
constexpr uint64_t LINUX_SA_RESTORER = 0x04000000;

constexpr int LINUX_SIG_BLOCK = 0;
constexpr int LINUX_SIG_UNBLOCK = 1;
constexpr int LINUX_SIG_SETMASK = 2;

constexpr uintptr_t LINUX_SIG_DFL = 0;
constexpr uintptr_t LINUX_SIG_IGN = 1;

struct LinuxSigaction {
    uintptr_t sa_handler{0};
    uint64_t sa_flags{0};
    uintptr_t sa_restorer{0};
    uint64_t sa_mask{0};
};

struct LinuxSiginfo {
    int32_t si_signo{0};
    int32_t si_errno{0};
    int32_t si_code{0};
    int32_t __pad0{0};
    union {
        struct {
            uintptr_t si_addr;
        } sigfault;
        struct {
            int32_t si_pid;
            uint32_t si_uid;
        } sigchld;
        struct {
            int32_t si_pid;
            uint32_t si_uid;
            union {
                int32_t sival_int;
                void *sival_ptr;
            } si_sigval;
        } rt;
        uint8_t _pad[112];
    } _sifields;
};

struct LinuxSignalfdSiginfo {
    uint32_t ssi_signo{0};
    int32_t ssi_errno{0};
    int32_t ssi_code{0};
    uint32_t ssi_pid{0};
    uint32_t ssi_uid{0};
    int32_t ssi_fd{0};
    uint32_t ssi_tid{0};
    uint32_t ssi_band{0};
    uint32_t ssi_overrun{0};
    uint32_t ssi_trapno{0};
    int32_t ssi_status{0};
    int32_t ssi_int{0};
    uint64_t ssi_ptr{0};
    uint64_t ssi_utime{0};
    uint64_t ssi_stime{0};
    uint64_t ssi_addr{0};
    uint16_t ssi_addr_lsb{0};
    uint16_t __pad2{0};
    int32_t ssi_syscall{0};
    uint64_t ssi_call_addr{0};
    uint32_t ssi_arch{0};
    uint8_t __pad[28]{0};
};
static_assert(sizeof(LinuxSignalfdSiginfo) == 128,
              "LinuxSignalfdSiginfo must be exactly 128 bytes");

struct LinuxStackT {
    void *ss_sp{nullptr};
    int32_t ss_flags{0};
    int32_t __pad{0};
    size_t ss_size{0};
};

struct LinuxMcontext {
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t r10{0};
    uint64_t r11{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};
    uint64_t rdi{0};
    uint64_t rsi{0};
    uint64_t rbp{0};
    uint64_t rbx{0};
    uint64_t rdx{0};
    uint64_t rax{0};
    uint64_t rcx{0};
    uint64_t rsp{0};
    uint64_t rip{0};
    uint64_t eflags{0};
    uint16_t cs{0};
    uint16_t gs{0};
    uint16_t fs{0};
    uint16_t __pad0{0};
    uint64_t err{0};
    uint64_t trapno{0};
    uint64_t oldmask{0};
    uint64_t cr2{0};
    void *fpregs{nullptr};
    uint64_t __reserved1[8]{0};
};

struct LinuxUcontext {
    uint64_t uc_flags{0};
    LinuxUcontext *uc_link{nullptr};
    LinuxStackT uc_stack{};
    LinuxMcontext uc_mcontext{};
    uint64_t uc_sigmask{0};
};

// Signal frame pushed onto guest stack when invoking sa_handler/sa_sigaction
struct LinuxRtSigframe {
    uintptr_t pretcode; // pointer to restorer / return trampoline
    LinuxUcontext uc;
    LinuxSiginfo info;
};

} // namespace portage::signal
