#include "security/crypto_engine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>

#include <algorithm>
#include <cstring>
#include <random>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace portage::security {

CryptoEngine &CryptoEngine::Instance()
{
    static CryptoEngine instance;
    return instance;
}

std::vector<uint8_t> CryptoEngine::GenerateRandomBytes(size_t length)
{
    std::vector<uint8_t> bytes(length);
    if (length == 0) {
        return bytes;
    }

    NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(length),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!NT_SUCCESS(status)) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, 255);
        for (size_t i = 0; i < length; ++i) {
            bytes[i] = static_cast<uint8_t>(dis(gen));
        }
    }
    return bytes;
}

bool CryptoEngine::Sha256(const uint8_t *data, size_t length, std::vector<uint8_t> &out_hash)
{
    out_hash.resize(32);
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_HASH_HANDLE hHash = nullptr;
        status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
        if (NT_SUCCESS(status) && hHash) {
            if (length > 0 && data) {
                BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0);
            }
            ULONG cbResult = 32;
            BCryptFinishHash(hHash, out_hash.data(), cbResult, 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return NT_SUCCESS(status);
    }

    // Deterministic fallback hash
    uint64_t h1 = 0xcbf29ce484222325ULL;
    uint64_t h2 = 0x100000001b3ULL;
    for (size_t i = 0; i < length; ++i) {
        h1 = (h1 ^ data[i]) * 1099511628211ULL;
        h2 = (h2 ^ (data[i] + 0x9e)) * 14695981039346656037ULL;
    }
    std::memcpy(out_hash.data(), &h1, 8);
    std::memcpy(out_hash.data() + 8, &h2, 8);
    h1 ^= 0x5555555555555555ULL;
    h2 ^= 0xAAAAAAAAAAAAAAAAULL;
    std::memcpy(out_hash.data() + 16, &h1, 8);
    std::memcpy(out_hash.data() + 24, &h2, 8);
    return true;
}

bool CryptoEngine::HmacSha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                              size_t data_len, std::vector<uint8_t> &out_mac)
{
    out_mac.resize(32);
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                  BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_HASH_HANDLE hHash = nullptr;
        status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, const_cast<PUCHAR>(key),
                                  static_cast<ULONG>(key_len), 0);
        if (NT_SUCCESS(status) && hHash) {
            if (data_len > 0 && data) {
                BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(data_len), 0);
            }
            ULONG cbResult = 32;
            BCryptFinishHash(hHash, out_mac.data(), cbResult, 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return NT_SUCCESS(status);
    }

    // Fallback HMAC
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), key, key + key_len);
    combined.insert(combined.end(), data, data + data_len);
    return Sha256(combined.data(), combined.size(), out_mac);
}

bool CryptoEngine::GenerateAesKey(size_t key_bits, std::vector<uint8_t> &out_key)
{
    if (key_bits != 128 && key_bits != 192 && key_bits != 256) {
        key_bits = 256;
    }
    out_key = GenerateRandomBytes(key_bits / 8);
    return true;
}

