#pragma once

#include "media/media_format.hpp"
#include "media/media_types.hpp"
#include "media/wmf_transform.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AMediaCodec;

extern "C" {

using AMediaCodecOnAsyncInputAvailable = void(__attribute__((sysv_abi)) *)(AMediaCodec *codec,
                                                                           void *userdata,
                                                                           int32_t index);
using AMediaCodecOnAsyncOutputAvailable = void(__attribute__((sysv_abi)) *)(
    AMediaCodec *codec, void *userdata, int32_t index, AMediaCodecBufferInfo *bufferInfo);
using AMediaCodecOnAsyncFormatChanged = void(__attribute__((sysv_abi)) *)(AMediaCodec *codec,
                                                                          void *userdata,
                                                                          AMediaFormat *format);
using AMediaCodecOnAsyncError = void(__attribute__((sysv_abi)) *)(AMediaCodec *codec,
                                                                  void *userdata,
                                                                  media_status_t error,
                                                                  int32_t actionCode,
                                                                  const char *detail);

struct AMediaCodecOnAsyncNotifyCallback {
    AMediaCodecOnAsyncInputAvailable onAsyncInputAvailable{nullptr};
    AMediaCodecOnAsyncOutputAvailable onAsyncOutputAvailable{nullptr};
    AMediaCodecOnAsyncFormatChanged onAsyncFormatChanged{nullptr};
    AMediaCodecOnAsyncError onAsyncError{nullptr};
};

} // extern "C"

struct CodecBuffer {
    std::vector<uint8_t> data{};
    AMediaCodecBufferInfo info{};
    bool in_use{false};
    portage::media::MftColorFormat color_format{portage::media::MftColorFormat::Unknown};
    std::unique_ptr<AMediaFormat> format_snapshot{};
};

struct AMediaCodec {
    enum class State { Uninitialized, Configured, Executing, Stopped, Released };
    enum class Mode { Auto, WmfActive, PassthroughFallback };

    State state{State::Uninitialized};
    Mode mode{Mode::Auto};
    std::string name{};
    std::string mime{};
    bool is_encoder{false};
    uint32_t configure_flags{0};

    int32_t width{128};
    int32_t height{128};
    int32_t stride{512};
    float frame_rate{30.0f};
    int32_t bitrate{2000000};
    int32_t i_frame_interval{1};
    int32_t sample_rate{44100};
    int32_t channel_count{2};

    std::unique_ptr<AMediaFormat> input_format{};
    std::unique_ptr<AMediaFormat> output_format{};
    ANativeWindow *surface{nullptr};
    ANativeWindow *input_surface{nullptr};
    uint64_t surface_frame_count{0};

    std::unique_ptr<portage::media::WmfTransformWrapper> mft{};
    bool format_changed_pending{false};
    bool eos_signaled{false};
    std::vector<uint8_t> last_synced_csd0{};

    std::vector<CodecBuffer> input_buffers{};
    std::vector<CodecBuffer> output_buffers{};
    std::deque<size_t> ready_output_indices{};
    size_t next_input_idx{0};
    int64_t current_pts_us{0};
    std::mutex mutex{};
    std::condition_variable cv{};

    bool async_mode{false};
    AMediaCodecOnAsyncNotifyCallback async_callbacks{};
    void *async_userdata{nullptr};
    std::thread async_thread{};
    bool async_stop_requested{false};
    std::vector<std::unique_ptr<AMediaFormat>> async_format_keepalive{};

    ~AMediaCodec();
};

extern "C" {

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createCodecByName(const char *name);

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type);

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createEncoderByType(const char *mime_type);

__attribute__((sysv_abi)) media_status_t AMediaCodec_delete(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_configure(AMediaCodec *codec,
                                                               const AMediaFormat *format,
                                                               ANativeWindow *surface,
                                                               AMediaCrypto *crypto,
                                                               uint32_t flags);

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

__attribute__((sysv_abi)) int64_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec,
                                                                  AMediaCodecBufferInfo *info,
                                                                  int64_t timeoutUs);

__attribute__((sysv_abi)) uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx,
                                                               size_t *out_size);

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *codec,
                                                                         size_t idx, bool render);

__attribute__((sysv_abi)) media_status_t
AMediaCodec_releaseOutputBufferAtTime(AMediaCodec *codec, size_t idx, int64_t renderTimestampNs);

__attribute__((sysv_abi)) media_status_t AMediaCodec_getName(AMediaCodec *codec, char **out_name);

__attribute__((sysv_abi)) void AMediaCodec_releaseName(AMediaCodec *codec, char *name);

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getInputFormat(AMediaCodec *codec);

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getBufferFormat(AMediaCodec *codec,
                                                                    size_t index);

__attribute__((sysv_abi)) media_status_t AMediaCodec_setParameters(AMediaCodec *codec,
                                                                   const AMediaFormat *params);

__attribute__((sysv_abi)) media_status_t AMediaCodec_signalEndOfInputStream(AMediaCodec *codec);

__attribute__((sysv_abi)) media_status_t AMediaCodec_createInputSurface(AMediaCodec *codec,
                                                                        ANativeWindow **surface);

__attribute__((sysv_abi)) media_status_t
AMediaCodec_createPersistentInputSurface(ANativeWindow **surface);

__attribute__((sysv_abi)) media_status_t AMediaCodec_setInputSurface(AMediaCodec *codec,
                                                                     ANativeWindow *surface);

__attribute__((sysv_abi)) media_status_t AMediaCodec_setAsyncNotifyCallback(
    AMediaCodec *codec, AMediaCodecOnAsyncNotifyCallback callback, void *userdata);

} // extern "C"
