#include "package/jni_env.hpp"
#include <gtest/gtest.h>

TEST(JniEnvTest, InitializesVmAndEnvTables)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *vm = jni.GetJavaVM();
    ASSERT_NE(vm, nullptr);
    ASSERT_NE(vm->functions, nullptr);

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);
    ASSERT_NE(env->functions, nullptr);

    void *env_out = nullptr;
    EXPECT_EQ(vm->functions->GetEnv(vm, &env_out, portage::package::JNI_VERSION_1_6),
              portage::package::JNI_OK);
    EXPECT_EQ(env_out, env);

    EXPECT_EQ(vm->functions->AttachCurrentThread(vm, &env_out, nullptr), portage::package::JNI_OK);
    EXPECT_EQ(env->functions->GetVersion(env), portage::package::JNI_VERSION_1_6);
}

TEST(JniEnvTest, ResolvesPackageAndDirectoryMethods)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    portage::package::jobject fake_obj = reinterpret_cast<portage::package::jobject>(0x1234);
    portage::package::jclass cls = env->functions->GetObjectClass(env, fake_obj);
    ASSERT_NE(cls, nullptr);

    portage::package::jmethodID mid_pkg =
        env->functions->GetMethodID(env, cls, "getPackageName", "()Ljava/lang/String;");
    ASSERT_NE(mid_pkg, nullptr);

    portage::package::jstring str_pkg = env->functions->CallObjectMethod(env, fake_obj, mid_pkg);
    ASSERT_NE(str_pkg, nullptr);

    const char *chars = env->functions->GetStringUTFChars(env, str_pkg, nullptr);
    ASSERT_NE(chars, nullptr);
    EXPECT_STREQ(chars, "com.portage.unit");
    env->functions->ReleaseStringUTFChars(env, str_pkg, chars);

    portage::package::jmethodID mid_files =
        env->functions->GetMethodID(env, cls, "getFilesDir", "()Ljava/io/File;");
    ASSERT_NE(mid_files, nullptr);

    portage::package::jstring str_files =
        env->functions->CallObjectMethod(env, fake_obj, mid_files);
    ASSERT_NE(str_files, nullptr);
    chars = env->functions->GetStringUTFChars(env, str_files, nullptr);
    EXPECT_STREQ(chars, "/data/data/com.portage.unit/files");
    env->functions->ReleaseStringUTFChars(env, str_files, chars);
}
