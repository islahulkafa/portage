#include "media/media_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <span>

namespace {

std::unique_ptr<AMediaFormat> CloneFormat(const AMediaFormat *src)
{
    auto copy = std::make_unique<AMediaFormat>();
    if (src) {
        AMediaFormat_copy(copy.get(), const_cast<AMediaFormat *>(src));
    }
    return copy;
}

bool LooksLikeAnnexB(std::span<const uint8_t> bytes) noexcept
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

bool LooksLikeAdts(std::span<const uint8_t> bytes) noexcept
{
    return bytes.size() >= 7 && bytes[0] == 0xFF && (bytes[1] & 0xF6) == 0xF0;
}

bool IsCodedInputPacket(const AMediaCodec *codec, std::span<const uint8_t> data,
                        uint32_t flags) noexcept
{
    if ((flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
        return true;
    }
    void *csd_ptr = nullptr;
    size_t csd_len = 0;
    if (codec->input_format &&
        AMediaFormat_getBuffer(codec->input_format.get(), AMEDIAFORMAT_KEY_CSD_0, &csd_ptr,
                               &csd_len) &&
        csd_ptr && csd_len > 0) {
        return true;
    }
    if (codec->mime.rfind("video/", 0) == 0 && LooksLikeAnnexB(data)) {
        return true;
    }
    if (codec->mime == "audio/mp4a-latm" && LooksLikeAdts(data)) {
        return true;
    }
    return false;
}

void SyncOutputFormatFromMftLocked(AMediaCodec *codec)
{
    if (!codec || !codec->mft) {
        return;
    }

    if (!codec->output_format) {
        codec->output_format = std::make_unique<AMediaFormat>();
    }

    if (codec->mft->IsVideo()) {
        codec->width = codec->mft->Width();
        codec->height = codec->mft->Height();
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_WIDTH, codec->width);
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_HEIGHT, codec->height);

        if (codec->is_encoder) {
            AMediaFormat_setString(codec->output_format.get(), AMEDIAFORMAT_KEY_MIME,
                                   codec->mime.c_str());
            codec->stride = codec->width * 4;
            AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_STRIDE,
                                  codec->stride);
        } else {
            AMediaFormat_setString(codec->output_format.get(), AMEDIAFORMAT_KEY_MIME, "video/raw");
            codec->stride = codec->mft->Stride();
            int32_t cf = (codec->mft->OutputColorFormat() == portage::media::MftColorFormat::I420)
                             ? COLOR_FormatYUV420Flexible
                             : COLOR_FormatYUV420SemiPlanar;
            AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, cf);
            AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_STRIDE,
                                  codec->stride);
        }
    } else {
        codec->sample_rate = codec->mft->SampleRate();
        codec->channel_count = codec->mft->ChannelCount();
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE,
                              codec->sample_rate);
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT,
                              codec->channel_count);
        AMediaFormat_setString(codec->output_format.get(), AMEDIAFORMAT_KEY_MIME,
                               codec->is_encoder ? codec->mime.c_str() : "audio/raw");
    }

    const auto &csd = codec->mft->NegotiatedCsd0();
    if (!csd.empty()) {
        AMediaFormat_setBuffer(codec->output_format.get(), AMEDIAFORMAT_KEY_CSD_0, csd.data(),
                               csd.size());
        if (csd != codec->last_synced_csd0) {
            codec->last_synced_csd0 = csd;
            codec->format_changed_pending = true;
        }
    }
}

size_t AllocateOutputSlotLocked(AMediaCodec *codec)
{
    for (size_t i = 0; i < codec->output_buffers.size(); ++i) {
        if (!codec->output_buffers[i].in_use) {
            codec->output_buffers[i].in_use = true;
            return i;
        }
    }
    codec->output_buffers.emplace_back();
    codec->output_buffers.back().in_use = true;
    return codec->output_buffers.size() - 1;
}

