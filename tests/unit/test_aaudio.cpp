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

TEST_F(AAudioTest, PausesAndResumesStream)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STARTED);

    EXPECT_EQ(AAudioStream_requestPause(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_PAUSED);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STARTED);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STOPPED);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, FlushesStreamFromPausedState)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_requestFlush(stream), AAUDIO_ERROR_INVALID_STATE);

    EXPECT_EQ(AAudioStream_requestPause(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_PAUSED);

    EXPECT_EQ(AAudioStream_requestFlush(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_FLUSHED);

    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, WaitForStateChangeReturnsAfterStart)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    aaudio_stream_state_t timeout_next = AAUDIO_STREAM_STATE_UNKNOWN;
    EXPECT_EQ(AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_OPEN, &timeout_next, 0),
              AAUDIO_ERROR_TIMEOUT);

    aaudio_stream_state_t next_state = AAUDIO_STREAM_STATE_UNKNOWN;
    aaudio_result_t wait_result = AAUDIO_ERROR_INTERNAL;
    std::thread waiter([stream, &next_state, &wait_result]() {
        wait_result = AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_OPEN, &next_state,
                                                      1000000000LL);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    waiter.join();

    EXPECT_EQ(wait_result, AAUDIO_OK);
    EXPECT_EQ(next_state, AAUDIO_STREAM_STATE_STARTED);

    aaudio_stream_state_t sync_next = AAUDIO_STREAM_STATE_UNKNOWN;
    EXPECT_EQ(
        AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_OPEN, &sync_next, 1000000000LL),
        AAUDIO_OK);
    EXPECT_EQ(sync_next, AAUDIO_STREAM_STATE_STARTED);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, FrameCountersAdvanceAfterWrite)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_getFramesWritten(stream), 0);
    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);

    std::vector<int16_t> samples(512 * 2, 300);
    EXPECT_EQ(AAudioStream_write(stream, samples.data(), 512, 0), 512);
    EXPECT_GE(AAudioStream_getFramesWritten(stream), 512);

    int64_t frame_pos = -1;
    int64_t time_ns = -1;
    EXPECT_EQ(AAudioStream_getTimestamp(stream, 1, &frame_pos, &time_ns), AAUDIO_OK);
    EXPECT_GE(frame_pos, 0);
    EXPECT_GT(time_ns, 0);
    EXPECT_GE(AAudioStream_getFramesRead(stream), 0);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, XRunCountStartsAtZero)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_getXRunCount(stream), 0);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, GetPerformanceModeMatchesBuilder)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_getPerformanceMode(stream), AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    EXPECT_EQ(AAudioStream_getDeviceId(stream), 0);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, GetSharingModeMatchesBuilder)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    EXPECT_EQ(AAudioStream_getSharingMode(stream), AAUDIO_SHARING_MODE_SHARED);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

TEST_F(AAudioTest, UsageAndContentTypeHintsAccepted)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_NE(stream, nullptr);
    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}

static std::atomic<int> g_error_callback_count{0};
static std::atomic<aaudio_result_t> g_last_error_code{AAUDIO_OK};

static void __attribute__((sysv_abi)) TestAudioErrorCallback(AAudioStream *stream, void *userData,
                                                             aaudio_result_t error)
{
    (void)stream;
    (void)userData;
    g_last_error_code.store(error);
    g_error_callback_count.fetch_add(1);
}

TEST_F(AAudioTest, ErrorCallbackFiredOnDisconnect)
{
    g_error_callback_count.store(0);
    g_last_error_code.store(AAUDIO_OK);

    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setErrorCallback(builder, TestAudioErrorCallback, nullptr);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    ASSERT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    SimulateStreamError(stream, AAUDIO_ERROR_DISCONNECTED);

    EXPECT_EQ(g_error_callback_count.load(), 1);
    EXPECT_EQ(g_last_error_code.load(), AAUDIO_ERROR_DISCONNECTED);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_DISCONNECTED);

    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}
