#pragma once

#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace portage::elf {

struct LinkMapEntry {
    void *handle{nullptr};
    std::string name;
    std::string filepath;
    LoadedElf elf;
    int ref_count{1};
    int flags{RTLD_LOCAL};
    bool init_called{false};
    bool fini_called{false};
    std::vector<std::shared_ptr<LinkMapEntry>> dependencies;
};

class LinkMap {
  public:
    static LinkMap &Instance() noexcept;

    void SetAddressSpace(mem::AddressSpace *as) noexcept { as_ = as; }
    void SetVfs(const portage::fs::Vfs *vfs) noexcept { vfs_ = vfs; }

    void AddSearchPath(std::string_view path);
    [[nodiscard]] const std::vector<std::string> &GetSearchPaths() const noexcept
    {
        return search_paths_;
    }

    std::shared_ptr<LinkMapEntry> RegisterLoadedElf(LoadedElf elf, std::string_view name);

    void *DlOpen(const char *filename, int flags);
    void *DlSym(void *handle, const char *symbol);
    int DlClose(void *handle);
    char *DlError();
    int DlAddr(const void *addr, Dl_info *info);

    [[nodiscard]] uintptr_t ResolveGlobalSymbol(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<LinkMapEntry> FindModule(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<LinkMapEntry> FindModuleByAddress(uintptr_t addr) const;

    void Reset();

  private:
    LinkMap() = default;

    std::shared_ptr<LinkMapEntry> LoadLibraryInternal(std::string_view libname, int flags,
                                                      std::unordered_set<std::string> &loading);
    std::string ResolveLibraryPath(std::string_view libname) const;
    void CallInit(const std::shared_ptr<LinkMapEntry> &entry);
    void CallFini(const std::shared_ptr<LinkMapEntry> &entry);

    mem::AddressSpace *as_{nullptr};
    const portage::fs::Vfs *vfs_{nullptr};
    std::vector<std::string> search_paths_;
    std::vector<std::shared_ptr<LinkMapEntry>> modules_;
    std::unordered_map<void *, std::shared_ptr<LinkMapEntry>> modules_by_handle_;
    std::unordered_map<std::string, std::shared_ptr<LinkMapEntry>> modules_by_name_;
    uintptr_t next_handle_{1};
    mutable std::string last_error_;
    mutable std::mutex mutex_;
};

} // namespace portage::elf

extern "C" {

__attribute__((sysv_abi)) void *dlopen(const char *filename, int flags);
__attribute__((sysv_abi)) void *dlsym(void *handle, const char *symbol);
__attribute__((sysv_abi)) int dlclose(void *handle);
__attribute__((sysv_abi)) char *dlerror();
__attribute__((sysv_abi)) int dladdr(const void *addr, portage::elf::Dl_info *info);

} // extern "C"
