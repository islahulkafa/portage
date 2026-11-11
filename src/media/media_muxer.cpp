#include "media/media_muxer.hpp"
#include "media/mf_byte_stream.hpp"
#include "media/wmf_decoder.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct MuxTrack {
    bool is_video{false};
    IMFMediaType *media_type{nullptr};
    DWORD sink_stream_index{0};
    int64_t last_pts_us{-1};
};

IMFMediaType *BuildVideoMuxType(const AMediaFormat *fmt, std::string_view mime)
{
    int32_t width = 128;
    int32_t height = 128;
    int32_t bitrate = 2000000;
    float fps = 30.0f;
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_HEIGHT, &height);
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_BIT_RATE, &bitrate);
    AMediaFormat_getFloat(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_FRAME_RATE, &fps);

    IMFMediaType *type = nullptr;
    if (FAILED(MFCreateMediaType(&type)) || !type) {
        return nullptr;
    }

    GUID subtype = (mime == "video/hevc") ? MFVideoFormat_HEVC : MFVideoFormat_H264;
    UINT32 fps_num = (fps > 0.0f) ? static_cast<UINT32>(std::lround(fps)) : 30u;

    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    type->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate > 0 ? bitrate : 2000000));
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(type, MF_MT_FRAME_SIZE, static_cast<UINT32>(std::max(1, width)),
                       static_cast<UINT32>(std::max(1, height)));
    MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps_num, 1u);
    MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1u, 1u);

    std::vector<uint8_t> seq_hdr;
    void *p = nullptr;
    size_t len = 0;
    if (AMediaFormat_getBuffer(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_CSD_0, &p, &len) &&
        p && len > 0) {
        seq_hdr.insert(seq_hdr.end(), static_cast<const uint8_t *>(p),
                       static_cast<const uint8_t *>(p) + len);
    }
    if (AMediaFormat_getBuffer(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_CSD_1, &p, &len) &&
        p && len > 0) {
        seq_hdr.insert(seq_hdr.end(), static_cast<const uint8_t *>(p),
                       static_cast<const uint8_t *>(p) + len);
    }
    if (!seq_hdr.empty()) {
        type->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, seq_hdr.data(),
                      static_cast<UINT32>(seq_hdr.size()));
    }
    return type;
}

IMFMediaType *BuildAudioMuxType(const AMediaFormat *fmt)
{
    int32_t sample_rate = 44100;
    int32_t channels = 2;
    int32_t bitrate = 128000;
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_SAMPLE_RATE,
                          &sample_rate);
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_CHANNEL_COUNT,
                          &channels);
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_BIT_RATE, &bitrate);

    IMFMediaType *type = nullptr;
    if (FAILED(MFCreateMediaType(&type)) || !type) {
        return nullptr;
    }

    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                    static_cast<UINT32>(sample_rate > 0 ? sample_rate : 44100));
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channels > 0 ? channels : 2));
    type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                    static_cast<UINT32>(bitrate > 0 ? bitrate / 8 : 16000));
    type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);

    std::vector<uint8_t> asc;
    void *p = nullptr;
    size_t len = 0;
    if (AMediaFormat_getBuffer(const_cast<AMediaFormat *>(fmt), AMEDIAFORMAT_KEY_CSD_0, &p, &len) &&
        p && len > 0) {
        asc.assign(static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + len);
    }

    std::vector<uint8_t> ud(12 + asc.size(), 0);
    ud[2] = 0xFE;
    if (!asc.empty()) {
        std::memcpy(ud.data() + 12, asc.data(), asc.size());
    }
    type->SetBlob(MF_MT_USER_DATA, ud.data(), static_cast<UINT32>(ud.size()));
    return type;
}

} // namespace

struct AMediaMuxer {
    enum class State { Uninitialized, Initialized, Started, Stopped };

    State state{State::Initialized};
    OutputFormat format{AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4};
    portage::media::PortageMfByteStream *byte_stream{nullptr};
    IMFMediaSink *media_sink{nullptr};
    IMFSinkWriter *sink_writer{nullptr};

    int orientation_degrees{0};
    bool has_location{false};
    float latitude{0.0f};
    float longitude{0.0f};

    std::vector<MuxTrack> tracks{};
    int video_track_idx{-1};
    int audio_track_idx{-1};
    uint64_t samples_written{0};
    std::mutex mutex{};

    ~AMediaMuxer();
};

AMediaMuxer::~AMediaMuxer()
{
    std::lock_guard<std::mutex> lock(mutex);
    portage::media::WmfManager::Instance().DispatchSync([this]() {
        if (sink_writer) {
            if (state == State::Started) {
                sink_writer->Finalize();
            }
            sink_writer->Release();
            sink_writer = nullptr;
        }
        if (media_sink) {
            media_sink->Shutdown();
            media_sink->Release();
            media_sink = nullptr;
        }
        for (auto &t : tracks) {
            if (t.media_type) {
                t.media_type->Release();
                t.media_type = nullptr;
            }
        }
    });
    if (byte_stream) {
        byte_stream->Close();
        byte_stream->Release();
        byte_stream = nullptr;
    }
}

