#include "media/wmf_transform.hpp"
#include "media/media_types.hpp"
#include "media/wmf_decoder.hpp"

#include <icodecapi.h>
#include <codecapi.h>
#include <mferror.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace portage::media {

namespace {

uint8_t ClampByte(int32_t v) noexcept { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

void YuvToRgbPixel(int32_t y_val, int32_t u_val, int32_t v_val, uint8_t *out_rgba) noexcept
{
    int32_t c = y_val - 16;
    int32_t d = u_val - 128;
    int32_t e = v_val - 128;
    int32_t r = (298 * c + 409 * e + 128) >> 8;
    int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int32_t b = (298 * c + 516 * d + 128) >> 8;
    out_rgba[0] = ClampByte(r);
    out_rgba[1] = ClampByte(g);
    out_rgba[2] = ClampByte(b);
    out_rgba[3] = 255;
}

bool MapVideoSubtype(std::string_view mime, GUID *out_guid) noexcept
{
    if (mime == "video/avc") {
        *out_guid = MFVideoFormat_H264;
        return true;
    }
    if (mime == "video/hevc") {
        *out_guid = MFVideoFormat_HEVC;
        return true;
    }
    if (mime == "video/mp4v-es") {
        *out_guid = MFVideoFormat_MP4V;
        return true;
    }
    return false;
}

IMFTransform *InstantiateMft(const GUID &category, const MFT_REGISTER_TYPE_INFO *in_info,
                             const MFT_REGISTER_TYPE_INFO *out_info)
{
    IMFActivate **activates = nullptr;
    UINT32 count = 0;
    UINT32 flags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER;
    HRESULT hr = MFTEnumEx(category, flags, in_info, out_info, &activates, &count);
    if (FAILED(hr) || count == 0 || !activates) {
        return nullptr;
    }

    IMFTransform *selected = nullptr;
    for (UINT32 i = 0; i < count; ++i) {
        if (!selected) {
            IMFTransform *candidate = nullptr;
            if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&candidate))) && candidate) {
                selected = candidate;
            }
        }
        activates[i]->Release();
    }
    CoTaskMemFree(activates);
    return selected;
}

std::vector<uint8_t> BuildAacUserData(uint16_t payload_type, const std::vector<uint8_t> &asc)
{
    std::vector<uint8_t> ud(12 + asc.size(), 0);
    ud[0] = static_cast<uint8_t>(payload_type & 0xFF);
    ud[1] = static_cast<uint8_t>((payload_type >> 8) & 0xFF);
    ud[2] = 0xFE;
    ud[3] = 0x00;
    if (!asc.empty()) {
        std::memcpy(ud.data() + 12, asc.data(), asc.size());
    }
    return ud;
}

uint32_t SnapAacAvgBytesPerSec(int32_t bitrate) noexcept
{
    const uint32_t target = (bitrate > 0) ? static_cast<uint32_t>(bitrate / 8) : 16000u;
    const uint32_t supported[] = {12000u, 16000u, 20000u, 24000u};
    uint32_t best = supported[1];
    uint32_t best_diff = 0xFFFFFFFFu;
    for (uint32_t val : supported) {
        uint32_t diff = (val > target) ? (val - target) : (target - val);
        if (diff < best_diff) {
            best_diff = diff;
            best = val;
        }
    }
    return best;
}

bool HasAnnexBStartCode(std::span<const uint8_t> bytes) noexcept
{
    if (bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x00 &&
        bytes[3] == 0x01) {
        return true;
    }
    if (bytes.size() >= 3 && bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x01) {
        return true;
    }
    return false;
}

