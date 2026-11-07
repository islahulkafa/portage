#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "gles/egl_dispatcher.hpp"
#include "mem/address_space.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/hardware_buffer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "vulkan/vulkan_bridge.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace portage::ndk {

class HardwareBufferTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);
        mem::SharedMemoryManager::Instance().Reset();
        HardwareBufferManager::Instance().Reset();
    }

    void TearDown() override
    {
        HardwareBufferManager::Instance().Reset();
        mem::SharedMemoryManager::Instance().Reset();
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

    std::unique_ptr<mem::AddressSpace> as_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

TEST_F(HardwareBufferTest, AllocatesAndDescribesRgbaBuffer)
{
    AHardwareBuffer_Desc desc{
        .width = 64,
        .height = 64,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };

    AHardwareBuffer *buf = nullptr;
    EXPECT_EQ(AHardwareBuffer_allocate(&desc, &buf), AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(buf, nullptr);

    AHardwareBuffer_Desc out_desc{};
    AHardwareBuffer_describe(buf, &out_desc);
    EXPECT_EQ(out_desc.width, 64u);
    EXPECT_EQ(out_desc.height, 64u);
    EXPECT_GE(out_desc.stride, 64u);
    EXPECT_EQ(out_desc.format, static_cast<uint32_t>(AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM));

    uint64_t id = 0;
    EXPECT_EQ(AHardwareBuffer_getId(buf, &id), AHARDWAREBUFFER_STATUS_OK);
    EXPECT_GT(id, 0u);

    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, LocksAndModifiesBufferMemory)
{
    AHardwareBuffer_Desc desc{
        .width = 32,
        .height = 32,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };

    AHardwareBuffer *buf = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &buf), AHARDWAREBUFFER_STATUS_OK);

    void *write_ptr = nullptr;
    int32_t bpp = 0;
    int32_t byte_stride = 0;
    EXPECT_EQ(AHardwareBuffer_lockAndGetInfo(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                                             nullptr, &write_ptr, &bpp, &byte_stride),
              AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(write_ptr, nullptr);
    EXPECT_EQ(bpp, 4);
    EXPECT_GE(byte_stride, 128);

    uint32_t *pixels = static_cast<uint32_t *>(write_ptr);
    pixels[0] = 0x11223344;
    pixels[31] = 0xAABBCCDD;

    int32_t fence = 0;
    EXPECT_EQ(AHardwareBuffer_unlock(buf, &fence), AHARDWAREBUFFER_STATUS_OK);

    void *read_ptr = nullptr;
    EXPECT_EQ(
        AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &read_ptr),
        AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(read_ptr, nullptr);

    const uint32_t *read_pixels = static_cast<const uint32_t *>(read_ptr);
    EXPECT_EQ(read_pixels[0], 0x11223344u);
    EXPECT_EQ(read_pixels[31], 0xAABBCCDDu);

    EXPECT_EQ(AHardwareBuffer_unlock(buf, nullptr), AHARDWAREBUFFER_STATUS_OK);
    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, LocksMultiPlaneYuvBuffer)
{
    AHardwareBuffer_Desc desc{
        .width = 64,
        .height = 64,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };

    AHardwareBuffer *buf = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &buf), AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer_Planes planes{};
    EXPECT_EQ(AHardwareBuffer_lockPlanes(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr,
                                         &planes),
              AHARDWAREBUFFER_STATUS_OK);

    EXPECT_EQ(planes.planeCount, 3u);
    ASSERT_NE(planes.planes[0].data, nullptr);
    ASSERT_NE(planes.planes[1].data, nullptr);
    ASSERT_NE(planes.planes[2].data, nullptr);

    EXPECT_EQ(planes.planes[0].pixelStride, 1u);
    EXPECT_GE(planes.planes[0].rowStride, 64u);
    EXPECT_EQ(planes.planes[1].pixelStride, 1u);
    EXPECT_GE(planes.planes[1].rowStride, 32u);
    EXPECT_EQ(planes.planes[2].pixelStride, 1u);
    EXPECT_GE(planes.planes[2].rowStride, 32u);

    // Write to each plane
    static_cast<uint8_t *>(planes.planes[0].data)[0] = 0xAA;
    static_cast<uint8_t *>(planes.planes[1].data)[0] = 0xBB;
    static_cast<uint8_t *>(planes.planes[2].data)[0] = 0xCC;

    EXPECT_EQ(AHardwareBuffer_unlock(buf, nullptr), AHARDWAREBUFFER_STATUS_OK);
    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, AllocatesBlobBuffer)
{
    AHardwareBuffer_Desc desc{
        .width = 1024,
        .height = 1,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_BLOB,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };

    AHardwareBuffer *buf = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &buf), AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer_Desc out_desc{};
    AHardwareBuffer_describe(buf, &out_desc);
    EXPECT_EQ(out_desc.width, 1024u);
    EXPECT_EQ(out_desc.stride, 1024u);

    void *ptr = nullptr;
    EXPECT_EQ(AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &ptr),
              AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(ptr, nullptr);
    std::memcpy(ptr, "BLOB_TEST_DATA", 15);
    EXPECT_EQ(AHardwareBuffer_unlock(buf, nullptr), AHARDWAREBUFFER_STATUS_OK);

    void *rptr = nullptr;
    EXPECT_EQ(AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &rptr),
              AHARDWAREBUFFER_STATUS_OK);
    EXPECT_STREQ(static_cast<const char *>(rptr), "BLOB_TEST_DATA");
    EXPECT_EQ(AHardwareBuffer_unlock(buf, nullptr), AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, RejectsInvalidUsageAndParameters)
{
    EXPECT_EQ(AHardwareBuffer_allocate(nullptr, nullptr), AHARDWAREBUFFER_STATUS_INVALID_ARG);

    AHardwareBuffer_Desc zero_desc{.width = 0, .height = 64, .layers = 1, .format = 1};
    AHardwareBuffer *buf = nullptr;
    EXPECT_EQ(AHardwareBuffer_allocate(&zero_desc, &buf), AHARDWAREBUFFER_STATUS_INVALID_ARG);

    AHardwareBuffer_Desc bad_format{.width = 64, .height = 64, .layers = 1, .format = 0x9999};
    EXPECT_EQ(AHardwareBuffer_allocate(&bad_format, &buf), AHARDWAREBUFFER_STATUS_UNSUPPORTED);

    EXPECT_EQ(AHardwareBuffer_isSupported(&zero_desc), 0);
    AHardwareBuffer_Desc good_desc{
        .width = 64, .height = 64, .layers = 1, .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM};
    EXPECT_EQ(AHardwareBuffer_isSupported(&good_desc), 1);

    // Allocate buffer with CPU_READ only
    good_desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    ASSERT_EQ(AHardwareBuffer_allocate(&good_desc, &buf), AHARDWAREBUFFER_STATUS_OK);

    void *ptr = nullptr;
    // Attempting to lock for writing on read-only buffer must fail
    EXPECT_EQ(AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &ptr),
              AHARDWAREBUFFER_STATUS_INVALID_ARG);

    // Relocking without unlock must fail
    EXPECT_EQ(AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &ptr),
              AHARDWAREBUFFER_STATUS_OK);
    void *ptr2 = nullptr;
    EXPECT_EQ(AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &ptr2),
              AHARDWAREBUFFER_STATUS_INVALID_ARG);
    EXPECT_EQ(AHardwareBuffer_unlock(buf, nullptr), AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, InteroperatesWithEglAndVulkan)
{
    AHardwareBuffer_Desc desc{
        .width = 64,
        .height = 64,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
    };

    AHardwareBuffer *buf = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &buf), AHARDWAREBUFFER_STATUS_OK);

    // EGL interop
    EGLClientBuffer client_buf = eglGetNativeClientBufferANDROID(buf);
    EXPECT_NE(client_buf, nullptr);

    EGLImageKHR img = eglCreateImageKHR(EGL_NO_DISPLAY, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                        client_buf, nullptr);
    EXPECT_NE(img, nullptr);
    EXPECT_EQ(eglDestroyImageKHR(EGL_NO_DISPLAY, img), 1u);

    // Vulkan interop
    vulkan::VulkanBridge::Instance().Initialize();
    const auto *table = vulkan::VulkanBridge::Instance().GetDispatchTable();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->vkGetAndroidHardwareBufferPropertiesANDROID, nullptr);

    VkAndroidHardwareBufferPropertiesANDROID props{};
    VkResult res = table->vkGetAndroidHardwareBufferPropertiesANDROID(VK_NULL_HANDLE, buf, &props);
    EXPECT_EQ(res, VK_SUCCESS);
    EXPECT_GT(props.allocationSize, 0u);
    EXPECT_EQ(props.memoryTypeBits, 0x1u);

    AHardwareBuffer_release(buf);
}

