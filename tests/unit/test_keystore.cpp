#include "security/crypto_engine.hpp"
#include "security/keystore_manager.hpp"
#include "security/keystore_ndk.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

using namespace portage::security;

TEST(KeyStoreTest, KeyStoreManagerAesGcmLifecycle)
{
    auto &mgr = KeyStoreManager::Instance();
    mgr.Reset();

    KeyStoreStatus status =
        mgr.GenerateKey("test_aes_gcm", KeyAlgorithm::Aes, 256, PurposeEncrypt | PurposeDecrypt,
                        BlockMode::Gcm, Padding::None);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_TRUE(mgr.ContainsAlias("test_aes_gcm"));

    const std::string message = "Secret Android Payload 123456";
    std::vector<uint8_t> plain(message.begin(), message.end());

    auto enc_res = mgr.Encrypt("test_aes_gcm", plain);
    EXPECT_EQ(enc_res.status, KeyStoreStatus::Success);
    EXPECT_EQ(enc_res.iv.size(), 12u);
    EXPECT_EQ(enc_res.tag.size(), 16u);
    EXPECT_FALSE(enc_res.data.empty());

    auto dec_res = mgr.Decrypt("test_aes_gcm", enc_res.data, enc_res.iv, enc_res.tag);
    EXPECT_EQ(dec_res.status, KeyStoreStatus::Success);
    std::string decrypted(dec_res.data.begin(), dec_res.data.end());
    EXPECT_EQ(decrypted, message);
}

TEST(KeyStoreTest, KeyStoreManagerAesCbcLifecycle)
{
    auto &mgr = KeyStoreManager::Instance();
    mgr.Reset();

    KeyStoreStatus status =
        mgr.GenerateKey("test_aes_cbc", KeyAlgorithm::Aes, 128, PurposeEncrypt | PurposeDecrypt,
                        BlockMode::Cbc, Padding::Pkcs7);
    EXPECT_EQ(status, KeyStoreStatus::Success);

    const std::string message = "CBC Padded Secret Data";
    std::vector<uint8_t> plain(message.begin(), message.end());

    auto enc_res = mgr.Encrypt("test_aes_cbc", plain);
    EXPECT_EQ(enc_res.status, KeyStoreStatus::Success);
    EXPECT_EQ(enc_res.iv.size(), 16u);
    EXPECT_FALSE(enc_res.data.empty());

    auto dec_res = mgr.Decrypt("test_aes_cbc", enc_res.data, enc_res.iv);
    EXPECT_EQ(dec_res.status, KeyStoreStatus::Success);
    std::string decrypted(dec_res.data.begin(), dec_res.data.end());
    EXPECT_EQ(decrypted, message);
}

TEST(KeyStoreTest, KeyStoreManagerHmacSha256)
{
    std::vector<uint8_t> key = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> data = {'H', 'M', 'A', 'C', ' ', 'D', 'A', 'T', 'A'};
    std::vector<uint8_t> mac1;
    std::vector<uint8_t> mac2;

    EXPECT_TRUE(CryptoEngine::Instance().HmacSha256(key.data(), key.size(), data.data(),
                                                    data.size(), mac1));
    EXPECT_TRUE(CryptoEngine::Instance().HmacSha256(key.data(), key.size(), data.data(),
                                                    data.size(), mac2));
    EXPECT_EQ(mac1.size(), 32u);
    EXPECT_EQ(mac1, mac2);
}

TEST(KeyStoreTest, KeyStoreManagerRsaSigningAndVerification)
{
    auto &mgr = KeyStoreManager::Instance();
    mgr.Reset();

    KeyStoreStatus status =
        mgr.GenerateKeyPair("test_rsa_key", KeyAlgorithm::Rsa, 2048, PurposeSign | PurposeVerify,
                            Digest::Sha256, Padding::Pkcs1);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_TRUE(mgr.ContainsAlias("test_rsa_key"));

    const std::string doc = "Important Document To Sign";
    std::vector<uint8_t> doc_bytes(doc.begin(), doc.end());
    std::vector<uint8_t> signature;

    status = mgr.Sign("test_rsa_key", doc_bytes, signature);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_FALSE(signature.empty());

    status = mgr.Verify("test_rsa_key", doc_bytes, signature);
    EXPECT_EQ(status, KeyStoreStatus::Success);

    std::vector<uint8_t> tampered = doc_bytes;
    tampered[0] ^= 0xFF;
    status = mgr.Verify("test_rsa_key", tampered, signature);
    EXPECT_NE(status, KeyStoreStatus::Success);
}

