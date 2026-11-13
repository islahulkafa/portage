#include "ndk/jnigraphics.hpp"
#include "package/jni_env.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>
#include <zlib.h>

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

namespace {

// Minimal valid 4x4 uncompressed 24bpp Windows BMP bytes
std::vector<uint8_t> BuildSolidBmp4x4(uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint8_t> bmp(54 + 4 * 4 * 3, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    uint32_t total = static_cast<uint32_t>(bmp.size());
    std::memcpy(bmp.data() + 2, &total, 4);
    uint32_t off_bits = 54;
    std::memcpy(bmp.data() + 10, &off_bits, 4);
    uint32_t dib_sz = 40;
    std::memcpy(bmp.data() + 14, &dib_sz, 4);
    int32_t w = 4, h = 4;
    std::memcpy(bmp.data() + 18, &w, 4);
    std::memcpy(bmp.data() + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(bmp.data() + 26, &planes, 2);
    std::memcpy(bmp.data() + 28, &bpp, 2);
    for (int i = 0; i < 16; ++i) {
        bmp[54 + i * 3 + 0] = b;
        bmp[54 + i * 3 + 1] = g;
        bmp[54 + i * 3 + 2] = r;
    }
    return bmp;
}

} // namespace

TEST(JniEnvTest, BitmapFactoryBoundsAndSamplingDecode)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();

    auto bmp_bytes = BuildSolidBmp4x4(10, 180, 240);
    auto jarr =
        env->functions->NewByteArray(env, static_cast<portage::package::jsize>(bmp_bytes.size()));
    ASSERT_NE(jarr, nullptr);
    env->functions->SetByteArrayRegion(
        env, jarr, 0, static_cast<portage::package::jsize>(bmp_bytes.size()),
        reinterpret_cast<const portage::package::jbyte *>(bmp_bytes.data()));

    auto opts_cls = env->functions->FindClass(env, "android/graphics/BitmapFactory$Options");
    auto opts_ctor = env->functions->GetMethodID(env, opts_cls, "<init>", "()V");
    auto opts = env->functions->NewObject(env, opts_cls, opts_ctor);
    ASSERT_NE(opts, nullptr);

    auto f_bounds = env->functions->GetFieldID(env, opts_cls, "inJustDecodeBounds", "Z");
    auto f_sample = env->functions->GetFieldID(env, opts_cls, "inSampleSize", "I");
    auto f_w = env->functions->GetFieldID(env, opts_cls, "outWidth", "I");
    auto f_h = env->functions->GetFieldID(env, opts_cls, "outHeight", "I");
    auto f_mime = env->functions->GetFieldID(env, opts_cls, "outMimeType", "Ljava/lang/String;");

    env->functions->SetBooleanField(env, opts, f_bounds, portage::package::JNI_TRUE);

    auto bf_cls = env->functions->FindClass(env, "android/graphics/BitmapFactory");
    auto decode_mid = env->functions->GetStaticMethodID(
        env, bf_cls, "decodeByteArray",
        "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;");

    auto bounds_ret = env->functions->CallStaticObjectMethod(
        env, bf_cls, decode_mid, jarr, 0, static_cast<int>(bmp_bytes.size()), opts);
    EXPECT_EQ(bounds_ret, nullptr);
    EXPECT_EQ(env->functions->GetIntField(env, opts, f_w), 4);
    EXPECT_EQ(env->functions->GetIntField(env, opts, f_h), 4);
    auto mime_jstr =
        static_cast<portage::package::jstring>(env->functions->GetObjectField(env, opts, f_mime));
    ASSERT_NE(mime_jstr, nullptr);
    EXPECT_STREQ(jni.GetStringChars(mime_jstr), "image/bmp");

    env->functions->SetBooleanField(env, opts, f_bounds, portage::package::JNI_FALSE);
    env->functions->SetIntField(env, opts, f_sample, 2);

    auto jbitmap = env->functions->CallStaticObjectMethod(env, bf_cls, decode_mid, jarr, 0,
                                                          static_cast<int>(bmp_bytes.size()), opts);
    ASSERT_NE(jbitmap, nullptr);

    AndroidBitmapInfo info{};
    ASSERT_EQ(AndroidBitmap_getInfo(env, jbitmap, &info), ANDROID_BITMAP_RESULT_SUCCESS);
    EXPECT_EQ(info.width, 2U);
    EXPECT_EQ(info.height, 2U);

    void *pixels = nullptr;
    ASSERT_EQ(AndroidBitmap_lockPixels(env, jbitmap, &pixels), ANDROID_BITMAP_RESULT_SUCCESS);
    ASSERT_NE(pixels, nullptr);
    const auto *rgba = static_cast<const uint8_t *>(pixels);
    EXPECT_EQ(rgba[0], 10);
    EXPECT_EQ(rgba[1], 180);
    EXPECT_EQ(rgba[2], 240);
    EXPECT_EQ(AndroidBitmap_unlockPixels(env, jbitmap), ANDROID_BITMAP_RESULT_SUCCESS);
}

TEST(JniEnvTest, SharedPreferencesWindowMetricsAndPackageInfo)
{
    auto tmp_root = std::filesystem::temp_directory_path() / "portage_jni_prefs_test";
    std::error_code ec;
    std::filesystem::remove_all(tmp_root, ec);
    std::filesystem::create_directories(tmp_root / "files", ec);
    std::filesystem::create_directories(tmp_root / "cache", ec);

    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", (tmp_root / "files").string(),
                   (tmp_root / "cache").string());
    jni.SetDisplayMetrics(1920, 1080, 2.5f, 400);
    auto *env = jni.GetJNIEnv();

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->GetObjectClass(env, ctx);
    auto get_sp =
        env->functions->GetMethodID(env, act_cls, "getSharedPreferences",
                                    "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    auto sp = env->functions->CallObjectMethod(env, ctx, get_sp, jni.CreateString("settings"), 0);
    ASSERT_NE(sp, nullptr);

    auto sp_cls = env->functions->GetObjectClass(env, sp);
    auto edit_mid = env->functions->GetMethodID(env, sp_cls, "edit",
                                                "()Landroid/content/SharedPreferences$Editor;");
    auto editor = env->functions->CallObjectMethod(env, sp, edit_mid);
    ASSERT_NE(editor, nullptr);

    auto ed_cls = env->functions->GetObjectClass(env, editor);
    auto put_str = env->functions->GetMethodID(
        env, ed_cls, "putString",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
    auto put_int = env->functions->GetMethodID(
        env, ed_cls, "putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;");
    auto commit_mid = env->functions->GetMethodID(env, ed_cls, "commit", "()Z");

    env->functions->CallObjectMethod(env, editor, put_str, jni.CreateString("player"),
                                     jni.CreateString("Ada"));
    env->functions->CallObjectMethod(env, editor, put_int, jni.CreateString("score"), 42);
    EXPECT_EQ(env->functions->CallBooleanMethod(env, editor, commit_mid),
              portage::package::JNI_TRUE);

    auto get_str = env->functions->GetMethodID(
        env, sp_cls, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    auto get_int = env->functions->GetMethodID(env, sp_cls, "getInt", "(Ljava/lang/String;I)I");
    auto val_str = static_cast<portage::package::jstring>(env->functions->CallObjectMethod(
        env, sp, get_str, jni.CreateString("player"), jni.CreateString("Guest")));
    EXPECT_STREQ(jni.GetStringChars(val_str), "Ada");
    EXPECT_EQ(env->functions->CallIntMethod(env, sp, get_int, jni.CreateString("score"), -1), 42);

    // WindowManager -> getDefaultDisplay -> getMetrics
    auto get_svc = env->functions->GetMethodID(env, act_cls, "getSystemService",
                                               "(Ljava/lang/String;)Ljava/lang/Object;");
    auto wm = env->functions->CallObjectMethod(env, ctx, get_svc, jni.CreateString("window"));
    ASSERT_NE(wm, nullptr);
    auto wm_cls = env->functions->GetObjectClass(env, wm);
    auto get_disp =
        env->functions->GetMethodID(env, wm_cls, "getDefaultDisplay", "()Landroid/view/Display;");
    auto disp = env->functions->CallObjectMethod(env, wm, get_disp);
    ASSERT_NE(disp, nullptr);

    auto dm_cls = env->functions->FindClass(env, "android/util/DisplayMetrics");
    auto dm = env->functions->NewObject(env, dm_cls,
                                        env->functions->GetMethodID(env, dm_cls, "<init>", "()V"));
    auto disp_cls = env->functions->GetObjectClass(env, disp);
    auto get_metrics = env->functions->GetMethodID(env, disp_cls, "getMetrics",
                                                   "(Landroid/util/DisplayMetrics;)V");
    env->functions->CallVoidMethod(env, disp, get_metrics, dm);
    EXPECT_EQ(env->functions->GetIntField(
                  env, dm, env->functions->GetFieldID(env, dm_cls, "widthPixels", "I")),
              1920);
    EXPECT_EQ(env->functions->GetIntField(
                  env, dm, env->functions->GetFieldID(env, dm_cls, "heightPixels", "I")),
              1080);

    // PackageManager -> getPackageInfo
    auto get_pm = env->functions->GetMethodID(env, act_cls, "getPackageManager",
                                              "()Landroid/content/pm/PackageManager;");
    auto pm = env->functions->CallObjectMethod(env, ctx, get_pm);
    ASSERT_NE(pm, nullptr);
    auto pm_cls = env->functions->GetObjectClass(env, pm);
    auto get_pi = env->functions->GetMethodID(
        env, pm_cls, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    auto pi =
        env->functions->CallObjectMethod(env, pm, get_pi, jni.CreateString("com.portage.unit"), 0);
    ASSERT_NE(pi, nullptr);
    auto pi_cls = env->functions->GetObjectClass(env, pi);
    auto pkg_str = static_cast<portage::package::jstring>(env->functions->GetObjectField(
        env, pi, env->functions->GetFieldID(env, pi_cls, "packageName", "Ljava/lang/String;")));
    ASSERT_NE(pkg_str, nullptr);
    EXPECT_STREQ(jni.GetStringChars(pkg_str), "com.portage.unit");

    std::filesystem::remove_all(tmp_root, ec);
}

TEST(JniEnvTest, ReflectionToAndFromReflectedMethod)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->FindClass(env, "android/app/Activity");
    auto mid = env->functions->GetMethodID(env, act_cls, "getPackageName", "()Ljava/lang/String;");
    ASSERT_NE(mid, nullptr);

    auto reflected =
        env->functions->ToReflectedMethod(env, act_cls, mid, portage::package::JNI_FALSE);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(env->functions->FromReflectedMethod(env, reflected), mid);

    auto method_cls = env->functions->GetObjectClass(env, reflected);
    auto get_name_mid =
        env->functions->GetMethodID(env, method_cls, "getName", "()Ljava/lang/String;");
    auto name_str = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, reflected, get_name_mid));
    ASSERT_NE(name_str, nullptr);
    EXPECT_STREQ(jni.GetStringChars(name_str), "getPackageName");

    auto invoke_mid = env->functions->GetMethodID(
        env, method_cls, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    auto invoked = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, reflected, invoke_mid, ctx, nullptr));
    ASSERT_NE(invoked, nullptr);
    EXPECT_STREQ(jni.GetStringChars(invoked), "com.portage.unit");