TEST_F(HardwareBufferTest, TransfersHandleViaUnixSocket)
{
    int pipefds[2] = {-1, -1};
    ASSERT_EQ(fd_table_->CreatePipe(pipefds, 0), 0);

    AHardwareBuffer_Desc desc{
        .width = 64,
        .height = 64,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };

    AHardwareBuffer *send_buf = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &send_buf), AHARDWAREBUFFER_STATUS_OK);

    void *ptr = nullptr;
    ASSERT_EQ(
        AHardwareBuffer_lock(send_buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &ptr),
        AHARDWAREBUFFER_STATUS_OK);
    uint32_t *pixels = static_cast<uint32_t *>(ptr);
    pixels[0] = 0xCAFEBABE;
    ASSERT_EQ(AHardwareBuffer_unlock(send_buf, nullptr), AHARDWAREBUFFER_STATUS_OK);

    EXPECT_EQ(AHardwareBuffer_sendHandleToUnixSocket(send_buf, pipefds[1]),
              AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer *recv_buf = nullptr;
    EXPECT_EQ(AHardwareBuffer_recvHandleFromUnixSocket(pipefds[0], &recv_buf),
              AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(recv_buf, nullptr);
    EXPECT_EQ(recv_buf->id, send_buf->id);

    void *rptr = nullptr;
    ASSERT_EQ(
        AHardwareBuffer_lock(recv_buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &rptr),
        AHARDWAREBUFFER_STATUS_OK);
    const uint32_t *rpixels = static_cast<const uint32_t *>(rptr);
    EXPECT_EQ(rpixels[0], 0xCAFEBABEu);
    ASSERT_EQ(AHardwareBuffer_unlock(recv_buf, nullptr), AHARDWAREBUFFER_STATUS_OK);

    AHardwareBuffer_release(recv_buf);
    AHardwareBuffer_release(send_buf);

    EXPECT_EQ(Dispatch(3 /* sys_close */, pipefds[0]), 0);
    EXPECT_EQ(Dispatch(3 /* sys_close */, pipefds[1]), 0);
}

TEST_F(HardwareBufferTest, RegistersAllNdkSymbols)
{
    auto &reg = NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("AHardwareBuffer_allocate"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_acquire"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_release"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_describe"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_lock"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_unlock"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_lockPlanes"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_getId"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_isSupported"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_lockAndGetInfo"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_sendHandleToUnixSocket"), 0ULL);
    EXPECT_NE(reg.Lookup("AHardwareBuffer_recvHandleFromUnixSocket"), 0ULL);
}

} // namespace portage::ndk