bool ContainsIdrOrConfigNals(std::span<const uint8_t> annexb, bool *out_has_idr,
                             std::vector<uint8_t> *out_config_nals)
{
    bool found_any = false;
    size_t i = 0;
    std::vector<size_t> nal_starts;
    std::vector<size_t> prefix_starts;

    while (i + 3 <= annexb.size()) {
        if (annexb[i] == 0 && annexb[i + 1] == 0) {
            if (i + 3 < annexb.size() && annexb[i + 2] == 0 && annexb[i + 3] == 1) {
                prefix_starts.push_back(i);
                nal_starts.push_back(i + 4);
                i += 4;
                continue;
            }
            if (annexb[i + 2] == 1) {
                prefix_starts.push_back(i);
                nal_starts.push_back(i + 3);
                i += 3;
                continue;
            }
        }
        ++i;
    }

    for (size_t idx = 0; idx < nal_starts.size(); ++idx) {
        size_t s = nal_starts[idx];
        size_t e = (idx + 1 < prefix_starts.size()) ? prefix_starts[idx + 1] : annexb.size();
        if (s >= e) {
            continue;
        }
        uint8_t nal_type = annexb[s] & 0x1F;
        if (nal_type == 5) {
            if (out_has_idr) {
                *out_has_idr = true;
            }
            found_any = true;
        } else if ((nal_type == 7 || nal_type == 8) && out_config_nals) {
            out_config_nals->insert(out_config_nals->end(),
                                    annexb.begin() +
                                        static_cast<std::ptrdiff_t>(prefix_starts[idx]),
                                    annexb.begin() + static_cast<std::ptrdiff_t>(e));
            found_any = true;
        }
    }
    return found_any;
}

} // namespace

WmfTransformWrapper::~WmfTransformWrapper() { Close(); }

void WmfTransformWrapper::Close()
{
    if (mft_) {
        WmfManager::Instance().DispatchSync([this]() {
            if (streaming_started_) {
                mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                streaming_started_ = false;
            }
            mft_->Release();
            mft_ = nullptr;
        });
    }
    sent_initial_config_ = false;
    audio_output_is_float_ = false;
}

bool WmfTransformWrapper::Open(const MftConfig &config)
{
    Close();
    if (!WmfManager::Instance().Initialize()) {
        return false;
    }

    config_ = config;
    width_ = std::max(16, config.width);
    height_ = std::max(16, config.height);
    stride_ = width_;
    sample_rate_ = std::max(8000, config.sample_rate);
    channel_count_ = std::max(1, config.channel_count);
    csd0_ = config.csd0;

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        if (config.mime.rfind("video/", 0) == 0) {
            is_video_ = true;
            ok = config.is_encoder ? ConfigureVideoEncoder(config) : ConfigureVideoDecoder(config);
        } else if (config.mime.rfind("audio/", 0) == 0) {
            is_video_ = false;
            ok = config.is_encoder ? ConfigureAudioEncoder(config) : ConfigureAudioDecoder(config);
        }
    });
    return ok;
}

bool WmfTransformWrapper::ConfigureVideoDecoder(const MftConfig &config)
{
    GUID subtype{};
    if (!MapVideoSubtype(config.mime, &subtype)) {
        return false;
    }

    MFT_REGISTER_TYPE_INFO in_info{MFMediaType_Video, subtype};
    mft_ = InstantiateMft(MFT_CATEGORY_VIDEO_DECODER, &in_info, nullptr);
    if (!mft_) {
        return false;
    }

    IMFMediaType *in_type = nullptr;
    if (FAILED(MFCreateMediaType(&in_type))) {
        Close();
        return false;
    }
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in_type->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(in_type, MF_MT_FRAME_SIZE, static_cast<UINT32>(width_),
                       static_cast<UINT32>(height_));
    if (config.frame_rate > 0.0f) {
        UINT32 fps_num = static_cast<UINT32>(std::lround(config.frame_rate * 1000.0f));
        MFSetAttributeRatio(in_type, MF_MT_FRAME_RATE, fps_num, 1000u);
    }
    if (!config.csd0.empty()) {
        in_type->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, config.csd0.data(),
                         static_cast<UINT32>(config.csd0.size()));
    }

    HRESULT hr = mft_->SetInputType(0, in_type, 0);
    in_type->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }

    if (!SelectNegotiatedVideoOutputType()) {
        Close();
        return false;
    }

    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    streaming_started_ = true;
    return true;
}

