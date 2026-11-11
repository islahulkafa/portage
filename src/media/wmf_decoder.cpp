#include "media/wmf_decoder.hpp"
#include "media/mf_byte_stream.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace portage::media {

namespace stdfs = std::filesystem;

namespace {
thread_local bool t_on_wmf_worker = false;
}

WmfManager &WmfManager::Instance() noexcept
{
    static WmfManager instance;
    return instance;
}

WmfManager::~WmfManager() { Shutdown(); }

void WmfManager::WorkerLoop()
{
    t_on_wmf_worker = true;
    HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool mf_ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        initialized_.store(mf_ok);
    }
    done_cv_.notify_all();

    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this]() { return stop_worker_ || !queue_.empty(); });
            if (stop_worker_ && queue_.empty()) {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        if (task) {
            task();
        }
    }

    if (mf_ok) {
        MFShutdown();
    }
    if (SUCCEEDED(com_hr)) {
        CoUninitialize();
    }
    t_on_wmf_worker = false;
}

bool WmfManager::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        return true;
    }

    if (!worker_.joinable()) {
        stop_worker_ = false;
        worker_ = std::thread(&WmfManager::WorkerLoop, this);
        std::unique_lock<std::mutex> lk(queue_mutex_);
        done_cv_.wait(lk, [this]() { return initialized_.load() || !worker_.joinable(); });
    }
    return initialized_.load();
}

void WmfManager::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            stop_worker_ = true;
        }
        queue_cv_.notify_all();
        if (std::this_thread::get_id() == worker_.get_id()) {
            worker_.detach();
        } else {
            worker_.join();
        }
    }
    initialized_.store(false);
}

void WmfManager::DispatchSync(std::function<void()> task)
{
    if (!task) {
        return;
    }
    if (t_on_wmf_worker || !Initialize()) {
        task();
        return;
    }
    std::mutex done_mu;
    std::condition_variable done_cv;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        queue_.push_back([&]() {
            task();
            {
                std::lock_guard<std::mutex> dlk(done_mu);
                finished = true;
            }
            done_cv.notify_one();
        });
    }
    queue_cv_.notify_one();
    std::unique_lock<std::mutex> dlk(done_mu);
    done_cv.wait(dlk, [&finished]() { return finished; });
}

WmfSourceReaderWrapper::~WmfSourceReaderWrapper() { Close(); }

void WmfSourceReaderWrapper::Close()
{
    if (reader_) {
        IMFSourceReader *r = reader_;
        reader_ = nullptr;
        WmfManager::Instance().DispatchSync([r]() { r->Release(); });
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

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        HRESULT hr = MFCreateSourceReaderFromURL(url.c_str(), nullptr, &reader_);
        if (FAILED(hr) || !reader_) {
            return;
        }
        ProbeTracks();
        ok = !tracks_.empty();
    });
    return ok;
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

bool WmfSourceReaderWrapper::OpenFd(int fd, int64_t offset, int64_t length)
{
    Close();
    if (!WmfManager::Instance().Initialize()) {
        return false;
    }

    auto *stream = PortageMfByteStream::CreateFromFd(fd, offset, length, false);
    if (!stream) {
        return false;
    }

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        HRESULT hr = MFCreateSourceReaderFromByteStream(stream, nullptr, &reader_);
        stream->Release();
        if (FAILED(hr) || !reader_) {
            return;
        }
        ProbeTracks();
        ok = !tracks_.empty();
    });
    if (!ok) {
        Close();
    }
    return ok;
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
        GUID sub_type = GUID_NULL;
        media_type->GetMajorType(&major_type);
        media_type->GetGUID(MF_MT_SUBTYPE, &sub_type);

        if (major_type == MFMediaType_Video) {
            if (sub_type == MFVideoFormat_HEVC) {
                track.mime = "video/hevc";
            } else {
                track.mime = "video/avc";
            }
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
            UINT32 csd_len = 0;
            if (SUCCEEDED(media_type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &csd_len)) &&
                csd_len > 0) {
                track.csd0.resize(csd_len);
                media_type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, track.csd0.data(), csd_len,
                                    nullptr);
            }
        } else if (major_type == MFMediaType_Audio) {
            track.mime = (sub_type == MFAudioFormat_MP3) ? "audio/mpeg" : "audio/mp4a-latm";
            UINT32 sr = 0, ch = 0;
            media_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
            media_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
            track.sample_rate = static_cast<int32_t>(sr);
            track.channel_count = static_cast<int32_t>(ch);
            UINT32 ud_len = 0;
            if (SUCCEEDED(media_type->GetBlobSize(MF_MT_USER_DATA, &ud_len)) && ud_len > 0) {
                std::vector<uint8_t> raw(ud_len);
                if (SUCCEEDED(media_type->GetBlob(MF_MT_USER_DATA, raw.data(), ud_len, nullptr))) {
                    if (sub_type == MFAudioFormat_AAC && ud_len > 12) {
                        track.csd0.assign(raw.begin() + 12, raw.end());
                    } else {
                        track.csd0 = std::move(raw);
                    }
                }
            }
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

size_t WmfSourceReaderWrapper::GetTrackCount() const noexcept { return tracks_.size(); }

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
        WmfManager::Instance().DispatchSync(
            [&]() { reader_->SetStreamSelection(static_cast<DWORD>(index), TRUE); });
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
        WmfManager::Instance().DispatchSync(
            [&]() { reader_->SetStreamSelection(static_cast<DWORD>(index), FALSE); });
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

    int64_t ret = -1;
    WmfManager::Instance().DispatchSync([&]() {
        DWORD actual_stream_index = 0;
        DWORD stream_flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = nullptr;

        HRESULT hr = reader_->ReadSample(static_cast<DWORD>(current_selected_track_), 0,
                                         &actual_stream_index, &stream_flags, &timestamp, &sample);
        if (FAILED(hr)) {
            ret = -1;
            return;
        }

        if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (out_flags) {
                *out_flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
            }
            if (sample) {
                sample->Release();
            }
            ret = 0;
            return;
        }

        if (!sample) {
            ret = 0;
            return;
        }

        IMFMediaBuffer *media_buffer = nullptr;
        hr = sample->ConvertToContiguousBuffer(&media_buffer);
        if (FAILED(hr) || !media_buffer) {
            sample->Release();
            ret = -1;
            return;
        }

        BYTE *data = nullptr;
        DWORD current_length = 0;
        hr = media_buffer->Lock(&data, nullptr, &current_length);
        if (FAILED(hr) || !data) {
            media_buffer->Release();
            sample->Release();
            ret = -1;
            return;
        }

        size_t copy_bytes = std::min(capacity, static_cast<size_t>(current_length));
        std::memcpy(buffer, data, copy_bytes);

        UINT32 clean_point = 0;
        bool has_clean_attr =
            SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &clean_point));

        media_buffer->Unlock();
        media_buffer->Release();
        sample->Release();

        if (out_pts_us) {
            *out_pts_us = timestamp / 10;
        }
        if (out_flags) {
            if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                *out_flags = static_cast<uint32_t>(AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
            } else if (!has_clean_attr || clean_point != 0) {
                *out_flags = static_cast<uint32_t>(AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC);
            } else {
                *out_flags = 0;
            }
        }
        ret = static_cast<int64_t>(copy_bytes);
    });
    return ret;
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

    bool ok = false;
    WmfManager::Instance().DispatchSync([&]() {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = time_us * 10; // convert us to 100ns units

        HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        ok = SUCCEEDED(hr);
    });
    return ok;
}

} // namespace portage::media
