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

TEST(JniEnvTest, ResolvesBuildAndVersionConstants)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    portage::package::jclass ver_cls = env->functions->FindClass(env, "android/os/Build$VERSION");
    ASSERT_NE(ver_cls, nullptr);

    portage::package::jfieldID fid_sdk =
        env->functions->GetStaticFieldID(env, ver_cls, "SDK_INT", "I");
    ASSERT_NE(fid_sdk, nullptr);
    EXPECT_EQ(env->functions->GetStaticIntField(env, ver_cls, fid_sdk), 33);

    portage::package::jfieldID fid_rel =
        env->functions->GetStaticFieldID(env, ver_cls, "RELEASE", "Ljava/lang/String;");
    ASSERT_NE(fid_rel, nullptr);
    portage::package::jstring str_rel = env->functions->GetStaticObjectField(env, ver_cls, fid_rel);
    ASSERT_NE(str_rel, nullptr);
    const char *rel_chars = env->functions->GetStringUTFChars(env, str_rel, nullptr);
    EXPECT_STREQ(rel_chars, "13");
    env->functions->ReleaseStringUTFChars(env, str_rel, rel_chars);

    portage::package::jclass build_cls = env->functions->FindClass(env, "android/os/Build");
    ASSERT_NE(build_cls, nullptr);

    portage::package::jfieldID fid_model =
        env->functions->GetStaticFieldID(env, build_cls, "MODEL", "Ljava/lang/String;");
    ASSERT_NE(fid_model, nullptr);
    portage::package::jstring str_model =
        env->functions->GetStaticObjectField(env, build_cls, fid_model);
    ASSERT_NE(str_model, nullptr);
    const char *model_chars = env->functions->GetStringUTFChars(env, str_model, nullptr);
    EXPECT_STREQ(model_chars, "Portage Virtual Device");
    env->functions->ReleaseStringUTFChars(env, str_model, model_chars);

    portage::package::jfieldID fid_mfg =
        env->functions->GetStaticFieldID(env, build_cls, "MANUFACTURER", "Ljava/lang/String;");
    ASSERT_NE(fid_mfg, nullptr);
    portage::package::jstring str_mfg =
        env->functions->GetStaticObjectField(env, build_cls, fid_mfg);
    ASSERT_NE(str_mfg, nullptr);
    const char *mfg_chars = env->functions->GetStringUTFChars(env, str_mfg, nullptr);
    EXPECT_STREQ(mfg_chars, "Google");
    env->functions->ReleaseStringUTFChars(env, str_mfg, mfg_chars);
}

TEST(JniEnvTest, ResolvesDisplayMetricsAndWindowHierarchy)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto act_obj = jni.GetContextObject();
    ASSERT_NE(act_obj, nullptr);
    auto act_cls = env->functions->GetObjectClass(env, act_obj);
    ASSERT_NE(act_cls, nullptr);

    // Activity.getResources()
    auto mid_res = env->functions->GetMethodID(env, act_cls, "getResources",
                                               "()Landroid/content/res/Resources;");
    ASSERT_NE(mid_res, nullptr);
    auto res_obj = env->functions->CallObjectMethod(env, act_obj, mid_res);
    ASSERT_NE(res_obj, nullptr);
    auto res_cls = env->functions->GetObjectClass(env, res_obj);
    ASSERT_NE(res_cls, nullptr);

    // Resources.getDisplayMetrics()
    auto mid_dm = env->functions->GetMethodID(env, res_cls, "getDisplayMetrics",
                                              "()Landroid/util/DisplayMetrics;");
    ASSERT_NE(mid_dm, nullptr);
    auto dm_obj = env->functions->CallObjectMethod(env, res_obj, mid_dm);
    ASSERT_NE(dm_obj, nullptr);
    auto dm_cls = env->functions->GetObjectClass(env, dm_obj);
    ASSERT_NE(dm_cls, nullptr);

    // DisplayMetrics fields
    auto fid_w = env->functions->GetFieldID(env, dm_cls, "widthPixels", "I");
    ASSERT_NE(fid_w, nullptr);
    EXPECT_EQ(env->functions->GetIntField(env, dm_obj, fid_w), 1280);

    auto fid_h = env->functions->GetFieldID(env, dm_cls, "heightPixels", "I");
    ASSERT_NE(fid_h, nullptr);
    EXPECT_EQ(env->functions->GetIntField(env, dm_obj, fid_h), 720);

    auto fid_d = env->functions->GetFieldID(env, dm_cls, "density", "F");
    ASSERT_NE(fid_d, nullptr);
    EXPECT_FLOAT_EQ(env->functions->GetFloatField(env, dm_obj, fid_d), 2.0f);

    auto fid_dpi = env->functions->GetFieldID(env, dm_cls, "densityDpi", "I");
    ASSERT_NE(fid_dpi, nullptr);
    EXPECT_EQ(env->functions->GetIntField(env, dm_obj, fid_dpi), 320);

    // Activity.getWindow()
    auto mid_win =
        env->functions->GetMethodID(env, act_cls, "getWindow", "()Landroid/view/Window;");
    ASSERT_NE(mid_win, nullptr);
    auto win_obj = env->functions->CallObjectMethod(env, act_obj, mid_win);
    ASSERT_NE(win_obj, nullptr);
    auto win_cls = env->functions->GetObjectClass(env, win_obj);
    ASSERT_NE(win_cls, nullptr);

    // Window.getDecorView()
    auto mid_view =
        env->functions->GetMethodID(env, win_cls, "getDecorView", "()Landroid/view/View;");
    ASSERT_NE(mid_view, nullptr);
    auto view_obj = env->functions->CallObjectMethod(env, win_obj, mid_view);
    ASSERT_NE(view_obj, nullptr);
    auto view_cls = env->functions->GetObjectClass(env, view_obj);
    ASSERT_NE(view_cls, nullptr);

    auto mid_vw = env->functions->GetMethodID(env, view_cls, "getWidth", "()I");
    ASSERT_NE(mid_vw, nullptr);
    EXPECT_EQ(env->functions->CallIntMethod(env, view_obj, mid_vw), 1280);

    auto mid_vh = env->functions->GetMethodID(env, view_cls, "getHeight", "()I");
    ASSERT_NE(mid_vh, nullptr);
    EXPECT_EQ(env->functions->CallIntMethod(env, view_obj, mid_vh), 720);

    // Activity.getClassLoader()
    auto mid_cl =
        env->functions->GetMethodID(env, act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    ASSERT_NE(mid_cl, nullptr);
    auto cl_obj = env->functions->CallObjectMethod(env, act_obj, mid_cl);
    ASSERT_NE(cl_obj, nullptr);
    auto cl_cls = env->functions->GetObjectClass(env, cl_obj);
    ASSERT_NE(cl_cls, nullptr);

    auto mid_find = env->functions->GetMethodID(env, cl_cls, "loadClass",
                                                "(Ljava/lang/String;)Ljava/lang/Class;");
    ASSERT_NE(mid_find, nullptr);
    auto class_str = env->functions->NewStringUTF(env, "android.os.Build");
    auto loaded_cls = env->functions->CallObjectMethod(env, cl_obj, mid_find, class_str);
    EXPECT_NE(loaded_cls, nullptr);
}

