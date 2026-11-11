#include "fs/fd_table.hpp"
#include "gles/egl_dispatcher.hpp"
#include "gles/gles_dispatcher.hpp"
#include "ndk/hardware_buffer.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "poll/epoll_manager.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace portage;
using namespace portage::gles;

class ModernGlesAndSyncTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_NE(display_, EGL_NO_DISPLAY);

        ASSERT_EQ(eglInitialize(display_, &major_, &minor_), EGL_TRUE);

        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint num_configs = 0;
        ASSERT_EQ(eglChooseConfig(display_, attribs, &config, 1, &num_configs), EGL_TRUE);

        ANativeWindow window{
            .width = 64, .height = 64, .format = 1, .ref_count = 1, .native_handle = nullptr};
        surface_ = eglCreateWindowSurface(display_, config,
                                          reinterpret_cast<EGLNativeWindowType>(&window), nullptr);
        ASSERT_NE(surface_, EGL_NO_SURFACE);

        const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctx_attribs);
        ASSERT_NE(context_, EGL_NO_CONTEXT);

        ASSERT_EQ(eglMakeCurrent(display_, surface_, surface_, context_), EGL_TRUE);
    }

    void TearDown() override
    {
        EglDispatcher::Instance().SetFdTable(nullptr);
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
            }
            if (surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, surface_);
            }
            eglTerminate(display_);
        }
    }

    EGLint major_{0};
    EGLint minor_{0};
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLContext context_{EGL_NO_CONTEXT};
};

TEST_F(ModernGlesAndSyncTest, EglSyncFenceLifecycle)
{
    EXPECT_EQ(major_, 1);
    EXPECT_EQ(minor_, 5);

    const char *exts = eglQueryString(display_, EGL_EXTENSIONS);
    ASSERT_NE(exts, nullptr);
    std::string ext_str(exts);
    EXPECT_NE(ext_str.find("EGL_KHR_fence_sync"), std::string::npos);
    EXPECT_NE(ext_str.find("EGL_ANDROID_native_fence_sync"), std::string::npos);

    EGLSync sync = eglCreateSync(display_, EGL_SYNC_FENCE, nullptr);
    ASSERT_NE(sync, EGL_NO_SYNC);

    EGLint wait_res = eglClientWaitSync(display_, sync, EGL_SYNC_FLUSH_COMMANDS_BIT, EGL_FOREVER);
    EXPECT_EQ(wait_res, EGL_CONDITION_SATISFIED);

    EGLAttrib status = 0;
    EXPECT_EQ(eglGetSyncAttrib(display_, sync, EGL_SYNC_STATUS, &status), EGL_TRUE);
    EXPECT_EQ(status, EGL_SIGNALED);

    EGLAttrib type = 0;
    EXPECT_EQ(eglGetSyncAttrib(display_, sync, EGL_SYNC_TYPE, &type), EGL_TRUE);
    EXPECT_EQ(static_cast<EGLenum>(type), EGL_SYNC_FENCE);

    EXPECT_EQ(eglDestroySync(display_, sync), EGL_TRUE);

    EGLSyncKHR sync_khr = eglCreateSyncKHR(display_, EGL_SYNC_FENCE_KHR, nullptr);
    ASSERT_NE(sync_khr, EGL_NO_SYNC_KHR);
    EXPECT_EQ(eglWaitSyncKHR(display_, sync_khr, 0), EGL_TRUE);
    EXPECT_EQ(
        eglClientWaitSyncKHR(display_, sync_khr, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR),
        EGL_CONDITION_SATISFIED_KHR);
    EGLint khr_status = 0;
    EXPECT_EQ(eglGetSyncAttribKHR(display_, sync_khr, EGL_SYNC_STATUS_KHR, &khr_status), EGL_TRUE);
    EXPECT_EQ(khr_status, EGL_SIGNALED_KHR);
    EXPECT_EQ(eglDestroySyncKHR(display_, sync_khr), EGL_TRUE);
}

