#pragma once

#include "audio/aaudio_types.hpp"

namespace portage::audio {

extern "C" {

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder) __attribute__((sysv_abi));
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *builder, int32_t sampleRate)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channelCount)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, aaudio_format_t format)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *builder, aaudio_direction_t direction)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder,
                                         AAudioStream_dataCallback callback, void *userData)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *builder,
                                          AAudioStream_errorCallback callback, void *userData)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *builder, int32_t frames)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder *builder, int32_t numFrames)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder,
                                            aaudio_performance_mode_t mode)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *builder, aaudio_sharing_mode_t mode)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *builder, aaudio_usage_t usage)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *builder,
                                        aaudio_content_type_t contentType)
    __attribute__((sysv_abi));
void AAudioStreamBuilder_setInputPreset(AAudioStreamBuilder *builder,
                                        aaudio_input_preset_t inputPreset)
    __attribute__((sysv_abi));
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream)
    __attribute__((sysv_abi));
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder) __attribute__((sysv_abi));

aaudio_result_t AAudioStream_requestStart(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_requestPause(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_requestFlush(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_requestStop(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_close(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t
AAudioStream_waitForStateChange(AAudioStream *stream, aaudio_stream_state_t inputState,
                                aaudio_stream_state_t *nextState, int64_t timeoutNanoseconds)
    __attribute__((sysv_abi));
aaudio_result_t AAudioStream_write(AAudioStream *stream, const void *buffer, int32_t numFrames,
                                   int64_t timeoutNanoseconds) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_read(AAudioStream *stream, void *buffer, int32_t numFrames,
                                  int64_t timeoutNanoseconds) __attribute__((sysv_abi));

int32_t AAudioStream_getSampleRate(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getChannelCount(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_format_t AAudioStream_getFormat(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_stream_state_t AAudioStream_getState(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getFramesPerDataCallback(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *stream) __attribute__((sysv_abi));
int64_t AAudioStream_getFramesWritten(AAudioStream *stream) __attribute__((sysv_abi));
int64_t AAudioStream_getFramesRead(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *stream, int32_t clockid,
                                          int64_t *framePosition, int64_t *timeNanoseconds)
    __attribute__((sysv_abi));
int32_t AAudioStream_getXRunCount(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *stream)
    __attribute__((sysv_abi));
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getDeviceId(AAudioStream *stream) __attribute__((sysv_abi));
}

void SimulateStreamError(AAudioStream *stream, aaudio_result_t error);

} // namespace portage::audio
