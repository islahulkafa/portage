#include "rewrite/syscall_rewriter.hpp"
#include <gtest/gtest.h>
#include <windows.h>

using namespace portage::rewrite;

TEST(SyscallRewriterTest, RewritesSyscallWithCallRel32)
{
    // Allocate small trampoline island
    void *island = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ASSERT_NE(island, nullptr);

    SyscallRewriter rewriter(island, 4096);

    // Synthetic code sequence within island vicinity:
    // mov $1, %eax        (B8 01 00 00 00) -> 5 bytes
    // syscall             (0F 05)          -> 2 bytes
    // nop; nop; nop       (90 90 90)       -> 3 bytes
    // ret                 (C3)             -> 1 byte
    std::vector<uint8_t> code = {0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x90, 0x90, 0x90, 0xC3};

    uintptr_t base_vaddr = reinterpret_cast<uintptr_t>(code.data());
    size_t patches = rewriter.RewriteSegment(code, base_vaddr);

    EXPECT_EQ(patches, 1U);
    // At offset 5, should now be INT 3 (0xCC) + NOP (0x90)
    EXPECT_EQ(code[5], 0xCC);
    EXPECT_EQ(code[6], 0x90);

    VirtualFree(island, 0, MEM_RELEASE);
}
