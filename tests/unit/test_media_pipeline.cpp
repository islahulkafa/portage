#include "media/mf_byte_stream.hpp"
#include "media/media_codec.hpp"
#include "media/media_extractor.hpp"
#include "media/media_muxer.hpp"
#include "media/wmf_decoder.hpp"
#include "media/wmf_transform.hpp"

#include <gtest/gtest.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <vector>

namespace portage::media {

namespace {

std::filesystem::path MakeTempPath(const char *suffix)
{
    auto dir = std::filesystem::temp_directory_path();
    static std::atomic<uint64_t> counter{1};
    return dir / ("portage_media_test_" + std::to_string(counter++) + suffix);
}

} // namespace

TEST(MediaPipelineTest, ByteStreamClipsOffsetAndLength)
{
    auto path = MakeTempPath(".bin");
    {
        std::ofstream out(path, std::ios::binary);
        std::vector<uint8_t> payload(64);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<uint8_t>(i);
        }
        out.write(reinterpret_cast<const char *>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
    }

    int fd = _open(path.string().c_str(), _O_RDONLY | _O_BINARY);
    ASSERT_GE(fd, 0);

    auto *stream = PortageMfByteStream::CreateFromFd(fd, 16, 20, false);
    ASSERT_NE(stream, nullptr);

    QWORD len = 0;
    EXPECT_EQ(stream->GetLength(&len), S_OK);
    EXPECT_EQ(len, 20u);

    std::vector<uint8_t> buf(32, 0);
    ULONG bytes_read = 0;
    EXPECT_EQ(stream->Read(buf.data(), 12, &bytes_read), S_OK);
    EXPECT_EQ(bytes_read, 12u);
    for (size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(buf[i], static_cast<uint8_t>(16 + i));
    }

    EXPECT_EQ(stream->Read(buf.data(), 20, &bytes_read), S_OK);
    EXPECT_EQ(bytes_read, 8u);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(buf[i], static_cast<uint8_t>(28 + i));
    }

    BOOL eos = FALSE;
    EXPECT_EQ(stream->IsEndOfStream(&eos), S_OK);
    EXPECT_TRUE(eos);

    QWORD new_pos = 0;
    EXPECT_EQ(stream->Seek(msoBegin, 4, 0, &new_pos), S_OK);
    EXPECT_EQ(new_pos, 4u);
    EXPECT_EQ(stream->Read(buf.data(), 4, &bytes_read), S_OK);
    EXPECT_EQ(bytes_read, 4u);
    EXPECT_EQ(buf[0], 20u);

    stream->Release();
    _close(fd);
    std::filesystem::remove(path);
}

TEST(MediaPipelineTest, ConvertsRgbaNv12RoundtripWithinTolerance)
{
    constexpr int32_t width = 64;
    constexpr int32_t height = 64;
    std::vector<uint8_t> rgba_in(width * height * 4);
    for (size_t i = 0; i < static_cast<size_t>(width * height); ++i) {
        rgba_in[i * 4 + 0] = 40;
        rgba_in[i * 4 + 1] = 180;
        rgba_in[i * 4 + 2] = 90;
        rgba_in[i * 4 + 3] = 255;
    }

    std::vector<uint8_t> nv12;
    ConvertRgbaToNv12(rgba_in.data(), width * 4, width, height, nv12);
    ASSERT_EQ(nv12.size(), static_cast<size_t>(width * height * 3 / 2));

    std::vector<uint8_t> rgba_out;
    ConvertNv12ToRgba(nv12.data(), width, width, height, rgba_out);
    ASSERT_EQ(rgba_out.size(), static_cast<size_t>(width * height * 4));

    for (size_t i = 0; i < static_cast<size_t>(width * height); ++i) {
        EXPECT_NEAR(rgba_out[i * 4 + 0], 40, 6);
        EXPECT_NEAR(rgba_out[i * 4 + 1], 180, 6);
        EXPECT_NEAR(rgba_out[i * 4 + 2], 90, 6);
        EXPECT_EQ(rgba_out[i * 4 + 3], 255);
    }
}

TEST(MediaPipelineTest, ConvertsI420ToRgbaCorrectly)
{
    constexpr int32_t width = 16;
    constexpr int32_t height = 16;
    std::vector<uint8_t> i420(width * height * 3 / 2, 128);
    std::fill(i420.begin(), i420.begin() + width * height, 235);

    std::vector<uint8_t> rgba_out;
    ConvertI420ToRgba(i420.data(), width, width, height, rgba_out);
    ASSERT_EQ(rgba_out.size(), static_cast<size_t>(width * height * 4));
    EXPECT_GE(rgba_out[0], 245);
    EXPECT_GE(rgba_out[1], 245);
    EXPECT_GE(rgba_out[2], 245);
    EXPECT_EQ(rgba_out[3], 255);
}

