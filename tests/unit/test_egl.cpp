#include "gles/egl_dispatcher.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <gtest/gtest.h>

using namespace portage::gles;

class EglTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_NE(display_, EGL_NO_DISPLAY);

        EGLint major = 0, minor = 0;
        ASSERT_EQ(eglInitialize(display_, &major, &minor), EGL_TRUE);
        EXPECT_EQ(major, 1);
        EXPECT_EQ(minor, 4);
    }

    void TearDown() override
    {
        if (display_ != EGL_NO_DISPLAY) {
            eglTerminate(display_);
        }
    }

    EGLDisplay display_{EGL_NO_DISPLAY};
};

TEST_F(EglTest, InitializesAndQueriesEglDisplay)
{
    const char *vendor = eglQueryString(display_, EGL_VENDOR);
    EXPECT_NE(vendor, nullptr);
    EXPECT_STRNE(vendor, "");

    const char *version = eglQueryString(display_, EGL_VERSION);
    EXPECT_NE(version, nullptr);
    EXPECT_STRNE(version, "");

    const char *apis = eglQueryString(display_, EGL_CLIENT_APIS);
    EXPECT_NE(apis, nullptr);
    EXPECT_STREQ(apis, "OpenGL_ES");
}

TEST_F(EglTest, ChoosesConfigAndQueriesAttributes)
{
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_ES2_BIT,
                              EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_NONE};

    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    EXPECT_EQ(eglChooseConfig(display_, attribs, &config, 1, &num_configs), EGL_TRUE);
    EXPECT_GT(num_configs, 0);
    EXPECT_NE(config, nullptr);

    EGLint red = 0, green = 0, blue = 0, alpha = 0;
    EXPECT_EQ(eglGetConfigAttrib(display_, config, EGL_RED_SIZE, &red), EGL_TRUE);
    EXPECT_EQ(eglGetConfigAttrib(display_, config, EGL_GREEN_SIZE, &green), EGL_TRUE);
    EXPECT_EQ(eglGetConfigAttrib(display_, config, EGL_BLUE_SIZE, &blue), EGL_TRUE);
    EXPECT_EQ(eglGetConfigAttrib(display_, config, EGL_ALPHA_SIZE, &alpha), EGL_TRUE);

    EXPECT_EQ(red, 8);
    EXPECT_EQ(green, 8);
    EXPECT_EQ(blue, 8);
    EXPECT_EQ(alpha, 8);
}

TEST_F(EglTest, CreatesAndDestroysContextAndSurface)
{
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};

    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    ASSERT_EQ(eglChooseConfig(display_, attribs, &config, 1, &num_configs), EGL_TRUE);

    ANativeWindow window{
        .width = 800, .height = 600, .format = 1, .ref_count = 1, .native_handle = nullptr};
    EGLSurface surface = eglCreateWindowSurface(
        display_, config, reinterpret_cast<EGLNativeWindowType>(&window), nullptr);
    EXPECT_NE(surface, EGL_NO_SURFACE);

    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctx_attribs);
    EXPECT_NE(context, EGL_NO_CONTEXT);

    EXPECT_EQ(eglMakeCurrent(display_, surface, surface, context), EGL_TRUE);
    EXPECT_EQ(eglGetCurrentContext(), context);
    EXPECT_EQ(eglGetCurrentSurface(EGL_DRAW), surface);

    EXPECT_EQ(eglSwapBuffers(display_, surface), EGL_TRUE);

    EXPECT_EQ(eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), EGL_TRUE);
    EXPECT_EQ(eglDestroyContext(display_, context), EGL_TRUE);
    EXPECT_EQ(eglDestroySurface(display_, surface), EGL_TRUE);
}

TEST_F(EglTest, ResolvesProcAddresses)
{
    void *p_clear = eglGetProcAddress("glClear");
    EXPECT_NE(p_clear, nullptr);

    void *p_shader = eglGetProcAddress("glCreateShader");
    EXPECT_NE(p_shader, nullptr);

    void *p_draw = eglGetProcAddress("glDrawArrays");
    EXPECT_NE(p_draw, nullptr);

    void *p_invalid = eglGetProcAddress("glNonExistentFunction12345");
    EXPECT_EQ(p_invalid, nullptr);
}