void PushMftPacketsLocked(AMediaCodec *codec, std::vector<portage::media::MftPacket> &packets,
                          bool expect_eos)
{
    bool pushed_eos = false;
    for (auto &pkt : packets) {
        if (pkt.format_changed) {
            SyncOutputFormatFromMftLocked(codec);
            codec->format_changed_pending = true;
            if (pkt.data.empty()) {
                continue;
            }
        }
        SyncOutputFormatFromMftLocked(codec);

        size_t out_idx = AllocateOutputSlotLocked(codec);
        auto &out_buf = codec->output_buffers[out_idx];
        out_buf.data = std::move(pkt.data);
        out_buf.info.offset = 0;
        out_buf.info.size = static_cast<int32_t>(out_buf.data.size());
        out_buf.info.presentationTimeUs = pkt.pts_us;
        out_buf.info.flags = pkt.flags;
        out_buf.color_format =
            codec->mft ? codec->mft->OutputColorFormat() : portage::media::MftColorFormat::Unknown;
        out_buf.format_snapshot = CloneFormat(codec->output_format.get());
        if ((out_buf.info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
            pushed_eos = true;
        }
        codec->ready_output_indices.push_back(out_idx);
    }

    if (expect_eos && !pushed_eos) {
        if (!codec->ready_output_indices.empty()) {
            size_t tail_idx = codec->ready_output_indices.back();
            codec->output_buffers[tail_idx].info.flags |= AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
        } else {
            size_t eos_idx = AllocateOutputSlotLocked(codec);
            auto &eos_buf = codec->output_buffers[eos_idx];
            eos_buf.data.clear();
            eos_buf.info = AMediaCodecBufferInfo{0, 0, codec->current_pts_us,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM};
            eos_buf.color_format = portage::media::MftColorFormat::Unknown;
            eos_buf.format_snapshot = CloneFormat(codec->output_format.get());
            codec->ready_output_indices.push_back(eos_idx);
        }
    }
    codec->cv.notify_all();
}

void DetachInputSurfaceLocked(AMediaCodec *codec)
{
    if (codec->input_surface) {
        codec->input_surface->on_post = nullptr;
        ANativeWindow_release(codec->input_surface);
        codec->input_surface = nullptr;
    }
}

void AttachInputSurfaceHookLocked(AMediaCodec *codec)
{
    if (!codec || !codec->input_surface) {
        return;
    }
    codec->input_surface->width = codec->width;
    codec->input_surface->height = codec->height;
    codec->input_surface->format = 1;
    codec->input_surface->on_post = [codec](ANativeWindow *win) {
        if (!codec || !win) {
            return;
        }
        std::vector<uint8_t> nv12;
        int32_t w = (win->locked_width > 0) ? win->locked_width : win->width;
        int32_t h = (win->locked_height > 0) ? win->locked_height : win->height;
        int32_t src_stride = (win->locked_stride > 0) ? (win->locked_stride * 4) : (w * 4);
        if (win->frontbuffer.empty()) {
            return;
        }
        portage::media::ConvertRgbaToNv12(win->frontbuffer.data(), src_stride, w, h, nv12);

        std::lock_guard<std::mutex> lock(codec->mutex);
        if (codec->state != AMediaCodec::State::Executing || codec->eos_signaled) {
            return;
        }
        float fps = (codec->frame_rate > 0.0f) ? codec->frame_rate : 30.0f;
        int64_t pts_us = static_cast<int64_t>(
            (static_cast<double>(codec->surface_frame_count++) * 1000000.0) / fps);
        codec->current_pts_us = pts_us;

        if (codec->mode == AMediaCodec::Mode::WmfActive && codec->mft && codec->mft->IsOpen()) {
            if (codec->mft->ProcessInput(nv12, pts_us, 0)) {
                std::vector<portage::media::MftPacket> packets;
                codec->mft->ProcessOutput(packets);
                PushMftPacketsLocked(codec, packets, false);
            }
        } else {
            size_t out_idx = AllocateOutputSlotLocked(codec);
            auto &out_buf = codec->output_buffers[out_idx];
            out_buf.data = std::move(nv12);
            out_buf.info =
                AMediaCodecBufferInfo{0, static_cast<int32_t>(out_buf.data.size()), pts_us, 0};
            out_buf.format_snapshot = CloneFormat(codec->output_format.get());
            codec->ready_output_indices.push_back(out_idx);
            codec->cv.notify_all();
        }
    };
}

void StopAsyncWorkerLocked(AMediaCodec *codec, std::unique_lock<std::mutex> &lock)
{
    codec->async_stop_requested = true;
    codec->cv.notify_all();
    if (codec->async_thread.joinable()) {
        if (std::this_thread::get_id() == codec->async_thread.get_id()) {
            codec->async_thread.detach();
        } else {
            std::thread t = std::move(codec->async_thread);
            lock.unlock();
            t.join();
            lock.lock();
        }
    }
}

void AsyncWorkerLoop(AMediaCodec *codec)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::unique_lock<std::mutex> lock(codec->mutex);
    while (!codec->async_stop_requested && codec->state == AMediaCodec::State::Executing) {
        auto find_free_input = [codec]() -> int32_t {
            if (codec->eos_signaled || codec->input_surface ||
                !codec->async_callbacks.onAsyncInputAvailable) {
                return -1;
            }
            for (size_t i = 0; i < codec->input_buffers.size(); ++i) {
                size_t idx = (codec->next_input_idx + i) % codec->input_buffers.size();
                if (!codec->input_buffers[idx].in_use) {
                    return static_cast<int32_t>(idx);
                }
            }
            return -1;
        };

        codec->cv.wait(lock, [codec, &find_free_input]() {
            if (codec->async_stop_requested || codec->state != AMediaCodec::State::Executing) {
                return true;
            }
            if (codec->format_changed_pending || !codec->ready_output_indices.empty()) {
                return true;
            }
            return find_free_input() >= 0;
        });

        if (codec->async_stop_requested || codec->state != AMediaCodec::State::Executing) {
            break;
        }

        AMediaFormat *fmt_to_fire = nullptr;
        if (codec->format_changed_pending) {
            codec->format_changed_pending = false;
            if (codec->async_callbacks.onAsyncFormatChanged && codec->output_format) {
                codec->async_format_keepalive.push_back(CloneFormat(codec->output_format.get()));
                fmt_to_fire = codec->async_format_keepalive.back().get();
            }
        }

        struct ReadyOut {
            int32_t idx;
            AMediaCodecBufferInfo info;
        };
        std::vector<ReadyOut> outs;
        while (!codec->ready_output_indices.empty()) {
            size_t out_idx = codec->ready_output_indices.front();
            codec->ready_output_indices.pop_front();
            outs.push_back(
                ReadyOut{static_cast<int32_t>(out_idx), codec->output_buffers[out_idx].info});
        }

        int32_t in_idx = find_free_input();
        if (in_idx >= 0) {
            codec->input_buffers[static_cast<size_t>(in_idx)].in_use = true;
            codec->next_input_idx = (static_cast<size_t>(in_idx) + 1) % codec->input_buffers.size();
        }

        AMediaCodecOnAsyncNotifyCallback cb = codec->async_callbacks;
        void *ud = codec->async_userdata;

        lock.unlock();

        if (fmt_to_fire && cb.onAsyncFormatChanged) {
            cb.onAsyncFormatChanged(codec, ud, fmt_to_fire);
        }
        if (cb.onAsyncOutputAvailable) {
            for (auto &out_ev : outs) {
                AMediaCodecBufferInfo info_copy = out_ev.info;
                cb.onAsyncOutputAvailable(codec, ud, out_ev.idx, &info_copy);
            }
        }
        if (in_idx >= 0 && cb.onAsyncInputAvailable) {
            cb.onAsyncInputAvailable(codec, ud, in_idx);
        }

        lock.lock();
    }
}

} // namespace