TEST(MediaPipelineTest, ExtractorReadsRealContainerAtNonZeroOffset)
{
    ASSERT_TRUE(WmfManager::Instance().Initialize());

    auto mp4_path = MakeTempPath("_src.mp4");
    auto wrapped_path = MakeTempPath("_wrapped.apk");

    {
        IMFSinkWriter *writer = nullptr;
        std::wstring wpath = mp4_path.wstring();
        ASSERT_EQ(MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, nullptr, &writer), S_OK);

        IMFMediaType *out_type = nullptr;
        ASSERT_EQ(MFCreateMediaType(&out_type), S_OK);
        out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out_type->SetUINT32(MF_MT_AVG_BITRATE, 400000);
        out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out_type, MF_MT_FRAME_SIZE, 160, 128);
        MFSetAttributeRatio(out_type, MF_MT_FRAME_RATE, 30, 1);
        MFSetAttributeRatio(out_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

        DWORD stream_idx = 0;
        ASSERT_EQ(writer->AddStream(out_type, &stream_idx), S_OK);
        out_type->Release();

        IMFMediaType *in_type = nullptr;
        ASSERT_EQ(MFCreateMediaType(&in_type), S_OK);
        in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        in_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(in_type, MF_MT_FRAME_SIZE, 160, 128);
        MFSetAttributeRatio(in_type, MF_MT_FRAME_RATE, 30, 1);
        MFSetAttributeRatio(in_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        ASSERT_EQ(writer->SetInputMediaType(stream_idx, in_type, nullptr), S_OK);
        in_type->Release();

        ASSERT_EQ(writer->BeginWriting(), S_OK);

        for (int f = 0; f < 3; ++f) {
            IMFSample *sample = nullptr;
            IMFMediaBuffer *mb = nullptr;
            ASSERT_EQ(MFCreateSample(&sample), S_OK);
            ASSERT_EQ(MFCreateMemoryBuffer(160 * 128 * 4, &mb), S_OK);
            BYTE *ptr = nullptr;
            ASSERT_EQ(mb->Lock(&ptr, nullptr, nullptr), S_OK);
            std::fill_n(ptr, 160 * 128 * 4, static_cast<BYTE>(0x40 + f * 20));
            mb->Unlock();
            mb->SetCurrentLength(160 * 128 * 4);
            sample->AddBuffer(mb);
            sample->SetSampleTime(f * 333333LL);
            sample->SetSampleDuration(333333LL);
            ASSERT_EQ(writer->WriteSample(stream_idx, sample), S_OK);
            mb->Release();
            sample->Release();
        }
        ASSERT_EQ(writer->Finalize(), S_OK);
        writer->Release();
    }

    std::vector<uint8_t> mp4_bytes;
    {
        std::ifstream in(mp4_path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(in.is_open());
        auto sz = static_cast<size_t>(in.tellg());
        in.seekg(0);
        mp4_bytes.resize(sz);
        in.read(reinterpret_cast<char *>(mp4_bytes.data()), static_cast<std::streamsize>(sz));
    }

    constexpr size_t kPrefix = 128;
    {
        std::ofstream out(wrapped_path, std::ios::binary);
        std::vector<uint8_t> prefix(kPrefix, 0xDE);
        std::vector<uint8_t> trailer(128, 0xAD);
        out.write(reinterpret_cast<const char *>(prefix.data()), kPrefix);
        out.write(reinterpret_cast<const char *>(mp4_bytes.data()),
                  static_cast<std::streamsize>(mp4_bytes.size()));
        out.write(reinterpret_cast<const char *>(trailer.data()), 128);
    }

    int fd = _open(wrapped_path.string().c_str(), _O_RDONLY | _O_BINARY);
    ASSERT_GE(fd, 0);

    AMediaExtractor *ex = AMediaExtractor_new();
    ASSERT_NE(ex, nullptr);
    EXPECT_EQ(
        AMediaExtractor_setDataSourceFd(ex, fd, kPrefix, static_cast<int64_t>(mp4_bytes.size())),
        AMEDIA_OK);
    ASSERT_GE(AMediaExtractor_getTrackCount(ex), 1u);

    AMediaFormat *fmt = AMediaExtractor_getTrackFormat(ex, 0);
    ASSERT_NE(fmt, nullptr);
    int32_t w = 0;
    int32_t h = 0;
    EXPECT_TRUE(AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w));
    EXPECT_TRUE(AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h));
    EXPECT_EQ(w, 160);
    EXPECT_EQ(h, 128);

    AMediaFormat_delete(fmt);
    AMediaExtractor_delete(ex);
    _close(fd);
    std::filesystem::remove(mp4_path);
    std::filesystem::remove(wrapped_path);
}

