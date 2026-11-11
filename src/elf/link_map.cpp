#include "elf/link_map.hpp"
#include "fs/fd_table.hpp"
#include "mem/shared_memory.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <immintrin.h>
#include <iostream>
#include <windows.h>

namespace portage::elf {

namespace {

std::mutex g_tls_mutex;
std::unordered_map<DWORD, std::unordered_map<size_t, std::vector<uint8_t>>> g_thread_tls_blocks;

__attribute__((sysv_abi)) void AtExitThunk(void *arg)
{
    using ZeroArgFn = __attribute__((sysv_abi)) void (*)();
    auto fn = reinterpret_cast<ZeroArgFn>(arg);
    if (fn) {
        fn();
    }
}

__attribute__((sysv_abi)) uintptr_t PortageDlRuntimeResolve(uintptr_t got_slot,
                                                             const char *sym_name)
{
    return LinkMap::Instance().ResolveLazySymbol(got_slot, sym_name);
}

__attribute__((sysv_abi)) ptrdiff_t
PortageTlsdescDynamicResolve(const tlsdesc_dynamic_arg *arg)
{
    if (arg == nullptr) {
        return 0;
    }
    void *addr = LinkMap::Instance().GetTlsAddress(arg->ti.ti_module, arg->ti.ti_offset);
    uintptr_t fs_base = _readfsbase_u64();
    return static_cast<ptrdiff_t>(reinterpret_cast<uintptr_t>(addr) - fs_base);
}

} // namespace

LinkMap &LinkMap::Instance() noexcept
{
    static LinkMap instance;
    return instance;
}

void LinkMap::Reset()
{
    {
        std::lock_guard lock(g_tls_mutex);
        g_thread_tls_blocks.clear();
    }
    std::lock_guard lock(mutex_);
    modules_.clear();
    modules_by_handle_.clear();
    modules_by_name_.clear();
    search_paths_.clear();
    active_runpaths_.clear();
    tls_modules_.clear();
    cxa_atexit_entries_.clear();
    for (const auto &[pool_ptr, pool_sz] : exec_pools_) {
        if (pool_ptr) {
            VirtualFree(pool_ptr, 0, MEM_RELEASE);
        }
    }
    exec_pools_.clear();
    current_pool_ = nullptr;
    current_pool_used_ = 0;
    current_pool_cap_ = 0;
    tlsdesc_return_thunk_ = 0;
    tlsdesc_dynamic_thunk_ = 0;
    lazy_resolve_count_.store(0, std::memory_order_relaxed);
    last_error_.clear();
    next_handle_ = 1;
    next_tls_modid_ = 1;
    dlpi_adds_ = 0;
    dlpi_subs_ = 0;
    as_ = nullptr;
    vfs_ = nullptr;
}

void LinkMap::AddSearchPath(std::string_view path)
{
    std::lock_guard lock(mutex_);
    std::string p(path);
    if (std::find(search_paths_.begin(), search_paths_.end(), p) == search_paths_.end()) {
        search_paths_.push_back(p);
    }
}

void LinkMap::PushModuleRunpaths(const std::vector<std::string> &rpaths,
                                 const std::vector<std::string> &runpaths)
{
    std::lock_guard lock(mutex_);
    active_runpaths_.push_back({rpaths, runpaths});
}

void LinkMap::PopModuleRunpaths()
{
    std::lock_guard lock(mutex_);
    if (!active_runpaths_.empty()) {
        active_runpaths_.pop_back();
    }
}

std::string LinkMap::ResolveLibraryPath(std::string_view libname) const
{
    std::string name(libname);

    auto try_candidate = [&](const std::string &candidate) -> std::string {
        if (vfs_ != nullptr) {
            auto resolved = vfs_->Resolve(candidate);
            if (!resolved.host_path.empty() && std::filesystem::exists(resolved.host_path)) {
                return resolved.host_path.string();
            }
        }
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        return {};
    };

    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        if (auto found = try_candidate(name); !found.empty()) {
            return found;
        }
    }

    for (auto it = active_runpaths_.rbegin(); it != active_runpaths_.rend(); ++it) {
        if (it->runpaths.empty()) {
            for (const auto &dir : it->rpaths) {
                if (auto found = try_candidate(dir + "/" + name); !found.empty()) {
                    return found;
                }
            }
        }
    }

    for (const auto &dir : search_paths_) {
        if (auto found = try_candidate(dir + "/" + name); !found.empty()) {
            return found;
        }
    }

    for (auto it = active_runpaths_.rbegin(); it != active_runpaths_.rend(); ++it) {
        for (const auto &dir : it->runpaths) {
            if (auto found = try_candidate(dir + "/" + name); !found.empty()) {
                return found;
            }
        }
        if (!it->runpaths.empty()) {
            for (const auto &dir : it->rpaths) {
                if (auto found = try_candidate(dir + "/" + name); !found.empty()) {
                    return found;
                }
            }
        }
    }

    if (std::filesystem::exists(name)) {
        return name;
    }

    return {};
}

std::shared_ptr<LinkMapEntry> LinkMap::FindModule(std::string_view name) const
{
    std::lock_guard lock(mutex_);
    auto it = modules_by_name_.find(std::string(name));
    if (it != modules_by_name_.end()) {
        return it->second;
    }
    for (const auto &m : modules_) {
        if (m->name == name || m->filepath == name || m->elf.soname == name ||
            std::filesystem::path(m->filepath).filename().string() == name) {
            return m;
        }
    }
    return nullptr;
}

std::shared_ptr<LinkMapEntry> LinkMap::FindModuleByAddress(uintptr_t addr) const
{
    std::lock_guard lock(mutex_);
    for (const auto &m : modules_) {
        for (const auto &seg : m->elf.segments) {
            if (addr >= seg.vaddr && addr < seg.vaddr + seg.mem_size) {
                return m;
            }
        }
    }
    return nullptr;
}

uintptr_t LinkMap::ResolveGlobalSymbol(std::string_view name) const
{
    if (uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(name); ndk_addr != 0) {
        return ndk_addr;
    }
    std::lock_guard lock(mutex_);
    for (const auto &m : modules_) {
        auto it = m->elf.symbols.find(std::string(name));
        if (it != m->elf.symbols.end()) {
            return it->second;
        }
    }
    return 0;
}

