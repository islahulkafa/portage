#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct AKeyStore;
typedef struct AKeyStore AKeyStore;

typedef struct AKeyStore_AliasList {
    int numAliases;
    const char **aliases;
} AKeyStore_AliasList;

__attribute__((sysv_abi)) AKeyStore *AKeyStore_create();
__attribute__((sysv_abi)) void AKeyStore_delete(AKeyStore *store);

__attribute__((sysv_abi)) int32_t AKeyStore_containsAlias(AKeyStore *store, const char *alias);
__attribute__((sysv_abi)) int32_t AKeyStore_deleteKey(AKeyStore *store, const char *alias);
__attribute__((sysv_abi)) int32_t AKeyStore_listAliases(AKeyStore *store,
                                                         AKeyStore_AliasList **outList);
__attribute__((sysv_abi)) void AKeyStore_deleteAliasList(AKeyStore_AliasList *list);

__attribute__((sysv_abi)) int32_t AKeyStore_generateAesKey(AKeyStore *store, const char *alias,
                                                            int32_t keyBits, int32_t blockMode,
                                                            int32_t padding);
__attribute__((sysv_abi)) int32_t AKeyStore_generateRsaKeyPair(AKeyStore *store, const char *alias,
                                                               int32_t keyBits, int32_t padding);
__attribute__((sysv_abi)) int32_t AKeyStore_generateEcKeyPair(AKeyStore *store, const char *alias);

__attribute__((sysv_abi)) int32_t AKeyStore_encrypt(AKeyStore *store, const char *alias,
                                                    const uint8_t *plaintext, size_t plainLen,
                                                    uint8_t **outCiphertext, size_t *outCipherLen,
                                                    uint8_t **outIv, size_t *outIvLen,
                                                    uint8_t **outTag, size_t *outTagLen);
__attribute__((sysv_abi)) int32_t AKeyStore_decrypt(AKeyStore *store, const char *alias,
                                                    const uint8_t *ciphertext, size_t cipherLen,
                                                    const uint8_t *iv, size_t ivLen,
                                                    const uint8_t *tag, size_t tagLen,
                                                    uint8_t **outPlaintext, size_t *outPlainLen);

__attribute__((sysv_abi)) int32_t AKeyStore_sign(AKeyStore *store, const char *alias,
                                                 const uint8_t *data, size_t dataLen,
                                                 uint8_t **outSig, size_t *outSigLen);
__attribute__((sysv_abi)) int32_t AKeyStore_verify(AKeyStore *store, const char *alias,
                                                   const uint8_t *data, size_t dataLen,
                                                   const uint8_t *sig, size_t sigLen);
__attribute__((sysv_abi)) void AKeyStore_freeBuffer(void *buf);

} // extern "C"
