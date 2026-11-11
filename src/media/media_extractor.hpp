#pragma once

#include "media/media_format.hpp"
#include "media/media_types.hpp"
#include "media/wmf_decoder.hpp"

#include <memory>
#include <vector>

struct AMediaExtractor {
    std::unique_ptr<portage::media::WmfSourceReaderWrapper> reader{
        std::make_unique<portage::media::WmfSourceReaderWrapper>()};
    int64_t last_pts_us{0};
    uint32_t last_flags{0};
    size_t last_track{0};
    int64_t last_sample_size{0};
    std::vector<uint8_t> cached_sample{};
    bool has_cached_sample{false};
};

extern "C" {

__attribute__((sysv_abi)) AMediaExtractor *AMediaExtractor_new();

__attribute__((sysv_abi)) media_status_t AMediaExtractor_delete(AMediaExtractor *extractor);

__attribute__((sysv_abi)) media_status_t AMediaExtractor_setDataSourceFd(AMediaExtractor *extractor,
                                                                         int fd, int64_t offset,
                                                                         int64_t length);

__attribute__((sysv_abi)) media_status_t AMediaExtractor_setDataSource(AMediaExtractor *extractor,
                                                                       const char *location);

__attribute__((sysv_abi)) size_t AMediaExtractor_getTrackCount(AMediaExtractor *extractor);

__attribute__((sysv_abi)) AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *extractor,
                                                                       size_t idx);

__attribute__((sysv_abi)) media_status_t AMediaExtractor_selectTrack(AMediaExtractor *extractor,
                                                                     size_t idx);

__attribute__((sysv_abi)) media_status_t AMediaExtractor_unselectTrack(AMediaExtractor *extractor,
                                                                       size_t idx);

__attribute__((sysv_abi)) int64_t AMediaExtractor_readSampleData(AMediaExtractor *extractor,
                                                                 uint8_t *buffer, size_t capacity);

__attribute__((sysv_abi)) int64_t AMediaExtractor_getSampleSize(AMediaExtractor *extractor);

__attribute__((sysv_abi)) int AMediaExtractor_getSampleTrackIndex(AMediaExtractor *extractor);

__attribute__((sysv_abi)) int64_t AMediaExtractor_getSampleTime(AMediaExtractor *extractor);

__attribute__((sysv_abi)) uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor *extractor);

__attribute__((sysv_abi)) bool AMediaExtractor_advance(AMediaExtractor *extractor);

__attribute__((sysv_abi)) media_status_t AMediaExtractor_seekTo(AMediaExtractor *extractor,
                                                                int64_t timeUs, int mode);

} // extern "C"