TEST(MediaPipelineTest, EncodesAndDecodesH264VideoWithNv12AndSurfaceBlit)
{
    constexpr int32_t kWidth = 128;
    constexpr int32_t kHeight = 128;

    std::vector<uint8_t> solid_rgba(kWidth * kHeight * 4);
    for (size_t i = 0; i < static_cast<size_t>(kWidth * kHeight); ++i) {
        solid_rgba[i * 4 + 0] = 30;
        solid_rgba[i * 4 + 1] = 200;
        solid_rgba[i * 4 + 2] = 60;
        solid_rgba[i * 4 + 3] = 255;
    }
    std::vector<uint8_t> solid_nv12;
    ConvertRgbaToNv12(solid_rgba.data(), kWidth * 4, kWidth, kHeight, solid_nv12);

    AMediaCodec *encoder = AMediaCodec_createEncoderByType("video/avc");
    ASSERT_NE(encoder, nullptr);

    AMediaFormat *enc_fmt = AMediaFormat_new();
    AMediaFormat_setString(enc_fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_BIT_RATE, 1000000);
    AMediaFormat_setFloat(enc_fmt, AMEDIAFORMAT_KEY_FRAME_RATE, 30.0f);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatYUV420SemiPlanar);

    ASSERT_EQ(AMediaCodec_configure(encoder, enc_fmt, nullptr, nullptr,
                                    AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
              AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(encoder), AMEDIA_OK);

    std::vector<std::vector<uint8_t>> encoded_packets;
    std::vector<uint8_t> csd0;
    for (int f = 0; f < 5; ++f) {
        int64_t in_idx = AMediaCodec_dequeueInputBuffer(encoder, 50000);
        ASSERT_GE(in_idx, 0);
        size_t in_cap = 0;
        uint8_t *in_ptr = AMediaCodec_getInputBuffer(encoder, static_cast<size_t>(in_idx), &in_cap);
        ASSERT_NE(in_ptr, nullptr);
        ASSERT_GE(in_cap, solid_nv12.size());
        std::memcpy(in_ptr, solid_nv12.data(), solid_nv12.size());

        uint32_t flags = (f == 4) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        ASSERT_EQ(AMediaCodec_queueInputBuffer(encoder, static_cast<size_t>(in_idx), 0,
                                               solid_nv12.size(), static_cast<uint64_t>(f) * 33333u,
                                               flags),
                  AMEDIA_OK);

        for (int spin = 0; spin < 8; ++spin) {
            AMediaCodecBufferInfo info{};
            int64_t out_idx = AMediaCodec_dequeueOutputBuffer(encoder, &info, 10000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat *of = AMediaCodec_getOutputFormat(encoder);
                void *c_ptr = nullptr;
                size_t c_len = 0;
                if (AMediaFormat_getBuffer(of, AMEDIAFORMAT_KEY_CSD_0, &c_ptr, &c_len) && c_ptr) {
                    csd0.assign(static_cast<uint8_t *>(c_ptr),
                                static_cast<uint8_t *>(c_ptr) + c_len);
                }
                AMediaFormat_delete(of);
                continue;
            }
            if (out_idx < 0) {
                break;
            }
            size_t out_cap = 0;
            uint8_t *out_ptr =
                AMediaCodec_getOutputBuffer(encoder, static_cast<size_t>(out_idx), &out_cap);
            if (out_ptr && info.size > 0) {
                encoded_packets.emplace_back(out_ptr + info.offset,
                                             out_ptr + info.offset + info.size);
            }
            AMediaCodec_releaseOutputBuffer(encoder, static_cast<size_t>(out_idx), false);
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                break;
            }
        }
    }

    EXPECT_FALSE(encoded_packets.empty());

    AMediaCodec_stop(encoder);
    AMediaCodec_delete(encoder);
    AMediaFormat_delete(enc_fmt);

    ANativeWindow win{kWidth, kHeight, 1, 1, nullptr};
    AMediaCodec *decoder = AMediaCodec_createDecoderByType("video/avc");
    ASSERT_NE(decoder, nullptr);

    AMediaFormat *dec_fmt = AMediaFormat_new();
    AMediaFormat_setString(dec_fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(dec_fmt, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(dec_fmt, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    if (!csd0.empty()) {
        AMediaFormat_setBuffer(dec_fmt, AMEDIAFORMAT_KEY_CSD_0, csd0.data(), csd0.size());
    }

    ASSERT_EQ(AMediaCodec_configure(decoder, dec_fmt, &win, nullptr, 0), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(decoder), AMEDIA_OK);

    bool rendered_frame = false;
    for (size_t i = 0; i < encoded_packets.size(); ++i) {
        int64_t in_idx = AMediaCodec_dequeueInputBuffer(decoder, 50000);
        ASSERT_GE(in_idx, 0);
        size_t in_cap = 0;
        uint8_t *in_ptr = AMediaCodec_getInputBuffer(decoder, static_cast<size_t>(in_idx), &in_cap);
        ASSERT_NE(in_ptr, nullptr);
        ASSERT_GE(in_cap, encoded_packets[i].size());
        std::memcpy(in_ptr, encoded_packets[i].data(), encoded_packets[i].size());

        uint32_t flags =
            (i + 1 == encoded_packets.size()) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        ASSERT_EQ(AMediaCodec_queueInputBuffer(decoder, static_cast<size_t>(in_idx), 0,
                                               encoded_packets[i].size(), i * 33333u, flags),
                  AMEDIA_OK);

        for (int spin = 0; spin < 10; ++spin) {
            AMediaCodecBufferInfo info{};
            int64_t out_idx = AMediaCodec_dequeueOutputBuffer(decoder, &info, 10000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                continue;
            }
            if (out_idx < 0) {
                break;
            }
            if (info.size > 0) {
                AMediaFormat *buf_fmt =
                    AMediaCodec_getBufferFormat(decoder, static_cast<size_t>(out_idx));
                ASSERT_NE(buf_fmt, nullptr);
                int32_t cf = 0;
                EXPECT_TRUE(AMediaFormat_getInt32(buf_fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &cf));
                EXPECT_EQ(cf, COLOR_FormatYUV420SemiPlanar);
                AMediaFormat_delete(buf_fmt);

                ASSERT_EQ(
                    AMediaCodec_releaseOutputBuffer(decoder, static_cast<size_t>(out_idx), true),
                    AMEDIA_OK);
                rendered_frame = true;
            } else {
                AMediaCodec_releaseOutputBuffer(decoder, static_cast<size_t>(out_idx), false);
            }
        }
    }

    EXPECT_TRUE(rendered_frame);
    ASSERT_GE(win.frontbuffer.size(), static_cast<size_t>(kWidth * kHeight * 4));
    EXPECT_NEAR(win.frontbuffer[0], 30, 25);
    EXPECT_NEAR(win.frontbuffer[1], 200, 25);
    EXPECT_NEAR(win.frontbuffer[2], 60, 25);

    AMediaCodec_stop(decoder);
    AMediaCodec_delete(decoder);
    AMediaFormat_delete(dec_fmt);
}

TEST(MediaPipelineTest, EncodesFromInputSurfaceAndPersistentSurface)
{
    constexpr int32_t kWidth = 128;
    constexpr int32_t kHeight = 128;

    ANativeWindow *persistent = nullptr;
    ASSERT_EQ(AMediaCodec_createPersistentInputSurface(&persistent), AMEDIA_OK);
    ASSERT_NE(persistent, nullptr);

    AMediaCodec *encoder = AMediaCodec_createEncoderByType("video/avc");
    ASSERT_NE(encoder, nullptr);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, 800000);
    AMediaFormat_setFloat(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, 30.0f);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatSurface);

    ASSERT_EQ(
        AMediaCodec_configure(encoder, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
        AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_setInputSurface(encoder, persistent), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(encoder), AMEDIA_OK);

    for (int f = 0; f < 3; ++f) {
        ANativeWindow_Buffer buf{};
        ASSERT_EQ(ANativeWindow_lock(persistent, &buf, nullptr), 0);
        auto *pixels = static_cast<uint8_t *>(buf.bits);
        ASSERT_NE(pixels, nullptr);
        for (int32_t y = 0; y < buf.height; ++y) {
            uint8_t *row = pixels + static_cast<size_t>(y * buf.stride) * 4u;
            for (int32_t x = 0; x < buf.width; ++x) {
                row[x * 4 + 0] = 200;
                row[x * 4 + 1] = 40;
                row[x * 4 + 2] = 40;
                row[x * 4 + 3] = 255;
            }
        }
        ASSERT_EQ(ANativeWindow_unlockAndPost(persistent), 0);
    }

    ASSERT_EQ(AMediaCodec_signalEndOfInputStream(encoder), AMEDIA_OK);

    bool saw_data = false;
    bool saw_eos = false;
    for (int spin = 0; spin < 20; ++spin) {
        AMediaCodecBufferInfo info{};
        int64_t out_idx = AMediaCodec_dequeueOutputBuffer(encoder, &info, 20000);
        if (out_idx >= 0) {
            if (info.size > 0) {
                saw_data = true;
            }
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                saw_eos = true;
            }
            AMediaCodec_releaseOutputBuffer(encoder, static_cast<size_t>(out_idx), false);
            if (saw_eos) {
                break;
            }
        }
    }

    EXPECT_TRUE(saw_data);
    EXPECT_TRUE(saw_eos);

    AMediaCodec_stop(encoder);
    AMediaCodec_delete(encoder);
    AMediaFormat_delete(fmt);
    ANativeWindow_release(persistent);
}

