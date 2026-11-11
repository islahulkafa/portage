#pragma once

#include "ndk/jnigraphics.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/asset_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum {
    ANDROID_IMAGE_DECODER_SUCCESS = 0,
    ANDROID_IMAGE_DECODER_INCOMPLETE = -1,
    ANDROID_IMAGE_DECODER_ERROR = -2,
    ANDROID_IMAGE_DECODER_INVALID_CONVERSION = -3,
    ANDROID_IMAGE_DECODER_INVALID_SCALE = -4,
    ANDROID_IMAGE_DECODER_BAD_PARAMETER = -5,
    ANDROID_IMAGE_DECODER_INVALID_INPUT = -6,
    ANDROID_IMAGE_DECODER_SEEK_ERROR = -7,
    ANDROID_IMAGE_DECODER_INTERNAL_ERROR = -8,
    ANDROID_IMAGE_DECODER_UNSUPPORTED_FORMAT = -9,
    ANDROID_IMAGE_DECODER_FINISHED = -10,
    ANDROID_IMAGE_DECODER_INVALID_STATE = -11,
};

constexpr int32_t ADATASPACE_UNKNOWN = 0;
constexpr int32_t ADATASPACE_SRGB = 142671872;
constexpr int32_t AIMAGE_DECODER_INFINITE_REPEATS = -1;

struct AImageDecoder;
struct AImageDecoderHeaderInfo;
struct AImageDecoderFrameInfo;

namespace portage::ndk {

struct DecodedImageHeader {
    uint32_t width{0};
    uint32_t height{0};
    int32_t bitmap_format{ANDROID_BITMAP_FORMAT_RGBA_8888};
    uint32_t alpha_flags{ANDROID_BITMAP_FLAGS_ALPHA_PREMUL};
    int32_t data_space{ADATASPACE_SRGB};
    uint32_t frame_count{1};
    bool is_animated{false};
    int32_t repeat_count{AIMAGE_DECODER_INFINITE_REPEATS};
    std::string mime_type{"image/png"};
    std::vector<int64_t> frame_durations_ns{};
    std::vector<ARect> frame_rects{};
};

struct ImageDecodeOptions {
    int32_t target_width{0};
    int32_t target_height{0};
    bool has_crop{false};
    ARect crop_rect{};
    int32_t android_format{0};
    bool unpremul{false};
};

class WicImageCodec {
  public:
    static int InspectImage(std::span<const uint8_t> encoded, DecodedImageHeader &out_header);

    static int DecodeImageFrame(std::span<const uint8_t> encoded, uint32_t frame_index,
                                const ImageDecodeOptions &opts, std::vector<uint8_t> &out_pixels,
                                uint32_t &out_stride, DecodedImageHeader &out_header);
};

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int AImageDecoder_createFromAAsset(portage::package::AAsset *asset,
                                                             AImageDecoder **outDecoder);

__attribute__((sysv_abi)) int AImageDecoder_createFromBuffer(const void *buffer, size_t length,
                                                             AImageDecoder **outDecoder);

__attribute__((sysv_abi)) int AImageDecoder_createFromFd(int fd, AImageDecoder **outDecoder);

__attribute__((sysv_abi)) void AImageDecoder_delete(AImageDecoder *decoder);

__attribute__((sysv_abi)) const AImageDecoderHeaderInfo *
AImageDecoder_getHeaderInfo(const AImageDecoder *decoder);

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getWidth(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getHeight(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) const char *
AImageDecoderHeaderInfo_getMimeType(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getDataSpace(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getAndroidBitmapFormat(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) int
AImageDecoderHeaderInfo_getAlphaFlags(const AImageDecoderHeaderInfo *info);

__attribute__((sysv_abi)) int AImageDecoder_setAndroidBitmapFormat(AImageDecoder *decoder,
                                                                   int32_t format);

__attribute__((sysv_abi)) int AImageDecoder_setUnpremultipliedRequired(AImageDecoder *decoder,
                                                                       bool required);

__attribute__((sysv_abi)) int AImageDecoder_setDataSpace(AImageDecoder *decoder, int32_t dataspace);

__attribute__((sysv_abi)) int AImageDecoder_setTargetSize(AImageDecoder *decoder, int32_t width,
                                                          int32_t height);

__attribute__((sysv_abi)) int AImageDecoder_setCrop(AImageDecoder *decoder, ARect crop);

__attribute__((sysv_abi)) int AImageDecoder_computeSampledSize(const AImageDecoder *decoder,
                                                               int sampleSize, int32_t *width,
                                                               int32_t *height);

__attribute__((sysv_abi)) size_t AImageDecoder_getMinimumStride(const AImageDecoder *decoder);

__attribute__((sysv_abi)) int AImageDecoder_decodeImage(AImageDecoder *decoder, void *pixels,
                                                        size_t stride, size_t size);

__attribute__((sysv_abi)) const char *AImageDecoder_resultToString(int result);

__attribute__((sysv_abi)) bool AImageDecoder_isAnimated(const AImageDecoder *decoder);

__attribute__((sysv_abi)) int AImageDecoder_setRepeatCount(AImageDecoder *decoder,
                                                           int32_t repeatCount);

__attribute__((sysv_abi)) int32_t AImageDecoder_getRepeatCount(const AImageDecoder *decoder);

__attribute__((sysv_abi)) AImageDecoderFrameInfo *AImageDecoderFrameInfo_create(void);

__attribute__((sysv_abi)) void AImageDecoderFrameInfo_delete(AImageDecoderFrameInfo *info);

__attribute__((sysv_abi)) int AImageDecoder_getFrameInfo(const AImageDecoder *decoder,
                                                         AImageDecoderFrameInfo *info);

__attribute__((sysv_abi)) int64_t
AImageDecoderFrameInfo_getDuration(const AImageDecoderFrameInfo *info);

__attribute__((sysv_abi)) ARect
AImageDecoderFrameInfo_getFrameRect(const AImageDecoderFrameInfo *info);

__attribute__((sysv_abi)) int AImageDecoder_advanceFrame(AImageDecoder *decoder);

__attribute__((sysv_abi)) int AImageDecoder_rewind(AImageDecoder *decoder);

} // extern "C"
