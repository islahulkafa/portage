#include "camera/camera_manager.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace portage::camera {

ImageReaderImpl::ImageReaderImpl(int32_t width, int32_t height, int32_t format,
                                 int32_t max_images)
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

ANativeWindow *ImageReaderImpl::GetNativeWindow()
{
    return window_.get();
}

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
        listener_copy.onImageAvailable(listener_copy.context, reinterpret_cast<AImageReader *>(this));
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

CameraDeviceImpl::CameraDeviceImpl(std::string id, const ACameraDevice_StateCallbacks *callbacks)
    : id_(std::move(id))
{
    if (callbacks) {
        callbacks_ = *callbacks;
    }
}

CameraDeviceImpl::~CameraDeviceImpl()
{
    Close();
}

camera_status_t CameraDeviceImpl::Close()
{
    if (!closed_.exchange(true)) {
        if (callbacks_.onDisconnected) {
            ACameraDevice dev_wrap;
            dev_wrap.impl = nullptr;
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

CaptureSessionImpl::~CaptureSessionImpl()
{
    Close();
}

camera_status_t CaptureSessionImpl::Close()
{
    if (!closed_.exchange(true)) {
        StopRepeating();
        if (callbacks_.onClosed) {
            callbacks_.onClosed(callbacks_.context, reinterpret_cast<ACameraCaptureSession *>(this));
        }
    }
    return ACAMERA_OK;
}

void CaptureSessionImpl::ProduceFrame()
{
    if (closed_.load()) {
        return;
    }

    int64_t now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    int64_t current_frame = ++frame_counter_;

    for (auto *win : targets_) {
        if (!win) {
            continue;
        }
        auto *reader = CameraManager::Instance().LookupImageReader(win);
        if (reader) {
            std::vector<ImagePlaneBuffer> planes;
            CameraManager::Instance().GenerateSyntheticFrame(reader->GetWidth(), reader->GetHeight(),
                                                             reader->GetFormat(), now_nanos,
                                                             current_frame, planes);
            reader->PushFrame(reader->GetWidth(), reader->GetHeight(), reader->GetFormat(),
                              now_nanos, planes);
        }
        win->last_queue_time_nanos = now_nanos;
        win->last_latch_time_nanos = now_nanos;
        win->last_present_time_nanos = now_nanos;
        win->frame_index++;
    }
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

    int seq = sequence_counter_++;
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
    while (repeating_active_.load() && !closed_.load()) {
        ProduceFrame();

        if (repeating_callbacks_.onCaptureCompleted) {
            ACameraMetadata meta{};
            repeating_callbacks_.onCaptureCompleted(
                repeating_callbacks_.context, reinterpret_cast<ACameraCaptureSession *>(this),
                nullptr, &meta);
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

    for (int i = 0; i < num_requests; ++i) {
        ProduceFrame();
        if (callbacks && callbacks->onCaptureCompleted) {
            ACameraMetadata meta{};
            callbacks->onCaptureCompleted(callbacks->context,
                                          reinterpret_cast<ACameraCaptureSession *>(this),
                                          requests[i], &meta);
        }
    }
    return ACAMERA_OK;
}

camera_status_t CaptureSessionImpl::StopRepeating()
{
    repeating_active_.store(false);
    worker_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    return ACAMERA_OK;
}

camera_status_t CaptureSessionImpl::AbortCaptures()
{
    StopRepeating();
    return ACAMERA_OK;
}

CameraManager &CameraManager::Instance()
{
    static CameraManager instance;
    return instance;
}

CameraManager::CameraManager()
{
    Initialize();
}

CameraManager::~CameraManager()
{
    Shutdown();
}

void CameraManager::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
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
    registered_readers_.clear();
    MFShutdown();
    initialized_ = false;
}

void CameraManager::EnumeratePhysicalCameras()
{
    devices_.clear();

    if (!force_synthetic_) {
        IMFAttributes *attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(hr) && attributes) {
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

            IMFActivate **device_activates = nullptr;
            UINT32 count = 0;
            hr = MFEnumDeviceSources(attributes, &device_activates, &count);
            if (SUCCEEDED(hr) && count > 0) {
                for (UINT32 i = 0; i < count; ++i) {
                    WCHAR *friendly_name = nullptr;
                    UINT32 name_len = 0;
                    device_activates[i]->GetAllocatedString(
                        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly_name, &name_len);

                    std::string name_str = "Physical Camera";
                    if (friendly_name) {
                        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, friendly_name, -1, nullptr,
                                                           0, nullptr, nullptr);
                        if (utf8_len > 0) {
                            name_str.resize(utf8_len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, friendly_name, -1, name_str.data(),
                                                utf8_len, nullptr, nullptr);
                        }
                        CoTaskMemFree(friendly_name);
                    }

                    CameraDeviceInfo info;
                    info.id = std::to_string(devices_.size());
                    info.name = std::move(name_str);
                    info.facing = (i == 0) ? ACAMERA_LENS_FACING_BACK : ACAMERA_LENS_FACING_FRONT;
                    info.width = (i == 0) ? 1920 : 1280;
                    info.height = (i == 0) ? 1080 : 720;
                    info.orientation = (i == 0) ? 90 : 270;
                    info.is_physical = true;
                    devices_.push_back(std::move(info));

                    device_activates[i]->Release();
                }
                CoTaskMemFree(device_activates);
            }
            attributes->Release();
        }
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
    for (const auto &dev : devices_) {
        if (dev.id == id) {
            return &dev;
        }
    }
    return nullptr;
}

camera_status_t CameraManager::OpenCamera(const char *id,
                                          const ACameraDevice_StateCallbacks *callbacks,
                                          ACameraDevice **out_device)
{
    if (!id || !out_device) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *out_device = nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    auto dev_impl = std::make_unique<CameraDeviceImpl>(std::string(id), callbacks);
    auto dev_wrapper = std::make_unique<ACameraDevice>();
    dev_wrapper->impl = std::move(dev_impl);
    *out_device = dev_wrapper.release();
    return ACAMERA_OK;
}

void CameraManager::CloseCamera(CameraDeviceImpl *device)
{
    if (device) {
        device->Close();
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

    const uint8_t color_bars[8][3] = {
        {255, 255, 255}, // White
        {255, 255, 0},   // Yellow
        {0, 255, 255},   // Cyan
        {0, 255, 0},     // Green
        {255, 0, 255},   // Magenta
        {255, 0, 0},     // Red
        {0, 0, 255},     // Blue
        {0, 0, 0}        // Black
    };

    if (format == AIMAGE_FORMAT_YUV_420_888) {
        ImagePlaneBuffer y_plane;
        y_plane.width = width;
        y_plane.height = height;
        y_plane.pixel_stride = 1;
        y_plane.row_stride = width;
        y_plane.data.resize(static_cast<size_t>(width) * height);

        int bar_width = std::max(1, width / 8);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int bar_idx = std::min(7, x / bar_width);
                int r = color_bars[bar_idx][0];
                int g = color_bars[bar_idx][1];
                int b = color_bars[bar_idx][2];
                uint8_t y_val = static_cast<uint8_t>((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                y_plane.data[y * width + x] = y_val;
            }
        }
        out_planes.push_back(std::move(y_plane));

        int uv_width = width / 2;
        int uv_height = height / 2;

        ImagePlaneBuffer u_plane;
        u_plane.width = uv_width;
        u_plane.height = uv_height;
        u_plane.pixel_stride = 1;
        u_plane.row_stride = uv_width;
        u_plane.data.resize(static_cast<size_t>(uv_width) * uv_height);

        ImagePlaneBuffer v_plane;
        v_plane.width = uv_width;
        v_plane.height = uv_height;
        v_plane.pixel_stride = 1;
        v_plane.row_stride = uv_width;
        v_plane.data.resize(static_cast<size_t>(uv_width) * uv_height);

        int uv_bar_width = std::max(1, uv_width / 8);
        for (int y = 0; y < uv_height; ++y) {
            for (int x = 0; x < uv_width; ++x) {
                int bar_idx = std::min(7, x / uv_bar_width);
                int r = color_bars[bar_idx][0];
                int g = color_bars[bar_idx][1];
                int b = color_bars[bar_idx][2];
                uint8_t u_val = static_cast<uint8_t>((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                uint8_t v_val = static_cast<uint8_t>((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                u_plane.data[y * uv_width + x] = u_val;
                v_plane.data[y * uv_width + x] = v_val;
            }
        }
        out_planes.push_back(std::move(u_plane));
        out_planes.push_back(std::move(v_plane));
    } else {
        ImagePlaneBuffer rgba_plane;
        rgba_plane.width = width;
        rgba_plane.height = height;
        rgba_plane.pixel_stride = 4;
        rgba_plane.row_stride = width * 4;
        rgba_plane.data.resize(static_cast<size_t>(width) * height * 4);

        int bar_width = std::max(1, width / 8);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int bar_idx = std::min(7, x / bar_width);
                size_t offset = static_cast<size_t>((y * width + x) * 4);
                rgba_plane.data[offset + 0] = color_bars[bar_idx][0];
                rgba_plane.data[offset + 1] = color_bars[bar_idx][1];
                rgba_plane.data[offset + 2] = color_bars[bar_idx][2];
                rgba_plane.data[offset + 3] = 255;
            }
        }

        int box_size = 32;
        int box_x = static_cast<int>((frame_index * 8) % std::max(1, width - box_size));
        int box_y = static_cast<int>((frame_index * 4) % std::max(1, height - box_size));
        for (int cy = box_y; cy < std::min(height, box_y + box_size); ++cy) {
            for (int cx = box_x; cx < std::min(width, box_x + box_size); ++cx) {
                size_t offset = static_cast<size_t>((cy * width + cx) * 4);
                rgba_plane.data[offset + 0] = 255;
                rgba_plane.data[offset + 1] = 255;
                rgba_plane.data[offset + 2] = 255;
                rgba_plane.data[offset + 3] = 255;
            }
        }
        out_planes.push_back(std::move(rgba_plane));
    }
}

} // namespace portage::camera