bool LinkMap::ResolveGlobalSymbolWithSize(std::string_view name, uintptr_t &out_addr,
                                          size_t &out_size) const
{
    std::lock_guard lock(mutex_);
    std::string key(name);
    for (const auto &m : modules_) {
        auto it = m->elf.symbols.find(key);
        if (it != m->elf.symbols.end()) {
            out_addr = it->second;
            auto sit = m->elf.symbol_sizes.find(key);
            out_size = (sit != m->elf.symbol_sizes.end()) ? sit->second : 0;
            return true;
        }
    }
    return false;
}

size_t LinkMap::RegisterTlsModule(uintptr_t image_addr, size_t filesz, size_t memsz, size_t align)
{
    std::lock_guard lock(mutex_);
    size_t modid = next_tls_modid_++;
    tls_modules_[modid] =
        TlsModuleTemplate{modid, image_addr, filesz, memsz, align == 0 ? 16 : align};
    return modid;
}

bool LinkMap::ResolveTlsSymbol(std::string_view name, size_t &out_modid,
                               uintptr_t &out_offset) const
{
    std::lock_guard lock(mutex_);
    std::string key(name);
    for (const auto &m : modules_) {
        if (m->elf.tls_modid == 0) {
            continue;
        }
        auto it = m->elf.tls_symbols.find(key);
        if (it != m->elf.tls_symbols.end()) {
            out_modid = m->elf.tls_modid;
            out_offset = it->second;
            return true;
        }
    }
    return false;
}

void *LinkMap::GetTlsAddress(uint64_t modid, uint64_t offset)
{
    TlsModuleTemplate tmpl{};
    bool has_tmpl = false;
    {
        std::lock_guard lock(mutex_);
        auto it = tls_modules_.find(static_cast<size_t>(modid));
        if (it != tls_modules_.end()) {
            tmpl = it->second;
            has_tmpl = true;
        }
    }

    std::lock_guard tls_lock(g_tls_mutex);
    DWORD tid = GetCurrentThreadId();
    auto &mod_map = g_thread_tls_blocks[tid];
    auto bit = mod_map.find(static_cast<size_t>(modid));
    if (bit == mod_map.end()) {
        size_t min_req = static_cast<size_t>(offset) + 64;
        size_t block_size = has_tmpl ? std::max(tmpl.memsz, min_req) : std::max<size_t>(4096, min_req);
        std::vector<uint8_t> block(block_size, 0);
        if (has_tmpl && tmpl.image_addr != 0 && tmpl.filesz > 0) {
            std::memcpy(block.data(), reinterpret_cast<const void *>(tmpl.image_addr),
                        std::min(tmpl.filesz, block_size));
        }
        bit = mod_map.emplace(static_cast<size_t>(modid), std::move(block)).first;
    } else if (offset >= bit->second.size()) {
        bit->second.resize(static_cast<size_t>(offset) + 64, 0);
    }

    return bit->second.data() + offset;
}

void *LinkMap::TryGetLoadedTlsBlock(uint64_t modid) const
{
    if (modid == 0) {
        return nullptr;
    }
    std::lock_guard tls_lock(g_tls_mutex);
    DWORD tid = GetCurrentThreadId();
    auto tit = g_thread_tls_blocks.find(tid);
    if (tit == g_thread_tls_blocks.end()) {
        return nullptr;
    }
    auto bit = tit->second.find(static_cast<size_t>(modid));
    if (bit == tit->second.end()) {
        return nullptr;
    }
    return const_cast<uint8_t *>(bit->second.data());
}

void LinkMap::CleanupCurrentThreadTls()
{
    std::lock_guard tls_lock(g_tls_mutex);
    g_thread_tls_blocks.erase(GetCurrentThreadId());
}

int LinkMap::RegisterCxaAtExit(CxaAtExitFn func, void *arg, void *dso_handle)
{
    if (func == nullptr) {
        return -1;
    }
    std::lock_guard lock(mutex_);
    void *mod_handle = nullptr;
    if (dso_handle != nullptr) {
        if (modules_by_handle_.contains(dso_handle)) {
            mod_handle = dso_handle;
        } else if (auto owner = FindModuleByAddress(reinterpret_cast<uintptr_t>(dso_handle))) {
            mod_handle = owner->handle;
        }
    }
    if (mod_handle == nullptr) {
        if (auto owner = FindModuleByAddress(reinterpret_cast<uintptr_t>(func))) {
            mod_handle = owner->handle;
        }
    }
    cxa_atexit_entries_.push_back(CxaAtExitEntry{func, arg, dso_handle, mod_handle});
    return 0;
}

void LinkMap::FinalizeCxaAtExit(void *dso_handle)
{
    while (true) {
        CxaAtExitEntry target{};
        bool found = false;
        {
            std::lock_guard lock(mutex_);
            for (auto it = cxa_atexit_entries_.rbegin(); it != cxa_atexit_entries_.rend(); ++it) {
                if (dso_handle == nullptr || it->dso_handle == dso_handle ||
                    it->module_handle == dso_handle) {
                    target = *it;
                    cxa_atexit_entries_.erase(std::next(it).base());
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            break;
        }
        if (target.func != nullptr) {
            target.func(target.arg);
        }
    }
}

void LinkMap::FinalizeAll()
{
    FinalizeCxaAtExit(nullptr);

    std::vector<std::shared_ptr<LinkMapEntry>> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = modules_;
    }
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
        CallFini(*it);
    }
}

uintptr_t LinkMap::ResolveSymbolForModule(const LinkMapEntry &entry, std::string_view sym_name) const
{
    if (uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(sym_name); ndk_addr != 0) {
        return ndk_addr;
    }
    uintptr_t addr = entry.elf.FindSymbol(sym_name);
    if (addr != 0) {
        return addr;
    }
    for (const auto &dep : entry.dependencies) {
        auto it = dep->elf.symbols.find(std::string(sym_name));
        if (it != dep->elf.symbols.end()) {
            return it->second;
        }
    }
    return ResolveGlobalSymbol(sym_name);
}

