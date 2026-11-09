#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace portage::security {

enum class KeyAlgorithm {
    Unknown = 0,
    Aes,
    HmacSha256,
    Rsa,
    Ec,
};

enum class BlockMode {
    None = 0,
    Cbc,
    Gcm,
    Ctr,
    Ecb,
};

enum class Padding {
    None = 0,
    Pkcs7,
    Pkcs1,
    Oaep,
};

enum KeyPurpose : uint32_t {
    PurposeEncrypt = 1 << 0,
    PurposeDecrypt = 1 << 1,
    PurposeSign = 1 << 2,
    PurposeVerify = 1 << 3,
};

enum class Digest {
    None = 0,
    Sha256,
    Sha384,
    Sha512,
};

enum class KeyStoreStatus {
    Success = 0,
    ErrorNotFound = -1,
    ErrorInvalidArgument = -2,
    ErrorCryptoFailed = -3,
    ErrorPermissionDenied = -4,
    ErrorUnsupportedAlgorithm = -5,
};

struct KeyMetadata {
    std::string alias{};
    KeyAlgorithm algorithm{KeyAlgorithm::Unknown};
    size_t key_size_bits{0};
    uint32_t purposes{0};
    BlockMode block_mode{BlockMode::None};
    Padding padding{Padding::None};
    Digest digest{Digest::None};
    int64_t created_timestamp_ns{0};
};

struct KeyEntry {
    KeyMetadata metadata{};
    std::vector<uint8_t> secret_key{};
    std::vector<uint8_t> public_key{};
    std::vector<uint8_t> private_key{};
};

struct CipherResult {
    KeyStoreStatus status{KeyStoreStatus::Success};
    std::vector<uint8_t> data{};
    std::vector<uint8_t> iv{};
    std::vector<uint8_t> tag{};
};

} // namespace portage::security
