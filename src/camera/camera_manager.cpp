#include "camera/camera_manager.hpp"

#include "media/wmf_decoder.hpp"
#include "media/wmf_transform.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace portage::camera {

namespace {

std::string WideToUtf8(const WCHAR *wstr)
{
    if (!wstr) {
        return {};
    }
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(utf8_len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), utf8_len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string &str)
{
    if (str.empty()) {
        return {};
    }
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (wide_len <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(wide_len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, out.data(), wide_len);
    return out;
}

inline uint8_t ClampByte(int value) { return static_cast<uint8_t>(std::clamp(value, 0, 255)); }

inline void YuvPixelToRgba(int y, int u, int v, uint8_t *dst)
{
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;
    dst[0] = ClampByte((298 * c + 409 * e + 128) >> 8);
    dst[1] = ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
    dst[2] = ClampByte((298 * c + 516 * d + 128) >> 8);
    dst[3] = 255;
}

} // namespace

ImageReaderImpl::ImageReaderImpl(int32_t width, int32_t height, int32_t format, int32_t max_images)
    : width_(width), height_(height), format_(format), max_images_(std::max(1, max_images)),
      window_(std::make_unique<ANativeWindow>())
{
    window_->width = width;
    window_->height = height;
    window_->format = format;
    CameraManager::Instance().RegisterImageReader(window_.get(), this);
}

ImageReaderImpl::~ImageReaderImpl()
{
    CameraManager::Instance().UnregisterImageReader(window_.get());
}

ANativeWindow *ImageReaderImpl::GetNativeWindow() { return window_.get(); }

void ImageReaderImpl::SetListener(AImageReader_ImageListener *listener)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (listener) {
        listener_ = *listener;
    } else {
        listener_ = {nullptr, nullptr};
    }
}

void ImageReaderImpl::PushFrame(int32_t width, int32_t height, int32_t format, int64_t timestamp,
                                const std::vector<ImagePlaneBuffer> &planes)
{
    AImageReader_ImageListener listener_copy{nullptr, nullptr};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (static_cast<int32_t>(available_images_.size() + acquired_images_.size()) >=
               max_images_) {
            if (!available_images_.empty()) {
                available_images_.pop_front();
            } else {
                break;
            }
        }

        auto img = std::make_unique<InternalImage>();
        img->width = width;
        img->height = height;
        img->format = format;
        img->timestamp = timestamp;
        img->planes = planes;
        img->acquired = false;

        available_images_.push_back(std::move(img));
        listener_copy = listener_;
    }

    if (listener_copy.onImageAvailable) {
        AImageReader reader_wrap;
        reader_wrap.impl = nullptr;
        listener_copy.onImageAvailable(listener_copy.context,
                                       reinterpret_cast<AImageReader *>(this));
    }
}

media_status_t ImageReaderImpl::AcquireNextImage(AImage **out_image)
{
    if (!out_image) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *out_image = nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    if (available_images_.empty()) {
        return AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
    }

    auto img = std::move(available_images_.front());
    available_images_.pop_front();
    img->acquired = true;

    auto *raw_ptr = img.get();
    acquired_images_.push_back(std::move(img));

    auto *wrapper = new AImage();
    wrapper->internal_image = raw_ptr;
    wrapper->owner = this;
    *out_image = wrapper;
    return AMEDIA_OK;
}

media_status_t ImageReaderImpl::AcquireLatestImage(AImage **out_image)
{
    if (!out_image) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *out_image = nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    if (available_images_.empty()) {
        return AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
    }

    while (available_images_.size() > 1) {
        available_images_.pop_front();
    }

    auto img = std::move(available_images_.front());
    available_images_.pop_front();
    img->acquired = true;

    auto *raw_ptr = img.get();
    acquired_images_.push_back(std::move(img));

    auto *wrapper = new AImage();
    wrapper->internal_image = raw_ptr;
    wrapper->owner = this;
    *out_image = wrapper;
    return AMEDIA_OK;
}

void ImageReaderImpl::ReleaseImage(AImage *image)
{
    if (!image) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = acquired_images_.begin(); it != acquired_images_.end(); ++it) {
        if (it->get() == image->internal_image) {
            acquired_images_.erase(it);
            break;
        }
    }
    delete image;
}

void ConvertYuy2ToRgba(const uint8_t *yuy2, int32_t stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba)
{
    out_rgba.clear();
    if (!yuy2 || width <= 0 || height <= 0) {
        return;
    }

    const int32_t abs_stride = (stride != 0) ? std::abs(stride) : width * 2;
    out_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    for (int32_t y = 0; y < height; ++y) {
        const uint8_t *src_row = yuy2 + static_cast<size_t>(y) * static_cast<size_t>(abs_stride);
        uint8_t *dst_row =
            out_rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
        for (int32_t x = 0; x + 1 < width; x += 2) {
            const size_t pair_off = static_cast<size_t>(x) * 2u;
            const int y0 = src_row[pair_off + 0];
            const int u = src_row[pair_off + 1];
            const int y1 = src_row[pair_off + 2];
            const int v = src_row[pair_off + 3];
            YuvPixelToRgba(y0, u, v, dst_row + static_cast<size_t>(x) * 4u);
            YuvPixelToRgba(y1, u, v, dst_row + static_cast<size_t>(x + 1) * 4u);
        }
        if ((width & 1) != 0) {
            const int32_t x = width - 1;
            const size_t pair_off = static_cast<size_t>(x) * 2u;
            YuvPixelToRgba(src_row[pair_off + 0], src_row[pair_off + 1], src_row[pair_off + 3],
                           dst_row + static_cast<size_t>(x) * 4u);
        }
    }
}

void ConvertBgraToRgba(const uint8_t *bgra, int32_t stride, int32_t width, int32_t height,
                       std::vector<uint8_t> &out_rgba, bool force_opaque_alpha)
{
    out_rgba.clear();
    if (!bgra || width <= 0 || height <= 0) {
        return;
    }

    const int32_t abs_stride = (stride != 0) ? std::abs(stride) : width * 4;
    const bool bottom_up = (stride < 0);
    out_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

    for (int32_t y = 0; y < height; ++y) {
        const int32_t src_y = bottom_up ? (height - 1 - y) : y;
        const uint8_t *src_row =
            bgra + static_cast<size_t>(src_y) * static_cast<size_t>(abs_stride);
        uint8_t *dst_row =
            out_rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
        for (int32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(x) * 4u;
            dst_row[idx + 0] = src_row[idx + 2];
            dst_row[idx + 1] = src_row[idx + 1];
            dst_row[idx + 2] = src_row[idx + 0];
            dst_row[idx + 3] = force_opaque_alpha ? 255 : src_row[idx + 3];
        }
    }
}

