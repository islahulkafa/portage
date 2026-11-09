#pragma once

#include "camera/camera_types.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portage::camera {

struct CameraDeviceInfo {
    std::string id;
    std::string name;
    int facing{ACAMERA_LENS_FACING_BACK};
    int32_t width{1920};
    int32_t height{1080};
    int32_t orientation{90};
    bool is_physical{false};
};

struct ImagePlaneBuffer {
    int32_t width{0};
    int32_t height{0};
    int32_t pixel_stride{1};
    int32_t row_stride{0};
    std::vector<uint8_t> data{};
};

struct InternalImage {
    int32_t width{0};
    int32_t height{0};
    int32_t format{AIMAGE_FORMAT_RGBA_8888};
    int64_t timestamp{0};
    std::vector<ImagePlaneBuffer> planes{};
    bool acquired{false};
};

class ImageReaderImpl {
public:
    ImageReaderImpl(int32_t width, int32_t height, int32_t format, int32_t max_images);
    ~ImageReaderImpl();

    ANativeWindow *GetNativeWindow();
    media_status_t AcquireNextImage(AImage **out_image);
    media_status_t AcquireLatestImage(AImage **out_image);
    void ReleaseImage(AImage *image);
    void SetListener(AImageReader_ImageListener *listener);
    void PushFrame(int32_t width, int32_t height, int32_t format, int64_t timestamp,
                   const std::vector<ImagePlaneBuffer> &planes);

    int32_t GetWidth() const { return width_; }
    int32_t GetHeight() const { return height_; }
    int32_t GetFormat() const { return format_; }
    int32_t GetMaxImages() const { return max_images_; }

private:
    int32_t width_{0};
    int32_t height_{0};
    int32_t format_{AIMAGE_FORMAT_RGBA_8888};
    int32_t max_images_{4};
    std::unique_ptr<ANativeWindow> window_{};
    AImageReader_ImageListener listener_{nullptr, nullptr};

    mutable std::mutex mutex_{};
    std::deque<std::unique_ptr<InternalImage>> available_images_{};
    std::vector<std::unique_ptr<InternalImage>> acquired_images_{};
};

class CameraDeviceImpl {
public:
    CameraDeviceImpl(std::string id, const ACameraDevice_StateCallbacks *callbacks);
    ~CameraDeviceImpl();

    const std::string &GetId() const { return id_; }
    camera_status_t Close();
    bool IsClosed() const { return closed_.load(); }

    const ACameraDevice_StateCallbacks &GetCallbacks() const { return callbacks_; }

private:
    std::string id_{};
    ACameraDevice_StateCallbacks callbacks_{nullptr, nullptr, nullptr};
    std::atomic<bool> closed_{false};
};

class CaptureSessionImpl {
public:
    CaptureSessionImpl(CameraDeviceImpl *device, const std::vector<ANativeWindow *> &targets,
                       const ACameraCaptureSession_stateCallbacks *callbacks);
    ~CaptureSessionImpl();

    camera_status_t SetRepeatingRequest(ACameraCaptureSession_captureCallbacks *callbacks,
                                        int num_requests, ACaptureRequest **requests,
                                        int *out_sequence_id);
    camera_status_t Capture(ACameraCaptureSession_captureCallbacks *callbacks, int num_requests,
                            ACaptureRequest **requests, int *out_sequence_id);
    camera_status_t StopRepeating();
    camera_status_t AbortCaptures();
    camera_status_t Close();

    CameraDeviceImpl *GetDevice() const { return device_; }
    bool IsClosed() const { return closed_.load(); }

    void ProduceFrame();

private:
    void RepeatingWorkerLoop();

    CameraDeviceImpl *device_{nullptr};
    std::vector<ANativeWindow *> targets_{};
    ACameraCaptureSession_stateCallbacks callbacks_{nullptr, nullptr, nullptr, nullptr};
    std::atomic<bool> closed_{false};
    std::atomic<bool> repeating_active_{false};

    std::mutex worker_mutex_{};
    std::condition_variable worker_cv_{};
    std::thread worker_thread_{};

    int sequence_counter_{1};
    int64_t frame_counter_{0};
    ACameraCaptureSession_captureCallbacks repeating_callbacks_{};
};

class CameraManager {
public:
    static CameraManager &Instance();

    void Initialize();
    void Shutdown();

    void SetForceSynthetic(bool force) { force_synthetic_ = force; }
    bool IsForceSynthetic() const { return force_synthetic_; }

    std::vector<std::string> GetCameraIds();
    const CameraDeviceInfo *GetDeviceInfo(const std::string &id);

    camera_status_t OpenCamera(const char *id, const ACameraDevice_StateCallbacks *callbacks,
                               ACameraDevice **out_device);
    void CloseCamera(CameraDeviceImpl *device);

    void RegisterImageReader(ANativeWindow *window, ImageReaderImpl *reader);
    void UnregisterImageReader(ANativeWindow *window);
    ImageReaderImpl *LookupImageReader(ANativeWindow *window);

    void GenerateSyntheticFrame(int32_t width, int32_t height, int32_t format, int64_t timestamp,
                                int64_t frame_index, std::vector<ImagePlaneBuffer> &out_planes);

private:
    CameraManager();
    ~CameraManager();
    CameraManager(const CameraManager &) = delete;
    CameraManager &operator=(const CameraManager &) = delete;

    void EnumeratePhysicalCameras();

    bool initialized_{false};
    bool force_synthetic_{false};
    mutable std::mutex mutex_{};
    std::vector<CameraDeviceInfo> devices_{};
    std::unordered_map<ANativeWindow *, ImageReaderImpl *> registered_readers_{};
};

} // namespace portage::camera

struct ACameraManager {
    portage::camera::CameraManager *mgr{nullptr};
};

struct ACameraDevice {
    std::unique_ptr<portage::camera::CameraDeviceImpl> impl{nullptr};
};

struct ACameraCaptureSession {
    std::unique_ptr<portage::camera::CaptureSessionImpl> impl{nullptr};
};

struct ACaptureRequest {
    int template_id{TEMPLATE_PREVIEW};
    std::vector<const ACameraOutputTarget *> targets{};
    std::unordered_map<uint32_t, std::vector<uint8_t>> entries{};
};

struct ACameraOutputTarget {
    ANativeWindow *window{nullptr};
};

struct ACaptureSessionOutput {
    ANativeWindow *window{nullptr};
};

struct ACaptureSessionOutputContainer {
    std::vector<const ACaptureSessionOutput *> outputs{};
};

struct ACameraMetadata {
    int32_t facing{ACAMERA_LENS_FACING_BACK};
    int32_t sensor_size[2]{1920, 1080};
    int32_t orientation{90};
};

struct AImageReader {
    std::unique_ptr<portage::camera::ImageReaderImpl> impl{nullptr};
};

struct AImage {
    portage::camera::InternalImage *internal_image{nullptr};
    portage::camera::ImageReaderImpl *owner{nullptr};
};