TEST(JniEnvTest, HandlesDirectByteBuffer)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    char local_buf[64] = "Hello Direct Byte Buffer";
    auto byte_buf = env->functions->NewDirectByteBuffer(env, local_buf, sizeof(local_buf));
    ASSERT_NE(byte_buf, nullptr);

    EXPECT_EQ(env->functions->GetDirectBufferAddress(env, byte_buf), local_buf);
    EXPECT_EQ(env->functions->GetDirectBufferCapacity(env, byte_buf),
              static_cast<portage::package::jlong>(sizeof(local_buf)));
}

TEST(JniEnvTest, HandlesPrimitiveArrays)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    // ByteArray
    auto byte_arr = env->functions->NewByteArray(env, 8);
    ASSERT_NE(byte_arr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, byte_arr), 8);

    auto *bytes = env->functions->GetByteArrayElements(env, byte_arr, nullptr);
    ASSERT_NE(bytes, nullptr);
    bytes[0] = 42;
    bytes[7] = 99;
    env->functions->ReleaseByteArrayElements(env, byte_arr, bytes, 0);

    portage::package::jbyte read_buf[8] = {0};
    env->functions->GetByteArrayRegion(env, byte_arr, 0, 8, read_buf);
    EXPECT_EQ(read_buf[0], 42);
    EXPECT_EQ(read_buf[7], 99);

    // IntArray
    auto int_arr = env->functions->NewIntArray(env, 4);
    ASSERT_NE(int_arr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, int_arr), 4);

    portage::package::jint test_ints[4] = {10, 20, 30, 40};
    env->functions->SetIntArrayRegion(env, int_arr, 0, 4, test_ints);

    auto *ints = env->functions->GetIntArrayElements(env, int_arr, nullptr);
    ASSERT_NE(ints, nullptr);
    EXPECT_EQ(ints[0], 10);
    EXPECT_EQ(ints[3], 40);
    env->functions->ReleaseIntArrayElements(env, int_arr, ints, 0);
}

static void dummy_native_func() {}

TEST(JniEnvTest, HandlesRegisterNativesAndExceptions)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");

    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto dummy_cls = env->functions->FindClass(env, "com/example/TestClass");
    ASSERT_NE(dummy_cls, nullptr);

    portage::package::JNINativeMethod methods[] = {
        {"dummyNative", "()V", reinterpret_cast<void *>(dummy_native_func)},
    };
    EXPECT_EQ(env->functions->RegisterNatives(env, dummy_cls, methods, 1),
              portage::package::JNI_OK);

    EXPECT_EQ(jni.LookupNativeMethod("com/example/TestClass", "dummyNative", "()V"),
              reinterpret_cast<void *>(dummy_native_func));

    // Exception handling
    EXPECT_EQ(env->functions->ExceptionCheck(env), portage::package::JNI_FALSE);
    EXPECT_EQ(env->functions->ExceptionOccurred(env), nullptr);

    env->functions->ThrowNew(env, dummy_cls, "Test exception message");
    EXPECT_EQ(env->functions->ExceptionCheck(env), portage::package::JNI_TRUE);
    EXPECT_NE(env->functions->ExceptionOccurred(env), nullptr);

    env->functions->ExceptionClear(env);
    EXPECT_EQ(env->functions->ExceptionCheck(env), portage::package::JNI_FALSE);
    EXPECT_EQ(env->functions->ExceptionOccurred(env), nullptr);
}
