#include "security/keystore_manager.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace portage::security {

namespace fs = std::filesystem;

KeyStoreManager &KeyStoreManager::Instance()
{
    static KeyStoreManager instance;
    return instance;
}

void KeyStoreManager::Initialize(const std::string &storage_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    storage_path_ = storage_path;
    if (!storage_path_.empty()) {
        std::error_code ec;
        fs::create_directories(fs::path(storage_path_).parent_path(), ec);
        LoadFromDisk();
    }
}

void KeyStoreManager::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    keys_.clear();
}

KeyStoreStatus KeyStoreManager::GenerateKey(const std::string &alias, KeyAlgorithm algorithm,
                                           size_t key_size_bits, uint32_t purposes,
                                           BlockMode block_mode, Padding padding)
{
    if (alias.empty()) {
        return KeyStoreStatus::ErrorInvalidArgument;
    }
    if (algorithm != KeyAlgorithm::Aes && algorithm != KeyAlgorithm::HmacSha256) {
        return KeyStoreStatus::ErrorUnsupportedAlgorithm;
    }

    KeyEntry entry;
    entry.metadata.alias = alias;
    entry.metadata.algorithm = algorithm;
    entry.metadata.key_size_bits = key_size_bits;
    entry.metadata.purposes = purposes;
    entry.metadata.block_mode = block_mode;
    entry.metadata.padding = padding;
    entry.metadata.created_timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    if (!CryptoEngine::Instance().GenerateAesKey(key_size_bits, entry.secret_key)) {
        return KeyStoreStatus::ErrorCryptoFailed;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        keys_[alias] = std::move(entry);
    }
    SaveToDisk();
    return KeyStoreStatus::Success;
}

KeyStoreStatus KeyStoreManager::GenerateKeyPair(const std::string &alias, KeyAlgorithm algorithm,
                                              size_t key_size_bits, uint32_t purposes,
                                              Digest digest, Padding padding)
{
    if (alias.empty()) {
        return KeyStoreStatus::ErrorInvalidArgument;
    }
    if (algorithm != KeyAlgorithm::Rsa && algorithm != KeyAlgorithm::Ec) {
        return KeyStoreStatus::ErrorUnsupportedAlgorithm;
    }

    KeyEntry entry;
    entry.metadata.alias = alias;
    entry.metadata.algorithm = algorithm;
    entry.metadata.key_size_bits = key_size_bits;
    entry.metadata.purposes = purposes;
    entry.metadata.digest = digest;
    entry.metadata.padding = padding;
    entry.metadata.created_timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    if (algorithm == KeyAlgorithm::Rsa) {
        if (!CryptoEngine::Instance().GenerateRsaKeyPair(key_size_bits, entry.public_key,
                                                         entry.private_key)) {
            return KeyStoreStatus::ErrorCryptoFailed;
        }
    } else {
        if (!CryptoEngine::Instance().GenerateEcKeyPair(entry.public_key, entry.private_key)) {
            return KeyStoreStatus::ErrorCryptoFailed;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        keys_[alias] = std::move(entry);
    }
    SaveToDisk();
    return KeyStoreStatus::Success;
}

bool KeyStoreManager::ContainsAlias(const std::string &alias) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return keys_.find(alias) != keys_.end();
}

const KeyEntry *KeyStoreManager::GetKey(const std::string &alias) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keys_.find(alias);
    if (it != keys_.end()) {
        return &it->second;
    }
    return nullptr;
}

KeyStoreStatus KeyStoreManager::DeleteKey(const std::string &alias)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(alias);
        if (it == keys_.end()) {
            return KeyStoreStatus::ErrorNotFound;
        }
        keys_.erase(it);
    }
    SaveToDisk();
    return KeyStoreStatus::Success;
}

std::vector<std::string> KeyStoreManager::ListAliases() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> aliases;
    aliases.reserve(keys_.size());
    for (const auto &pair : keys_) {
        aliases.push_back(pair.first);
    }
    return aliases;
}

