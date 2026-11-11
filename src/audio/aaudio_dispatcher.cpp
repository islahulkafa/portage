#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace portage::audio {

namespace {

inline constexpr DWORD PORTAGE_WAITABLE_TIMER_HIGH_RES = 0x00000002;
inline constexpr int32_t GUEST_CLOCK_MONOTONIC = 1;
inline constexpr int32_t GUEST_CLOCK_BOOTTIME = 7;

int64_t ScaleFrames(uint64_t count, uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return static_cast<int64_t>((count / den) * num + ((count % den) * num) / den);
}

int64_t MonotonicNowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

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
    aaudio_usage_t usage{AAUDIO_USAGE_MEDIA};
    aaudio_content_type_t content_type{AAUDIO_CONTENT_TYPE_MUSIC};
    aaudio_input_preset_t input_preset{AAUDIO_INPUT_PRESET_GENERIC};
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
    aaudio_performance_mode_t performance_mode{AAUDIO_PERFORMANCE_MODE_LOW_LATENCY};
    aaudio_sharing_mode_t sharing_mode{AAUDIO_SHARING_MODE_SHARED};
    aaudio_usage_t usage{AAUDIO_USAGE_MEDIA};
    aaudio_content_type_t content_type{AAUDIO_CONTENT_TYPE_MUSIC};
    aaudio_input_preset_t input_preset{AAUDIO_INPUT_PRESET_GENERIC};
    int32_t device_id{0};

    std::atomic<aaudio_stream_state_t> state{AAUDIO_STREAM_STATE_OPEN};
    std::atomic<int64_t> frames_written{0};
    std::atomic<int64_t> frames_read{0};
    std::atomic<int32_t> xrun_count{0};
    uint32_t last_mgr_xrun{0};

    std::mutex state_mutex{};
    std::condition_variable state_cv{};

    std::thread pump_thread{};
    std::atomic<bool> pump_running{false};
    std::mutex pump_mutex{};
    std::condition_variable pump_cv{};
    HANDLE pump_stop_event{nullptr};

    AAudioStreamStruct() { pump_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

    ~AAudioStreamStruct()
    {
        StopPump();
        if (pump_stop_event) {
            CloseHandle(pump_stop_event);
            pump_stop_event = nullptr;
        }
    }

    void SetState(aaudio_stream_state_t next)
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            state.store(next);
        }
        state_cv.notify_all();
    }

    void SyncXRunDelta()
    {
        uint32_t current = AudioManager::Instance().GetXRunCount();
        if (current > last_mgr_xrun) {
            xrun_count.fetch_add(static_cast<int32_t>(current - last_mgr_xrun));
        }
        last_mgr_xrun = current;
    }

    void StopPump()
    {
        pump_running = false;
        pump_cv.notify_all();
        if (pump_stop_event) {
            SetEvent(pump_stop_event);
        }
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

void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *builder, aaudio_usage_t usage)
{
    if (builder) {
        builder->usage = usage;
    }
}

void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *builder,
                                        aaudio_content_type_t contentType)
{
    if (builder) {
        builder->content_type = contentType;
    }
}