void ScaleRgbaFrame(const uint8_t *src_rgba, int32_t src_w, int32_t src_h, int32_t dst_w,
                    int32_t dst_h, std::vector<uint8_t> &out_rgba)
{
    out_rgba.clear();
    if (!src_rgba || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    const size_t expected_bytes = static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 4u;
    if (src_w == dst_w && src_h == dst_h) {
        out_rgba.assign(src_rgba, src_rgba + expected_bytes);
        return;
    }

    out_rgba.resize(expected_bytes);
    for (int32_t dy = 0; dy < dst_h; ++dy) {
        const int32_t sy = static_cast<int32_t>((static_cast<int64_t>(dy) * src_h) / dst_h);
        const uint8_t *src_row =
            src_rgba + static_cast<size_t>(sy) * static_cast<size_t>(src_w) * 4u;
        uint8_t *dst_row =
            out_rgba.data() + static_cast<size_t>(dy) * static_cast<size_t>(dst_w) * 4u;
        for (int32_t dx = 0; dx < dst_w; ++dx) {
            const int32_t sx = static_cast<int32_t>((static_cast<int64_t>(dx) * src_w) / dst_w);
            std::memcpy(dst_row + static_cast<size_t>(dx) * 4u,
                        src_row + static_cast<size_t>(sx) * 4u, 4);
        }
    }
}

void ExtractYuv420PlanesFromRgba(const uint8_t *rgba, int32_t width, int32_t height,
                                 std::vector<ImagePlaneBuffer> &out_planes)
{
    out_planes.clear();
    if (!rgba || width <= 0 || height <= 0) {
        return;
    }

    const int32_t cw = (width + 1) / 2;
    const int32_t ch = (height + 1) / 2;

    ImagePlaneBuffer y_plane;
    y_plane.width = width;
    y_plane.height = height;
    y_plane.row_stride = width;
    y_plane.pixel_stride = 1;
    y_plane.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    ImagePlaneBuffer u_plane;
    u_plane.width = cw;
    u_plane.height = ch;
    u_plane.row_stride = cw;
    u_plane.pixel_stride = 1;
    u_plane.data.resize(static_cast<size_t>(cw) * static_cast<size_t>(ch));

    ImagePlaneBuffer v_plane;
    v_plane.width = cw;
    v_plane.height = ch;
    v_plane.row_stride = cw;
    v_plane.pixel_stride = 1;
    v_plane.data.resize(static_cast<size_t>(cw) * static_cast<size_t>(ch));

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const size_t px_off =
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            const int r = rgba[px_off + 0];
            const int g = rgba[px_off + 1];
            const int b = rgba[px_off + 2];

            y_plane.data[static_cast<size_t>(y) * static_cast<size_t>(width) +
                         static_cast<size_t>(x)] =
                ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);

            if ((y % 2) == 0 && (x % 2) == 0) {
                const size_t c_idx = static_cast<size_t>(y / 2) * static_cast<size_t>(cw) +
                                     static_cast<size_t>(x / 2);
                u_plane.data[c_idx] = ClampByte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                v_plane.data[c_idx] = ClampByte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
            }
        }
    }

    out_planes.push_back(std::move(y_plane));
    out_planes.push_back(std::move(u_plane));
    out_planes.push_back(std::move(v_plane));
}

bool EncodeRgbaToJpeg(const uint8_t *rgba, int32_t width, int32_t height,
                      std::vector<uint8_t> &out_jpeg, int quality_percent)
{
    out_jpeg.clear();
    if (!rgba || width <= 0 || height <= 0) {
        return false;
    }

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> bgr(pixel_count * 3u);
    for (size_t i = 0; i < pixel_count; ++i) {
        bgr[i * 3u + 0] = rgba[i * 4u + 2];
        bgr[i * 3u + 1] = rgba[i * 4u + 1];
        bgr[i * 3u + 2] = rgba[i * 4u + 0];
    }

    portage::media::WmfManager::Instance().Initialize();
    bool encoded = false;
    const float quality = static_cast<float>(std::clamp(quality_percent, 1, 100)) / 100.0f;

    portage::media::WmfManager::Instance().DispatchSync([&]() {
        IWICImagingFactory *factory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) {
            return;
        }

        IStream *mem_stream = nullptr;
        IWICBitmapEncoder *encoder = nullptr;
        IWICBitmapFrameEncode *frame = nullptr;
        IPropertyBag2 *prop_bag = nullptr;

        hr = CreateStreamOnHGlobal(nullptr, TRUE, &mem_stream);
        if (SUCCEEDED(hr) && mem_stream) {
            hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
        }
        if (SUCCEEDED(hr) && encoder) {
            hr = encoder->Initialize(mem_stream, WICBitmapEncoderNoCache);
        }
        if (SUCCEEDED(hr) && encoder) {
            hr = encoder->CreateNewFrame(&frame, &prop_bag);
        }
        if (SUCCEEDED(hr) && frame) {
            if (prop_bag) {
                wchar_t prop_name[] = L"ImageQuality";
                PROPBAG2 option{};
                option.pstrName = prop_name;
                VARIANT val{};
                val.vt = VT_R4;
                val.fltVal = quality;
                prop_bag->Write(1, &option, &val);
            }
            hr = frame->Initialize(prop_bag);
        }
        if (SUCCEEDED(hr) && frame) {
            hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
        }
        if (SUCCEEDED(hr) && frame) {
            WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
            hr = frame->SetPixelFormat(&pf);
        }
        if (SUCCEEDED(hr) && frame) {
            const UINT row_bytes = static_cast<UINT>(width) * 3u;
            hr = frame->WritePixels(static_cast<UINT>(height), row_bytes,
                                    static_cast<UINT>(bgr.size()), bgr.data());
        }
        if (SUCCEEDED(hr) && frame) {
            hr = frame->Commit();
        }
        if (SUCCEEDED(hr) && encoder) {
            hr = encoder->Commit();
        }
        if (SUCCEEDED(hr) && mem_stream) {
            HGLOBAL hglobal = nullptr;
            if (SUCCEEDED(GetHGlobalFromStream(mem_stream, &hglobal)) && hglobal) {
                const SIZE_T byte_size = GlobalSize(hglobal);
                const void *locked = GlobalLock(hglobal);
                if (locked && byte_size > 0) {
                    const auto *bytes = static_cast<const uint8_t *>(locked);
                    out_jpeg.assign(bytes, bytes + byte_size);
                    encoded = true;
                }
                if (locked) {
                    GlobalUnlock(hglobal);
                }
            }
        }

        if (prop_bag) {
            prop_bag->Release();
        }
        if (frame) {
            frame->Release();
        }
        if (encoder) {
            encoder->Release();
        }
        if (mem_stream) {
            mem_stream->Release();
        }
        factory->Release();
    });

    return encoded;
}

