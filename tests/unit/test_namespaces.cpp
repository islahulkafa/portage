#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "process/namespace_manager.hpp"
#include "process/namespace_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <string>

using namespace portage::process;
using namespace portage::syscall;
using namespace portage::ndk;
using namespace portage::fs;
using namespace portage::thread;

class NamespaceTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override { SyscallDispatcher::Shutdown(); }

    portage::mem::AddressSpace as_;
    Vfs vfs_;
    FileDescriptorTable fd_table_;
};

TEST_F(NamespaceTest, PidNamespaceIsolationAndPid1)
{
    auto &ns_mgr = NamespaceManager::Instance();
    auto root_pid_ns = ns_mgr.GetRootPidNamespace();
    ASSERT_NE(root_pid_ns, nullptr);

    auto child_ns = ns_mgr.CreateChildPidNamespace(root_pid_ns);
    ASSERT_NE(child_ns, nullptr);

    int local1 = child_ns->AllocatePid(5001);
    int local2 = child_ns->AllocatePid(5002);

    EXPECT_EQ(local1, 1);
    EXPECT_EQ(local2, 2);

    EXPECT_EQ(child_ns->GetGlobalPid(1), 5001);
    EXPECT_EQ(child_ns->GetGlobalPid(2), 5002);

    // Root namespace also knows about 5001 and 5002, with distinct parent-namespace PIDs
    int parent_view_1 = root_pid_ns->GetLocalPid(5001);
    int parent_view_2 = root_pid_ns->GetLocalPid(5002);
    EXPECT_GT(parent_view_1, 1);
    EXPECT_GT(parent_view_2, parent_view_1);
}

