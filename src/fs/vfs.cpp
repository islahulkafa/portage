#include "fs/vfs.hpp"
#include "fs/proc_fs.hpp"
#include "package/package_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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

bool Vfs::AddMount(std::string_view guest_path, const std::filesystem::path &host_path)
{
    if (guest_path.empty() || host_path.empty()) {
        return false;
    }
    std::string norm = NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mount_mutex);
    for (auto &m : m_mounts) {
        if (m.guest_path == norm) {
            m.host_path = host_path;
            return true;
        }
    }
    m_mounts.push_back({norm, host_path});
    return true;
}

bool Vfs::RemoveMount(std::string_view guest_path)
{
    if (guest_path.empty()) {
        return false;
    }
    std::string norm = NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mount_mutex);
    for (auto it = m_mounts.begin(); it != m_mounts.end(); ++it) {
        if (it->guest_path == norm) {
            m_mounts.erase(it);
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> Vfs::GetMountTarget(std::string_view guest_path) const
{
    std::string norm = NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_mount_mutex);
    for (const auto &m : m_mounts) {
        if (m.guest_path == norm) {
            return m.host_path;
        }
    }
    return std::nullopt;
}

void Vfs::ClearMounts()
{
    std::lock_guard<std::mutex> lock(m_mount_mutex);
    m_mounts.clear();
}

std::string Vfs::CombineAtPath(int dirfd, std::string_view guest_path,
                               std::string_view dirfd_path) const
{
    if (guest_path.starts_with('/')) {
        return std::string(guest_path);
    }
    std::string base = (dirfd == -100 || dirfd_path.empty()) ? m_cwd : std::string(dirfd_path);
    if (base == "/") {
        return "/" + std::string(guest_path);
    }
    return base + "/" + std::string(guest_path);
}

bool Vfs::CreateSymlink(std::string_view target, std::string_view linkpath, int dirfd,
                        std::string_view dirfd_path)
{
    if (target.empty() || linkpath.empty()) {
        return false;
    }

    std::string combined = CombineAtPath(dirfd, linkpath, dirfd_path);
    std::string normalized = NormalizePath(combined);

    {
        std::lock_guard<std::mutex> lock(m_symlink_mutex);
        if (m_virtual_symlinks.contains(normalized)) {
            return false;
        }
    }

    ResolvedPath existing = ResolveNoSymlink(normalized);
    std::error_code ec;
    if (existing.dev_type == DeviceType::Regular && !existing.host_path.empty() &&
        (std::filesystem::exists(existing.host_path, ec) ||
         std::filesystem::is_symlink(existing.host_path, ec))) {
        return false;
    }

    if (existing.dev_type == DeviceType::Regular && !existing.host_path.empty()) {
        std::string target_combined =
            target.starts_with('/')
                ? std::string(target)
                : NormalizePath(
                      (normalized.rfind('/') == 0 ? "/" : normalized.substr(0, normalized.rfind('/'))) +
                      "/" + std::string(target));
        ResolvedPath target_resolved = Resolve(target_combined);

        DWORD symlink_flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        if (!target_resolved.host_path.empty() &&
            std::filesystem::is_directory(target_resolved.host_path, ec)) {
            symlink_flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
        }

        std::wstring link_wpath = existing.host_path.wstring();
        std::wstring target_wpath = target_resolved.host_path.empty()
                                        ? std::filesystem::path(target).wstring()
                                        : target_resolved.host_path.wstring();
        CreateSymbolicLinkW(link_wpath.c_str(), target_wpath.c_str(), symlink_flags);
    }

    std::lock_guard<std::mutex> lock(m_symlink_mutex);
    m_virtual_symlinks[normalized] = std::string(target);
    return true;
}

bool Vfs::RemoveSymlink(std::string_view linkpath, int dirfd, std::string_view dirfd_path)
{
    if (linkpath.empty()) {
        return false;
    }
    std::string normalized = NormalizePath(CombineAtPath(dirfd, linkpath, dirfd_path));
    std::lock_guard<std::mutex> lock(m_symlink_mutex);
    return m_virtual_symlinks.erase(normalized) > 0;
}

bool Vfs::RenameSymlink(std::string_view oldpath, std::string_view newpath)
{
    if (oldpath.empty() || newpath.empty()) {
        return false;
    }
    std::string norm_old = NormalizePath(CombineAtPath(-100, oldpath, ""));
    std::string norm_new = NormalizePath(CombineAtPath(-100, newpath, ""));
    std::lock_guard<std::mutex> lock(m_symlink_mutex);
    auto it = m_virtual_symlinks.find(norm_old);
    if (it == m_virtual_symlinks.end()) {
        return false;
    }
    std::string target = std::move(it->second);
    m_virtual_symlinks.erase(it);
    m_virtual_symlinks[norm_new] = std::move(target);
    return true;
}

std::optional<std::string> Vfs::Readlink(std::string_view linkpath, int dirfd,
                                         std::string_view dirfd_path) const
{
    if (linkpath.empty()) {
        return std::nullopt;
    }

    std::string combined = CombineAtPath(dirfd, linkpath, dirfd_path);
    ResolvedPath resolved = Resolve(combined, false);
    if (resolved.status != 0) {
        return std::nullopt;
    }
    if (resolved.is_symlink) {
        return resolved.symlink_target;
    }

    std::string normalized = NormalizePath(combined);
    {
        std::lock_guard<std::mutex> lock(m_symlink_mutex);
        auto it = m_virtual_symlinks.find(normalized);
        if (it != m_virtual_symlinks.end()) {
            return it->second;
        }
    }

    if (resolved.dev_type == DeviceType::Regular && !resolved.host_path.empty()) {
        std::error_code ec;
        if (std::filesystem::is_symlink(resolved.host_path, ec)) {
            auto target = std::filesystem::read_symlink(resolved.host_path, ec);
            if (!ec) {
                return target.string();
            }
        }
    }
    return std::nullopt;
}

bool Vfs::IsSymlink(std::string_view linkpath, int dirfd, std::string_view dirfd_path) const
{
    return Readlink(linkpath, dirfd, dirfd_path).has_value();
}

void Vfs::SetMode(std::string_view guest_path, uint32_t mode)
{
    std::string normalized = NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_symlink_mutex);
    m_custom_modes[normalized] = mode & 07777;
}

