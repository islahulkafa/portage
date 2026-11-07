#include "fs/fd_table.hpp"
#include <gtest/gtest.h>

using namespace portage::fs;

TEST(FdTableTest, InitializesStdioDescriptors)
{
    FileDescriptorTable table;

    auto fd0 = table.Get(0);
    ASSERT_NE(fd0, nullptr);
    EXPECT_EQ(fd0->fd, 0);

    auto fd1 = table.Get(1);
    ASSERT_NE(fd1, nullptr);
    EXPECT_EQ(fd1->fd, 1);

    auto fd2 = table.Get(2);
    ASSERT_NE(fd2, nullptr);
    EXPECT_EQ(fd2->fd, 2);
}

TEST(FdTableTest, AllocatesAndClosesDescriptors)
{
    FileDescriptorTable table;

    int fd3 = table.Allocate(INVALID_HANDLE_VALUE, 0, DeviceType::DevNull, "/dev/null");
    EXPECT_GE(fd3, 3);

    auto desc = table.Get(fd3);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->dev, DeviceType::DevNull);

    int close_res = table.Close(fd3);
    EXPECT_EQ(close_res, 0);

    EXPECT_EQ(table.Get(fd3), nullptr);
}

TEST(FdTableTest, RejectsInvalidFdOnClose)
{
    FileDescriptorTable table;
    int close_res = table.Close(999);
    EXPECT_EQ(close_res, -9); // -EBADF
}

TEST(FdTableTest, AllocatesAndClosesBinderDescriptor)
{
    FileDescriptorTable table;
    int bfd = table.AllocateBinder(42, DeviceType::Binder, 2);
    EXPECT_GE(bfd, 3);

    auto desc = table.Get(bfd);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->dev, DeviceType::Binder);
    EXPECT_EQ(desc->binder_id, 42);

    int dup_fd = table.Duplicate(bfd);
    EXPECT_GE(dup_fd, 3);
    EXPECT_NE(dup_fd, bfd);

    auto dup_desc = table.Get(dup_fd);
    ASSERT_NE(dup_desc, nullptr);
    EXPECT_EQ(dup_desc->dev, DeviceType::Binder);
    EXPECT_EQ(dup_desc->binder_id, 42);

    EXPECT_EQ(table.Close(dup_fd), 0);
    EXPECT_EQ(table.Close(bfd), 0);
}