TEST(KeyStoreTest, KeyStoreManagerEcdsaSigningAndVerification)
{
    auto &mgr = KeyStoreManager::Instance();
    mgr.Reset();

    KeyStoreStatus status =
        mgr.GenerateKeyPair("test_ec_key", KeyAlgorithm::Ec, 256, PurposeSign | PurposeVerify,
                            Digest::Sha256, Padding::None);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_TRUE(mgr.ContainsAlias("test_ec_key"));

    const std::string msg = "ECDSA Secure Message";
    std::vector<uint8_t> msg_bytes(msg.begin(), msg.end());
    std::vector<uint8_t> signature;

    status = mgr.Sign("test_ec_key", msg_bytes, signature);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_FALSE(signature.empty());

    status = mgr.Verify("test_ec_key", msg_bytes, signature);
    EXPECT_EQ(status, KeyStoreStatus::Success);

    std::vector<uint8_t> tampered = msg_bytes;
    tampered.push_back('!');
    status = mgr.Verify("test_ec_key", tampered, signature);
    EXPECT_NE(status, KeyStoreStatus::Success);
}

TEST(KeyStoreTest, KeyStoreManagerVfsPersistence)
{
    auto &mgr = KeyStoreManager::Instance();
    mgr.Reset();

    std::string test_path = "build/debug/test_keystore.bin";
    std::filesystem::remove(test_path);

    mgr.Initialize(test_path);
    KeyStoreStatus status =
        mgr.GenerateKey("persistent_key", KeyAlgorithm::Aes, 256, PurposeEncrypt | PurposeDecrypt,
                        BlockMode::Gcm, Padding::None);
    EXPECT_EQ(status, KeyStoreStatus::Success);
    EXPECT_TRUE(mgr.ContainsAlias("persistent_key"));

    mgr.Reset();
    EXPECT_FALSE(mgr.ContainsAlias("persistent_key"));

    EXPECT_TRUE(mgr.LoadFromDisk());
    EXPECT_TRUE(mgr.ContainsAlias("persistent_key"));

    std::filesystem::remove(test_path);
}

