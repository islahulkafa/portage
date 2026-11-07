#include "media/wmf_decoder.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace portage::media {

namespace stdfs = std::filesystem;

WmfManager &WmfManager::Instance() noexcept
{
    static WmfManager instance;
    return instance;
}

WmfManager::~WmfManager()
{
    Shutdown();
}

bool WmfManager::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        return true;
    }

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (SUCCEEDED(hr)) {
        initialized_.store(true);
        return true;
    }
    return false;
}

void WmfManager::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        MFShutdown();
        initialized_.store(false);
    }
}

WmfSourceReaderWrapper::~WmfSourceReaderWrapper()
{
    Close();
}

void WmfSourceReaderWrapper::Close()
{
    if (reader_) {
        reader_->Release();
        reader_ = nullptr;
    }
    tracks_.clear();
    selected_tracks_.clear();
    current_selected_track_ = 0;
    current_pts_us_ = 0;
    is_synthetic_ = false;
    synthetic_frame_index_ = 0;
}

bool WmfSourceReaderWrapper::OpenUrl(const std::wstring &url)
{
    Close();
    if (!WmfManager::Instance().Initialize()) {
        return false;
    }

    HRESULT hr = MFCreateSourceReaderFromURL(url.c_str(), nullptr, &reader_);
    if (FAILED(hr) || !reader_) {
        return false;
    }

    ProbeTracks();
    return !tracks_.empty();
}

bool WmfSourceReaderWrapper::OpenFile(const std::string &path)
{
    std::error_code ec;
    if (!stdfs::exists(path, ec)) {
        return false;
    }

    std::wstring wide_path(path.begin(), path.end());
    return OpenUrl(wide_path);
}

bool WmfSourceReaderWrapper::OpenSynthetic(std::string_view mime, int32_t width, int32_t height,
                                           int64_t duration_us)
{
    Close();
    is_synthetic_ = true;

    MediaTrackInfo video_track{};
    video_track.index = 0;
    video_track.mime = mime.empty() ? "video/avc" : std::string(mime);
    video_track.width = (width > 0) ? width : 128;
    video_track.height = (height > 0) ? height : 128;
    video_track.stride = video_track.width * 4;
    video_track.frame_rate = 30.0f;
    video_track.duration_us = (duration_us > 0) ? duration_us : 5000000;
    video_track.csd0 = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E};

    MediaTrackInfo audio_track{};
    audio_track.index = 1;
    audio_track.mime = "audio/mp4a-latm";
    audio_track.sample_rate = 44100;
    audio_track.channel_count = 2;
    audio_track.duration_us = video_track.duration_us;
    audio_track.csd0 = {0x12, 0x10};

    tracks_.push_back(std::move(video_track));
    tracks_.push_back(std::move(audio_track));
    selected_tracks_.resize(2, false);
    return true;
}

void WmfSourceReaderWrapper::ProbeTracks()
{
    if (!reader_) {
        return;
    }

    tracks_.clear();
    DWORD stream_index = 0;
    while (true) {
        IMFMediaType *media_type = nullptr;
        HRESULT hr = reader_->GetNativeMediaType(stream_index, 0, &media_type);
        if (FAILED(hr) || !media_type) {
            break;
        }

        MediaTrackInfo track{};
        track.index = stream_index;

        GUID major_type = GUID_NULL;
        media_type->GetMajorType(&major_type);

        if (major_type == MFMediaType_Video) {
            track.mime = "video/avc";
            UINT32 w = 0, h = 0;
            MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &w, &h);
            track.width = static_cast<int32_t>(w);
            track.height = static_cast<int32_t>(h);
            track.stride = track.width * 4;

            UINT32 num = 30, den = 1;
            MFGetAttributeRatio(media_type, MF_MT_FRAME_RATE, &num, &den);
            if (den != 0) {
                track.frame_rate = static_cast<float>(num) / static_cast<float>(den);
            }
        } else if (major_type == MFMediaType_Audio) {
            track.mime = "audio/mp4a-latm";
            UINT32 sr = 0, ch = 0;
            media_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
            media_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
            track.sample_rate = static_cast<int32_t>(sr);
            track.channel_count = static_cast<int32_t>(ch);
        }

        media_type->Release();

        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader_->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                       MF_PD_DURATION, &var))) {
            if (var.vt == VT_UI8) {
                track.duration_us = static_cast<int64_t>(var.uhVal.QuadPart) / 10;
            }
            PropVariantClear(&var);
        }

        tracks_.push_back(std::move(track));
        stream_index++;
    }

    selected_tracks_.resize(tracks_.size(), false);
}

size_t WmfSourceReaderWrapper::GetTrackCount() const noexcept
{
    return tracks_.size();
}