    auto env_cls = env->functions->FindClass(env, "android/os/Environment");
    auto data_dir_mid =
        env->functions->GetStaticMethodID(env, env_cls, "getDataDirectory", "()Ljava/io/File;");
    auto static_reflected =
        env->functions->ToReflectedMethod(env, env_cls, data_dir_mid, portage::package::JNI_TRUE);
    ASSERT_NE(static_reflected, nullptr);
    auto file_obj =
        env->functions->CallObjectMethod(env, static_reflected, invoke_mid, nullptr, nullptr);
    ASSERT_NE(file_obj, nullptr);
    auto file_cls = env->functions->GetObjectClass(env, file_obj);
    auto abs_mid =
        env->functions->GetMethodID(env, file_cls, "getAbsolutePath", "()Ljava/lang/String;");
    auto abs_str = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, file_obj, abs_mid));
    EXPECT_STREQ(jni.GetStringChars(abs_str), "/data");
}

TEST(JniEnvTest, ReflectionToAndFromReflectedField)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->GetObjectClass(env, ctx);
    auto get_pm = env->functions->GetMethodID(env, act_cls, "getPackageManager",
                                              "()Landroid/content/pm/PackageManager;");
    auto pm = env->functions->CallObjectMethod(env, ctx, get_pm);
    auto pm_cls = env->functions->GetObjectClass(env, pm);
    auto get_pi = env->functions->GetMethodID(
        env, pm_cls, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    auto pi =
        env->functions->CallObjectMethod(env, pm, get_pi, jni.CreateString("com.portage.orig"), 0);
    ASSERT_NE(pi, nullptr);

    auto pi_cls = env->functions->GetObjectClass(env, pi);
    auto fid = env->functions->GetFieldID(env, pi_cls, "packageName", "Ljava/lang/String;");
    ASSERT_NE(fid, nullptr);

    auto reflected =
        env->functions->ToReflectedField(env, pi_cls, fid, portage::package::JNI_FALSE);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(env->functions->FromReflectedField(env, reflected), fid);

    auto field_cls = env->functions->GetObjectClass(env, reflected);
    auto get_name_mid =
        env->functions->GetMethodID(env, field_cls, "getName", "()Ljava/lang/String;");
    auto fname = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, reflected, get_name_mid));
    EXPECT_STREQ(jni.GetStringChars(fname), "packageName");

    auto get_mid = env->functions->GetMethodID(env, field_cls, "get",
                                               "(Ljava/lang/Object;)Ljava/lang/Object;");
    auto cur_val = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, reflected, get_mid, pi));
    EXPECT_STREQ(jni.GetStringChars(cur_val), "com.portage.orig");

    auto set_mid = env->functions->GetMethodID(env, field_cls, "set",
                                               "(Ljava/lang/Object;Ljava/lang/Object;)V");
    env->functions->CallVoidMethod(env, reflected, set_mid, pi,
                                   jni.CreateString("com.portage.reflected"));
    auto updated_val = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, reflected, get_mid, pi));
    EXPECT_STREQ(jni.GetStringChars(updated_val), "com.portage.reflected");
}

