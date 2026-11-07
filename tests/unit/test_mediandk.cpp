#include "media/media_codec.hpp"
#include "media/media_extractor.hpp"
#include "media/media_format.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace portage::media {

class MediaNdkTest : public ::testing::Test {
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MediaNdkTest, CreatesAndManipulatesMediaFormat)
{
    AMediaFormat *fmt = AMediaFormat_new();
    ASSERT_NE(fmt, nullptr);

    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 1920);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 1080);
    AMediaFormat_setInt64(fmt, AMEDIAFORMAT_KEY_DURATION, 60000000LL);
    AMediaFormat_setFloat(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, 60.0f);
    AMediaFormat_setSize(fmt, "custom_size", 4096);

    const uint8_t raw_csd[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f};
    AMediaFormat_setBuffer(fmt, AMEDIAFORMAT_KEY_CSD_0, raw_csd, sizeof(raw_csd));

    const char *mime = nullptr;
    EXPECT_TRUE(AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime));
    ASSERT_NE(mime, nullptr);
    EXPECT_STREQ(mime, "video/avc");

    int32_t width = 0;
    EXPECT_TRUE(AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &width));
    EXPECT_EQ(width, 1920);

    int32_t height = 0;
    EXPECT_TRUE(AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &height));
    EXPECT_EQ(height, 1080);

    int64_t duration = 0;
    EXPECT_TRUE(AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &duration));
    EXPECT_EQ(duration, 60000000LL);

    float frame_rate = 0.0f;
    EXPECT_TRUE(AMediaFormat_getFloat(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, &frame_rate));
    EXPECT_FLOAT_EQ(frame_rate, 60.0f);

    size_t custom_sz = 0;
    EXPECT_TRUE(AMediaFormat_getSize(fmt, "custom_size", &custom_sz));
    EXPECT_EQ(custom_sz, 4096u);

    void *csd_data = nullptr;
    size_t csd_len = 0;
    EXPECT_TRUE(AMediaFormat_getBuffer(fmt, AMEDIAFORMAT_KEY_CSD_0, &csd_data, &csd_len));
    ASSERT_NE(csd_data, nullptr);
    EXPECT_EQ(csd_len, sizeof(raw_csd));
    EXPECT_EQ(std::memcmp(csd_data, raw_csd, sizeof(raw_csd)), 0);

    const char *desc = AMediaFormat_toString(fmt);
    ASSERT_NE(desc, nullptr);
    EXPECT_NE(std::strstr(desc, "video/avc"), nullptr);

    AMediaFormat *fmt_copy = AMediaFormat_new();
    ASSERT_NE(fmt_copy, nullptr);
    EXPECT_TRUE(AMediaFormat_copy(fmt_copy, fmt));

    int32_t copy_width = 0;
    EXPECT_TRUE(AMediaFormat_getInt32(fmt_copy, AMEDIAFORMAT_KEY_WIDTH, &copy_width));
    EXPECT_EQ(copy_width, 1920);

    AMediaFormat_clear(fmt_copy);
    EXPECT_FALSE(AMediaFormat_getInt32(fmt_copy, AMEDIAFORMAT_KEY_WIDTH, &copy_width));

    EXPECT_EQ(AMediaFormat_delete(fmt_copy), AMEDIA_OK);
    EXPECT_EQ(AMediaFormat_delete(fmt), AMEDIA_OK);
}

