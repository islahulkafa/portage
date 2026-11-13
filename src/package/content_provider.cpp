#include "package/content_provider.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <windows.h>

#include "package/package_manager.hpp"

namespace portage::package {

namespace {

int64_t GetLastModifiedMs(const std::filesystem::path &path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER uli;
        uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
        constexpr uint64_t kEpochDiff = 116444736000000000ULL;
        if (uli.QuadPart >= kEpochDiff) {
            return static_cast<int64_t>((uli.QuadPart - kEpochDiff) / 10000ULL);
        }
    }
    return 0;
}

bool IsWithinRoot(const std::filesystem::path &child, const std::filesystem::path &root)
{
    std::error_code ec;
    auto canon_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        canon_root = root.lexically_normal();
    }
    auto canon_child = std::filesystem::weakly_canonical(child, ec);
    if (ec) {
        canon_child = child.lexically_normal();
    }

    auto root_str = canon_root.generic_string();
    auto child_str = canon_child.generic_string();

    if (root_str.ends_with('/')) {
        root_str.pop_back();
    }

    if (child_str == root_str) {
        return true;
    }
    if (child_str.starts_with(root_str) && child_str.size() > root_str.size() &&
        child_str[root_str.size()] == '/') {
        return true;
    }
    return false;
}

} // namespace

ContentProviderManager &ContentProviderManager::Instance() noexcept
{
    static ContentProviderManager instance;
    return instance;
}

void ContentProviderManager::Initialize(const std::filesystem::path &sandbox_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sandbox_root = sandbox_root;
    std::error_code ec;
    std::filesystem::create_directories(m_sandbox_root, ec);
    std::filesystem::create_directories(m_sandbox_root / "Documents", ec);
    std::filesystem::create_directories(m_sandbox_root / "Downloads", ec);
    std::filesystem::create_directories(m_sandbox_root / "Pictures", ec);
    std::filesystem::create_directories(m_sandbox_root / "Media", ec);
}

void ContentProviderManager::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sandbox_root.clear();
    m_persisted_permissions.clear();
    m_registered_media.clear();
}

const std::filesystem::path &ContentProviderManager::GetSandboxRoot() const noexcept
{
    if (m_sandbox_root.empty()) {
        const auto &pkg_sandbox = PackageManager::Instance().GetSandboxBase();
        if (!pkg_sandbox.empty()) {
            static std::filesystem::path s_fallback;
            s_fallback = pkg_sandbox / "sdcard";
            return s_fallback;
        }
    }
    return m_sandbox_root;
}

std::string ContentProviderManager::UrlDecode(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            int h1 = hex_val(in[i + 1]);
            int h2 = hex_val(in[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string ContentProviderManager::UrlEncode(std::string_view in)
{
    std::string out;
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
            out.push_back(hex[static_cast<unsigned char>(c) & 0xF]);
        }
    }
    return out;
}

std::optional<std::filesystem::path>
ContentProviderManager::ResolveDocumentId(std::string_view authority, std::string_view doc_id) const
{
    if (authority != "com.android.externalstorage.documents") {
        return std::nullopt;
    }

    std::string decoded = UrlDecode(doc_id);
    if (decoded.find("..") != std::string::npos) {
        return std::nullopt;
    }

    std::string rel;
    if (decoded.starts_with("primary:")) {
        rel = decoded.substr(8);
    } else if (decoded == "primary") {
        rel = "";
    } else {
        rel = decoded;
    }

    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
        rel.erase(rel.begin());
    }

    const auto &root = GetSandboxRoot();
    if (root.empty()) {
        return std::nullopt;
    }

    std::filesystem::path resolved = rel.empty() ? root : (root / rel);
    if (!IsWithinRoot(resolved, root)) {
        return std::nullopt;
    }

    return resolved;
}