TEST(KeyStoreTest, SyntheticKeyStoreJni)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jclass ks_class = reinterpret_cast<jclass>(const_cast<char *>("java/security/KeyStore"));
    jmethodID get_instance_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getInstance"));
    jstring provider_str = jenv->functions->NewStringUTF(jenv, "AndroidKeyStore");
    jobject ks_obj =
        jenv->functions->CallStaticObjectMethod(jenv, ks_class, get_instance_mid, provider_str);
    ASSERT_NE(ks_obj, nullptr);

    jmethodID load_mid = reinterpret_cast<jmethodID>(const_cast<char *>("load"));
    jenv->functions->CallVoidMethod(jenv, ks_obj, load_mid, nullptr);

    jclass kg_class = reinterpret_cast<jclass>(const_cast<char *>("javax/crypto/KeyGenerator"));
    jstring aes_str = jenv->functions->NewStringUTF(jenv, "AES");
    jobject kg_obj = jenv->functions->CallStaticObjectMethod(jenv, kg_class, get_instance_mid,
                                                             aes_str, provider_str);
    ASSERT_NE(kg_obj, nullptr);

    jmethodID gen_key_mid = reinterpret_cast<jmethodID>(const_cast<char *>("generateKey"));
    jobject secret_key = jenv->functions->CallObjectMethod(jenv, kg_obj, gen_key_mid);
    ASSERT_NE(secret_key, nullptr);

    jclass cipher_class = reinterpret_cast<jclass>(const_cast<char *>("javax/crypto/Cipher"));
    jstring transf_str = jenv->functions->NewStringUTF(jenv, "AES/GCM/NoPadding");
    jobject cipher_obj =
        jenv->functions->CallStaticObjectMethod(jenv, cipher_class, get_instance_mid, transf_str);
    ASSERT_NE(cipher_obj, nullptr);

    jmethodID init_mid = reinterpret_cast<jmethodID>(const_cast<char *>("init"));
    jenv->functions->CallVoidMethod(jenv, cipher_obj, init_mid, 1, secret_key);

    const std::string payload = "Encrypted Via JNI Cipher";
    jbyteArray in_arr = jenv->functions->NewByteArray(jenv, static_cast<jsize>(payload.size()));
    auto in_syn = env.LookupObject(in_arr);
    ASSERT_NE(in_syn, nullptr);
    in_syn->byte_data.assign(payload.begin(), payload.end());

    jmethodID do_final_mid = reinterpret_cast<jmethodID>(const_cast<char *>("doFinal"));
    jbyteArray cipher_arr =
        jenv->functions->CallObjectMethod(jenv, cipher_obj, do_final_mid, in_arr);
    ASSERT_NE(cipher_arr, nullptr);
    auto cipher_syn = env.LookupObject(cipher_arr);
    ASSERT_NE(cipher_syn, nullptr);
    EXPECT_FALSE(cipher_syn->byte_data.empty());

    jmethodID get_iv_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getIV"));
    jbyteArray iv_arr = jenv->functions->CallObjectMethod(jenv, cipher_obj, get_iv_mid);
    ASSERT_NE(iv_arr, nullptr);
    auto iv_syn = env.LookupObject(iv_arr);
    ASSERT_NE(iv_syn, nullptr);
    EXPECT_FALSE(iv_syn->byte_data.empty());

    jclass kpg_class =
        reinterpret_cast<jclass>(const_cast<char *>("java/security/KeyPairGenerator"));
    jstring rsa_str = jenv->functions->NewStringUTF(jenv, "RSA");
    jobject kpg_obj = jenv->functions->CallStaticObjectMethod(jenv, kpg_class, get_instance_mid,
                                                              rsa_str, provider_str);
    ASSERT_NE(kpg_obj, nullptr);

    jmethodID gen_kp_mid = reinterpret_cast<jmethodID>(const_cast<char *>("generateKeyPair"));
    jobject kp_obj = jenv->functions->CallObjectMethod(jenv, kpg_obj, gen_kp_mid);
    ASSERT_NE(kp_obj, nullptr);

    jmethodID get_priv_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getPrivate"));
    jmethodID get_pub_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getPublic"));
    jobject priv_key = jenv->functions->CallObjectMethod(jenv, kp_obj, get_priv_mid);
    jobject pub_key = jenv->functions->CallObjectMethod(jenv, kp_obj, get_pub_mid);
    ASSERT_NE(priv_key, nullptr);
    ASSERT_NE(pub_key, nullptr);

    jclass sig_class = reinterpret_cast<jclass>(const_cast<char *>("java/security/Signature"));
    jstring sha256_rsa_str = jenv->functions->NewStringUTF(jenv, "SHA256withRSA");
    jobject sig_obj =
        jenv->functions->CallStaticObjectMethod(jenv, sig_class, get_instance_mid, sha256_rsa_str);
    ASSERT_NE(sig_obj, nullptr);

    jmethodID init_sign_mid = reinterpret_cast<jmethodID>(const_cast<char *>("initSign"));
    jenv->functions->CallVoidMethod(jenv, sig_obj, init_sign_mid, priv_key);

    jmethodID update_mid = reinterpret_cast<jmethodID>(const_cast<char *>("update"));
    jenv->functions->CallVoidMethod(jenv, sig_obj, update_mid, in_arr);

    jmethodID sign_mid = reinterpret_cast<jmethodID>(const_cast<char *>("sign"));
    jbyteArray sig_arr = jenv->functions->CallObjectMethod(jenv, sig_obj, sign_mid);
    ASSERT_NE(sig_arr, nullptr);

    jmethodID init_verify_mid = reinterpret_cast<jmethodID>(const_cast<char *>("initVerify"));
    jenv->functions->CallVoidMethod(jenv, sig_obj, init_verify_mid, pub_key);
    jenv->functions->CallVoidMethod(jenv, sig_obj, update_mid, in_arr);

    jmethodID verify_mid = reinterpret_cast<jmethodID>(const_cast<char *>("verify"));
    jboolean verified = jenv->functions->CallBooleanMethod(jenv, sig_obj, verify_mid, sig_arr);
    EXPECT_EQ(verified, JNI_TRUE);
}

TEST(KeyStoreTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("AKeyStore_create"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_generateAesKey"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_generateRsaKeyPair"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_encrypt"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_decrypt"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_sign"), 0u);
    EXPECT_NE(registry.Lookup("AKeyStore_verify"), 0u);
}