TEST_F(MediaNdkTest, ExtractsTrackInformationAndSamples)
{
    AMediaExtractor *ex = AMediaExtractor_new();
    ASSERT_NE(ex, nullptr);

    // Using nonexistent file falls back to synthetic source cleanly
    media_status_t status = AMediaExtractor_setDataSource(ex, "synthetic_stream.mp4");
    EXPECT_EQ(status, AMEDIA_OK);

    size_t track_count = AMediaExtractor_getTrackCount(ex);
    EXPECT_GE(track_count, 2u);

    AMediaFormat *track0_fmt = AMediaExtractor_getTrackFormat(ex, 0);
    ASSERT_NE(track0_fmt, nullptr);
    const char *track0_mime = nullptr;
    EXPECT_TRUE(AMediaFormat_getString(track0_fmt, AMEDIAFORMAT_KEY_MIME, &track0_mime));
    ASSERT_NE(track0_mime, nullptr);
    EXPECT_STREQ(track0_mime, "video/avc");
    AMediaFormat_delete(track0_fmt);

    AMediaFormat *track1_fmt = AMediaExtractor_getTrackFormat(ex, 1);
    ASSERT_NE(track1_fmt, nullptr);
    const char *track1_mime = nullptr;
    EXPECT_TRUE(AMediaFormat_getString(track1_fmt, AMEDIAFORMAT_KEY_MIME, &track1_mime));
    ASSERT_NE(track1_mime, nullptr);
    EXPECT_STREQ(track1_mime, "audio/mp4a-latm");
    AMediaFormat_delete(track1_fmt);

    EXPECT_EQ(AMediaExtractor_selectTrack(ex, 0), AMEDIA_OK);

    std::vector<uint8_t> sample_buf(4096);
    int64_t read_bytes =
        AMediaExtractor_readSampleData(ex, sample_buf.data(), sample_buf.size());
    EXPECT_GT(read_bytes, 0);

    EXPECT_EQ(AMediaExtractor_getSampleTrackIndex(ex), 0);
    EXPECT_GE(AMediaExtractor_getSampleSize(ex), 0);
    EXPECT_GE(AMediaExtractor_getSampleTime(ex), 0);

    EXPECT_TRUE(AMediaExtractor_advance(ex));
    EXPECT_EQ(AMediaExtractor_seekTo(ex, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC), AMEDIA_OK);

    EXPECT_EQ(AMediaExtractor_unselectTrack(ex, 0), AMEDIA_OK);
    EXPECT_EQ(AMediaExtractor_delete(ex), AMEDIA_OK);
}

