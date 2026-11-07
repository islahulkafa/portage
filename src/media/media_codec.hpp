#pragma once

#include "media/media_format.hpp"
#include "media/media_types.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct CodecBuffer {
    std::vector<uint8_t> data{};
    AMediaCodecBufferInfo info{};
    bool in_use{false};
};

struct AMediaCodec {
    enum class State { Uninitialized, Configured, Executing, Stopped, Released };
    State state{State::Uninitialized};
    std::string name{};
    std::string mime{};
    bool is_encoder{false};
    uint32_t configure_flags{0};

    int32_t width{128};
    int32_t height{128};
    int32_t stride{512};
    int32_t sample_rate{44100};
    int32_t channel_count{2};

    std::unique_ptr<AMediaFormat> input_format{};
    std::unique_ptr<AMediaFormat> output_format{};
    ANativeWindow *surface{nullptr};

    std::vector<CodecBuffer> input_buffers{};
    std::vector<CodecBuffer> output_buffers{};
    std::deque<size_t> ready_output_indices{};
    size_t next_input_idx{0};
    int64_t current_pts_us{0};
    std::mutex mutex{};
};

extern "C" {

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createCodecByName(const char *name);

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type);

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createEncoderByType(const char *mime_type);

__attribute__((sysv_abi)) media_status_t AMediaCodec_delete(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t
AMediaCodec_configure(AMediaCodec *codec, const AMediaFormat *format, ANativeWindow *surface,
                      AMediaCrypto *crypto, uint32_t flags);

__attribute__((sysv_abi)) media_status_t AMediaCodec_start(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_stop(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_flush(AMediaCodec *codec);

__attribute__((sysv_abi)) int64_t AMediaCodec_dequeueInputBuffer(AMediaCodec *codec,
                                                               int64_t timeoutUs);

__attribute__((sysv_abi)) uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *codec, size_t idx,
                                                              size_t *out_size);

__attribute__((sysv_abi)) media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *codec,
                                                                     size_t idx, int64_t offset,
                                                                     size_t size, uint64_t timeUs,
                                                                     uint32_t flags);

__attribute__((sysv_abi)) int64_t
AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info, int64_t timeoutUs);

__attribute__((sysv_abi)) uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx,
                                                               size_t *out_size);

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *codec,
                                                                         size_t idx, bool render);

__attribute__((sysv_abi)) media_status_t
AMediaCodec_releaseOutputBufferAtTime(AMediaCodec *codec, size_t idx, int64_t renderTimestampNs);

} // extern "C"
