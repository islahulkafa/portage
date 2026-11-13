#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    SyncFence,
    Signalfd,
    Netlink
};

struct ResolvedPath {
    DeviceType dev_type{DeviceType::None};
    std::filesystem::path host_path{};
    int status{0};
    bool is_symlink{false};
    std::string symlink_target{};
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
    [[nodiscard]] std::vector<MountPoint> GetMounts() const;
    void ClearMounts();

    bool CreateSymlink(std::string_view target, std::string_view linkpath, int dirfd = -100,
                       std::string_view dirfd_path = "");
    bool RemoveSymlink(std::string_view linkpath, int dirfd = -100,
                       std::string_view dirfd_path = "");
    bool RenameSymlink(std::string_view oldpath, std::string_view newpath);
    [[nodiscard]] std::optional<std::string> Readlink(std::string_view linkpath, int dirfd = -100,
                                                      std::string_view dirfd_path = "") const;
    [[nodiscard]] bool IsSymlink(std::string_view linkpath, int dirfd = -100,
                                 std::string_view dirfd_path = "") const;

    void SetMode(std::string_view guest_path, uint32_t mode);
    [[nodiscard]] std::optional<uint32_t> GetMode(std::string_view guest_path) const;

    [[nodiscard]] ResolvedPath Resolve(std::string_view guest_path,
                                       bool follow_symlinks = true) const;
    [[nodiscard]] ResolvedPath ResolveAt(int dirfd, std::string_view guest_path,
                                         std::string_view dirfd_path = "",
                                         bool follow_symlinks = true) const;

    [[nodiscard]] static std::string NormalizePath(std::string_view path);

  private:
    [[nodiscard]] std::string CombineAtPath(int dirfd, std::string_view guest_path,
                                            std::string_view dirfd_path) const;
    [[nodiscard]] ResolvedPath ResolveNoSymlink(std::string_view normalized_path) const;

    std::filesystem::path m_rootfs{};
    std::filesystem::path m_executable_path{};
    std::string m_cwd{"/"};
    mutable std::mutex m_mount_mutex{};
    std::vector<MountPoint> m_mounts{};
    mutable std::mutex m_symlink_mutex{};
    std::unordered_map<std::string, std::string> m_virtual_symlinks{};
    std::unordered_map<std::string, uint32_t> m_custom_modes{};
};

} // namespace portage::fs