TEST_F(ModernGlesAndSyncTest, EglDupNativeFenceFdAndEpoll)
{
    fs::FileDescriptorTable fd_table;
    EglDispatcher::Instance().SetFdTable(&fd_table);

    EGLSyncKHR sync = eglCreateSyncKHR(display_, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    ASSERT_NE(sync, EGL_NO_SYNC_KHR);

    int fence_fd = eglDupNativeFenceFDANDROID(display_, sync);
    ASSERT_GE(fence_fd, 0);

    auto desc = fd_table.Get(fence_fd);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->dev, fs::DeviceType::SyncFence);
    ASSERT_NE(desc->sync_fence, nullptr);
    desc->sync_fence->Signal();

    const int epoll_id = poll::EpollManager::Instance().CreateInstance();
    ASSERT_GE(epoll_id, 0);

    poll::LinuxEpollEvent ev{};
    ev.events = poll::LINUX_EPOLLIN;
    ev.data = static_cast<uint64_t>(fence_fd);
    ASSERT_EQ(
        poll::EpollManager::Instance().Ctl(epoll_id, poll::LINUX_EPOLL_CTL_ADD, fence_fd, &ev), 0);

    poll::LinuxEpollEvent out_ev{};
    int ready = poll::EpollManager::Instance().Wait(epoll_id, &out_ev, 1, 50, &fd_table);
    EXPECT_EQ(ready, 1);
    EXPECT_EQ(out_ev.data, static_cast<uint64_t>(fence_fd));
    EXPECT_NE(out_ev.events & poll::LINUX_EPOLLIN, 0u);

    poll::LinuxPollFd pfd{.fd = fence_fd, .events = poll::LINUX_POLLIN, .revents = 0};
    EXPECT_EQ(poll::EpollManager::Instance().Poll(&pfd, 1, 10, &fd_table), 1);
    EXPECT_NE(pfd.revents & poll::LINUX_POLLIN, 0);

    const EGLint import_attribs[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fence_fd, EGL_NONE};
    EGLSyncKHR imported = eglCreateSyncKHR(display_, EGL_SYNC_NATIVE_FENCE_ANDROID, import_attribs);
    ASSERT_NE(imported, EGL_NO_SYNC_KHR);
    EGLint imported_status = 0;
    EXPECT_EQ(eglGetSyncAttribKHR(display_, imported, EGL_SYNC_STATUS_KHR, &imported_status),
              EGL_TRUE);
    EXPECT_EQ(imported_status, EGL_SIGNALED_KHR);

    poll::EpollManager::Instance().CloseInstance(epoll_id);
    eglDestroySyncKHR(display_, imported);
    eglDestroySyncKHR(display_, sync);
    fd_table.Close(fence_fd);
}