std::shared_ptr<LinkMapEntry> LinkMap::LoadLibraryInternal(std::string_view libname, int flags,
                                                           std::unordered_set<std::string> &loading)
{
    std::string name(libname);
    if (auto existing = FindModule(name)) {
        return existing;
    }

    std::string path = ResolveLibraryPath(name);
    if (path.empty()) {
        last_error_ = "dlopen failed: library not found: " + name;
        return nullptr;
    }

    std::string fn = std::filesystem::path(path).filename().string();
    if (loading.contains(name) || loading.contains(fn)) {
        return nullptr;
    }

    if (auto existing = FindModule(fn)) {
        return existing;
    }

    loading.insert(name);
    loading.insert(fn);

    if (as_ == nullptr) {
        last_error_ = "dlopen failed: address space not initialized";
        loading.erase(name);
        loading.erase(fn);
        return nullptr;
    }

    ElfLoader loader(as_);
    auto entry = std::make_shared<LinkMapEntry>();

    ElfLoader::DependencyLoader dep_loader = [&](const std::vector<std::string> &needed) {
        for (const auto &dep_name : needed) {
            auto dep_entry = LoadLibraryInternal(dep_name, flags, loading);
            if (dep_entry) {
                entry->dependencies.push_back(dep_entry);
            }
        }
    };

    ElfLoader::SymbolResolver resolver = [this, &entry](std::string_view sym_name) -> uintptr_t {
        return ResolveSymbolForModule(*entry, sym_name);
    };

    LoadOptions options{};
    options.lazy_binding = ((flags & RTLD_NOW) == 0);
    auto res = loader.LoadFromFile(path, vfs_, resolver, dep_loader, options);
    if (std::holds_alternative<LoaderError>(res)) {
        last_error_ = "dlopen failed: error loading " + path;
        loading.erase(name);
        loading.erase(fn);
        return nullptr;
    }

    entry->elf = std::move(std::get<LoadedElf>(res));
    entry->handle = reinterpret_cast<void *>(next_handle_++);
    entry->name = name;
    entry->filepath = path;
    entry->flags = flags;
    entry->ref_count = 1;

    modules_.push_back(entry);
    modules_by_handle_[entry->handle] = entry;
    modules_by_name_[entry->name] = entry;
    modules_by_name_[fn] = entry;
    if (!entry->elf.soname.empty()) {
        modules_by_name_[entry->elf.soname] = entry;
    }
    dlpi_adds_++;
    RebuildRawLinkMapChain();

    loading.erase(name);
    loading.erase(fn);

    return entry;
}

void LinkMap::CallInit(const std::shared_ptr<LinkMapEntry> &entry)
{
    if (!entry) {
        return;
    }

    std::vector<std::shared_ptr<LinkMapEntry>> deps;
    std::vector<uintptr_t> init_fns;
    {
        std::lock_guard lock(mutex_);
        if (entry->init_called) {
            return;
        }
        entry->init_called = true;
        deps = entry->dependencies;
        init_fns = entry->elf.init_functions;
    }

    for (const auto &dep : deps) {
        CallInit(dep);
    }

    using SysVInitFn = __attribute__((sysv_abi)) void (*)();
    for (uintptr_t fn_addr : init_fns) {
        if (fn_addr != 0) {
            auto fn = reinterpret_cast<SysVInitFn>(fn_addr);
            fn();
        }
    }
}

void LinkMap::CallFini(const std::shared_ptr<LinkMapEntry> &entry)
{
    if (!entry) {
        return;
    }

    std::vector<std::shared_ptr<LinkMapEntry>> deps;
    std::vector<uintptr_t> fini_fns;
    void *mod_handle = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (entry->fini_called) {
            return;
        }
        entry->fini_called = true;
        deps = entry->dependencies;
        fini_fns = entry->elf.fini_functions;
        mod_handle = entry->handle;
    }

    FinalizeCxaAtExit(mod_handle);

    using SysVFiniFn = __attribute__((sysv_abi)) void (*)();
    for (auto it = fini_fns.rbegin(); it != fini_fns.rend(); ++it) {
        if (*it != 0) {
            auto fn = reinterpret_cast<SysVFiniFn>(*it);
            fn();
        }
    }

    for (auto it = deps.rbegin(); it != deps.rend(); ++it) {
        CallFini(*it);
    }
}

std::shared_ptr<LinkMapEntry> LinkMap::RegisterLoadedElf(LoadedElf elf, std::string_view name)
{
    std::shared_ptr<LinkMapEntry> entry;
    {
        std::lock_guard lock(mutex_);
        entry = std::make_shared<LinkMapEntry>();
        entry->handle = reinterpret_cast<void *>(next_handle_++);
        entry->name = std::string(name);
        entry->filepath = elf.filepath.empty() ? std::string(name) : elf.filepath;
        entry->elf = std::move(elf);
        entry->ref_count = 1;

        if (entry->elf.tls_memsz > 0 && entry->elf.tls_modid == 0) {
            entry->elf.tls_modid =
                RegisterTlsModule(entry->elf.tls_image_addr, entry->elf.tls_filesz,
                                  entry->elf.tls_memsz, entry->elf.tls_align);
        }

        modules_.push_back(entry);
        modules_by_handle_[entry->handle] = entry;
        modules_by_name_[entry->name] = entry;
        std::string fn = std::filesystem::path(entry->filepath).filename().string();
        modules_by_name_[fn] = entry;
        if (!entry->elf.soname.empty()) {
            modules_by_name_[entry->elf.soname] = entry;
        }
        dlpi_adds_++;
        RebuildRawLinkMapChain();
    }

    CallInit(entry);
    return entry;
}

void *LinkMap::DlOpen(const char *filename, int flags)
{
    std::shared_ptr<LinkMapEntry> entry;
    {
        std::lock_guard lock(mutex_);

        if (filename == nullptr) {
            if (modules_.empty()) {
                last_error_ = "dlopen: no loaded modules";
                return nullptr;
            }
            return modules_.front()->handle;
        }

        if (auto existing = FindModule(filename)) {
            existing->ref_count++;
            last_error_.clear();
            return existing->handle;
        }

        std::unordered_set<std::string> loading;
        entry = LoadLibraryInternal(filename, flags, loading);
        if (!entry) {
            return nullptr;
        }
        last_error_.clear();
    }

    CallInit(entry);
    return entry->handle;
}

