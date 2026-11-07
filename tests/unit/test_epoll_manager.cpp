#include "fs/fd_table.hpp"
#include "poll/epoll_manager.hpp"
#include "poll/poll_types.hpp"
#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace portage::poll;
using namespace portage::fs;

TEST(EpollManagerTest, ManagesEpollLifecycleAndRegistrations)
{
    auto &em = EpollManager::Instance();
    int epfd = em.CreateInstance();
    EXPECT_GT(epfd, 0);

    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = 0x1234;

    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_ADD, 10, &ev), 0);
    // Duplicate add should fail with -EEXIST
    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_ADD, 10, &ev), -17);

    // Modify
    ev.events = LINUX_EPOLLOUT;
    ev.data = 0x5678;
    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_MOD, 10, &ev), 0);

    // Delete
    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_DEL, 10, nullptr), 0);
    // Duplicate delete should fail with -ENOENT
    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_DEL, 10, nullptr), -2);

    EXPECT_EQ(em.CloseInstance(epfd), 0);
    // Closing closed instance should fail with -EBADF
    EXPECT_EQ(em.CloseInstance(epfd), -9);
}

TEST(EpollManagerTest, DetectsPipeReadReadinessInWait)
{
    FileDescriptorTable fds;
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(fds.CreatePipe(pipe_fds), 0);

    auto &em = EpollManager::Instance();
    int epfd = em.CreateInstance();
    ASSERT_GT(epfd, 0);

    LinuxEpollEvent ev{};
    ev.events = LINUX_EPOLLIN;
    ev.data = 42;
    EXPECT_EQ(em.Ctl(epfd, LINUX_EPOLL_CTL_ADD, pipe_fds[0], &ev), 0);

    // With no data in pipe, Wait with 0 timeout should return 0
    LinuxEpollEvent out_events[2]{};
    int ready = em.Wait(epfd, out_events, 2, 0, &fds);
    EXPECT_EQ(ready, 0);

    // Write a byte to pipe write end
    auto write_desc = fds.Get(pipe_fds[1]);
    ASSERT_TRUE(write_desc);
    DWORD written = 0;
    const char byte = 'x';
    EXPECT_TRUE(WriteFile(write_desc->handle, &byte, 1, &written, nullptr));

    // Now Wait should report pipe_fds[0] ready with LINUX_EPOLLIN
    ready = em.Wait(epfd, out_events, 2, 100, &fds);
    EXPECT_EQ(ready, 1);
    EXPECT_TRUE(out_events[0].events & LINUX_EPOLLIN);
    EXPECT_EQ(out_events[0].data, 42);

    fds.Close(pipe_fds[0]);
    fds.Close(pipe_fds[1]);
    em.CloseInstance(epfd);
}

TEST(EpollManagerTest, DetectsPipeReadReadinessInPoll)
{
    FileDescriptorTable fds;
    int pipe_fds[2] = {-1, -1};
    ASSERT_EQ(fds.CreatePipe(pipe_fds), 0);

    LinuxPollFd pfds[1]{};
    pfds[0].fd = pipe_fds[0];
    pfds[0].events = LINUX_POLLIN;

    auto &em = EpollManager::Instance();
    int ready = em.Poll(pfds, 1, 0, &fds);
    EXPECT_EQ(ready, 0);
    EXPECT_EQ(pfds[0].revents, 0);

    // Write data to pipe
    auto write_desc = fds.Get(pipe_fds[1]);
    ASSERT_TRUE(write_desc);
    DWORD written = 0;
    const char byte = 'y';
    EXPECT_TRUE(WriteFile(write_desc->handle, &byte, 1, &written, nullptr));

    ready = em.Poll(pfds, 1, 100, &fds);
    EXPECT_EQ(ready, 1);
    EXPECT_TRUE(pfds[0].revents & LINUX_POLLIN);

    fds.Close(pipe_fds[0]);
    fds.Close(pipe_fds[1]);
}
