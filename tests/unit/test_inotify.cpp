#include "fs/fd_table.hpp"
#include "fs/inotify_state.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "poll/epoll_manager.hpp"
#include "poll/poll_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace portage::fs;
using namespace portage::syscall;
using namespace portage::ndk;
using namespace portage::poll;

class InotifyTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        portage::thread::ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override { SyscallDispatcher::Shutdown(); }

    portage::mem::AddressSpace as_;
    Vfs vfs_;
    FileDescriptorTable fd_table_;
};

TEST_F(InotifyTest, InitAndClose)
{
    int ifd = portage_inotify_init1(LINUX_IN_NONBLOCK | LINUX_IN_CLOEXEC);
    ASSERT_GE(ifd, 0);

    char buf[sizeof(LinuxInotifyEvent) + 64]{};
    *portage_errno() = 0;
    int64_t n = portage_read(ifd, buf, sizeof(buf));
    EXPECT_EQ(n, -1);
    EXPECT_EQ(*portage_errno(), 11 /* EAGAIN */);

    EXPECT_EQ(portage_close(ifd), 0);
}

TEST_F(InotifyTest, AddWatchAndRemoveWatch)
{
    auto temp_dir = std::filesystem::temp_directory_path() /
                    ("inotify_test_" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(temp_dir);

    int ifd = portage_inotify_init1(LINUX_IN_NONBLOCK);
    ASSERT_GE(ifd, 0);

    std::string path_str = temp_dir.string();
    int wd = portage_inotify_add_watch(ifd, path_str.c_str(), IN_CREATE | IN_DELETE);
    ASSERT_GE(wd, 1);

    EXPECT_EQ(portage_inotify_rm_watch(ifd, wd), 0);

    char buf[sizeof(LinuxInotifyEvent) + 64]{};
    int64_t n = portage_read(ifd, buf, sizeof(buf));
    ASSERT_GE(n, static_cast<int64_t>(sizeof(LinuxInotifyEvent)));

    auto *ev = reinterpret_cast<LinuxInotifyEvent *>(buf);
    EXPECT_EQ(ev->wd, wd);
    EXPECT_TRUE((ev->mask & IN_IGNORED) != 0);

    EXPECT_EQ(portage_close(ifd), 0);
    std::filesystem::remove_all(temp_dir);
}

TEST_F(InotifyTest, RingBufferAndOverflow)
{
    InotifyState state(LINUX_IN_NONBLOCK);
    EXPECT_TRUE(state.IsEmpty());

    InotifyRecord rec1{};
    rec1.wd = 1;
    rec1.mask = IN_CREATE;
    rec1.cookie = 42;
    std::strncpy(rec1.name, "file1.txt", sizeof(rec1.name) - 1);
    EXPECT_TRUE(state.PushEvent(rec1));
    EXPECT_FALSE(state.IsEmpty());

    InotifyRecord out{};
    EXPECT_TRUE(state.PopEvent(out));
    EXPECT_EQ(out.wd, 1);
    EXPECT_EQ(out.mask, IN_CREATE);
    EXPECT_EQ(out.cookie, 42U);
    EXPECT_STREQ(out.name, "file1.txt");
    EXPECT_TRUE(state.IsEmpty());

    for (size_t i = 0; i < 512; ++i) {
        InotifyRecord r{};
        r.wd = static_cast<int32_t>(i + 1);
        r.mask = IN_MODIFY;
        EXPECT_TRUE(state.PushEvent(r));
    }

    InotifyRecord extra{};
    extra.wd = 999;
    extra.mask = IN_DELETE;
    EXPECT_FALSE(state.PushEvent(extra));

    EXPECT_TRUE(state.PopEvent(out));
    EXPECT_EQ(out.wd, 2);

    InotifyRecord last{};
    while (state.PopEvent(out)) {
        last = out;
    }
    EXPECT_EQ(last.wd, -1);
    EXPECT_EQ(last.mask, IN_Q_OVERFLOW);
}

TEST_F(InotifyTest, CreateAndDeleteEventDelivery)
{
    auto temp_dir =
        std::filesystem::temp_directory_path() / ("inotify_ev_" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(temp_dir);

    int ifd = portage_inotify_init1(LINUX_IN_NONBLOCK);
    ASSERT_GE(ifd, 0);

    std::string path_str = temp_dir.string();
    int wd = portage_inotify_add_watch(ifd, path_str.c_str(),
                                       IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE);
    ASSERT_GE(wd, 1);

    auto test_file = temp_dir / "sample.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "inotify test content";
    }

    char buf[512]{};
    int64_t bytes = 0;
    auto start = std::chrono::steady_clock::now();
    while (bytes <= 0) {
        bytes = portage_read(ifd, buf, sizeof(buf));
        if (bytes > 0) {
            break;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(1500)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_GE(bytes, static_cast<int64_t>(sizeof(LinuxInotifyEvent)));
    auto *ev = reinterpret_cast<LinuxInotifyEvent *>(buf);
    EXPECT_EQ(ev->wd, wd);
    EXPECT_TRUE((ev->mask & (IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE)) != 0);

    std::filesystem::remove(test_file);

    bytes = 0;
    start = std::chrono::steady_clock::now();
    while (bytes <= 0) {
        bytes = portage_read(ifd, buf, sizeof(buf));
        if (bytes > 0) {
            break;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(1500)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_GE(bytes, static_cast<int64_t>(sizeof(LinuxInotifyEvent)));
    ev = reinterpret_cast<LinuxInotifyEvent *>(buf);
    EXPECT_EQ(ev->wd, wd);
    EXPECT_TRUE((ev->mask & IN_DELETE) != 0);

    EXPECT_EQ(portage_close(ifd), 0);
    std::filesystem::remove_all(temp_dir);
}

TEST_F(InotifyTest, EpollIntegration)
{
    int ifd = portage_inotify_init1(LINUX_IN_NONBLOCK);
    ASSERT_GE(ifd, 0);

    int epfd = EpollManager::Instance().CreateInstance();
    ASSERT_GE(epfd, 0);

    LinuxEpollEvent item{};
    item.events = LINUX_EPOLLIN;
    item.data = 8888;
    EXPECT_EQ(EpollManager::Instance().Ctl(epfd, LINUX_EPOLL_CTL_ADD, ifd, &item), 0);

    LinuxEpollEvent evs[2]{};
    EXPECT_EQ(EpollManager::Instance().Wait(epfd, evs, 2, 0, &fd_table_), 0);

    auto desc = fd_table_.Get(ifd);
    ASSERT_TRUE(desc != nullptr);
    ASSERT_TRUE(desc->inotify != nullptr);

    InotifyRecord rec{};
    rec.wd = 1;
    rec.mask = IN_CREATE;
    rec.cookie = 0;
    std::strncpy(rec.name, "epoll_test.txt", sizeof(rec.name) - 1);
    desc->inotify->PushEvent(rec);

    int ready = EpollManager::Instance().Wait(epfd, evs, 2, 500, &fd_table_);
    EXPECT_EQ(ready, 1);
    EXPECT_EQ(evs[0].data, 8888ULL);
    EXPECT_TRUE((evs[0].events & LINUX_EPOLLIN) != 0);

    char buf[sizeof(LinuxInotifyEvent) + 64]{};
    int64_t bytes = portage_read(ifd, buf, sizeof(buf));
    EXPECT_GT(bytes, 0);

    EXPECT_EQ(EpollManager::Instance().Wait(epfd, evs, 2, 0, &fd_table_), 0);

    EpollManager::Instance().CloseInstance(epfd);
    EXPECT_EQ(portage_close(ifd), 0);
}