TEST(JniEnvTest, SuperclassAndIsAssignableFrom)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto act_cls = env->functions->FindClass(env, "android/app/Activity");
    auto ctx_cls = env->functions->FindClass(env, "android/content/Context");
    auto obj_cls = env->functions->FindClass(env, "java/lang/Object");
    auto bundle_cls = env->functions->FindClass(env, "android/os/Bundle");

    auto act_super = env->functions->GetSuperclass(env, act_cls);
    ASSERT_NE(act_super, nullptr);
    EXPECT_STREQ(reinterpret_cast<const char *>(act_super), "android/content/Context");

    auto ctx_super = env->functions->GetSuperclass(env, ctx_cls);
    ASSERT_NE(ctx_super, nullptr);
    EXPECT_STREQ(reinterpret_cast<const char *>(ctx_super), "java/lang/Object");

    EXPECT_EQ(env->functions->GetSuperclass(env, obj_cls), nullptr);

    EXPECT_EQ(env->functions->IsAssignableFrom(env, act_cls, ctx_cls), portage::package::JNI_TRUE);
    EXPECT_EQ(env->functions->IsAssignableFrom(env, act_cls, obj_cls), portage::package::JNI_TRUE);
    EXPECT_EQ(env->functions->IsAssignableFrom(env, act_cls, act_cls), portage::package::JNI_TRUE);
    EXPECT_EQ(env->functions->IsAssignableFrom(env, ctx_cls, act_cls), portage::package::JNI_FALSE);
    EXPECT_EQ(env->functions->IsAssignableFrom(env, bundle_cls, ctx_cls),
              portage::package::JNI_FALSE);
}