CipherResult KeyStoreManager::Encrypt(const std::string &alias,
                                      const std::vector<uint8_t> &plaintext,
                                      const std::vector<uint8_t> &iv)
{
    CipherResult res;
    KeyEntry entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(alias);
        if (it == keys_.end()) {
            res.status = KeyStoreStatus::ErrorNotFound;
            return res;
        }
        entry = it->second;
    }

    if (!(entry.metadata.purposes & PurposeEncrypt)) {
        res.status = KeyStoreStatus::ErrorPermissionDenied;
        return res;
    }

    std::vector<uint8_t> actual_iv = iv;
    if (entry.metadata.block_mode == BlockMode::Gcm) {
        if (actual_iv.empty()) {
            actual_iv = CryptoEngine::Instance().GenerateRandomBytes(12);
        }
        res.iv = actual_iv;
        if (!CryptoEngine::Instance().EncryptAesGcm(entry.secret_key, actual_iv, plaintext,
                                                    res.tag, res.data)) {
            res.status = KeyStoreStatus::ErrorCryptoFailed;
            return res;
        }
    } else {
        if (actual_iv.empty()) {
            actual_iv = CryptoEngine::Instance().GenerateRandomBytes(16);
        }
        res.iv = actual_iv;
        if (!CryptoEngine::Instance().EncryptAesCbc(entry.secret_key, actual_iv, plaintext,
                                                    res.data)) {
            res.status = KeyStoreStatus::ErrorCryptoFailed;
            return res;
        }
    }

    res.status = KeyStoreStatus::Success;
    return res;
}

CipherResult KeyStoreManager::Decrypt(const std::string &alias,
                                      const std::vector<uint8_t> &ciphertext,
                                      const std::vector<uint8_t> &iv,
                                      const std::vector<uint8_t> &tag)
{
    CipherResult res;
    KeyEntry entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(alias);
        if (it == keys_.end()) {
            res.status = KeyStoreStatus::ErrorNotFound;
            return res;
        }
        entry = it->second;
    }

    if (!(entry.metadata.purposes & PurposeDecrypt)) {
        res.status = KeyStoreStatus::ErrorPermissionDenied;
        return res;
    }

    if (entry.metadata.block_mode == BlockMode::Gcm) {
        if (!CryptoEngine::Instance().DecryptAesGcm(entry.secret_key, iv, ciphertext, tag,
                                                    res.data)) {
            res.status = KeyStoreStatus::ErrorCryptoFailed;
            return res;
        }
    } else {
        if (!CryptoEngine::Instance().DecryptAesCbc(entry.secret_key, iv, ciphertext, res.data)) {
            res.status = KeyStoreStatus::ErrorCryptoFailed;
            return res;
        }
    }

    res.status = KeyStoreStatus::Success;
    return res;
}

KeyStoreStatus KeyStoreManager::Sign(const std::string &alias, const std::vector<uint8_t> &data,
                                    std::vector<uint8_t> &out_signature)
{
    KeyEntry entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(alias);
        if (it == keys_.end()) {
            return KeyStoreStatus::ErrorNotFound;
        }
        entry = it->second;
    }

    if (!(entry.metadata.purposes & PurposeSign)) {
        return KeyStoreStatus::ErrorPermissionDenied;
    }

    bool ok = false;
    if (entry.metadata.algorithm == KeyAlgorithm::Rsa) {
        ok = CryptoEngine::Instance().SignSha256WithRsa(entry.private_key, data, out_signature);
    } else if (entry.metadata.algorithm == KeyAlgorithm::Ec) {
        ok = CryptoEngine::Instance().SignSha256WithEcdsa(entry.private_key, data, out_signature);
    } else if (entry.metadata.algorithm == KeyAlgorithm::HmacSha256) {
        ok = CryptoEngine::Instance().HmacSha256(entry.secret_key.data(), entry.secret_key.size(),
                                                 data.data(), data.size(), out_signature);
    }

    return ok ? KeyStoreStatus::Success : KeyStoreStatus::ErrorCryptoFailed;
}

KeyStoreStatus KeyStoreManager::Verify(const std::string &alias, const std::vector<uint8_t> &data,
                                      const std::vector<uint8_t> &signature)
{
    KeyEntry entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(alias);
        if (it == keys_.end()) {
            return KeyStoreStatus::ErrorNotFound;
        }
        entry = it->second;
    }

    if (!(entry.metadata.purposes & PurposeVerify)) {
        return KeyStoreStatus::ErrorPermissionDenied;
    }

    bool ok = false;
    if (entry.metadata.algorithm == KeyAlgorithm::Rsa) {
        ok = CryptoEngine::Instance().VerifySha256WithRsa(entry.public_key, data, signature);
    } else if (entry.metadata.algorithm == KeyAlgorithm::Ec) {
        ok = CryptoEngine::Instance().VerifySha256WithEcdsa(entry.public_key, data, signature);
    } else if (entry.metadata.algorithm == KeyAlgorithm::HmacSha256) {
        std::vector<uint8_t> expected;
        ok = CryptoEngine::Instance().HmacSha256(entry.secret_key.data(), entry.secret_key.size(),
                                                 data.data(), data.size(), expected);
        ok = ok && (expected == signature);
    }

    return ok ? KeyStoreStatus::Success : KeyStoreStatus::ErrorCryptoFailed;
}