void *LinkMap::DlOpenExt(const char *filename, int flags, const android_dlextinfo *extinfo)
{
    if (extinfo == nullptr) {
        return DlOpen(filename, flags);
    }

    if ((extinfo->flags & ANDROID_DLEXT_USE_LIBRARY_FD) == 0) {
        if ((extinfo->flags & ANDROID_DLEXT_RESERVED_ADDRESS) != 0 && filename != nullptr) {
            std::shared_ptr<LinkMapEntry> entry;
            {
                std::lock_guard lock(mutex_);
                if (auto existing = FindModule(filename)) {
                    existing->ref_count++;
                    last_error_.clear();
                    return existing->handle;
                }
                if (as_ == nullptr) {
                    last_error_ = "android_dlopen_ext: address space not initialized";
                    return nullptr;
                }
                std::string path = ResolveLibraryPath(filename);
                if (path.empty()) {
                    last_error_ =
                        "android_dlopen_ext failed: library not found: " + std::string(filename);
                    return nullptr;
                }

                entry = std::make_shared<LinkMapEntry>();
                std::unordered_set<std::string> loading;
                ElfLoader loader(as_);
                ElfLoader::DependencyLoader dep_loader =
                    [&](const std::vector<std::string> &needed) {
                        for (const auto &dep_name : needed) {
                            auto dep_entry = LoadLibraryInternal(dep_name, flags, loading);
                            if (dep_entry) {
                                entry->dependencies.push_back(dep_entry);
                            }
                        }
                    };
                ElfLoader::SymbolResolver resolver =
                    [this, &entry](std::string_view sym_name) -> uintptr_t {
                    return ResolveSymbolForModule(*entry, sym_name);
                };

                LoadOptions options{};
                options.lazy_binding = ((flags & RTLD_NOW) == 0);
                options.reserved_addr = extinfo->reserved_addr;
                options.reserved_size = extinfo->reserved_size;

                auto res = loader.LoadFromFile(path, vfs_, resolver, dep_loader, options);
                if (std::holds_alternative<LoaderError>(res)) {
                    last_error_ = "android_dlopen_ext failed: error loading " + path;
                    return nullptr;
                }

                std::string fn = std::filesystem::path(path).filename().string();
                entry->elf = std::move(std::get<LoadedElf>(res));
                entry->handle = reinterpret_cast<void *>(next_handle_++);
                entry->name = filename;
                entry->filepath = path;
                entry->flags = flags;
                entry->ref_count = 1;

                modules_.push_back(entry);
                modules_by_handle_[entry->handle] = entry;
                modules_by_name_[entry->name] = entry;
                modules_by_name_[fn] = entry;
                if (!entry->elf.soname.empty()) {
                    modules_by_name_[entry->elf.soname] = entry;
                }
                dlpi_adds_++;
                RebuildRawLinkMapChain();
                last_error_.clear();
            }
            CallInit(entry);
            return entry->handle;
        }
        return DlOpen(filename, flags);
    }

    int fd = extinfo->library_fd;
    auto *fd_table = syscall::SyscallDispatcher::GetFdTable();
    auto desc = fd_table ? fd_table->Get(fd) : nullptr;
    if (!desc) {
        std::lock_guard lock(mutex_);
        last_error_ = "android_dlopen_ext: invalid library_fd";
        return nullptr;
    }

    int64_t offset = ((extinfo->flags & ANDROID_DLEXT_USE_LIBRARY_FD_OFFSET) != 0)
                         ? extinfo->library_fd_offset
                         : 0;
    if (offset < 0) {
        std::lock_guard lock(mutex_);
        last_error_ = "android_dlopen_ext: invalid negative library_fd_offset";
        return nullptr;
    }

    std::vector<uint8_t> elf_bytes;
    if (desc->shared_memory && desc->shared_memory->mapping_handle != INVALID_HANDLE_VALUE) {
        size_t total_sz = desc->shared_memory->size;
        if (static_cast<uint64_t>(offset) >= total_sz) {
            std::lock_guard lock(mutex_);
            last_error_ = "android_dlopen_ext: offset exceeds shared memory size";
            return nullptr;
        }
        void *view =
            MapViewOfFile(desc->shared_memory->mapping_handle, FILE_MAP_READ, 0, 0, total_sz);
        if (!view) {
            std::lock_guard lock(mutex_);
            last_error_ = "android_dlopen_ext: failed to map shared memory fd";
            return nullptr;
        }
        size_t copy_sz = total_sz - static_cast<size_t>(offset);
        elf_bytes.resize(copy_sz);
        std::memcpy(elf_bytes.data(), static_cast<const uint8_t *>(view) + offset, copy_sz);
        UnmapViewOfFile(view);
    } else if (!desc->synthetic_data.empty()) {
        if (static_cast<size_t>(offset) >= desc->synthetic_data.size()) {
            std::lock_guard lock(mutex_);
            last_error_ = "android_dlopen_ext: offset exceeds synthetic file size";
            return nullptr;
        }
        elf_bytes.assign(
            reinterpret_cast<const uint8_t *>(desc->synthetic_data.data() + offset),
            reinterpret_cast<const uint8_t *>(desc->synthetic_data.data() +
                                              desc->synthetic_data.size()));
    } else if (desc->handle != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER file_sz{};
        if (!GetFileSizeEx(desc->handle, &file_sz) || offset >= file_sz.QuadPart) {
            std::lock_guard lock(mutex_);
            last_error_ = "android_dlopen_ext: failed to query file size or invalid offset";
            return nullptr;
        }
        LARGE_INTEGER zero{};
        LARGE_INTEGER orig_pos{};
        SetFilePointerEx(desc->handle, zero, &orig_pos, FILE_CURRENT);

        LARGE_INTEGER target_pos{};
        target_pos.QuadPart = offset;
        SetFilePointerEx(desc->handle, target_pos, nullptr, FILE_BEGIN);

        size_t remaining = static_cast<size_t>(file_sz.QuadPart - offset);
        elf_bytes.resize(remaining);
        size_t total_read = 0;
        while (total_read < remaining) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining - total_read, 1 << 20));
            DWORD bytes_read = 0;
            if (!ReadFile(desc->handle, elf_bytes.data() + total_read, chunk, &bytes_read,
                          nullptr) ||
                bytes_read == 0) {
                break;
            }
            total_read += bytes_read;
        }
        elf_bytes.resize(total_read);
        SetFilePointerEx(desc->handle, orig_pos, nullptr, FILE_BEGIN);
    }

    if (elf_bytes.size() < sizeof(Elf64_Ehdr)) {
        std::lock_guard lock(mutex_);
        last_error_ = "android_dlopen_ext: ELF data from fd too small";
        return nullptr;
    }

    std::string mod_name = (filename != nullptr && *filename != '\0')
                               ? std::string(filename)
                               : ("fd:" + std::to_string(fd));
    std::string mod_path = !desc->guest_path.empty() ? desc->guest_path : mod_name;

    std::shared_ptr<LinkMapEntry> entry;
    {
        std::lock_guard lock(mutex_);
        if (filename != nullptr && *filename != '\0') {
            if (auto existing = FindModule(filename)) {
                existing->ref_count++;
                last_error_.clear();
                return existing->handle;
            }
        }
        if (as_ == nullptr) {
            last_error_ = "android_dlopen_ext: address space not initialized";
            return nullptr;
        }

        entry = std::make_shared<LinkMapEntry>();
        std::unordered_set<std::string> loading;
        ElfLoader loader(as_);
        ElfLoader::DependencyLoader dep_loader = [&](const std::vector<std::string> &needed) {
            for (const auto &dep_name : needed) {
                auto dep_entry = LoadLibraryInternal(dep_name, flags, loading);
                if (dep_entry) {
                    entry->dependencies.push_back(dep_entry);
                }
            }
        };
        ElfLoader::SymbolResolver resolver =
            [this, &entry](std::string_view sym_name) -> uintptr_t {
            return ResolveSymbolForModule(*entry, sym_name);
        };

        LoadOptions options{};
        options.lazy_binding = ((flags & RTLD_NOW) == 0);
        if ((extinfo->flags & ANDROID_DLEXT_RESERVED_ADDRESS) != 0) {
            options.reserved_addr = extinfo->reserved_addr;
            options.reserved_size = extinfo->reserved_size;
        }

        std::string origin_dir = std::filesystem::path(mod_path).parent_path().string();
        auto res = loader.LoadFromMemory(elf_bytes, vfs_, resolver, dep_loader, origin_dir, options);
        if (std::holds_alternative<LoaderError>(res)) {
            last_error_ = "android_dlopen_ext failed: error loading from fd";
            return nullptr;
        }

        entry->elf = std::move(std::get<LoadedElf>(res));
        entry->elf.filepath = mod_path;
        entry->handle = reinterpret_cast<void *>(next_handle_++);
        entry->name = mod_name;
        entry->filepath = mod_path;
        entry->flags = flags;
        entry->ref_count = 1;

        modules_.push_back(entry);
        modules_by_handle_[entry->handle] = entry;
        modules_by_name_[entry->name] = entry;
        if (!entry->elf.soname.empty()) {
            modules_by_name_[entry->elf.soname] = entry;
        }
        dlpi_adds_++;
        RebuildRawLinkMapChain();
        last_error_.clear();
    }

    CallInit(entry);
    return entry->handle;
}

