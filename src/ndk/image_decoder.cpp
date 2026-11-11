#include "ndk/image_decoder.hpp"
#include "fs/fd_table.hpp"
#include "media/wmf_decoder.hpp"
#include "syscall/syscall_dispatcher.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <propidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

struct AImageDecoderHeaderInfo {
    portage::ndk::DecodedImageHeader header{};
};

struct AImageDecoderFrameInfo {
    int64_t duration_ns{100'000'000LL};
    ARect frame_rect{};
};

struct AImageDecoder {
    std::vector<uint8_t> encoded_bytes{};
    AImageDecoderHeaderInfo header_info{};
    portage::ndk::ImageDecodeOptions options{};
    int32_t active_format{ANDROID_BITMAP_FORMAT_RGBA_8888};
    uint32_t current_frame{0};
    int32_t repeat_count{AIMAGE_DECODER_INFINITE_REPEATS};
    bool has_crop_configured{false};
};

namespace portage::ndk {

namespace {

int32_t BytesPerPixelForFormat(int32_t format)
{
    switch (format) {
    case ANDROID_BITMAP_FORMAT_RGB_565:
        return 2;
    case ANDROID_BITMAP_FORMAT_A_8:
        return 1;
    case ANDROID_BITMAP_FORMAT_RGBA_F16:
        return 8;
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
    case ANDROID_BITMAP_FORMAT_RGBA_1010102:
    default:
        return 4;
    }
}

std::string ContainerGuidToMime(const GUID &guid)
{
    if (guid == GUID_ContainerFormatPng) {
        return "image/png";
    }
    if (guid == GUID_ContainerFormatJpeg) {
        return "image/jpeg";
    }
    if (guid == GUID_ContainerFormatBmp) {
        return "image/bmp";
    }
    if (guid == GUID_ContainerFormatGif) {
        return "image/gif";
    }
    if (guid == GUID_ContainerFormatWebp) {
        return "image/webp";
    }
    if (guid == GUID_ContainerFormatTiff) {
        return "image/tiff";
    }
    if (guid == GUID_ContainerFormatIco) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

uint16_t ReadPropUint16(IWICMetadataQueryReader *reader, LPCWSTR query, uint16_t fallback)
{
    if (!reader) {
        return fallback;
    }
    PROPVARIANT var{};
    PropVariantInit(&var);
    uint16_t out = fallback;
    if (SUCCEEDED(reader->GetMetadataByName(query, &var))) {
        if (var.vt == VT_UI2) {
            out = var.uiVal;
        } else if (var.vt == VT_UI4) {
            out = static_cast<uint16_t>(var.ulVal);
        } else if (var.vt == VT_I4 && var.lVal >= 0) {
            out = static_cast<uint16_t>(var.lVal);
        }
    }
    PropVariantClear(&var);
    return out;
}

std::pair<uint32_t, uint32_t> EffectiveDimensions(const AImageDecoder *decoder)
{
    uint32_t w = decoder->header_info.header.width;
    uint32_t h = decoder->header_info.header.height;
    if (decoder->options.target_width > 0 && decoder->options.target_height > 0) {
        w = static_cast<uint32_t>(decoder->options.target_width);
        h = static_cast<uint32_t>(decoder->options.target_height);
    }
    if (decoder->has_crop_configured) {
        w = static_cast<uint32_t>(decoder->options.crop_rect.right -
                                  decoder->options.crop_rect.left);
        h = static_cast<uint32_t>(decoder->options.crop_rect.bottom -
                                  decoder->options.crop_rect.top);
    }
    return {w, h};
}

void ConvertRgbaPixels(std::span<const uint8_t> src_rgba, uint32_t width, uint32_t height,
                       int32_t target_format, bool unpremul, std::vector<uint8_t> &out_pixels,
                       uint32_t &out_stride)
{
    const uint32_t bpp = static_cast<uint32_t>(BytesPerPixelForFormat(target_format));
    out_stride = width * bpp;
    out_pixels.assign(static_cast<size_t>(out_stride) * height, 0);

    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
        uint8_t r = src_rgba[i * 4 + 0];
        uint8_t g = src_rgba[i * 4 + 1];
        uint8_t b = src_rgba[i * 4 + 2];
        uint8_t a = src_rgba[i * 4 + 3];

        if (!unpremul && a < 255 && target_format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
            r = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
            g = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
            b = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
        }

        switch (target_format) {
        case ANDROID_BITMAP_FORMAT_RGB_565: {
            uint16_t packed = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            std::memcpy(out_pixels.data() + i * 2, &packed, sizeof(uint16_t));
            break;
        }
        case ANDROID_BITMAP_FORMAT_A_8:
            out_pixels[i] = a;
            break;
        case ANDROID_BITMAP_FORMAT_RGBA_1010102: {
            uint32_t r10 = (static_cast<uint32_t>(r) * 1023U + 127U) / 255U;
            uint32_t g10 = (static_cast<uint32_t>(g) * 1023U + 127U) / 255U;
            uint32_t b10 = (static_cast<uint32_t>(b) * 1023U + 127U) / 255U;
            uint32_t a2 = (static_cast<uint32_t>(a) * 3U + 127U) / 255U;
            uint32_t packed = r10 | (g10 << 10) | (b10 << 20) | (a2 << 30);
            std::memcpy(out_pixels.data() + i * 4, &packed, sizeof(uint32_t));
            break;
        }
        case ANDROID_BITMAP_FORMAT_RGBA_8888:
        default:
            out_pixels[i * 4 + 0] = r;
            out_pixels[i * 4 + 1] = g;
            out_pixels[i * 4 + 2] = b;
            out_pixels[i * 4 + 3] = a;
            break;
        }
    }
}

int RunWicPipeline(std::span<const uint8_t> encoded, uint32_t frame_index, bool decode_pixels,
                   const ImageDecodeOptions &opts, std::vector<uint8_t> &out_pixels,
                   uint32_t &out_stride, DecodedImageHeader &out_header)
{
    if (encoded.empty()) {
        return ANDROID_IMAGE_DECODER_INVALID_INPUT;
    }

    portage::media::WmfManager::Instance().Initialize();
    int status = ANDROID_IMAGE_DECODER_INTERNAL_ERROR;

    portage::media::WmfManager::Instance().DispatchSync([&]() {
        IWICImagingFactory *factory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) {
            status = ANDROID_IMAGE_DECODER_INTERNAL_ERROR;
            return;
        }

        IWICStream *wic_stream = nullptr;
        IWICBitmapDecoder *decoder = nullptr;
        IWICBitmapFrameDecode *frame = nullptr;
        IWICBitmapScaler *scaler = nullptr;
        IWICBitmapClipper *clipper = nullptr;
        IWICFormatConverter *converter = nullptr;
        IWICMetadataQueryReader *container_meta = nullptr;

        auto cleanup = [&]() {
            if (container_meta)
                container_meta->Release();
            if (converter)
                converter->Release();
            if (clipper)
                clipper->Release();
            if (scaler)
                scaler->Release();
            if (frame)
                frame->Release();
            if (decoder)
                decoder->Release();
            if (wic_stream)
                wic_stream->Release();
            factory->Release();
        };

        hr = factory->CreateStream(&wic_stream);
        if (FAILED(hr) || !wic_stream) {
            status = ANDROID_IMAGE_DECODER_INTERNAL_ERROR;
            cleanup();
            return;
        }

        hr = wic_stream->InitializeFromMemory(const_cast<BYTE *>(encoded.data()),
                                              static_cast<DWORD>(encoded.size()));
        if (FAILED(hr)) {
            status = ANDROID_IMAGE_DECODER_INVALID_INPUT;
            cleanup();
            return;
        }

        hr = factory->CreateDecoderFromStream(wic_stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                              &decoder);
        if (FAILED(hr) || !decoder) {
            status = ANDROID_IMAGE_DECODER_UNSUPPORTED_FORMAT;
            cleanup();
            return;
        }

        GUID container_guid{};
        if (SUCCEEDED(decoder->GetContainerFormat(&container_guid))) {
            out_header.mime_type = ContainerGuidToMime(container_guid);
        }

        UINT frame_count = 0;
        if (FAILED(decoder->GetFrameCount(&frame_count)) || frame_count == 0) {
            status = ANDROID_IMAGE_DECODER_ERROR;
            cleanup();
            return;
        }

        out_header.frame_count = frame_count;
        out_header.is_animated = (frame_count > 1);
        out_header.bitmap_format = ANDROID_BITMAP_FORMAT_RGBA_8888;
        out_header.alpha_flags = ANDROID_BITMAP_FLAGS_ALPHA_PREMUL;
        out_header.data_space = ADATASPACE_SRGB;

        if (frame_index >= frame_count) {
            status = ANDROID_IMAGE_DECODER_FINISHED;
            cleanup();
            return;
        }

        hr = decoder->GetFrame(frame_index, &frame);
        if (FAILED(hr) || !frame) {
            status = ANDROID_IMAGE_DECODER_ERROR;
            cleanup();
            return;
        }

        UINT fwidth = 0;
        UINT fheight = 0;
        if (FAILED(frame->GetSize(&fwidth, &fheight)) || fwidth == 0 || fheight == 0) {
            status = ANDROID_IMAGE_DECODER_ERROR;
            cleanup();
            return;
        }

        uint32_t canvas_w = fwidth;
        uint32_t canvas_h = fheight;
        if (container_guid == GUID_ContainerFormatGif &&
            SUCCEEDED(decoder->GetMetadataQueryReader(&container_meta)) && container_meta) {
            uint16_t lw = ReadPropUint16(container_meta, L"/logscrdesc/Width", 0);
            uint16_t lh = ReadPropUint16(container_meta, L"/logscrdesc/Height", 0);
            if (lw > 0 && lh > 0) {
                canvas_w = lw;
                canvas_h = lh;
            }
        }
        out_header.width = canvas_w;
        out_header.height = canvas_h;

        if (!decode_pixels) {
            out_header.frame_durations_ns.resize(frame_count, 100'000'000LL);
            out_header.frame_rects.resize(frame_count, ARect{0, 0, static_cast<int32_t>(canvas_w),
                                                             static_cast<int32_t>(canvas_h)});
            for (UINT fi = 0; fi < frame_count; ++fi) {
                IWICBitmapFrameDecode *meta_frame = nullptr;
                IWICMetadataQueryReader *fmeta = nullptr;
                if (SUCCEEDED(decoder->GetFrame(fi, &meta_frame)) && meta_frame) {
                    if (SUCCEEDED(meta_frame->GetMetadataQueryReader(&fmeta)) && fmeta) {
                        uint16_t delay_cs = ReadPropUint16(fmeta, L"/grctlext/Delay", 10);
                        if (delay_cs == 0) {
                            delay_cs = 10;
                        }
                        out_header.frame_durations_ns[fi] =
                            static_cast<int64_t>(delay_cs) * 10'000'000LL;
                        uint16_t left = ReadPropUint16(fmeta, L"/imgdesc/Left", 0);
                        uint16_t top = ReadPropUint16(fmeta, L"/imgdesc/Top", 0);
                        uint16_t fw = ReadPropUint16(fmeta, L"/imgdesc/Width",
                                                     static_cast<uint16_t>(canvas_w));
                        uint16_t fh = ReadPropUint16(fmeta, L"/imgdesc/Height",
                                                     static_cast<uint16_t>(canvas_h));
                        out_header.frame_rects[fi] = ARect{left, top, left + fw, top + fh};
                        fmeta->Release();
                    }
                    meta_frame->Release();
                }
            }
            status = ANDROID_IMAGE_DECODER_SUCCESS;
            cleanup();
            return;
        }

        IWICBitmapSource *source = frame;
        UINT src_w = fwidth;
        UINT src_h = fheight;

        if (opts.target_width > 0 && opts.target_height > 0 &&
            (static_cast<UINT>(opts.target_width) != src_w ||
             static_cast<UINT>(opts.target_height) != src_h)) {
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && scaler &&
                SUCCEEDED(scaler->Initialize(source, static_cast<UINT>(opts.target_width),
                                             static_cast<UINT>(opts.target_height),
                                             WICBitmapInterpolationModeFant))) {
                source = scaler;
                src_w = static_cast<UINT>(opts.target_width);
                src_h = static_cast<UINT>(opts.target_height);
            } else {
                status = ANDROID_IMAGE_DECODER_INVALID_SCALE;
                cleanup();
                return;
            }
        }

        if (opts.has_crop) {
            if (opts.crop_rect.left < 0 || opts.crop_rect.top < 0 ||
                opts.crop_rect.right <= opts.crop_rect.left ||
                opts.crop_rect.bottom <= opts.crop_rect.top ||
                static_cast<UINT>(opts.crop_rect.right) > src_w ||
                static_cast<UINT>(opts.crop_rect.bottom) > src_h) {
                status = ANDROID_IMAGE_DECODER_BAD_PARAMETER;
                cleanup();
                return;
            }
            WICRect wic_crop{};
            wic_crop.X = opts.crop_rect.left;
            wic_crop.Y = opts.crop_rect.top;
            wic_crop.Width = opts.crop_rect.right - opts.crop_rect.left;
            wic_crop.Height = opts.crop_rect.bottom - opts.crop_rect.top;
            if (SUCCEEDED(factory->CreateBitmapClipper(&clipper)) && clipper &&
                SUCCEEDED(clipper->Initialize(source, &wic_crop))) {
                source = clipper;
                src_w = static_cast<UINT>(wic_crop.Width);
                src_h = static_cast<UINT>(wic_crop.Height);
            } else {
                status = ANDROID_IMAGE_DECODER_BAD_PARAMETER;
                cleanup();
                return;
            }
        }

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr) || !converter) {
            status = ANDROID_IMAGE_DECODER_INTERNAL_ERROR;
            cleanup();
            return;
        }

