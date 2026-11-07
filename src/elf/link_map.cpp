#include "elf/link_map.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace portage::elf {

LinkMap &LinkMap::Instance() noexcept
{
    static LinkMap instance;
    return instance;
}

void LinkMap::Reset()
{
    std::lock_guard lock(mutex_);
    modules_.clear();
    modules_by_handle_.clear();
    modules_by_name_.clear();
    search_paths_.clear();
    last_error_.clear();
    next_handle_ = 1;
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

std::string LinkMap::ResolveLibraryPath(std::string_view libname) const
{
    std::string name(libname);

    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        if (vfs_ != nullptr) {
            auto resolved = vfs_->Resolve(name);
            if (std::filesystem::exists(resolved.host_path)) {
                return resolved.host_path.string();
            }
        }
        if (std::filesystem::exists(name)) {
            return name;
        }
    }

    for (const auto &dir : search_paths_) {
        std::string candidate = dir + "/" + name;
        if (vfs_ != nullptr) {
            auto resolved = vfs_->Resolve(candidate);
            if (std::filesystem::exists(resolved.host_path)) {
                return resolved.host_path.string();
            }
        }
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    if (std::filesystem::exists(name)) {
        return name;
    }

    return {};
}

std::shared_ptr<LinkMapEntry> LinkMap::FindModule(std::string_view name) const
{
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
    for (const auto &m : modules_) {
        auto it = m->elf.symbols.find(std::string(name));
        if (it != m->elf.symbols.end()) {
            return it->second;
        }
    }
    return 0;
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
        uintptr_t addr = entry->elf.FindSymbol(sym_name);
        if (addr != 0) {
            return addr;
        }
        for (const auto &dep : entry->dependencies) {
            auto it = dep->elf.symbols.find(std::string(sym_name));
            if (it != dep->elf.symbols.end()) {
                return it->second;
            }
        }
        addr = this->ResolveGlobalSymbol(sym_name);
        if (addr != 0) {
            return addr;
        }
        return ndk::NdkSymbolRegistry::Instance().Lookup(sym_name);
    };

    auto res = loader.LoadFromFile(path, vfs_, resolver, dep_loader);
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

    loading.erase(name);
    loading.erase(fn);

    return entry;
}

void LinkMap::CallInit(const std::shared_ptr<LinkMapEntry> &entry)
{
    if (!entry || entry->init_called) {
        return;
    }

    for (const auto &dep : entry->dependencies) {
        CallInit(dep);
    }

    entry->init_called = true;
    using SysVInitFn = __attribute__((sysv_abi)) void (*)();
    for (uintptr_t fn_addr : entry->elf.init_functions) {
        if (fn_addr != 0) {
            auto fn = reinterpret_cast<SysVInitFn>(fn_addr);
            fn();
        }
    }
}

void LinkMap::CallFini(const std::shared_ptr<LinkMapEntry> &entry)
{
    if (!entry || entry->fini_called) {
        return;
    }

    entry->fini_called = true;
    using SysVFiniFn = __attribute__((sysv_abi)) void (*)();
    for (auto it = entry->elf.fini_functions.rbegin(); it != entry->elf.fini_functions.rend();
         ++it) {
        if (*it != 0) {
            auto fn = reinterpret_cast<SysVFiniFn>(*it);
            fn();
        }
    }

    for (auto it = entry->dependencies.rbegin(); it != entry->dependencies.rend(); ++it) {
        CallFini(*it);
    }
}

std::shared_ptr<LinkMapEntry> LinkMap::RegisterLoadedElf(LoadedElf elf, std::string_view name)
{
    std::lock_guard lock(mutex_);
    auto entry = std::make_shared<LinkMapEntry>();
    entry->handle = reinterpret_cast<void *>(next_handle_++);
    entry->name = std::string(name);
    entry->filepath = elf.filepath.empty() ? std::string(name) : elf.filepath;
    entry->elf = std::move(elf);
    entry->ref_count = 1;

    modules_.push_back(entry);
    modules_by_handle_[entry->handle] = entry;
    modules_by_name_[entry->name] = entry;
    std::string fn = std::filesystem::path(entry->filepath).filename().string();
    modules_by_name_[fn] = entry;
    if (!entry->elf.soname.empty()) {
        modules_by_name_[entry->elf.soname] = entry;
    }

    CallInit(entry);
    return entry;
}

void *LinkMap::DlOpen(const char *filename, int flags)
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
    auto entry = LoadLibraryInternal(filename, flags, loading);
    if (!entry) {
        return nullptr;
    }

    CallInit(entry);
    last_error_.clear();
    return entry->handle;
}

void *LinkMap::DlSym(void *handle, const char *symbol)
{
    if (symbol == nullptr) {
        std::lock_guard lock(mutex_);
        last_error_ = "dlsym: null symbol";
        return nullptr;
    }

    std::lock_guard lock(mutex_);

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
    if (handle == nullptr || handle == RTLD_DEFAULT) {
        std::lock_guard lock(mutex_);
        last_error_ = "dlclose: invalid handle";
        return -1;
    }

    std::lock_guard lock(mutex_);
    auto it = modules_by_handle_.find(handle);
    if (it == modules_by_handle_.end()) {
        last_error_ = "dlclose: invalid handle";
        return -1;
    }

    auto entry = it->second;
    entry->ref_count--;
    if (entry->ref_count <= 0) {
        CallFini(entry);
    }
    last_error_.clear();
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

} // namespace portage::elf

extern "C" {

__attribute__((sysv_abi)) void *dlopen(const char *filename, int flags)
{
    return portage::elf::LinkMap::Instance().DlOpen(filename, flags);
}

__attribute__((sysv_abi)) void *dlsym(void *handle, const char *symbol)
{
    return portage::elf::LinkMap::Instance().DlSym(handle, symbol);
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

} // extern "C"