void *LinkMap::DlSym(void *handle, const char *symbol)
{
    return DlSymWithCaller(handle, symbol, 0);
}

void *LinkMap::DlSymWithCaller(void *handle, const char *symbol, uintptr_t caller_addr)
{
    if (symbol == nullptr) {
        std::lock_guard lock(mutex_);
        last_error_ = "dlsym: null symbol";
        return nullptr;
    }

    std::lock_guard lock(mutex_);

    if (handle == RTLD_NEXT) {
        bool start_searching = (caller_addr == 0);
        if (!start_searching) {
            auto caller_mod = FindModuleByAddress(caller_addr);
            if (!caller_mod) {
                start_searching = true;
            } else {
                for (const auto &m : modules_) {
                    if (m == caller_mod) {
                        start_searching = true;
                        continue;
                    }
                    if (!start_searching) {
                        continue;
                    }
                    auto it = m->elf.symbols.find(symbol);
                    if (it != m->elf.symbols.end()) {
                        last_error_.clear();
                        return reinterpret_cast<void *>(it->second);
                    }
                }
                uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(symbol);
                if (ndk_addr != 0) {
                    last_error_.clear();
                    return reinterpret_cast<void *>(ndk_addr);
                }
                last_error_ = "dlsym(RTLD_NEXT): symbol not found: " + std::string(symbol);
                return nullptr;
            }
        }
        for (size_t i = 1; i < modules_.size(); ++i) {
            auto it = modules_[i]->elf.symbols.find(symbol);
            if (it != modules_[i]->elf.symbols.end()) {
                last_error_.clear();
                return reinterpret_cast<void *>(it->second);
            }
        }
        uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(symbol);
        if (ndk_addr != 0) {
            last_error_.clear();
            return reinterpret_cast<void *>(ndk_addr);
        }
        last_error_ = "dlsym(RTLD_NEXT): symbol not found: " + std::string(symbol);
        return nullptr;
    }

    if (handle == RTLD_DEFAULT || handle == nullptr) {
        for (const auto &m : modules_) {
            auto it = m->elf.symbols.find(symbol);
            if (it != m->elf.symbols.end()) {
                last_error_.clear();
                return reinterpret_cast<void *>(it->second);
            }
        }
        uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(symbol);
        if (ndk_addr != 0) {
            last_error_.clear();
            return reinterpret_cast<void *>(ndk_addr);
        }
        last_error_ = "dlsym: symbol not found: " + std::string(symbol);
        return nullptr;
    }

    auto hit = modules_by_handle_.find(handle);
    if (hit == modules_by_handle_.end()) {
        last_error_ = "dlsym: invalid library handle";
        return nullptr;
    }

    const auto &entry = hit->second;
    auto sit = entry->elf.symbols.find(symbol);
    if (sit != entry->elf.symbols.end()) {
        last_error_.clear();
        return reinterpret_cast<void *>(sit->second);
    }

    for (const auto &dep : entry->dependencies) {
        auto dit = dep->elf.symbols.find(symbol);
        if (dit != dep->elf.symbols.end()) {
            last_error_.clear();
            return reinterpret_cast<void *>(dit->second);
        }
    }

    uintptr_t ndk_addr = ndk::NdkSymbolRegistry::Instance().Lookup(symbol);
    if (ndk_addr != 0) {
        last_error_.clear();
        return reinterpret_cast<void *>(ndk_addr);
    }

    last_error_ = "dlsym: symbol not found: " + std::string(symbol);
    return nullptr;
}

int LinkMap::DlClose(void *handle)
{
    if (handle == nullptr || handle == RTLD_DEFAULT || handle == RTLD_NEXT) {
        std::lock_guard lock(mutex_);
        last_error_ = "dlclose: invalid handle";
        return -1;
    }

    std::shared_ptr<LinkMapEntry> entry_to_fini;
    bool should_unload = false;
    {
        std::lock_guard lock(mutex_);
        auto it = modules_by_handle_.find(handle);
        if (it == modules_by_handle_.end()) {
            last_error_ = "dlclose: invalid handle";
            return -1;
        }

        auto entry = it->second;
        entry->ref_count--;
        if (entry->ref_count <= 0) {
            dlpi_subs_++;
            entry_to_fini = entry;
            should_unload = ((entry->flags & RTLD_NODELETE) == 0);
        }
        last_error_.clear();
    }

    if (entry_to_fini) {
        CallFini(entry_to_fini);
        if (should_unload) {
            std::lock_guard lock(mutex_);
            if (as_ != nullptr) {
                for (const auto &seg : entry_to_fini->elf.segments) {
                    if (seg.vaddr != 0 && seg.mem_size > 0) {
                        (void)as_->Munmap(reinterpret_cast<void *>(seg.vaddr), seg.mem_size);
                    }
                }
            }
            modules_by_handle_.erase(entry_to_fini->handle);
            for (auto it = modules_by_name_.begin(); it != modules_by_name_.end();) {
                if (it->second == entry_to_fini) {
                    it = modules_by_name_.erase(it);
                } else {
                    ++it;
                }
            }
            modules_.erase(std::remove(modules_.begin(), modules_.end(), entry_to_fini),
                           modules_.end());
            RebuildRawLinkMapChain();
        }
    }
    return 0;
}