bool WmfTransformWrapper::ConfigureVideoEncoder(const MftConfig &config)
{
    GUID subtype{};
    if (!MapVideoSubtype(config.mime, &subtype)) {
        return false;
    }

    MFT_REGISTER_TYPE_INFO out_info{MFMediaType_Video, subtype};
    mft_ = InstantiateMft(MFT_CATEGORY_VIDEO_ENCODER, nullptr, &out_info);
    if (!mft_) {
        return false;
    }

    UINT32 fps_num =
        (config.frame_rate > 0.0f) ? static_cast<UINT32>(std::lround(config.frame_rate)) : 30u;
    if (fps_num == 0) {
        fps_num = 30;
    }

    IMFMediaType *out_type = nullptr;
    if (FAILED(MFCreateMediaType(&out_type))) {
        Close();
        return false;
    }
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out_type->SetGUID(MF_MT_SUBTYPE, subtype);
    out_type->SetUINT32(MF_MT_AVG_BITRATE,
                        static_cast<UINT32>(config.bitrate > 0 ? config.bitrate : 2000000));
    out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out_type, MF_MT_FRAME_SIZE, static_cast<UINT32>(width_),
                       static_cast<UINT32>(height_));
    MFSetAttributeRatio(out_type, MF_MT_FRAME_RATE, fps_num, 1u);
    MFSetAttributeRatio(out_type, MF_MT_PIXEL_ASPECT_RATIO, 1u, 1u);
    if (subtype == MFVideoFormat_H264) {
        out_type->SetUINT32(MF_MT_MPEG2_PROFILE, 66); // Baseline
    }

    HRESULT hr = mft_->SetOutputType(0, out_type, 0);
    out_type->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }

    IMFMediaType *chosen_in = nullptr;
    for (DWORD idx = 0;; ++idx) {
        IMFMediaType *avail = nullptr;
        if (FAILED(mft_->GetInputAvailableType(0, idx, &avail))) {
            break;
        }
        GUID sub = GUID_NULL;
        avail->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFVideoFormat_NV12 || (!chosen_in && sub == MFVideoFormat_IYUV)) {
            chosen_in = avail;
            if (sub == MFVideoFormat_NV12) {
                break;
            }
        } else {
            avail->Release();
        }
    }

    if (!chosen_in) {
        MFCreateMediaType(&chosen_in);
        chosen_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        chosen_in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    }
    MFSetAttributeSize(chosen_in, MF_MT_FRAME_SIZE, static_cast<UINT32>(width_),
                       static_cast<UINT32>(height_));
    MFSetAttributeRatio(chosen_in, MF_MT_FRAME_RATE, fps_num, 1u);
    chosen_in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    GUID in_sub = GUID_NULL;
    chosen_in->GetGUID(MF_MT_SUBTYPE, &in_sub);
    output_color_ = (in_sub == MFVideoFormat_IYUV) ? MftColorFormat::I420 : MftColorFormat::Nv12;

    hr = mft_->SetInputType(0, chosen_in, 0);
    chosen_in->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }

    SetParameters(config.bitrate, false);

    IMFMediaType *curr_out = nullptr;
    if (SUCCEEDED(mft_->GetOutputCurrentType(0, &curr_out)) && curr_out) {
        ExtractCodecConfigFromOutputType(curr_out);
        curr_out->Release();
    }

    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    streaming_started_ = true;
    return true;
}

bool WmfTransformWrapper::ConfigureAudioDecoder(const MftConfig &config)
{
    if (config.mime != "audio/mp4a-latm") {
        return false;
    }

    MFT_REGISTER_TYPE_INFO in_info{MFMediaType_Audio, MFAudioFormat_AAC};
    mft_ = InstantiateMft(MFT_CATEGORY_AUDIO_DECODER, &in_info, nullptr);
    if (!mft_) {
        return false;
    }

    IMFMediaType *in_type = nullptr;
    if (FAILED(MFCreateMediaType(&in_type))) {
        Close();
        return false;
    }
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    in_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sample_rate_));
    in_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channel_count_));
    in_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

    uint16_t payload_type = config.csd0.empty() ? 1 : 0;
    in_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, payload_type);
    auto ud = BuildAacUserData(payload_type, config.csd0);
    in_type->SetBlob(MF_MT_USER_DATA, ud.data(), static_cast<UINT32>(ud.size()));

    HRESULT hr = mft_->SetInputType(0, in_type, 0);
    in_type->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }

    if (!SelectNegotiatedAudioOutputType()) {
        Close();
        return false;
    }

    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    streaming_started_ = true;
    return true;
}

