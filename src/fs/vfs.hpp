#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace portage::fs {

enum class DeviceType {
    None,
    Regular,
    Directory,
    Pipe,
    DevNull,
    DevZero,
    DevUrandom,
    SyntheticProc,
    Socket,
    Epoll,
    Binder,
    HwBinder,
    Ashmem,
    SharedMemory,
    VulkanDevice,
    AlsaPcm,
    AlsaControl,
    InputEvent0,
    InputEvent1,
    InputDir,
    Inotify,
    PropAreaDir,
    PropSerial,
    PropInfo,
    PropContext,
    PropLegacy,
    PropSocketDir,
    PropServiceSocket,
    EventFd,
    TimerFd,
    SyncFence
};

struct ResolvedPath {
    DeviceType dev_type{DeviceType::None};
    std::filesystem::path host_path{};
};

class Vfs {
  public:
    explicit Vfs(std::filesystem::path rootfs = "");

    void SetRootfs(std::filesystem::path rootfs);
    [[nodiscard]] const std::filesystem::path &GetRootfs() const noexcept;

    void SetExecutablePath(std::filesystem::path exe_path);
    [[nodiscard]] const std::filesystem::path &GetExecutablePath() const noexcept;

    [[nodiscard]] const std::string &GetCwd() const noexcept;
    int ChDir(std::string_view path);

    struct MountPoint {
        std::string guest_path;
        std::filesystem::path host_path;
    };

    bool AddMount(std::string_view guest_path, const std::filesystem::path &host_path);
    bool RemoveMount(std::string_view guest_path);
    [[nodiscard]] std::optional<std::filesystem::path>
    GetMountTarget(std::string_view guest_path) const;
    void ClearMounts();

    [[nodiscard]] ResolvedPath Resolve(std::string_view guest_path) const;
    [[nodiscard]] ResolvedPath ResolveAt(int dirfd, std::string_view guest_path,
                                         std::string_view dirfd_path = "") const;

    [[nodiscard]] static std::string NormalizePath(std::string_view path);

  private:
    std::filesystem::path m_rootfs{};
    std::filesystem::path m_executable_path{};
    std::string m_cwd{"/"};
    mutable std::mutex m_mount_mutex{};
    std::vector<MountPoint> m_mounts{};
};

} // namespace portage::fs