AMediaCodec::~AMediaCodec()
{
    std::unique_lock<std::mutex> lock(mutex);
    StopAsyncWorkerLocked(this, lock);
    DetachInputSurfaceLocked(this);
    if (mft) {
        mft->Close();
        mft.reset();
    }
}

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

__attribute__((sysv_abi)) media_status_t AMediaCodec_configure(AMediaCodec *codec,
                                                               const AMediaFormat *format,
                                                               ANativeWindow *surface,
                                                               AMediaCrypto *crypto, uint32_t flags)
{
    (void)crypto;
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(codec->mutex);
    codec->configure_flags = flags;
    codec->is_encoder = (flags & AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != 0 || codec->is_encoder;
    codec->surface = surface;
    codec->format_changed_pending = false;
    codec->eos_signaled = false;
    codec->surface_frame_count = 0;
    codec->last_synced_csd0.clear();

    std::vector<uint8_t> csd0;
    std::vector<uint8_t> csd1;

    if (format) {
        codec->input_format = CloneFormat(format);

        const char *fmt_mime = nullptr;
        if (AMediaFormat_getString(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_MIME,
                                   &fmt_mime) &&
            fmt_mime) {
            codec->mime = fmt_mime;
        }

        int32_t w = 0, h = 0, sr = 0, ch = 0, br = 0, ifi = 0;
        float fps = 0.0f;
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
        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_BIT_RATE,
                                  &br) &&
            br > 0) {
            codec->bitrate = br;
        }
        if (AMediaFormat_getInt32(const_cast<AMediaFormat *>(format),
                                  AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, &ifi) &&
            ifi >= 0) {
            codec->i_frame_interval = ifi;
        }
        if (AMediaFormat_getFloat(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_FRAME_RATE,
                                  &fps) &&
            fps > 0.0f) {
            codec->frame_rate = fps;
        }

        void *buf_ptr = nullptr;
        size_t buf_len = 0;
        if (AMediaFormat_getBuffer(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_CSD_0,
                                   &buf_ptr, &buf_len) &&
            buf_ptr && buf_len > 0) {
            csd0.assign(static_cast<const uint8_t *>(buf_ptr),
                        static_cast<const uint8_t *>(buf_ptr) + buf_len);
        }
        if (AMediaFormat_getBuffer(const_cast<AMediaFormat *>(format), AMEDIAFORMAT_KEY_CSD_1,
                                   &buf_ptr, &buf_len) &&
            buf_ptr && buf_len > 0) {
            csd1.assign(static_cast<const uint8_t *>(buf_ptr),
                        static_cast<const uint8_t *>(buf_ptr) + buf_len);
        }

        codec->output_format = CloneFormat(format);
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, 1);
        AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_STRIDE, codec->stride);
    }

    codec->mft = std::make_unique<portage::media::WmfTransformWrapper>();
    portage::media::MftConfig mft_cfg{};
    mft_cfg.mime = codec->mime;
    mft_cfg.is_encoder = codec->is_encoder;
    mft_cfg.width = codec->width;
    mft_cfg.height = codec->height;
    mft_cfg.frame_rate = codec->frame_rate;
    mft_cfg.bitrate = codec->bitrate;
    mft_cfg.i_frame_interval = codec->i_frame_interval;
    mft_cfg.sample_rate = codec->sample_rate;
    mft_cfg.channel_count = codec->channel_count;
    mft_cfg.csd0 = std::move(csd0);
    mft_cfg.csd1 = std::move(csd1);

    if (codec->mft->Open(mft_cfg)) {
        codec->mode = codec->is_encoder ? AMediaCodec::Mode::WmfActive : AMediaCodec::Mode::Auto;
        if (codec->is_encoder || !mft_cfg.csd0.empty()) {
            SyncOutputFormatFromMftLocked(codec);
        }
    } else {
        codec->mft.reset();
        codec->mode = AMediaCodec::Mode::PassthroughFallback;
    }

    size_t in_buf_size = 65536;
    size_t out_buf_size = 65536;
    if (codec->mime.rfind("video/", 0) == 0) {
        out_buf_size = static_cast<size_t>(std::max(1, codec->width * codec->height * 4));
        in_buf_size = std::max<size_t>(out_buf_size, 262144u);
    } else {
        out_buf_size = 16384;
        in_buf_size = 16384;
    }

    codec->input_buffers.resize(8);
    for (auto &b : codec->input_buffers) {
        b.data.resize(in_buf_size, 0);
        b.in_use = false;
    }

    codec->output_buffers.clear();
    codec->output_buffers.resize(8);
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
    std::unique_lock<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Configured) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    StopAsyncWorkerLocked(codec, lock);
    codec->async_stop_requested = false;
    codec->async_format_keepalive.clear();
    if (codec->input_surface) {
        AttachInputSurfaceHookLocked(codec);
    }
    codec->state = AMediaCodec::State::Executing;
    if (codec->async_mode) {
        codec->async_thread = std::thread(AsyncWorkerLoop, codec);
    }
    codec->cv.notify_all();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_stop(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::unique_lock<std::mutex> lock(codec->mutex);
    codec->state = AMediaCodec::State::Stopped;
    StopAsyncWorkerLocked(codec, lock);
    DetachInputSurfaceLocked(codec);
    if (codec->mft) {
        codec->mft->Flush();
    }
    codec->ready_output_indices.clear();
    codec->cv.notify_all();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_flush(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::unique_lock<std::mutex> lock(codec->mutex);
    if (codec->async_mode) {
        StopAsyncWorkerLocked(codec, lock);
        codec->async_stop_requested = false;
    }
    if (codec->mft) {
        codec->mft->Flush();
    }
    codec->eos_signaled = false;
    codec->ready_output_indices.clear();
    for (auto &b : codec->input_buffers) {
        b.in_use = false;
    }
    for (auto &b : codec->output_buffers) {
        b.in_use = false;
    }
    codec->cv.notify_all();
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
    if (codec->state != AMediaCodec::State::Executing || codec->async_mode) {
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
    codec->current_pts_us = static_cast<int64_t>(timeUs);
    if ((flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
        codec->eos_signaled = true;
    }

    auto &in_buf = codec->input_buffers[idx];
    std::span<const uint8_t> payload{};
    if (size > 0 && offset >= 0 && static_cast<size_t>(offset) + size <= in_buf.data.size()) {
        payload = std::span<const uint8_t>(in_buf.data.data() + offset, size);
    }

    if (codec->mode == AMediaCodec::Mode::Auto) {
        if (codec->mft && codec->mft->IsOpen() &&
            (!payload.empty() ? IsCodedInputPacket(codec, payload, flags)
                              : (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0)) {
            codec->mode = AMediaCodec::Mode::WmfActive;
            SyncOutputFormatFromMftLocked(codec);
        } else if (!payload.empty()) {
            codec->mode = AMediaCodec::Mode::PassthroughFallback;
            if (codec->mft) {
                codec->mft->Close();
                codec->mft.reset();
            }
            if (codec->output_format) {
                AMediaFormat_setInt32(codec->output_format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, 1);
            }
        }
    }

    if (codec->mode == AMediaCodec::Mode::WmfActive && codec->mft && codec->mft->IsOpen()) {
        bool try_again = false;
        if (!codec->mft->ProcessInput(payload, static_cast<int64_t>(timeUs), flags, &try_again)) {
            if (!try_again) {
                return AMEDIA_ERROR_UNKNOWN;
            }
        }
        std::vector<portage::media::MftPacket> packets;
        codec->mft->ProcessOutput(packets);
        bool expect_eos = (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        PushMftPacketsLocked(codec, packets, expect_eos);
        return AMEDIA_OK;
    }

    size_t out_idx = AllocateOutputSlotLocked(codec);
    auto &out_buf = codec->output_buffers[out_idx];

    if (!payload.empty()) {
        if (out_buf.data.size() < payload.size()) {
            out_buf.data.resize(payload.size());
        }
        std::memcpy(out_buf.data.data(), payload.data(), payload.size());
        out_buf.info.offset = 0;
        out_buf.info.size = static_cast<int32_t>(payload.size());
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

    out_buf.color_format = portage::media::MftColorFormat::Rgba8888;
    out_buf.format_snapshot = CloneFormat(codec->output_format.get());
    codec->ready_output_indices.push_back(out_idx);
    codec->cv.notify_all();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) int64_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec,
                                                                  AMediaCodecBufferInfo *info,
                                                                  int64_t timeoutUs)
{
    if (!codec || !info) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::unique_lock<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Executing || codec->async_mode) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }

    if (codec->format_changed_pending) {
        codec->format_changed_pending = false;
        return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    }

    if (codec->ready_output_indices.empty() && timeoutUs != 0) {
        auto wait_fn = [codec]() {
            return !codec->ready_output_indices.empty() || codec->format_changed_pending ||
                   codec->state != AMediaCodec::State::Executing;
        };
        if (timeoutUs < 0) {
            codec->cv.wait(lock, wait_fn);
        } else {
            codec->cv.wait_for(lock, std::chrono::microseconds(timeoutUs), wait_fn);
        }
    }

    if (codec->state != AMediaCodec::State::Executing) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    if (codec->format_changed_pending) {
        codec->format_changed_pending = false;
        return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
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
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (!codec->output_format) {
        return nullptr;
    }
    return CloneFormat(codec->output_format.get()).release();
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

    auto &buf = codec->output_buffers[idx];
    if (render && codec->surface && !buf.data.empty()) {
        std::vector<uint8_t> rgba_converted;
        const uint8_t *src_rgba = buf.data.data();
        int32_t src_byte_stride = codec->stride;

        if (buf.color_format == portage::media::MftColorFormat::Nv12) {
            int32_t y_stride =
                (codec->mft && codec->mft->Stride() > 0) ? codec->mft->Stride() : codec->width;
            portage::media::ConvertNv12ToRgba(buf.data.data(), y_stride, codec->width,
                                              codec->height, rgba_converted);
            src_rgba = rgba_converted.data();
            src_byte_stride = codec->width * 4;
        } else if (buf.color_format == portage::media::MftColorFormat::I420) {
            int32_t y_stride =
                (codec->mft && codec->mft->Stride() > 0) ? codec->mft->Stride() : codec->width;
            portage::media::ConvertI420ToRgba(buf.data.data(), y_stride, codec->width,
                                              codec->height, rgba_converted);
            src_rgba = rgba_converted.data();
            src_byte_stride = codec->width * 4;
        }

        ANativeWindow_Buffer win_buf{};
        if (ANativeWindow_lock(codec->surface, &win_buf, nullptr) == 0) {
            if (win_buf.bits && win_buf.width > 0 && win_buf.height > 0) {
                uint8_t *dst = static_cast<uint8_t *>(win_buf.bits);
                int32_t bpp = (win_buf.format == 4) ? 2 : 4;
                int32_t copy_w = std::min<int32_t>(win_buf.width, codec->width);
                int32_t copy_h = std::min<int32_t>(win_buf.height, codec->height);
                for (int32_t y = 0; y < copy_h; ++y) {
                    std::memcpy(dst + y * win_buf.stride * bpp, src_rgba + y * src_byte_stride,
                                static_cast<size_t>(copy_w * bpp));
                }
            }
            ANativeWindow_unlockAndPost(codec->surface);
        }
    }

    buf.in_use = false;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t
AMediaCodec_releaseOutputBufferAtTime(AMediaCodec *codec, size_t idx, int64_t renderTimestampNs)
{
    (void)renderTimestampNs;
    return AMediaCodec_releaseOutputBuffer(codec, idx, true);
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_getName(AMediaCodec *codec, char **out_name)
{
    if (!codec || !out_name) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    const std::string &src = codec->name.empty() ? codec->mime : codec->name;
    char *copy = static_cast<char *>(std::malloc(src.size() + 1));
    if (!copy) {
        return AMEDIA_ERROR_UNKNOWN;
    }
    std::memcpy(copy, src.c_str(), src.size() + 1);
    *out_name = copy;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) void AMediaCodec_releaseName(AMediaCodec *codec, char *name)
{
    (void)codec;
    std::free(name);
}

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getInputFormat(AMediaCodec *codec)
{
    if (!codec) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (!codec->input_format) {
        return nullptr;
    }
    return CloneFormat(codec->input_format.get()).release();
}

__attribute__((sysv_abi)) AMediaFormat *AMediaCodec_getBufferFormat(AMediaCodec *codec,
                                                                    size_t index)
{
    if (!codec) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (index >= codec->output_buffers.size()) {
        return nullptr;
    }
    if (codec->output_buffers[index].format_snapshot) {
        return CloneFormat(codec->output_buffers[index].format_snapshot.get()).release();
    }
    return codec->output_format ? CloneFormat(codec->output_format.get()).release() : nullptr;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_setParameters(AMediaCodec *codec,
                                                                   const AMediaFormat *params)
{
    if (!codec || !params) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Executing) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    int32_t new_bitrate = 0;
    if (!AMediaFormat_getInt32(const_cast<AMediaFormat *>(params), AMEDIAFORMAT_KEY_VIDEO_BITRATE,
                               &new_bitrate)) {
        AMediaFormat_getInt32(const_cast<AMediaFormat *>(params), AMEDIAFORMAT_KEY_BIT_RATE,
                              &new_bitrate);
    }
    if (new_bitrate > 0) {
        codec->bitrate = new_bitrate;
    }
    int32_t sync_req = 0;
    AMediaFormat_getInt32(const_cast<AMediaFormat *>(params), AMEDIAFORMAT_KEY_REQUEST_SYNC_FRAME,
                          &sync_req);
    if (codec->mft && codec->mft->IsOpen()) {
        codec->mft->SetParameters(new_bitrate, sync_req != 0);
    }
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_signalEndOfInputStream(AMediaCodec *codec)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Executing || !codec->is_encoder) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    codec->eos_signaled = true;
    std::vector<portage::media::MftPacket> packets;
    if (codec->mode == AMediaCodec::Mode::WmfActive && codec->mft && codec->mft->IsOpen()) {
        codec->mft->Drain();
        codec->mft->ProcessOutput(packets);
    }
    PushMftPacketsLocked(codec, packets, true);
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_createInputSurface(AMediaCodec *codec,
                                                                        ANativeWindow **surface)
{
    if (!codec || !surface) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (!codec->is_encoder || codec->state != AMediaCodec::State::Configured) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    auto *win = new ANativeWindow();
    win->width = codec->width;
    win->height = codec->height;
    win->format = 1;
    win->ref_count = 1;
    win->owned_heap = true;

    DetachInputSurfaceLocked(codec);
    codec->input_surface = win;
    ANativeWindow_acquire(win);
    *surface = win;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t
AMediaCodec_createPersistentInputSurface(ANativeWindow **surface)
{
    if (!surface) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    auto *win = new ANativeWindow();
    win->width = 1280;
    win->height = 720;
    win->format = 1;
    win->ref_count = 1;
    win->owned_heap = true;
    *surface = win;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_setInputSurface(AMediaCodec *codec,
                                                                     ANativeWindow *surface)
{
    if (!codec || !surface) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (!codec->is_encoder || codec->state != AMediaCodec::State::Configured) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    DetachInputSurfaceLocked(codec);
    codec->input_surface = surface;
    ANativeWindow_acquire(surface);
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AMediaCodec_setAsyncNotifyCallback(
    AMediaCodec *codec, AMediaCodecOnAsyncNotifyCallback callback, void *userdata)
{
    if (!codec) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    std::lock_guard<std::mutex> lock(codec->mutex);
    if (codec->state != AMediaCodec::State::Uninitialized &&
        codec->state != AMediaCodec::State::Configured) {
        return AMEDIA_ERROR_INVALID_OPERATION;
    }
    bool any = callback.onAsyncInputAvailable || callback.onAsyncOutputAvailable ||
               callback.onAsyncFormatChanged || callback.onAsyncError;
    codec->async_mode = any;
    codec->async_callbacks = callback;
    codec->async_userdata = userdata;
    return AMEDIA_OK;
}

} // extern "C"