TEST_F(NamespaceTest, UnshareNewPidStagesForNextChild)
{
    int64_t pid_before = SyscallDispatcher::Dispatch(39 /* SYS_getpid */, 0, 0, 0, 0, 0, 0);
    EXPECT_GT(pid_before, 0);

    EXPECT_EQ(portage_unshare(static_cast<int>(LINUX_CLONE_NEWPID)), 0);

    // Calling thread's own PID must remain unchanged
    int64_t pid_after = SyscallDispatcher::Dispatch(39 /* SYS_getpid */, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(pid_after, pid_before);

    auto caller = ThreadManager::Instance().GetCurrentGuestThread();
    ASSERT_NE(caller, nullptr);
    ASSERT_NE(caller->next_child_pid_ns, nullptr);

    // Spawn child process via clone3 with stack=0, rip=0 (safe no-op thread exit)
    LinuxCloneArgs args{};
    args.flags = 0;
    int64_t child_tid = SyscallDispatcher::Dispatch(
        435 /* SYS_clone3 */, reinterpret_cast<uint64_t>(&args), sizeof(args), 0, 0, 0, 0);
    ASSERT_GT(child_tid, 0);

    auto child_thread = ThreadManager::Instance().GetThread(static_cast<int>(child_tid));
    ASSERT_NE(child_thread, nullptr);
    ASSERT_EQ(child_thread->pid_ns, caller->next_child_pid_ns);
    EXPECT_EQ(child_thread->pid_ns->GetLocalPid(child_thread->tgid), 1);
}

TEST_F(NamespaceTest, MountNamespaceIsolation)
{
    vfs_.AddMount("/shared_dir", "C:/temp/shared_host");
    NamespaceManager::Instance().Initialize(&vfs_);
    ThreadManager::Instance().RegisterMainThread();

    EXPECT_EQ(portage_unshare(static_cast<int>(LINUX_CLONE_NEWNS)), 0);

    // Private mount inside unshared namespace
    EXPECT_TRUE(vfs_.AddMount("/private_dir", "C:/temp/private_host"));
    auto priv_target = vfs_.GetMountTarget("/private_dir");
    ASSERT_TRUE(priv_target.has_value());
    EXPECT_EQ(priv_target->string(), "C:/temp/private_host");

    // Inherited mount is still visible
    auto shared_target = vfs_.GetMountTarget("/shared_dir");
    ASSERT_TRUE(shared_target.has_value());

    // Root mount namespace must NOT contain /private_dir
    auto root_mnt = NamespaceManager::Instance().GetRootMountNamespace();
    ASSERT_NE(root_mnt, nullptr);
    EXPECT_FALSE(root_mnt->GetMountTarget("/private_dir").has_value());
    EXPECT_TRUE(root_mnt->GetMountTarget("/shared_dir").has_value());
}

TEST_F(NamespaceTest, UtsNamespaceIsolationAndUname)
{
    LinuxUtsname u{};
    EXPECT_EQ(SyscallDispatcher::Dispatch(63 /* SYS_uname */, reinterpret_cast<uint64_t>(&u), 0, 0,
                                          0, 0, 0),
              0);
    EXPECT_STREQ(u.nodename, "portage-win");

    EXPECT_EQ(portage_unshare(static_cast<int>(LINUX_CLONE_NEWUTS)), 0);

    const char *new_host = "sandbox-node";
    EXPECT_EQ(SyscallDispatcher::Dispatch(170 /* SYS_sethostname */,
                                          reinterpret_cast<uint64_t>(new_host),
                                          std::strlen(new_host), 0, 0, 0, 0),
              0);

    LinuxUtsname u_isolated{};
    EXPECT_EQ(SyscallDispatcher::Dispatch(63 /* SYS_uname */,
                                          reinterpret_cast<uint64_t>(&u_isolated), 0, 0, 0, 0, 0),
              0);
    EXPECT_STREQ(u_isolated.nodename, "sandbox-node");

    // Root UTS namespace must remain unchanged
    auto root_uts = NamespaceManager::Instance().GetRootUtsNamespace()->GetUtsname();
    EXPECT_STREQ(root_uts.nodename, "portage-win");
}

TEST_F(NamespaceTest, Clone3AndSetNsRoundtrip)
{
    // Open /proc/self/ns/uts before unsharing
    const char *uts_ns_path = "/proc/self/ns/uts";
    int uts_fd = static_cast<int>(SyscallDispatcher::Dispatch(
        257 /* SYS_openat */, static_cast<uint64_t>(-100 /* AT_FDCWD */),
        reinterpret_cast<uint64_t>(uts_ns_path), 0 /* O_RDONLY */, 0, 0, 0));
    ASSERT_GE(uts_fd, 0);

    // Readlink /proc/self/ns/pid
    char link_buf[128]{};
    const char *pid_ns_path = "/proc/self/ns/pid";
    int64_t link_len = SyscallDispatcher::Dispatch(
        267 /* SYS_readlinkat */, static_cast<uint64_t>(-100),
        reinterpret_cast<uint64_t>(pid_ns_path), reinterpret_cast<uint64_t>(link_buf),
        sizeof(link_buf) - 1, 0, 0);
    ASSERT_GT(link_len, 0);
    std::string link_str(link_buf, static_cast<size_t>(link_len));
    EXPECT_TRUE(link_str.starts_with("pid:["));

    // Unshare UTS and change hostname
    EXPECT_EQ(portage_unshare(static_cast<int>(LINUX_CLONE_NEWUTS)), 0);
    const char *iso_name = "isolated-uts";
    EXPECT_EQ(SyscallDispatcher::Dispatch(170 /* SYS_sethostname */,
                                          reinterpret_cast<uint64_t>(iso_name),
                                          std::strlen(iso_name), 0, 0, 0, 0),
              0);

    LinuxUtsname u1{};
    SyscallDispatcher::Dispatch(63, reinterpret_cast<uint64_t>(&u1), 0, 0, 0, 0, 0);
    EXPECT_STREQ(u1.nodename, "isolated-uts");

    // Re-attach to original UTS namespace via setns
    EXPECT_EQ(portage_setns(uts_fd, static_cast<int>(LINUX_CLONE_NEWUTS)), 0);

    LinuxUtsname u2{};
    SyscallDispatcher::Dispatch(63, reinterpret_cast<uint64_t>(&u2), 0, 0, 0, 0, 0);
    EXPECT_STREQ(u2.nodename, "portage-win");

    portage_close(uts_fd);
}