TEST(MediaPipelineTest, EncodesAndDecodesAacAudio)
{
    AMediaCodec *enc = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    ASSERT_NE(enc, nullptr);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, 128000);

    ASSERT_EQ(AMediaCodec_configure(enc, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
              AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(enc), AMEDIA_OK);

    std::vector<int16_t> pcm(1024 * 2);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>((i % 64) * 200);
    }

    std::vector<std::vector<uint8_t>> aac_frames;
    std::vector<uint8_t> asc;
    for (int f = 0; f < 4; ++f) {
        int64_t in_idx = AMediaCodec_dequeueInputBuffer(enc, 50000);
        ASSERT_GE(in_idx, 0);
        size_t cap = 0;
        uint8_t *in_ptr = AMediaCodec_getInputBuffer(enc, static_cast<size_t>(in_idx), &cap);
        size_t byte_len = pcm.size() * sizeof(int16_t);
        ASSERT_GE(cap, byte_len);
        std::memcpy(in_ptr, pcm.data(), byte_len);
        uint32_t flags = (f == 3) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        ASSERT_EQ(AMediaCodec_queueInputBuffer(enc, static_cast<size_t>(in_idx), 0, byte_len,
                                               static_cast<uint64_t>(f) * 23220u, flags),
                  AMEDIA_OK);

        for (int spin = 0; spin < 8; ++spin) {
            AMediaCodecBufferInfo info{};
            int64_t out_idx = AMediaCodec_dequeueOutputBuffer(enc, &info, 10000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat *of = AMediaCodec_getOutputFormat(enc);
                void *c_ptr = nullptr;
                size_t c_len = 0;
                if (AMediaFormat_getBuffer(of, AMEDIAFORMAT_KEY_CSD_0, &c_ptr, &c_len) && c_ptr) {
                    asc.assign(static_cast<uint8_t *>(c_ptr),
                               static_cast<uint8_t *>(c_ptr) + c_len);
                }
                AMediaFormat_delete(of);
                continue;
            }
            if (out_idx < 0) {
                break;
            }
            size_t out_cap = 0;
            uint8_t *out_ptr =
                AMediaCodec_getOutputBuffer(enc, static_cast<size_t>(out_idx), &out_cap);
            if (out_ptr && info.size > 0) {
                aac_frames.emplace_back(out_ptr + info.offset, out_ptr + info.offset + info.size);
            }
            AMediaCodec_releaseOutputBuffer(enc, static_cast<size_t>(out_idx), false);
        }
    }

    if (asc.empty()) {
        AMediaFormat *of = AMediaCodec_getOutputFormat(enc);
        void *c_ptr = nullptr;
        size_t c_len = 0;
        if (of && AMediaFormat_getBuffer(of, AMEDIAFORMAT_KEY_CSD_0, &c_ptr, &c_len) && c_ptr) {
            asc.assign(static_cast<uint8_t *>(c_ptr), static_cast<uint8_t *>(c_ptr) + c_len);
        }
        AMediaFormat_delete(of);
    }

    ASSERT_FALSE(aac_frames.empty());
    AMediaCodec_stop(enc);
    AMediaCodec_delete(enc);

    AMediaCodec *dec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
    ASSERT_NE(dec, nullptr);
    if (!asc.empty()) {
        AMediaFormat_setBuffer(fmt, AMEDIAFORMAT_KEY_CSD_0, asc.data(), asc.size());
    }
    ASSERT_EQ(AMediaCodec_configure(dec, fmt, nullptr, nullptr, 0), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(dec), AMEDIA_OK);

    size_t decoded_bytes = 0;
    for (size_t i = 0; i < aac_frames.size(); ++i) {
        int64_t in_idx = AMediaCodec_dequeueInputBuffer(dec, 50000);
        ASSERT_GE(in_idx, 0);
        size_t cap = 0;
        uint8_t *in_ptr = AMediaCodec_getInputBuffer(dec, static_cast<size_t>(in_idx), &cap);
        ASSERT_GE(cap, aac_frames[i].size());
        std::memcpy(in_ptr, aac_frames[i].data(), aac_frames[i].size());
        uint32_t flags = (i + 1 == aac_frames.size()) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        ASSERT_EQ(AMediaCodec_queueInputBuffer(dec, static_cast<size_t>(in_idx), 0,
                                               aac_frames[i].size(), i * 23220u, flags),
                  AMEDIA_OK);

        for (int spin = 0; spin < 8; ++spin) {
            AMediaCodecBufferInfo info{};
            int64_t out_idx = AMediaCodec_dequeueOutputBuffer(dec, &info, 10000);
            if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                continue;
            }
            if (out_idx < 0) {
                break;
            }
            decoded_bytes += static_cast<size_t>(std::max<int32_t>(0, info.size));
            AMediaCodec_releaseOutputBuffer(dec, static_cast<size_t>(out_idx), false);
        }
    }

    EXPECT_GT(decoded_bytes, 0u);
    AMediaCodec_stop(dec);
    AMediaCodec_delete(dec);
    AMediaFormat_delete(fmt);
}

