#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AKeyStore;
typedef struct AKeyStore AKeyStore;

typedef struct AKeyStore_AliasList {
    int numAliases;
    const char **aliases;
} AKeyStore_AliasList;

__attribute__((visibility("default"))) AKeyStore *AKeyStore_create(void) { return (AKeyStore *)0; }

__attribute__((visibility("default"))) void AKeyStore_delete(AKeyStore *store) { (void)store; }

__attribute__((visibility("default"))) int32_t AKeyStore_containsAlias(AKeyStore *store,
                                                                       const char *alias)
{
    (void)store;
    (void)alias;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_deleteKey(AKeyStore *store,
                                                                   const char *alias)
{
    (void)store;
    (void)alias;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_listAliases(AKeyStore *store,
                                                                     AKeyStore_AliasList **outList)
{
    (void)store;
    (void)outList;
    return 0;
}

__attribute__((visibility("default"))) void AKeyStore_deleteAliasList(AKeyStore_AliasList *list)
{
    (void)list;
}

__attribute__((visibility("default"))) int32_t AKeyStore_generateAesKey(
    AKeyStore *store, const char *alias, int32_t keyBits, int32_t blockMode, int32_t padding)
{
    (void)store;
    (void)alias;
    (void)keyBits;
    (void)blockMode;
    (void)padding;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_generateRsaKeyPair(AKeyStore *store,
                                                                            const char *alias,
                                                                            int32_t keyBits,
                                                                            int32_t padding)
{
    (void)store;
    (void)alias;
    (void)keyBits;
    (void)padding;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_generateEcKeyPair(AKeyStore *store,
                                                                           const char *alias)
{
    (void)store;
    (void)alias;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AKeyStore_encrypt(AKeyStore *store, const char *alias, const uint8_t *plaintext, size_t plainLen,
                  uint8_t **outCiphertext, size_t *outCipherLen, uint8_t **outIv, size_t *outIvLen,
                  uint8_t **outTag, size_t *outTagLen)
{
    (void)store;
    (void)alias;
    (void)plaintext;
    (void)plainLen;
    (void)outCiphertext;
    (void)outCipherLen;
    (void)outIv;
    (void)outIvLen;
    (void)outTag;
    (void)outTagLen;
    return 0;
}

__attribute__((visibility("default"))) int32_t
AKeyStore_decrypt(AKeyStore *store, const char *alias, const uint8_t *ciphertext, size_t cipherLen,
                  const uint8_t *iv, size_t ivLen, const uint8_t *tag, size_t tagLen,
                  uint8_t **outPlaintext, size_t *outPlainLen)
{
    (void)store;
    (void)alias;
    (void)ciphertext;
    (void)cipherLen;
    (void)iv;
    (void)ivLen;
    (void)tag;
    (void)tagLen;
    (void)outPlaintext;
    (void)outPlainLen;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_sign(AKeyStore *store, const char *alias,
                                                              const uint8_t *data, size_t dataLen,
                                                              uint8_t **outSig, size_t *outSigLen)
{
    (void)store;
    (void)alias;
    (void)data;
    (void)dataLen;
    (void)outSig;
    (void)outSigLen;
    return 0;
}

__attribute__((visibility("default"))) int32_t AKeyStore_verify(AKeyStore *store, const char *alias,
                                                                const uint8_t *data, size_t dataLen,
                                                                const uint8_t *sig, size_t sigLen)
{
    (void)store;
    (void)alias;
    (void)data;
    (void)dataLen;
    (void)sig;
    (void)sigLen;
    return 0;
}

__attribute__((visibility("default"))) void AKeyStore_freeBuffer(void *buf) { (void)buf; }
