#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace portage::mem {

struct SharedMemoryRegion {
    int id{0};
    std::string name{};
    size_t size{0};
    int prot_mask{7}; // PROT_READ (1) | PROT_WRITE (2) | PROT_EXEC (4)
    int seals{0};
    HANDLE mapping_handle{INVALID_HANDLE_VALUE};
    bool is_pinned{true};
    std::vector<void *> mapped_views{};

    ~SharedMemoryRegion();
};

class SharedMemoryManager {
  public:
    static SharedMemoryManager &Instance() noexcept;

    std::shared_ptr<SharedMemoryRegion> CreateRegion(std::string_view name, size_t size);
    std::shared_ptr<SharedMemoryRegion> CreateAshmem();

    int SetRegionSize(SharedMemoryRegion &region, size_t size);
    int SetRegionName(SharedMemoryRegion &region, std::string_view name);
    int SetProtMask(SharedMemoryRegion &region, int prot);
    int PinRegion(SharedMemoryRegion &region, size_t offset, size_t len);
    int UnpinRegion(SharedMemoryRegion &region, size_t offset, size_t len);
    int GetPinStatus(const SharedMemoryRegion &region) const;

    void *MapShared(SharedMemoryRegion &region, void *desired_addr, size_t length, int prot,
                    int flags, uint64_t offset);
    bool UnmapShared(void *addr);

    std::shared_ptr<SharedMemoryRegion> FindRegionByAddress(void *addr);

    void Reset();

  private:
    SharedMemoryManager() = default;
    ~SharedMemoryManager() = default;

    SharedMemoryManager(const SharedMemoryManager &) = delete;
    SharedMemoryManager &operator=(const SharedMemoryManager &) = delete;

    std::mutex mutex_{};
    std::unordered_map<int, std::shared_ptr<SharedMemoryRegion>> regions_{};
    std::unordered_map<void *, std::shared_ptr<SharedMemoryRegion>> view_to_region_{};
    int next_id_{1};
};

} // namespace portage::mem