TEST_F(MediaNdkTest, DecodesAudioStreamToPcm)
{
    AMediaCodec *codec = AMediaCodec_createDecoderByType("audio/mp4a-latm");
    ASSERT_NE(codec, nullptr);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);

    EXPECT_EQ(AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_start(codec), AMEDIA_OK);

    int64_t in_idx = AMediaCodec_dequeueInputBuffer(codec, 1000);
    ASSERT_GE(in_idx, 0);

    size_t in_buf_size = 0;
    uint8_t *in_buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_idx), &in_buf_size);
    ASSERT_NE(in_buf, nullptr);
    ASSERT_GT(in_buf_size, 0u);

    // Write 512 bytes of synthetic audio test data
    size_t test_audio_size = 512;
    for (size_t i = 0; i < test_audio_size; ++i) {
        in_buf[i] = static_cast<uint8_t>(i & 0xFF);
    }

    EXPECT_EQ(AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0, test_audio_size,
                                           1000, 0),
              AMEDIA_OK);

    AMediaCodecBufferInfo out_info{};
    int64_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &out_info, 1000);
    ASSERT_GE(out_idx, 0);
    EXPECT_EQ(out_info.size, static_cast<int32_t>(test_audio_size));

    size_t out_buf_size = 0;
    uint8_t *out_buf =
        AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(out_idx), &out_buf_size);
    ASSERT_NE(out_buf, nullptr);
    EXPECT_GE(out_buf_size, test_audio_size);
    EXPECT_EQ(std::memcmp(out_buf, in_buf, test_audio_size), 0);

    EXPECT_EQ(
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_idx), false /* render */),
        AMEDIA_OK);

    EXPECT_EQ(AMediaCodec_stop(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_delete(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaFormat_delete(fmt), AMEDIA_OK);
}

TEST_F(MediaNdkTest, DecodesVideoStreamAndBlitsToWindow)
{
    ANativeWindow win{64, 64, 1, 1, nullptr};

    AMediaCodec *codec = AMediaCodec_createDecoderByType("video/avc");
    ASSERT_NE(codec, nullptr);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 64);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 64);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, 256);

    EXPECT_EQ(AMediaCodec_configure(codec, fmt, &win, nullptr, 0), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_start(codec), AMEDIA_OK);

    int64_t in_idx = AMediaCodec_dequeueInputBuffer(codec, 1000);
    ASSERT_GE(in_idx, 0);

    size_t in_buf_size = 0;
    uint8_t *in_buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_idx), &in_buf_size);
    ASSERT_NE(in_buf, nullptr);

    // Fill with pattern 0xAABBCCDD as 32-bit words
    size_t frame_bytes = 64 * 256;
    ASSERT_GE(in_buf_size, frame_bytes);
    auto *in_words = reinterpret_cast<uint32_t *>(in_buf);
    for (size_t i = 0; i < frame_bytes / sizeof(uint32_t); ++i) {
        in_words[i] = 0xAABBCCDD;
    }

    EXPECT_EQ(AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(in_idx), 0, frame_bytes,
                                           2000, 0),
              AMEDIA_OK);

    AMediaCodecBufferInfo out_info{};
    int64_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &out_info, 1000);
    ASSERT_GE(out_idx, 0);

    // Release with render=true to trigger surface blit
    EXPECT_EQ(
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_idx), true /* render */),
        AMEDIA_OK);

    ASSERT_FALSE(win.frontbuffer.empty());
    auto *front_pixels = reinterpret_cast<const uint32_t *>(win.frontbuffer.data());
    EXPECT_EQ(front_pixels[0], 0xAABBCCDD);
    EXPECT_EQ(front_pixels[10], 0xAABBCCDD);

    EXPECT_EQ(AMediaCodec_stop(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_delete(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaFormat_delete(fmt), AMEDIA_OK);
}

TEST_F(MediaNdkTest, ManagesCodecLifecycleAndStateTransitions)
{
    AMediaCodec *codec = AMediaCodec_createCodecByName("c2.android.avc.decoder");
    ASSERT_NE(codec, nullptr);

    // Calling start before configure should fail
    EXPECT_EQ(AMediaCodec_start(codec), AMEDIA_ERROR_INVALID_OPERATION);

    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 320);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 240);

    EXPECT_EQ(AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_start(codec), AMEDIA_OK);

    EXPECT_EQ(AMediaCodec_flush(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_stop(codec), AMEDIA_OK);
    EXPECT_EQ(AMediaCodec_delete(codec), AMEDIA_OK);

    // Null checks
    EXPECT_EQ(AMediaCodec_start(nullptr), AMEDIA_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(AMediaCodec_stop(nullptr), AMEDIA_ERROR_INVALID_PARAMETER);
    EXPECT_EQ(AMediaCodec_delete(nullptr), AMEDIA_ERROR_INVALID_PARAMETER);

    AMediaFormat_delete(fmt);
}

TEST_F(MediaNdkTest, RegistersAllNdkSymbols)
{
    auto &registry = ndk::NdkSymbolRegistry::Instance();

    const char *media_symbols[] = {
        "AMediaFormat_new",
        "AMediaFormat_delete",
        "AMediaFormat_getString",
        "AMediaFormat_getInt32",
        "AMediaFormat_getInt64",
        "AMediaFormat_getFloat",
        "AMediaFormat_getSize",
        "AMediaFormat_getBuffer",
        "AMediaFormat_setString",
        "AMediaFormat_setInt32",
        "AMediaFormat_setInt64",
        "AMediaFormat_setFloat",
        "AMediaFormat_setSize",
        "AMediaFormat_setBuffer",
        "AMediaFormat_toString",
        "AMediaFormat_copy",
        "AMediaFormat_clear",
        "AMediaExtractor_new",
        "AMediaExtractor_delete",
        "AMediaExtractor_setDataSourceFd",
        "AMediaExtractor_setDataSource",
        "AMediaExtractor_getTrackCount",
        "AMediaExtractor_getTrackFormat",
        "AMediaExtractor_selectTrack",
        "AMediaExtractor_unselectTrack",
        "AMediaExtractor_readSampleData",
        "AMediaExtractor_getSampleSize",
        "AMediaExtractor_getSampleTrackIndex",
        "AMediaExtractor_getSampleTime",
        "AMediaExtractor_getSampleFlags",
        "AMediaExtractor_advance",
        "AMediaExtractor_seekTo",
        "AMediaCodec_createCodecByName",
        "AMediaCodec_createDecoderByType",
        "AMediaCodec_createEncoderByType",
        "AMediaCodec_delete",
        "AMediaCodec_configure",
        "AMediaCodec_start",
        "AMediaCodec_stop",
        "AMediaCodec_flush",
        "AMediaCodec_dequeueInputBuffer",
        "AMediaCodec_getInputBuffer",
        "AMediaCodec_queueInputBuffer",
        "AMediaCodec_dequeueOutputBuffer",
        "AMediaCodec_getOutputBuffer",
        "AMediaCodec_getOutputFormat",
        "AMediaCodec_releaseOutputBuffer",
        "AMediaCodec_releaseOutputBufferAtTime",
    };

    for (const char *sym : media_symbols) {
        uintptr_t addr = registry.Lookup(sym);
        EXPECT_NE(addr, 0u) << "Symbol missing in registry: " << sym;
    }
}

} // namespace portage::media
