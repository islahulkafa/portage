#include "process/namespace_manager.hpp"
#include "thread/thread_manager.hpp"

#include <cstring>

namespace portage::process {

namespace {

std::atomic<uint64_t> g_next_ns_id{4026531836ULL};

uint64_t AllocateNamespaceId() noexcept
{
    return g_next_ns_id.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

PidNamespace::PidNamespace(std::shared_ptr<PidNamespace> parent)
    : m_id(AllocateNamespaceId()), m_parent(std::move(parent))
{
}

int PidNamespace::AllocatePid(int global_pid) noexcept
{
    int local = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_global_to_local.find(global_pid);
        if (it != m_global_to_local.end()) {
            return it->second;
        }
        local = m_next_local_pid++;
        m_global_to_local[global_pid] = local;
        m_local_to_global[local] = global_pid;
    }

    if (m_parent) {
        m_parent->AllocatePid(global_pid);
    }
    return local;
}

int PidNamespace::GetLocalPid(int global_pid) const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_global_to_local.find(global_pid);
    if (it != m_global_to_local.end()) {
        return it->second;
    }
    return 0;
}

int PidNamespace::GetGlobalPid(int local_pid) const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_local_to_global.find(local_pid);
    if (it != m_local_to_global.end()) {
        return it->second;
    }
    return 0;
}

void PidNamespace::RemovePid(int global_pid) noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_global_to_local.find(global_pid);
        if (it != m_global_to_local.end()) {
            m_local_to_global.erase(it->second);
            m_global_to_local.erase(it);
        }
    }
    if (m_parent) {
        m_parent->RemovePid(global_pid);
    }
}

MountNamespace::MountNamespace() : m_id(AllocateNamespaceId()) {}

MountNamespace::MountNamespace(std::vector<fs::Vfs::MountPoint> mounts)
    : m_id(AllocateNamespaceId()), m_mounts(std::move(mounts))
{
}

bool MountNamespace::AddMount(std::string_view guest_path, const std::filesystem::path &host_path)
{
    if (guest_path.empty() || host_path.empty()) {
        return false;
    }
    std::string norm = fs::Vfs::NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &m : m_mounts) {
        if (m.guest_path == norm) {
            m.host_path = host_path;
            return true;
        }
    }
    m_mounts.push_back({norm, host_path});
    return true;
}

bool MountNamespace::RemoveMount(std::string_view guest_path)
{
    if (guest_path.empty()) {
        return false;
    }
    std::string norm = fs::Vfs::NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_mounts.begin(); it != m_mounts.end(); ++it) {
        if (it->guest_path == norm) {
            m_mounts.erase(it);
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path>
MountNamespace::GetMountTarget(std::string_view guest_path) const
{
    std::string norm = fs::Vfs::NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &m : m_mounts) {
        if (m.guest_path == norm) {
            return m.host_path;
        }
    }
    return std::nullopt;
}

std::vector<fs::Vfs::MountPoint> MountNamespace::GetMounts() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mounts;
}

UtsNamespace::UtsNamespace() : m_id(AllocateNamespaceId())
{
    std::memset(&m_uts, 0, sizeof(m_uts));
    std::strncpy(m_uts.sysname, "Linux", sizeof(m_uts.sysname) - 1);
    std::strncpy(m_uts.nodename, "portage-win", sizeof(m_uts.nodename) - 1);
    std::strncpy(m_uts.release, "6.1.0-android14-x86_64", sizeof(m_uts.release) - 1);
    std::strncpy(m_uts.version, "#1 SMP PREEMPT Portage", sizeof(m_uts.version) - 1);
    std::strncpy(m_uts.machine, "x86_64", sizeof(m_uts.machine) - 1);
    std::strncpy(m_uts.domainname, "(none)", sizeof(m_uts.domainname) - 1);
}

UtsNamespace::UtsNamespace(const syscall::LinuxUtsname &uts)
    : m_id(AllocateNamespaceId()), m_uts(uts)
{
}

