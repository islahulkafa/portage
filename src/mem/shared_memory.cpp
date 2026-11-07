#include "shared_memory.hpp"

#include <algorithm>

namespace portage::mem {

SharedMemoryRegion::~SharedMemoryRegion()
{
    for (void *view : mapped_views) {
        if (view) {
            ::UnmapViewOfFile(view);
        }
    }
    mapped_views.clear();

    if (mapping_handle != INVALID_HANDLE_VALUE && mapping_handle != nullptr) {
        ::CloseHandle(mapping_handle);
        mapping_handle = INVALID_HANDLE_VALUE;
    }
}

SharedMemoryManager &SharedMemoryManager::Instance() noexcept
{
    static SharedMemoryManager instance;
    return instance;
}

std::shared_ptr<SharedMemoryRegion> SharedMemoryManager::CreateRegion(std::string_view name,
                                                                      size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto region = std::make_shared<SharedMemoryRegion>();
    region->id = next_id_++;
    region->name = std::string(name);
    region->size = (size > 0) ? size : 4096;
    region->prot_mask = 7; // PROT_READ | PROT_WRITE | PROT_EXEC
    region->seals = 0;
    region->is_pinned = true;

    ULARGE_INTEGER uli;
    uli.QuadPart = region->size;
    region->mapping_handle = ::CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, uli.HighPart, uli.LowPart, nullptr);

    regions_[region->id] = region;
    return region;
}

std::shared_ptr<SharedMemoryRegion> SharedMemoryManager::CreateAshmem()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto region = std::make_shared<SharedMemoryRegion>();
    region->id = next_id_++;
    region->name = "";
    region->size = 0;
    region->prot_mask = 7;
    region->seals = 0;
    region->mapping_handle = INVALID_HANDLE_VALUE;
    region->is_pinned = true;

    regions_[region->id] = region;
    return region;
}

int SharedMemoryManager::SetRegionSize(SharedMemoryRegion &region, size_t size)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (size == 0) {
        return -22; // -EINVAL
    }

    if (region.mapping_handle != INVALID_HANDLE_VALUE && region.mapping_handle != nullptr) {
        if (region.size == size) {
            return 0;
        }
        // If seals prevent resizing
        if ((region.seals & 2 /* F_SEAL_SHRINK */) && size < region.size) {
            return -1; // EPERM
        }
        if ((region.seals & 4 /* F_SEAL_GROW */) && size > region.size) {
            return -1; // EPERM
        }

        // Recreate mapping with new size
        ::CloseHandle(region.mapping_handle);
        region.mapping_handle = INVALID_HANDLE_VALUE;
    }

    ULARGE_INTEGER uli;
    uli.QuadPart = size;
    region.mapping_handle = ::CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, uli.HighPart, uli.LowPart, nullptr);
    if (!region.mapping_handle || region.mapping_handle == INVALID_HANDLE_VALUE) {
        return -12; // -ENOMEM
    }

    region.size = size;
    return 0;
}

int SharedMemoryManager::SetRegionName(SharedMemoryRegion &region, std::string_view name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    region.name = std::string(name);
    return 0;
}

int SharedMemoryManager::SetProtMask(SharedMemoryRegion &region, int prot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Protection can only be made more restrictive, never more permissive
    if ((prot & ~region.prot_mask) != 0) {
        return -22; // -EINVAL
    }
    region.prot_mask = prot;
    return 0;
}

int SharedMemoryManager::PinRegion(SharedMemoryRegion &region, size_t, size_t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    region.is_pinned = true;
    return 0; // ASHMEM_NOT_PURGED
}

int SharedMemoryManager::UnpinRegion(SharedMemoryRegion &region, size_t, size_t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    region.is_pinned = false;
    return 0; // ASHMEM_IS_UNPINNED
}

int SharedMemoryManager::GetPinStatus(const SharedMemoryRegion &region) const
{
    return region.is_pinned ? 1 : 0; // ASHMEM_IS_PINNED : ASHMEM_IS_UNPINNED
}

void *SharedMemoryManager::MapShared(SharedMemoryRegion &region, void *desired_addr, size_t length,
                                     int prot, int flags, uint64_t offset)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if ((prot & ~region.prot_mask) != 0) {
        return nullptr; // Exceeds allowed protection mask
    }

    if (region.mapping_handle == INVALID_HANDLE_VALUE || region.mapping_handle == nullptr) {
        return nullptr;
    }

    DWORD access = 0;
    if ((prot & 2) != 0) { // PROT_WRITE
        access = FILE_MAP_ALL_ACCESS;
    } else {
        access = FILE_MAP_READ;
    }
    if ((prot & 4) != 0) { // PROT_EXEC
        access |= FILE_MAP_EXECUTE;
    }

    ULARGE_INTEGER uli_offset;
    uli_offset.QuadPart = offset;

    void *view = nullptr;
    if (desired_addr != nullptr) {
        view = ::MapViewOfFileEx(region.mapping_handle, access, uli_offset.HighPart,
                                 uli_offset.LowPart, length, desired_addr);
    }
    if (!view && (flags & 16 /* MAP_FIXED */) == 0) {
        view = ::MapViewOfFile(region.mapping_handle, access, uli_offset.HighPart,
                               uli_offset.LowPart, length);
    }

    if (view) {
        region.mapped_views.push_back(view);
        view_to_region_[view] = regions_[region.id];
    }
    return view;
}

bool SharedMemoryManager::UnmapShared(void *addr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = view_to_region_.find(addr);
    if (it == view_to_region_.end()) {
        return false;
    }

    auto region = it->second;
    view_to_region_.erase(it);

    if (region) {
        auto vit = std::find(region->mapped_views.begin(), region->mapped_views.end(), addr);
        if (vit != region->mapped_views.end()) {
            region->mapped_views.erase(vit);
        }
    }

    ::UnmapViewOfFile(addr);
    return true;
}

std::shared_ptr<SharedMemoryRegion> SharedMemoryManager::FindRegionByAddress(void *addr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = view_to_region_.find(addr);
    return (it != view_to_region_.end()) ? it->second : nullptr;
}

void SharedMemoryManager::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[addr, region] : view_to_region_) {
        (void)region;
        ::UnmapViewOfFile(addr);
    }
    view_to_region_.clear();
    regions_.clear();
    next_id_ = 1;
}

} // namespace portage::mem
