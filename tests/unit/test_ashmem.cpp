#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace portage::mem {

class AshmemTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);
        SharedMemoryManager::Instance().Reset();
    }

    void TearDown() override
    {
        SharedMemoryManager::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        as_.reset();
    }

    static int64_t Dispatch(uint64_t nr, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0,
                            uint64_t a4 = 0, uint64_t a5 = 0, uint64_t a6 = 0)
    {
        return syscall::SyscallDispatcher::Dispatch(nr, a1, a2, a3, a4, a5, a6);
    }

    std::unique_ptr<AddressSpace> as_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

TEST_F(AshmemTest, CreatesAndSizesAshmemViaIoctls)
{
    // open("/dev/ashmem", O_RDWR)
    int64_t fd = Dispatch(257 /* sys_openat */, -100, reinterpret_cast<uint64_t>("/dev/ashmem"),
                          2 /* O_RDWR */, 0);
    ASSERT_GE(fd, 0);

    // Set Name
    char name[256] = "test_ashmem_region";
    int64_t ret = Dispatch(16 /* sys_ioctl */, fd, 0x41007701, reinterpret_cast<uint64_t>(name));
    EXPECT_EQ(ret, 0);

    // Get Name
    char get_name[256] = {0};
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x81007702, reinterpret_cast<uint64_t>(get_name));
    EXPECT_EQ(ret, 0);
    EXPECT_STREQ(get_name, "test_ashmem_region");

    // Set Size
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x40087703, 8192);
    EXPECT_EQ(ret, 0);

    // Get Size
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x00007704, 0);
    EXPECT_EQ(ret, 8192);

    // Pinning
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x40087708 /* UNPIN */, 0);
    EXPECT_EQ(ret, 0);
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x00007709 /* PIN_STATUS */, 0);
    EXPECT_EQ(ret, 0); // UNPINNED

    ret = Dispatch(16 /* sys_ioctl */, fd, 0x40087707 /* PIN */, 0);
    EXPECT_EQ(ret, 0); // NOT_PURGED
    ret = Dispatch(16 /* sys_ioctl */, fd, 0x00007709 /* PIN_STATUS */, 0);
    EXPECT_EQ(ret, 1); // PINNED

    // Close
    EXPECT_EQ(Dispatch(3 /* sys_close */, fd), 0);
}

TEST_F(AshmemTest, CreatesSharedMemoryViaMemfdSyscall)
{
    // sys_memfd_create("my_memfd", 1 /* MFD_CLOEXEC */)
    int64_t fd = Dispatch(319 /* sys_memfd_create */, reinterpret_cast<uint64_t>("my_memfd"), 1);
    ASSERT_GE(fd, 0);

    // sys_ftruncate(fd, 4096)
    int64_t ret = Dispatch(77 /* sys_ftruncate */, fd, 4096);
    EXPECT_EQ(ret, 0);

    // sys_mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
    int64_t addr = Dispatch(9 /* sys_mmap */, 0, 4096, 3 /* PROT_READ|PROT_WRITE */,
                            1 /* MAP_SHARED */, fd, 0);
    ASSERT_GT(addr, 0);

    char *ptr = reinterpret_cast<char *>(addr);
    std::memcpy(ptr, "HELLO_MEMFD", 12);
    EXPECT_STREQ(ptr, "HELLO_MEMFD");

    // sys_munmap
    EXPECT_EQ(Dispatch(11 /* sys_munmap */, addr, 4096), 0);

    // sys_close
    EXPECT_EQ(Dispatch(3 /* sys_close */, fd), 0);
}

