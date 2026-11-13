#pragma once

#include <cstdint>

namespace portage::process {

constexpr uint64_t LINUX_CLONE_NEWNS = 0x00020000;
constexpr uint64_t LINUX_CLONE_NEWCGROUP = 0x02000000;
constexpr uint64_t LINUX_CLONE_NEWUTS = 0x04000000;
constexpr uint64_t LINUX_CLONE_NEWIPC = 0x08000000;
constexpr uint64_t LINUX_CLONE_NEWUSER = 0x10000000;
constexpr uint64_t LINUX_CLONE_NEWPID = 0x20000000;
constexpr uint64_t LINUX_CLONE_NEWNET = 0x40000000;

struct LinuxCloneArgs {
    uint64_t flags{0};
    uint64_t pidfd{0};
    uint64_t child_tid{0};
    uint64_t parent_tid{0};
    uint64_t exit_signal{0};
    uint64_t stack{0};
    uint64_t stack_size{0};
    uint64_t tls{0};
    uint64_t set_tid{0};
    uint64_t set_tid_size{0};
    uint64_t cgroup{0};
};

} // namespace portage::process
