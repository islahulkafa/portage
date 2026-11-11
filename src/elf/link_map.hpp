#pragma once

#include "elf/elf_loader.hpp"
#include "elf/elf_types.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace portage::elf {

struct TlsModuleTemplate {
    size_t modid{0};
    uintptr_t image_addr{0};
    size_t filesz{0};
    size_t memsz{0};
    size_t align{16};
};

struct CxaAtExitEntry {
    __attribute__((sysv_abi)) void (*func)(void *){nullptr};
    void *arg{nullptr};
    void *dso_handle{nullptr};
    void *module_handle{nullptr};
};

struct LinkMapEntry {
    void *handle{nullptr};
    std::string name;
    std::string filepath;
    LoadedElf elf;
    int ref_count{1};
    int flags{RTLD_LOCAL};
    bool is_virtual{false};
    bool init_called{false};
    bool fini_called{false};
    link_map_raw raw_link_map{};
    std::vector<std::shared_ptr<LinkMapEntry>> dependencies;
};

class LinkMap {
  public:
    using PhdrCallback = __attribute__((sysv_abi)) int (*)(dl_phdr_info *info, size_t size,
                                                           void *data);
    using CxaAtExitFn = __attribute__((sysv_abi)) void (*)(void *);

    static LinkMap &Instance() noexcept;

    void SetAddressSpace(mem::AddressSpace *as) noexcept { as_ = as; }
    void SetVfs(const portage::fs::Vfs *vfs) noexcept { vfs_ = vfs; }

    void AddSearchPath(std::string_view path);
    [[nodiscard]] const std::vector<std::string> &GetSearchPaths() const noexcept
    {
        return search_paths_;
    }

    void PushModuleRunpaths(const std::vector<std::string> &rpaths,
                            const std::vector<std::string> &runpaths);
    void PopModuleRunpaths();

    std::shared_ptr<LinkMapEntry> RegisterLoadedElf(LoadedElf elf, std::string_view name);

    void *DlOpen(const char *filename, int flags);
    void *DlOpenExt(const char *filename, int flags, const android_dlextinfo *extinfo);
    void *DlSym(void *handle, const char *symbol);
    void *DlSymWithCaller(void *handle, const char *symbol, uintptr_t caller_addr);
    int DlClose(void *handle);
    char *DlError();
    int DlAddr(const void *addr, Dl_info *info);
    int DlInfo(void *handle, int request, void *info);
    int IteratePhdr(PhdrCallback callback, void *data);

    [[nodiscard]] uintptr_t GetTlsdescReturnThunk();
    [[nodiscard]] uintptr_t GetTlsdescDynamicThunk();
    [[nodiscard]] uintptr_t AllocateLazyPltTrampoline(uintptr_t got_slot, const char *sym_name);
    [[nodiscard]] uintptr_t ResolveLazySymbol(uintptr_t got_slot, const char *sym_name);
    [[nodiscard]] uintptr_t AllocateUnresolvedTrap(std::string_view sym_name);
    [[nodiscard]] std::vector<std::string> CollectUnresolvedSymbols() const;
    [[nodiscard]] static bool IsBuiltinSystemLibrary(std::string_view name);
    [[nodiscard]] uint64_t GetLazyResolveCount() const noexcept
    {
        return lazy_resolve_count_.load();
    }

    size_t RegisterTlsModule(uintptr_t image_addr, size_t filesz, size_t memsz, size_t align);
    bool ResolveTlsSymbol(std::string_view name, size_t &out_modid, uintptr_t &out_offset) const;
    void *GetTlsAddress(uint64_t modid, uint64_t offset);
    void *TryGetLoadedTlsBlock(uint64_t modid) const;
    void CleanupCurrentThreadTls();

    int RegisterCxaAtExit(CxaAtExitFn func, void *arg, void *dso_handle);
    void FinalizeCxaAtExit(void *dso_handle);
    void FinalizeAll();