std::optional<std::filesystem::path> ContentProviderManager::ResolveUri(std::string_view uri) const
{
    if (!uri.starts_with("content://")) {
        return std::nullopt;
    }

    size_t host_start = 10;
    size_t path_start = uri.find('/', host_start);
    if (path_start == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view authority = uri.substr(host_start, path_start - host_start);
    std::string_view path_part = uri.substr(path_start);

    std::lock_guard<std::mutex> lock(m_mutex);

    if (authority == "com.android.externalstorage.documents") {
        std::string path_str(path_part);
        if (path_str.ends_with("/children")) {
            path_str = path_str.substr(0, path_str.size() - 9);
        }

        std::string doc_id;
        constexpr std::string_view kDocPrefix = "/document/";
        constexpr std::string_view kTreePrefix = "/tree/";

        auto doc_pos = path_str.rfind("/document/");
        if (doc_pos != std::string::npos) {
            doc_id = path_str.substr(doc_pos + kDocPrefix.size());
        } else if (path_str.starts_with(kTreePrefix)) {
            doc_id = path_str.substr(kTreePrefix.size());
        } else if (path_str.starts_with(kDocPrefix)) {
            doc_id = path_str.substr(kDocPrefix.size());
        } else {
            return std::nullopt;
        }

        return ResolveDocumentId(authority, doc_id);
    }

    if (authority == "media") {
        std::string decoded_path = UrlDecode(path_part);
        if (decoded_path.find("..") != std::string::npos) {
            return std::nullopt;
        }

        for (const auto &[key, entry] : m_registered_media) {
            if (uri.find(key) != std::string_view::npos ||
                decoded_path.find(key) != std::string::npos) {
                return entry.first;
            }
        }

        const auto &root = GetSandboxRoot();
        if (root.empty()) {
            return std::nullopt;
        }

        std::string rel = decoded_path;
        if (rel.starts_with("/external/")) {
            rel = rel.substr(10);
        } else if (rel.starts_with('/')) {
            rel = rel.substr(1);
        }

        std::filesystem::path candidate = root / "Media" / rel;
        if (IsWithinRoot(candidate, root)) {
            return candidate;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::string ContentProviderManager::BuildDocumentUri(std::string_view authority,
                                                     std::string_view document_id) const
{
    return std::string("content://") + std::string(authority) + "/document/" +
           UrlEncode(document_id);
}

std::string ContentProviderManager::BuildTreeDocumentUri(std::string_view tree_uri,
                                                         std::string_view document_id) const
{
    std::string res(tree_uri);
    if (!res.ends_with('/')) {
        res += "/";
    }
    res += "document/" + UrlEncode(document_id);
    return res;
}

std::vector<DocumentMetadata> ContentProviderManager::Query(std::string_view uri) const
{
    std::vector<DocumentMetadata> results;
    bool is_children_query = uri.ends_with("/children");

    auto target_path_opt = ResolveUri(uri);
    if (!target_path_opt.has_value()) {
        return results;
    }

    const auto &target_path = *target_path_opt;
    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec)) {
        return results;
    }

    const auto &root = GetSandboxRoot();

    auto make_metadata = [&](const std::filesystem::path &p) -> DocumentMetadata {
        DocumentMetadata meta{};
        meta.is_directory = std::filesystem::is_directory(p, ec);
        meta.display_name = p.filename().string();
        meta.size = meta.is_directory ? 0 : static_cast<int64_t>(std::filesystem::file_size(p, ec));
        meta.last_modified = GetLastModifiedMs(p);
        meta.mime_type = meta.is_directory ? "vnd.android.document/directory" : GetMimeType(p);

        std::string rel = std::filesystem::relative(p, root, ec).generic_string();
        if (rel == "." || rel.empty()) {
            meta.document_id = "primary:";
        } else {
            meta.document_id = "primary:" + rel;
        }

        meta.flags = DOCUMENT_FLAG_SUPPORTS_DELETE | DOCUMENT_FLAG_SUPPORTS_RENAME;
        if (meta.is_directory) {
            meta.flags |= DOCUMENT_FLAG_DIR_SUPPORTS_CREATE;
        } else {
            meta.flags |= DOCUMENT_FLAG_SUPPORTS_WRITE;
        }

        return meta;
    };

    if (is_children_query) {
        if (std::filesystem::is_directory(target_path, ec)) {
            for (const auto &entry : std::filesystem::directory_iterator(target_path, ec)) {
                results.push_back(make_metadata(entry.path()));
            }
        }
    } else {
        results.push_back(make_metadata(target_path));
    }

    return results;
}

std::optional<std::string> ContentProviderManager::CreateDocument(std::string_view parent_uri,
                                                                  std::string_view mime_type,
                                                                  std::string_view display_name)
{
    if (display_name.find("..") != std::string_view::npos ||
        display_name.find('/') != std::string_view::npos ||
        display_name.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }

    auto parent_opt = ResolveUri(parent_uri);
    if (!parent_opt.has_value()) {
        return std::nullopt;
    }

    std::filesystem::path parent_path = *parent_opt;
    std::error_code ec;
    if (!std::filesystem::exists(parent_path, ec)) {
        std::filesystem::create_directories(parent_path, ec);
    }

    std::filesystem::path child_path = parent_path / std::string(display_name);
    const auto &root = GetSandboxRoot();
    if (!IsWithinRoot(child_path, root)) {
        return std::nullopt;
    }

    if (mime_type == "vnd.android.document/directory") {
        if (!std::filesystem::create_directories(child_path, ec) &&
            !std::filesystem::exists(child_path, ec)) {
            return std::nullopt;
        }
    } else {
        std::ofstream ofs(child_path, std::ios::binary);
        if (!ofs.is_open()) {
            return std::nullopt;
        }
    }

    std::string rel = std::filesystem::relative(child_path, root, ec).generic_string();
    std::string doc_id = "primary:" + rel;
    return BuildDocumentUri("com.android.externalstorage.documents", doc_id);
}

bool ContentProviderManager::DeleteDocument(std::string_view uri)
{
    auto target_opt = ResolveUri(uri);
    if (!target_opt.has_value()) {
        return false;
    }

    const auto &target_path = *target_opt;
    const auto &root = GetSandboxRoot();
    if (target_path == root) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec)) {
        return false;
    }

    std::filesystem::remove_all(target_path, ec);
    return !ec;
}