namespace {

void GenerateSyntheticRgba(int32_t width, int32_t height, int64_t frame_index,
                           std::vector<uint8_t> &out_rgba)
{
    out_rgba.clear();
    if (width <= 0 || height <= 0) {
        return;
    }

    static constexpr uint8_t kColorBars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
    };

    out_rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    const int bar_width = std::max(1, width / 8);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const int bar_idx = std::min(7, x / bar_width);
            const size_t offset =
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            out_rgba[offset + 0] = kColorBars[bar_idx][0];
            out_rgba[offset + 1] = kColorBars[bar_idx][1];
            out_rgba[offset + 2] = kColorBars[bar_idx][2];
            out_rgba[offset + 3] = 255;
        }
    }

    const int box_size = 32;
    const int box_x = static_cast<int>((frame_index * 8) % std::max(1, width - box_size));
    const int box_y = static_cast<int>((frame_index * 4) % std::max(1, height - box_size));
    for (int cy = box_y; cy < std::min<int>(height, box_y + box_size); ++cy) {
        for (int cx = box_x; cx < std::min<int>(width, box_x + box_size); ++cx) {
            const size_t offset =
                (static_cast<size_t>(cy) * static_cast<size_t>(width) + static_cast<size_t>(cx)) *
                4u;
            out_rgba[offset + 0] = 255;
            out_rgba[offset + 1] = 255;
            out_rgba[offset + 2] = 255;
            out_rgba[offset + 3] = 255;
        }
    }
}