    [[nodiscard]] uintptr_t ResolveGlobalSymbol(std::string_view name) const;
    [[nodiscard]] bool ResolveGlobalSymbolWithSize(std::string_view name, uintptr_t &out_addr,
                                                   size_t &out_size) const;
    [[nodiscard]] std::shared_ptr<LinkMapEntry> FindModule(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<LinkMapEntry> FindModuleByAddress(uintptr_t addr) const;

    void Reset();

  private:
    LinkMap() = default;

    struct ScopedRunpath {
        std::vector<std::string> rpaths;
        std::vector<std::string> runpaths;
    };

    std::shared_ptr<LinkMapEntry> LoadLibraryInternal(std::string_view libname, int flags,
                                                      std::unordered_set<std::string> &loading);
    std::string ResolveLibraryPath(std::string_view libname) const;
    void CallInit(const std::shared_ptr<LinkMapEntry> &entry);
    void CallFini(const std::shared_ptr<LinkMapEntry> &entry);
    [[nodiscard]] uintptr_t ResolveSymbolForModule(const LinkMapEntry &entry,
                                                   std::string_view sym_name) const;
    void *AllocateExecutableCode(size_t size);
    void RebuildRawLinkMapChain();

    mem::AddressSpace *as_{nullptr};
    const portage::fs::Vfs *vfs_{nullptr};
    std::vector<std::string> search_paths_;
    std::vector<ScopedRunpath> active_runpaths_;
    std::vector<std::shared_ptr<LinkMapEntry>> modules_;
    std::unordered_map<void *, std::shared_ptr<LinkMapEntry>> modules_by_handle_;
    std::unordered_map<std::string, std::shared_ptr<LinkMapEntry>> modules_by_name_;
    std::unordered_map<size_t, TlsModuleTemplate> tls_modules_;
    std::vector<CxaAtExitEntry> cxa_atexit_entries_;
    std::vector<std::pair<void *, size_t>> exec_pools_;
    uint8_t *current_pool_{nullptr};
    size_t current_pool_used_{0};
    size_t current_pool_cap_{0};
    uintptr_t tlsdesc_return_thunk_{0};
    uintptr_t tlsdesc_dynamic_thunk_{0};
    std::deque<std::string> unresolved_trap_names_;
    std::unordered_map<std::string, uintptr_t> unresolved_traps_;
    std::atomic<uint64_t> lazy_resolve_count_{0};
    uintptr_t next_handle_{1};
    size_t next_tls_modid_{1};
    uint64_t dlpi_adds_{0};
    uint64_t dlpi_subs_{0};
    mutable std::string last_error_;
    mutable std::recursive_mutex mutex_;
};

} // namespace portage::elf

extern "C" {

__attribute__((sysv_abi)) void *dlopen(const char *filename, int flags);
__attribute__((sysv_abi)) void *android_dlopen_ext(const char *filename, int flags,
                                                   const portage::elf::android_dlextinfo *extinfo);
__attribute__((sysv_abi)) void *dlsym(void *handle, const char *symbol);
__attribute__((sysv_abi)) int dlclose(void *handle);
__attribute__((sysv_abi)) char *dlerror();
__attribute__((sysv_abi)) int dladdr(const void *addr, portage::elf::Dl_info *info);
__attribute__((sysv_abi)) int dlinfo(void *handle, int request, void *info);
__attribute__((sysv_abi)) int dl_iterate_phdr(portage::elf::LinkMap::PhdrCallback callback,
                                              void *data);
__attribute__((sysv_abi)) void *__tls_get_addr(const portage::elf::TlsIndex *ti);
__attribute__((sysv_abi)) int portage_cxa_atexit(portage::elf::LinkMap::CxaAtExitFn func, void *arg,
                                                 void *dso_handle) noexcept;
__attribute__((sysv_abi)) void portage_cxa_finalize(void *dso_handle) noexcept;
__attribute__((sysv_abi)) int
portage_cxa_thread_atexit_impl(portage::elf::LinkMap::CxaAtExitFn dtor, void *obj,
                               void *dso_handle) noexcept;
__attribute__((sysv_abi)) int
portage_guest_atexit(__attribute__((sysv_abi)) void (*func)()) noexcept;

} // extern "C"
