#pragma once

#include <filesystem>
#include <string>
#include <string_view>

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
    PropServiceSocket
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

    [[nodiscard]] ResolvedPath Resolve(std::string_view guest_path) const;
    [[nodiscard]] ResolvedPath ResolveAt(int dirfd, std::string_view guest_path,
                                         std::string_view dirfd_path = "") const;

    [[nodiscard]] static std::string NormalizePath(std::string_view path);

  private:
    std::filesystem::path m_rootfs{};
    std::filesystem::path m_executable_path{};
    std::string m_cwd{"/"};
};

} // namespace portage::fs