TEST(JniEnvTest, BuildAndVersionFields)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto build_cls = env->functions->FindClass(env, "android/os/Build");
    auto check_build_field = [&](const char *field, const char *expected) {
        auto fid = env->functions->GetStaticFieldID(env, build_cls, field, "Ljava/lang/String;");
        ASSERT_NE(fid, nullptr);
        auto str = static_cast<portage::package::jstring>(
            env->functions->GetStaticObjectField(env, build_cls, fid));
        ASSERT_NE(str, nullptr);
        EXPECT_STREQ(jni.GetStringChars(str), expected);
    };

    check_build_field("MODEL", "Portage Virtual Device");
    check_build_field("BRAND", "generic");
    check_build_field("MANUFACTURER", "Google");
    check_build_field("HOST", "portage-win");
    check_build_field("USER", "android-build");
    check_build_field("TAGS", "release-keys");
    check_build_field("TYPE", "user");

    auto ver_cls = env->functions->FindClass(env, "android/os/Build$VERSION");
    auto fid_inc =
        env->functions->GetStaticFieldID(env, ver_cls, "INCREMENTAL", "Ljava/lang/String;");
    auto str_inc = static_cast<portage::package::jstring>(
        env->functions->GetStaticObjectField(env, ver_cls, fid_inc));
    ASSERT_NE(str_inc, nullptr);
    EXPECT_STREQ(jni.GetStringChars(str_inc), "1");

    auto fid_code =
        env->functions->GetStaticFieldID(env, ver_cls, "CODENAME", "Ljava/lang/String;");
    auto str_code = static_cast<portage::package::jstring>(
        env->functions->GetStaticObjectField(env, ver_cls, fid_code));
    ASSERT_NE(str_code, nullptr);
    EXPECT_STREQ(jni.GetStringChars(str_code), "REL");
}

