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
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream)
    __attribute__((sysv_abi));
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder) __attribute__((sysv_abi));

aaudio_result_t AAudioStream_requestStart(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_requestPause(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_requestStop(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_close(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_result_t AAudioStream_write(AAudioStream *stream, const void *buffer, int32_t numFrames,
                                   int64_t timeoutNanoseconds) __attribute__((sysv_abi));

int32_t AAudioStream_getSampleRate(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getChannelCount(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_format_t AAudioStream_getFormat(AAudioStream *stream) __attribute__((sysv_abi));
aaudio_stream_state_t AAudioStream_getState(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getFramesPerDataCallback(AAudioStream *stream) __attribute__((sysv_abi));
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *stream) __attribute__((sysv_abi));
}

} // namespace portage::audio
