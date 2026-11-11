#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace portage::media {

enum class MftColorFormat {
    Unknown,
    Nv12,
    I420,
    Rgba8888,
    Pcm16,
};

struct MftConfig {
    std::string mime{};
    bool is_encoder{false};
    int32_t width{128};
    int32_t height{128};
    float frame_rate{30.0f};
    int32_t bitrate{2000000};
    int32_t i_frame_interval{1};
    int32_t sample_rate{44100};
    int32_t channel_count{2};
    std::vector<uint8_t> csd0{};
    std::vector<uint8_t> csd1{};
};

struct MftPacket {
    std::vector<uint8_t> data{};
    int64_t pts_us{0};
    uint32_t flags{0};
    bool format_changed{false};
};

class WmfTransformWrapper {
  public:
    WmfTransformWrapper() = default;
    ~WmfTransformWrapper();

    WmfTransformWrapper(const WmfTransformWrapper &) = delete;
    WmfTransformWrapper &operator=(const WmfTransformWrapper &) = delete;

    bool Open(const MftConfig &config);
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept { return mft_ != nullptr; }
    [[nodiscard]] bool IsEncoder() const noexcept { return config_.is_encoder; }
    [[nodiscard]] bool IsVideo() const noexcept { return is_video_; }

    bool Flush();
    bool Drain();

    bool ProcessInput(std::span<const uint8_t> data, int64_t pts_us, uint32_t flags,
                      bool *out_try_again = nullptr);
    bool ProcessOutput(std::vector<MftPacket> &out_packets);

    void SetParameters(int32_t bitrate, bool request_sync_frame);

    [[nodiscard]] int32_t Width() const noexcept { return width_; }
    [[nodiscard]] int32_t Height() const noexcept { return height_; }
    [[nodiscard]] int32_t Stride() const noexcept { return stride_; }
    [[nodiscard]] int32_t SampleRate() const noexcept { return sample_rate_; }
    [[nodiscard]] int32_t ChannelCount() const noexcept { return channel_count_; }
    [[nodiscard]] MftColorFormat OutputColorFormat() const noexcept { return output_color_; }
    [[nodiscard]] const std::vector<uint8_t> &NegotiatedCsd0() const noexcept { return csd0_; }

  private:
    bool ConfigureVideoDecoder(const MftConfig &config);
    bool ConfigureVideoEncoder(const MftConfig &config);
    bool ConfigureAudioDecoder(const MftConfig &config);
    bool ConfigureAudioEncoder(const MftConfig &config);
    bool SelectNegotiatedVideoOutputType();
    bool SelectNegotiatedAudioOutputType();
    void ExtractCodecConfigFromOutputType(IMFMediaType *type);
    void RefreshStrideFromType(IMFMediaType *type);

    IMFTransform *mft_{nullptr};
    MftConfig config_{};
    bool is_video_{false};
    bool streaming_started_{false};
    bool sent_initial_config_{false};
    bool audio_output_is_float_{false};

    int32_t width_{0};
    int32_t height_{0};
    int32_t stride_{0};
    int32_t sample_rate_{44100};
    int32_t channel_count_{2};
    MftColorFormat output_color_{MftColorFormat::Unknown};
    std::vector<uint8_t> csd0_{};
};

void ConvertNv12ToRgba(const uint8_t *nv12, int32_t y_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba);

void ConvertI420ToRgba(const uint8_t *i420, int32_t y_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba);

void ConvertRgbaToNv12(const uint8_t *rgba, int32_t rgba_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_nv12);

} // namespace portage::media
