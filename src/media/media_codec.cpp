#include "media/media_codec.hpp"

#include <algorithm>
#include <cstring>

extern "C" {

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createCodecByName(const char *name)
{
    if (!name) {
        return nullptr;
    }
    auto *codec = new AMediaCodec();
    codec->name = name;
    if (codec->name.find("avc") != std::string::npos ||
        codec->name.find("video") != std::string::npos) {
        codec->mime = "video/avc";
    } else if (codec->name.find("aac") != std::string::npos ||
               codec->name.find("audio") != std::string::npos) {
        codec->mime = "audio/mp4a-latm";
    } else {
        codec->mime = "video/avc";
    }
    return codec;
}

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type)
{
    if (!mime_type) {
        return nullptr;
    }
    auto *codec = new AMediaCodec();
    codec->mime = mime_type;
    codec->name = std::string("decoder.") + mime_type;
    return codec;
}

__attribute__((sysv_abi)) AMediaCodec *AMediaCodec_createEncoderByType(const char *mime_type)
{
    if (!mime_type) {
        return nullptr;
    }
    auto *codec = new AMediaCodec();
    codec->mime = mime_type;
    codec->name = std::string("encoder.") + mime_type;
    codec->is_encoder = true;
    return codec;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_delete(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    delete codec;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t
AMediaCodec_configure(AMediaCodec *codec, const AMediaFormat *format, ANativeWindow *surface,
                      AMediaCrypto *crypto, uint32_t flags)
{
    (void)crypto;
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    codec->configure_flags = flags;
    codec->surface = surface;

    if (format) {
        codec->input_format = std::make_unique<AMediaFormat>();
        AMediaFormat_copy(codec->input_format.get(), const_cast<AMediaFormat *>(format));

        int32_t w = 0, h = 0, sr = 0, ch = 0;
        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_WIDTH, &w) &&
            w > 0) {
            codec->width = w;
        }
        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_HEIGHT,
                                 &h) &&
            h > 0) {
            codec->height = h;
        }
        codec->stride = codec->width * 4;

        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_SAMPLE_RATE,
                                 &sr) &&
            sr > 0) {
            codec->sample_rate = sr;
        }
        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format),
                                 AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch) &&
            ch > 0) {
            codec->channel_count = ch;
        }

        codec->output_format = std::make_unique<AMediaFormat>();
        AMediaFormat_copy(codec->output_format.get(), const_cast<AMediaFormat *>(format));
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, 1);
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_STRIDE, codec->stride);
    }

    size_t in_buf_size = 65536;
    size_t out_buf_size = 65536;
    if (codec->mime.rfind("video/", 0) == 0) {
        out_buf_size = static_cast<size_t>(codec->width * codec->height * 4);
        in_buf_size = out_buf_size;
    } else {
        out_buf_size = 4096;
        in_buf_size = 4096;
    }

    codec->input_buffers.resize(4);
    for (auto &b : codec->input_buffers) {
        b.data.resize(in_buf_size, 0);
        b.in_use = false;
    }

    codec->output_buffers.resize(4);
    for (auto &b : codec->output_buffers) {
        b.data.resize(out_buf_size, 0);
        b.in_use = false;
    }

    codec->ready_output_indices.clear();
    codec->state = AMediaCodec::State::Configured;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_start(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Configured) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    codec->state = AMediaCodec::State::Executing;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_stop(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    codec->state = AMediaCodec::State::Stopped;
    codec->ready_output_indices.clear();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_flush(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    codec->ready_output_indices.clear();
    for (auto &b : codec->output_buffers) {
        b.in_use = false;
    }
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) int64_t AMediaCodec_dequeueInputBuffer(AMediaCodec *codec,
                                                               int64_t timeoutUs)
{
    (void)timeoutUs;
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Executing) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    for (size_t i = 0; i < codec->input_buffers.size(); ++i) {
        size_t idx = (codec->next_input_idx + i) % codec->input_buffers.size();
        if (!codec->input_buffers[idx].in_use) {
            codec->input_buffers[idx].in_use = true;
            codec->next_input_idx = (idx + 1) % codec->input_buffers.size();
            return static_cast<int64_t>(idx);
        }
    }

    return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}

