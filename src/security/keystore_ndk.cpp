#include "security/keystore_ndk.hpp"
#include "security/keystore_manager.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

using namespace portage::security;

struct AKeyStore {
    KeyStoreManager *mgr{nullptr};
};

extern "C" {

__attribute__((sysv_abi)) AKeyStore *AKeyStore_create()
{
    auto *store = new (std::nothrow) AKeyStore();
    if (store) {
        store->mgr = &KeyStoreManager::Instance();
    }
    return store;
}

__attribute__((sysv_abi)) void AKeyStore_delete(AKeyStore *store)
{
    delete store;
}

__attribute__((sysv_abi)) int32_t AKeyStore_containsAlias(AKeyStore *store, const char *alias)
{
    if (!store || !store->mgr || !alias) {
        return 0;
    }
    return store->mgr->ContainsAlias(alias) ? 1 : 0;
}

__attribute__((sysv_abi)) int32_t AKeyStore_deleteKey(AKeyStore *store, const char *alias)
{
    if (!store || !store->mgr || !alias) {
        return -1;
    }
    return (store->mgr->DeleteKey(alias) == KeyStoreStatus::Success) ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AKeyStore_listAliases(AKeyStore *store,
                                                         AKeyStore_AliasList **outList)
{
    if (!store || !store->mgr || !outList) {
        return -1;
    }
    *outList = nullptr;

    auto aliases = store->mgr->ListAliases();
    auto *list = new (std::nothrow) AKeyStore_AliasList();
    if (!list) {
        return -1;
    }

    list->numAliases = static_cast<int>(aliases.size());
    list->aliases = new (std::nothrow) const char *[aliases.size()];
    if (!list->aliases) {
        delete list;
        return -1;
    }

    for (size_t i = 0; i < aliases.size(); ++i) {
        size_t len = aliases[i].size();
        char *str = new (std::nothrow) char[len + 1];
        if (str) {
            std::memcpy(str, aliases[i].c_str(), len + 1);
        }
        list->aliases[i] = str;
    }

    *outList = list;
    return 0;
}

__attribute__((sysv_abi)) void AKeyStore_deleteAliasList(AKeyStore_AliasList *list)
{
    if (!list) {
        return;
    }
    if (list->aliases) {
        for (int i = 0; i < list->numAliases; ++i) {
            delete[] list->aliases[i];
        }
        delete[] list->aliases;
    }
    delete list;
}

__attribute__((sysv_abi)) int32_t AKeyStore_generateAesKey(AKeyStore *store, const char *alias,
                                                            int32_t keyBits, int32_t blockMode,
                                                            int32_t padding)
{
    if (!store || !store->mgr || !alias) {
        return -1;
    }

    BlockMode bm = (blockMode == 2) ? BlockMode::Gcm : BlockMode::Cbc;
    Padding pad = (padding == 1) ? Padding::None : Padding::Pkcs7;
    uint32_t purposes = PurposeEncrypt | PurposeDecrypt;

    auto status = store->mgr->GenerateKey(alias, KeyAlgorithm::Aes, static_cast<size_t>(keyBits),
                                          purposes, bm, pad);
    return (status == KeyStoreStatus::Success) ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AKeyStore_generateRsaKeyPair(AKeyStore *store, const char *alias,
                                                               int32_t keyBits, int32_t padding)
{
    if (!store || !store->mgr || !alias) {
        return -1;
    }

    Padding pad = (padding == 2) ? Padding::Oaep : Padding::Pkcs1;
    uint32_t purposes = PurposeEncrypt | PurposeDecrypt | PurposeSign | PurposeVerify;

    auto status = store->mgr->GenerateKeyPair(alias, KeyAlgorithm::Rsa,
                                              static_cast<size_t>(keyBits), purposes,
                                              Digest::Sha256, pad);
    return (status == KeyStoreStatus::Success) ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AKeyStore_generateEcKeyPair(AKeyStore *store, const char *alias)
{
    if (!store || !store->mgr || !alias) {
        return -1;
    }

    uint32_t purposes = PurposeSign | PurposeVerify;
    auto status = store->mgr->GenerateKeyPair(alias, KeyAlgorithm::Ec, 256, purposes,
                                              Digest::Sha256, Padding::None);
    return (status == KeyStoreStatus::Success) ? 0 : -1;
}

__attribute__((sysv_abi)) int32_t AKeyStore_encrypt(AKeyStore *store, const char *alias,
                                                    const uint8_t *plaintext, size_t plainLen,
                                                    uint8_t **outCiphertext, size_t *outCipherLen,
                                                    uint8_t **outIv, size_t *outIvLen,
                                                    uint8_t **outTag, size_t *outTagLen)
{
    if (!store || !store->mgr || !alias || !outCiphertext || !outCipherLen) {
        return -1;
    }

    std::vector<uint8_t> plain(plaintext, plaintext + plainLen);
    auto res = store->mgr->Encrypt(alias, plain);
    if (res.status != KeyStoreStatus::Success) {
        return -1;
    }

    *outCiphertext = static_cast<uint8_t *>(std::malloc(res.data.size()));
    if (*outCiphertext && !res.data.empty()) {
        std::memcpy(*outCiphertext, res.data.data(), res.data.size());
    }
    *outCipherLen = res.data.size();

    if (outIv && outIvLen) {
        *outIv = static_cast<uint8_t *>(std::malloc(res.iv.size()));
        if (*outIv && !res.iv.empty()) {
            std::memcpy(*outIv, res.iv.data(), res.iv.size());
        }
        *outIvLen = res.iv.size();
    }

    if (outTag && outTagLen) {
        *outTag = static_cast<uint8_t *>(std::malloc(res.tag.size()));
        if (*outTag && !res.tag.empty()) {
            std::memcpy(*outTag, res.tag.data(), res.tag.size());
        }
        *outTagLen = res.tag.size();
    }

    return 0;
}

__attribute__((sysv_abi)) int32_t AKeyStore_decrypt(AKeyStore *store, const char *alias,
                                                    const uint8_t *ciphertext, size_t cipherLen,
                                                    const uint8_t *iv, size_t ivLen,
                                                    const uint8_t *tag, size_t tagLen,
                                                    uint8_t **outPlaintext, size_t *outPlainLen)
{
    if (!store || !store->mgr || !alias || !outPlaintext || !outPlainLen) {
        return -1;
    }

    std::vector<uint8_t> cipher(ciphertext, ciphertext + cipherLen);
    std::vector<uint8_t> iv_vec(iv, iv + ivLen);
    std::vector<uint8_t> tag_vec(tag, tag + tagLen);

    auto res = store->mgr->Decrypt(alias, cipher, iv_vec, tag_vec);
    if (res.status != KeyStoreStatus::Success) {
        return -1;
    }

    *outPlaintext = static_cast<uint8_t *>(std::malloc(res.data.size()));
    if (*outPlaintext && !res.data.empty()) {
        std::memcpy(*outPlaintext, res.data.data(), res.data.size());
    }
    *outPlainLen = res.data.size();
    return 0;
}

__attribute__((sysv_abi)) int32_t AKeyStore_sign(AKeyStore *store, const char *alias,
                                                 const uint8_t *data, size_t dataLen,
                                                 uint8_t **outSig, size_t *outSigLen)
{
    if (!store || !store->mgr || !alias || !outSig || !outSigLen) {
        return -1;
    }

    std::vector<uint8_t> input(data, data + dataLen);
    std::vector<uint8_t> sig;
    auto status = store->mgr->Sign(alias, input, sig);
    if (status != KeyStoreStatus::Success) {
        return -1;
    }

    *outSig = static_cast<uint8_t *>(std::malloc(sig.size()));
    if (*outSig && !sig.empty()) {
        std::memcpy(*outSig, sig.data(), sig.size());
    }
    *outSigLen = sig.size();
    return 0;
}

__attribute__((sysv_abi)) int32_t AKeyStore_verify(AKeyStore *store, const char *alias,
                                                   const uint8_t *data, size_t dataLen,
                                                   const uint8_t *sig, size_t sigLen)
{
    if (!store || !store->mgr || !alias || !data || !sig) {
        return -1;
    }

    std::vector<uint8_t> input(data, data + dataLen);
    std::vector<uint8_t> signature(sig, sig + sigLen);
    auto status = store->mgr->Verify(alias, input, signature);
    return (status == KeyStoreStatus::Success) ? 0 : -1;
}

__attribute__((sysv_abi)) void AKeyStore_freeBuffer(void *buf)
{
    std::free(buf);
}

} // extern "C"