TEST(JniEnvTest, EnvironmentAndProcessMethods)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto env_cls = env->functions->FindClass(env, "android/os/Environment");
    auto mid_data =
        env->functions->GetStaticMethodID(env, env_cls, "getDataDirectory", "()Ljava/io/File;");
    auto data_file = env->functions->CallStaticObjectMethod(env, env_cls, mid_data);
    ASSERT_NE(data_file, nullptr);
    auto file_cls = env->functions->GetObjectClass(env, data_file);
    auto mid_abs =
        env->functions->GetMethodID(env, file_cls, "getAbsolutePath", "()Ljava/lang/String;");
    auto path_str = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, data_file, mid_abs));
    EXPECT_STREQ(jni.GetStringChars(path_str), "/data");

    auto mid_emul =
        env->functions->GetStaticMethodID(env, env_cls, "isExternalStorageEmulated", "()Z");
    EXPECT_EQ(env->functions->CallStaticBooleanMethod(env, env_cls, mid_emul),
              portage::package::JNI_TRUE);

    auto proc_cls = env->functions->FindClass(env, "android/os/Process");
    auto mid_pid = env->functions->GetStaticMethodID(env, proc_cls, "myPid", "()I");
    auto mid_tid = env->functions->GetStaticMethodID(env, proc_cls, "myTid", "()I");
    auto mid_uid = env->functions->GetStaticMethodID(env, proc_cls, "myUid", "()I");

    EXPECT_GT(env->functions->CallStaticIntMethod(env, proc_cls, mid_pid), 0);
    EXPECT_GE(env->functions->CallStaticIntMethod(env, proc_cls, mid_tid), 0);
    EXPECT_EQ(env->functions->CallStaticIntMethod(env, proc_cls, mid_uid), 10000);
}

