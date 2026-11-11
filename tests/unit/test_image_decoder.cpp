#include "fs/fd_table.hpp"
#include "media/wmf_decoder.hpp"
#include "ndk/image_decoder.hpp"
#include "ndk/jnigraphics.hpp"
#include "package/asset_manager.hpp"
#include "package/zip_archive.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

using namespace portage::ndk;

namespace {

std::vector<uint8_t> EncodeTestImage(const GUID &container_guid, uint32_t width, uint32_t height,
                                     const std::vector<uint8_t> &rgba_pixels)
{
    std::vector<uint8_t> encoded;
    portage::media::WmfManager::Instance().Initialize();
    portage::media::WmfManager::Instance().DispatchSync([&]() {
        IWICImagingFactory *factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))) ||
            !factory) {
            return;
        }
        IStream *stream = nullptr;
        IWICBitmapEncoder *encoder = nullptr;
        IWICBitmapFrameEncode *frame = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
            SUCCEEDED(factory->CreateEncoder(container_guid, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height))) {
            WICPixelFormatGUID pf = GUID_WICPixelFormat32bppRGBA;
            if (container_guid == GUID_ContainerFormatJpeg ||
                container_guid == GUID_ContainerFormatBmp) {
                pf = GUID_WICPixelFormat24bppBGR;
            }
            if (SUCCEEDED(frame->SetPixelFormat(&pf))) {
                if (pf == GUID_WICPixelFormat24bppBGR) {
                    std::vector<uint8_t> bgr(static_cast<size_t>(width) * height * 3);
                    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                        bgr[i * 3 + 0] = rgba_pixels[i * 4 + 2];
                        bgr[i * 3 + 1] = rgba_pixels[i * 4 + 1];
                        bgr[i * 3 + 2] = rgba_pixels[i * 4 + 0];
                    }
                    frame->WritePixels(height, width * 3, static_cast<UINT>(bgr.size()),
                                       bgr.data());
                } else if (pf == GUID_WICPixelFormat32bppBGRA ||
                           pf == GUID_WICPixelFormat32bppPBGRA) {
                    std::vector<uint8_t> bgra(rgba_pixels.size());
                    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                        bgra[i * 4 + 0] = rgba_pixels[i * 4 + 2];
                        bgra[i * 4 + 1] = rgba_pixels[i * 4 + 1];
                        bgra[i * 4 + 2] = rgba_pixels[i * 4 + 0];
                        bgra[i * 4 + 3] = rgba_pixels[i * 4 + 3];
                    }
                    frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()),
                                       bgra.data());
                } else {
                    frame->WritePixels(height, width * 4, static_cast<UINT>(rgba_pixels.size()),
                                       const_cast<BYTE *>(rgba_pixels.data()));
                }
                if (SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                    STATSTG stg{};
                    LARGE_INTEGER zero{};
                    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                    if (SUCCEEDED(stream->Stat(&stg, STATFLAG_NONAME)) && stg.cbSize.QuadPart > 0) {
                        encoded.resize(static_cast<size_t>(stg.cbSize.QuadPart));
                        ULONG read_bytes = 0;
                        stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()),
                                     &read_bytes);
                        encoded.resize(read_bytes);
                    }
                }
            }
        }
        if (frame)
            frame->Release();
        if (encoder)
            encoder->Release();
        if (stream)
            stream->Release();
        factory->Release();
    });
    return encoded;
}

// Minimal valid 2x2 2-frame animated GIF89a (frame 0 red, frame 1 blue, delay 120ms each)
std::vector<uint8_t> BuildTwoFrameGif()
{
    static const uint8_t kGif[] = {
        'G',  'I',  'F',  '8',  '9',  'a',  0x02, 0x00, 0x02, 0x00, 0x80, 0x00,
        0x00, 0xFF, 0x00, 0x00, // Color 0: Red
        0x00, 0x00, 0xFF,       // Color 1: Blue
        0x21, 0xFF, 0x0B, 'N',  'E',  'T',  'S',  'C',  'A',  'P',  'E',  '2',
        '.',  '0',  0x03, 0x01, 0x02, 0x00, 0x00,       // Loop count 2
        0x21, 0xF9, 0x04, 0x04, 0x0C, 0x00, 0x00, 0x00, // Delay 12 * 10ms = 120ms
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x02, 0x02,
        0x84, 0x51, 0x00, 0x21, 0xF9, 0x04, 0x04, 0x0C, 0x00, 0x00, 0x00, // Delay 120ms
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x02, 0x02,
        0x8C, 0x53, 0x00, 0x3B};
    return std::vector<uint8_t>(std::begin(kGif), std::end(kGif));
}

} // namespace