__attribute__((sysv_abi)) uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *codec, size_t idx,
                                                              size_t *out_size)
{
    if (!codec || !out_size) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (idx >= codec->input_buffers.size()) {
        return nullptr;
    }

    *out_size = codec->input_buffers[idx].data.size();
    return codec->input_buffers[idx].data.data();
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *codec,
                                                                     size_t idx, int64_t offset,
                                                                     size_t size, uint64_t timeUs,
                                                                     uint32_t flags)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (idx >= codec->input_buffers.size() || !codec->input_buffers[idx].in_use) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    codec->input_buffers[idx].in_use = false;

    // Find an available output buffer to hold decoded sample
    size_t out_idx = (idx % codec->output_buffers.size());
    auto &out_buf = codec->output_buffers[out_idx];
    out_buf.in_use = true;

    auto &in_buf = codec->input_buffers[idx];
    if (size > 0 && offset >= 0 &&
        static_cast<size_t>(offset + size) <= in_buf.data.size()) {
        if (out_buf.data.size() < static_cast<size_t>(size)) {
            out_buf.data.resize(static_cast<size_t>(size));
        }
        std::memcpy(out_buf.data.data(), in_buf.data.data() + offset, static_cast<size_t>(size));
        out_buf.info.offset = 0;
        out_buf.info.size = static_cast<int32_t>(size);
        out_buf.info.presentationTimeUs = static_cast<int64_t>(timeUs);
        out_buf.info.flags = flags;
    } else if (codec->mime.rfind("video/", 0) == 0) {
        size_t frame_bytes = static_cast<size_t>(codec->width * codec->height * 4);
        if (out_buf.data.size() < frame_bytes) {
            out_buf.data.resize(frame_bytes, 0);
        }

        uint32_t *pix = reinterpret_cast<uint32_t *>(out_buf.data.data());
        size_t count = frame_bytes / 4;
        uint32_t color = 0xFF112233;
        for (size_t i = 0; i < count; ++i) {
            pix[i] = color;
        }

        out_buf.info.offset = 0;
        out_buf.info.size = static_cast<int32_t>(frame_bytes);
        out_buf.info.presentationTimeUs = static_cast<int64_t>(timeUs);
        out_buf.info.flags = flags;
    } else {
        size_t pcm_bytes = (std::min<size_t>)(out_buf.data.size(), 2048);
        int16_t *pcm = reinterpret_cast<int16_t *>(out_buf.data.data());
        size_t count = pcm_bytes / 2;
        for (size_t i = 0; i < count; ++i) {
            pcm[i] = static_cast<int16_t>((i % 128) * 200);
        }

        out_buf.info.offset = 0;
        out_buf.info.size = static_cast<int32_t>(pcm_bytes);
        out_buf.info.presentationTimeUs = static_cast<int64_t>(timeUs);
        out_buf.info.flags = flags;
    }

    codec->ready_output_indices.push_back(out_idx);
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) int64_t
AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    (void)timeoutUs;
    if (!codec || !info) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Executing) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    if (codec->ready_output_indices.empty()) {
        return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    }

    size_t out_idx = codec->ready_output_indices.front();
    codec->ready_output_indices.pop_front();

    *info = codec->output_buffers[out_idx].info;
    return static_cast<int64_t>(out_idx);
}

__attribute__((sysv_abi)) uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx,
                                                               size_t *out_size)
{
    if (!codec || !out_size) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (idx >= codec->output_buffers.size()) {
        return nullptr;
    }

    *out_size = static_cast<size_t>(codec->output_buffers[idx].info.size);
    return codec->output_buffers[idx].data.data();
}

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *codec)
{
    if (!codec) {
        return nullptr;
    }
    return codec->output_format.get();
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *codec,
                                                                         size_t idx, bool render)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    if (idx >= codec->output_buffers.size()) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    if (render && codec->surface) {
        ANativeWindow_Buffer win_buf{};
        if (ANativeWindow_lock(codec->surface, &win_buf, nullptr) == 0) {
            if (win_buf.bits && win_buf.width > 0 && win_buf.height > 0) {
                const uint8_t *src = codec->output_buffers[idx].data.data();
                uint8_t *dst = static_cast<uint8_t *>(win_buf.bits);
                int32_t bpp = (win_buf.format == 4) ? 2 : 4;
                int32_t copy_w = std::min<int32_t>(win_buf.width, codec->width);
                int32_t copy_h = std::min<int32_t>(win_buf.height, codec->height);
                for (int32_t y = 0; y < copy_h; ++y) {
                    std::memcpy(dst + y * win_buf.stride * bpp, src + y * codec->stride,
                                copy_w * bpp);
                }
            }
            ANativeWindow_unlockAndPost(codec->surface);
        }
    }

    codec->output_buffers[idx].in_use = false;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t
AMediaCodec_releaseOutputBufferAtTime(AMediaCodec *codec, size_t idx, int64_t renderTimestampNs)
{
    (void)renderTimestampNs;
    return AMediaCodec_releaseOutputBuffer(codec, idx, true);
}

} // extern "C"