        hr = converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) {
            status = ANDROID_IMAGE_DECODER_INVALID_CONVERSION;
            cleanup();
            return;
        }

        std::vector<uint8_t> raw_rgba(static_cast<size_t>(src_w) * src_h * 4, 0);
        hr = converter->CopyPixels(nullptr, src_w * 4, static_cast<UINT>(raw_rgba.size()),
                                   raw_rgba.data());
        if (FAILED(hr)) {
            status = ANDROID_IMAGE_DECODER_ERROR;
            cleanup();
            return;
        }

        int32_t fmt =
            (opts.android_format != 0) ? opts.android_format : ANDROID_BITMAP_FORMAT_RGBA_8888;
        if (fmt == ANDROID_BITMAP_FORMAT_RGBA_F16) {
            IWICFormatConverter *f16_conv = nullptr;
            if (SUCCEEDED(factory->CreateFormatConverter(&f16_conv)) && f16_conv &&
                SUCCEEDED(f16_conv->Initialize(source, GUID_WICPixelFormat64bppRGBAHalf,
                                               WICBitmapDitherTypeNone, nullptr, 0.0f,
                                               WICBitmapPaletteTypeMedianCut))) {
                out_stride = src_w * 8;
                out_pixels.resize(static_cast<size_t>(out_stride) * src_h);
                if (SUCCEEDED(f16_conv->CopyPixels(nullptr, out_stride,
                                                   static_cast<UINT>(out_pixels.size()),
                                                   out_pixels.data()))) {
                    f16_conv->Release();
                    status = ANDROID_IMAGE_DECODER_SUCCESS;
                    cleanup();
                    return;
                }
            }
            if (f16_conv) {
                f16_conv->Release();
            }
        }

        ConvertRgbaPixels(raw_rgba, src_w, src_h, fmt, opts.unpremul, out_pixels, out_stride);
        status = ANDROID_IMAGE_DECODER_SUCCESS;
        cleanup();
    });

    return status;
}

} // namespace

