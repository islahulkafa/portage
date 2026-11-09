#include "camera/camera_ndk.hpp"
#include "camera/camera_manager.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

using namespace portage::camera;

extern "C" {

__attribute__((sysv_abi)) ACameraManager *ACameraManager_create()
{
    auto *manager = new (std::nothrow) ACameraManager();
    if (manager) {
        manager->mgr = &CameraManager::Instance();
    }
    return manager;
}

__attribute__((sysv_abi)) void ACameraManager_delete(ACameraManager *manager)
{
    delete manager;
}

__attribute__((sysv_abi)) camera_status_t ACameraManager_getCameraIdList(ACameraManager *manager,
                                                                         ACameraIdList **cameraIdList)
{
    if (!manager || !cameraIdList) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *cameraIdList = nullptr;

    auto ids = CameraManager::Instance().GetCameraIds();
    auto *list = new (std::nothrow) ACameraIdList();
    if (!list) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }

    list->numCameras = static_cast<int>(ids.size());
    list->cameraIds = new (std::nothrow) const char *[ids.size()];
    if (!list->cameraIds) {
        delete list;
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        size_t len = ids[i].size();
        char *str = new (std::nothrow) char[len + 1];
        if (str) {
            std::memcpy(str, ids[i].c_str(), len + 1);
        }
        list->cameraIds[i] = str;
    }

    *cameraIdList = list;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) void ACameraManager_deleteCameraIdList(ACameraIdList *cameraIdList)
{
    if (!cameraIdList) {
        return;
    }
    if (cameraIdList->cameraIds) {
        for (int i = 0; i < cameraIdList->numCameras; ++i) {
            delete[] cameraIdList->cameraIds[i];
        }
        delete[] cameraIdList->cameraIds;
    }
    delete cameraIdList;
}

__attribute__((sysv_abi)) camera_status_t
ACameraManager_getCameraCharacteristics(ACameraManager *manager, const char *cameraId,
                                        ACameraMetadata **characteristics)
{
    if (!manager || !cameraId || !characteristics) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *characteristics = nullptr;

    const auto *info = CameraManager::Instance().GetDeviceInfo(cameraId);
    if (!info) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    auto *meta = new (std::nothrow) ACameraMetadata();
    if (!meta) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }

    meta->facing = info->facing;
    meta->sensor_size[0] = info->width;
    meta->sensor_size[1] = info->height;
    meta->orientation = info->orientation;

    *characteristics = meta;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACameraManager_openCamera(ACameraManager *manager, const char *cameraId,
                          ACameraDevice_StateCallbacks *callback, ACameraDevice **device)
{
    if (!manager) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    return CameraManager::Instance().OpenCamera(cameraId, callback, device);
}