char *LinkMap::DlError()
{
    std::lock_guard lock(mutex_);
    static thread_local std::string tls_error;
    if (last_error_.empty()) {
        return nullptr;
    }
    tls_error = std::move(last_error_);
    last_error_.clear();
    return tls_error.data();
}

int LinkMap::DlAddr(const void *addr, Dl_info *info)
{
    if (info == nullptr) {
        return 0;
    }

    std::lock_guard lock(mutex_);
    uintptr_t target = reinterpret_cast<uintptr_t>(addr);

    for (const auto &m : modules_) {
        bool in_module = false;
        for (const auto &seg : m->elf.segments) {
            if (target >= seg.vaddr && target < seg.vaddr + seg.mem_size) {
                in_module = true;
                break;
            }
        }
        if (!in_module) {
            continue;
        }

        info->dli_fname = m->filepath.empty() ? m->name.c_str() : m->filepath.c_str();
        info->dli_fbase = reinterpret_cast<void *>(m->elf.base_address);
        info->dli_sname = nullptr;
        info->dli_saddr = nullptr;

        uintptr_t best_diff = UINT64_MAX;
        for (const auto &[sname, saddr] : m->elf.symbols) {
            if (saddr <= target && (target - saddr) < best_diff) {
                best_diff = target - saddr;
                info->dli_sname = sname.c_str();
                info->dli_saddr = reinterpret_cast<void *>(saddr);
            }
        }
        return 1;
    }

    return 0;
}

int LinkMap::IteratePhdr(PhdrCallback callback, void *data)
{
    if (callback == nullptr) {
        return 0;
    }

    std::vector<std::shared_ptr<LinkMapEntry>> snapshot;
    uint64_t adds = 0;
    uint64_t subs = 0;
    {
        std::lock_guard lock(mutex_);
        snapshot = modules_;
        adds = dlpi_adds_;
        subs = dlpi_subs_;
    }

    int result = 0;
    for (const auto &m : snapshot) {
        dl_phdr_info info{};
        info.dlpi_addr = m->elf.load_bias != 0 ? m->elf.load_bias : m->elf.base_address;
        info.dlpi_name = m->filepath.empty() ? m->name.c_str() : m->filepath.c_str();
        if (m->elf.phdr_address != 0) {
            info.dlpi_phdr = reinterpret_cast<const Elf64_Phdr *>(m->elf.phdr_address);
            info.dlpi_phnum = m->elf.phnum;
        } else if (!m->elf.phdrs.empty()) {
            info.dlpi_phdr = m->elf.phdrs.data();
            info.dlpi_phnum = static_cast<uint16_t>(m->elf.phdrs.size());
        }
        info.dlpi_adds = adds;
        info.dlpi_subs = subs;
        info.dlpi_tls_modid = m->elf.tls_modid;
        info.dlpi_tls_data = TryGetLoadedTlsBlock(m->elf.tls_modid);

        result = callback(&info, sizeof(dl_phdr_info), data);
        if (result != 0) {
            break;
        }
    }

    return result;
}

void LinkMap::RebuildRawLinkMapChain()
{
    for (size_t i = 0; i < modules_.size(); ++i) {
        auto &m = modules_[i];
        m->raw_link_map.l_addr = m->elf.load_bias != 0 ? m->elf.load_bias : m->elf.base_address;
        m->raw_link_map.l_name =
            const_cast<char *>(m->filepath.empty() ? m->name.c_str() : m->filepath.c_str());
        m->raw_link_map.l_ld = reinterpret_cast<void *>(m->elf.dynamic_vaddr);
        m->raw_link_map.l_prev = (i > 0) ? &modules_[i - 1]->raw_link_map : nullptr;
        m->raw_link_map.l_next = (i + 1 < modules_.size()) ? &modules_[i + 1]->raw_link_map : nullptr;
    }
}

int LinkMap::DlInfo(void *handle, int request, void *info)
{
    if (info == nullptr) {
        std::lock_guard lock(mutex_);
        last_error_ = "dlinfo: null info pointer";
        return -1;
    }

    std::shared_ptr<LinkMapEntry> entry;
    {
        std::lock_guard lock(mutex_);
        if (handle == nullptr || handle == RTLD_DEFAULT) {
            if (modules_.empty()) {
                last_error_ = "dlinfo: no modules loaded";
                return -1;
            }
            entry = modules_.front();
        } else {
            auto it = modules_by_handle_.find(handle);
            if (it == modules_by_handle_.end()) {
                last_error_ = "dlinfo: invalid handle";
                return -1;
            }
            entry = it->second;
        }
        RebuildRawLinkMapChain();
    }

    switch (request) {
    case RTLD_DI_LMID:
        *reinterpret_cast<int64_t *>(info) = 0;
        return 0;
    case RTLD_DI_LINKMAP:
        *reinterpret_cast<link_map_raw **>(info) = &entry->raw_link_map;
        return 0;
    case RTLD_DI_ORIGIN: {
        std::string origin = std::filesystem::path(entry->filepath).parent_path().string();
        if (origin.empty()) {
            origin = ".";
        }
        std::memcpy(info, origin.c_str(), origin.size() + 1);
        return 0;
    }
    case RTLD_DI_TLS_MODID:
        *reinterpret_cast<size_t *>(info) = entry->elf.tls_modid;
        return 0;
    case RTLD_DI_TLS_DATA:
        *reinterpret_cast<void **>(info) =
            (entry->elf.tls_modid > 0) ? GetTlsAddress(entry->elf.tls_modid, 0) : nullptr;
        return 0;
    default:
        std::lock_guard lock(mutex_);
        last_error_ = "dlinfo: unsupported request";
        return -1;
    }
}