bool WmfTransformWrapper::ConfigureAudioEncoder(const MftConfig &config)
{
    if (config.mime != "audio/mp4a-latm") {
        return false;
    }

    MFT_REGISTER_TYPE_INFO out_info{MFMediaType_Audio, MFAudioFormat_AAC};
    mft_ = InstantiateMft(MFT_CATEGORY_AUDIO_ENCODER, nullptr, &out_info);
    if (!mft_) {
        return false;
    }

    IMFMediaType *in_type = nullptr;
    if (FAILED(MFCreateMediaType(&in_type))) {
        Close();
        return false;
    }
    UINT32 ch = static_cast<UINT32>(channel_count_);
    UINT32 sr = static_cast<UINT32>(sample_rate_);
    UINT32 block_align = ch * 2;
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    in_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    in_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sr);
    in_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
    in_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
    in_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sr * block_align);

    HRESULT hr = mft_->SetInputType(0, in_type, 0);
    in_type->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }

    IMFMediaType *out_type = nullptr;
    if (FAILED(MFCreateMediaType(&out_type))) {
        Close();
        return false;
    }
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sr);
    out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, SnapAacAvgBytesPerSec(config.bitrate));
    out_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);

    hr = mft_->SetOutputType(0, out_type, 0);
    if (FAILED(hr)) {
        out_type->Release();
        Close();
        return false;
    }
    out_type->Release();

    IMFMediaType *curr_out = nullptr;
    if (SUCCEEDED(mft_->GetOutputCurrentType(0, &curr_out)) && curr_out) {
        ExtractCodecConfigFromOutputType(curr_out);
        curr_out->Release();
    }

    output_color_ = MftColorFormat::Pcm16;
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    streaming_started_ = true;
    return true;
}

bool WmfTransformWrapper::SelectNegotiatedVideoOutputType()
{
    IMFMediaType *chosen = nullptr;
    MftColorFormat chosen_fmt = MftColorFormat::Unknown;

    for (DWORD idx = 0;; ++idx) {
        IMFMediaType *avail = nullptr;
        if (FAILED(mft_->GetOutputAvailableType(0, idx, &avail))) {
            break;
        }
        GUID sub = GUID_NULL;
        avail->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFVideoFormat_NV12) {
            if (chosen) {
                chosen->Release();
            }
            chosen = avail;
            chosen_fmt = MftColorFormat::Nv12;
            break;
        }
        if (sub == MFVideoFormat_IYUV && !chosen) {
            chosen = avail;
            chosen_fmt = MftColorFormat::I420;
        } else {
            avail->Release();
        }
    }

    if (!chosen) {
        return false;
    }

    HRESULT hr = mft_->SetOutputType(0, chosen, 0);
    if (SUCCEEDED(hr)) {
        UINT32 w = 0;
        UINT32 h = 0;
        if (SUCCEEDED(MFGetAttributeSize(chosen, MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0) {
            width_ = static_cast<int32_t>(w);
            height_ = static_cast<int32_t>(h);
        }
        output_color_ = chosen_fmt;
        RefreshStrideFromType(chosen);
    }
    chosen->Release();
    return SUCCEEDED(hr);
}

bool WmfTransformWrapper::SelectNegotiatedAudioOutputType()
{
    IMFMediaType *chosen = nullptr;
    bool chosen_float = false;

    for (DWORD idx = 0;; ++idx) {
        IMFMediaType *avail = nullptr;
        if (FAILED(mft_->GetOutputAvailableType(0, idx, &avail))) {
            break;
        }
        GUID sub = GUID_NULL;
        avail->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFAudioFormat_PCM) {
            UINT32 bps = 0;
            avail->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bps);
            if (bps == 16) {
                if (chosen) {
                    chosen->Release();
                }
                chosen = avail;
                chosen_float = false;
                break;
            }
        }
        if (sub == MFAudioFormat_Float && !chosen) {
            chosen = avail;
            chosen_float = true;
        } else {
            avail->Release();
        }
    }

    if (!chosen) {
        return false;
    }

    HRESULT hr = mft_->SetOutputType(0, chosen, 0);
    if (SUCCEEDED(hr)) {
        UINT32 sr = 0;
        UINT32 ch = 0;
        if (SUCCEEDED(chosen->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr)) && sr > 0) {
            sample_rate_ = static_cast<int32_t>(sr);
        }
        if (SUCCEEDED(chosen->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch)) && ch > 0) {
            channel_count_ = static_cast<int32_t>(ch);
        }
        audio_output_is_float_ = chosen_float;
        output_color_ = MftColorFormat::Pcm16;
    }
    chosen->Release();
    return SUCCEEDED(hr);
}

