#include "ndk/hardware_buffer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <cstring>

namespace portage::ndk {

namespace {

uint32_t GetFormatBytesPerPixel(uint32_t format) noexcept
{
    switch (format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
        return 4;
    case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
        return 3;
    case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
    case AHARDWAREBUFFER_FORMAT_D16_UNORM:
    case AHARDWAREBUFFER_FORMAT_RAW10:
    case AHARDWAREBUFFER_FORMAT_RAW12:
        return 2;
    case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
        return 8;
    case AHARDWAREBUFFER_FORMAT_BLOB:
    case AHARDWAREBUFFER_FORMAT_RAW_OPAQUE:
    case AHARDWAREBUFFER_FORMAT_S8_UINT:
        return 1;
    case AHARDWAREBUFFER_FORMAT_D24_UNORM:
    case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
        return 4;
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
        return 8;
    case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
        return 1;
    case AHARDWAREBUFFER_FORMAT_YCbCr_P010:
        return 2;
    default:
        return 4;
    }
}

bool IsFormatSupported(uint32_t format) noexcept
{
    switch (format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
    case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
    case AHARDWAREBUFFER_FORMAT_RAW_OPAQUE:
    case AHARDWAREBUFFER_FORMAT_RAW10:
    case AHARDWAREBUFFER_FORMAT_RAW12:
    case AHARDWAREBUFFER_FORMAT_BLOB:
    case AHARDWAREBUFFER_FORMAT_D16_UNORM:
    case AHARDWAREBUFFER_FORMAT_D24_UNORM:
    case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
    case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
    case AHARDWAREBUFFER_FORMAT_S8_UINT:
    case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
    case AHARDWAREBUFFER_FORMAT_YCbCr_P010:
        return true;
    default:
        return false;
    }
}

struct AhwbWirePacket {
    uint32_t magic{0x41485742}; // "AHWB"
    uint64_t id{0};
    AHardwareBuffer_Desc desc{};
    uint64_t total_size{0};
};

} // namespace

HardwareBufferManager &HardwareBufferManager::Instance() noexcept
{
    static HardwareBufferManager instance;
    return instance;
}

int HardwareBufferManager::Allocate(const AHardwareBuffer_Desc &desc, AHardwareBuffer **out_buffer)
{
    if (!out_buffer) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }
    *out_buffer = nullptr;

    if (desc.width == 0 || desc.height == 0 || desc.layers == 0) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if (!IsFormatSupported(desc.format)) {
        return AHARDWAREBUFFER_STATUS_UNSUPPORTED;
    }

    uint32_t bpp = GetFormatBytesPerPixel(desc.format);
    uint32_t stride = desc.width;
    size_t total_size = 0;

    if (desc.format == AHARDWAREBUFFER_FORMAT_BLOB) {
        stride = desc.width;
        total_size = static_cast<size_t>(desc.width) * desc.height * desc.layers;
    } else if (desc.format == AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420) {
        uint32_t y_stride = (desc.width + 63) / 64 * 64;
        uint32_t uv_stride = (desc.width / 2 + 63) / 64 * 64;
        size_t y_size = static_cast<size_t>(y_stride) * desc.height;
        size_t uv_size = static_cast<size_t>(uv_stride) * (desc.height / 2);
        size_t layer_size = y_size + uv_size * 2;
        stride = y_stride;
        total_size = layer_size * desc.layers;
    } else {
        uint32_t row_bytes = (desc.width * bpp + 63) / 64 * 64;
        stride = row_bytes / bpp;
        total_size = static_cast<size_t>(row_bytes) * desc.height * desc.layers;
    }

    if (total_size == 0) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    auto shm = mem::SharedMemoryManager::Instance().CreateRegion("ahardwarebuffer", total_size);
    if (!shm) {
        return AHARDWAREBUFFER_STATUS_NO_MEMORY;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto hw_buf = std::make_shared<AHardwareBuffer>();
    hw_buf->desc = desc;
    hw_buf->desc.stride = stride;
    hw_buf->id = next_id_++;
    hw_buf->ref_count = 1;
    hw_buf->shared_memory = shm;
    hw_buf->total_size = total_size;
    hw_buf->bytes_per_pixel = bpp;
    hw_buf->is_locked = false;
    hw_buf->locked_usage = 0;

    buffers_[hw_buf->id] = hw_buf;
    *out_buffer = hw_buf.get();
    return AHARDWAREBUFFER_STATUS_OK;
}

void HardwareBufferManager::Acquire(AHardwareBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    buffer->ref_count.fetch_add(1, std::memory_order_relaxed);
}

void HardwareBufferManager::Release(AHardwareBuffer *buffer)
{
    if (!buffer) {
        return;
    }

    if (buffer->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer->mapped_address) {
            mem::SharedMemoryManager::Instance().UnmapShared(buffer->mapped_address);
            buffer->mapped_address = nullptr;
        }
        buffers_.erase(buffer->id);
    }
}