bool WriteRgbaToNativeWindow(ANativeWindow *win, const std::vector<uint8_t> &rgba, int32_t src_w,
                             int32_t src_h)
{
    if (!win || rgba.empty() || src_w <= 0 || src_h <= 0) {
        return false;
    }

    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(win, &buf, nullptr) != 0 || !buf.bits || buf.width <= 0 ||
        buf.height <= 0) {
        return false;
    }

    const std::vector<uint8_t> *src_ptr = &rgba;
    std::vector<uint8_t> scaled_rgba;
    if (buf.width != src_w || buf.height != src_h) {
        ScaleRgbaFrame(rgba.data(), src_w, src_h, buf.width, buf.height, scaled_rgba);
        src_ptr = &scaled_rgba;
    }

    if (buf.format == 4) {
        auto *dst16 = static_cast<uint16_t *>(buf.bits);
        for (int32_t y = 0; y < buf.height; ++y) {
            const uint8_t *src_row =
                src_ptr->data() + static_cast<size_t>(y) * static_cast<size_t>(buf.width) * 4u;
            uint16_t *dst_row = dst16 + static_cast<size_t>(y) * static_cast<size_t>(buf.stride);
            for (int32_t x = 0; x < buf.width; ++x) {
                const uint8_t r = src_row[static_cast<size_t>(x) * 4u + 0];
                const uint8_t g = src_row[static_cast<size_t>(x) * 4u + 1];
                const uint8_t b = src_row[static_cast<size_t>(x) * 4u + 2];
                dst_row[x] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else {
        auto *dst8 = static_cast<uint8_t *>(buf.bits);
        const bool force_opaque = (buf.format == 2);
        const size_t row_bytes = static_cast<size_t>(buf.width) * 4u;
        for (int32_t y = 0; y < buf.height; ++y) {
            const uint8_t *src_row = src_ptr->data() + static_cast<size_t>(y) * row_bytes;
            uint8_t *dst_row = dst8 + static_cast<size_t>(y) * static_cast<size_t>(buf.stride) * 4u;
            std::memcpy(dst_row, src_row, row_bytes);
            if (force_opaque) {
                for (int32_t x = 0; x < buf.width; ++x) {
                    dst_row[static_cast<size_t>(x) * 4u + 3] = 255;
                }
            }
        }
    }

    return ANativeWindow_unlockAndPost(win) == 0;
}

bool BuildImagePlanesFromRgba(const std::vector<uint8_t> &rgba, int32_t width, int32_t height,
                              int32_t format, std::vector<ImagePlaneBuffer> &out_planes)
{
    out_planes.clear();
    if (rgba.empty() || width <= 0 || height <= 0) {
        return false;
    }

    if (format == AIMAGE_FORMAT_YUV_420_888) {
        ExtractYuv420PlanesFromRgba(rgba.data(), width, height, out_planes);
        return !out_planes.empty();
    }
    if (format == AIMAGE_FORMAT_JPEG) {
        std::vector<uint8_t> jpeg;
        if (!EncodeRgbaToJpeg(rgba.data(), width, height, jpeg)) {
            return false;
        }
        ImagePlaneBuffer jpeg_plane;
        jpeg_plane.width = width;
        jpeg_plane.height = height;
        jpeg_plane.pixel_stride = 1;
        jpeg_plane.row_stride = static_cast<int32_t>(jpeg.size());
        jpeg_plane.data = std::move(jpeg);
        out_planes.push_back(std::move(jpeg_plane));
        return true;
    }

    ImagePlaneBuffer rgba_plane;
    rgba_plane.width = width;
    rgba_plane.height = height;
    rgba_plane.pixel_stride = 4;
    rgba_plane.row_stride = width * 4;
    rgba_plane.data = rgba;
    out_planes.push_back(std::move(rgba_plane));
    return true;
}

ACameraMetadata BuildCaptureResultMetadata(const CameraDeviceImpl *device)
{
    ACameraMetadata meta{};
    if (device) {
        const auto &info = device->GetInfo();
        meta.facing = info.facing;
        meta.sensor_size[0] = info.width;
        meta.sensor_size[1] = info.height;
        meta.orientation = info.orientation;
    }
    return meta;
}

} // namespace

struct WmfCameraReader::Impl {
    IMFMediaSource *source{nullptr};
    IMFSourceReader *reader{nullptr};
    GUID subtype{GUID_NULL};
    int32_t width{0};
    int32_t height{0};
    int32_t stride{0};
};

WmfCameraReader::WmfCameraReader() : impl_(std::make_unique<Impl>()) {}

WmfCameraReader::~WmfCameraReader() { Close(); }

bool WmfCameraReader::IsOpen() const noexcept { return impl_ && impl_->reader != nullptr; }

bool WmfCameraReader::Open(const std::string &symbolic_link, int32_t preferred_width,
                           int32_t preferred_height)
{
    Close();
    if (symbolic_link.empty()) {
        return false;
    }

    portage::media::WmfManager::Instance().Initialize();
    bool opened = false;
    const std::wstring wide_link = Utf8ToWide(symbolic_link);
    if (wide_link.empty()) {
        return false;
    }

    portage::media::WmfManager::Instance().DispatchSync([&]() {
        IMFAttributes *src_attrs = nullptr;
        if (FAILED(MFCreateAttributes(&src_attrs, 2)) || !src_attrs) {
            return;
        }
        src_attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        src_attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                             wide_link.c_str());

        IMFMediaSource *media_source = nullptr;
        HRESULT hr = MFCreateDeviceSource(src_attrs, &media_source);
        src_attrs->Release();
        if (FAILED(hr) || !media_source) {
            return;
        }

        IMFAttributes *reader_attrs = nullptr;
        IMFSourceReader *reader = nullptr;
        if (SUCCEEDED(MFCreateAttributes(&reader_attrs, 1)) && reader_attrs) {
            reader_attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        hr = MFCreateSourceReaderFromMediaSource(media_source, reader_attrs, &reader);
        if (reader_attrs) {
            reader_attrs->Release();
        }
        if (FAILED(hr) || !reader) {
            media_source->Shutdown();
            media_source->Release();
            return;
        }

        const GUID candidate_subtypes[] = {
            MFVideoFormat_RGB32,
            MFVideoFormat_NV12,
            MFVideoFormat_YUY2,
        };
        for (const GUID &candidate : candidate_subtypes) {
            IMFMediaType *req_type = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&req_type)) && req_type) {
                req_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                req_type->SetGUID(MF_MT_SUBTYPE, candidate);
                if (preferred_width > 0 && preferred_height > 0) {
                    MFSetAttributeSize(req_type, MF_MT_FRAME_SIZE,
                                       static_cast<UINT32>(preferred_width),
                                       static_cast<UINT32>(preferred_height));
                }
                HRESULT set_hr = reader->SetCurrentMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, req_type);
                req_type->Release();
                if (SUCCEEDED(set_hr)) {
                    break;
                }
            }
        }

        IMFMediaType *cur_type = nullptr;
        hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                         &cur_type);
        if (FAILED(hr) || !cur_type) {
            reader->Release();
            media_source->Shutdown();
            media_source->Release();
            return;
        }

        GUID active_subtype = GUID_NULL;
        cur_type->GetGUID(MF_MT_SUBTYPE, &active_subtype);
        UINT32 w = 0;
        UINT32 h = 0;
        MFGetAttributeSize(cur_type, MF_MT_FRAME_SIZE, &w, &h);
        UINT32 raw_stride = 0;
        int32_t stride = 0;
        if (SUCCEEDED(cur_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride))) {
            stride = static_cast<int32_t>(raw_stride);
        }
        cur_type->Release();

        const bool supported =
            (active_subtype == MFVideoFormat_RGB32 || active_subtype == MFVideoFormat_ARGB32 ||
             active_subtype == MFVideoFormat_NV12 || active_subtype == MFVideoFormat_YUY2);
        if (!supported || w == 0 || h == 0) {
            reader->Release();
            media_source->Shutdown();
            media_source->Release();
            return;
        }

        impl_->source = media_source;
        impl_->reader = reader;
        impl_->subtype = active_subtype;
        impl_->width = static_cast<int32_t>(w);
        impl_->height = static_cast<int32_t>(h);
        impl_->stride = stride;
        opened = true;
    });

    return opened;
}

void WmfCameraReader::Close()
{
    if (!impl_ || (!impl_->reader && !impl_->source)) {
        return;
    }

    portage::media::WmfManager::Instance().DispatchSync([this]() {
        if (impl_->reader) {
            impl_->reader->Release();
            impl_->reader = nullptr;
        }
        if (impl_->source) {
            impl_->source->Shutdown();
            impl_->source->Release();
            impl_->source = nullptr;
        }
        impl_->subtype = GUID_NULL;
        impl_->width = 0;
        impl_->height = 0;
        impl_->stride = 0;
    });
}