TEST(JniEnvTest, BundleOperations)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto bundle_cls = env->functions->FindClass(env, "android/os/Bundle");
    auto init_mid = env->functions->GetMethodID(env, bundle_cls, "<init>", "()V");
    auto bundle = env->functions->NewObject(env, bundle_cls, init_mid);
    ASSERT_NE(bundle, nullptr);

    auto put_str = env->functions->GetMethodID(env, bundle_cls, "putString",
                                               "(Ljava/lang/String;Ljava/lang/String;)V");
    auto put_int = env->functions->GetMethodID(env, bundle_cls, "putInt", "(Ljava/lang/String;I)V");
    auto put_bool =
        env->functions->GetMethodID(env, bundle_cls, "putBoolean", "(Ljava/lang/String;Z)V");

    env->functions->CallVoidMethod(env, bundle, put_str, jni.CreateString("hero"),
                                   jni.CreateString("Portage"));
    env->functions->CallVoidMethod(env, bundle, put_int, jni.CreateString("level"), 99);
    env->functions->CallVoidMethod(env, bundle, put_bool, jni.CreateString("enabled"),
                                   portage::package::JNI_TRUE);

    auto contains =
        env->functions->GetMethodID(env, bundle_cls, "containsKey", "(Ljava/lang/String;)Z");
    EXPECT_EQ(env->functions->CallBooleanMethod(env, bundle, contains, jni.CreateString("hero")),
              portage::package::JNI_TRUE);
    EXPECT_EQ(env->functions->CallBooleanMethod(env, bundle, contains, jni.CreateString("missing")),
              portage::package::JNI_FALSE);

    auto get_str = env->functions->GetMethodID(env, bundle_cls, "getString",
                                               "(Ljava/lang/String;)Ljava/lang/String;");
    auto get_int = env->functions->GetMethodID(env, bundle_cls, "getInt", "(Ljava/lang/String;I)I");
    auto get_bool =
        env->functions->GetMethodID(env, bundle_cls, "getBoolean", "(Ljava/lang/String;Z)Z");

    auto hero = static_cast<portage::package::jstring>(
        env->functions->CallObjectMethod(env, bundle, get_str, jni.CreateString("hero")));
    ASSERT_NE(hero, nullptr);
    EXPECT_STREQ(jni.GetStringChars(hero), "Portage");
    EXPECT_EQ(env->functions->CallObjectMethod(env, bundle, get_str, jni.CreateString("missing")),
              nullptr);

    EXPECT_EQ(env->functions->CallIntMethod(env, bundle, get_int, jni.CreateString("level"), -1),
              99);
    EXPECT_EQ(env->functions->CallIntMethod(env, bundle, get_int, jni.CreateString("missing"), -1),
              -1);

    EXPECT_EQ(env->functions->CallBooleanMethod(env, bundle, get_bool, jni.CreateString("enabled"),
                                                portage::package::JNI_FALSE),
              portage::package::JNI_TRUE);
    EXPECT_EQ(env->functions->CallBooleanMethod(env, bundle, get_bool, jni.CreateString("missing"),
                                                portage::package::JNI_FALSE),
              portage::package::JNI_FALSE);
}

TEST(JniEnvTest, VibratorDispatch)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->GetObjectClass(env, ctx);
    auto get_svc = env->functions->GetMethodID(env, act_cls, "getSystemService",
                                               "(Ljava/lang/String;)Ljava/lang/Object;");
    auto vib = env->functions->CallObjectMethod(env, ctx, get_svc, jni.CreateString("vibrator"));
    ASSERT_NE(vib, nullptr);

    auto vib_cls = env->functions->GetObjectClass(env, vib);
    auto has_vib = env->functions->GetMethodID(env, vib_cls, "hasVibrator", "()Z");
    EXPECT_EQ(env->functions->CallBooleanMethod(env, vib, has_vib), portage::package::JNI_TRUE);

    auto vibrate_mid = env->functions->GetMethodID(env, vib_cls, "vibrate", "(J)V");
    env->functions->CallVoidMethod(env, vib, vibrate_mid,
                                   static_cast<portage::package::jlong>(100));
}