void AAudioStreamBuilder_setInputPreset(AAudioStreamBuilder *builder,
                                        aaudio_input_preset_t inputPreset)
{
    if (builder) {
        builder->input_preset = inputPreset;
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
    st->performance_mode = builder->performance_mode;
    st->sharing_mode = builder->sharing_mode;
    st->usage = builder->usage;
    st->content_type = builder->content_type;
    st->input_preset = builder->input_preset;
    st->last_mgr_xrun = AudioManager::Instance().GetXRunCount();
    st->SetState(AAUDIO_STREAM_STATE_OPEN);

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

    aaudio_stream_state_t current = stream->state.load();
    if (current == AAUDIO_STREAM_STATE_STARTED) {
        return AAUDIO_OK;
    }
    if (current == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    if (current == AAUDIO_STREAM_STATE_CLOSED || current == AAUDIO_STREAM_STATE_CLOSING) {
        return AAUDIO_ERROR_INVALID_STATE;
    }

    int32_t alsa_format = (stream->format == AAUDIO_FORMAT_PCM_FLOAT) ? SNDRV_PCM_FORMAT_FLOAT_LE
                                                                      : SNDRV_PCM_FORMAT_S16_LE;
    bool low_latency = (stream->performance_mode == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        AudioManager::Instance().ConfigureCaptureStream(stream->sample_rate, stream->channel_count,
                                                        alsa_format);
        AudioManager::Instance().StartCapture();
    } else {
        AudioManager::Instance().ConfigureStream(stream->sample_rate, stream->channel_count,
                                                 alsa_format, low_latency);
    }

    stream->last_mgr_xrun = AudioManager::Instance().GetXRunCount();
    stream->SetState(AAUDIO_STREAM_STATE_STARTED);

    if (stream->data_callback) {
        stream->StopPump();
        if (stream->pump_stop_event) {
            ResetEvent(stream->pump_stop_event);
        }
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
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, PORTAGE_WAITABLE_TIMER_HIGH_RES,
                                                  TIMER_ALL_ACCESS);
            auto next_wakeup = std::chrono::steady_clock::now();

            while (stream->pump_running.load() &&
                   stream->state.load() == AAUDIO_STREAM_STATE_STARTED) {
                if (stream->direction == AAUDIO_DIRECTION_INPUT) {
                    int64_t read_frames = AudioManager::Instance().ReadiFrames(
                        buffer.data(), frames, period_duration.count() * 1000);
                    if (read_frames <= 0) {
                        std::fill(buffer.begin(), buffer.end(), 0);
                    } else {
                        stream->frames_read.fetch_add(read_frames);
                    }
                    aaudio_data_callback_result_t cb_res =
                        stream->data_callback(stream, stream->user_data, buffer.data(), frames);
                    if (cb_res != AAUDIO_CALLBACK_RESULT_CONTINUE) {
                        stream->SetState(AAUDIO_STREAM_STATE_STOPPED);
                        break;
                    }
                } else {
                    std::fill(buffer.begin(), buffer.end(), 0);

                    aaudio_data_callback_result_t cb_res =
                        stream->data_callback(stream, stream->user_data, buffer.data(), frames);

                    if (cb_res == AAUDIO_CALLBACK_RESULT_CONTINUE) {
                        AudioManager::Instance().WritePcmBytes(buffer.data(), buffer.size());
                        stream->frames_written.fetch_add(frames);
                        stream->SyncXRunDelta();
                    } else {
                        stream->SetState(AAUDIO_STREAM_STATE_STOPPED);
                        break;
                    }
                }

                next_wakeup += period_duration;
                auto now = std::chrono::steady_clock::now();
                if (now > next_wakeup + (period_duration * 2)) {
                    next_wakeup = now + period_duration;
                }
                int64_t remaining_ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(next_wakeup - now).count();
                if (remaining_ns > 0) {
                    if (timer && stream->pump_stop_event) {
                        LARGE_INTEGER due{};
                        int64_t ticks_100ns = std::max<int64_t>(1, remaining_ns / 100);
                        due.QuadPart = -ticks_100ns;
                        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                            HANDLE handles[2] = {timer, stream->pump_stop_event};
                            WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                        } else {
                            std::unique_lock<std::mutex> lock(stream->pump_mutex);
                            stream->pump_cv.wait_for(
                                lock, std::chrono::nanoseconds(remaining_ns),
                                [stream] { return !stream->pump_running.load(); });
                        }
                    } else {
                        std::unique_lock<std::mutex> lock(stream->pump_mutex);
                        stream->pump_cv.wait_for(lock, std::chrono::nanoseconds(remaining_ns),
                                                 [stream] { return !stream->pump_running.load(); });
                    }
                }
            }

            if (timer) {
                CloseHandle(timer);
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
    if (stream->state.load() == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    stream->StopPump();
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        AudioManager::Instance().StopCapture();
    }
    stream->SetState(AAUDIO_STREAM_STATE_PAUSED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestFlush(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        return AAUDIO_ERROR_UNIMPLEMENTED;
    }
    aaudio_stream_state_t current = stream->state.load();
    if (current == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    if (current != AAUDIO_STREAM_STATE_OPEN && current != AAUDIO_STREAM_STATE_PAUSED &&
        current != AAUDIO_STREAM_STATE_STOPPED && current != AAUDIO_STREAM_STATE_FLUSHED) {
        return AAUDIO_ERROR_INVALID_STATE;
    }
    stream->SetState(AAUDIO_STREAM_STATE_FLUSHING);
    AudioManager::Instance().FlushStream();
    stream->SetState(AAUDIO_STREAM_STATE_FLUSHED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestStop(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    if (stream->state.load() == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    stream->StopPump();
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        AudioManager::Instance().StopCapture();
    }
    stream->SetState(AAUDIO_STREAM_STATE_STOPPED);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_close(AAudioStream *stream)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    stream->StopPump();
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        AudioManager::Instance().CloseCaptureStream();
    }
    stream->SetState(AAUDIO_STREAM_STATE_CLOSED);
    delete stream;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *stream,
                                                aaudio_stream_state_t inputState,
                                                aaudio_stream_state_t *nextState,
                                                int64_t timeoutNanoseconds)
{
    if (!stream) {
        return AAUDIO_ERROR_NULL;
    }
    if (timeoutNanoseconds < 0) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock(stream->state_mutex);
    aaudio_stream_state_t current = stream->state.load();
    if (current != inputState) {
        if (nextState) {
            *nextState = current;
        }
        return AAUDIO_OK;
    }
    if (timeoutNanoseconds == 0) {
        if (nextState) {
            *nextState = current;
        }
        return AAUDIO_ERROR_TIMEOUT;
    }

    bool changed = stream->state_cv.wait_for(
        lock, std::chrono::nanoseconds(timeoutNanoseconds),
        [stream, inputState] { return stream->state.load() != inputState; });
    current = stream->state.load();
    if (nextState) {
        *nextState = current;
    }
    return changed ? AAUDIO_OK : AAUDIO_ERROR_TIMEOUT;
}

aaudio_result_t AAudioStream_write(AAudioStream *stream, const void *buffer, int32_t numFrames,
                                   int64_t timeoutNanoseconds)
{
    (void)timeoutNanoseconds;
    if (!stream || !buffer) {
        return AAUDIO_ERROR_NULL;
    }
    aaudio_stream_state_t current = stream->state.load();
    if (current == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    if (numFrames <= 0) {
        return 0;
    }

    if (current != AAUDIO_STREAM_STATE_STARTED) {
        int32_t alsa_format = (stream->format == AAUDIO_FORMAT_PCM_FLOAT)
                                  ? SNDRV_PCM_FORMAT_FLOAT_LE
                                  : SNDRV_PCM_FORMAT_S16_LE;
        bool low_latency = (stream->performance_mode == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AudioManager::Instance().ConfigureStream(stream->sample_rate, stream->channel_count,
                                                 alsa_format, low_latency);
        stream->last_mgr_xrun = AudioManager::Instance().GetXRunCount();
        stream->SetState(AAUDIO_STREAM_STATE_STARTED);
    }

    int32_t sample_bytes = (stream->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4 : 2;
    size_t byte_count = static_cast<size_t>(numFrames) * stream->channel_count * sample_bytes;
    int64_t written = AudioManager::Instance().WritePcmBytes(buffer, byte_count);
    if (written < 0) {
        return AAUDIO_ERROR_INTERNAL;
    }
    stream->frames_written.fetch_add(numFrames);
    stream->SyncXRunDelta();
    return numFrames;
}

aaudio_result_t AAudioStream_read(AAudioStream *stream, void *buffer, int32_t numFrames,
                                  int64_t timeoutNanoseconds)
{
    if (!stream || !buffer) {
        return AAUDIO_ERROR_NULL;
    }
    if (stream->state.load() == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    if (numFrames <= 0) {
        return 0;
    }
    if (stream->direction != AAUDIO_DIRECTION_INPUT) {
        return AAUDIO_ERROR_INVALID_STATE;
    }

    if (stream->state.load() != AAUDIO_STREAM_STATE_STARTED) {
        int32_t alsa_format = (stream->format == AAUDIO_FORMAT_PCM_FLOAT)
                                  ? SNDRV_PCM_FORMAT_FLOAT_LE
                                  : SNDRV_PCM_FORMAT_S16_LE;
        AudioManager::Instance().ConfigureCaptureStream(stream->sample_rate, stream->channel_count,
                                                        alsa_format);
        AudioManager::Instance().StartCapture();
        stream->SetState(AAUDIO_STREAM_STATE_STARTED);
    }

    int64_t frames_read =
        AudioManager::Instance().ReadiFrames(buffer, numFrames, timeoutNanoseconds);
    if (frames_read < 0) {
        return AAUDIO_ERROR_INTERNAL;
    }
    stream->frames_read.fetch_add(frames_read);
    return static_cast<aaudio_result_t>(frames_read);
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

int64_t AAudioStream_getFramesWritten(AAudioStream *stream)
{
    return stream ? stream->frames_written.load() : 0;
}

int64_t AAudioStream_getFramesRead(AAudioStream *stream)
{
    if (!stream) {
        return 0;
    }
    if (stream->direction == AAUDIO_DIRECTION_OUTPUT) {
        uint64_t hw_pos = 0;
        uint64_t hw_freq = 0;
        uint64_t qpc_100ns = 0;
        if (AudioManager::Instance().GetHardwareClockPosition(&hw_pos, &hw_freq, &qpc_100ns) &&
            hw_freq > 0) {
            int64_t hw_frames =
                ScaleFrames(hw_pos, static_cast<uint64_t>(stream->sample_rate), hw_freq);
            int64_t prev = stream->frames_read.load();
            if (hw_frames > prev) {
                stream->frames_read.store(hw_frames);
            }
        }
    }
    return stream->frames_read.load();
}

aaudio_result_t AAudioStream_getTimestamp(AAudioStream *stream, int32_t clockid,
                                          int64_t *framePosition, int64_t *timeNanoseconds)
{
    if (!stream || !framePosition || !timeNanoseconds) {
        return AAUDIO_ERROR_NULL;
    }
    if (clockid != GUEST_CLOCK_MONOTONIC && clockid != GUEST_CLOCK_BOOTTIME) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }
    aaudio_stream_state_t current = stream->state.load();
    if (current == AAUDIO_STREAM_STATE_DISCONNECTED) {
        return AAUDIO_ERROR_DISCONNECTED;
    }
    if (current != AAUDIO_STREAM_STATE_STARTED) {
        return AAUDIO_ERROR_INVALID_STATE;
    }

    uint64_t hw_pos = 0;
    uint64_t hw_freq = 0;
    uint64_t qpc_100ns = 0;
    if (stream->direction == AAUDIO_DIRECTION_OUTPUT &&
        AudioManager::Instance().GetHardwareClockPosition(&hw_pos, &hw_freq, &qpc_100ns) &&
        hw_freq > 0 && qpc_100ns > 0) {
        int64_t hw_frames =
            ScaleFrames(hw_pos, static_cast<uint64_t>(stream->sample_rate), hw_freq);
        int64_t prev = stream->frames_read.load();
        if (hw_frames > prev) {
            stream->frames_read.store(hw_frames);
        }
        *framePosition = hw_frames;
        *timeNanoseconds = static_cast<int64_t>(qpc_100ns * 100ULL);
        return AAUDIO_OK;
    }

    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        *framePosition = stream->frames_read.load();
    } else {
        int64_t rendered =
            static_cast<int64_t>(AudioManager::Instance().GetStats().total_frames_rendered);
        *framePosition = std::min(stream->frames_written.load(), rendered);
    }
    *timeNanoseconds = MonotonicNowNanos();
    return AAUDIO_OK;
}

int32_t AAudioStream_getXRunCount(AAudioStream *stream)
{
    if (!stream) {
        return 0;
    }
    stream->SyncXRunDelta();
    return stream->xrun_count.load();
}

aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *stream)
{
    return stream ? stream->performance_mode : AAUDIO_PERFORMANCE_MODE_NONE;
}

aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *stream)
{
    return stream ? stream->sharing_mode : AAUDIO_SHARING_MODE_SHARED;
}

int32_t AAudioStream_getDeviceId(AAudioStream *stream) { return stream ? stream->device_id : 0; }
}

void SimulateStreamError(AAudioStream *stream, aaudio_result_t error)
{
    if (!stream) {
        return;
    }
    stream->StopPump();
    if (stream->direction == AAUDIO_DIRECTION_INPUT) {
        AudioManager::Instance().StopCapture();
    } else {
        AudioManager::Instance().CloseStream();
    }
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        stream->SetState(AAUDIO_STREAM_STATE_DISCONNECTED);
    }
    if (stream->error_callback) {
        stream->error_callback(stream, stream->error_user_data, error);
    }
}

} // namespace portage::audio