bool WmfCameraReader::CaptureRgbaFrame(int32_t &out_width, int32_t &out_height,
                                       std::vector<uint8_t> &out_rgba)
{
    if (!IsOpen()) {
        return false;
    }

    bool captured = false;
    portage::media::WmfManager::Instance().DispatchSync([&]() {
        if (!impl_->reader) {
            return;
        }

        for (int attempt = 0; attempt < 5; ++attempt) {
            DWORD stream_idx = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            IMFSample *sample = nullptr;
            HRESULT hr =
                impl_->reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
                                          0, &stream_idx, &flags, &timestamp, &sample);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
                if (sample) {
                    sample->Release();
                }
                return;
            }

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                IMFMediaType *cur_type = nullptr;
                if (SUCCEEDED(impl_->reader->GetCurrentMediaType(
                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur_type)) &&
                    cur_type) {
                    UINT32 w = 0;
                    UINT32 h = 0;
                    MFGetAttributeSize(cur_type, MF_MT_FRAME_SIZE, &w, &h);
                    cur_type->GetGUID(MF_MT_SUBTYPE, &impl_->subtype);
                    UINT32 raw_stride = 0;
                    if (SUCCEEDED(cur_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw_stride))) {
                        impl_->stride = static_cast<int32_t>(raw_stride);
                    }
                    if (w > 0 && h > 0) {
                        impl_->width = static_cast<int32_t>(w);
                        impl_->height = static_cast<int32_t>(h);
                    }
                    cur_type->Release();
                }
            }

            if (!sample) {
                continue;
            }

            IMFMediaBuffer *buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer) {
                BYTE *raw_data = nullptr;
                DWORD max_len = 0;
                DWORD cur_len = 0;
                if (SUCCEEDED(buffer->Lock(&raw_data, &max_len, &cur_len)) && raw_data &&
                    cur_len > 0) {
                    const int32_t w = impl_->width;
                    const int32_t h = impl_->height;
                    if (impl_->subtype == MFVideoFormat_NV12) {
                        const int32_t y_stride = (impl_->stride != 0) ? std::abs(impl_->stride) : w;
                        const size_t min_bytes =
                            static_cast<size_t>(y_stride) * static_cast<size_t>(h) * 3u / 2u;
                        if (cur_len >= min_bytes) {
                            portage::media::ConvertNv12ToRgba(raw_data, y_stride, w, h, out_rgba);
                            captured = !out_rgba.empty();
                        }
                    } else if (impl_->subtype == MFVideoFormat_YUY2) {
                        const int32_t yuy2_stride =
                            (impl_->stride != 0) ? std::abs(impl_->stride) : w * 2;
                        const size_t min_bytes =
                            static_cast<size_t>(yuy2_stride) * static_cast<size_t>(h);
                        if (cur_len >= min_bytes) {
                            ConvertYuy2ToRgba(raw_data, yuy2_stride, w, h, out_rgba);
                            captured = !out_rgba.empty();
                        }
                    } else if (impl_->subtype == MFVideoFormat_RGB32 ||
                               impl_->subtype == MFVideoFormat_ARGB32) {
                        const int32_t bgra_stride = (impl_->stride != 0) ? impl_->stride : w * 4;
                        const size_t min_bytes =
                            static_cast<size_t>(std::abs(bgra_stride)) * static_cast<size_t>(h);
                        if (cur_len >= min_bytes) {
                            ConvertBgraToRgba(raw_data, bgra_stride, w, h, out_rgba,
                                              impl_->subtype == MFVideoFormat_RGB32);
                            captured = !out_rgba.empty();
                        }
                    }
                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();

            if (captured) {
                out_width = impl_->width;
                out_height = impl_->height;
                break;
            }
        }
    });

    return captured;
}

CameraDeviceImpl::CameraDeviceImpl(CameraDeviceInfo info,
                                   const ACameraDevice_StateCallbacks *callbacks)
    : info_(std::move(info))
{
    if (callbacks) {
        callbacks_ = *callbacks;
    }
}

CameraDeviceImpl::~CameraDeviceImpl() { Close(); }

camera_status_t CameraDeviceImpl::Close()
{
    if (!closed_.exchange(true)) {
        if (callbacks_.onDisconnected) {
            callbacks_.onDisconnected(callbacks_.context, reinterpret_cast<ACameraDevice *>(this));
        }
    }
    return ACAMERA_OK;
}

CaptureSessionImpl::CaptureSessionImpl(CameraDeviceImpl *device,
                                       const std::vector<ANativeWindow *> &targets,
                                       const ACameraCaptureSession_stateCallbacks *callbacks)
    : device_(device), targets_(targets)
{
    if (callbacks) {
        callbacks_ = *callbacks;
    }
    if (callbacks_.onReady) {
        callbacks_.onReady(callbacks_.context, reinterpret_cast<ACameraCaptureSession *>(this));
    }
}

CaptureSessionImpl::~CaptureSessionImpl() { Close(); }

camera_status_t CaptureSessionImpl::Close()
{
    if (!closed_.exchange(true)) {
        StopRepeatingInternal(false);
        {
            std::lock_guard<std::mutex> lock(capture_mutex_);
            if (physical_reader_) {
                physical_reader_->Close();
                physical_reader_.reset();
            }
        }
        if (callbacks_.onClosed) {
            callbacks_.onClosed(callbacks_.context,
                                reinterpret_cast<ACameraCaptureSession *>(this));
        }
    }
    return ACAMERA_OK;
}

std::vector<ANativeWindow *>
CaptureSessionImpl::CollectRequestTargets(ACaptureRequest *request) const
{
    if (request && !request->targets.empty()) {
        std::vector<ANativeWindow *> resolved;
        resolved.reserve(request->targets.size());
        for (const auto *t : request->targets) {
            if (t && t->window) {
                resolved.push_back(t->window);
            }
        }
        if (!resolved.empty()) {
            return resolved;
        }
    }
    return targets_;
}

void CaptureSessionImpl::ProduceFrame()
{
    const int64_t now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
    ProduceFrameInternal(targets_, now_nanos);
}