int HardwareBufferManager::Describe(const AHardwareBuffer *buffer,
                                    AHardwareBuffer_Desc *out_desc) const
{
    if (!buffer || !out_desc) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }
    *out_desc = buffer->desc;
    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::Lock(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                                const ARect *rect, void **out_virtual_address)
{
    (void)fence;
    if (!buffer || !out_virtual_address) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if ((usage & (AHARDWAREBUFFER_USAGE_CPU_READ_MASK | AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK)) ==
        0) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if ((usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK) &&
        (buffer->desc.usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK) == 0) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if (buffer->is_locked) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if (!buffer->mapped_address && buffer->shared_memory) {
        buffer->mapped_address = mem::SharedMemoryManager::Instance().MapShared(
            *buffer->shared_memory, nullptr, buffer->total_size, 3 /* PROT_READ|PROT_WRITE */,
            1 /* MAP_SHARED */, 0);
        if (!buffer->mapped_address) {
            return AHARDWAREBUFFER_STATUS_NO_MEMORY;
        }
    }

    uint8_t *ptr = static_cast<uint8_t *>(buffer->mapped_address);
    if (rect && buffer->desc.format != AHARDWAREBUFFER_FORMAT_BLOB) {
        ptr += (rect->top * buffer->desc.stride + rect->left) * buffer->bytes_per_pixel;
    }

    *out_virtual_address = ptr;
    buffer->is_locked = true;
    buffer->locked_usage = usage;
    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::LockPlanes(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                                      const ARect *rect, AHardwareBuffer_Planes *out_planes)
{
    (void)fence;
    if (!buffer || !out_planes) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    void *base_addr = nullptr;
    int ret = Lock(buffer, usage, fence, rect, &base_addr);
    if (ret != AHARDWAREBUFFER_STATUS_OK) {
        return ret;
    }

    uint8_t *base = static_cast<uint8_t *>(base_addr);
    if (buffer->desc.format == AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420) {
        out_planes->planeCount = 3;

        uint32_t y_stride = buffer->desc.stride;
        uint32_t uv_stride = (buffer->desc.width / 2 + 63) / 64 * 64;
        size_t y_size = static_cast<size_t>(y_stride) * buffer->desc.height;
        size_t uv_size = static_cast<size_t>(uv_stride) * (buffer->desc.height / 2);

        out_planes->planes[0].data = base;
        out_planes->planes[0].pixelStride = 1;
        out_planes->planes[0].rowStride = y_stride;

        out_planes->planes[1].data = base + y_size;
        out_planes->planes[1].pixelStride = 1;
        out_planes->planes[1].rowStride = uv_stride;

        out_planes->planes[2].data = base + y_size + uv_size;
        out_planes->planes[2].pixelStride = 1;
        out_planes->planes[2].rowStride = uv_stride;
    } else {
        out_planes->planeCount = 1;
        out_planes->planes[0].data = base;
        out_planes->planes[0].pixelStride = buffer->bytes_per_pixel;
        out_planes->planes[0].rowStride = buffer->desc.stride * buffer->bytes_per_pixel;
        std::memset(&out_planes->planes[1], 0, sizeof(AHardwareBuffer_Plane) * 3);
    }

    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::LockAndGetInfo(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                                          const ARect *rect, void **out_virtual_address,
                                          int32_t *out_bytes_per_pixel,
                                          int32_t *out_bytes_per_stride)
{
    int ret = Lock(buffer, usage, fence, rect, out_virtual_address);
    if (ret != AHARDWAREBUFFER_STATUS_OK) {
        return ret;
    }

    if (out_bytes_per_pixel) {
        *out_bytes_per_pixel = static_cast<int32_t>(buffer->bytes_per_pixel);
    }
    if (out_bytes_per_stride) {
        *out_bytes_per_stride = static_cast<int32_t>(buffer->desc.stride * buffer->bytes_per_pixel);
    }
    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::Unlock(AHardwareBuffer *buffer, int32_t *fence)
{
    if (!buffer) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    if (!buffer->is_locked) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    buffer->is_locked = false;
    buffer->locked_usage = 0;
    if (fence) {
        *fence = -1;
    }
    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::GetId(const AHardwareBuffer *buffer, uint64_t *out_id) const
{
    if (!buffer || !out_id) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }
    *out_id = buffer->id;
    return AHARDWAREBUFFER_STATUS_OK;
}

int HardwareBufferManager::IsSupported(const AHardwareBuffer_Desc &desc) const
{
    if (desc.width == 0 || desc.height == 0 || desc.layers == 0) {
        return 0;
    }
    return IsFormatSupported(desc.format) ? 1 : 0;
}

int HardwareBufferManager::SendHandleToSocket(const AHardwareBuffer *buffer, int socket_fd)
{
    if (!buffer || socket_fd < 0) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }

    AhwbWirePacket pkt{};
    pkt.magic = 0x41485742;
    pkt.id = buffer->id;
    pkt.desc = buffer->desc;
    pkt.total_size = buffer->total_size;

    int64_t written = syscall::SyscallDispatcher::Dispatch(
        1 /* sys_write */, static_cast<uint64_t>(socket_fd), reinterpret_cast<uint64_t>(&pkt),
        sizeof(pkt), 0, 0, 0);

    return (written == sizeof(pkt)) ? AHARDWAREBUFFER_STATUS_OK
                                    : AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR;
}

int HardwareBufferManager::RecvHandleFromSocket(int socket_fd, AHardwareBuffer **out_buffer)
{
    if (socket_fd < 0 || !out_buffer) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }
    *out_buffer = nullptr;

    AhwbWirePacket pkt{};
    int64_t nread = syscall::SyscallDispatcher::Dispatch(
        0 /* sys_read */, static_cast<uint64_t>(socket_fd), reinterpret_cast<uint64_t>(&pkt),
        sizeof(pkt), 0, 0, 0);

    if (nread != sizeof(pkt) || pkt.magic != 0x41485742) {
        return AHARDWAREBUFFER_STATUS_UNKNOWN_ERROR;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(pkt.id);
    if (it != buffers_.end()) {
        it->second->ref_count.fetch_add(1, std::memory_order_relaxed);
        *out_buffer = it->second.get();
        return AHARDWAREBUFFER_STATUS_OK;
    }

    return AHARDWAREBUFFER_STATUS_INVALID_ARG;
}

bool HardwareBufferManager::IsValid(const AHardwareBuffer *buffer) const
{
    if (!buffer) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(buffer->id) != buffers_.end();
}

std::shared_ptr<AHardwareBuffer> HardwareBufferManager::FindById(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(id);
    if (it != buffers_.end()) {
        return it->second;
    }
    return nullptr;
}

void HardwareBufferManager::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[id, buf] : buffers_) {
        if (buf->mapped_address) {
            mem::SharedMemoryManager::Instance().UnmapShared(buf->mapped_address);
            buf->mapped_address = nullptr;
        }
    }
    buffers_.clear();
    next_id_ = 1;
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc,
                                                       AHardwareBuffer **outBuffer)
{
    if (!desc || !outBuffer) {
        return AHARDWAREBUFFER_STATUS_INVALID_ARG;
    }
    return portage::ndk::HardwareBufferManager::Instance().Allocate(*desc, outBuffer);
}