TEST(MediaPipelineTest, SupportsCodecNameParametersAndBufferFormat)
{
    AMediaCodec *codec = AMediaCodec_createEncoderByType("video/avc");
    ASSERT_NE(codec, nullptr);

    char *name = nullptr;
    ASSERT_EQ(AMediaCodec_getName(codec, &name), AMEDIA_OK);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(std::strlen(name), 0u);
    AMediaCodec_releaseName(codec, name);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 128);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 128);
    ASSERT_EQ(
        AMediaCodec_configure(codec, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
        AMEDIA_OK);

    AMediaFormat *in_fmt = AMediaCodec_getInputFormat(codec);
    ASSERT_NE(in_fmt, nullptr);
    int32_t in_w = 0;
    EXPECT_TRUE(AMediaFormat_getInt32(in_fmt, AMEDIAFORMAT_KEY_WIDTH, &in_w));
    EXPECT_EQ(in_w, 128);
    AMediaFormat_delete(in_fmt);

    ASSERT_EQ(AMediaCodec_start(codec), AMEDIA_OK);

    AMediaFormat *params = AMediaFormat_new();
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_VIDEO_BITRATE, 1500000);
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_REQUEST_SYNC_FRAME, 0);
    EXPECT_EQ(AMediaCodec_setParameters(codec, params), AMEDIA_OK);
    AMediaFormat_delete(params);

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(fmt);
}

TEST(MediaPipelineTest, RejectsDequeueInAsyncMode)
{
    AMediaCodec *codec = AMediaCodec_createDecoderByType("video/avc");
    ASSERT_NE(codec, nullptr);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 128);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 128);
    ASSERT_EQ(AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0), AMEDIA_OK);

    AMediaCodecOnAsyncNotifyCallback cb{};
    cb.onAsyncInputAvailable = [](AMediaCodec *, void *, int32_t) __attribute__((sysv_abi)) {};
    ASSERT_EQ(AMediaCodec_setAsyncNotifyCallback(codec, cb, nullptr), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(codec), AMEDIA_OK);

    EXPECT_EQ(AMediaCodec_dequeueInputBuffer(codec, 0), AMEDIA_ERROR_INVALID_OPERATION);
    AMediaCodecBufferInfo info{};
    EXPECT_EQ(AMediaCodec_dequeueOutputBuffer(codec, &info, 0), AMEDIA_ERROR_INVALID_OPERATION);

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(fmt);
}