__attribute__((sysv_abi)) camera_status_t
ACameraManager_registerAvailabilityCallback(ACameraManager *manager, const void *callback)
{
    (void)manager;
    (void)callback;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACameraManager_unregisterAvailabilityCallback(ACameraManager *manager, const void *callback)
{
    (void)manager;
    (void)callback;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACameraDevice_close(ACameraDevice *device)
{
    if (!device) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    delete device;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) const char *ACameraDevice_getId(const ACameraDevice *device)
{
    if (!device || !device->impl) {
        return "";
    }
    return device->impl->GetId().c_str();
}

__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureRequest(const ACameraDevice *device,
                                   ACameraDevice_request_template templateId,
                                   ACaptureRequest **request)
{
    if (!device || !request) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    auto *req = new (std::nothrow) ACaptureRequest();
    if (!req) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }
    req->template_id = templateId;
    *request = req;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureSession(ACameraDevice *device,
                                   const ACaptureSessionOutputContainer *outputs,
                                   const ACameraCaptureSession_stateCallbacks *callbacks,
                                   ACameraCaptureSession **session)
{
    if (!device || !device->impl || !outputs || !session) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *session = nullptr;

    std::vector<ANativeWindow *> targets;
    for (const auto *out : outputs->outputs) {
        if (out && out->window) {
            targets.push_back(out->window);
        }
    }

    auto sess_impl =
        std::make_unique<CaptureSessionImpl>(device->impl.get(), targets, callbacks);
    auto sess_wrapper = std::make_unique<ACameraCaptureSession>();
    sess_wrapper->impl = std::move(sess_impl);
    *session = sess_wrapper.release();
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACameraDevice_createCaptureSessionWithSessionParameters(
    ACameraDevice *device, const ACaptureSessionOutputContainer *outputs,
    const ACameraMetadata *sessionParameters,
    const ACameraCaptureSession_stateCallbacks *callbacks, ACameraCaptureSession **session)
{
    (void)sessionParameters;
    return ACameraDevice_createCaptureSession(device, outputs, callbacks, session);
}

__attribute__((sysv_abi)) void ACaptureRequest_free(ACaptureRequest *request)
{
    delete request;
}

__attribute__((sysv_abi)) camera_status_t ACaptureRequest_addTarget(ACaptureRequest *request,
                                                                    const ACameraOutputTarget *target)
{
    if (!request || !target) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    request->targets.push_back(target);
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureRequest_removeTarget(ACaptureRequest *request, const ACameraOutputTarget *target)
{
    if (!request || !target) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    for (auto it = request->targets.begin(); it != request->targets.end(); ++it) {
        if (*it == target) {
            request->targets.erase(it);
            break;
        }
    }
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureRequest_getAllOptions(const ACaptureRequest *request, ACameraMetadata **metadata)
{
    if (!request || !metadata) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    auto *meta = new (std::nothrow) ACameraMetadata();
    if (!meta) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }
    *metadata = meta;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_u8(ACaptureRequest *request,
                                                                      uint32_t tag, uint32_t count,
                                                                      const uint8_t *data)
{
    if (!request || !data) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    std::vector<uint8_t> val(data, data + count);
    request->entries[tag] = std::move(val);
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_i32(ACaptureRequest *request,
                                                                       uint32_t tag, uint32_t count,
                                                                       const int32_t *data)
{
    if (!request || !data) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    std::vector<uint8_t> val(bytes, bytes + count * sizeof(int32_t));
    request->entries[tag] = std::move(val);
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACaptureRequest_setEntry_float(ACaptureRequest *request,
                                                                         uint32_t tag,
                                                                         uint32_t count,
                                                                         const float *data)
{
    if (!request || !data) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    std::vector<uint8_t> val(bytes, bytes + count * sizeof(float));
    request->entries[tag] = std::move(val);
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACameraCaptureSession_close(ACameraCaptureSession *session)
{
    if (!session) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    delete session;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_getDevice(ACameraCaptureSession *session, ACameraDevice **device)
{
    if (!session || !session->impl || !device) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    *device = reinterpret_cast<ACameraDevice *>(session->impl->GetDevice());
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t ACameraCaptureSession_setRepeatingRequest(
    ACameraCaptureSession *session, ACameraCaptureSession_captureCallbacks *callbacks,
    int numRequests, ACaptureRequest **requests, int *captureSequenceId)
{
    if (!session || !session->impl) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    return session->impl->SetRepeatingRequest(callbacks, numRequests, requests, captureSequenceId);
}

__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_capture(ACameraCaptureSession *session,
                              ACameraCaptureSession_captureCallbacks *callbacks, int numRequests,
                              ACaptureRequest **requests, int *captureSequenceId)
{
    if (!session || !session->impl) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    return session->impl->Capture(callbacks, numRequests, requests, captureSequenceId);
}

__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_stopRepeating(ACameraCaptureSession *session)
{
    if (!session || !session->impl) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    return session->impl->StopRepeating();
}

__attribute__((sysv_abi)) camera_status_t
ACameraCaptureSession_abortCaptures(ACameraCaptureSession *session)
{
    if (!session || !session->impl) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    return session->impl->AbortCaptures();
}

__attribute__((sysv_abi)) camera_status_t ACameraOutputTarget_create(ANativeWindow *window,
                                                                     ACameraOutputTarget **output)
{
    if (!window || !output) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    auto *target = new (std::nothrow) ACameraOutputTarget();
    if (!target) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }
    target->window = window;
    *output = target;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) void ACameraOutputTarget_free(ACameraOutputTarget *output)
{
    delete output;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutput_create(ANativeWindow *window, ACaptureSessionOutput **output)
{
    if (!window || !output) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    auto *out = new (std::nothrow) ACaptureSessionOutput();
    if (!out) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }
    out->window = window;
    *output = out;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) void ACaptureSessionOutput_free(ACaptureSessionOutput *output)
{
    delete output;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer **container)
{
    if (!container) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    auto *cont = new (std::nothrow) ACaptureSessionOutputContainer();
    if (!cont) {
        return ACAMERA_ERROR_NOT_ENOUGH_MEMORY;
    }
    *container = cont;
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) void
ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer *container)
{
    delete container;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer *container,
                                   const ACaptureSessionOutput *output)
{
    if (!container || !output) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    container->outputs.push_back(output);
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) camera_status_t
ACaptureSessionOutputContainer_remove(ACaptureSessionOutputContainer *container,
                                      const ACaptureSessionOutput *output)
{
    if (!container || !output) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }
    for (auto it = container->outputs.begin(); it != container->outputs.end(); ++it) {
        if (*it == output) {
            container->outputs.erase(it);
            break;
        }
    }
    return ACAMERA_OK;
}

__attribute__((sysv_abi)) void ACameraMetadata_free(ACameraMetadata *metadata)
{
    delete metadata;
}

__attribute__((sysv_abi)) camera_status_t
ACameraMetadata_getConstEntry(const ACameraMetadata *metadata, uint32_t tag,
                              ACameraMetadata_const_entry *entry)
{
    if (!metadata || !entry) {
        return ACAMERA_ERROR_INVALID_PARAMETER;
    }

    if (tag == ACAMERA_LENS_FACING) {
        entry->tag = tag;
        entry->type = 0; // TYPE_BYTE
        entry->count = 1;
        static thread_local uint8_t s_facing = 0;
        s_facing = static_cast<uint8_t>(metadata->facing);
        entry->data.u8 = &s_facing;
        return ACAMERA_OK;
    } else if (tag == ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE) {
        entry->tag = tag;
        entry->type = 1; // TYPE_INT32
        entry->count = 2;
        entry->data.i32 = metadata->sensor_size;
        return ACAMERA_OK;
    } else if (tag == ACAMERA_SENSOR_ORIENTATION) {
        entry->tag = tag;
        entry->type = 1; // TYPE_INT32
        entry->count = 1;
        entry->data.i32 = &metadata->orientation;
        return ACAMERA_OK;
    }

    return ACAMERA_ERROR_METADATA_NOT_FOUND;
}

__attribute__((sysv_abi)) media_status_t AImageReader_new(int32_t width, int32_t height,
                                                          int32_t format, int32_t maxImages,
                                                          AImageReader **reader)
{
    if (width <= 0 || height <= 0 || !reader) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *reader = nullptr;

    auto impl = std::make_unique<ImageReaderImpl>(width, height, format, maxImages);
    auto wrap = std::make_unique<AImageReader>();
    wrap->impl = std::move(impl);
    *reader = wrap.release();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImageReader_newWithUsage(int32_t width, int32_t height,
                                                                   int32_t format, uint64_t usage,
                                                                   int32_t maxImages,
                                                                   AImageReader **reader)
{
    (void)usage;
    return AImageReader_new(width, height, format, maxImages, reader);
}

__attribute__((sysv_abi)) void AImageReader_delete(AImageReader *reader)
{
    delete reader;
}

__attribute__((sysv_abi)) media_status_t AImageReader_getWindow(AImageReader *reader,
                                                                ANativeWindow **window)
{
    if (!reader || !reader->impl || !window) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *window = reader->impl->GetNativeWindow();
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImageReader_acquireNextImage(AImageReader *reader,
                                                                       AImage **image)
{
    if (!reader || !reader->impl) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    return reader->impl->AcquireNextImage(image);
}

__attribute__((sysv_abi)) media_status_t AImageReader_acquireLatestImage(AImageReader *reader,
                                                                         AImage **image)
{
    if (!reader || !reader->impl) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    return reader->impl->AcquireLatestImage(image);
}

__attribute__((sysv_abi)) media_status_t
AImageReader_setImageListener(AImageReader *reader, AImageReader_ImageListener *listener)
{
    if (!reader || !reader->impl) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    reader->impl->SetListener(listener);
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) void AImage_delete(AImage *image)
{
    if (image && image->owner) {
        image->owner->ReleaseImage(image);
    } else {
        delete image;
    }
}

__attribute__((sysv_abi)) media_status_t AImage_getWidth(const AImage *image, int32_t *width)
{
    if (!image || !image->internal_image || !width) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *width = image->internal_image->width;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getHeight(const AImage *image, int32_t *height)
{
    if (!image || !image->internal_image || !height) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *height = image->internal_image->height;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getFormat(const AImage *image, int32_t *format)
{
    if (!image || !image->internal_image || !format) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *format = image->internal_image->format;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getTimestamp(const AImage *image,
                                                             int64_t *timestamp)
{
    if (!image || !image->internal_image || !timestamp) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *timestamp = image->internal_image->timestamp;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getNumberOfPlanes(const AImage *image,
                                                                  int32_t *numPlanes)
{
    if (!image || !image->internal_image || !numPlanes) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *numPlanes = static_cast<int32_t>(image->internal_image->planes.size());
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getPlanePixelStride(const AImage *image,
                                                                    int32_t planeIdx,
                                                                    int32_t *pixelStride)
{
    if (!image || !image->internal_image || !pixelStride) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    if (planeIdx < 0 || planeIdx >= static_cast<int32_t>(image->internal_image->planes.size())) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *pixelStride = image->internal_image->planes[planeIdx].pixel_stride;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getPlaneRowStride(const AImage *image,
                                                                  int32_t planeIdx,
                                                                  int32_t *rowStride)
{
    if (!image || !image->internal_image || !rowStride) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    if (planeIdx < 0 || planeIdx >= static_cast<int32_t>(image->internal_image->planes.size())) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *rowStride = image->internal_image->planes[planeIdx].row_stride;
    return AMEDIA_OK;
}

__attribute__((sysv_abi)) media_status_t AImage_getPlaneData(const AImage *image, int32_t planeIdx,
                                                             uint8_t **data, int *dataLength)
{
    if (!image || !image->internal_image || !data || !dataLength) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    if (planeIdx < 0 || planeIdx >= static_cast<int32_t>(image->internal_image->planes.size())) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    *data = image->internal_image->planes[planeIdx].data.data();
    *dataLength = static_cast<int>(image->internal_image->planes[planeIdx].data.size());
    return AMEDIA_OK;
}

} // extern "C"
