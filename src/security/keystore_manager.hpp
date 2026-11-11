#pragma once

#include "security/crypto_engine.hpp"
#include "security/keystore_types.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace portage::security {

class KeyStoreManager {
  public:
    static KeyStoreManager &Instance();

    void Initialize(const std::string &storage_path = "");
    void Reset();

    KeyStoreStatus GenerateKey(const std::string &alias, KeyAlgorithm algorithm,
                               size_t key_size_bits, uint32_t purposes, BlockMode block_mode,
                               Padding padding);
    KeyStoreStatus GenerateKeyPair(const std::string &alias, KeyAlgorithm algorithm,
                                   size_t key_size_bits, uint32_t purposes, Digest digest,
                                   Padding padding);

    bool ContainsAlias(const std::string &alias) const;
    const KeyEntry *GetKey(const std::string &alias) const;
    KeyStoreStatus DeleteKey(const std::string &alias);
    std::vector<std::string> ListAliases() const;

    CipherResult Encrypt(const std::string &alias, const std::vector<uint8_t> &plaintext,
                         const std::vector<uint8_t> &iv = {});
    CipherResult Decrypt(const std::string &alias, const std::vector<uint8_t> &ciphertext,
                         const std::vector<uint8_t> &iv, const std::vector<uint8_t> &tag = {});

    KeyStoreStatus Sign(const std::string &alias, const std::vector<uint8_t> &data,
                        std::vector<uint8_t> &out_signature);
    KeyStoreStatus Verify(const std::string &alias, const std::vector<uint8_t> &data,
                          const std::vector<uint8_t> &signature);

    bool SaveToDisk();
    bool LoadFromDisk();

  private:
    KeyStoreManager() = default;
    ~KeyStoreManager() = default;
    KeyStoreManager(const KeyStoreManager &) = delete;
    KeyStoreManager &operator=(const KeyStoreManager &) = delete;

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, KeyEntry> keys_{};
    std::string storage_path_{};
};

} // namespace portage::security
