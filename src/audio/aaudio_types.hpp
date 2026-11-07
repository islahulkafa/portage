#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::audio {

using aaudio_result_t = int32_t;
inline constexpr aaudio_result_t AAUDIO_OK = 0;
inline constexpr aaudio_result_t AAUDIO_ERROR_BASE = -900;
inline constexpr aaudio_result_t AAUDIO_ERROR_DISCONNECTED = -899;
inline constexpr aaudio_result_t AAUDIO_ERROR_ILLEGAL_ARGUMENT = -898;
inline constexpr aaudio_result_t AAUDIO_ERROR_INTERNAL = -896;
inline constexpr aaudio_result_t AAUDIO_ERROR_INVALID_STATE = -895;
inline constexpr aaudio_result_t AAUDIO_ERROR_INVALID_HANDLE = -892;
inline constexpr aaudio_result_t AAUDIO_ERROR_UNIMPLEMENTED = -890;
inline constexpr aaudio_result_t AAUDIO_ERROR_UNAVAILABLE = -889;
inline constexpr aaudio_result_t AAUDIO_ERROR_NO_FREE_HANDLES = -888;
inline constexpr aaudio_result_t AAUDIO_ERROR_NO_MEMORY = -887;
inline constexpr aaudio_result_t AAUDIO_ERROR_NULL = -886;
inline constexpr aaudio_result_t AAUDIO_ERROR_TIMEOUT = -885;
inline constexpr aaudio_result_t AAUDIO_ERROR_WOULD_BLOCK = -884;
inline constexpr aaudio_result_t AAUDIO_ERROR_INVALID_FORMAT = -883;
inline constexpr aaudio_result_t AAUDIO_ERROR_OUT_OF_RANGE = -882;
inline constexpr aaudio_result_t AAUDIO_ERROR_NO_SERVICE = -881;
inline constexpr aaudio_result_t AAUDIO_ERROR_INVALID_RATE = -880;

using aaudio_stream_state_t = int32_t;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_UNINITIALIZED = 0;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_UNKNOWN = 1;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_OPEN = 2;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STARTING = 3;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STARTED = 4;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_PAUSING = 5;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_PAUSED = 6;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_FLUSHING = 7;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_FLUSHED = 8;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STOPPING = 9;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STOPPED = 10;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_CLOSING = 11;
inline constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_CLOSED = 12;

using aaudio_direction_t = int32_t;
inline constexpr aaudio_direction_t AAUDIO_DIRECTION_OUTPUT = 0;
inline constexpr aaudio_direction_t AAUDIO_DIRECTION_INPUT = 1;

using aaudio_format_t = int32_t;
inline constexpr aaudio_format_t AAUDIO_FORMAT_INVALID = -1;
inline constexpr aaudio_format_t AAUDIO_FORMAT_UNSPECIFIED = 0;
inline constexpr aaudio_format_t AAUDIO_FORMAT_PCM_I16 = 1;
inline constexpr aaudio_format_t AAUDIO_FORMAT_PCM_FLOAT = 2;

using aaudio_data_callback_result_t = int32_t;
inline constexpr aaudio_data_callback_result_t AAUDIO_CALLBACK_RESULT_CONTINUE = 0;
inline constexpr aaudio_data_callback_result_t AAUDIO_CALLBACK_RESULT_STOP = 1;

using aaudio_performance_mode_t = int32_t;
inline constexpr aaudio_performance_mode_t AAUDIO_PERFORMANCE_MODE_NONE = 10;
inline constexpr aaudio_performance_mode_t AAUDIO_PERFORMANCE_MODE_POWER_SAVING = 11;
inline constexpr aaudio_performance_mode_t AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12;

using aaudio_sharing_mode_t = int32_t;
inline constexpr aaudio_sharing_mode_t AAUDIO_SHARING_MODE_EXCLUSIVE = 0;
inline constexpr aaudio_sharing_mode_t AAUDIO_SHARING_MODE_SHARED = 1;

struct AAudioStreamStruct;
using AAudioStream = AAudioStreamStruct;

struct AAudioStreamBuilderStruct;
using AAudioStreamBuilder = AAudioStreamBuilderStruct;

using AAudioStream_dataCallback = __attribute__((sysv_abi))
aaudio_data_callback_result_t (*)(AAudioStream *stream, void *userData, void *audioData,
                                  int32_t numFrames);

using AAudioStream_errorCallback = __attribute__((sysv_abi)) void (*)(AAudioStream *stream,
                                                                      void *userData,
                                                                      aaudio_result_t error);

} // namespace portage::audio
