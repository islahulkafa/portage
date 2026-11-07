#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace portage::audio {

struct AAudioStreamBuilderStruct {
    int32_t sample_rate{48000};
    int32_t channel_count{2};
    aaudio_format_t format{AAUDIO_FORMAT_PCM_I16};
    aaudio_direction_t direction{AAUDIO_DIRECTION_OUTPUT};
    AAudioStream_dataCallback data_callback{nullptr};
    void *user_data{nullptr};
    AAudioStream_errorCallback error_callback{nullptr};
    void *error_user_data{nullptr};
    int32_t frames_per_callback{512};
    int32_t buffer_capacity{4096};
    aaudio_performance_mode_t performance_mode{AAUDIO_PERFORMANCE_MODE_LOW_LATENCY};
    aaudio_sharing_mode_t sharing_mode{AAUDIO_SHARING_MODE_SHARED};
};

struct AAudioStreamStruct {
    int32_t sample_rate{48000};
    int32_t channel_count{2};
    aaudio_format_t format{AAUDIO_FORMAT_PCM_I16};
    aaudio_direction_t direction{AAUDIO_DIRECTION_OUTPUT};
    AAudioStream_dataCallback data_callback{nullptr};
    void *user_data{nullptr};
    AAudioStream_errorCallback error_callback{nullptr};
    void *error_user_data{nullptr};
    int32_t frames_per_callback{512};
    int32_t buffer_capacity{4096};
    std::atomic<aaudio_stream_state_t> state{AAUDIO_STREAM_STATE_OPEN};

    std::thread pump_thread{};
    std::atomic<bool> pump_running{false};
    std::mutex pump_mutex{};
    std::condition_variable pump_cv{};

    ~AAudioStreamStruct() { StopPump(); }

    void StopPump()
    {
        pump_running = false;
        pump_cv.notify_all();
        if (pump_thread.joinable()) {
            if (pump_thread.get_id() != std::this_thread::get_id()) {
                pump_thread.join();
            }
        }
    }
};

extern "C" {

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder)
{
    if (!builder) {
        return AAUDIO_ERROR_NULL;
    }
    *builder = new AAudioStreamBuilderStruct();
    return AAUDIO_OK;
}

void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *builder, int32_t sampleRate)
{
    if (builder && sampleRate > 0) {
        builder->sample_rate = sampleRate;
    }
}

void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channelCount)
{
    if (builder && channelCount >= 1 && channelCount <= 2) {
        builder->channel_count = channelCount;
    }
}

void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, aaudio_format_t format)
{
    if (builder) {
        builder->format = format;
    }
}

void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *builder, aaudio_direction_t direction)
{
    if (builder) {
        builder->direction = direction;
    }
}

void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder,
                                         AAudioStream_dataCallback callback, void *userData)
{
    if (builder) {
        builder->data_callback = callback;
        builder->user_data = userData;
    }
}

void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *builder,
                                          AAudioStream_errorCallback callback, void *userData)
{
    if (builder) {
        builder->error_callback = callback;
        builder->error_user_data = userData;
    }
}

void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *builder, int32_t frames)
{
    if (builder && frames > 0) {
        builder->frames_per_callback = frames;
    }
}

void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder *builder, int32_t numFrames)
{
    if (builder && numFrames > 0) {
        builder->buffer_capacity = numFrames;
    }
}

void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder,
                                            aaudio_performance_mode_t mode)
{
    if (builder) {
        builder->performance_mode = mode;
    }
}

void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *builder, aaudio_sharing_mode_t mode)
{
    if (builder) {
        builder->sharing_mode = mode;
    }
}

aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream)
{
    if (!builder || !stream) {
        return AAUDIO_ERROR_NULL;
    }

    auto *st = new AAudioStreamStruct();
    st->sample_rate = builder->sample_rate;
    st->channel_count = builder->channel_count;
    st->format = (builder->format == AAUDIO_FORMAT_PCM_FLOAT) ? AAUDIO_FORMAT_PCM_FLOAT
                                                              : AAUDIO_FORMAT_PCM_I16;
    st->direction = builder->direction;
    st->data_callback = builder->data_callback;
    st->user_data = builder->user_data;
    st->error_callback = builder->error_callback;
    st->error_user_data = builder->error_user_data;
    st->frames_per_callback = builder->frames_per_callback;
    st->buffer_capacity = builder->buffer_capacity;
    st->state = AAUDIO_STREAM_STATE_OPEN;

    *stream = st;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder)
{
    if (!builder) {
        return AAUDIO_ERROR_NULL;
    }
    delete builder;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestStart(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }

    if (stream->state.load() == AAUDIO_STREAM_STATE_STARTED) {
        return AAUDIO_OK;
    }

    int32_t alsa_format = (stream->format == AAUDIO_FORMAT_PCM_FLOAT) ? SNDRV_PCM_FORMAT_FLOAT_LE
                                                                      : SNDRV_PCM_FORMAT_S16_LE;
    AudioManager::Instance().ConfigureStream(stream->sample_rate, stream->channel_count,
                                             alsa_format);

    stream->state.store(AAUDIO_STREAM_STATE_STARTED);

    if (stream->data_callback) {
        stream->StopPump();
        stream->pump_running = true;

        stream->pump_thread = std::thread([stream]() {
            int32_t sample_bytes = (stream->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4 : 2;
            int32_t frame_size = stream->channel_count * sample_bytes;
            int32_t frames = stream->frames_per_callback;
            if (frames <= 0) {
                frames = 512;
            }
            std::vector<uint8_t> buffer(frames * frame_size, 0);

            auto period_duration = std::chrono::microseconds(
                (static_cast<uint64_t>(frames) * 1000000ULL) / stream->sample_rate);

            while (stream->pump_running.load() &&
                   stream->state.load() == AAUDIO_STREAM_STATE_STARTED) {
                std::fill(buffer.begin(), buffer.end(), 0);

                aaudio_data_callback_result_t cb_res =
                    stream->data_callback(stream, stream->user_data, buffer.data(), frames);

                if (cb_res == AAUDIO_CALLBACK_RESULT_CONTINUE) {
                    AudioManager::Instance().WritePcmBytes(buffer.data(), buffer.size());
                } else {
                    stream->state.store(AAUDIO_STREAM_STATE_STOPPED);
                    break;
                }

                std::unique_lock<std::mutex> lock(stream->pump_mutex);
                stream->pump_cv.wait_for(lock, period_duration,
                                         [stream] { return !stream->pump_running.load(); });
            }
        });
    }

    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestPause(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    stream->StopPump();
    stream->state.store(AAUDIO_STREAM_STATE_PAUSED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestStop(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    stream->StopPump();
    stream->state.store(AAUDIO_STREAM_STATE_STOPPED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_close(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    stream->StopPump();
    stream->state.store(AAUDIO_STREAM_STATE_CLOSED);
    delete stream;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_write(AAudioStream *stream, const void *buffer, int32_t numFrames,
                                   int64_t timeoutNanoseconds)
{
    (void)timeoutNanoseconds;
    if (!stream || !buffer) {
        return AAUDIO_ERROR_NULL;
    }
    if (numFrames <= 0) {
        return 0;
    }

    if (stream->state.load() != AAUDIO_STREAM_STATE_STARTED) {
        int32_t alsa_format = (stream->format == AAUDIO_FORMAT_PCM_FLOAT)
                                  ? SNDRV_PCM_FORMAT_FLOAT_LE
                                  : SNDRV_PCM_FORMAT_S16_LE;
        AudioManager::Instance().ConfigureStream(stream->sample_rate, stream->channel_count,
                                                 alsa_format);
        stream->state.store(AAUDIO_STREAM_STATE_STARTED);
    }

    int32_t sample_bytes = (stream->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4 : 2;
    size_t byte_count = static_cast<size_t>(numFrames) * stream->channel_count * sample_bytes;
    int64_t written = AudioManager::Instance().WritePcmBytes(buffer, byte_count);
    if (written < 0) {
        return AAUDIO_ERROR_INTERNAL;
    }
    return numFrames;
}

int32_t AAudioStream_getSampleRate(AAudioStream *stream)
{
    return stream ? stream->sample_rate : 48000;
}

int32_t AAudioStream_getChannelCount(AAudioStream *stream)
{
    return stream ? stream->channel_count : 2;
}

aaudio_format_t AAudioStream_getFormat(AAudioStream *stream)
{
    return stream ? stream->format : AAUDIO_FORMAT_PCM_I16;
}

aaudio_stream_state_t AAudioStream_getState(AAudioStream *stream)
{
    return stream ? stream->state.load() : AAUDIO_STREAM_STATE_UNINITIALIZED;
}

int32_t AAudioStream_getFramesPerDataCallback(AAudioStream *stream)
{
    return stream ? stream->frames_per_callback : 512;
}

int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *stream)
{
    return stream ? stream->buffer_capacity : 4096;
}
}

} // namespace portage::audio