const MediaTrackInfo *WmfSourceReaderWrapper::GetTrack(size_t index) const noexcept
{
    if (index < tracks_.size()) {
        return &tracks_[index];
    }
    return nullptr;
}

bool WmfSourceReaderWrapper::SelectTrack(size_t index)
{
    if (index >= tracks_.size()) {
        return false;
    }
    selected_tracks_[index] = true;
    current_selected_track_ = index;

    if (reader_) {
        reader_->SetStreamSelection(static_cast<DWORD>(index), TRUE);
    }
    return true;
}

bool WmfSourceReaderWrapper::UnselectTrack(size_t index)
{
    if (index >= tracks_.size()) {
        return false;
    }
    selected_tracks_[index] = false;
    if (reader_) {
        reader_->SetStreamSelection(static_cast<DWORD>(index), FALSE);
    }
    return true;
}

int64_t WmfSourceReaderWrapper::ReadSample(uint8_t *buffer, size_t capacity, size_t *out_track,
                                           int64_t *out_pts_us, uint32_t *out_flags)
{
    if (!buffer || capacity == 0) {
        return -1;
    }

    if (out_track) {
        *out_track = current_selected_track_;
    }

    if (is_synthetic_) {
        if (tracks_.empty()) {
            return -1;
        }
        const auto &track = tracks_[current_selected_track_];
        if (current_pts_us_ >= track.duration_us) {
            if (out_flags) {
                *out_flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            }
            return 0;
        }

        size_t write_bytes = 0;
        if (track.mime.rfind("video/", 0) == 0) {
            write_bytes = std::min(capacity, static_cast<size_t>(track.width * track.height * 4));
            uint32_t *pix = reinterpret_cast<uint32_t *>(buffer);
            size_t count = write_bytes / 4;
            uint32_t color = 0xFF000000 | ((synthetic_frame_index_ * 20) & 0xFF);
            for (size_t i = 0; i < count; ++i) {
                pix[i] = color;
            }
            if (out_pts_us) {
                *out_pts_us = current_pts_us_;
            }
            if (out_flags) {
                *out_flags = (synthetic_frame_index_ == 0) ? AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC : 0;
            }
            current_pts_us_ += 33333; // ~30 fps
            synthetic_frame_index_++;
        } else {
            write_bytes = std::min(capacity, static_cast<size_t>(1024));
            int16_t *pcm = reinterpret_cast<int16_t *>(buffer);
            size_t count = write_bytes / 2;
            for (size_t i = 0; i < count; ++i) {
                pcm[i] = static_cast<int16_t>((i % 100) * 100);
            }
            if (out_pts_us) {
                *out_pts_us = current_pts_us_;
            }
            if (out_flags) {
                *out_flags = AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC;
            }
            current_pts_us_ += 23219; // ~1024 samples at 44.1kHz
        }
        return static_cast<int64_t>(write_bytes);
    }

    if (!reader_) {
        return -1;
    }

    DWORD actual_stream_index = 0;
    DWORD stream_flags = 0;
    LONGLONG timestamp = 0;
    IMFSample *sample = nullptr;

    HRESULT hr = reader_->ReadSample(static_cast<DWORD>(current_selected_track_), 0,
                                     &actual_stream_index, &stream_flags, &timestamp, &sample);
    if (FAILED(hr)) {
        return -1;
    }

    if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (out_flags) {
            *out_flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
        }
        if (sample) {
            sample->Release();
        }
        return 0;
    }

    if (!sample) {
        return 0;
    }

    IMFMediaBuffer *media_buffer = nullptr;
    hr = sample->ConvertToContiguousBuffer(&media_buffer);
    if (FAILED(hr) || !media_buffer) {
        sample->Release();
        return -1;
    }

    BYTE *data = nullptr;
    DWORD current_length = 0;
    hr = media_buffer->Lock(&data, nullptr, &current_length);
    if (FAILED(hr) || !data) {
        media_buffer->Release();
        sample->Release();
        return -1;
    }

    size_t copy_bytes = std::min(capacity, static_cast<size_t>(current_length));
    std::memcpy(buffer, data, copy_bytes);

    media_buffer->Unlock();
    media_buffer->Release();
    sample->Release();

    if (out_pts_us) {
        *out_pts_us = timestamp / 10;
    }
    if (out_flags) {
        *out_flags = (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                         ? static_cast<uint32_t>(AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
                         : static_cast<uint32_t>(AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC);
    }
    return static_cast<int64_t>(copy_bytes);
}

bool WmfSourceReaderWrapper::SeekTo(int64_t time_us)
{
    if (is_synthetic_) {
        current_pts_us_ = std::max<int64_t>(0, time_us);
        return true;
    }

    if (!reader_) {
        return false;
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = time_us * 10; // convert us to 100ns units

    HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    return SUCCEEDED(hr);
}

} // namespace portage::media