std::optional<uint32_t> Vfs::GetMode(std::string_view guest_path) const
{
    std::string normalized = NormalizePath(guest_path);
    std::lock_guard<std::mutex> lock(m_symlink_mutex);
    auto it = m_custom_modes.find(normalized);
    if (it != m_custom_modes.end()) {
        return it->second;
    }
    return std::nullopt;
}

ResolvedPath Vfs::ResolveAt(int dirfd, std::string_view guest_path, std::string_view dirfd_path,
                            bool follow_symlinks) const
{
    if (guest_path.empty()) {
        return {DeviceType::None, {}};
    }
    std::string combined = CombineAtPath(dirfd, guest_path, dirfd_path);
    return Resolve(combined, follow_symlinks);
}

ResolvedPath Vfs::Resolve(std::string_view guest_path, bool follow_symlinks) const
{
    if (guest_path.empty()) {
        return {DeviceType::None, {}};
    }

    std::string normalized = NormalizePath(guest_path);
    if (normalized == "/storage/emulated/0") {
        normalized = "/sdcard";
    } else if (normalized.starts_with("/storage/emulated/0/")) {
        normalized = "/sdcard" + normalized.substr(19);
    }

    constexpr int kMaxSymlinkHops = 40;
    int hops = 0;

    auto split_components = [](std::string_view path) {
        std::vector<std::string> parts;
        std::stringstream ss{std::string(path)};
        std::string token;
        while (std::getline(ss, token, '/')) {
            if (!token.empty()) {
                parts.push_back(std::move(token));
            }
        }
        return parts;
    };

    bool is_abs = normalized.starts_with('/');
    std::vector<std::string> components = split_components(normalized);
    if (components.empty()) {
        return ResolveNoSymlink(normalized);
    }

    std::string current;
    for (size_t i = 0; i < components.size(); ++i) {
        if (is_abs) {
            current += "/" + components[i];
        } else {
            current += (current.empty() ? "" : "/") + components[i];
        }

        const bool is_last = (i + 1 == components.size());

        std::optional<std::string> link_target;
        {
            std::lock_guard<std::mutex> lock(m_symlink_mutex);
            auto it = m_virtual_symlinks.find(current);
            if (it != m_virtual_symlinks.end()) {
                link_target = it->second;
            }
        }

        if (!link_target.has_value()) {
            ResolvedPath partial = ResolveNoSymlink(current);
            if (partial.dev_type == DeviceType::Regular && !partial.host_path.empty()) {
                std::error_code ec;
                if (std::filesystem::is_symlink(partial.host_path, ec)) {
                    auto target = std::filesystem::read_symlink(partial.host_path, ec);
                    if (!ec) {
                        link_target = target.string();
                    }
                }
            }
        }

        if (link_target.has_value()) {
            if (is_last && !follow_symlinks) {
                ResolvedPath res = ResolveNoSymlink(current);
                res.is_symlink = true;
                res.symlink_target = *link_target;
                return res;
            }

            ++hops;
            if (hops > kMaxSymlinkHops) {
                return {DeviceType::None, {}, -40, false, {}};
            }

            std::string rebuilt;
            if (link_target->starts_with('/')) {
                rebuilt = *link_target;
            } else {
                auto pos = current.rfind('/');
                std::string parent =
                    (pos == std::string::npos) ? "" : (pos == 0 ? "/" : current.substr(0, pos));
                if (parent.empty()) {
                    rebuilt = *link_target;
                } else if (parent == "/") {
                    rebuilt = "/" + *link_target;
                } else {
                    rebuilt = parent + "/" + *link_target;
                }
            }

            for (size_t j = i + 1; j < components.size(); ++j) {
                rebuilt += "/" + components[j];
            }

            normalized = NormalizePath(rebuilt);
            if (normalized == "/storage/emulated/0") {
                normalized = "/sdcard";
            } else if (normalized.starts_with("/storage/emulated/0/")) {
                normalized = "/sdcard" + normalized.substr(19);
            }

            is_abs = normalized.starts_with('/');
            components = split_components(normalized);
            if (components.empty()) {
                return ResolveNoSymlink(normalized);
            }
            current.clear();
            i = static_cast<size_t>(-1);
        }
    }

    return ResolveNoSymlink(current);
}