int64_t CaptureSessionImpl::ProduceFrameInternal(const std::vector<ANativeWindow *> &targets,
                                                 int64_t timestamp_nanos)
{
    if (closed_.load()) {
        return frame_counter_.load();
    }

    const int64_t current_frame = ++frame_counter_;

    int32_t raw_w = 0;
    int32_t raw_h = 0;
    std::vector<uint8_t> raw_rgba;
    bool has_physical_frame = false;

    const bool can_use_physical = device_ && device_->GetInfo().is_physical &&
                                  !CameraManager::Instance().IsForceSynthetic() &&
                                  !device_->GetInfo().symbolic_link.empty();
    if (can_use_physical) {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        if (!closed_.load()) {
            if (!physical_open_attempted_) {
                physical_open_attempted_ = true;
                int32_t pref_w = device_->GetInfo().width;
                int32_t pref_h = device_->GetInfo().height;
                for (auto *win : targets) {
                    if (win && win->width > 0 && win->height > 0) {
                        pref_w = win->width;
                        pref_h = win->height;
                        break;
                    }
                }
                auto candidate = std::make_unique<WmfCameraReader>();
                if (candidate->Open(device_->GetInfo().symbolic_link, pref_w, pref_h)) {
                    physical_reader_ = std::move(candidate);
                }
            }
            if (physical_reader_ && physical_reader_->IsOpen()) {
                has_physical_frame = physical_reader_->CaptureRgbaFrame(raw_w, raw_h, raw_rgba);
            }
        }
    }

    for (auto *win : targets) {
        if (!win) {
            continue;
        }
        auto *reader = CameraManager::Instance().LookupImageReader(win);
        if (reader) {
            const int32_t dst_w = reader->GetWidth();
            const int32_t dst_h = reader->GetHeight();
            const int32_t dst_fmt = reader->GetFormat();
            std::vector<ImagePlaneBuffer> planes;

            if (has_physical_frame) {
                const std::vector<uint8_t> *rgba_ptr = &raw_rgba;
                std::vector<uint8_t> scaled_rgba;
                if (raw_w != dst_w || raw_h != dst_h) {
                    ScaleRgbaFrame(raw_rgba.data(), raw_w, raw_h, dst_w, dst_h, scaled_rgba);
                    rgba_ptr = &scaled_rgba;
                }
                BuildImagePlanesFromRgba(*rgba_ptr, dst_w, dst_h, dst_fmt, planes);
            }

            if (planes.empty()) {
                CameraManager::Instance().GenerateSyntheticFrame(
                    dst_w, dst_h, dst_fmt, timestamp_nanos, current_frame, planes);
            }
            reader->PushFrame(dst_w, dst_h, dst_fmt, timestamp_nanos, planes);
            win->last_queue_time_nanos = timestamp_nanos;
            win->last_latch_time_nanos = timestamp_nanos;
            win->last_present_time_nanos = timestamp_nanos;
            win->frame_index++;
        } else {
            const int32_t dst_w = (win->width > 0) ? win->width : 1280;
            const int32_t dst_h = (win->height > 0) ? win->height : 720;
            bool posted = false;
            if (has_physical_frame) {
                posted = WriteRgbaToNativeWindow(win, raw_rgba, raw_w, raw_h);
            } else {
                std::vector<uint8_t> syn_rgba;
                GenerateSyntheticRgba(dst_w, dst_h, current_frame, syn_rgba);
                posted = WriteRgbaToNativeWindow(win, syn_rgba, dst_w, dst_h);
            }
            if (!posted) {
                win->last_queue_time_nanos = timestamp_nanos;
                win->last_latch_time_nanos = timestamp_nanos;
                win->last_present_time_nanos = timestamp_nanos;
                win->frame_index++;
            }
        }
    }

    return current_frame;
}

camera_status_t
CaptureSessionImpl::SetRepeatingRequest(ACameraCaptureSession_captureCallbacks *callbacks,
                                        int num_requests, ACaptureRequest **requests,
                                        int *out_sequence_id)
{
    if (closed_.load()) {
        return ACAMERA_ERROR_SESSION_CLOSED;
    }
    if (num_requests <= 0 || !requests) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    StopRepeating();

    if (callbacks) {
        repeating_callbacks_ = *callbacks;
    } else {
        repeating_callbacks_ = {};
    }

    repeating_request_ptrs_.clear();
    repeating_request_targets_.clear();
    repeating_request_ptrs_.reserve(static_cast<size_t>(num_requests));
    repeating_request_targets_.reserve(static_cast<size_t>(num_requests));
    for (int i = 0; i < num_requests; ++i) {
        repeating_request_ptrs_.push_back(requests[i]);
        repeating_request_targets_.push_back(CollectRequestTargets(requests[i]));
    }

    int seq = sequence_counter_++;
    active_repeating_seq_ = seq;
    active_repeating_last_frame_.store(-1);
    if (out_sequence_id) {
        *out_sequence_id = seq;
    }

    repeating_active_.store(true);
    worker_thread_ = std::thread(&CaptureSessionImpl::RepeatingWorkerLoop, this);

    if (callbacks_.onActive) {
        callbacks_.onActive(callbacks_.context, reinterpret_cast<ACameraCaptureSession *>(this));
    }
    return ACAMERA_OK;
}

void CaptureSessionImpl::RepeatingWorkerLoop()
{
    auto *session_ptr = reinterpret_cast<ACameraCaptureSession *>(this);
    while (repeating_active_.load() && !closed_.load()) {
        for (size_t i = 0; i < repeating_request_targets_.size(); ++i) {
            if (!repeating_active_.load() || closed_.load()) {
                break;
            }
            ACaptureRequest *req_ptr = repeating_request_ptrs_[i];
            const int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
            if (repeating_callbacks_.onCaptureStarted) {
                repeating_callbacks_.onCaptureStarted(repeating_callbacks_.context, session_ptr,
                                                      req_ptr, ts);
            }
            const int64_t frame_num = ProduceFrameInternal(repeating_request_targets_[i], ts);
            active_repeating_last_frame_.store(frame_num);

            if (repeating_callbacks_.onCaptureCompleted) {
                ACameraMetadata meta = BuildCaptureResultMetadata(device_);
                repeating_callbacks_.onCaptureCompleted(repeating_callbacks_.context, session_ptr,
                                                        req_ptr, &meta);
            }
        }

        std::unique_lock<std::mutex> lock(worker_mutex_);
        worker_cv_.wait_for(lock, std::chrono::milliseconds(33),
                            [this]() { return !repeating_active_.load() || closed_.load(); });
    }
}

