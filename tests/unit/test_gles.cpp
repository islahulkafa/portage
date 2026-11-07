#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <gtest/gtest.h>

using namespace portage::gles;

class GlesTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_NE(display_, EGL_NO_DISPLAY);

        EGLint major = 0, minor = 0;
        ASSERT_EQ(eglInitialize(display_, &major, &minor), EGL_TRUE);

        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint num_configs = 0;
        ASSERT_EQ(eglChooseConfig(display_, attribs, &config, 1, &num_configs), EGL_TRUE);

        ANativeWindow window{
            .width = 256, .height = 256, .format = 1, .ref_count = 1, .native_handle = nullptr};
        surface_ = eglCreateWindowSurface(display_, config,
                                          reinterpret_cast<EGLNativeWindowType>(&window), nullptr);
        ASSERT_NE(surface_, EGL_NO_SURFACE);

        const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctx_attribs);
        ASSERT_NE(context_, EGL_NO_CONTEXT);

        ASSERT_EQ(eglMakeCurrent(display_, surface_, surface_, context_), EGL_TRUE);
    }

    void TearDown() override
    {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT)
                eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE)
                eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
    }

    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLContext context_{EGL_NO_CONTEXT};
};

TEST_F(GlesTest, CompilesShaderAndInspectsStatus)
{
    const char *vs_src = "attribute vec4 a_pos;\n"
                         "void main() {\n"
                         "    gl_Position = a_pos;\n"
                         "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    ASSERT_NE(vs, 0U);

    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);

    GLint compiled = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
    EXPECT_EQ(compiled, 1);

    glDeleteShader(vs);
}

TEST_F(GlesTest, LinksProgramAndQueriesUniform)
{
    const char *vs_src = "uniform mat4 u_mvp;\n"
                         "attribute vec4 a_pos;\n"
                         "void main() {\n"
                         "    gl_Position = u_mvp * a_pos;\n"
                         "}\n";

    const char *fs_src = "precision mediump float;\n"
                         "uniform vec4 u_color;\n"
                         "void main() {\n"
                         "    gl_FragColor = u_color;\n"
                         "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);

    GLint compiled = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
    EXPECT_EQ(compiled, 1);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &compiled);
    EXPECT_EQ(compiled, 1);

    GLuint prog = glCreateProgram();
    ASSERT_NE(prog, 0U);

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    EXPECT_EQ(linked, 1);

    GLint u_mvp_loc = glGetUniformLocation(prog, "u_mvp");
    EXPECT_GE(u_mvp_loc, 0);

    GLint u_color_loc = glGetUniformLocation(prog, "u_color");
    EXPECT_GE(u_color_loc, 0);

    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

TEST_F(GlesTest, AllocatesBufferAndUploadsData)
{
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    ASSERT_NE(vbo, 0U);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    float data[] = {1.0f, 2.0f, 3.0f, 4.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
}

TEST_F(GlesTest, ClearsAndReadsBackPixels)
{
    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f); // Solid green
    glClear(GL_COLOR_BUFFER_BIT);

    uint8_t pixel[4] = {0, 0, 0, 0};
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    EXPECT_GE(pixel[1], 200); // Green channel is high
    EXPECT_LE(pixel[0], 50);  // Red channel is low
    EXPECT_LE(pixel[2], 50);  // Blue channel is low

    EXPECT_EQ(eglSwapBuffers(display_, surface_), EGL_TRUE);
}
