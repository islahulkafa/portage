#pragma once

#include "fs/vfs.hpp"
#include "process/namespace_types.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::process {

class PidNamespace : public std::enable_shared_from_this<PidNamespace> {
  public:
    explicit PidNamespace(std::shared_ptr<PidNamespace> parent = nullptr);

    int AllocatePid(int global_pid) noexcept;
    [[nodiscard]] int GetLocalPid(int global_pid) const noexcept;
    [[nodiscard]] int GetGlobalPid(int local_pid) const noexcept;
    void RemovePid(int global_pid) noexcept;

    [[nodiscard]] std::shared_ptr<PidNamespace> GetParent() const noexcept { return m_parent; }
    [[nodiscard]] uint64_t GetId() const noexcept { return m_id; }

  private:
    uint64_t m_id{0};
    std::shared_ptr<PidNamespace> m_parent{nullptr};
    mutable std::mutex m_mutex{};
    int m_next_local_pid{1};
    std::unordered_map<int, int> m_global_to_local{};
    std::unordered_map<int, int> m_local_to_global{};
};

class MountNamespace {
  public:
    MountNamespace();
    explicit MountNamespace(std::vector<fs::Vfs::MountPoint> mounts);

    bool AddMount(std::string_view guest_path, const std::filesystem::path &host_path);
    bool RemoveMount(std::string_view guest_path);
    [[nodiscard]] std::optional<std::filesystem::path>
    GetMountTarget(std::string_view guest_path) const;
    [[nodiscard]] std::vector<fs::Vfs::MountPoint> GetMounts() const;

    [[nodiscard]] uint64_t GetId() const noexcept { return m_id; }
    void SetRoot(bool is_root) noexcept { m_is_root = is_root; }
    [[nodiscard]] bool IsRoot() const noexcept { return m_is_root; }

  private:
    uint64_t m_id{0};
    bool m_is_root{false};
    mutable std::mutex m_mutex{};
    std::vector<fs::Vfs::MountPoint> m_mounts{};
};

class UtsNamespace {
  public:
    UtsNamespace();
    explicit UtsNamespace(const syscall::LinuxUtsname &uts);

    [[nodiscard]] syscall::LinuxUtsname GetUtsname() const;
    void SetHostname(std::string_view name);
    void SetDomainname(std::string_view name);

    [[nodiscard]] uint64_t GetId() const noexcept { return m_id; }
    void SetRoot(bool is_root) noexcept { m_is_root = is_root; }
    [[nodiscard]] bool IsRoot() const noexcept { return m_is_root; }

  private:
    uint64_t m_id{0};
    bool m_is_root{false};
    mutable std::mutex m_mutex{};
    syscall::LinuxUtsname m_uts{};
};

class NamespaceManager {
  public:
    static NamespaceManager &Instance() noexcept;

    void Initialize(fs::Vfs *root_vfs = nullptr);
    void Reset();

    [[nodiscard]] std::shared_ptr<PidNamespace> GetRootPidNamespace() noexcept;
    [[nodiscard]] std::shared_ptr<MountNamespace> GetRootMountNamespace() noexcept;
    [[nodiscard]] std::shared_ptr<UtsNamespace> GetRootUtsNamespace() noexcept;

    std::shared_ptr<PidNamespace> CreateChildPidNamespace(std::shared_ptr<PidNamespace> parent);
    std::shared_ptr<MountNamespace> CloneMountNamespace(const MountNamespace &source);
    std::shared_ptr<UtsNamespace> CloneUtsNamespace(const UtsNamespace &source);

    [[nodiscard]] std::shared_ptr<PidNamespace> FindPidNamespaceById(uint64_t id) noexcept;
    [[nodiscard]] std::shared_ptr<MountNamespace> FindMountNamespaceById(uint64_t id) noexcept;
    [[nodiscard]] std::shared_ptr<UtsNamespace> FindUtsNamespaceById(uint64_t id) noexcept;

    int Unshare(uint64_t flags) noexcept;
    int SetNsById(uint64_t ns_id, int ns_type) noexcept;

  private:
    NamespaceManager() = default;
    void EnsureInitializedLocked();

    std::mutex m_mutex{};
    bool m_initialized{false};
    fs::Vfs *m_root_vfs{nullptr};
    std::shared_ptr<PidNamespace> m_root_pid_ns{};
    std::shared_ptr<MountNamespace> m_root_mnt_ns{};
    std::shared_ptr<UtsNamespace> m_root_uts_ns{};

    std::unordered_map<uint64_t, std::weak_ptr<PidNamespace>> m_pid_ns_registry{};
    std::unordered_map<uint64_t, std::weak_ptr<MountNamespace>> m_mnt_ns_registry{};
    std::unordered_map<uint64_t, std::weak_ptr<UtsNamespace>> m_uts_ns_registry{};
};

} // namespace portage::process
