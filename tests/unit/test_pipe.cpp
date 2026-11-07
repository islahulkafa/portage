#include "fs/fd_table.hpp"
#include <gtest/gtest.h>

using namespace portage::fs;

TEST(PipeTest, CreatesAnonymousPipeWithReadAndWriteEnds)
{
    FileDescriptorTable table;
    int fds[2] = {-1, -1};

    int rc = table.CreatePipe(fds, 0);
    EXPECT_EQ(rc, 0);
    EXPECT_GE(fds[0], 3);
    EXPECT_GE(fds[1], 3);
    EXPECT_NE(fds[0], fds[1]);

    auto r_desc = table.Get(fds[0]);
    auto w_desc = table.Get(fds[1]);
    ASSERT_NE(r_desc, nullptr);
    ASSERT_NE(w_desc, nullptr);
    EXPECT_EQ(r_desc->dev, DeviceType::Pipe);
    EXPECT_EQ(w_desc->dev, DeviceType::Pipe);

    // Verify Win32 read/write across pipe handles
    const char msg[] = "hello pipe payload";
    DWORD written = 0;
    BOOL write_ok = WriteFile(w_desc->handle, msg, sizeof(msg), &written, nullptr);
    EXPECT_TRUE(write_ok);
    EXPECT_EQ(written, sizeof(msg));

    char buf[64] = {0};
    DWORD read_bytes = 0;
    BOOL read_ok = ReadFile(r_desc->handle, buf, sizeof(buf), &read_bytes, nullptr);
    EXPECT_TRUE(read_ok);
    EXPECT_STREQ(buf, msg);

    table.Close(fds[0]);
    table.Close(fds[1]);
}

TEST(PipeTest, DuplicatesDescriptorWithNewFd)
{
    FileDescriptorTable table;
    int fds[2] = {-1, -1};
    ASSERT_EQ(table.CreatePipe(fds, 0), 0);

    int dup_fd = table.Duplicate(fds[0], 100);
    EXPECT_EQ(dup_fd, 100);

    auto dup_desc = table.Get(100);
    ASSERT_NE(dup_desc, nullptr);
    EXPECT_EQ(dup_desc->dev, DeviceType::Pipe);

    table.Close(fds[0]);
    table.Close(fds[1]);
    table.Close(100);
}
