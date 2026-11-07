#include "rewrite/x86_decode.hpp"
#include <gtest/gtest.h>

using namespace portage::rewrite;

TEST(X86DecodeTest, DecodesSingleByteInstructions)
{
    // 0x90: NOP
    uint8_t nop_code[] = {0x90};
    auto nop_info = DecodeInstruction(nop_code);
    EXPECT_TRUE(nop_info.is_valid);
    EXPECT_EQ(nop_info.length, 1U);

    // 0xC3: RET
    uint8_t ret_code[] = {0xC3};
    auto ret_info = DecodeInstruction(ret_code);
    EXPECT_TRUE(ret_info.is_valid);
    EXPECT_EQ(ret_info.length, 1U);
}

TEST(X86DecodeTest, DecodesSyscallInstruction)
{
    // 0x0F, 0x05: syscall
    uint8_t syscall_code[] = {0x0F, 0x05};
    auto info = DecodeInstruction(syscall_code);
    EXPECT_TRUE(info.is_valid);
    EXPECT_EQ(info.length, 2U);
}

TEST(X86DecodeTest, DecodesRexPrefixedInstructions)
{
    // 48 C7 C0 01 00 00 00: mov $1, %rax (7 bytes)
    uint8_t mov_rax[] = {0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00};
    auto info = DecodeInstruction(mov_rax);
    EXPECT_TRUE(info.is_valid);
    EXPECT_EQ(info.length, 7U);

    // 48 83 EC 08: sub $8, %rsp (4 bytes)
    uint8_t sub_rsp[] = {0x48, 0x83, 0xEC, 0x08};
    auto sub_info = DecodeInstruction(sub_rsp);
    EXPECT_TRUE(sub_info.is_valid);
    EXPECT_EQ(sub_info.length, 4U);
}

TEST(X86DecodeTest, CalculatesRelocationSpan)
{
    // Sequence:
    // 0F 05                      (syscall, 2 bytes)
    // 48 3D 00 F0 FF FF          (cmp $-4096, %rax, 6 bytes)
    // Total needed: >= 5 bytes -> should relocate 2 + 6 = 8 bytes
    uint8_t code[] = {0x0F, 0x05, 0x48, 0x3D, 0x00, 0xF0, 0xFF, 0xFF, 0xC3};

    size_t span = CalculateRelocationSpan(code, 5);
    EXPECT_EQ(span, 8U);
}
