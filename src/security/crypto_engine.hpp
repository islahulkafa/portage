#pragma once

#include "security/keystore_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace portage::security {

class CryptoEngine {
public:
    static CryptoEngine &Instance();

    std::vector<uint8_t> GenerateRandomBytes(size_t length);

    bool Sha256(const uint8_t *data, size_t length, std::vector<uint8_t> &out_hash);
    bool HmacSha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                    std::vector<uint8_t> &out_mac);

    bool GenerateAesKey(size_t key_bits, std::vector<uint8_t> &out_key);
    bool EncryptAesGcm(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                       const std::vector<uint8_t> &plaintext, std::vector<uint8_t> &out_tag,
                       std::vector<uint8_t> &out_ciphertext);
    bool DecryptAesGcm(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                       const std::vector<uint8_t> &ciphertext, const std::vector<uint8_t> &tag,
                       std::vector<uint8_t> &out_plaintext);

    bool EncryptAesCbc(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                       const std::vector<uint8_t> &plaintext, std::vector<uint8_t> &out_ciphertext);
    bool DecryptAesCbc(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                       const std::vector<uint8_t> &ciphertext,
                       std::vector<uint8_t> &out_plaintext);

    bool GenerateRsaKeyPair(size_t key_bits, std::vector<uint8_t> &out_pub,
                            std::vector<uint8_t> &out_priv);
    bool SignSha256WithRsa(const std::vector<uint8_t> &priv_key, const std::vector<uint8_t> &data,
                           std::vector<uint8_t> &out_signature);
    bool VerifySha256WithRsa(const std::vector<uint8_t> &pub_key, const std::vector<uint8_t> &data,
                             const std::vector<uint8_t> &signature);

    bool GenerateEcKeyPair(std::vector<uint8_t> &out_pub, std::vector<uint8_t> &out_priv);
    bool SignSha256WithEcdsa(const std::vector<uint8_t> &priv_key,
                             const std::vector<uint8_t> &data,
                             std::vector<uint8_t> &out_signature);
    bool VerifySha256WithEcdsa(const std::vector<uint8_t> &pub_key,
                               const std::vector<uint8_t> &data,
                               const std::vector<uint8_t> &signature);

private:
    CryptoEngine() = default;
    ~CryptoEngine() = default;
    CryptoEngine(const CryptoEngine &) = delete;
    CryptoEngine &operator=(const CryptoEngine &) = delete;
};

} // namespace portage::security
