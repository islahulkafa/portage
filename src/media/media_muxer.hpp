#pragma once

#include "media/media_format.hpp"
#include "media/media_types.hpp"

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

enum OutputFormat {
    AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4 = 0,
    AMEDIAMUXER_OUTPUT_FORMAT_WEBM = 1,
    AMEDIAMUXER_OUTPUT_FORMAT_THREE_GPP = 2,
    AMEDIAMUXER_OUTPUT_FORMAT_HEIF = 3,
    AMEDIAMUXER_OUTPUT_FORMAT_OGG = 4,
};

struct AMediaMuxer;
typedef struct AMediaMuxer AMediaMuxer;

extern "C" {

__attribute__((sysv_abi)) AMediaMuxer *AMediaMuxer_new(int fd, OutputFormat format);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_delete(AMediaMuxer *muxer);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_setLocation(AMediaMuxer *muxer, float latitude,
                                                                 float longitude);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_setOrientationHint(AMediaMuxer *muxer,
                                                                        int degrees);

__attribute__((sysv_abi)) ssize_t AMediaMuxer_addTrack(AMediaMuxer *muxer,
                                                       const AMediaFormat *format);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_start(AMediaMuxer *muxer);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_writeSampleData(
    AMediaMuxer *muxer, size_t trackIdx, const uint8_t *data, const AMediaCodecBufferInfo *info);

__attribute__((sysv_abi)) media_status_t AMediaMuxer_stop(AMediaMuxer *muxer);

} // extern "C"
