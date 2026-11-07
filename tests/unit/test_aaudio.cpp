#include <gtest/gtest.h>

#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace portage::audio;

class AAudioTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        AudioManager::Instance().ForceNullSink(true);
        AudioManager::Instance().Reset();
    }

    void TearDown() override { AudioManager::Instance().Reset(); }
};

TEST_F(AAudioTest, ConfiguresStreamBuilderAndOpensStream)
{
    AAudioStreamBuilder *builder = nullptr;
    EXPECT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    ASSERT_NE(builder, nullptr);

    AAudioStreamBuilder_setSampleRate(builder, 44100);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setBufferCapacityInFrames(builder, 2048);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_NE(stream, nullptr);

    EXPECT_EQ(AAudioStream_getSampleRate(stream), 44100);
    EXPECT_EQ(AAudioStream_getChannelCount(stream), 2);
    EXPECT_EQ(AAudioStream_getFormat(stream), AAUDIO_FORMAT_PCM_I16);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_OPEN);

    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, WritesPcmFramesSynchronously)
{
    AAudioStreamBuilder *builder = nullptr;
    EXPECT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STARTED);

    std::vector<int16_t> pcm(512 * 2, 1000);
    int32_t written = AAudioStream_write(stream, pcm.data(), 512, 1000000);
    EXPECT_EQ(written, 512);

    auto stats = AudioManager::Instance().GetStats();
    EXPECT_GE(stats.total_frames_written, 512u);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STOPPED);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

static std::atomic<int32_t> g_callback_invocations{0};

static aaudio_data_callback_result_t __attribute__((sysv_abi))
TestAudioDataCallback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames)
{
    (void)stream;
    (void)userData;
    g_callback_invocations++;
    auto *s16 = static_cast<int16_t *>(audioData);
    for (int32_t i = 0; i < numFrames * 2; ++i) {
        s16[i] = 500;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

TEST_F(AAudioTest, DrivesStreamViaDataCallbackPump)
{
    g_callback_invocations = 0;

    AAudioStreamBuilder *builder = nullptr;
    EXPECT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 256);
    AAudioStreamBuilder_setDataCallback(builder, TestAudioDataCallback, nullptr);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STARTED);

    // Wait for a few periods to be processed by pump thread
    for (int i = 0; i < 50 && g_callback_invocations.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(g_callback_invocations.load(), 0);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STOPPED);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}