TEST(MediaPipelineTest, AsyncNotifyCallbacksEncodeAndDecodeWithReentrantCalls)
{
    constexpr int32_t kWidth = 128;
    constexpr int32_t kHeight = 128;

    std::vector<uint8_t> solid_rgba(kWidth * kHeight * 4, 0xFF);
    for (size_t i = 0; i < static_cast<size_t>(kWidth * kHeight); ++i) {
        solid_rgba[i * 4 + 0] = 20;
        solid_rgba[i * 4 + 1] = 80;
        solid_rgba[i * 4 + 2] = 220;
        solid_rgba[i * 4 + 3] = 255;
    }
    std::vector<uint8_t> nv12;
    ConvertRgbaToNv12(solid_rgba.data(), kWidth * 4, kWidth, kHeight, nv12);

    struct AsyncCtx {
        std::mutex mu{};
        std::condition_variable cv{};
        std::vector<uint8_t> frame{};
        int frames_queued{0};
        int frames_to_send{4};
        std::vector<std::vector<uint8_t>> packets{};
        std::vector<uint8_t> csd0{};
        bool format_changed{false};
        bool done{false};
        media_status_t err{AMEDIA_OK};
    };

    AsyncCtx enc_ctx;
    enc_ctx.frame = nv12;

    AMediaCodec *encoder = AMediaCodec_createEncoderByType("video/avc");
    ASSERT_NE(encoder, nullptr);

    AMediaFormat *enc_fmt = AMediaFormat_new();
    AMediaFormat_setString(enc_fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_BIT_RATE, 800000);
    AMediaFormat_setFloat(enc_fmt, AMEDIAFORMAT_KEY_FRAME_RATE, 30.0f);
    AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatYUV420SemiPlanar);
    ASSERT_EQ(AMediaCodec_configure(encoder, enc_fmt, nullptr, nullptr,
                                    AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
              AMEDIA_OK);

    AMediaCodecOnAsyncNotifyCallback enc_cb{};
    enc_cb.onAsyncInputAvailable = [](AMediaCodec *c, void *ud,
                                      int32_t idx) __attribute__((sysv_abi)) {
        auto *ctx = static_cast<AsyncCtx *>(ud);
        std::lock_guard<std::mutex> lock(ctx->mu);
        if (ctx->frames_queued >= ctx->frames_to_send) {
            return;
        }
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getInputBuffer(c, static_cast<size_t>(idx), &cap);
        if (!buf || cap < ctx->frame.size()) {
            return;
        }
        std::memcpy(buf, ctx->frame.data(), ctx->frame.size());
        uint32_t flags = (ctx->frames_queued + 1 == ctx->frames_to_send)
                             ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM
                             : 0;
        AMediaCodec_queueInputBuffer(c, static_cast<size_t>(idx), 0, ctx->frame.size(),
                                     static_cast<uint64_t>(ctx->frames_queued) * 33333u, flags);
        ctx->frames_queued++;
    };
    enc_cb.onAsyncFormatChanged = [](AMediaCodec *, void *ud,
                                     AMediaFormat *fmt_out) __attribute__((sysv_abi)) {
        auto *ctx = static_cast<AsyncCtx *>(ud);
        std::lock_guard<std::mutex> lock(ctx->mu);
        ctx->format_changed = true;
        void *p = nullptr;
        size_t n = 0;
        if (fmt_out && AMediaFormat_getBuffer(fmt_out, AMEDIAFORMAT_KEY_CSD_0, &p, &n) && p) {
            ctx->csd0.assign(static_cast<uint8_t *>(p), static_cast<uint8_t *>(p) + n);
        }
    };
    enc_cb.onAsyncOutputAvailable = [](AMediaCodec *c, void *ud, int32_t idx,
                                       AMediaCodecBufferInfo *info) __attribute__((sysv_abi)) {
        auto *ctx = static_cast<AsyncCtx *>(ud);
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(c, static_cast<size_t>(idx), &cap);
        {
            std::lock_guard<std::mutex> lock(ctx->mu);
            if (buf && info && info->size > 0) {
                ctx->packets.emplace_back(buf + info->offset, buf + info->offset + info->size);
            }
            if (info && (info->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                ctx->done = true;
            }
        }
        AMediaCodec_releaseOutputBuffer(c, static_cast<size_t>(idx), false);
        ctx->cv.notify_all();
    };
    enc_cb.onAsyncError = [](AMediaCodec *, void *ud, media_status_t error, int32_t, const char *)
                              __attribute__((sysv_abi)) {
                                  auto *ctx = static_cast<AsyncCtx *>(ud);
                                  std::lock_guard<std::mutex> lock(ctx->mu);
                                  ctx->err = error;
                                  ctx->done = true;
                                  ctx->cv.notify_all();
                              };

    ASSERT_EQ(AMediaCodec_setAsyncNotifyCallback(encoder, enc_cb, &enc_ctx), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(encoder), AMEDIA_OK);

    {
        std::unique_lock<std::mutex> lock(enc_ctx.mu);
        ASSERT_TRUE(enc_ctx.cv.wait_for(lock, std::chrono::seconds(5),
                                        [&enc_ctx]() { return enc_ctx.done; }));
    }
    EXPECT_EQ(enc_ctx.err, AMEDIA_OK);
    EXPECT_FALSE(enc_ctx.packets.empty());

    AMediaCodec_stop(encoder);
    AMediaCodec_delete(encoder);
    AMediaFormat_delete(enc_fmt);

    struct DecCtx {
        std::mutex mu{};
        std::condition_variable cv{};
        std::vector<std::vector<uint8_t>> inputs{};
        size_t next_in{0};
        int decoded_frames{0};
        bool done{false};
    };

    DecCtx dec_ctx;
    dec_ctx.inputs = std::move(enc_ctx.packets);

    AMediaCodec *decoder = AMediaCodec_createDecoderByType("video/avc");
    ASSERT_NE(decoder, nullptr);
    AMediaFormat *dec_fmt = AMediaFormat_new();
    AMediaFormat_setString(dec_fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(dec_fmt, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(dec_fmt, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    if (!enc_ctx.csd0.empty()) {
        AMediaFormat_setBuffer(dec_fmt, AMEDIAFORMAT_KEY_CSD_0, enc_ctx.csd0.data(),
                               enc_ctx.csd0.size());
    }
    ASSERT_EQ(AMediaCodec_configure(decoder, dec_fmt, nullptr, nullptr, 0), AMEDIA_OK);

    AMediaCodecOnAsyncNotifyCallback dec_cb{};
    dec_cb.onAsyncInputAvailable = [](AMediaCodec *c, void *ud,
                                      int32_t idx) __attribute__((sysv_abi)) {
        auto *ctx = static_cast<DecCtx *>(ud);
        std::lock_guard<std::mutex> lock(ctx->mu);
        if (ctx->next_in >= ctx->inputs.size()) {
            return;
        }
        const auto &pkt = ctx->inputs[ctx->next_in];
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getInputBuffer(c, static_cast<size_t>(idx), &cap);
        if (!buf || cap < pkt.size()) {
            return;
        }
        std::memcpy(buf, pkt.data(), pkt.size());
        uint32_t flags =
            (ctx->next_in + 1 == ctx->inputs.size()) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        AMediaCodec_queueInputBuffer(c, static_cast<size_t>(idx), 0, pkt.size(),
                                     ctx->next_in * 33333u, flags);
        ctx->next_in++;
    };
    dec_cb.onAsyncOutputAvailable = [](AMediaCodec *c, void *ud, int32_t idx,
                                       AMediaCodecBufferInfo *info) __attribute__((sysv_abi)) {
        auto *ctx = static_cast<DecCtx *>(ud);
        {
            std::lock_guard<std::mutex> lock(ctx->mu);
            if (info && info->size > 0) {
                ctx->decoded_frames++;
            }
            if (info && (info->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                ctx->done = true;
            }
        }
        AMediaCodec_releaseOutputBuffer(c, static_cast<size_t>(idx), false);
        ctx->cv.notify_all();
    };

    ASSERT_EQ(AMediaCodec_setAsyncNotifyCallback(decoder, dec_cb, &dec_ctx), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(decoder), AMEDIA_OK);

    {
        std::unique_lock<std::mutex> lock(dec_ctx.mu);
        ASSERT_TRUE(dec_ctx.cv.wait_for(lock, std::chrono::seconds(5),
                                        [&dec_ctx]() { return dec_ctx.done; }));
    }
    EXPECT_GT(dec_ctx.decoded_frames, 0);

    AMediaCodec_stop(decoder);
    AMediaCodec_delete(decoder);
    AMediaFormat_delete(dec_fmt);
}

TEST(MediaPipelineTest, MuxerRejectsUnsupportedFormatsAndInvalidMetadata)
{
    auto path = MakeTempPath("_invalid_mux.bin");
    int fd = _open(path.string().c_str(), _O_CREAT | _O_RDWR | _O_TRUNC | _O_BINARY, 0644);
    ASSERT_GE(fd, 0);

    EXPECT_EQ(AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_WEBM), nullptr);
    EXPECT_EQ(AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_OGG), nullptr);

    AMediaMuxer *mux = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    ASSERT_NE(mux, nullptr);

    EXPECT_EQ(AMediaMuxer_setLocation(mux, 95.0f, 0.0f), AMEDIA_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(AMediaMuxer_setLocation(mux, 0.0f, 185.0f), AMEDIA_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(AMediaMuxer_setLocation(mux, 37.42f, -122.08f), AMEDIA_OK);

    EXPECT_EQ(AMediaMuxer_setOrientationHint(mux, 45), AMEDIA_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(AMediaMuxer_setOrientationHint(mux, 90), AMEDIA_OK);

    AMediaFormat *vfmt = AMediaFormat_new();
    AMediaFormat_setString(vfmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(vfmt, AMEDIAFORMAT_KEY_WIDTH, 128);
    AMediaFormat_setInt32(vfmt, AMEDIAFORMAT_KEY_HEIGHT, 128);
    ssize_t tid = AMediaMuxer_addTrack(mux, vfmt);
    EXPECT_GE(tid, 0);

    EXPECT_EQ(AMediaMuxer_start(mux), AMEDIA_OK);
    EXPECT_LT(AMediaMuxer_addTrack(mux, vfmt), 0);
    EXPECT_EQ(AMediaMuxer_setOrientationHint(mux, 180), AMEDIA_ERROR_INVALID_OPERATION);

    EXPECT_EQ(AMediaMuxer_stop(mux), AMEDIA_OK);
    EXPECT_EQ(AMediaMuxer_delete(mux), AMEDIA_OK);
    AMediaFormat_delete(vfmt);
    _close(fd);
    std::filesystem::remove(path);
}

TEST(MediaPipelineTest, MuxesEncodedVideoAndAudioIntoPlayableMp4)
{
    constexpr int32_t kWidth = 128;
    constexpr int32_t kHeight = 128;

    auto out_path = MakeTempPath("_muxed.mp4");
    int out_fd = _open(out_path.string().c_str(), _O_CREAT | _O_RDWR | _O_TRUNC | _O_BINARY, 0644);
    ASSERT_GE(out_fd, 0);

    AMediaMuxer *mux = AMediaMuxer_new(out_fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    ASSERT_NE(mux, nullptr);

    // Encode H.264 frames via input surface
    AMediaCodec *venc = AMediaCodec_createEncoderByType("video/avc");
    ASSERT_NE(venc, nullptr);
    AMediaFormat *vcfg = AMediaFormat_new();
    AMediaFormat_setString(vcfg, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(vcfg, AMEDIAFORMAT_KEY_WIDTH, kWidth);
    AMediaFormat_setInt32(vcfg, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
    AMediaFormat_setInt32(vcfg, AMEDIAFORMAT_KEY_BIT_RATE, 800000);
    AMediaFormat_setFloat(vcfg, AMEDIAFORMAT_KEY_FRAME_RATE, 30.0f);
    AMediaFormat_setInt32(vcfg, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    ASSERT_EQ(
        AMediaCodec_configure(venc, vcfg, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
        AMEDIA_OK);
    ANativeWindow *vsuf = nullptr;
    ASSERT_EQ(AMediaCodec_createInputSurface(venc, &vsuf), AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(venc), AMEDIA_OK);

    for (int f = 0; f < 4; ++f) {
        ANativeWindow_Buffer wb{};
        ASSERT_EQ(ANativeWindow_lock(vsuf, &wb, nullptr), 0);
        auto *px = static_cast<uint8_t *>(wb.bits);
        for (int32_t y = 0; y < wb.height; ++y) {
            uint8_t *row = px + static_cast<size_t>(y * wb.stride) * 4u;
            for (int32_t x = 0; x < wb.width; ++x) {
                row[x * 4 + 0] = 60;
                row[x * 4 + 1] = 170;
                row[x * 4 + 2] = 90;
                row[x * 4 + 3] = 255;
            }
        }
        ASSERT_EQ(ANativeWindow_unlockAndPost(vsuf), 0);
    }
    ASSERT_EQ(AMediaCodec_signalEndOfInputStream(venc), AMEDIA_OK);

    struct EncSample {
        std::vector<uint8_t> bytes{};
        AMediaCodecBufferInfo info{};
    };
    std::vector<EncSample> vsamples;
    AMediaFormat *vout_fmt = nullptr;
    for (int i = 0; i < 32; ++i) {
        AMediaCodecBufferInfo info{};
        int64_t idx = AMediaCodec_dequeueOutputBuffer(venc, &info, 20000);
        if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (vout_fmt) {
                AMediaFormat_delete(vout_fmt);
            }
            vout_fmt = AMediaCodec_getOutputFormat(venc);
            continue;
        }
        if (idx < 0) {
            break;
        }
        if (!vout_fmt) {
            vout_fmt = AMediaCodec_getOutputFormat(venc);
        }
        size_t sz = 0;
        uint8_t *p = AMediaCodec_getOutputBuffer(venc, static_cast<size_t>(idx), &sz);
        if (p && info.size > 0) {
            vsamples.push_back(EncSample{
                std::vector<uint8_t>(p + info.offset, p + info.offset + info.size), info});
        }
        AMediaCodec_releaseOutputBuffer(venc, static_cast<size_t>(idx), false);
        if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
            break;
        }
    }
    if (!vout_fmt) {
        vout_fmt = AMediaCodec_getOutputFormat(venc);
    }
    ASSERT_NE(vout_fmt, nullptr);
    ASSERT_FALSE(vsamples.empty());

    // Encode AAC frames
    AMediaCodec *aenc = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    ASSERT_NE(aenc, nullptr);
    AMediaFormat *acfg = AMediaFormat_new();
    AMediaFormat_setString(acfg, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(acfg, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(acfg, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(acfg, AMEDIAFORMAT_KEY_BIT_RATE, 128000);
    ASSERT_EQ(
        AMediaCodec_configure(aenc, acfg, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE),
        AMEDIA_OK);
    ASSERT_EQ(AMediaCodec_start(aenc), AMEDIA_OK);

    std::vector<int16_t> pcm(1024 * 2, 300);
    std::vector<EncSample> asamples;
    AMediaFormat *aout_fmt = nullptr;
    for (int f = 0; f < 4; ++f) {
        int64_t in_idx = AMediaCodec_dequeueInputBuffer(aenc, 50000);
        ASSERT_GE(in_idx, 0);
        size_t cap = 0;
        uint8_t *in_ptr = AMediaCodec_getInputBuffer(aenc, static_cast<size_t>(in_idx), &cap);
        size_t b = pcm.size() * sizeof(int16_t);
        std::memcpy(in_ptr, pcm.data(), b);
        uint32_t fl = (f == 3) ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
        ASSERT_EQ(AMediaCodec_queueInputBuffer(aenc, static_cast<size_t>(in_idx), 0, b,
                                               static_cast<uint64_t>(f) * 23220u, fl),
                  AMEDIA_OK);

        for (int spin = 0; spin < 8; ++spin) {
            AMediaCodecBufferInfo info{};
            int64_t idx = AMediaCodec_dequeueOutputBuffer(aenc, &info, 10000);
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                if (aout_fmt) {
                    AMediaFormat_delete(aout_fmt);
                }
                aout_fmt = AMediaCodec_getOutputFormat(aenc);
                continue;
            }
            if (idx < 0) {
                break;
            }
            if (!aout_fmt) {
                aout_fmt = AMediaCodec_getOutputFormat(aenc);
            }
            size_t sz = 0;
            uint8_t *p = AMediaCodec_getOutputBuffer(aenc, static_cast<size_t>(idx), &sz);
            if (p && info.size > 0) {
                asamples.push_back(EncSample{
                    std::vector<uint8_t>(p + info.offset, p + info.offset + info.size), info});
            }
            AMediaCodec_releaseOutputBuffer(aenc, static_cast<size_t>(idx), false);
        }
    }
    if (!aout_fmt) {
        aout_fmt = AMediaCodec_getOutputFormat(aenc);
    }
    ASSERT_NE(aout_fmt, nullptr);
    ASSERT_FALSE(asamples.empty());

    ssize_t vtrack = AMediaMuxer_addTrack(mux, vout_fmt);
    ssize_t atrack = AMediaMuxer_addTrack(mux, aout_fmt);
    EXPECT_GE(vtrack, 0);
    EXPECT_GE(atrack, 0);
    ASSERT_EQ(AMediaMuxer_start(mux), AMEDIA_OK);

    for (const auto &s : vsamples) {
        EXPECT_EQ(
            AMediaMuxer_writeSampleData(mux, static_cast<size_t>(vtrack), s.bytes.data(), &s.info),
            AMEDIA_OK);
    }
    for (const auto &s : asamples) {
        EXPECT_EQ(
            AMediaMuxer_writeSampleData(mux, static_cast<size_t>(atrack), s.bytes.data(), &s.info),
            AMEDIA_OK);
    }

    EXPECT_EQ(AMediaMuxer_stop(mux), AMEDIA_OK);
    EXPECT_EQ(AMediaMuxer_delete(mux), AMEDIA_OK);
    _close(out_fd);

    // Verify muxed MP4 parses cleanly via AMediaExtractor
    int in_fd = _open(out_path.string().c_str(), _O_RDONLY | _O_BINARY);
    ASSERT_GE(in_fd, 0);
    AMediaExtractor *ex = AMediaExtractor_new();
    ASSERT_EQ(AMediaExtractor_setDataSourceFd(ex, in_fd, 0, -1), AMEDIA_OK);
    size_t ntracks = AMediaExtractor_getTrackCount(ex);
    EXPECT_EQ(ntracks, 2u);

    bool found_video = false;
    bool found_audio = false;
    size_t video_track_index = 0;
    for (size_t ti = 0; ti < ntracks; ++ti) {
        AMediaFormat *tf = AMediaExtractor_getTrackFormat(ex, ti);
        ASSERT_NE(tf, nullptr);
        const char *tmime = nullptr;
        ASSERT_TRUE(AMediaFormat_getString(tf, AMEDIAFORMAT_KEY_MIME, &tmime));
        ASSERT_NE(tmime, nullptr);
        if (std::string_view(tmime) == "video/avc") {
            int32_t ex_w = 0;
            int32_t ex_h = 0;
            EXPECT_TRUE(AMediaFormat_getInt32(tf, AMEDIAFORMAT_KEY_WIDTH, &ex_w));
            EXPECT_TRUE(AMediaFormat_getInt32(tf, AMEDIAFORMAT_KEY_HEIGHT, &ex_h));
            EXPECT_EQ(ex_w, kWidth);
            EXPECT_EQ(ex_h, kHeight);
            found_video = true;
            video_track_index = ti;
        } else if (std::string_view(tmime) == "audio/mp4a-latm") {
            int32_t ex_sr = 0;
            EXPECT_TRUE(AMediaFormat_getInt32(tf, AMEDIAFORMAT_KEY_SAMPLE_RATE, &ex_sr));
            EXPECT_EQ(ex_sr, 44100);
            found_audio = true;
        }
        AMediaFormat_delete(tf);
    }
    EXPECT_TRUE(found_video);
    EXPECT_TRUE(found_audio);

    EXPECT_EQ(AMediaExtractor_selectTrack(ex, video_track_index), AMEDIA_OK);
    std::vector<uint8_t> extracted(65536);
    EXPECT_GT(AMediaExtractor_readSampleData(ex, extracted.data(), extracted.size()), 0);
    AMediaExtractor_delete(ex);
    _close(in_fd);

    AMediaFormat_delete(vout_fmt);
    AMediaFormat_delete(aout_fmt);
    AMediaFormat_delete(vcfg);
    AMediaFormat_delete(acfg);
    AMediaCodec_stop(venc);
    AMediaCodec_delete(venc);
    ANativeWindow_release(vsuf);
    AMediaCodec_stop(aenc);
    AMediaCodec_delete(aenc);
    std::filesystem::remove(out_path);
}

} // namespace portage::media