TEST_F(AshmemTest, SharesMemoryAcrossMultipleMappings)
{
    int32_t fd = ASharedMemory_create("coherent_shm", 4096);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(ASharedMemory_getSize(fd), 4096u);

    // Map View 1
    int64_t addr1 = Dispatch(9 /* sys_mmap */, 0, 4096, 3 /* PROT_READ|PROT_WRITE */,
                             1 /* MAP_SHARED */, fd, 0);
    ASSERT_GT(addr1, 0);

    // Map View 2
    int64_t addr2 = Dispatch(9 /* sys_mmap */, 0, 4096, 3 /* PROT_READ|PROT_WRITE */,
                             1 /* MAP_SHARED */, fd, 0);
    ASSERT_GT(addr2, 0);

    uint32_t *p1 = reinterpret_cast<uint32_t *>(addr1);
    uint32_t *p2 = reinterpret_cast<uint32_t *>(addr2);

    p1[0] = 0xDEADBEEF;
    EXPECT_EQ(p2[0], 0xDEADBEEF);

    p2[100] = 0xCAFEBABE;
    EXPECT_EQ(p1[100], 0xCAFEBABE);

    EXPECT_EQ(Dispatch(11 /* sys_munmap */, addr1, 4096), 0);
    EXPECT_EQ(Dispatch(11 /* sys_munmap */, addr2, 4096), 0);
    EXPECT_EQ(Dispatch(3 /* sys_close */, fd), 0);
}

TEST_F(AshmemTest, EnforcesProtectionRestrictions)
{
    int32_t fd = ASharedMemory_create("prot_shm", 4096);
    ASSERT_GE(fd, 0);

    // Restrict protection to PROT_READ only
    int32_t set_ret = ASharedMemory_setProt(fd, 1 /* PROT_READ */);
    EXPECT_EQ(set_ret, 0);

    // Subsequent mmap with PROT_WRITE must fail (-EACCES)
    int64_t fail_map = Dispatch(9 /* sys_mmap */, 0, 4096, 3 /* PROT_READ|PROT_WRITE */,
                                1 /* MAP_SHARED */, fd, 0);
    EXPECT_EQ(fail_map, -13 /* -EACCES */);

    // Subsequent mmap with PROT_READ must succeed
    int64_t ok_map =
        Dispatch(9 /* sys_mmap */, 0, 4096, 1 /* PROT_READ */, 1 /* MAP_SHARED */, fd, 0);
    ASSERT_GT(ok_map, 0);

    // Cannot make protection more permissive
    int32_t bad_set = ASharedMemory_setProt(fd, 3 /* PROT_READ|PROT_WRITE */);
    EXPECT_EQ(bad_set, -22 /* -EINVAL */);

    EXPECT_EQ(Dispatch(11 /* sys_munmap */, ok_map, 4096), 0);
    EXPECT_EQ(Dispatch(3 /* sys_close */, fd), 0);
}

TEST_F(AshmemTest, DuplicatesDescriptorsAndRetainsMemory)
{
    int32_t fd = ASharedMemory_create("dup_shm", 4096);
    ASSERT_GE(fd, 0);

    int64_t addr = Dispatch(9 /* sys_mmap */, 0, 4096, 3 /* PROT_READ|PROT_WRITE */,
                            1 /* MAP_SHARED */, fd, 0);
    ASSERT_GT(addr, 0);
    std::strcpy(reinterpret_cast<char *>(addr), "PERSISTENT_PAYLOAD");
    EXPECT_EQ(Dispatch(11 /* sys_munmap */, addr, 4096), 0);

    int32_t dup_fd = ASharedMemory_dup(fd);
    ASSERT_GE(dup_fd, 0);
    EXPECT_NE(dup_fd, fd);

    // Close original descriptor
    EXPECT_EQ(Dispatch(3 /* sys_close */, fd), 0);

    // Query size on dup_fd
    EXPECT_EQ(ASharedMemory_getSize(dup_fd), 4096u);

    // Map through duplicated descriptor
    int64_t dup_addr =
        Dispatch(9 /* sys_mmap */, 0, 4096, 1 /* PROT_READ */, 1 /* MAP_SHARED */, dup_fd, 0);
    ASSERT_GT(dup_addr, 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(dup_addr), "PERSISTENT_PAYLOAD");

    EXPECT_EQ(Dispatch(11 /* sys_munmap */, dup_addr, 4096), 0);
    EXPECT_EQ(Dispatch(3 /* sys_close */, dup_fd), 0);
}

TEST_F(AshmemTest, RegistersAllNdkSymbols)
{
    auto &reg = ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("ASharedMemory_create"), 0ULL);
    EXPECT_NE(reg.Lookup("ASharedMemory_getSize"), 0ULL);
    EXPECT_NE(reg.Lookup("ASharedMemory_setProt"), 0ULL);
    EXPECT_NE(reg.Lookup("ASharedMemory_dup"), 0ULL);
}

} // namespace portage::mem
