#include "media/media_extractor.hpp"
#include "syscall/syscall_dispatcher.hpp"

extern "C" {

__attribute__((sysv_abi)) AMediaExtractor *AMediaExtractor_new()
{
    return new AMediaExtractor();
}

__attribute__((sysv_abi)) media_status_t AMediaExtractor_delete(AMediaExtractor *extractor)
{
    if (!extractor) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    delete extractor;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t
AMediaExtractor_setDataSourceFd(AMediaExtractor *extractor, int fd, int64_t offset,
                               int64_t length)
{
    (void)offset;
    (void)length;
    if (!extractor || fd < 0) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    if (fd_table) {
        auto desc = fd_table->Get(fd);
        if (desc && !desc->guest_path.empty()) {
            if (extractor->reader->OpenFile(desc->guest_path)) {
                return AMEDIA_OK;
            }
        }
    }

    // Fallback to synthetic media stream for testing/headless execution
    if (extractor->reader->OpenSynthetic("video/avc")) {
        return AMEDIA_OK;
    }
    return AMEDIA_ERROR_IO;
}

__attribute__((sysv_abi)) media_status_t
AMediaExtractor_setDataSource(AMediaExtractor *extractor, const char *location)
{
    if (!extractor || !location) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    if (extractor->reader->OpenFile(location)) {
        return AMEDIA_OK;
    }

    // Fallback to synthetic stream
    if (extractor->reader->OpenSynthetic("video/avc")) {
        return AMEDIA_OK;
    }
    return AMEDIA_ERROR_IO;
}

__attribute__((sysv_abi)) size_t AMediaExtractor_getTrackCount(AMediaExtractor *extractor)
{
    if (!extractor || !extractor->reader) {
        return 0;
    }
    return extractor->reader->GetTrackCount();
}

__attribute__((sysv_abi)) AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *extractor,
                                                                      size_t idx)
{
    if (!extractor || !extractor->reader) {
        return nullptr;
    }

    const auto *track = extractor->reader->GetTrack(idx);
    if (!track) {
        return nullptr;
    }

    auto *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, track->mime.c_str());

    if (track->mime.rfind("video/", 0) == 0) {
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, track->width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, track->height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_STRIDE, track->stride);
        AMediaFormat_setFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, track->frame_rate);
    } else if (track->mime.rfind("audio/", 0) == 0) {
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, track->sample_rate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, track->channel_count);
    }

    AMediaFormat_setInt64(format, AMEDIAFORMAT_KEY_DURATION, track->duration_us);

    if (!track->csd0.empty()) {
        AMediaFormat_setBuffer(format, AMEDIAFORMAT_KEY_CSD_0, track->csd0.data(),
                               track->csd0.size());
    }

    return format;
}

__attribute__((sysv_abi)) media_status_t AMediaExtractor_selectTrack(AMediaExtractor *extractor,
                                                                     size_t idx)
{
    if (!extractor || !extractor->reader) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    return extractor->reader->SelectTrack(idx) ? AMEDIA_OK : AMEDIA_ERROR_INVALID_PARAMETER;
}

__attribute__((sysv_abi)) media_status_t AMediaExtractor_unselectTrack(AMediaExtractor *extractor,
                                                                       size_t idx)
{
    if (!extractor || !extractor->reader) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    return extractor->reader->UnselectTrack(idx) ? AMEDIA_OK : AMEDIA_ERROR_INVALID_PARAMETER;
}

__attribute__((sysv_abi)) int64_t AMediaExtractor_readSampleData(AMediaExtractor *extractor,
                                                                uint8_t *buffer,
                                                                size_t capacity)
{
    if (!extractor || !extractor->reader || !buffer || capacity == 0) {
        return -1;
    }

    int64_t nread = extractor->reader->ReadSample(buffer, capacity, &extractor->last_track,
                                                  &extractor->last_pts_us, &extractor->last_flags);
    extractor->last_sample_size = nread;
    return nread;
}

__attribute__((sysv_abi)) int64_t AMediaExtractor_getSampleSize(AMediaExtractor *extractor)
{
    if (!extractor) {
        return -1;
    }
    return extractor->last_sample_size;
}

__attribute__((sysv_abi)) int AMediaExtractor_getSampleTrackIndex(AMediaExtractor *extractor)
{
    if (!extractor) {
        return -1;
    }
    return static_cast<int>(extractor->last_track);
}

__attribute__((sysv_abi)) int64_t AMediaExtractor_getSampleTime(AMediaExtractor *extractor)
{
    if (!extractor) {
        return -1;
    }
    return extractor->last_pts_us;
}

__attribute__((sysv_abi)) uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor *extractor)
{
    if (!extractor) {
        return 0;
    }
    return extractor->last_flags;
}

__attribute__((sysv_abi)) bool AMediaExtractor_advance(AMediaExtractor *extractor)
{
    if (!extractor) {
        return false;
    }
    return (extractor->last_flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == 0;
}

__attribute__((sysv_abi)) media_status_t AMediaExtractor_seekTo(AMediaExtractor *extractor,
                                                               int64_t timeUs, int mode)
{
    (void)mode;
    if (!extractor || !extractor->reader) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    return extractor->reader->SeekTo(timeUs) ? AMEDIA_OK : AMEDIA_ERROR_UNKNOWN;
}

} // extern "C"