TEST_F(ModernGlesAndSyncTest, GlesFenceSyncAndClientWait)
{
    EXPECT_NE(eglGetProcAddress("glFenceSync"), nullptr);
    EXPECT_NE(eglGetProcAddress("glClientWaitSync"), nullptr);
    EXPECT_NE(eglGetProcAddress("glGetSynciv"), nullptr);

    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    ASSERT_NE(sync, nullptr);
    EXPECT_EQ(glIsSync(sync), GL_TRUE);

    GLenum wait_res = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    EXPECT_TRUE(wait_res == GL_ALREADY_SIGNALED || wait_res == GL_CONDITION_SATISFIED);

    GLint status = 0;
    GLsizei len = 0;
    glGetSynciv(sync, GL_SYNC_STATUS, 1, &len, &status);
    EXPECT_EQ(status, static_cast<GLint>(GL_SIGNALED));

    glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(sync);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, UniformBufferObjectBinding)
{
    EXPECT_NE(eglGetProcAddress("glBindBufferBase"), nullptr);
    EXPECT_NE(eglGetProcAddress("glBindBufferRange"), nullptr);
    EXPECT_NE(eglGetProcAddress("glGetUniformBlockIndex"), nullptr);
    EXPECT_NE(eglGetProcAddress("glUniformBlockBinding"), nullptr);

    GLuint ubo = 0;
    glGenBuffers(1, &ubo);
    ASSERT_NE(ubo, 0u);

    const float block_data[8] = {1.0f, 0.5f, 0.25f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(block_data), block_data, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    glBindBufferRange(GL_UNIFORM_BUFFER, 1, ubo, 0, sizeof(float) * 4);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glDeleteBuffers(1, &ubo);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, InstancedDrawingParameters)
{
    EXPECT_NE(eglGetProcAddress("glDrawArraysInstanced"), nullptr);
    EXPECT_NE(eglGetProcAddress("glDrawElementsInstanced"), nullptr);
    EXPECT_NE(eglGetProcAddress("glVertexAttribDivisor"), nullptr);

    GLuint vao = 0;
    GLuint vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    const float vertices[] = {0.0f, 0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glVertexAttribDivisor(0, 1);

    constexpr GLenum kGlTriangles = 0x0004;
    glDrawArraysInstanced(kGlTriangles, 0, 3, 4);

    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, MapBufferRangeLifecycle)
{
    EXPECT_NE(eglGetProcAddress("glMapBufferRange"), nullptr);
    EXPECT_NE(eglGetProcAddress("glUnmapBuffer"), nullptr);
    EXPECT_NE(eglGetProcAddress("glFlushMappedBufferRange"), nullptr);

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    ASSERT_NE(vbo, 0u);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 64, nullptr, GL_DYNAMIC_DRAW);

    void *mapped =
        glMapBufferRange(GL_ARRAY_BUFFER, 0, 64, GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT);
    if (mapped) {
        auto *fdata = static_cast<float *>(mapped);
        fdata[0] = 0.25f;
        fdata[1] = 0.75f;
        glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, sizeof(float) * 2);
        EXPECT_EQ(glUnmapBuffer(GL_ARRAY_BUFFER), GL_TRUE);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, SamplerObjectsAndFiltering)
{
    EXPECT_NE(eglGetProcAddress("glGenSamplers"), nullptr);
    EXPECT_NE(eglGetProcAddress("glBindSampler"), nullptr);
    EXPECT_NE(eglGetProcAddress("glSamplerParameteri"), nullptr);

    GLuint sampler = 0;
    glGenSamplers(1, &sampler);
    ASSERT_NE(sampler, 0u);

    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_REPEAT));
    glBindSampler(0, sampler);
    glBindSampler(0, 0);
    glDeleteSamplers(1, &sampler);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, TextureStorageAnd3DAllocation)
{
    EXPECT_NE(eglGetProcAddress("glTexStorage2D"), nullptr);
    EXPECT_NE(eglGetProcAddress("glTexStorage3D"), nullptr);
    EXPECT_NE(eglGetProcAddress("glTexSubImage3D"), nullptr);

    GLuint textures[2] = {0, 0};
    glGenTextures(2, textures);
    ASSERT_NE(textures[0], 0u);
    ASSERT_NE(textures[1], 0u);

    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 16, 16);

    glBindTexture(GL_TEXTURE_3D, textures[1]);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 2, 2, 2);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    std::vector<uint8_t> voxels(2 * 2 * 2 * 4, 0x80);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 2, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, voxels.data());

    glDeleteTextures(2, textures);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ModernGlesAndSyncTest, HardwareBufferEglImageTextureBinding)
{
    EXPECT_NE(eglGetProcAddress("glEGLImageTargetTexture2DOES"), nullptr);

    AHardwareBuffer_Desc desc{
        .width = 4,
        .height = 4,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
    };
    AHardwareBuffer *ahb = nullptr;
    ASSERT_EQ(AHardwareBuffer_allocate(&desc, &ahb), AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(ahb, nullptr);

    void *ptr = nullptr;
    ASSERT_EQ(AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &ptr),
              AHARDWAREBUFFER_STATUS_OK);
    ASSERT_NE(ptr, nullptr);
    auto *pixels = static_cast<uint8_t *>(ptr);
    for (uint32_t y = 0; y < desc.height; ++y) {
        for (uint32_t x = 0; x < desc.width; ++x) {
            size_t idx = (static_cast<size_t>(y) * ahb->desc.stride + x) * 4;
            pixels[idx + 0] = 0x22;
            pixels[idx + 1] = 0x88;
            pixels[idx + 2] = 0xCC;
            pixels[idx + 3] = 0xFF;
        }
    }
    ASSERT_EQ(AHardwareBuffer_unlock(ahb, nullptr), AHARDWAREBUFFER_STATUS_OK);

    EGLClientBuffer client_buf = eglGetNativeClientBufferANDROID(ahb);
    ASSERT_NE(client_buf, nullptr);

    EGLImageKHR image =
        eglCreateImageKHR(display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client_buf, nullptr);
    ASSERT_NE(image, EGL_NO_IMAGE_KHR);
    ASSERT_NE(EglDispatcher::Instance().FindImage(image), nullptr);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    ASSERT_NE(tex, 0u);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    glDeleteTextures(1, &tex);
    EXPECT_EQ(eglDestroyImageKHR(display_, image), EGL_TRUE);
    EXPECT_EQ(EglDispatcher::Instance().FindImage(image), nullptr);
    AHardwareBuffer_release(ahb);
}

TEST_F(ModernGlesAndSyncTest, EglSwapIntervalVsync)
{
    EXPECT_NE(eglGetProcAddress("eglSwapInterval"), nullptr);

    EXPECT_EQ(eglSwapInterval(display_, 0), EGL_TRUE);
    EXPECT_EQ(EglDispatcher::Instance().GetSwapInterval(), 0);

    EXPECT_EQ(eglSwapInterval(display_, 1), EGL_TRUE);
    EXPECT_EQ(EglDispatcher::Instance().GetSwapInterval(), 1);
}
