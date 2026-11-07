#include "fs/vfs.hpp"
#include "package/package_manager.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace portage::fs {

Vfs::Vfs(std::filesystem::path rootfs) : m_rootfs(std::move(rootfs)) {}

void Vfs::SetRootfs(std::filesystem::path rootfs) { m_rootfs = std::move(rootfs); }

const std::filesystem::path &Vfs::GetRootfs() const noexcept { return m_rootfs; }

void Vfs::SetExecutablePath(std::filesystem::path exe_path)
{
    m_executable_path = std::move(exe_path);
}

const std::filesystem::path &Vfs::GetExecutablePath() const noexcept { return m_executable_path; }

const std::string &Vfs::GetCwd() const noexcept { return m_cwd; }

std::string Vfs::NormalizePath(std::string_view path)
{
    if (path.empty()) {
        return "/";
    }

    std::string clean;
    clean.reserve(path.size());
    for (char c : path) {
        if (c == '\\') {
            clean.push_back('/');
        } else {
            clean.push_back(c);
        }
    }

    bool is_absolute = clean.front() == '/';

    std::vector<std::string> parts;
    std::stringstream ss(clean);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (token.empty() || token == ".") {
            continue;
        }
        if (token == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else {
            parts.push_back(token);
        }
    }

    std::string result;
    if (is_absolute) {
        result = "/";
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        result += parts[i];
        if (i + 1 < parts.size()) {
            result += "/";
        }
    }

    if (result.empty()) {
        return "/";
    }
    return result;
}

int Vfs::ChDir(std::string_view path)
{
    ResolvedPath resolved = ResolveAt(-100, path);
    if (resolved.dev_type == DeviceType::Directory ||
        (resolved.dev_type == DeviceType::Regular &&
         std::filesystem::is_directory(resolved.host_path))) {
        if (path.starts_with('/')) {
            m_cwd = NormalizePath(path);
        } else {
            std::string combined =
                (m_cwd == "/") ? ("/" + std::string(path)) : (m_cwd + "/" + std::string(path));
            m_cwd = NormalizePath(combined);
        }
        return 0;
    }
    if (std::filesystem::exists(resolved.host_path) &&
        !std::filesystem::is_directory(resolved.host_path)) {
        return -20; // -ENOTDIR
    }
    return -2; // -ENOENT
}

ResolvedPath Vfs::ResolveAt(int dirfd, std::string_view guest_path,
                            std::string_view dirfd_path) const
{
    if (guest_path.empty()) {
        return {DeviceType::None, {}};
    }
    if (guest_path.starts_with('/')) {
        return Resolve(guest_path);
    }

    std::string base = (dirfd == -100 || dirfd_path.empty()) ? m_cwd : std::string(dirfd_path);
    std::string combined;
    if (base == "/") {
        combined = "/" + std::string(guest_path);
    } else {
        combined = base + "/" + std::string(guest_path);
    }
    return Resolve(combined);
}

ResolvedPath Vfs::Resolve(std::string_view guest_path) const
{
    if (guest_path.empty()) {
        return {DeviceType::None, {}};
    }

    if (guest_path == "/dev/null") {
        return {DeviceType::DevNull, {}};
    }
    if (guest_path == "/dev/zero") {
        return {DeviceType::DevZero, {}};
    }
    if (guest_path == "/dev/urandom" || guest_path == "/dev/random") {
        return {DeviceType::DevUrandom, {}};
    }
    if (guest_path == "/dev/binder") {
        return {DeviceType::Binder, {}};
    }
    if (guest_path == "/dev/hwbinder") {
        return {DeviceType::HwBinder, {}};
    }
    if (guest_path == "/dev/ashmem") {
        return {DeviceType::Ashmem, {}};
    }
    if (guest_path == "/dev/vulkan") {
        return {DeviceType::VulkanDevice, {}};
    }
    if (guest_path == "/dev/snd/pcmC0D0p") {
        return {DeviceType::AlsaPcm, {}};
    }
    if (guest_path == "/dev/snd/controlC0") {
        return {DeviceType::AlsaControl, {}};
    }
    if (guest_path == "/dev/input/event0") {
        return {DeviceType::InputEvent0, {}};
    }
    if (guest_path == "/dev/input/event1") {
        return {DeviceType::InputEvent1, {}};
    }
    if (guest_path == "/dev/input" || guest_path == "/dev/input/") {
        return {DeviceType::InputDir, "/dev/input"};
    }
    if (guest_path == "/dev/__properties__" || guest_path == "/dev/__properties__/") {
        return {DeviceType::PropAreaDir, "/dev/__properties__"};
    }
    if (guest_path == "/dev/__properties__/properties_serial") {
        return {DeviceType::PropSerial, {}};
    }
    if (guest_path == "/dev/__properties__/property_info") {
        return {DeviceType::PropInfo, {}};
    }
    if (guest_path.starts_with("/dev/__properties__/")) {
        return {DeviceType::PropContext, {}};
    }
    if (guest_path == "/dev/socket" || guest_path == "/dev/socket/") {
        return {DeviceType::PropSocketDir, "/dev/socket"};
    }
    if (guest_path == "/dev/socket/property_service") {
        return {DeviceType::PropServiceSocket, {}};
    }
    if (guest_path == "/proc/self/exe") {
        return {DeviceType::Regular, m_executable_path};
    }

    std::string normalized = NormalizePath(guest_path);

    if (normalized.starts_with("/data/") || normalized.starts_with("/sdcard/") ||
        normalized == "/data" || normalized == "/sdcard") {
        const auto &sandbox = package::PackageManager::Instance().GetSandboxBase();
        if (!sandbox.empty()) {
            std::string rel_sub =
                (normalized == "/" || normalized.empty()) ? "." : normalized.substr(1);
            std::filesystem::path candidate = sandbox / rel_sub;
            if (std::filesystem::exists(candidate)) {
                if (std::filesystem::is_directory(candidate)) {
                    return {DeviceType::Directory, candidate};
                }
                return {DeviceType::Regular, candidate};
            }
            return {DeviceType::Regular, candidate};
        }
    }

    if (normalized == "/proc" || normalized == "/proc/self") {
        return {DeviceType::Directory, normalized};
    }
    if (normalized.starts_with("/proc/")) {
        return {DeviceType::SyntheticProc, normalized};
    }

    if (!m_rootfs.empty() && normalized.starts_with('/')) {
        std::string rel_sub = (normalized == "/") ? "." : normalized.substr(1);
        std::filesystem::path candidate = m_rootfs / rel_sub;
        if (std::filesystem::exists(candidate)) {
            if (std::filesystem::is_directory(candidate)) {
                return {DeviceType::Directory, candidate};
            }
            return {DeviceType::Regular, candidate};
        }
        return {DeviceType::Regular, candidate};
    }

    if (normalized.starts_with('/')) {
        std::string rel_sub = (normalized == "/") ? "." : normalized.substr(1);
        std::filesystem::path local_candidate = rel_sub;
        if (std::filesystem::exists(local_candidate)) {
            if (std::filesystem::is_directory(local_candidate)) {
                return {DeviceType::Directory, local_candidate};
            }
            return {DeviceType::Regular, local_candidate};
        }
        return {DeviceType::Regular, rel_sub};
    }

    if (std::filesystem::exists(normalized) && std::filesystem::is_directory(normalized)) {
        return {DeviceType::Directory, normalized};
    }

    return {DeviceType::Regular, normalized};
}

} // namespace portage::fs
