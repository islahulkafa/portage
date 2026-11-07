#include "ndk/jnigraphics.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace portage::ndk {

using package::jobject;

class SurfaceRenderingTest : public ::testing::Test {
  protected:
    void SetUp() override { package::JniEnvironment::Instance().Reset(); }

    void TearDown() override { package::JniEnvironment::Instance().Reset(); }
};

TEST_F(SurfaceRenderingTest, LocksAndUnlocksNativeWindow)
{
    ANativeWindow win{1280, 720, 1, 1, nullptr};
    ANativeWindow_Buffer buffer{};
    ARect dirty{10, 10, 100, 100};

    int32_t lock_res = ANativeWindow_lock(&win, &buffer, &dirty);
    EXPECT_EQ(lock_res, 0);
    EXPECT_EQ(buffer.width, 1280);
    EXPECT_EQ(buffer.height, 720);
    EXPECT_GE(buffer.stride, 1280);
    EXPECT_EQ((buffer.stride * 4) % 64, 0);
    EXPECT_EQ(buffer.format, 1);
    ASSERT_NE(buffer.bits, nullptr);

    ANativeWindow_Buffer second_buf{};
    EXPECT_EQ(ANativeWindow_lock(&win, &second_buf, nullptr), -1);

    auto *pixels = static_cast<uint32_t *>(buffer.bits);
    pixels[0] = 0xAABBCCDD;
    pixels[100] = 0x11223344;

    int32_t unlock_res = ANativeWindow_unlockAndPost(&win);
    EXPECT_EQ(unlock_res, 0);
    EXPECT_FALSE(win.is_locked);
    EXPECT_GE(win.frontbuffer.size(), static_cast<size_t>(buffer.stride * buffer.height * 4));

    auto *front_pixels = reinterpret_cast<const uint32_t *>(win.frontbuffer.data());
    EXPECT_EQ(front_pixels[0], 0xAABBCCDD);
    EXPECT_EQ(front_pixels[100], 0x11223344);

    EXPECT_EQ(ANativeWindow_unlockAndPost(&win), -1);
}

TEST_F(SurfaceRenderingTest, SupportsMultiplePixelFormats)
{
    ANativeWindow win565{320, 240, 4, 1, nullptr};
    ANativeWindow_Buffer buf565{};
    EXPECT_EQ(ANativeWindow_lock(&win565, &buf565, nullptr), 0);
    EXPECT_EQ(buf565.format, 4);
    EXPECT_GE(buf565.stride, 320);
    EXPECT_EQ((buf565.stride * 2) % 64, 0);
    ASSERT_NE(buf565.bits, nullptr);

    auto *pixels16 = static_cast<uint16_t *>(buf565.bits);
    pixels16[0] = 0xF800; // Red in RGB_565
    EXPECT_EQ(ANativeWindow_unlockAndPost(&win565), 0);
    auto *front16 = reinterpret_cast<const uint16_t *>(win565.frontbuffer.data());
    EXPECT_EQ(front16[0], 0xF800);

    ANativeWindow winRGBX{640, 480, 2, 1, nullptr};
    ANativeWindow_Buffer bufRGBX{};
    EXPECT_EQ(ANativeWindow_lock(&winRGBX, &bufRGBX, nullptr), 0);
    EXPECT_EQ(bufRGBX.format, 2);
    EXPECT_GE(bufRGBX.stride, 640);
    EXPECT_EQ((bufRGBX.stride * 4) % 64, 0);
    EXPECT_EQ(ANativeWindow_unlockAndPost(&winRGBX), 0);
}

TEST_F(SurfaceRenderingTest, QueriesBitmapInfoAndLocksPixels)
{
    auto &env = package::JniEnvironment::Instance();
    jobject bmp = env.CreateBitmap(128, 64, ANDROID_BITMAP_FORMAT_RGBA_8888);
    ASSERT_NE(bmp, nullptr);

    AndroidBitmapInfo info{};
    int res = AndroidBitmap_getInfo(nullptr, bmp, &info);
    EXPECT_EQ(res, ANDROID_BITMAP_RESULT_SUCCESS);
    EXPECT_EQ(info.width, 128u);
    EXPECT_EQ(info.height, 64u);
    EXPECT_EQ(info.stride, 128u * 4);
    EXPECT_EQ(info.format, ANDROID_BITMAP_FORMAT_RGBA_8888);

    void *pixels = nullptr;
    res = AndroidBitmap_lockPixels(nullptr, bmp, &pixels);
    EXPECT_EQ(res, ANDROID_BITMAP_RESULT_SUCCESS);
    ASSERT_NE(pixels, nullptr);

    auto *pixel_u32 = static_cast<uint32_t *>(pixels);
    pixel_u32[0] = 0xFF445566;
    pixel_u32[127] = 0xFF778899;

    res = AndroidBitmap_unlockPixels(nullptr, bmp);
    EXPECT_EQ(res, ANDROID_BITMAP_RESULT_SUCCESS);

    EXPECT_EQ(AndroidBitmap_unlockPixels(nullptr, bmp), ANDROID_BITMAP_RESULT_BAD_PARAMETER);

    void *pixels2 = nullptr;
    res = AndroidBitmap_lockPixels(nullptr, bmp, &pixels2);
    EXPECT_EQ(res, ANDROID_BITMAP_RESULT_SUCCESS);
    EXPECT_EQ(pixels2, pixels);
    auto *pixel_u32_verify = static_cast<const uint32_t *>(pixels2);
    EXPECT_EQ(pixel_u32_verify[0], 0xFF445566u);
    EXPECT_EQ(pixel_u32_verify[127], 0xFF778899u);
    AndroidBitmap_unlockPixels(nullptr, bmp);
}

TEST_F(SurfaceRenderingTest, RejectsInvalidParameters)
{
    AndroidBitmapInfo info{};
    void *pixels = nullptr;

    EXPECT_EQ(AndroidBitmap_getInfo(nullptr, nullptr, &info), ANDROID_BITMAP_RESULT_BAD_PARAMETER);
    EXPECT_EQ(AndroidBitmap_lockPixels(nullptr, nullptr, &pixels),
              ANDROID_BITMAP_RESULT_BAD_PARAMETER);
    EXPECT_EQ(AndroidBitmap_unlockPixels(nullptr, nullptr), ANDROID_BITMAP_RESULT_BAD_PARAMETER);
    EXPECT_EQ(AndroidBitmap_getData(nullptr, nullptr, &pixels),
              ANDROID_BITMAP_RESULT_BAD_PARAMETER);

    ANativeWindow_Buffer buf{};
    EXPECT_EQ(ANativeWindow_lock(nullptr, &buf, nullptr), -1);
    EXPECT_EQ(ANativeWindow_unlockAndPost(nullptr), -1);
}

TEST_F(SurfaceRenderingTest, RegistersAllSymbols)
{
    const char *kSymbols[] = {
        "ANativeWindow_lock",       "ANativeWindow_unlockAndPost", "AndroidBitmap_getInfo",
        "AndroidBitmap_lockPixels", "AndroidBitmap_unlockPixels",  "AndroidBitmap_getData",
    };

    for (const char *name : kSymbols) {
        uintptr_t addr = NdkSymbolRegistry::Instance().Lookup(name);
        EXPECT_NE(addr, 0u) << "Symbol missing in NdkSymbolRegistry: " << name;
    }
}

} // namespace portage::ndk
