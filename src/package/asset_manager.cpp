#include "asset_manager.hpp"

#include <algorithm>
#include <cstring>

namespace portage::package {

AssetManagerBridge &AssetManagerBridge::Instance() noexcept
{
    static AssetManagerBridge instance;
    return instance;
}

AAssetManager *AssetManagerBridge::CreateManager(std::shared_ptr<ZipArchive> archive)
{
    if (!archive)
        return nullptr;
    auto mgr = std::make_unique<AAssetManager>();
    mgr->archive = archive;
    mgr->openFunc = &AssetManagerBridge::Sysv_AAssetManager_open;
    mgr->readFunc = &AssetManagerBridge::Sysv_AAsset_read;
    mgr->seekFunc = &AssetManagerBridge::Sysv_AAsset_seek;
    mgr->getLengthFunc = &AssetManagerBridge::Sysv_AAsset_getLength;
    mgr->getBufferFunc = &AssetManagerBridge::Sysv_AAsset_getBuffer;
    mgr->closeFunc = &AssetManagerBridge::Sysv_AAsset_close;
    AAssetManager *raw = mgr.get();
    managers_.push_back(std::move(mgr));
    return raw;
}

void AssetManagerBridge::DestroyManager(AAssetManager *mgr)
{
    auto it =
        std::find_if(managers_.begin(), managers_.end(),
                     [mgr](const std::unique_ptr<AAssetManager> &p) { return p.get() == mgr; });
    if (it != managers_.end()) {
        managers_.erase(it);
    }
}

__attribute__((sysv_abi)) AAsset *
AssetManagerBridge::Sysv_AAssetManager_open(AAssetManager *mgr, const char *filename, int mode)
{
    (void)mode;
    if (!mgr || !mgr->archive || !filename) {
        return nullptr;
    }

    std::string path(filename);
    if (!path.starts_with("assets/")) {
        path = "assets/" + path;
    }

    if (!mgr->archive->HasEntry(path)) {
        return nullptr;
    }

    auto asset = std::make_unique<AAsset>();
    asset->filename = path;
    asset->readFunc = &AssetManagerBridge::Sysv_AAsset_read;
    asset->seekFunc = &AssetManagerBridge::Sysv_AAsset_seek;
    asset->getLengthFunc = &AssetManagerBridge::Sysv_AAsset_getLength;
    asset->getBufferFunc = &AssetManagerBridge::Sysv_AAsset_getBuffer;
    asset->closeFunc = &AssetManagerBridge::Sysv_AAsset_close;

    auto raw_span = mgr->archive->GetRawEntrySpan(path);
    if (raw_span.has_value()) {
        asset->raw_buffer = raw_span->data();
        asset->length = raw_span->size();
        asset->is_allocated = false;
        asset->offset = 0;
        return asset.release();
    }

    auto decompressed = mgr->archive->ReadEntry(path);
    if (!decompressed.has_value()) {
        return nullptr;
    }

    asset->buffer = std::move(*decompressed);
    asset->raw_buffer = asset->buffer.data();
    asset->length = asset->buffer.size();
    asset->is_allocated = true;
    asset->offset = 0;
    return asset.release();
}

__attribute__((sysv_abi)) AAssetDir *
AssetManagerBridge::Sysv_AAssetManager_openDir(AAssetManager *mgr, const char *dirName)
{
    if (!mgr || !mgr->archive) {
        return nullptr;
    }

    std::string prefix = "assets/";
    if (dirName && dirName[0] != '\0') {
        prefix += dirName;
        if (!prefix.ends_with('/'))
            prefix += '/';
    }

    auto dir = std::make_unique<AAssetDir>();
    for (const auto &entry : mgr->archive->GetAllEntries()) {
        if (entry.name.starts_with(prefix) && entry.name.size() > prefix.size()) {
            std::string sub = entry.name.substr(prefix.size());
            size_t slash = sub.find('/');
            if (slash == std::string::npos) {
                dir->file_names.push_back(sub);
            }
        }
    }

    return dir.release();
}

__attribute__((sysv_abi)) int AssetManagerBridge::Sysv_AAsset_read(AAsset *asset, void *buf,
                                                                   size_t count)
{
    if (!asset || !asset->raw_buffer || !buf) {
        return -1;
    }
    if (asset->offset >= asset->length) {
        return 0;
    }
    size_t to_read = std::min(count, asset->length - asset->offset);
    std::memcpy(buf, asset->raw_buffer + asset->offset, to_read);
    asset->offset += to_read;
    return static_cast<int>(to_read);
}

__attribute__((sysv_abi)) int64_t AssetManagerBridge::Sysv_AAsset_seek(AAsset *asset,
                                                                       int64_t offset, int whence)
{
    return Sysv_AAsset_seek64(asset, offset, whence);
}

__attribute__((sysv_abi)) int64_t AssetManagerBridge::Sysv_AAsset_seek64(AAsset *asset,
                                                                         int64_t offset, int whence)
{
    if (!asset) {
        return -1;
    }

    int64_t target = 0;
    if (whence == 0) { // SEEK_SET
        target = offset;
    } else if (whence == 1) { // SEEK_CUR
        target = static_cast<int64_t>(asset->offset) + offset;
    } else if (whence == 2) { // SEEK_END
        target = static_cast<int64_t>(asset->length) + offset;
    } else {
        return -1;
    }

    if (target < 0 || static_cast<size_t>(target) > asset->length) {
        return -1;
    }

    asset->offset = static_cast<size_t>(target);
    return target;
}

__attribute__((sysv_abi)) int64_t AssetManagerBridge::Sysv_AAsset_getLength(AAsset *asset)
{
    return Sysv_AAsset_getLength64(asset);
}

__attribute__((sysv_abi)) int64_t AssetManagerBridge::Sysv_AAsset_getLength64(AAsset *asset)
{
    return asset ? static_cast<int64_t>(asset->length) : -1;
}

__attribute__((sysv_abi)) int64_t AssetManagerBridge::Sysv_AAsset_getRemainingLength(AAsset *asset)
{
    return Sysv_AAsset_getRemainingLength64(asset);
}

__attribute__((sysv_abi)) int64_t
AssetManagerBridge::Sysv_AAsset_getRemainingLength64(AAsset *asset)
{
    if (!asset || asset->offset > asset->length)
        return -1;
    return static_cast<int64_t>(asset->length - asset->offset);
}

__attribute__((sysv_abi)) const void *AssetManagerBridge::Sysv_AAsset_getBuffer(AAsset *asset)
{
    return asset ? asset->raw_buffer : nullptr;
}

__attribute__((sysv_abi)) int AssetManagerBridge::Sysv_AAsset_isAllocated(AAsset *asset)
{
    return asset ? (asset->is_allocated ? 1 : 0) : 0;
}

__attribute__((sysv_abi)) void AssetManagerBridge::Sysv_AAsset_close(AAsset *asset)
{
    delete asset;
}

__attribute__((sysv_abi)) const char *
AssetManagerBridge::Sysv_AAssetDir_getNextFileName(AAssetDir *assetDir)
{
    if (!assetDir || assetDir->current_index >= assetDir->file_names.size()) {
        return nullptr;
    }
    return assetDir->file_names[assetDir->current_index++].c_str();
}

__attribute__((sysv_abi)) void AssetManagerBridge::Sysv_AAssetDir_rewind(AAssetDir *assetDir)
{
    if (assetDir)
        assetDir->current_index = 0;
}

__attribute__((sysv_abi)) void AssetManagerBridge::Sysv_AAssetDir_close(AAssetDir *assetDir)
{
    delete assetDir;
}

} // namespace portage::package