std::optional<std::string> ContentProviderManager::RenameDocument(std::string_view uri,
                                                                  std::string_view new_name)
{
    if (new_name.find("..") != std::string_view::npos ||
        new_name.find('/') != std::string_view::npos ||
        new_name.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }

    auto target_opt = ResolveUri(uri);
    if (!target_opt.has_value()) {
        return std::nullopt;
    }

    const auto &target_path = *target_opt;
    const auto &root = GetSandboxRoot();
    if (target_path == root) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec)) {
        return std::nullopt;
    }

    std::filesystem::path parent_path = target_path.parent_path();
    std::filesystem::path new_path = parent_path / std::string(new_name);

    if (!IsWithinRoot(new_path, root)) {
        return std::nullopt;
    }

    std::filesystem::rename(target_path, new_path, ec);
    if (ec) {
        return std::nullopt;
    }

    std::string rel = std::filesystem::relative(new_path, root, ec).generic_string();
    std::string doc_id = "primary:" + rel;
    return BuildDocumentUri("com.android.externalstorage.documents", doc_id);
}

void ContentProviderManager::TakePersistableUriPermission(std::string_view uri, int32_t mode_flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &entry = m_persisted_permissions[std::string(uri)];
    entry.uri = std::string(uri);
    if (mode_flags & FLAG_GRANT_READ_URI_PERMISSION) {
        entry.read = true;
    }
    if (mode_flags & FLAG_GRANT_WRITE_URI_PERMISSION) {
        entry.write = true;
    }
}

void ContentProviderManager::ReleasePersistableUriPermission(std::string_view uri,
                                                             int32_t mode_flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_persisted_permissions.find(std::string(uri));
    if (it != m_persisted_permissions.end()) {
        if (mode_flags & FLAG_GRANT_READ_URI_PERMISSION) {
            it->second.read = false;
        }
        if (mode_flags & FLAG_GRANT_WRITE_URI_PERMISSION) {
            it->second.write = false;
        }
        if (!it->second.read && !it->second.write) {
            m_persisted_permissions.erase(it);
        }
    }
}

std::vector<UriPermission> ContentProviderManager::GetPersistedUriPermissions() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UriPermission> list;
    list.reserve(m_persisted_permissions.size());
    for (const auto &[_, perm] : m_persisted_permissions) {
        list.push_back(perm);
    }
    return list;
}

std::string ContentProviderManager::GetMimeType(const std::filesystem::path &path) const
{
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return "vnd.android.document/directory";
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".txt") {
        return "text/plain";
    }
    if (ext == ".html" || ext == ".htm") {
        return "text/html";
    }
    if (ext == ".json") {
        return "application/json";
    }
    if (ext == ".xml") {
        return "application/xml";
    }
    if (ext == ".pdf") {
        return "application/pdf";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    if (ext == ".webp") {
        return "image/webp";
    }
    if (ext == ".mp4") {
        return "video/mp4";
    }
    if (ext == ".mkv") {
        return "video/x-matroska";
    }
    if (ext == ".mp3") {
        return "audio/mpeg";
    }
    if (ext == ".wav") {
        return "audio/wav";
    }
    if (ext == ".ogg") {
        return "audio/ogg";
    }

    return "application/octet-stream";
}

void ContentProviderManager::RegisterMedia(std::string_view media_id,
                                           const std::filesystem::path &host_path,
                                           std::string_view mime_type)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registered_media[std::string(media_id)] = {host_path, std::string(mime_type)};
}

} // namespace portage::package