ResolvedPath Vfs::ResolveNoSymlink(std::string_view guest_path) const
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

    if (normalized == "/storage/emulated/0") {
        normalized = "/sdcard";
    } else if (normalized.starts_with("/storage/emulated/0/")) {
        normalized = "/sdcard" + normalized.substr(19);
    }

    {
        std::lock_guard<std::mutex> lock(m_mount_mutex);
        for (const auto &m : m_mounts) {
            if (normalized == m.guest_path) {
                if (std::filesystem::exists(m.host_path)) {
                    if (std::filesystem::is_directory(m.host_path)) {
                        return {DeviceType::Directory, m.host_path};
                    }
                    return {DeviceType::Regular, m.host_path};
                }
                return {DeviceType::Directory, m.host_path};
            }
            if (normalized.starts_with(m.guest_path + "/")) {
                std::string rel = normalized.substr(m.guest_path.length() + 1);
                std::filesystem::path target = m.host_path / rel;
                if (std::filesystem::exists(target)) {
                    if (std::filesystem::is_directory(target)) {
                        return {DeviceType::Directory, target};
                    }
                    return {DeviceType::Regular, target};
                }
                return {DeviceType::Regular, target};
            }
        }
    }

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

    if (ProcFs::IsProcPath(normalized)) {
        std::string canon = ProcFs::CanonicalizePath(normalized);
        if (canon == "/proc/self/exe") {
            return {DeviceType::Regular, m_executable_path};
        }
        if (canon == "/proc" || canon == "/proc/self" || canon == "/proc/self/fd" ||
            canon == "/proc/self/task" || canon == "/proc/sys" || canon == "/proc/sys/kernel" ||
            canon == "/proc/sys/kernel/random" || canon == "/sys" || canon == "/sys/devices" ||
            canon == "/sys/devices/system" || canon == "/sys/devices/system/cpu" ||
            (canon.starts_with("/proc/self/task/") && canon.find('/', 16) == std::string::npos) ||
            (canon.starts_with("/sys/devices/system/cpu/cpu") &&
             canon.find('/', 24) == std::string::npos)) {
            return {DeviceType::Directory, canon};
        }
        return {DeviceType::SyntheticProc, canon};
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