TEST(JniEnvTest, DisplayAndPoint)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    jni.SetDisplayMetrics(1280, 720, 2.0f, 320);
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto pt_cls = env->functions->FindClass(env, "android/graphics/Point");
    auto pt_init = env->functions->GetMethodID(env, pt_cls, "<init>", "(II)V");
    auto pt = env->functions->NewObject(env, pt_cls, pt_init, 15, 30);
    ASSERT_NE(pt, nullptr);

    auto fid_x = env->functions->GetFieldID(env, pt_cls, "x", "I");
    auto fid_y = env->functions->GetFieldID(env, pt_cls, "y", "I");
    EXPECT_EQ(env->functions->GetIntField(env, pt, fid_x), 15);
    EXPECT_EQ(env->functions->GetIntField(env, pt, fid_y), 30);

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->GetObjectClass(env, ctx);
    auto get_svc = env->functions->GetMethodID(env, act_cls, "getSystemService",
                                               "(Ljava/lang/String;)Ljava/lang/Object;");
    auto wm = env->functions->CallObjectMethod(env, ctx, get_svc, jni.CreateString("window"));
    auto wm_cls = env->functions->GetObjectClass(env, wm);
    auto disp = env->functions->CallObjectMethod(
        env, wm,
        env->functions->GetMethodID(env, wm_cls, "getDefaultDisplay", "()Landroid/view/Display;"));
    ASSERT_NE(disp, nullptr);

    auto disp_cls = env->functions->GetObjectClass(env, disp);
    auto get_real_size =
        env->functions->GetMethodID(env, disp_cls, "getRealSize", "(Landroid/graphics/Point;)V");
    env->functions->CallVoidMethod(env, disp, get_real_size, pt);
    EXPECT_EQ(env->functions->GetIntField(env, pt, fid_x), 1280);
    EXPECT_EQ(env->functions->GetIntField(env, pt, fid_y), 720);

    auto get_hz = env->functions->GetMethodID(env, disp_cls, "getRefreshRate", "()F");
    EXPECT_FLOAT_EQ(env->functions->CallFloatMethod(env, disp, get_hz), 60.0f);

    auto get_rot = env->functions->GetMethodID(env, disp_cls, "getRotation", "()I");
    EXPECT_EQ(env->functions->CallIntMethod(env, disp, get_rot), 0);

    auto win = jni.GetWindowObject();
    auto win_cls = env->functions->GetObjectClass(env, win);
    auto decor = env->functions->CallObjectMethod(
        env, win,
        env->functions->GetMethodID(env, win_cls, "getDecorView", "()Landroid/view/View;"));
    ASSERT_NE(decor, nullptr);
    auto view_cls = env->functions->GetObjectClass(env, decor);
    env->functions->CallVoidMethod(
        env, decor, env->functions->GetMethodID(env, view_cls, "setSystemUiVisibility", "(I)V"),
        0x104);
    EXPECT_EQ(
        env->functions->CallIntMethod(
            env, decor, env->functions->GetMethodID(env, view_cls, "getSystemUiVisibility", "()I")),
        0x104);
}

TEST(JniEnvTest, PackageManagerAndPackageInfo)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto ctx = jni.GetContextObject();
    auto act_cls = env->functions->GetObjectClass(env, ctx);
    auto pm = env->functions->CallObjectMethod(
        env, ctx,
        env->functions->GetMethodID(env, act_cls, "getPackageManager",
                                    "()Landroid/content/pm/PackageManager;"));
    ASSERT_NE(pm, nullptr);

    auto pm_cls = env->functions->GetObjectClass(env, pm);
    auto pi = env->functions->CallObjectMethod(
        env, pm,
        env->functions->GetMethodID(env, pm_cls, "getPackageInfo",
                                    "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;"),
        jni.CreateString("com.portage.meta"), 0);
    ASSERT_NE(pi, nullptr);

    auto pi_cls = env->functions->GetObjectClass(env, pi);
    auto ver_name = static_cast<portage::package::jstring>(env->functions->GetObjectField(
        env, pi, env->functions->GetFieldID(env, pi_cls, "versionName", "Ljava/lang/String;")));
    ASSERT_NE(ver_name, nullptr);
    EXPECT_STREQ(jni.GetStringChars(ver_name), "1.0.0");
    EXPECT_EQ(env->functions->GetIntField(
                  env, pi, env->functions->GetFieldID(env, pi_cls, "versionCode", "I")),
              1);

    auto app_info = env->functions->CallObjectMethod(
        env, pm,
        env->functions->GetMethodID(env, pm_cls, "getApplicationInfo",
                                    "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;"),
        jni.CreateString("com.portage.meta"), 0);
    ASSERT_NE(app_info, nullptr);
    auto app_cls = env->functions->GetObjectClass(env, app_info);
    auto data_dir = static_cast<portage::package::jstring>(env->functions->GetObjectField(
        env, app_info, env->functions->GetFieldID(env, app_cls, "dataDir", "Ljava/lang/String;")));
    ASSERT_NE(data_dir, nullptr);
    EXPECT_STREQ(jni.GetStringChars(data_dir), "/data/data/com.portage.unit/files");
    auto src_dir = static_cast<portage::package::jstring>(env->functions->GetObjectField(
        env, app_info,
        env->functions->GetFieldID(env, app_cls, "sourceDir", "Ljava/lang/String;")));
    ASSERT_NE(src_dir, nullptr);
    EXPECT_GT(env->functions->GetIntField(
                  env, app_info, env->functions->GetFieldID(env, app_cls, "targetSdkVersion", "I")),
              0);
}