void *LinkMap::AllocateExecutableCode(size_t size)
{
    const size_t aligned = (size + 15) & ~size_t{15};
    if (current_pool_ == nullptr || (current_pool_used_ + aligned) > current_pool_cap_) {
        constexpr size_t POOL_SIZE = 65536;
        const size_t alloc_sz = std::max(POOL_SIZE, aligned);
        void *pool = VirtualAlloc(nullptr, alloc_sz, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
        if (pool == nullptr) {
            return nullptr;
        }
        exec_pools_.emplace_back(pool, alloc_sz);
        current_pool_ = static_cast<uint8_t *>(pool);
        current_pool_used_ = 0;
        current_pool_cap_ = alloc_sz;
    }

    void *ptr = current_pool_ + current_pool_used_;
    current_pool_used_ += aligned;
    return ptr;
}

uintptr_t LinkMap::GetTlsdescReturnThunk()
{
    std::lock_guard lock(mutex_);
    if (tlsdesc_return_thunk_ != 0) {
        return tlsdesc_return_thunk_;
    }

    static constexpr uint8_t code[] = {
        0x48, 0x8b, 0x40, 0x08, // mov rax, [rax + 8]
        0xc3                    // ret
    };
    void *mem = AllocateExecutableCode(sizeof(code));
    if (mem == nullptr) {
        return 0;
    }
    std::memcpy(mem, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), mem, sizeof(code));
    tlsdesc_return_thunk_ = reinterpret_cast<uintptr_t>(mem);
    return tlsdesc_return_thunk_;
}

uintptr_t LinkMap::GetTlsdescDynamicThunk()
{
    std::lock_guard lock(mutex_);
    if (tlsdesc_dynamic_thunk_ != 0) {
        return tlsdesc_dynamic_thunk_;
    }

    std::vector<uint8_t> code;
    auto emit_u8 = [&](uint8_t b) { code.push_back(b); };
    auto emit_u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            code.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
        }
    };

    // Preserve caller-saved integer registers (excluding rax) and rflags (9 pushes -> 16B aligned)
    emit_u8(0x51);                     // push rcx
    emit_u8(0x52);                     // push rdx
    emit_u8(0x56);                     // push rsi
    emit_u8(0x57);                     // push rdi
    emit_u8(0x41); emit_u8(0x50);      // push r8
    emit_u8(0x41); emit_u8(0x51);      // push r9
    emit_u8(0x41); emit_u8(0x52);      // push r10
    emit_u8(0x41); emit_u8(0x53);      // push r11
    emit_u8(0x9c);                     // pushfq

    // Preserve xmm0..xmm7
    emit_u8(0x48); emit_u8(0x81); emit_u8(0xec);
    emit_u8(0x80); emit_u8(0x00); emit_u8(0x00); emit_u8(0x00); // sub rsp, 128
    for (uint8_t reg = 0; reg < 8; ++reg) {
        emit_u8(0xf3);
        emit_u8(0x0f);
        emit_u8(0x7f);
        emit_u8(static_cast<uint8_t>(0x44 | (reg << 3)));
        emit_u8(0x24);
        emit_u8(static_cast<uint8_t>(reg * 16));
    }

    // rdi = desc->arg ([rax + 8])
    emit_u8(0x48); emit_u8(0x8b); emit_u8(0x78); emit_u8(0x08);
    // mov rax, PortageTlsdescDynamicResolve
    emit_u8(0x48); emit_u8(0xb8);
    emit_u64(reinterpret_cast<uintptr_t>(&PortageTlsdescDynamicResolve));
    // call rax
    emit_u8(0xff); emit_u8(0xd0);

    // Restore xmm0..xmm7
    for (uint8_t reg = 0; reg < 8; ++reg) {
        emit_u8(0xf3);
        emit_u8(0x0f);
        emit_u8(0x6f);
        emit_u8(static_cast<uint8_t>(0x44 | (reg << 3)));
        emit_u8(0x24);
        emit_u8(static_cast<uint8_t>(reg * 16));
    }
    emit_u8(0x48); emit_u8(0x81); emit_u8(0xc4);
    emit_u8(0x80); emit_u8(0x00); emit_u8(0x00); emit_u8(0x00); // add rsp, 128

    // Restore rflags and registers
    emit_u8(0x9d);                     // popfq
    emit_u8(0x41); emit_u8(0x5b);      // pop r11
    emit_u8(0x41); emit_u8(0x5a);      // pop r10
    emit_u8(0x41); emit_u8(0x59);      // pop r9
    emit_u8(0x41); emit_u8(0x58);      // pop r8
    emit_u8(0x5f);                     // pop rdi
    emit_u8(0x5e);                     // pop rsi
    emit_u8(0x5a);                     // pop rdx
    emit_u8(0x59);                     // pop rcx
    emit_u8(0xc3);                     // ret

    void *mem = AllocateExecutableCode(code.size());
    if (mem == nullptr) {
        return 0;
    }
    std::memcpy(mem, code.data(), code.size());
    FlushInstructionCache(GetCurrentProcess(), mem, code.size());
    tlsdesc_dynamic_thunk_ = reinterpret_cast<uintptr_t>(mem);
    return tlsdesc_dynamic_thunk_;
}

