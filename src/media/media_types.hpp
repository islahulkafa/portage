#pragma once

#include <cstddef>
#include <cstdint>

typedef enum {
    AMEDIA_OK = 0,
    AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE = 1100,
    AMEDIACODEC_ERROR_RECLAIMED = 1101,
    AMEDIA_ERROR_BASE = -10000,
    AMEDIA_ERROR_UNKNOWN = AMEDIA_ERROR_BASE,
    AMEDIA_ERROR_MALFORMED = AMEDIA_ERROR_BASE - 1,
    AMEDIA_ERROR_UNSUPPORTED = AMEDIA_ERROR_BASE - 2,
    AMEDIA_ERROR_INVALID_OBJECT = AMEDIA_ERROR_BASE - 3,
    AMEDIA_ERROR_INVALID_PARAMETER = AMEDIA_ERROR_BASE - 4,
    AMEDIA_ERROR_INVALID_OPERATION = AMEDIA_ERROR_BASE - 5,
    AMEDIA_ERROR_END_OF_STREAM = AMEDIA_ERROR_BASE - 6,
    AMEDIA_ERROR_IO = AMEDIA_ERROR_BASE - 7,
    AMEDIA_ERROR_WOULD_BLOCK = AMEDIA_ERROR_BASE - 8,
    AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE = AMEDIA_ERROR_BASE - 9,
    AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED = AMEDIA_ERROR_BASE - 10,
    AMEDIA_IMGREADER_CANNOT_DETACH_CLOSED_IMAGE = AMEDIA_ERROR_BASE - 11,
    AMEDIA_IMGREADER_IMAGE_NOT_DETACHED = AMEDIA_ERROR_BASE - 12,
    AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3,
    AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2,
    AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1,
} media_status_t;

enum {
    AMEDIACODEC_BUFFER_FLAG_KEY_FRAME = 1,
    AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG = 2,
    AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4,
    AMEDIACODEC_BUFFER_FLAG_PARTIAL_FRAME = 8,
    AMEDIACODEC_CONFIGURE_FLAG_ENCODE = 1,
};

enum {
    AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC = 0,
    AMEDIAEXTRACTOR_SEEK_NEXT_SYNC = 1,
    AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC = 2,
};

enum {
    AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC = 1,
    AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED = 2,
};

struct AMediaCodecBufferInfo {
    int32_t offset{0};
    int32_t size{0};
    int64_t presentationTimeUs{0};
    uint32_t flags{0};
};
typedef struct AMediaCodecBufferInfo AMediaCodecBufferInfo;

struct AMediaCrypto;
typedef struct AMediaCrypto AMediaCrypto;

inline constexpr const char *AMEDIAFORMAT_KEY_MIME = "mime";
inline constexpr const char *AMEDIAFORMAT_KEY_WIDTH = "width";
inline constexpr const char *AMEDIAFORMAT_KEY_HEIGHT = "height";
inline constexpr const char *AMEDIAFORMAT_KEY_STRIDE = "stride";
inline constexpr const char *AMEDIAFORMAT_KEY_COLOR_FORMAT = "color-format";
inline constexpr const char *AMEDIAFORMAT_KEY_FRAME_RATE = "frame-rate";
inline constexpr const char *AMEDIAFORMAT_KEY_BIT_RATE = "bitrate";
inline constexpr const char *AMEDIAFORMAT_KEY_DURATION = "durationUs";
inline constexpr const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channel-count";
inline constexpr const char *AMEDIAFORMAT_KEY_SAMPLE_RATE = "sample-rate";
inline constexpr const char *AMEDIAFORMAT_KEY_MAX_INPUT_SIZE = "max-input-size";
inline constexpr const char *AMEDIAFORMAT_KEY_CSD = "csd";
inline constexpr const char *AMEDIAFORMAT_KEY_CSD_0 = "csd-0";
inline constexpr const char *AMEDIAFORMAT_KEY_CSD_1 = "csd-1";
inline constexpr const char *AMEDIAFORMAT_KEY_CSD_2 = "csd-2";
inline constexpr const char *AMEDIAFORMAT_KEY_I_FRAME_INTERVAL = "i-frame-interval";
inline constexpr const char *AMEDIAFORMAT_KEY_PROFILE = "profile";
inline constexpr const char *AMEDIAFORMAT_KEY_LEVEL = "level";
inline constexpr const char *AMEDIAFORMAT_KEY_CAPTURE_RATE = "capture-rate";
inline constexpr const char *AMEDIAFORMAT_KEY_ROTATION = "rotation-degrees";
inline constexpr const char *AMEDIAFORMAT_KEY_IS_SYNC_FRAME = "is-sync-frame";
inline constexpr const char *AMEDIAFORMAT_KEY_REQUEST_SYNC_FRAME = "request-sync";
inline constexpr const char *AMEDIAFORMAT_KEY_VIDEO_BITRATE = "video-bitrate";

inline constexpr int32_t COLOR_FormatYUV420SemiPlanar = 21;
inline constexpr int32_t COLOR_FormatYUV420Flexible = 0x7F420888;
inline constexpr int32_t COLOR_FormatSurface = 0x7F000789;
inline constexpr int32_t COLOR_Format32bitRGBA8888 = 0x7F00A000;
