#include "fs/dirent.hpp"
#include <gtest/gtest.h>

using namespace portage::fs;

TEST(DirentTest, ReclenIsEightByteAligned)
{
    EXPECT_EQ(CalculateDirent64Reclen(1) % 8, 0u);
    EXPECT_EQ(CalculateDirent64Reclen(2) % 8, 0u);
    EXPECT_EQ(CalculateDirent64Reclen(3) % 8, 0u);
    EXPECT_EQ(CalculateDirent64Reclen(4) % 8, 0u);
    EXPECT_EQ(CalculateDirent64Reclen(15) % 8, 0u);
    EXPECT_EQ(CalculateDirent64Reclen(255) % 8, 0u);
}

TEST(DirentTest, ReclenAccommodatesHeaderAndName)
{
    // Header is 19 bytes, name "foo" is 3 bytes, null is 1 byte -> 23 bytes -> aligned to 24
    uint16_t reclen = CalculateDirent64Reclen(3);
    EXPECT_GE(reclen, 23);
    EXPECT_EQ(reclen, 24);
}

TEST(DirentTest, StructFieldOffsetsMatchLinuxAbi)
{
    EXPECT_EQ(offsetof(LinuxDirent64, d_ino), 0u);
    EXPECT_EQ(offsetof(LinuxDirent64, d_off), 8u);
    EXPECT_EQ(offsetof(LinuxDirent64, d_reclen), 16u);
    EXPECT_EQ(offsetof(LinuxDirent64, d_type), 18u);
    EXPECT_EQ(offsetof(LinuxDirent64, d_name), 19u);
}
