#pragma once

#include "mem/shared_memory.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Android NDK AHardwareBuffer format constants
enum {
    AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1,
    AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM = 2,
    AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM = 3,
    AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM = 4,
    AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT = 0x16,
    AHARDWAREBUFFER_FORMAT_RGBA_FP16 = 0x16,
    AHARDWAREBUFFER_FORMAT_RAW_OPAQUE = 0x24,
    AHARDWAREBUFFER_FORMAT_RAW10 = 0x25,
    AHARDWAREBUFFER_FORMAT_RAW12 = 0x26,
    AHARDWAREBUFFER_FORMAT_BLOB = 0x21,
    AHARDWAREBUFFER_FORMAT_D16_UNORM = 0x30,
    AHARDWAREBUFFER_FORMAT_D24_UNORM = 0x31,
    AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT = 0x32,
    AHARDWAREBUFFER_FORMAT_D32_FLOAT = 0x33,
    AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT = 0x34,
    AHARDWAREBUFFER_FORMAT_S8_UINT = 0x35,
    AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420 = 0x23,
    AHARDWAREBUFFER_FORMAT_YCbCr_P010 = 0x36,
};

// Android NDK AHardwareBuffer usage flags
enum {
    AHARDWAREBUFFER_USAGE_CPU_READ_NEVER = 0UL,
    AHARDWAREBUFFER_USAGE_CPU_READ_RARELY = 2UL,
    AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN = 3UL,
    AHARDWAREBUFFER_USAGE_CPU_READ_MASK = 0xFUL,
    AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER = 0UL << 4,
    AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY = 2UL << 4,
    AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN = 3UL << 4,
    AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK = 0xFUL << 4,
    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE = 1UL << 8,
    AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT = 1UL << 9,
    AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT = 1UL << 14,
    AHARDWAREBUFFER_USAGE_VIDEO_ENCODE = 1UL << 16,
    AHARDWAREBUFFER_USAGE_SENSOR_DIRECT_DATA = 1UL << 23,
    AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER = 1UL << 24,
    AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP = 1UL << 25,
    AHARDWAREBUFFER_USAGE_GPU_MIPMAP_COMPLETE = 1UL << 26,
};

// Android NDK status codes
enum {
    AHARDWAREBUFFER_STATUS_OK = 0,
    AHARDWAREBUFFER_STATUS_NO_MEMORY = -12,
    AHARDWAREBUFFER_STATUS_INVALID_ARG = -22,
    AHARDWAREBUFFER_STATUS_UNSUPPORTED = -38,
    AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR = -1,
};

typedef struct AHardwareBuffer_Desc {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t layers{0};
    uint32_t format{0};
    uint64_t usage{0};
    uint32_t stride{0};
    uint32_t rfu0{0};
    uint64_t rfu1{0};
} AHardwareBuffer_Desc;

typedef struct AHardwareBuffer_Plane {
    void *data{nullptr};
    uint32_t pixelStride{0};
    uint32_t rowStride{0};
} AHardwareBuffer_Plane;

typedef struct AHardwareBuffer_Planes {
    uint32_t planeCount{0};
    AHardwareBuffer_Plane planes[4]{};
} AHardwareBuffer_Planes;

struct AHardwareBuffer {
    AHardwareBuffer_Desc desc{};
    uint64_t id{0};
    std::atomic<int32_t> ref_count{1};
    std::shared_ptr<portage::mem::SharedMemoryRegion> shared_memory{};
    void *mapped_address{nullptr};
    size_t total_size{0};
    uint32_t bytes_per_pixel{0};
    bool is_locked{false};
    uint64_t locked_usage{0};
};

namespace portage::ndk {

class HardwareBufferManager {
  public:
    static HardwareBufferManager &Instance() noexcept;

    int Allocate(const AHardwareBuffer_Desc &desc, AHardwareBuffer **out_buffer);
    void Acquire(AHardwareBuffer *buffer);
    void Release(AHardwareBuffer *buffer);
    int Describe(const AHardwareBuffer *buffer, AHardwareBuffer_Desc *out_desc) const;

    int Lock(AHardwareBuffer *buffer, uint64_t usage, int32_t fence, const ARect *rect,
             void **out_virtual_address);
    int LockPlanes(AHardwareBuffer *buffer, uint64_t usage, int32_t fence, const ARect *rect,
                   AHardwareBuffer_Planes *out_planes);
    int LockAndGetInfo(AHardwareBuffer *buffer, uint64_t usage, int32_t fence, const ARect *rect,
                       void **out_virtual_address, int32_t *out_bytes_per_pixel,
                       int32_t *out_bytes_per_stride);
    int Unlock(AHardwareBuffer *buffer, int32_t *fence);

    int GetId(const AHardwareBuffer *buffer, uint64_t *out_id) const;
    int IsSupported(const AHardwareBuffer_Desc &desc) const;

    int SendHandleToSocket(const AHardwareBuffer *buffer, int socket_fd);
    int RecvHandleFromSocket(int socket_fd, AHardwareBuffer **out_buffer);

    bool IsValid(const AHardwareBuffer *buffer) const;
    std::shared_ptr<AHardwareBuffer> FindById(uint64_t id) const;

    void Reset();

  private:
    HardwareBufferManager() = default;
    ~HardwareBufferManager() = default;

    HardwareBufferManager(const HardwareBufferManager &) = delete;
    HardwareBufferManager &operator=(const HardwareBufferManager &) = delete;

    mutable std::mutex mutex_{};
    std::unordered_map<uint64_t, std::shared_ptr<AHardwareBuffer>> buffers_{};
    uint64_t next_id_{1};
};

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc,
                                                       AHardwareBuffer **outBuffer);

__attribute__((sysv_abi)) void AHardwareBuffer_acquire(AHardwareBuffer *buffer);

__attribute__((sysv_abi)) void AHardwareBuffer_release(AHardwareBuffer *buffer);

__attribute__((sysv_abi)) void AHardwareBuffer_describe(const AHardwareBuffer *buffer,
                                                        AHardwareBuffer_Desc *outDesc);

__attribute__((sysv_abi)) int AHardwareBuffer_lock(AHardwareBuffer *buffer, uint64_t usage,
                                                   int32_t fence, const ARect *rect,
                                                   void **outVirtualAddress);

__attribute__((sysv_abi)) int AHardwareBuffer_unlock(AHardwareBuffer *buffer, int32_t *fence);

__attribute__((sysv_abi)) int AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage,
                                                         int32_t fence, const ARect *rect,
                                                         AHardwareBuffer_Planes *outPlanes);

__attribute__((sysv_abi)) int AHardwareBuffer_getId(const AHardwareBuffer *buffer, uint64_t *outId);

__attribute__((sysv_abi)) int AHardwareBuffer_isSupported(const AHardwareBuffer_Desc *desc);

__attribute__((sysv_abi)) int
AHardwareBuffer_lockAndGetInfo(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                               const ARect *rect, void **outVirtualAddress,
                               int32_t *outBytesPerPixel, int32_t *outBytesPerStride);

__attribute__((sysv_abi)) int AHardwareBuffer_sendHandleToUnixSocket(const AHardwareBuffer *buffer,
                                                                     int socketFd);

__attribute__((sysv_abi)) int AHardwareBuffer_recvHandleFromUnixSocket(int socketFd,
                                                                       AHardwareBuffer **outBuffer);

} // extern "C"