camera_status_t CaptureSessionImpl::Capture(ACameraCaptureSession_captureCallbacks *callbacks,
                                            int num_requests, ACaptureRequest **requests,
                                            int *out_sequence_id)
{
    if (closed_.load()) {
        return ACAMERA_ERROR_SESSION_CLOSED;
    }
    if (num_requests <= 0 || !requests) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    int seq = sequence_counter_++;
    if (out_sequence_id) {
        *out_sequence_id = seq;
    }

    auto *session_ptr = reinterpret_cast<ACameraCaptureSession *>(this);
    ACameraCaptureSession_captureCallbacks cb{};
    if (callbacks) {
        cb = *callbacks;
    }

    int64_t last_frame_num = frame_counter_.load();
    for (int i = 0; i < num_requests; ++i) {
        const int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        if (cb.onCaptureStarted) {
            cb.onCaptureStarted(cb.context, session_ptr, requests[i], ts);
        }
        auto req_targets = CollectRequestTargets(requests[i]);
        last_frame_num = ProduceFrameInternal(req_targets, ts);
        if (cb.onCaptureCompleted) {
            ACameraMetadata meta = BuildCaptureResultMetadata(device_);
            cb.onCaptureCompleted(cb.context, session_ptr, requests[i], &meta);
        }
    }

    if (cb.onCaptureSequenceCompleted) {
        cb.onCaptureSequenceCompleted(cb.context, session_ptr, seq, last_frame_num);
    }
    return ACAMERA_OK;
}

void CaptureSessionImpl::StopRepeatingInternal(bool aborted)
{
    repeating_active_.store(false);
    worker_cv_.notify_all();
    if (worker_thread_.joinable() && worker_thread_.get_id() != std::this_thread::get_id()) {
        worker_thread_.join();
    }

    const int seq = active_repeating_seq_;
    active_repeating_seq_ = 0;
    const int64_t last_frame = active_repeating_last_frame_.exchange(-1);
    const ACameraCaptureSession_captureCallbacks cb = repeating_callbacks_;
    repeating_callbacks_ = {};
    repeating_request_ptrs_.clear();
    repeating_request_targets_.clear();

    if (seq > 0) {
        auto *session_ptr = reinterpret_cast<ACameraCaptureSession *>(this);
        if (aborted) {
            if (cb.onCaptureSequenceAborted) {
                cb.onCaptureSequenceAborted(cb.context, session_ptr, seq);
            }
        } else {
            if (cb.onCaptureSequenceCompleted) {
                cb.onCaptureSequenceCompleted(cb.context, session_ptr, seq, last_frame);
            }
        }
        if (!closed_.load() && callbacks_.onReady) {
            callbacks_.onReady(callbacks_.context, session_ptr);
        }
    }
}

camera_status_t CaptureSessionImpl::StopRepeating()
{
    StopRepeatingInternal(false);
    return ACAMERA_OK;
}

camera_status_t CaptureSessionImpl::AbortCaptures()
{
    StopRepeatingInternal(true);
    return ACAMERA_OK;
}

CameraManager &CameraManager::Instance()
{
    static CameraManager instance;
    return instance;
}

CameraManager::CameraManager() { Initialize(); }

CameraManager::~CameraManager() { Shutdown(); }

void CameraManager::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    InitializeLocked();
}

void CameraManager::InitializeLocked()
{
    if (initialized_) {
        return;
    }

    EnumeratePhysicalCameras();
    initialized_ = true;
}

void CameraManager::Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    devices_.clear();
    availability_callbacks_.clear();
    open_counts_.clear();
    registered_readers_.clear();
    initialized_ = false;
}

void CameraManager::SetForceSynthetic(bool force)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (force_synthetic_ == force) {
        return;
    }
    force_synthetic_ = force;
    if (initialized_) {
        EnumeratePhysicalCameras();
    }
}

bool CameraManager::IsForceSynthetic() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return force_synthetic_;
}

void CameraManager::EnumeratePhysicalCameras()
{
    devices_.clear();

    if (!force_synthetic_) {
        portage::media::WmfManager::Instance().Initialize();
        std::vector<CameraDeviceInfo> enumerated;
        portage::media::WmfManager::Instance().DispatchSync([&enumerated]() {
            IMFAttributes *attributes = nullptr;
            HRESULT hr = MFCreateAttributes(&attributes, 1);
            if (FAILED(hr) || !attributes) {
                return;
            }
            hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                     MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            if (SUCCEEDED(hr)) {
                IMFActivate **device_activates = nullptr;
                UINT32 count = 0;
                hr = MFEnumDeviceSources(attributes, &device_activates, &count);
                if (SUCCEEDED(hr) && device_activates) {
                    for (UINT32 i = 0; i < count; ++i) {
                        if (!device_activates[i]) {
                            continue;
                        }
                        WCHAR *friendly_name = nullptr;
                        UINT32 name_len = 0;
                        std::string name_str = "Physical Camera";
                        if (SUCCEEDED(device_activates[i]->GetAllocatedString(
                                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly_name, &name_len)) &&
                            friendly_name) {
                            std::string utf8_name = WideToUtf8(friendly_name);
                            if (!utf8_name.empty()) {
                                name_str = std::move(utf8_name);
                            }
                            CoTaskMemFree(friendly_name);
                        }

                        WCHAR *sym_link = nullptr;
                        UINT32 sym_len = 0;
                        std::string sym_str;
                        if (SUCCEEDED(device_activates[i]->GetAllocatedString(
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &sym_link,
                                &sym_len)) &&
                            sym_link) {
                            sym_str = WideToUtf8(sym_link);
                            CoTaskMemFree(sym_link);
                        }

                        CameraDeviceInfo info;
                        info.id = std::to_string(enumerated.size());
                        info.name = std::move(name_str);
                        info.symbolic_link = std::move(sym_str);
                        info.facing = (enumerated.empty()) ? ACAMERA_LENS_FACING_BACK
                                                           : ACAMERA_LENS_FACING_FRONT;
                        info.width = (enumerated.empty()) ? 1920 : 1280;
                        info.height = (enumerated.empty()) ? 1080 : 720;
                        info.orientation = (enumerated.empty()) ? 90 : 270;
                        info.is_physical = true;
                        enumerated.push_back(std::move(info));

                        device_activates[i]->Release();
                    }
                    CoTaskMemFree(device_activates);
                }
            }
            attributes->Release();
        });
        devices_ = std::move(enumerated);
    }

    if (devices_.empty()) {
        CameraDeviceInfo back;
        back.id = "0";
        back.name = "Portage Synthetic Back Camera";
        back.facing = ACAMERA_LENS_FACING_BACK;
        back.width = 1920;
        back.height = 1080;
        back.orientation = 90;
        back.is_physical = false;
        devices_.push_back(std::move(back));

        CameraDeviceInfo front;
        front.id = "1";
        front.name = "Portage Synthetic Front Camera";
        front.facing = ACAMERA_LENS_FACING_FRONT;
        front.width = 1280;
        front.height = 720;
        front.orientation = 270;
        front.is_physical = false;
        devices_.push_back(std::move(front));
    }
}