bool KeyStoreManager::SaveToDisk()
{
    if (storage_path_.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream ofs(storage_path_, std::ios::binary);
    if (!ofs.is_open()) {
        return false;
    }

    const char magic[4] = {'P', 'K', 'E', 'Y'};
    ofs.write(magic, 4);

    uint32_t count = static_cast<uint32_t>(keys_.size());
    ofs.write(reinterpret_cast<const char *>(&count), sizeof(count));

    for (const auto &pair : keys_) {
        const auto &e = pair.second;
        uint32_t alias_len = static_cast<uint32_t>(e.metadata.alias.size());
        ofs.write(reinterpret_cast<const char *>(&alias_len), sizeof(alias_len));
        ofs.write(e.metadata.alias.data(), alias_len);

        uint32_t alg = static_cast<uint32_t>(e.metadata.algorithm);
        uint32_t size = static_cast<uint32_t>(e.metadata.key_size_bits);
        uint32_t purp = e.metadata.purposes;
        uint32_t mode = static_cast<uint32_t>(e.metadata.block_mode);
        uint32_t pad = static_cast<uint32_t>(e.metadata.padding);
        uint32_t dig = static_cast<uint32_t>(e.metadata.digest);
        int64_t ts = e.metadata.created_timestamp_ns;

        ofs.write(reinterpret_cast<const char *>(&alg), sizeof(alg));
        ofs.write(reinterpret_cast<const char *>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char *>(&purp), sizeof(purp));
        ofs.write(reinterpret_cast<const char *>(&mode), sizeof(mode));
        ofs.write(reinterpret_cast<const char *>(&pad), sizeof(pad));
        ofs.write(reinterpret_cast<const char *>(&dig), sizeof(dig));
        ofs.write(reinterpret_cast<const char *>(&ts), sizeof(ts));

        auto write_blob = [&](const std::vector<uint8_t> &blob) {
            uint32_t len = static_cast<uint32_t>(blob.size());
            ofs.write(reinterpret_cast<const char *>(&len), sizeof(len));
            if (len > 0) {
                ofs.write(reinterpret_cast<const char *>(blob.data()), len);
            }
        };

        write_blob(e.secret_key);
        write_blob(e.public_key);
        write_blob(e.private_key);
    }
    return true;
}

bool KeyStoreManager::LoadFromDisk()
{
    if (storage_path_.empty() || !fs::exists(storage_path_)) {
        return false;
    }

    std::ifstream ifs(storage_path_, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }

    char magic[4];
    ifs.read(magic, 4);
    if (std::memcmp(magic, "PKEY", 4) != 0) {
        return false;
    }

    uint32_t count = 0;
    ifs.read(reinterpret_cast<char *>(&count), sizeof(count));

    keys_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        KeyEntry e;
        uint32_t alias_len = 0;
        ifs.read(reinterpret_cast<char *>(&alias_len), sizeof(alias_len));
        std::string alias(alias_len, '\0');
        ifs.read(alias.data(), alias_len);
        e.metadata.alias = alias;

        uint32_t alg = 0, size = 0, purp = 0, mode = 0, pad = 0, dig = 0;
        int64_t ts = 0;
        ifs.read(reinterpret_cast<char *>(&alg), sizeof(alg));
        ifs.read(reinterpret_cast<char *>(&size), sizeof(size));
        ifs.read(reinterpret_cast<char *>(&purp), sizeof(purp));
        ifs.read(reinterpret_cast<char *>(&mode), sizeof(mode));
        ifs.read(reinterpret_cast<char *>(&pad), sizeof(pad));
        ifs.read(reinterpret_cast<char *>(&dig), sizeof(dig));
        ifs.read(reinterpret_cast<char *>(&ts), sizeof(ts));

        e.metadata.algorithm = static_cast<KeyAlgorithm>(alg);
        e.metadata.key_size_bits = size;
        e.metadata.purposes = purp;
        e.metadata.block_mode = static_cast<BlockMode>(mode);
        e.metadata.padding = static_cast<Padding>(pad);
        e.metadata.digest = static_cast<Digest>(dig);
        e.metadata.created_timestamp_ns = ts;

        auto read_blob = [&](std::vector<uint8_t> &blob) {
            uint32_t len = 0;
            ifs.read(reinterpret_cast<char *>(&len), sizeof(len));
            blob.resize(len);
            if (len > 0) {
                ifs.read(reinterpret_cast<char *>(blob.data()), len);
            }
        };

        read_blob(e.secret_key);
        read_blob(e.public_key);
        read_blob(e.private_key);

        keys_[alias] = std::move(e);
    }
    return true;
}

} // namespace portage::security