syscall::LinuxUtsname UtsNamespace::GetUtsname() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_uts;
}

void UtsNamespace::SetHostname(std::string_view name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::memset(m_uts.nodename, 0, sizeof(m_uts.nodename));
    size_t copy_len = std::min(name.size(), sizeof(m_uts.nodename) - 1);
    std::memcpy(m_uts.nodename, name.data(), copy_len);
}

void UtsNamespace::SetDomainname(std::string_view name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::memset(m_uts.domainname, 0, sizeof(m_uts.domainname));
    size_t copy_len = std::min(name.size(), sizeof(m_uts.domainname) - 1);
    std::memcpy(m_uts.domainname, name.data(), copy_len);
}

NamespaceManager &NamespaceManager::Instance() noexcept
{
    static NamespaceManager instance;
    return instance;
}

void NamespaceManager::EnsureInitializedLocked()
{
    if (!m_initialized) {
        m_root_pid_ns = std::make_shared<PidNamespace>(nullptr);
        std::vector<fs::Vfs::MountPoint> initial_mounts;
        if (m_root_vfs) {
            initial_mounts = m_root_vfs->GetMounts();
        }
        m_root_mnt_ns = std::make_shared<MountNamespace>(std::move(initial_mounts));
        m_root_mnt_ns->SetRoot(true);
        m_root_uts_ns = std::make_shared<UtsNamespace>();

        m_pid_ns_registry[m_root_pid_ns->GetId()] = m_root_pid_ns;
        m_mnt_ns_registry[m_root_mnt_ns->GetId()] = m_root_mnt_ns;
        m_uts_ns_registry[m_root_uts_ns->GetId()] = m_root_uts_ns;
        m_initialized = true;
    }
}

void NamespaceManager::Initialize(fs::Vfs *root_vfs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root_vfs = root_vfs;
    m_initialized = false;
    EnsureInitializedLocked();
}

void NamespaceManager::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = false;
    m_pid_ns_registry.clear();
    m_mnt_ns_registry.clear();
    m_uts_ns_registry.clear();
    EnsureInitializedLocked();
}

std::shared_ptr<PidNamespace> NamespaceManager::GetRootPidNamespace() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureInitializedLocked();
    return m_root_pid_ns;
}

std::shared_ptr<MountNamespace> NamespaceManager::GetRootMountNamespace() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureInitializedLocked();
    return m_root_mnt_ns;
}

std::shared_ptr<UtsNamespace> NamespaceManager::GetRootUtsNamespace() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureInitializedLocked();
    return m_root_uts_ns;
}

std::shared_ptr<PidNamespace>
NamespaceManager::CreateChildPidNamespace(std::shared_ptr<PidNamespace> parent)
{
    auto ns = std::make_shared<PidNamespace>(std::move(parent));
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pid_ns_registry[ns->GetId()] = ns;
    return ns;
}

std::shared_ptr<MountNamespace> NamespaceManager::CloneMountNamespace(const MountNamespace &source)
{
    auto ns = std::make_shared<MountNamespace>(source.GetMounts());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mnt_ns_registry[ns->GetId()] = ns;
    return ns;
}

std::shared_ptr<UtsNamespace> NamespaceManager::CloneUtsNamespace(const UtsNamespace &source)
{
    auto ns = std::make_shared<UtsNamespace>(source.GetUtsname());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uts_ns_registry[ns->GetId()] = ns;
    return ns;
}

std::shared_ptr<PidNamespace> NamespaceManager::FindPidNamespaceById(uint64_t id) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pid_ns_registry.find(id);
    if (it != m_pid_ns_registry.end()) {
        return it->second.lock();
    }
    return nullptr;
}

std::shared_ptr<MountNamespace> NamespaceManager::FindMountNamespaceById(uint64_t id) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_mnt_ns_registry.find(id);
    if (it != m_mnt_ns_registry.end()) {
        return it->second.lock();
    }
    return nullptr;
}

