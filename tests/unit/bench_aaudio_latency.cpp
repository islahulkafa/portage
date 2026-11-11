#include <gtest/gtest.h>

#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace portage::audio;

class AAudioBenchmark : public ::testing::Test {
  protected:
    void SetUp() override
    {
        AudioManager::Instance().ForceNullSink(true);
        AudioManager::Instance().Reset();
    }

    void TearDown() override { AudioManager::Instance().Reset(); }
};

namespace {

struct JitterCaptureState {
    static constexpr size_t kMaxSamples = 64;
    std::array<int64_t, kMaxSamples> timestamps_ns{};
    std::atomic<size_t> count{0};
};

aaudio_data_callback_result_t __attribute__((sysv_abi))
BenchmarkDataCallback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames)
{
    (void)stream;
    auto *state = static_cast<JitterCaptureState *>(userData);
    auto *pcm = static_cast<int16_t *>(audioData);
    for (int32_t i = 0; i < numFrames * 2; ++i) {
        pcm[i] = 0;
    }

    size_t idx = state->count.load(std::memory_order_relaxed);
    if (idx < JitterCaptureState::kMaxSamples) {
        int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        state->timestamps_ns[idx] = now_ns;
        state->count.store(idx + 1, std::memory_order_release);
    }

    if (idx + 1 >= 52) {
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace

TEST_F(AAudioBenchmark, CallbackJitterUnder5ms)
{
    JitterCaptureState capture{};

    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 256);
    AAudioStreamBuilder_setDataCallback(builder, BenchmarkDataCallback, &capture);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    ASSERT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);

    for (int i = 0; i < 200 && capture.count.load(std::memory_order_acquire) < 51; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);

    size_t collected = capture.count.load(std::memory_order_acquire);
    ASSERT_GE(collected, 51u);

    constexpr double kExpectedPeriodMs = (256.0 * 1000.0) / 48000.0;
    double max_jitter_ms = 0.0;
    for (size_t i = 1; i < 51; ++i) {
        double delta_ms =
            static_cast<double>(capture.timestamps_ns[i] - capture.timestamps_ns[i - 1]) /
            1000000.0;
        double jitter_ms = std::abs(delta_ms - kExpectedPeriodMs);
        if (jitter_ms > max_jitter_ms) {
            max_jitter_ms = jitter_ms;
        }
    }

    EXPECT_LT(max_jitter_ms, 5.0);
}

TEST_F(AAudioBenchmark, UnderrunRateZeroOnNullSink)
{
    AAudioStreamBuilder *builder = nullptr;
    ASSERT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    AAudioStream *stream = nullptr;
    ASSERT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);

    ASSERT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);

    std::vector<int16_t> period(256 * 2, 128);
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(AAudioStream_write(stream, period.data(), 256, 0), 256);
    }

    EXPECT_EQ(AAudioStream_getFramesWritten(stream), 25600);
    EXPECT_EQ(AAudioStream_getXRunCount(stream), 0);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
}