void WmfTransformWrapper::ExtractCodecConfigFromOutputType(IMFMediaType *type)
{
    if (!type) {
        return;
    }
    UINT32 blob_size = 0;
    if (is_video_) {
        if (SUCCEEDED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &blob_size)) && blob_size > 0) {
            csd0_.resize(blob_size);
            type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, csd0_.data(), blob_size, nullptr);
        }
    } else {
        if (SUCCEEDED(type->GetBlobSize(MF_MT_USER_DATA, &blob_size)) && blob_size > 12) {
            std::vector<uint8_t> raw(blob_size);
            if (SUCCEEDED(type->GetBlob(MF_MT_USER_DATA, raw.data(), blob_size, nullptr))) {
                csd0_.assign(raw.begin() + 12, raw.end());
            }
        }
    }
}

void WmfTransformWrapper::RefreshStrideFromType(IMFMediaType *type)
{
    stride_ = width_;
    if (!type) {
        return;
    }
    UINT32 raw_stride = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride)) && raw_stride != 0) {
        int32_t signed_stride = static_cast<int32_t>(raw_stride);
        stride_ = std::max(width_, std::abs(signed_stride));
    }
}

bool WmfTransformWrapper::Flush()
{
    if (!mft_) {
        return false;
    }
    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        HRESULT hr = mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        sent_initial_config_ = false;
        ok = SUCCEEDED(hr);
    });
    return ok;
}

bool WmfTransformWrapper::Drain()
{
    if (!mft_) {
        return false;
    }
    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        HRESULT hr = mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        ok = SUCCEEDED(hr);
    });
    return ok;
}