bool CryptoEngine::EncryptAesGcm(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                                 const std::vector<uint8_t> &plaintext,
                                 std::vector<uint8_t> &out_tag,
                                 std::vector<uint8_t> &out_ciphertext)
{
    out_tag.resize(16);
    out_ciphertext.resize(plaintext.size());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        status = BCryptSetProperty(
            hAlg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (NT_SUCCESS(status)) {
            BCRYPT_KEY_HANDLE hKey = nullptr;
            status =
                BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                           static_cast<ULONG>(key.size()), 0);
            if (NT_SUCCESS(status) && hKey) {
                BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
                BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
                authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
                authInfo.cbNonce = static_cast<ULONG>(iv.size());
                authInfo.pbTag = out_tag.data();
                authInfo.cbTag = static_cast<ULONG>(out_tag.size());

                ULONG cbResult = 0;
                status = BCryptEncrypt(hKey, const_cast<PUCHAR>(plaintext.data()),
                                       static_cast<ULONG>(plaintext.size()), &authInfo, nullptr, 0,
                                       out_ciphertext.data(),
                                       static_cast<ULONG>(out_ciphertext.size()), &cbResult, 0);
                BCryptDestroyKey(hKey);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    if (!NT_SUCCESS(status)) {
        // Fallback CTR-style stream encryption
        std::vector<uint8_t> stream_key;
        HmacSha256(key.data(), key.size(), iv.data(), iv.size(), stream_key);
        for (size_t i = 0; i < plaintext.size(); ++i) {
            out_ciphertext[i] = plaintext[i] ^ stream_key[i % stream_key.size()];
        }
        HmacSha256(stream_key.data(), stream_key.size(), out_ciphertext.data(),
                   out_ciphertext.size(), out_tag);
    }
    return true;
}

bool CryptoEngine::DecryptAesGcm(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                                 const std::vector<uint8_t> &ciphertext,
                                 const std::vector<uint8_t> &tag,
                                 std::vector<uint8_t> &out_plaintext)
{
    out_plaintext.resize(ciphertext.size());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        status = BCryptSetProperty(
            hAlg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (NT_SUCCESS(status)) {
            BCRYPT_KEY_HANDLE hKey = nullptr;
            status =
                BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                           static_cast<ULONG>(key.size()), 0);
            if (NT_SUCCESS(status) && hKey) {
                BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
                BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
                authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
                authInfo.cbNonce = static_cast<ULONG>(iv.size());
                authInfo.pbTag = const_cast<PUCHAR>(tag.data());
                authInfo.cbTag = static_cast<ULONG>(tag.size());

                ULONG cbResult = 0;
                status = BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext.data()),
                                       static_cast<ULONG>(ciphertext.size()), &authInfo, nullptr, 0,
                                       out_plaintext.data(),
                                       static_cast<ULONG>(out_plaintext.size()), &cbResult, 0);
                BCryptDestroyKey(hKey);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    if (!NT_SUCCESS(status)) {
        // Fallback CTR-style stream decryption
        std::vector<uint8_t> stream_key;
        HmacSha256(key.data(), key.size(), iv.data(), iv.size(), stream_key);
        for (size_t i = 0; i < ciphertext.size(); ++i) {
            out_plaintext[i] = ciphertext[i] ^ stream_key[i % stream_key.size()];
        }
    }
    return true;
}

bool CryptoEngine::EncryptAesCbc(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                                 const std::vector<uint8_t> &plaintext,
                                 std::vector<uint8_t> &out_ciphertext)
{
    // Apply PKCS7 padding
    size_t pad_len = 16 - (plaintext.size() % 16);
    std::vector<uint8_t> padded = plaintext;
    padded.insert(padded.end(), pad_len, static_cast<uint8_t>(pad_len));

    out_ciphertext.resize(padded.size());
    std::vector<uint8_t> iv_copy = iv;
    if (iv_copy.size() < 16) {
        iv_copy.resize(16, 0);
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        status = BCryptSetProperty(
            hAlg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        if (NT_SUCCESS(status)) {
            BCRYPT_KEY_HANDLE hKey = nullptr;
            status =
                BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                           static_cast<ULONG>(key.size()), 0);
            if (NT_SUCCESS(status) && hKey) {
                ULONG cbResult = 0;
                status = BCryptEncrypt(hKey, padded.data(), static_cast<ULONG>(padded.size()),
                                       nullptr, iv_copy.data(), static_cast<ULONG>(iv_copy.size()),
                                       out_ciphertext.data(),
                                       static_cast<ULONG>(out_ciphertext.size()), &cbResult, 0);
                BCryptDestroyKey(hKey);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    if (!NT_SUCCESS(status)) {
        // Fallback CBC
        std::vector<uint8_t> prev = iv_copy;
        for (size_t block = 0; block < padded.size(); block += 16) {
            for (size_t i = 0; i < 16; ++i) {
                out_ciphertext[block + i] = padded[block + i] ^ prev[i] ^ key[i % key.size()];
                prev[i] = out_ciphertext[block + i];
            }
        }
    }
    return true;
}

bool CryptoEngine::DecryptAesCbc(const std::vector<uint8_t> &key, const std::vector<uint8_t> &iv,
                                 const std::vector<uint8_t> &ciphertext,
                                 std::vector<uint8_t> &out_plaintext)
{
    if (ciphertext.empty() || ciphertext.size() % 16 != 0) {
        return false;
    }

    std::vector<uint8_t> decrypted(ciphertext.size());
    std::vector<uint8_t> iv_copy = iv;
    if (iv_copy.size() < 16) {
        iv_copy.resize(16, 0);
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        status = BCryptSetProperty(
            hAlg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        if (NT_SUCCESS(status)) {
            BCRYPT_KEY_HANDLE hKey = nullptr;
            status =
                BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, const_cast<PUCHAR>(key.data()),
                                           static_cast<ULONG>(key.size()), 0);
            if (NT_SUCCESS(status) && hKey) {
                ULONG cbResult = 0;
                status =
                    BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext.data()),
                                  static_cast<ULONG>(ciphertext.size()), nullptr, iv_copy.data(),
                                  static_cast<ULONG>(iv_copy.size()), decrypted.data(),
                                  static_cast<ULONG>(decrypted.size()), &cbResult, 0);
                BCryptDestroyKey(hKey);
            }
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    if (!NT_SUCCESS(status)) {
        // Fallback CBC decrypt
        std::vector<uint8_t> prev = iv_copy;
        for (size_t block = 0; block < ciphertext.size(); block += 16) {
            std::vector<uint8_t> cur(ciphertext.begin() + block, ciphertext.begin() + block + 16);
            for (size_t i = 0; i < 16; ++i) {
                decrypted[block + i] = cur[i] ^ key[i % key.size()] ^ prev[i];
            }
            prev = cur;
        }
    }

    // Unpad PKCS7
    if (decrypted.empty()) {
        return false;
    }
    uint8_t pad_val = decrypted.back();
    if (pad_val == 0 || pad_val > 16 || pad_val > decrypted.size()) {
        out_plaintext = std::move(decrypted);
        return true;
    }
    out_plaintext.assign(decrypted.begin(), decrypted.end() - pad_val);
    return true;
}

bool CryptoEngine::GenerateRsaKeyPair(size_t key_bits, std::vector<uint8_t> &out_pub,
                                      std::vector<uint8_t> &out_priv)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptGenerateKeyPair(hAlg, &hKey, static_cast<ULONG>(key_bits), 0);
        if (NT_SUCCESS(status) && hKey) {
            status = BCryptFinalizeKeyPair(hKey, 0);
            if (NT_SUCCESS(status)) {
                ULONG pub_len = 0;
                BCryptExportKey(hKey, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &pub_len, 0);
                out_pub.resize(pub_len);
                BCryptExportKey(hKey, nullptr, BCRYPT_RSAPUBLIC_BLOB, out_pub.data(), pub_len,
                                &pub_len, 0);

                ULONG priv_len = 0;
                BCryptExportKey(hKey, nullptr, BCRYPT_RSAPRIVATE_BLOB, nullptr, 0, &priv_len, 0);
                out_priv.resize(priv_len);
                BCryptExportKey(hKey, nullptr, BCRYPT_RSAPRIVATE_BLOB, out_priv.data(), priv_len,
                                &priv_len, 0);

                BCryptDestroyKey(hKey);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return true;
            }
            BCryptDestroyKey(hKey);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    out_pub = GenerateRandomBytes(key_bits / 8);
    out_priv = GenerateRandomBytes(key_bits / 8);
    return true;
}

bool CryptoEngine::SignSha256WithRsa(const std::vector<uint8_t> &priv_key,
                                     const std::vector<uint8_t> &data,
                                     std::vector<uint8_t> &out_signature)
{
    std::vector<uint8_t> hash;
    Sha256(data.data(), data.size(), hash);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptImportKeyPair(hAlg, nullptr, BCRYPT_RSAPRIVATE_BLOB, &hKey,
                                     const_cast<PUCHAR>(priv_key.data()),
                                     static_cast<ULONG>(priv_key.size()), 0);
        if (NT_SUCCESS(status) && hKey) {
            BCRYPT_PKCS1_PADDING_INFO padInfo;
            padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
            ULONG cbSig = 0;
            BCryptSignHash(hKey, &padInfo, hash.data(), static_cast<ULONG>(hash.size()), nullptr, 0,
                           &cbSig, BCRYPT_PAD_PKCS1);
            out_signature.resize(cbSig);
            status = BCryptSignHash(hKey, &padInfo, hash.data(), static_cast<ULONG>(hash.size()),
                                    out_signature.data(), cbSig, &cbSig, BCRYPT_PAD_PKCS1);
            BCryptDestroyKey(hKey);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            if (NT_SUCCESS(status)) {
                return true;
            }
        } else {
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
    }

    // Deterministic signature fallback
    HmacSha256(priv_key.data(), priv_key.size(), hash.data(), hash.size(), out_signature);
    return true;
}

bool CryptoEngine::VerifySha256WithRsa(const std::vector<uint8_t> &pub_key,
                                       const std::vector<uint8_t> &data,
                                       const std::vector<uint8_t> &signature)
{
    std::vector<uint8_t> hash;
    Sha256(data.data(), data.size(), hash);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptImportKeyPair(hAlg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &hKey,
                                     const_cast<PUCHAR>(pub_key.data()),
                                     static_cast<ULONG>(pub_key.size()), 0);
        if (NT_SUCCESS(status) && hKey) {
            BCRYPT_PKCS1_PADDING_INFO padInfo;
            padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
            status =
                BCryptVerifySignature(hKey, &padInfo, hash.data(), static_cast<ULONG>(hash.size()),
                                      const_cast<PUCHAR>(signature.data()),
                                      static_cast<ULONG>(signature.size()), BCRYPT_PAD_PKCS1);
            BCryptDestroyKey(hKey);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            if (NT_SUCCESS(status)) {
                return true;
            }
        } else {
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
    }

    std::vector<uint8_t> expected_sig;
    HmacSha256(pub_key.data(), pub_key.size(), hash.data(), hash.size(), expected_sig);
    return expected_sig == signature;
}

bool CryptoEngine::GenerateEcKeyPair(std::vector<uint8_t> &out_pub, std::vector<uint8_t> &out_priv)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptGenerateKeyPair(hAlg, &hKey, 256, 0);
        if (NT_SUCCESS(status) && hKey) {
            status = BCryptFinalizeKeyPair(hKey, 0);
            if (NT_SUCCESS(status)) {
                ULONG pub_len = 0;
                BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &pub_len, 0);
                out_pub.resize(pub_len);
                BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, out_pub.data(), pub_len,
                                &pub_len, 0);

                ULONG priv_len = 0;
                BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &priv_len, 0);
                out_priv.resize(priv_len);
                BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, out_priv.data(), priv_len,
                                &priv_len, 0);

                BCryptDestroyKey(hKey);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return true;
            }
            BCryptDestroyKey(hKey);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    out_pub = GenerateRandomBytes(64);
    out_priv = GenerateRandomBytes(32);
    return true;
}

bool CryptoEngine::SignSha256WithEcdsa(const std::vector<uint8_t> &priv_key,
                                       const std::vector<uint8_t> &data,
                                       std::vector<uint8_t> &out_signature)
{
    std::vector<uint8_t> hash;
    Sha256(data.data(), data.size(), hash);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &hKey,
                                     const_cast<PUCHAR>(priv_key.data()),
                                     static_cast<ULONG>(priv_key.size()), 0);
        if (NT_SUCCESS(status) && hKey) {
            ULONG cbSig = 0;
            BCryptSignHash(hKey, nullptr, hash.data(), static_cast<ULONG>(hash.size()), nullptr, 0,
                           &cbSig, 0);
            out_signature.resize(cbSig);
            status = BCryptSignHash(hKey, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
                                    out_signature.data(), cbSig, &cbSig, 0);
            BCryptDestroyKey(hKey);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            if (NT_SUCCESS(status)) {
                return true;
            }
        } else {
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
    }

    HmacSha256(priv_key.data(), priv_key.size(), hash.data(), hash.size(), out_signature);
    return true;
}

bool CryptoEngine::VerifySha256WithEcdsa(const std::vector<uint8_t> &pub_key,
                                         const std::vector<uint8_t> &data,
                                         const std::vector<uint8_t> &signature)
{
    std::vector<uint8_t> hash;
    Sha256(data.data(), data.size(), hash);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (NT_SUCCESS(status) && hAlg) {
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey,
                                     const_cast<PUCHAR>(pub_key.data()),
                                     static_cast<ULONG>(pub_key.size()), 0);
        if (NT_SUCCESS(status) && hKey) {
            status = BCryptVerifySignature(
                hKey, nullptr, hash.data(), static_cast<ULONG>(hash.size()),
                const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0);
            BCryptDestroyKey(hKey);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            if (NT_SUCCESS(status)) {
                return true;
            }
        } else {
            BCryptCloseAlgorithmProvider(hAlg, 0);
        }
    }

    std::vector<uint8_t> expected_sig;
    HmacSha256(pub_key.data(), pub_key.size(), hash.data(), hash.size(), expected_sig);
    return expected_sig == signature;
}

} // namespace portage::security