int WicImageCodec::InspectImage(std::span<const uint8_t> encoded, DecodedImageHeader &out_header)
{
    std::vector<uint8_t> unused_pixels;
    uint32_t unused_stride = 0;
    ImageDecodeOptions opts{};
    return RunWicPipeline(encoded, 0, false, opts, unused_pixels, unused_stride, out_header);
}

int WicImageCodec::DecodeImageFrame(std::span<const uint8_t> encoded, uint32_t frame_index,
                                    const ImageDecodeOptions &opts,
                                    std::vector<uint8_t> &out_pixels, uint32_t &out_stride,
                                    DecodedImageHeader &out_header)
{
    return RunWicPipeline(encoded, frame_index, true, opts, out_pixels, out_stride, out_header);
}

} // namespace portage::ndk

extern "C" {

__attribute__((sysv_abi)) int AImageDecoder_createFromBuffer(const void *buffer, size_t length,
                                                             AImageDecoder **outDecoder)
{
    if (!buffer || length == 0 || !outDecoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    *outDecoder = nullptr;

    auto dec = std::make_unique<AImageDecoder>();
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    dec->encoded_bytes.assign(bytes, bytes + length);

    int rc = portage::ndk::WicImageCodec::InspectImage(dec->encoded_bytes, dec->header_info.header);
    if (rc != ANDROID_IMAGE_DECODER_SUCCESS) {
        return rc;
    }
    dec->active_format = dec->header_info.header.bitmap_format;
    *outDecoder = dec.release();
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_createFromAAsset(portage::package::AAsset *asset,
                                                             AImageDecoder **outDecoder)
{
    if (!asset || !outDecoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    *outDecoder = nullptr;

    const void *buf = portage::package::AssetManagerBridge::Sysv_AAsset_getBuffer(asset);
    int64_t len = portage::package::AssetManagerBridge::Sysv_AAsset_getLength(asset);
    if (buf && len > 0) {
        return AImageDecoder_createFromBuffer(buf, static_cast<size_t>(len), outDecoder);
    }

    if (len <= 0) {
        return ANDROID_IMAGE_DECODER_INVALID_INPUT;
    }
    std::vector<uint8_t> data(static_cast<size_t>(len));
    portage::package::AssetManagerBridge::Sysv_AAsset_seek(asset, 0, SEEK_SET);
    int read_bytes =
        portage::package::AssetManagerBridge::Sysv_AAsset_read(asset, data.data(), data.size());
    if (read_bytes <= 0) {
        return ANDROID_IMAGE_DECODER_INVALID_INPUT;
    }
    return AImageDecoder_createFromBuffer(data.data(), static_cast<size_t>(read_bytes), outDecoder);
}

__attribute__((sysv_abi)) int AImageDecoder_createFromFd(int fd, AImageDecoder **outDecoder)
{
    if (fd < 0 || !outDecoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    *outDecoder = nullptr;

    auto *fd_table = portage::syscall::SyscallDispatcher::GetFdTable();
    auto desc = fd_table ? fd_table->Get(fd) : nullptr;
    if (!desc) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }

    std::vector<uint8_t> data;
    if (!desc->synthetic_data.empty()) {
        data.assign(desc->synthetic_data.begin(), desc->synthetic_data.end());
    } else if (desc->handle != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER orig_pos{};
        LARGE_INTEGER zero{};
        LARGE_INTEGER file_sz{};
        if (!SetFilePointerEx(desc->handle, zero, &orig_pos, FILE_CURRENT) ||
            !GetFileSizeEx(desc->handle, &file_sz) || file_sz.QuadPart <= 0) {
            return ANDROID_IMAGE_DECODER_SEEK_ERROR;
        }
        if (orig_pos.QuadPart >= file_sz.QuadPart) {
            SetFilePointerEx(desc->handle, zero, nullptr, FILE_BEGIN);
            orig_pos.QuadPart = 0;
        }
        size_t to_read = static_cast<size_t>(file_sz.QuadPart - orig_pos.QuadPart);
        data.resize(to_read);
        size_t total = 0;
        while (total < to_read) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(to_read - total, 1 << 20));
            DWORD got = 0;
            if (!ReadFile(desc->handle, data.data() + total, chunk, &got, nullptr) || got == 0) {
                break;
            }
            total += got;
        }
        data.resize(total);
        SetFilePointerEx(desc->handle, orig_pos, nullptr, FILE_BEGIN);
    }

    if (data.empty()) {
        return ANDROID_IMAGE_DECODER_INVALID_INPUT;
    }
    return AImageDecoder_createFromBuffer(data.data(), data.size(), outDecoder);
}

__attribute__((sysv_abi)) void AImageDecoder_delete(AImageDecoder *decoder) { delete decoder; }

__attribute__((sysv_abi)) const AImageDecoderHeaderInfo *
AImageDecoder_getHeaderInfo(const AImageDecoder *decoder)
{
    return decoder ? &decoder->header_info : nullptr;
}

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getWidth(const AImageDecoderHeaderInfo *info)
{
    return info ? static_cast<int32_t>(info->header.width) : 0;
}

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getHeight(const AImageDecoderHeaderInfo *info)
{
    return info ? static_cast<int32_t>(info->header.height) : 0;
}

__attribute__((sysv_abi)) const char *
AImageDecoderHeaderInfo_getMimeType(const AImageDecoderHeaderInfo *info)
{
    return info ? info->header.mime_type.c_str() : "";
}

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getDataSpace(const AImageDecoderHeaderInfo *info)
{
    return info ? info->header.data_space : ADATASPACE_UNKNOWN;
}

__attribute__((sysv_abi)) int32_t
AImageDecoderHeaderInfo_getAndroidBitmapFormat(const AImageDecoderHeaderInfo *info)
{
    return info ? info->header.bitmap_format : ANDROID_BITMAP_FORMAT_NONE;
}

__attribute__((sysv_abi)) int
AImageDecoderHeaderInfo_getAlphaFlags(const AImageDecoderHeaderInfo *info)
{
    return info ? static_cast<int>(info->header.alpha_flags) : ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE;
}

__attribute__((sysv_abi)) int AImageDecoder_setAndroidBitmapFormat(AImageDecoder *decoder,
                                                                   int32_t format)
{
    if (!decoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    switch (format) {
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
    case ANDROID_BITMAP_FORMAT_RGB_565:
    case ANDROID_BITMAP_FORMAT_A_8:
    case ANDROID_BITMAP_FORMAT_RGBA_F16:
    case ANDROID_BITMAP_FORMAT_RGBA_1010102:
        decoder->active_format = format;
        decoder->options.android_format = format;
        return ANDROID_IMAGE_DECODER_SUCCESS;
    default:
        return ANDROID_IMAGE_DECODER_INVALID_CONVERSION;
    }
}

__attribute__((sysv_abi)) int AImageDecoder_setUnpremultipliedRequired(AImageDecoder *decoder,
                                                                       bool required)
{
    if (!decoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    decoder->options.unpremul = required;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_setDataSpace(AImageDecoder *decoder, int32_t dataspace)
{
    if (!decoder || dataspace == ADATASPACE_UNKNOWN) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    decoder->header_info.header.data_space = dataspace;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_setTargetSize(AImageDecoder *decoder, int32_t width,
                                                          int32_t height)
{
    if (!decoder || width <= 0 || height <= 0) {
        return ANDROID_IMAGE_DECODER_INVALID_SCALE;
    }
    decoder->options.target_width = width;
    decoder->options.target_height = height;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_setCrop(AImageDecoder *decoder, ARect crop)
{
    if (!decoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    if (crop.left == 0 && crop.top == 0 && crop.right == 0 && crop.bottom == 0) {
        decoder->has_crop_configured = false;
        decoder->options.has_crop = false;
        decoder->options.crop_rect = ARect{0, 0, 0, 0};
        return ANDROID_IMAGE_DECODER_SUCCESS;
    }
    int32_t max_w = (decoder->options.target_width > 0)
                        ? decoder->options.target_width
                        : static_cast<int32_t>(decoder->header_info.header.width);
    int32_t max_h = (decoder->options.target_height > 0)
                        ? decoder->options.target_height
                        : static_cast<int32_t>(decoder->header_info.header.height);
    if (crop.left < 0 || crop.top < 0 || crop.right <= crop.left || crop.bottom <= crop.top ||
        crop.right > max_w || crop.bottom > max_h) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    decoder->has_crop_configured = true;
    decoder->options.has_crop = true;
    decoder->options.crop_rect = crop;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_computeSampledSize(const AImageDecoder *decoder,
                                                               int sampleSize, int32_t *width,
                                                               int32_t *height)
{
    if (!decoder || sampleSize < 1 || !width || !height) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    *width = std::max(1, static_cast<int32_t>(decoder->header_info.header.width) / sampleSize);
    *height = std::max(1, static_cast<int32_t>(decoder->header_info.header.height) / sampleSize);
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) size_t AImageDecoder_getMinimumStride(const AImageDecoder *decoder)
{
    if (!decoder) {
        return 0;
    }
    auto [w, h] = portage::ndk::EffectiveDimensions(decoder);
    (void)h;
    int32_t bpp = portage::ndk::BytesPerPixelForFormat(decoder->active_format);
    return static_cast<size_t>(w) * static_cast<size_t>(bpp);
}

__attribute__((sysv_abi)) int AImageDecoder_decodeImage(AImageDecoder *decoder, void *pixels,
                                                        size_t stride, size_t size)
{
    if (!decoder || !pixels) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    auto [w, h] = portage::ndk::EffectiveDimensions(decoder);
    size_t min_stride = AImageDecoder_getMinimumStride(decoder);
    if (stride < min_stride || h == 0 || size < stride * h) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    if (decoder->current_frame >= decoder->header_info.header.frame_count) {
        return ANDROID_IMAGE_DECODER_FINISHED;
    }

    std::vector<uint8_t> decoded_pixels;
    uint32_t decoded_stride = 0;
    portage::ndk::DecodedImageHeader tmp_hdr = decoder->header_info.header;
    int rc = portage::ndk::WicImageCodec::DecodeImageFrame(decoder->encoded_bytes,
                                                           decoder->current_frame, decoder->options,
                                                           decoded_pixels, decoded_stride, tmp_hdr);
    if (rc != ANDROID_IMAGE_DECODER_SUCCESS) {
        return rc;
    }

    uint8_t *dst = static_cast<uint8_t *>(pixels);
    const size_t row_copy = std::min<size_t>(min_stride, decoded_stride);
    for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * stride,
                    decoded_pixels.data() + static_cast<size_t>(row) * decoded_stride, row_copy);
    }
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) const char *AImageDecoder_resultToString(int result)
{
    switch (result) {
    case ANDROID_IMAGE_DECODER_SUCCESS:
        return "ANDROID_IMAGE_DECODER_SUCCESS";
    case ANDROID_IMAGE_DECODER_INCOMPLETE:
        return "ANDROID_IMAGE_DECODER_INCOMPLETE";
    case ANDROID_IMAGE_DECODER_ERROR:
        return "ANDROID_IMAGE_DECODER_ERROR";
    case ANDROID_IMAGE_DECODER_INVALID_CONVERSION:
        return "ANDROID_IMAGE_DECODER_INVALID_CONVERSION";
    case ANDROID_IMAGE_DECODER_INVALID_SCALE:
        return "ANDROID_IMAGE_DECODER_INVALID_SCALE";
    case ANDROID_IMAGE_DECODER_BAD_PARAMETER:
        return "ANDROID_IMAGE_DECODER_BAD_PARAMETER";
    case ANDROID_IMAGE_DECODER_INVALID_INPUT:
        return "ANDROID_IMAGE_DECODER_INVALID_INPUT";
    case ANDROID_IMAGE_DECODER_SEEK_ERROR:
        return "ANDROID_IMAGE_DECODER_SEEK_ERROR";
    case ANDROID_IMAGE_DECODER_INTERNAL_ERROR:
        return "ANDROID_IMAGE_DECODER_INTERNAL_ERROR";
    case ANDROID_IMAGE_DECODER_UNSUPPORTED_FORMAT:
        return "ANDROID_IMAGE_DECODER_UNSUPPORTED_FORMAT";
    case ANDROID_IMAGE_DECODER_FINISHED:
        return "ANDROID_IMAGE_DECODER_FINISHED";
    case ANDROID_IMAGE_DECODER_INVALID_STATE:
        return "ANDROID_IMAGE_DECODER_INVALID_STATE";
    default:
        return "UNKNOWN";
    }
}

__attribute__((sysv_abi)) bool AImageDecoder_isAnimated(const AImageDecoder *decoder)
{
    return decoder ? decoder->header_info.header.is_animated : false;
}

__attribute__((sysv_abi)) int AImageDecoder_setRepeatCount(AImageDecoder *decoder,
                                                           int32_t repeatCount)
{
    if (!decoder || repeatCount < AIMAGE_DECODER_INFINITE_REPEATS) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    decoder->repeat_count = repeatCount;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int32_t AImageDecoder_getRepeatCount(const AImageDecoder *decoder)
{
    return decoder ? decoder->repeat_count : AIMAGE_DECODER_INFINITE_REPEATS;
}

__attribute__((sysv_abi)) AImageDecoderFrameInfo *AImageDecoderFrameInfo_create(void)
{
    return new AImageDecoderFrameInfo();
}

__attribute__((sysv_abi)) void AImageDecoderFrameInfo_delete(AImageDecoderFrameInfo *info)
{
    delete info;
}

__attribute__((sysv_abi)) int AImageDecoder_getFrameInfo(const AImageDecoder *decoder,
                                                         AImageDecoderFrameInfo *info)
{
    if (!decoder || !info) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    if (decoder->current_frame >= decoder->header_info.header.frame_count) {
        return ANDROID_IMAGE_DECODER_FINISHED;
    }
    const auto &hdr = decoder->header_info.header;
    info->duration_ns = (decoder->current_frame < hdr.frame_durations_ns.size())
                            ? hdr.frame_durations_ns[decoder->current_frame]
                            : 100'000'000LL;
    info->frame_rect =
        (decoder->current_frame < hdr.frame_rects.size())
            ? hdr.frame_rects[decoder->current_frame]
            : ARect{0, 0, static_cast<int32_t>(hdr.width), static_cast<int32_t>(hdr.height)};
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int64_t
AImageDecoderFrameInfo_getDuration(const AImageDecoderFrameInfo *info)
{
    return info ? info->duration_ns : -1;
}

__attribute__((sysv_abi)) ARect
AImageDecoderFrameInfo_getFrameRect(const AImageDecoderFrameInfo *info)
{
    return info ? info->frame_rect : ARect{0, 0, 0, 0};
}

__attribute__((sysv_abi)) int AImageDecoder_advanceFrame(AImageDecoder *decoder)
{
    if (!decoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    if (!decoder->header_info.header.is_animated) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    if (decoder->current_frame + 1 >= decoder->header_info.header.frame_count) {
        decoder->current_frame = decoder->header_info.header.frame_count;
        return ANDROID_IMAGE_DECODER_FINISHED;
    }
    decoder->current_frame++;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

__attribute__((sysv_abi)) int AImageDecoder_rewind(AImageDecoder *decoder)
{
    if (!decoder) {
        return ANDROID_IMAGE_DECODER_BAD_PARAMETER;
    }
    decoder->current_frame = 0;
    return ANDROID_IMAGE_DECODER_SUCCESS;
}

} // extern "C"