bool WmfTransformWrapper::ProcessInput(std::span<const uint8_t> data, int64_t pts_us,
                                       uint32_t flags, bool *out_try_again)
{
    if (out_try_again) {
        *out_try_again = false;
    }
    if (!mft_) {
        return false;
    }

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        if ((flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0 && data.empty()) {
            ok = Drain();
            return;
        }

        std::vector<uint8_t> assembled;
        if (!config_.is_encoder && is_video_) {
            if ((flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
                if (!data.empty()) {
                    config_.csd0.assign(data.begin(), data.end());
                    csd0_ = config_.csd0;
                    sent_initial_config_ = false;
                }
                if ((flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    Drain();
                }
                ok = true;
                return;
            }
            if (!sent_initial_config_ && !config_.csd0.empty()) {
                if (!HasAnnexBStartCode(data) || !ContainsIdrOrConfigNals(data, nullptr, nullptr)) {
                    assembled.insert(assembled.end(), config_.csd0.begin(), config_.csd0.end());
                    if (!config_.csd1.empty()) {
                        assembled.insert(assembled.end(), config_.csd1.begin(), config_.csd1.end());
                    }
                }
                sent_initial_config_ = true;
            }
        }

        if (!config_.is_encoder && !is_video_ && config_.mime == "audio/mp4a-latm" &&
            (flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0 && !data.empty()) {
            config_.csd0.assign(data.begin(), data.end());
            csd0_ = config_.csd0;
            IMFMediaType *re_in = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&re_in)) && re_in) {
                re_in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                re_in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
                re_in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sample_rate_));
                re_in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channel_count_));
                re_in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                re_in->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
                auto ud = BuildAacUserData(0, config_.csd0);
                re_in->SetBlob(MF_MT_USER_DATA, ud.data(), static_cast<UINT32>(ud.size()));
                mft_->SetInputType(0, re_in, 0);
                re_in->Release();
            }
            ok = true;
            return;
        }

        assembled.insert(assembled.end(), data.begin(), data.end());
        if (assembled.empty()) {
            if ((flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                ok = Drain();
                return;
            }
            ok = true;
            return;
        }

        IMFSample *sample = nullptr;
        IMFMediaBuffer *buf = nullptr;
        if (FAILED(MFCreateSample(&sample)) ||
            FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(assembled.size()), &buf))) {
            if (sample) {
                sample->Release();
            }
            ok = false;
            return;
        }

        BYTE *dst = nullptr;
        if (SUCCEEDED(buf->Lock(&dst, nullptr, nullptr)) && dst) {
            std::memcpy(dst, assembled.data(), assembled.size());
            buf->Unlock();
            buf->SetCurrentLength(static_cast<DWORD>(assembled.size()));
        }
        sample->AddBuffer(buf);
        sample->SetSampleTime(pts_us * 10);
        if (is_video_ && config_.frame_rate > 0.0f) {
            LONGLONG dur = static_cast<LONGLONG>(10000000.0 / config_.frame_rate);
            sample->SetSampleDuration(dur);
        }
        buf->Release();

        HRESULT hr = mft_->ProcessInput(0, sample, 0);
        sample->Release();

        if (hr == MF_E_NOTACCEPTING) {
            if (out_try_again) {
                *out_try_again = true;
            }
            ok = false;
            return;
        }

        if (SUCCEEDED(hr) && (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
            Drain();
        }
        ok = SUCCEEDED(hr);
    });
    return ok;
}