TEST(JniEnvTest, PrimitiveArrayTypeSafety)
{
    auto &jni = portage::package::JniEnvironment::Instance();
    jni.Initialize("com.portage.unit", "/data/data/com.portage.unit/files",
                   "/data/data/com.portage.unit/cache");
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto sarr = env->functions->NewShortArray(env, 5);
    ASSERT_NE(sarr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, sarr), 5);
    const portage::package::jshort s_in[5] = {10, -20, 300, -4000, 32000};
    env->functions->SetShortArrayRegion(env, sarr, 0, 5, s_in);
    portage::package::jshort s_out[5] = {};
    env->functions->GetShortArrayRegion(env, sarr, 0, 5, s_out);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(s_out[i], s_in[i]);
    }
    auto *s_elems = env->functions->GetShortArrayElements(env, sarr, nullptr);
    ASSERT_NE(s_elems, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(s_elems) % alignof(portage::package::jshort), 0U);
    env->functions->ReleaseShortArrayElements(env, sarr, s_elems, 0);

    auto larr = env->functions->NewLongArray(env, 4);
    ASSERT_NE(larr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, larr), 4);
    const portage::package::jlong l_in[4] = {0x1122334455667788LL, -987654321012345LL, 0LL, 42LL};
    env->functions->SetLongArrayRegion(env, larr, 0, 4, l_in);
    portage::package::jlong l_out[4] = {};
    env->functions->GetLongArrayRegion(env, larr, 0, 4, l_out);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(l_out[i], l_in[i]);
    }
    auto *l_elems = env->functions->GetLongArrayElements(env, larr, nullptr);
    ASSERT_NE(l_elems, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(l_elems) % alignof(portage::package::jlong), 0U);
    env->functions->ReleaseLongArrayElements(env, larr, l_elems, 0);

    auto darr = env->functions->NewDoubleArray(env, 3);
    ASSERT_NE(darr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, darr), 3);
    const portage::package::jdouble d_in[3] = {3.14159265358979, -2.718281828, 1.5};
    env->functions->SetDoubleArrayRegion(env, darr, 0, 3, d_in);
    portage::package::jdouble d_out[3] = {};
    env->functions->GetDoubleArrayRegion(env, darr, 0, 3, d_out);
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(d_out[i], d_in[i]);
    }
    auto *d_elems = env->functions->GetDoubleArrayElements(env, darr, nullptr);
    ASSERT_NE(d_elems, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(d_elems) % alignof(portage::package::jdouble), 0U);
    env->functions->ReleaseDoubleArrayElements(env, darr, d_elems, 0);

    auto carr = env->functions->NewCharArray(env, 6);
    ASSERT_NE(carr, nullptr);
    EXPECT_EQ(env->functions->GetArrayLength(env, carr), 6);
    const portage::package::jchar c_in[6] = {u'P', u'o', u'r', u't', u'a', u'g'};
    env->functions->SetCharArrayRegion(env, carr, 0, 6, c_in);
    portage::package::jchar c_out[6] = {};
    env->functions->GetCharArrayRegion(env, carr, 0, 6, c_out);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(c_out[i], c_in[i]);
    }
}