std::shared_ptr<UtsNamespace> NamespaceManager::FindUtsNamespaceById(uint64_t id) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_uts_ns_registry.find(id);
    if (it != m_uts_ns_registry.end()) {
        return it->second.lock();
    }
    return nullptr;
}

int NamespaceManager::Unshare(uint64_t flags) noexcept
{
    constexpr uint64_t supported_flags =
        LINUX_CLONE_NEWNS | LINUX_CLONE_NEWUTS | LINUX_CLONE_NEWPID | LINUX_CLONE_NEWIPC |
        LINUX_CLONE_NEWUSER | LINUX_CLONE_NEWNET | LINUX_CLONE_NEWCGROUP;
    if ((flags & ~supported_flags) != 0) {
        return static_cast<int>(syscall::LINUX_EINVAL);
    }

    auto thread = thread::ThreadManager::Instance().GetCurrentGuestThread();
    if (!thread) {
        thread::ThreadManager::Instance().RegisterMainThread();
        thread = thread::ThreadManager::Instance().GetCurrentGuestThread();
    }
    if (!thread) {
        return static_cast<int>(syscall::LINUX_EINVAL);
    }

    if (flags & LINUX_CLONE_NEWPID) {
        auto parent_ns = thread->pid_ns ? thread->pid_ns : GetRootPidNamespace();
        thread->next_child_pid_ns = CreateChildPidNamespace(parent_ns);
    }

    if (flags & LINUX_CLONE_NEWNS) {
        std::shared_ptr<MountNamespace> src =
            thread->mnt_ns ? thread->mnt_ns : GetRootMountNamespace();
        if (src == GetRootMountNamespace() && m_root_vfs) {
            thread->mnt_ns = std::make_shared<MountNamespace>(m_root_vfs->GetMounts());
            std::lock_guard<std::mutex> lock(m_mutex);
            m_mnt_ns_registry[thread->mnt_ns->GetId()] = thread->mnt_ns;
        } else {
            thread->mnt_ns = CloneMountNamespace(*src);
        }
    }

    if (flags & LINUX_CLONE_NEWUTS) {
        auto src_uts = thread->uts_ns ? thread->uts_ns : GetRootUtsNamespace();
        thread->uts_ns = CloneUtsNamespace(*src_uts);
    }

    return 0;
}

int NamespaceManager::SetNsById(uint64_t ns_id, int ns_type) noexcept
{
    auto thread = thread::ThreadManager::Instance().GetCurrentGuestThread();
    if (!thread) {
        return static_cast<int>(syscall::LINUX_EINVAL);
    }

    if (ns_type == 0 || ns_type == static_cast<int>(LINUX_CLONE_NEWPID)) {
        if (auto pid_ns = FindPidNamespaceById(ns_id)) {
            thread->next_child_pid_ns = pid_ns;
            return 0;
        }
        if (ns_type != 0) {
            return static_cast<int>(syscall::LINUX_EINVAL);
        }
    }

    if (ns_type == 0 || ns_type == static_cast<int>(LINUX_CLONE_NEWNS)) {
        if (auto mnt_ns = FindMountNamespaceById(ns_id)) {
            thread->mnt_ns = mnt_ns;
            return 0;
        }
        if (ns_type != 0) {
            return static_cast<int>(syscall::LINUX_EINVAL);
        }
    }

    if (ns_type == 0 || ns_type == static_cast<int>(LINUX_CLONE_NEWUTS)) {
        if (auto uts_ns = FindUtsNamespaceById(ns_id)) {
            thread->uts_ns = uts_ns;
            return 0;
        }
        if (ns_type != 0) {
            return static_cast<int>(syscall::LINUX_EINVAL);
        }
    }

    return static_cast<int>(syscall::LINUX_EINVAL);
}

} // namespace portage::process
