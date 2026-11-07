#include "elf/elf_loader.hpp"
#include "mem/address_space.hpp"
#include <gtest/gtest.h>

using namespace portage;

TEST(ElfLoaderTest, RejectsInvalidMagic)
{
    mem::AddressSpace as;
    elf::ElfLoader loader(&as);

    uint8_t invalid_data[64] = {0};
    auto res = loader.LoadFromMemory(invalid_data);
    ASSERT_TRUE(std::holds_alternative<elf::LoaderError>(res));
    EXPECT_EQ(std::get<elf::LoaderError>(res), elf::LoaderError::InvalidMagic);
}

TEST(ElfLoaderTest, RejectsTooSmallFile)
{
    mem::AddressSpace as;
    elf::ElfLoader loader(&as);

    uint8_t tiny_data[16] = {0x7F, 'E', 'L', 'F'};
    auto res = loader.LoadFromMemory(tiny_data);
    ASSERT_TRUE(std::holds_alternative<elf::LoaderError>(res));
    EXPECT_EQ(std::get<elf::LoaderError>(res), elf::LoaderError::FileTooSmall);
}