TEST(ImageDecoderTest, DecodesPngWithResizeCropAndFormats)
{
    std::vector<uint8_t> src(4 * 4 * 4, 0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            size_t idx = (static_cast<size_t>(y) * 4 + x) * 4;
            src[idx + 0] = (x < 2) ? 255 : 0;
            src[idx + 1] = (y >= 2) ? 200 : 40;
            src[idx + 2] = 0;
            src[idx + 3] = 128;
        }
    }

    auto png = EncodeTestImage(GUID_ContainerFormatPng, 4, 4, src);
    ASSERT_FALSE(png.empty());

    AImageDecoder *decoder = nullptr;
    ASSERT_EQ(AImageDecoder_createFromBuffer(png.data(), png.size(), &decoder),
              ANDROID_IMAGE_DECODER_SUCCESS);
    ASSERT_NE(decoder, nullptr);

    const AImageDecoderHeaderInfo *hdr = AImageDecoder_getHeaderInfo(decoder);
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(AImageDecoderHeaderInfo_getWidth(hdr), 4);
    EXPECT_EQ(AImageDecoderHeaderInfo_getHeight(hdr), 4);
    EXPECT_STREQ(AImageDecoderHeaderInfo_getMimeType(hdr), "image/png");
    EXPECT_EQ(AImageDecoderHeaderInfo_getAndroidBitmapFormat(hdr), ANDROID_BITMAP_FORMAT_RGBA_8888);
    EXPECT_FALSE(AImageDecoder_isAnimated(decoder));

    size_t min_stride = AImageDecoder_getMinimumStride(decoder);
    EXPECT_EQ(min_stride, 16U);

    // Unpremultiplied crop of bottom-left quadrant (0,2)-(2,4)
    ARect crop{0, 2, 2, 4};
    EXPECT_EQ(AImageDecoder_setCrop(decoder, crop), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(AImageDecoder_setUnpremultipliedRequired(decoder, true),
              ANDROID_IMAGE_DECODER_SUCCESS);

    min_stride = AImageDecoder_getMinimumStride(decoder);
    EXPECT_EQ(min_stride, 8U);

    std::vector<uint8_t> out(min_stride * 2, 0);
    EXPECT_EQ(AImageDecoder_decodeImage(decoder, out.data(), min_stride, out.size()),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(out[0], 255);
    EXPECT_EQ(out[1], 200);
    EXPECT_EQ(out[2], 0);
    EXPECT_EQ(out[3], 128);

    // RGB_565 target scaled back to 2x2
    EXPECT_EQ(AImageDecoder_setCrop(decoder, ARect{0, 0, 0, 0}), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(AImageDecoder_setTargetSize(decoder, 2, 2), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGB_565),
              ANDROID_IMAGE_DECODER_SUCCESS);
    size_t stride_565 = AImageDecoder_getMinimumStride(decoder);
    EXPECT_EQ(stride_565, 4U);
    std::vector<uint16_t> out_565(4, 0);
    EXPECT_EQ(AImageDecoder_decodeImage(decoder, out_565.data(), stride_565,
                                        out_565.size() * sizeof(uint16_t)),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_NE(out_565[0], 0U);

    // Invalid small stride rejected cleanly
    EXPECT_EQ(AImageDecoder_decodeImage(decoder, out_565.data(), 2, 8),
              ANDROID_IMAGE_DECODER_BAD_PARAMETER);

    AImageDecoder_delete(decoder);
}

TEST(ImageDecoderTest, DecodesJpegFromFdAndAnimatedGifFrames)
{
    std::vector<uint8_t> src(8 * 8 * 4, 0);
    for (size_t i = 0; i < 64; ++i) {
        src[i * 4 + 0] = 240;
        src[i * 4 + 1] = 20;
        src[i * 4 + 2] = 20;
        src[i * 4 + 3] = 255;
    }
    auto jpeg = EncodeTestImage(GUID_ContainerFormatJpeg, 8, 8, src);
    ASSERT_FALSE(jpeg.empty());

    auto tmp_path = std::filesystem::temp_directory_path() / "portage_test_img.jpg";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char *>(jpeg.data()),
                  static_cast<std::streamsize>(jpeg.size()));
    }

    portage::fs::FileDescriptorTable fd_table;
    HANDLE hFile = CreateFileW(tmp_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
    int fd =
        fd_table.Allocate(hFile, 0, portage::fs::DeviceType::Regular, "/tmp/portage_test_img.jpg");
    ASSERT_GE(fd, 0);
    portage::syscall::SyscallDispatcher::Initialize(nullptr, nullptr, &fd_table, false);

    AImageDecoder *jpg_dec = nullptr;
    EXPECT_EQ(AImageDecoder_createFromFd(fd, &jpg_dec), ANDROID_IMAGE_DECODER_SUCCESS);
    ASSERT_NE(jpg_dec, nullptr);
    const auto *hdr = AImageDecoder_getHeaderInfo(jpg_dec);
    EXPECT_EQ(AImageDecoderHeaderInfo_getWidth(hdr), 8);
    EXPECT_EQ(AImageDecoderHeaderInfo_getHeight(hdr), 8);
    EXPECT_STREQ(AImageDecoderHeaderInfo_getMimeType(hdr), "image/jpeg");

    std::vector<uint8_t> jpg_pixels(8 * 8 * 4, 0);
    EXPECT_EQ(AImageDecoder_decodeImage(jpg_dec, jpg_pixels.data(), 32, jpg_pixels.size()),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_GT(jpg_pixels[0], 180);
    EXPECT_LT(jpg_pixels[2], 80);

    AImageDecoder_delete(jpg_dec);
    portage::syscall::SyscallDispatcher::Shutdown();
    fd_table.Close(fd);
    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);

    // Animated GIF multi-frame progression
    auto gif = BuildTwoFrameGif();
    AImageDecoder *gif_dec = nullptr;
    ASSERT_EQ(AImageDecoder_createFromBuffer(gif.data(), gif.size(), &gif_dec),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_TRUE(AImageDecoder_isAnimated(gif_dec));

    AImageDecoderFrameInfo *finfo = AImageDecoderFrameInfo_create();
    ASSERT_NE(finfo, nullptr);
    EXPECT_EQ(AImageDecoder_getFrameInfo(gif_dec, finfo), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_GE(AImageDecoderFrameInfo_getDuration(finfo), 100'000'000LL);
    ARect frect = AImageDecoderFrameInfo_getFrameRect(finfo);
    EXPECT_EQ(frect.right, 2);
    EXPECT_EQ(frect.bottom, 2);

    std::vector<uint8_t> gif_px(2 * 2 * 4, 0);
    EXPECT_EQ(AImageDecoder_decodeImage(gif_dec, gif_px.data(), 8, gif_px.size()),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_GT(gif_px[0], 200); // Frame 0 red

    EXPECT_EQ(AImageDecoder_advanceFrame(gif_dec), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(AImageDecoder_decodeImage(gif_dec, gif_px.data(), 8, gif_px.size()),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_GT(gif_px[2], 200); // Frame 1 blue

    EXPECT_EQ(AImageDecoder_advanceFrame(gif_dec), ANDROID_IMAGE_DECODER_FINISHED);
    EXPECT_EQ(AImageDecoder_rewind(gif_dec), ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_EQ(AImageDecoder_decodeImage(gif_dec, gif_px.data(), 8, gif_px.size()),
              ANDROID_IMAGE_DECODER_SUCCESS);
    EXPECT_GT(gif_px[0], 200);

    AImageDecoderFrameInfo_delete(finfo);
    AImageDecoder_delete(gif_dec);
}