std::vector<std::string> CameraManager::GetCameraIds()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        InitializeLocked();
    }
    std::vector<std::string> ids;
    ids.reserve(devices_.size());
    for (const auto &dev : devices_) {
        ids.push_back(dev.id);
    }
    return ids;
}

const CameraDeviceInfo *CameraManager::GetDeviceInfo(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        InitializeLocked();
    }
    for (const auto &dev : devices_) {
        if (dev.id == id) {
            return &dev;
        }
    }
    return nullptr;
}

camera_status_t
CameraManager::RegisterAvailabilityCallback(const ACameraManager_AvailabilityCallbacks *callback)
{
    if (!callback) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    std::vector<std::pair<std::string, bool>> initial_states;
    ACameraManager_AvailabilityCallbacks cb_copy = *callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            InitializeLocked();
        }
        bool exists = false;
        for (const auto &entry : availability_callbacks_) {
            if (entry.key == callback) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            availability_callbacks_.push_back({callback, cb_copy});
        }

        initial_states.reserve(devices_.size());
        for (const auto &dev : devices_) {
            auto it = open_counts_.find(dev.id);
            bool available = (it == open_counts_.end() || it->second <= 0);
            initial_states.emplace_back(dev.id, available);
        }
    }

    for (const auto &[cam_id, available] : initial_states) {
        if (available) {
            if (cb_copy.onCameraAvailable) {
                cb_copy.onCameraAvailable(cb_copy.context, cam_id.c_str());
            }
        } else {
            if (cb_copy.onCameraUnavailable) {
                cb_copy.onCameraUnavailable(cb_copy.context, cam_id.c_str());
            }
        }
    }
    return ACAMERA_OK;
}

camera_status_t
CameraManager::UnregisterAvailabilityCallback(const ACameraManager_AvailabilityCallbacks *callback)
{
    if (!callback) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = availability_callbacks_.begin(); it != availability_callbacks_.end(); ++it) {
        if (it->key == callback ||
            (it->callbacks.context == callback->context &&
             it->callbacks.onCameraAvailable == callback->onCameraAvailable &&
             it->callbacks.onCameraUnavailable == callback->onCameraUnavailable)) {
            availability_callbacks_.erase(it);
            break;
        }
    }
    return ACAMERA_OK;
}

camera_status_t CameraManager::OpenCamera(const char *id,
                                          const ACameraDevice_StateCallbacks *callbacks,
                                          ACameraDevice **out_device)
{
    if (!id || !out_device) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *out_device = nullptr;

    CameraDeviceInfo device_info;
    bool found = false;
    bool became_unavailable = false;
    std::vector<ACameraManager_AvailabilityCallbacks> callbacks_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            InitializeLocked();
        }
        for (const auto &dev : devices_) {
            if (dev.id == id) {
                device_info = dev;
                found = true;
                break;
            }
        }
        if (!found) {
            return ACAMERA_ERROR_INVALID_PARAMETER;
        }

        int &open_count = open_counts_[device_info.id];
        became_unavailable = (open_count == 0);
        ++open_count;
        if (became_unavailable) {
            callbacks_snapshot.reserve(availability_callbacks_.size());
            for (const auto &entry : availability_callbacks_) {
                callbacks_snapshot.push_back(entry.callbacks);
            }
        }
    }

    std::string cam_id = device_info.id;
    auto dev_impl = std::make_unique<CameraDeviceImpl>(std::move(device_info), callbacks);
    auto dev_wrapper = std::make_unique<ACameraDevice>();
    dev_wrapper->impl = std::move(dev_impl);
    *out_device = dev_wrapper.release();

    if (became_unavailable) {
        for (const auto &cb : callbacks_snapshot) {
            if (cb.onCameraUnavailable) {
                cb.onCameraUnavailable(cb.context, cam_id.c_str());
            }
        }
    }
    return ACAMERA_OK;
}

void CameraManager::CloseCamera(CameraDeviceImpl *device)
{
    if (!device) {
        return;
    }

    bool first_close = !device->IsClosed();
    device->Close();
    if (!first_close) {
        return;
    }

    std::string cam_id = device->GetId();
    std::vector<ACameraManager_AvailabilityCallbacks> callbacks_snapshot;
    bool became_available = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_counts_.find(cam_id);
        if (it != open_counts_.end() && it->second > 0) {
            --it->second;
            if (it->second == 0) {
                open_counts_.erase(it);
                became_available = true;
                callbacks_snapshot.reserve(availability_callbacks_.size());
                for (const auto &entry : availability_callbacks_) {
                    callbacks_snapshot.push_back(entry.callbacks);
                }
            }
        }
    }

    if (became_available) {
        for (const auto &cb : callbacks_snapshot) {
            if (cb.onCameraAvailable) {
                cb.onCameraAvailable(cb.context, cam_id.c_str());
            }
        }
    }
}

void CameraManager::RegisterImageReader(ANativeWindow *window, ImageReaderImpl *reader)
{
    std::lock_guard<std::mutex> lock(mutex_);
    registered_readers_[window] = reader;
}

void CameraManager::UnregisterImageReader(ANativeWindow *window)
{
    std::lock_guard<std::mutex> lock(mutex_);
    registered_readers_.erase(window);
}

ImageReaderImpl *CameraManager::LookupImageReader(ANativeWindow *window)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registered_readers_.find(window);
    if (it != registered_readers_.end()) {
        return it->second;
    }
    return nullptr;
}

void CameraManager::GenerateSyntheticFrame(int32_t width, int32_t height, int32_t format,
                                           int64_t timestamp, int64_t frame_index,
                                           std::vector<ImagePlaneBuffer> &out_planes)
{
    (void)timestamp;
    out_planes.clear();
    if (width <= 0 || height <= 0) {
        return;
    }

    std::vector<uint8_t> rgba;
    GenerateSyntheticRgba(width, height, frame_index, rgba);
    BuildImagePlanesFromRgba(rgba, width, height, format, out_planes);
}

} // namespace portage::camera