extern "C" {

__attribute__((sysv_abi)) AMediaMuxer *AMediaMuxer_new(int fd, OutputFormat format)
{
    if (fd < 0) {
        return nullptr;
    }
    if (format != AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4 &&
        format != AMEDIAMUXER_OUTPUT_FORMAT_THREE_GPP && format != AMEDIAMUXER_OUTPUT_FORMAT_HEIF) {
        return nullptr;
    }
    if (!portage::media::WmfManager::Instance().Initialize()) {
        return nullptr;
    }

    auto *bs = portage::media::PortageMfByteStream::CreateFromFd(fd, 0, -1, true);
    if (!bs) {
        return nullptr;
    }

    auto *mux = new AMediaMuxer();
    mux->format = format;
    mux->byte_stream = bs;
    return mux;
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_delete(AMediaMuxer *muxer)
{
    if (!muxer) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    delete muxer;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_setLocation(AMediaMuxer *muxer, float latitude,
                                                                 float longitude)
{
    if (!muxer || latitude < -90.0f || latitude > 90.0f || longitude < -180.0f ||
        longitude > 180.0f) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Initialized) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    muxer->has_location = true;
    muxer->latitude = latitude;
    muxer->longitude = longitude;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_setOrientationHint(AMediaMuxer *muxer,
                                                                        int degrees)
{
    if (!muxer || (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Initialized) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    muxer->orientation_degrees = degrees;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) ssize_t AMediaMuxer_addTrack(AMediaMuxer *muxer,
                                                       const AMediaFormat *format)
{
    if (!muxer || !format) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Initialized) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    const char *mime_cstr = nullptr;
    if (!AMediaFormat_getString(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_MIME,
                                &mime_cstr) ||
        !mime_cstr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::string_view mime(mime_cstr);

    MuxTrack track{};
    if (mime == "video/avc" || mime == "video/hevc") {
        if (muxer->video_track_idx >= 0) {
            return AMEDIA_ERROR_UNSUPPORTED;
        }
        track.is_video = true;
        portage::media::WmfManager::Instance().DispatchSync(
            [&]() { track.media_type = BuildVideoMuxType(format, mime); });
        if (!track.media_type) {
            return AMEDIA_ERROR_UNKNOWN;
        }
        muxer->video_track_idx = static_cast<int>(muxer->tracks.size());
    } else if (mime == "audio/mp4a-latm") {
        if (muxer->audio_track_idx >= 0) {
            return AMEDIA_ERROR_UNSUPPORTED;
        }
        track.is_video = false;
        portage::media::WmfManager::Instance().DispatchSync(
            [&]() { track.media_type = BuildAudioMuxType(format); });
        if (!track.media_type) {
            return AMEDIA_ERROR_UNKNOWN;
        }
        muxer->audio_track_idx = static_cast<int>(muxer->tracks.size());
    } else {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    muxer->tracks.push_back(track);
    return static_cast<ssize_t>(muxer->tracks.size() - 1);
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_start(AMediaMuxer *muxer)
{
    if (!muxer) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Initialized || muxer->tracks.empty()) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    IMFMediaType *video_type =
        (muxer->video_track_idx >= 0)
            ? muxer->tracks[static_cast<size_t>(muxer->video_track_idx)].media_type
            : nullptr;
    IMFMediaType *audio_type =
        (muxer->audio_track_idx >= 0)
            ? muxer->tracks[static_cast<size_t>(muxer->audio_track_idx)].media_type
            : nullptr;

    DWORD next_sink_stream = 0;
    if (muxer->video_track_idx >= 0) {
        muxer->tracks[static_cast<size_t>(muxer->video_track_idx)].sink_stream_index =
            next_sink_stream++;
    }
    if (muxer->audio_track_idx >= 0) {
        muxer->tracks[static_cast<size_t>(muxer->audio_track_idx)].sink_stream_index =
            next_sink_stream++;
    }

    media_status_t status = AMEDIA_OK;
    portage::media::WmfManager::Instance().DispatchSync([&]() {
        HRESULT hr = E_FAIL;
        if (muxer->format == AMEDIAMUXER_OUTPUT_FORMAT_THREE_GPP) {
            hr = MFCreate3GPMediaSink(muxer->byte_stream, video_type, audio_type,
                                      &muxer->media_sink);
        } else {
            hr = MFCreateMPEG4MediaSink(muxer->byte_stream, video_type, audio_type,
                                        &muxer->media_sink);
        }
        if (FAILED(hr) || !muxer->media_sink) {
            status = AMEDIA_ERROR_UNKNOWN;
            return;
        }

        hr = MFCreateSinkWriterFromMediaSink(muxer->media_sink, nullptr, &muxer->sink_writer);
        if (FAILED(hr) || !muxer->sink_writer) {
            muxer->media_sink->Shutdown();
            muxer->media_sink->Release();
            muxer->media_sink = nullptr;
            status = AMEDIA_ERROR_UNKNOWN;
            return;
        }

        for (const auto &t : muxer->tracks) {
            hr = muxer->sink_writer->SetInputMediaType(t.sink_stream_index, t.media_type, nullptr);
            if (FAILED(hr)) {
                muxer->sink_writer->Release();
                muxer->sink_writer = nullptr;
                muxer->media_sink->Shutdown();
                muxer->media_sink->Release();
                muxer->media_sink = nullptr;
                status = AMEDIA_ERROR_UNSUPPORTED;
                return;
            }
        }

        hr = muxer->sink_writer->BeginWriting();
        if (FAILED(hr)) {
            muxer->sink_writer->Release();
            muxer->sink_writer = nullptr;
            muxer->media_sink->Shutdown();
            muxer->media_sink->Release();
            muxer->media_sink = nullptr;
            status = AMEDIA_ERROR_IO;
            return;
        }
    });

    if (status != AMEDIA_OK) {
        return status;
    }

    muxer->state = AMediaMuxer::State::Started;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_writeSampleData(
    AMediaMuxer *muxer, size_t trackIdx, const uint8_t *data, const AMediaCodecBufferInfo *info)
{
    if (!muxer || !data || !info) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Started || !muxer->sink_writer ||
        trackIdx >= muxer->tracks.size()) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    if (info->size <= 0) {
        return AMEDIA_OK;
    }
    if ((info->flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
        return AMEDIA_OK;
    }

    media_status_t status = AMEDIA_ERROR_IO;
    portage::media::WmfManager::Instance().DispatchSync([&]() {
        IMFSample *sample = nullptr;
        IMFMediaBuffer *mb = nullptr;
        DWORD byte_len = static_cast<DWORD>(info->size);
        if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(byte_len, &mb))) {
            if (sample) {
                sample->Release();
            }
            status = AMEDIA_ERROR_UNKNOWN;
            return;
        }

        BYTE *dst = nullptr;
        if (SUCCEEDED(mb->Lock(&dst, nullptr, nullptr)) && dst) {
            std::memcpy(dst, data + info->offset, byte_len);
            mb->Unlock();
            mb->SetCurrentLength(byte_len);
        }
        sample->AddBuffer(mb);
        mb->Release();

        auto &track = muxer->tracks[trackIdx];
        int64_t pts_us = std::max<int64_t>(0, info->presentationTimeUs);
        if (track.last_pts_us >= 0 && pts_us <= track.last_pts_us) {
            pts_us = track.last_pts_us + 1;
        }
        int64_t dur_us = (track.last_pts_us >= 0 && pts_us > track.last_pts_us)
                             ? (pts_us - track.last_pts_us)
                             : (track.is_video ? 33333LL : 23220LL);
        track.last_pts_us = pts_us;

        sample->SetSampleTime(pts_us * 10);
        sample->SetSampleDuration(dur_us * 10);
        if ((info->flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME) != 0) {
            sample->SetUINT32(MFSampleExtension_CleanPoint, 1);
        }

        HRESULT hr = muxer->sink_writer->WriteSample(track.sink_stream_index, sample);
        sample->Release();
        if (SUCCEEDED(hr)) {
            muxer->samples_written++;
            status = AMEDIA_OK;
        }
    });
    return status;
}

__attribute__((sysv_abi)) media_status_t AMediaMuxer_stop(AMediaMuxer *muxer)
{
    if (!muxer) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(muxer->mutex);
    if (muxer->state != AMediaMuxer::State::Started) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    HRESULT hr = S_OK;
    portage::media::WmfManager::Instance().DispatchSync([&]() {
        if (muxer->sink_writer) {
            hr = muxer->sink_writer->Finalize();
            if (muxer->samples_written == 0) {
                hr = S_OK;
            }
            muxer->sink_writer->Release();
            muxer->sink_writer = nullptr;
        }
        if (muxer->media_sink) {
            muxer->media_sink->Shutdown();
            muxer->media_sink->Release();
            muxer->media_sink = nullptr;
        }
    });
    if (muxer->byte_stream) {
        muxer->byte_stream->Flush();
    }
    muxer->state = AMediaMuxer::State::Stopped;
    return SUCCEEDED(hr) ? AMEDIA_OK : AMEDIA_ERROR_IO;
}

} // extern "C"