__attribute__((sysv_abi)) void AHardwareBuffer_acquire(AHardwareBuffer *buffer)
{
    portage::ndk::HardwareBufferManager::Instance().Acquire(buffer);
}

__attribute__((sysv_abi)) void AHardwareBuffer_release(AHardwareBuffer *buffer)
{
    portage::ndk::HardwareBufferManager::Instance().Release(buffer);
}

__attribute__((sysv_abi)) void AHardwareBuffer_describe(const AHardwareBuffer *buffer,
                                                        AHardwareBuffer_Desc *outDesc)
{
    portage::ndk::HardwareBufferManager::Instance().Describe(buffer, outDesc);
}

__attribute__((sysv_abi)) int AHardwareBuffer_lock(AHardwareBuffer *buffer, uint64_t usage,
                                                   int32_t fence, const ARect *rect,
                                                   void **outVirtualAddress)
{
    return portage::ndk::HardwareBufferManager::Instance().Lock(buffer, usage, fence, rect,
                                                                outVirtualAddress);
}

__attribute__((sysv_abi)) int AHardwareBuffer_unlock(AHardwareBuffer *buffer, int32_t *fence)
{
    return portage::ndk::HardwareBufferManager::Instance().Unlock(buffer, fence);
}

__attribute__((sysv_abi)) int AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage,
                                                         int32_t fence, const ARect *rect,
                                                         AHardwareBuffer_Planes *outPlanes)
{
    return portage::ndk::HardwareBufferManager::Instance().LockPlanes(buffer, usage, fence, rect,
                                                                      outPlanes);
}

__attribute__((sysv_abi)) int AHardwareBuffer_getId(const AHardwareBuffer *buffer, uint64_t *outId)
{
    return portage::ndk::HardwareBufferManager::Instance().GetId(buffer, outId);
}

__attribute__((sysv_abi)) int AHardwareBuffer_isSupported(const AHardwareBuffer_Desc *desc)
{
    if (!desc) {
        return 0;
    }
    return portage::ndk::HardwareBufferManager::Instance().IsSupported(*desc);
}

__attribute__((sysv_abi)) int
AHardwareBuffer_lockAndGetInfo(AHardwareBuffer *buffer, uint64_t usage, int32_t fence,
                               const ARect *rect, void **outVirtualAddress,
                               int32_t *outBytesPerPixel, int32_t *outBytesPerStride)
{
    return portage::ndk::HardwareBufferManager::Instance().LockAndGetInfo(
        buffer, usage, fence, rect, outVirtualAddress, outBytesPerPixel, outBytesPerStride);
}

__attribute__((sysv_abi)) int AHardwareBuffer_sendHandleToUnixSocket(const AHardwareBuffer *buffer,
                                                                     int socketFd)
{
    return portage::ndk::HardwareBufferManager::Instance().SendHandleToSocket(buffer, socketFd);
}

__attribute__((sysv_abi)) int AHardwareBuffer_recvHandleFromUnixSocket(int socketFd,
                                                                       AHardwareBuffer **outBuffer)
{
    return portage::ndk::HardwareBufferManager::Instance().RecvHandleFromSocket(socketFd,
                                                                                outBuffer);
}

} // extern "C"