bool WmfTransformWrapper::ProcessOutput(std::vector<MftPacket> &out_packets)
{
    if (!mft_) {
        return false;
    }

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        MFT_OUTPUT_STREAM_INFO stream_info{};
        if (FAILED(mft_->GetOutputStreamInfo(0, &stream_info))) {
            ok = false;
            return;
        }

        bool caller_allocates =
            (stream_info.dwFlags &
             (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0;

        while (true) {
            MFT_OUTPUT_DATA_BUFFER out_buf{};
            out_buf.dwStreamID = 0;

            IMFSample *alloc_sample = nullptr;
            if (caller_allocates) {
                DWORD cap = std::max<DWORD>(
                    stream_info.cbSize,
                    is_video_ ? static_cast<DWORD>(width_ * height_ * 4 + 65536) : 65536u);
                IMFMediaBuffer *mb = nullptr;
                if (FAILED(MFCreateSample(&alloc_sample)) ||
                    FAILED(MFCreateMemoryBuffer(cap, &mb))) {
                    if (alloc_sample) {
                        alloc_sample->Release();
                    }
                    ok = false;
                    return;
                }
                alloc_sample->AddBuffer(mb);
                mb->Release();
                out_buf.pSample = alloc_sample;
            }

            DWORD status = 0;
            HRESULT hr = mft_->ProcessOutput(0, 1, &out_buf, &status);

            if (out_buf.pEvents) {
                out_buf.pEvents->Release();
            }

            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (alloc_sample) {
                    alloc_sample->Release();
                }
                bool neg_ok = is_video_ ? SelectNegotiatedVideoOutputType()
                                        : SelectNegotiatedAudioOutputType();
                if (!neg_ok) {
                    ok = false;
                    return;
                }
                mft_->GetOutputStreamInfo(0, &stream_info);
                caller_allocates =
                    (stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                            MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0;

                MftPacket fmt_pkt{};
                fmt_pkt.format_changed = true;
                out_packets.push_back(std::move(fmt_pkt));
                continue;
            }

            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(hr)) {
                if (alloc_sample) {
                    alloc_sample->Release();
                }
                break;
            }

            IMFSample *out_sample = out_buf.pSample;
            if (out_sample) {
                IMFMediaBuffer *contig = nullptr;
                if (SUCCEEDED(out_sample->ConvertToContiguousBuffer(&contig)) && contig) {
                    BYTE *ptr = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(contig->Lock(&ptr, nullptr, &len)) && ptr && len > 0) {
                        MftPacket pkt{};
                        LONGLONG sample_time = 0;
                        if (SUCCEEDED(out_sample->GetSampleTime(&sample_time))) {
                            pkt.pts_us = sample_time / 10;
                        }

                        if (!is_video_ && !config_.is_encoder && audio_output_is_float_) {
                            size_t sample_count = len / sizeof(float);
                            pkt.data.resize(sample_count * sizeof(int16_t));
                            const float *fsrc = reinterpret_cast<const float *>(ptr);
                            auto *idst = reinterpret_cast<int16_t *>(pkt.data.data());
                            for (size_t s = 0; s < sample_count; ++s) {
                                float clamped = std::clamp(fsrc[s], -1.0f, 1.0f);
                                idst[s] = static_cast<int16_t>(clamped * 32767.0f);
                            }
                        } else {
                            pkt.data.assign(ptr, ptr + len);
                        }

                        UINT32 clean_pt = 0;
                        if (SUCCEEDED(
                                out_sample->GetUINT32(MFSampleExtension_CleanPoint, &clean_pt)) &&
                            clean_pt != 0) {
                            pkt.flags |= AMEDIACODEC_BUFFER_FLAG_KEY_FRAME;
                        }

                        if (is_video_ && config_.is_encoder) {
                            bool has_idr = false;
                            std::vector<uint8_t> inline_cfg;
                            ContainsIdrOrConfigNals(pkt.data, &has_idr, &inline_cfg);
                            if (has_idr) {
                                pkt.flags |= AMEDIACODEC_BUFFER_FLAG_KEY_FRAME;
                            }
                            if (!inline_cfg.empty() && csd0_.empty()) {
                                csd0_ = std::move(inline_cfg);
                            }
                        }

                        out_packets.push_back(std::move(pkt));
                        contig->Unlock();
                    }
                    contig->Release();
                }
                out_sample->Release();
            }
        }

        ok = true;
    });
    return ok;
}

void WmfTransformWrapper::SetParameters(int32_t bitrate, bool request_sync_frame)
{
    if (!mft_ || !config_.is_encoder) {
        return;
    }
    WmfManager::Instance().DispatchSync([&]() {
        ICodecAPI *codec_api = nullptr;
        if (FAILED(mft_->QueryInterface(IID_PPV_ARGS(&codec_api))) || !codec_api) {
            return;
        }

        if (bitrate > 0) {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_UI4;
            v.ulVal = static_cast<ULONG>(bitrate);
            codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v);
        }

        if (is_video_ && config_.i_frame_interval > 0) {
            VARIANT gop;
            VariantInit(&gop);
            gop.vt = VT_UI4;
            uint32_t fps = static_cast<uint32_t>(std::max(1.0f, config_.frame_rate));
            gop.ulVal = fps * static_cast<uint32_t>(config_.i_frame_interval);
            codec_api->SetValue(&CODECAPI_AVEncMPVGOPSize, &gop);
        }

        if (is_video_ && request_sync_frame) {
            VARIANT sync_v;
            VariantInit(&sync_v);
            sync_v.vt = VT_UI4;
            sync_v.ulVal = 1;
            codec_api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &sync_v);
        }

        codec_api->Release();
    });
}

void ConvertNv12ToRgba(const uint8_t *nv12, int32_t y_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba)
{
    if (!nv12 || width <= 0 || height <= 0) {
        return;
    }
    int32_t stride = std::max(width, y_stride);
    out_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    const uint8_t *y_plane = nv12;
    const uint8_t *uv_plane = nv12 + static_cast<size_t>(stride) * static_cast<size_t>(height);

    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *y_row = y_plane + static_cast<size_t>(y) * static_cast<size_t>(stride);
        const uint8_t *uv_row = uv_plane + static_cast<size_t>(y / 2) * static_cast<size_t>(stride);
        uint8_t *dst_row =
            out_rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;

        for (int32_t x = 0; x < width; ++x) {
            int32_t uv_x = (x / 2) * 2;
            YuvToRgbPixel(y_row[x], uv_row[uv_x], uv_row[uv_x + 1], &dst_row[x * 4]);
        }
    }
}