uintptr_t LinkMap::AllocateLazyPltTrampoline(uintptr_t got_slot, const char *sym_name)
{
    std::lock_guard lock(mutex_);

    std::vector<uint8_t> code;
    auto emit_u8 = [&](uint8_t b) { code.push_back(b); };
    auto emit_u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            code.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
        }
    };

    // Preserve rax, rcx, rdx, rsi, rdi, r8, r9, r10, rflags (9 pushes -> 16B aligned)
    emit_u8(0x50);                     // push rax
    emit_u8(0x51);                     // push rcx
    emit_u8(0x52);                     // push rdx
    emit_u8(0x56);                     // push rsi
    emit_u8(0x57);                     // push rdi
    emit_u8(0x41); emit_u8(0x50);      // push r8
    emit_u8(0x41); emit_u8(0x51);      // push r9
    emit_u8(0x41); emit_u8(0x52);      // push r10
    emit_u8(0x9c);                     // pushfq

    // Preserve xmm0..xmm7
    emit_u8(0x48); emit_u8(0x81); emit_u8(0xec);
    emit_u8(0x80); emit_u8(0x00); emit_u8(0x00); emit_u8(0x00); // sub rsp, 128
    for (uint8_t reg = 0; reg < 8; ++reg) {
        emit_u8(0xf3);
        emit_u8(0x0f);
        emit_u8(0x7f);
        emit_u8(static_cast<uint8_t>(0x44 | (reg << 3)));
        emit_u8(0x24);
        emit_u8(static_cast<uint8_t>(reg * 16));
    }

    // SysV call: PortageDlRuntimeResolve(got_slot, sym_name)
    emit_u8(0x48); emit_u8(0xbf); // mov rdi, got_slot
    emit_u64(got_slot);
    emit_u8(0x48); emit_u8(0xbe); // mov rsi, sym_name
    emit_u64(reinterpret_cast<uintptr_t>(sym_name));
    emit_u8(0x48); emit_u8(0xb8); // mov rax, PortageDlRuntimeResolve
    emit_u64(reinterpret_cast<uintptr_t>(&PortageDlRuntimeResolve));
    emit_u8(0xff); emit_u8(0xd0); // call rax

    // Stash resolved address in r11 (scratch in SysV ABI)
    emit_u8(0x49); emit_u8(0x89); emit_u8(0xc3); // mov r11, rax

    // Restore xmm0..xmm7
    for (uint8_t reg = 0; reg < 8; ++reg) {
        emit_u8(0xf3);
        emit_u8(0x0f);
        emit_u8(0x6f);
        emit_u8(static_cast<uint8_t>(0x44 | (reg << 3)));
        emit_u8(0x24);
        emit_u8(static_cast<uint8_t>(reg * 16));
    }
    emit_u8(0x48); emit_u8(0x81); emit_u8(0xc4);
    emit_u8(0x80); emit_u8(0x00); emit_u8(0x00); emit_u8(0x00); // add rsp, 128

    // Restore flags and integer registers
    emit_u8(0x9d);                     // popfq
    emit_u8(0x41); emit_u8(0x5a);      // pop r10
    emit_u8(0x41); emit_u8(0x59);      // pop r9
    emit_u8(0x41); emit_u8(0x58);      // pop r8
    emit_u8(0x5f);                     // pop rdi
    emit_u8(0x5e);                     // pop rsi
    emit_u8(0x5a);                     // pop rdx
    emit_u8(0x59);                     // pop rcx
    emit_u8(0x58);                     // pop rax

    // Jump to resolved target in r11
    emit_u8(0x41); emit_u8(0xff); emit_u8(0xe3); // jmp r11

    void *mem = AllocateExecutableCode(code.size());
    if (mem == nullptr) {
        return 0;
    }
    std::memcpy(mem, code.data(), code.size());
    FlushInstructionCache(GetCurrentProcess(), mem, code.size());
    return reinterpret_cast<uintptr_t>(mem);
}

uintptr_t LinkMap::ResolveLazySymbol(uintptr_t got_slot, const char *sym_name)
{
    lazy_resolve_count_.fetch_add(1, std::memory_order_relaxed);
    if (sym_name == nullptr) {
        return 0;
    }

    uintptr_t target = ndk::NdkSymbolRegistry::Instance().Lookup(sym_name);
    if (target == 0) {
        std::lock_guard lock(mutex_);
        std::shared_ptr<LinkMapEntry> owner = FindModuleByAddress(got_slot);
        if (owner) {
            target = owner->elf.FindSymbol(sym_name);
            if (target == 0) {
                for (const auto &dep : owner->dependencies) {
                    auto dit = dep->elf.symbols.find(sym_name);
                    if (dit != dep->elf.symbols.end()) {
                        target = dit->second;
                        break;
                    }
                }
            }
        }
        if (target == 0) {
            target = ResolveGlobalSymbol(sym_name);
        }
    }

    if (target != 0 && got_slot != 0) {
        *reinterpret_cast<uintptr_t *>(got_slot) = target;
        std::atomic_thread_fence(std::memory_order_release);
    }
    return target;
}

} // namespace portage::elf

extern "C" {

__attribute__((sysv_abi)) void *dlopen(const char *filename, int flags)
{
    return portage::elf::LinkMap::Instance().DlOpen(filename, flags);
}

__attribute__((sysv_abi)) void *android_dlopen_ext(const char *filename, int flags,
                                                   const portage::elf::android_dlextinfo *extinfo)
{
    return portage::elf::LinkMap::Instance().DlOpenExt(filename, flags, extinfo);
}

__attribute__((sysv_abi)) void *dlsym(void *handle, const char *symbol)
{
    uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    return portage::elf::LinkMap::Instance().DlSymWithCaller(handle, symbol, caller);
}

__attribute__((sysv_abi)) int dlclose(void *handle)
{
    return portage::elf::LinkMap::Instance().DlClose(handle);
}

__attribute__((sysv_abi)) char *dlerror() { return portage::elf::LinkMap::Instance().DlError(); }

__attribute__((sysv_abi)) int dladdr(const void *addr, portage::elf::Dl_info *info)
{
    return portage::elf::LinkMap::Instance().DlAddr(addr, info);
}

__attribute__((sysv_abi)) int dlinfo(void *handle, int request, void *info)
{
    return portage::elf::LinkMap::Instance().DlInfo(handle, request, info);
}

__attribute__((sysv_abi)) int dl_iterate_phdr(portage::elf::LinkMap::PhdrCallback callback,
                                              void *data)
{
    return portage::elf::LinkMap::Instance().IteratePhdr(callback, data);
}

__attribute__((sysv_abi)) void *__tls_get_addr(const portage::elf::TlsIndex *ti)
{
    if (ti == nullptr) {
        return nullptr;
    }
    return portage::elf::LinkMap::Instance().GetTlsAddress(ti->ti_module, ti->ti_offset);
}

__attribute__((sysv_abi)) int portage_cxa_atexit(portage::elf::LinkMap::CxaAtExitFn func, void *arg,
                                                 void *dso_handle) noexcept
{
    return portage::elf::LinkMap::Instance().RegisterCxaAtExit(func, arg, dso_handle);
}

__attribute__((sysv_abi)) void portage_cxa_finalize(void *dso_handle) noexcept
{
    portage::elf::LinkMap::Instance().FinalizeCxaAtExit(dso_handle);
}

__attribute__((sysv_abi)) int portage_cxa_thread_atexit_impl(portage::elf::LinkMap::CxaAtExitFn dtor,
                                                             void *obj, void *dso_handle) noexcept
{
    return portage::thread::ThreadManager::Instance().RegisterThreadAtExit(dtor, obj, dso_handle);
}

__attribute__((sysv_abi)) int portage_guest_atexit(__attribute__((sysv_abi)) void (*func)()) noexcept
{
    if (func == nullptr) {
        return -1;
    }
    return portage::elf::LinkMap::Instance().RegisterCxaAtExit(
        portage::elf::AtExitThunk, reinterpret_cast<void *>(func), nullptr);
}

} // extern "C"
