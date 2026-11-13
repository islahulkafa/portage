#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::package {

inline constexpr int32_t FLAG_GRANT_READ_URI_PERMISSION = 0x01;
inline constexpr int32_t FLAG_GRANT_WRITE_URI_PERMISSION = 0x02;

inline constexpr int32_t DOCUMENT_FLAG_SUPPORTS_WRITE = 0x02;
inline constexpr int32_t DOCUMENT_FLAG_SUPPORTS_DELETE = 0x04;
inline constexpr int32_t DOCUMENT_FLAG_DIR_SUPPORTS_CREATE = 0x08;
inline constexpr int32_t DOCUMENT_FLAG_SUPPORTS_RENAME = 0x40;

struct DocumentMetadata {
    std::string document_id{};
    std::string display_name{};
    std::string mime_type{};
    int64_t size{0};
    int64_t last_modified{0};
    int32_t flags{0};
    bool is_directory{false};
};

struct UriPermission {
    std::string uri{};
    bool read{false};
    bool write{false};
};

class ContentProviderManager {
  public:
    static ContentProviderManager &Instance() noexcept;

    void Initialize(const std::filesystem::path &sandbox_root);
    void Reset();

    [[nodiscard]] std::optional<std::filesystem::path> ResolveUri(std::string_view uri) const;
    [[nodiscard]] std::string BuildDocumentUri(std::string_view authority,
                                               std::string_view document_id) const;
    [[nodiscard]] std::string BuildTreeDocumentUri(std::string_view tree_uri,
                                                   std::string_view document_id) const;

    [[nodiscard]] std::vector<DocumentMetadata> Query(std::string_view uri) const;
    std::optional<std::string> CreateDocument(std::string_view parent_uri,
                                              std::string_view mime_type,
                                              std::string_view display_name);
    bool DeleteDocument(std::string_view uri);
    std::optional<std::string> RenameDocument(std::string_view uri, std::string_view new_name);

    void TakePersistableUriPermission(std::string_view uri, int32_t mode_flags);
    void ReleasePersistableUriPermission(std::string_view uri, int32_t mode_flags);
    [[nodiscard]] std::vector<UriPermission> GetPersistedUriPermissions() const;

    [[nodiscard]] std::string GetMimeType(const std::filesystem::path &path) const;
    [[nodiscard]] const std::filesystem::path &GetSandboxRoot() const noexcept;

    void RegisterMedia(std::string_view media_id, const std::filesystem::path &host_path,
                       std::string_view mime_type = "");

    static std::string UrlDecode(std::string_view in);
    static std::string UrlEncode(std::string_view in);

  private:
    ContentProviderManager() = default;

    [[nodiscard]] std::optional<std::filesystem::path>
    ResolveDocumentId(std::string_view authority, std::string_view doc_id) const;

    mutable std::mutex m_mutex{};
    std::filesystem::path m_sandbox_root{};
    std::unordered_map<std::string, UriPermission> m_persisted_permissions{};
    std::unordered_map<std::string, std::pair<std::filesystem::path, std::string>>
        m_registered_media{};
};

} // namespace portage::package