void ConvertI420ToRgba(const uint8_t *i420, int32_t y_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba)
{
    if (!i420 || width <= 0 || height <= 0) {
        return;
    }
    int32_t stride = std::max(width, y_stride);
    int32_t uv_stride = stride / 2;
    out_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    const uint8_t *y_plane = i420;
    const uint8_t *u_plane = y_plane + static_cast<size_t>(stride) * static_cast<size_t>(height);
    const uint8_t *v_plane =
        u_plane + static_cast<size_t>(uv_stride) * static_cast<size_t>((height + 1) / 2);

    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *y_row = y_plane + static_cast<size_t>(y) * static_cast<size_t>(stride);
        const uint8_t *u_row =
            u_plane + static_cast<size_t>(y / 2) * static_cast<size_t>(uv_stride);
        const uint8_t *v_row =
            v_plane + static_cast<size_t>(y / 2) * static_cast<size_t>(uv_stride);
        uint8_t *dst_row =
            out_rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;

        for (int32_t x = 0; x < width; ++x) {
            YuvToRgbPixel(y_row[x], u_row[x / 2], v_row[x / 2], &dst_row[x * 4]);
        }
    }
}

void ConvertRgbaToNv12(const uint8_t *rgba, int32_t rgba_stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_nv12)
{
    if (!rgba || width <= 0 || height <= 0) {
        return;
    }
    int32_t src_stride = std::max(width * 4, rgba_stride);
    size_t y_bytes = static_cast<size_t>(width) * static_cast<size_t>(height);
    size_t uv_bytes = static_cast<size_t>(width) * static_cast<size_t>((height + 1) / 2);
    out_nv12.resize(y_bytes + uv_bytes);

    uint8_t *y_plane = out_nv12.data();
    uint8_t *uv_plane = out_nv12.data() + y_bytes;

    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *src_row = rgba + static_cast<size_t>(y) * static_cast<size_t>(src_stride);
        uint8_t *y_row = y_plane + static_cast<size_t>(y) * static_cast<size_t>(width);

        for (int32_t x = 0; x < width; ++x) {
            int32_t r = src_row[x * 4 + 0];
            int32_t g = src_row[x * 4 + 1];
            int32_t b = src_row[x * 4 + 2];
            int32_t y_val = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y_row[x] = ClampByte(y_val);
        }

        if ((y & 1) == 0) {
            const uint8_t *src_row2 =
                (y + 1 < height)
                    ? (rgba + static_cast<size_t>(y + 1) * static_cast<size_t>(src_stride))
                    : src_row;
            uint8_t *uv_row = uv_plane + static_cast<size_t>(y / 2) * static_cast<size_t>(width);

            for (int32_t x = 0; x + 1 < width; x += 2) {
                int32_t r = (src_row[x * 4 + 0] + src_row[(x + 1) * 4 + 0] + src_row2[x * 4 + 0] +
                             src_row2[(x + 1) * 4 + 0] + 2) /
                            4;
                int32_t g = (src_row[x * 4 + 1] + src_row[(x + 1) * 4 + 1] + src_row2[x * 4 + 1] +
                             src_row2[(x + 1) * 4 + 1] + 2) /
                            4;
                int32_t b = (src_row[x * 4 + 2] + src_row[(x + 1) * 4 + 2] + src_row2[x * 4 + 2] +
                             src_row2[(x + 1) * 4 + 2] + 2) /
                            4;
                int32_t u_val = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int32_t v_val = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                uv_row[x] = ClampByte(u_val);
                uv_row[x + 1] = ClampByte(v_val);
            }
        }
    }
}

} // namespace portage::media
