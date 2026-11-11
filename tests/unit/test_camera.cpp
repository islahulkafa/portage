#include "camera/camera_manager.hpp"
#include "camera/camera_ndk.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>

TEST(CameraTest, CameraManagerDeviceEnumeration)
{
    auto &mgr = portage::camera::CameraManager::Instance();
    mgr.SetForceSynthetic(true);
    mgr.Initialize();

    auto ids = mgr.GetCameraIds();
    ASSERT_FALSE(ids.empty());
    EXPECT_EQ(ids[0], "0");

    const auto *info0 = mgr.GetDeviceInfo("0");
    ASSERT_NE(info0, nullptr);
    EXPECT_EQ(info0->facing, ACAMERA_LENS_FACING_BACK);
    EXPECT_EQ(info0->width, 1920);
    EXPECT_EQ(info0->height, 1080);
    EXPECT_EQ(info0->orientation, 90);

    const auto *info1 = mgr.GetDeviceInfo("1");
    ASSERT_NE(info1, nullptr);
    EXPECT_EQ(info1->facing, ACAMERA_LENS_FACING_FRONT);
    EXPECT_EQ(info1->width, 1280);
    EXPECT_EQ(info1->height, 720);
    EXPECT_EQ(info1->orientation, 270);
}

TEST(CameraTest, CameraManagerOpenAndClose)
{
    ACameraManager *mgr = ACameraManager_create();
    ASSERT_NE(mgr, nullptr);

    ACameraIdList *id_list = nullptr;
    camera_status_t status = ACameraManager_getCameraIdList(mgr, &id_list);
    EXPECT_EQ(status, ACAMERA_OK);
    ASSERT_NE(id_list, nullptr);
    EXPECT_GT(id_list->numCameras, 0);

    ACameraMetadata *chars = nullptr;
    status = ACameraManager_getCameraCharacteristics(mgr, id_list->cameraIds[0], &chars);
    EXPECT_EQ(status, ACAMERA_OK);
    ASSERT_NE(chars, nullptr);

    ACameraMetadata_const_entry entry{};
    status = ACameraMetadata_getConstEntry(chars, ACAMERA_LENS_FACING, &entry);
    EXPECT_EQ(status, ACAMERA_OK);
    EXPECT_EQ(entry.tag, static_cast<uint32_t>(ACAMERA_LENS_FACING));
    ASSERT_NE(entry.data.u8, nullptr);
    EXPECT_EQ(*entry.data.u8, static_cast<uint8_t>(ACAMERA_LENS_FACING_BACK));

    ACameraDevice *device = nullptr;
    status = ACameraManager_openCamera(mgr, id_list->cameraIds[0], nullptr, &device);
    EXPECT_EQ(status, ACAMERA_OK);
    ASSERT_NE(device, nullptr);
    EXPECT_STREQ(ACameraDevice_getId(device), "0");

    status = ACameraDevice_close(device);
    EXPECT_EQ(status, ACAMERA_OK);

    ACameraMetadata_free(chars);
    ACameraManager_deleteCameraIdList(id_list);
    ACameraManager_delete(mgr);
}

TEST(CameraTest, ImageReaderLifecycleAndPlanesRGBA)
{
    AImageReader *reader = nullptr;
    media_status_t mstatus = AImageReader_new(640, 480, AIMAGE_FORMAT_RGBA_8888, 3, &reader);
    EXPECT_EQ(mstatus, AMEDIA_OK);
    ASSERT_NE(reader, nullptr);

    ANativeWindow *win = nullptr;
    mstatus = AImageReader_getWindow(reader, &win);
    EXPECT_EQ(mstatus, AMEDIA_OK);
    ASSERT_NE(win, nullptr);

    ACameraManager *mgr = ACameraManager_create();
    ACameraDevice *device = nullptr;
    ACameraManager_openCamera(mgr, "0", nullptr, &device);
    ASSERT_NE(device, nullptr);

    ACaptureSessionOutputContainer *container = nullptr;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutput *out = nullptr;
    ACaptureSessionOutput_create(win, &out);
    ACaptureSessionOutputContainer_add(container, out);

    ACameraCaptureSession *session = nullptr;
    camera_status_t cstatus =
        ACameraDevice_createCaptureSession(device, container, nullptr, &session);
    EXPECT_EQ(cstatus, ACAMERA_OK);
    ASSERT_NE(session, nullptr);

    ACaptureRequest *request = nullptr;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &request);
    ASSERT_NE(request, nullptr);

    ACameraOutputTarget *target = nullptr;
    ACameraOutputTarget_create(win, &target);
    ACaptureRequest_addTarget(request, target);

    int seq_id = 0;
    cstatus = ACameraCaptureSession_capture(session, nullptr, 1, &request, &seq_id);
    EXPECT_EQ(cstatus, ACAMERA_OK);

    AImage *image = nullptr;
    mstatus = AImageReader_acquireNextImage(reader, &image);
    EXPECT_EQ(mstatus, AMEDIA_OK);
    ASSERT_NE(image, nullptr);

    int32_t width = 0;
    int32_t height = 0;
    int32_t format = 0;
    int64_t timestamp = 0;
    int32_t num_planes = 0;
    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);
    AImage_getFormat(image, &format);
    AImage_getTimestamp(image, &timestamp);
    AImage_getNumberOfPlanes(image, &num_planes);

    EXPECT_EQ(width, 640);
    EXPECT_EQ(height, 480);
    EXPECT_EQ(format, AIMAGE_FORMAT_RGBA_8888);
    EXPECT_GT(timestamp, 0);
    EXPECT_EQ(num_planes, 1);

    int32_t pixel_stride = 0;
    int32_t row_stride = 0;
    uint8_t *data = nullptr;
    int data_len = 0;
    AImage_getPlanePixelStride(image, 0, &pixel_stride);
    AImage_getPlaneRowStride(image, 0, &row_stride);
    AImage_getPlaneData(image, 0, &data, &data_len);

    EXPECT_EQ(pixel_stride, 4);
    EXPECT_EQ(row_stride, 640 * 4);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data_len, 640 * 480 * 4);

    AImage_delete(image);

    ACameraCaptureSession_close(session);
    ACaptureRequest_removeTarget(request, target);
    ACameraOutputTarget_free(target);
    ACaptureRequest_free(request);
    ACaptureSessionOutputContainer_remove(container, out);
    ACaptureSessionOutput_free(out);
    ACaptureSessionOutputContainer_free(container);
    ACameraDevice_close(device);
    ACameraManager_delete(mgr);
    AImageReader_delete(reader);
}

TEST(CameraTest, ImageReaderYuvPlanes)
{
    AImageReader *reader = nullptr;
    media_status_t mstatus = AImageReader_new(320, 240, AIMAGE_FORMAT_YUV_420_888, 2, &reader);
    EXPECT_EQ(mstatus, AMEDIA_OK);
    ASSERT_NE(reader, nullptr);

    ANativeWindow *win = nullptr;
    AImageReader_getWindow(reader, &win);
    ASSERT_NE(win, nullptr);

    ACameraManager *mgr = ACameraManager_create();
    ACameraDevice *device = nullptr;
    ACameraManager_openCamera(mgr, "0", nullptr, &device);

    ACaptureSessionOutputContainer *container = nullptr;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutput *out = nullptr;
    ACaptureSessionOutput_create(win, &out);
    ACaptureSessionOutputContainer_add(container, out);

    ACameraCaptureSession *session = nullptr;
    ACameraDevice_createCaptureSession(device, container, nullptr, &session);

    ACaptureRequest *request = nullptr;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &request);
    ACameraOutputTarget *target = nullptr;
    ACameraOutputTarget_create(win, &target);
    ACaptureRequest_addTarget(request, target);

    int seq_id = 0;
    ACameraCaptureSession_capture(session, nullptr, 1, &request, &seq_id);

    AImage *image = nullptr;
    mstatus = AImageReader_acquireLatestImage(reader, &image);
    EXPECT_EQ(mstatus, AMEDIA_OK);
    ASSERT_NE(image, nullptr);

    int32_t num_planes = 0;
    AImage_getNumberOfPlanes(image, &num_planes);
    EXPECT_EQ(num_planes, 3);

    int32_t y_row_stride = 0;
    uint8_t *y_data = nullptr;
    int y_len = 0;
    AImage_getPlaneRowStride(image, 0, &y_row_stride);
    AImage_getPlaneData(image, 0, &y_data, &y_len);
    EXPECT_EQ(y_row_stride, 320);
    EXPECT_EQ(y_len, 320 * 240);

    int32_t u_row_stride = 0;
    uint8_t *u_data = nullptr;
    int u_len = 0;
    AImage_getPlaneRowStride(image, 1, &u_row_stride);
    AImage_getPlaneData(image, 1, &u_data, &u_len);
    EXPECT_EQ(u_row_stride, 160);
    EXPECT_EQ(u_len, 160 * 120);

    AImage_delete(image);

    ACameraCaptureSession_close(session);
    ACaptureRequest_removeTarget(request, target);
    ACameraOutputTarget_free(target);
    ACaptureRequest_free(request);
    ACaptureSessionOutputContainer_remove(container, out);
    ACaptureSessionOutput_free(out);
    ACaptureSessionOutputContainer_free(container);
    ACameraDevice_close(device);
    ACameraManager_delete(mgr);
    AImageReader_delete(reader);
}

TEST(CameraTest, SyntheticCameraJni)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jclass context_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/Context"));
    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject context_obj = jenv->functions->NewObject(jenv, context_cls, ctor_mid);
    ASSERT_NE(context_obj, nullptr);

    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jstring camera_str = jenv->functions->NewStringUTF(jenv, "camera");
    jobject camera_mgr =
        jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, camera_str);
    ASSERT_NE(camera_mgr, nullptr);

    jmethodID get_ids_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getCameraIdList"));
    jobject id_list_obj = jenv->functions->CallObjectMethod(jenv, camera_mgr, get_ids_mid);
    ASSERT_NE(id_list_obj, nullptr);

    jsize arr_len = jenv->functions->GetArrayLength(jenv, reinterpret_cast<jarray>(id_list_obj));
    EXPECT_GE(arr_len, 2);

    jmethodID get_chars_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getCameraCharacteristics"));
    jstring cam0_str = jenv->functions->NewStringUTF(jenv, "0");
    jobject chars_obj =
        jenv->functions->CallObjectMethod(jenv, camera_mgr, get_chars_mid, cam0_str);
    ASSERT_NE(chars_obj, nullptr);

    jmethodID get_mid = reinterpret_cast<jmethodID>(const_cast<char *>("get"));
    jobject val_obj = jenv->functions->CallObjectMethod(jenv, chars_obj, get_mid, nullptr);
    ASSERT_NE(val_obj, nullptr);

    jmethodID int_val_mid = reinterpret_cast<jmethodID>(const_cast<char *>("intValue"));
    jint facing = jenv->functions->CallIntMethod(jenv, val_obj, int_val_mid);
    EXPECT_EQ(facing, ACAMERA_LENS_FACING_BACK);
}

TEST(CameraTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("ACameraManager_create"), 0u);
    EXPECT_NE(registry.Lookup("ACameraManager_openCamera"), 0u);
    EXPECT_NE(registry.Lookup("ACameraDevice_createCaptureSession"), 0u);
    EXPECT_NE(registry.Lookup("ACaptureRequest_addTarget"), 0u);
    EXPECT_NE(registry.Lookup("ACameraCaptureSession_capture"), 0u);
    EXPECT_NE(registry.Lookup("AImageReader_new"), 0u);
    EXPECT_NE(registry.Lookup("AImageReader_acquireNextImage"), 0u);
    EXPECT_NE(registry.Lookup("AImage_getPlaneData"), 0u);
    EXPECT_NE(registry.Lookup("ACameraManager_registerAvailabilityCallback"), 0u);
    EXPECT_NE(registry.Lookup("ACameraManager_unregisterAvailabilityCallback"), 0u);
}

TEST(CameraTest, AvailabilityCallbackFiresInitialAndOpenCloseTransitions)
{
    struct AvailabilityTrace {
        std::vector<std::string> available{};
        std::vector<std::string> unavailable{};
    };

    auto on_available = [](void *ctx, const char *camera_id) __attribute__((sysv_abi)) {
        auto *trace = static_cast<AvailabilityTrace *>(ctx);
        if (trace && camera_id) {
            trace->available.emplace_back(camera_id);
        }
    };
    auto on_unavailable = [](void *ctx, const char *camera_id) __attribute__((sysv_abi)) {
        auto *trace = static_cast<AvailabilityTrace *>(ctx);
        if (trace && camera_id) {
            trace->unavailable.emplace_back(camera_id);
        }
    };

    portage::camera::CameraManager::Instance().SetForceSynthetic(true);
    ACameraManager *mgr = ACameraManager_create();
    ASSERT_NE(mgr, nullptr);

    ACameraIdList *id_list = nullptr;
    ASSERT_EQ(ACameraManager_getCameraIdList(mgr, &id_list), ACAMERA_OK);
    ASSERT_NE(id_list, nullptr);
    ASSERT_GE(id_list->numCameras, 1);
    std::string target_id = id_list->cameraIds[0];

    AvailabilityTrace trace;
    ACameraManager_AvailabilityCallbacks cb{};
    cb.context = &trace;
    cb.onCameraAvailable = on_available;
    cb.onCameraUnavailable = on_unavailable;

    EXPECT_EQ(ACameraManager_registerAvailabilityCallback(mgr, &cb), ACAMERA_OK);
    EXPECT_EQ(static_cast<int>(trace.available.size()), id_list->numCameras);
    EXPECT_TRUE(trace.unavailable.empty());

    trace.available.clear();
    ACameraDevice *device = nullptr;
    ASSERT_EQ(ACameraManager_openCamera(mgr, target_id.c_str(), nullptr, &device), ACAMERA_OK);
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(trace.unavailable.size(), 1u);
    EXPECT_EQ(trace.unavailable[0], target_id);
    EXPECT_TRUE(trace.available.empty());

    ASSERT_EQ(ACameraDevice_close(device), ACAMERA_OK);
    ASSERT_EQ(trace.available.size(), 1u);
    EXPECT_EQ(trace.available[0], target_id);

    EXPECT_EQ(ACameraManager_unregisterAvailabilityCallback(mgr, &cb), ACAMERA_OK);

    trace.available.clear();
    trace.unavailable.clear();
    ASSERT_EQ(ACameraManager_openCamera(mgr, target_id.c_str(), nullptr, &device), ACAMERA_OK);
    ASSERT_EQ(ACameraDevice_close(device), ACAMERA_OK);
    EXPECT_TRUE(trace.available.empty());
    EXPECT_TRUE(trace.unavailable.empty());

    ACameraManager_deleteCameraIdList(id_list);
    ACameraManager_delete(mgr);
    portage::camera::CameraManager::Instance().SetForceSynthetic(false);
}

TEST(CameraTest, ConvertsYuy2AndBgraAndScalesFrames)
{
    std::vector<uint8_t> bgra = {
        10, 20, 30, 255, // pixel (0,0): B=10, G=20, R=30
        40, 50, 60, 128, // pixel (1,0): B=40, G=50, R=60
        70, 80, 90, 200, // pixel (0,1)
        11, 22, 33, 255  // pixel (1,1)
    };
    std::vector<uint8_t> rgba;
    portage::camera::ConvertBgraToRgba(bgra.data(), 8, 2, 2, rgba);
    ASSERT_EQ(rgba.size(), 16u);
    EXPECT_EQ(rgba[0], 30u);
    EXPECT_EQ(rgba[1], 20u);
    EXPECT_EQ(rgba[2], 10u);
    EXPECT_EQ(rgba[3], 255u);
    EXPECT_EQ(rgba[4], 60u);
    EXPECT_EQ(rgba[5], 50u);
    EXPECT_EQ(rgba[6], 40u);
    EXPECT_EQ(rgba[7], 128u);

    std::vector<uint8_t> yuy2 = {
        235, 128, 235, 128 // Y0=235, U=128, Y1=235, V=128 -> near-white studio BT.601
    };
    std::vector<uint8_t> yuy2_rgba;
    portage::camera::ConvertYuy2ToRgba(yuy2.data(), 4, 2, 1, yuy2_rgba);
    ASSERT_EQ(yuy2_rgba.size(), 8u);
    EXPECT_GE(yuy2_rgba[0], 250u);
    EXPECT_GE(yuy2_rgba[1], 250u);
    EXPECT_GE(yuy2_rgba[2], 250u);
    EXPECT_EQ(yuy2_rgba[3], 255u);
    EXPECT_GE(yuy2_rgba[4], 250u);

    std::vector<uint8_t> scaled;
    portage::camera::ScaleRgbaFrame(rgba.data(), 2, 2, 4, 4, scaled);
    ASSERT_EQ(scaled.size(), 64u);
    EXPECT_EQ(scaled[0], 30u);
    EXPECT_EQ(scaled[3 * 4 + 0], 60u);

    std::vector<portage::camera::ImagePlaneBuffer> yuv_planes;
    portage::camera::ExtractYuv420PlanesFromRgba(scaled.data(), 4, 4, yuv_planes);
    ASSERT_EQ(yuv_planes.size(), 3u);
    EXPECT_EQ(yuv_planes[0].data.size(), 16u);
    EXPECT_EQ(yuv_planes[1].data.size(), 4u);
    EXPECT_EQ(yuv_planes[2].data.size(), 4u);
}

TEST(CameraTest, PhysicalModeFallsBackGracefullyWithoutHardware)
{
    auto &cam_mgr = portage::camera::CameraManager::Instance();
    cam_mgr.SetForceSynthetic(false);

    ACameraManager *mgr = ACameraManager_create();
    ASSERT_NE(mgr, nullptr);

    ACameraIdList *id_list = nullptr;
    ASSERT_EQ(ACameraManager_getCameraIdList(mgr, &id_list), ACAMERA_OK);
    ASSERT_GE(id_list->numCameras, 1);

    AImageReader *reader = nullptr;
    ASSERT_EQ(AImageReader_new(160, 120, AIMAGE_FORMAT_RGBA_8888, 2, &reader), AMEDIA_OK);
    ANativeWindow *win = nullptr;
    ASSERT_EQ(AImageReader_getWindow(reader, &win), AMEDIA_OK);

    ACameraDevice *device = nullptr;
    ASSERT_EQ(ACameraManager_openCamera(mgr, id_list->cameraIds[0], nullptr, &device), ACAMERA_OK);

    ACaptureSessionOutput *sess_out = nullptr;
    ACaptureSessionOutput_create(win, &sess_out);
    ACaptureSessionOutputContainer *container = nullptr;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutputContainer_add(container, sess_out);

    ACameraCaptureSession *session = nullptr;
    ASSERT_EQ(ACameraDevice_createCaptureSession(device, container, nullptr, &session), ACAMERA_OK);

    ACameraOutputTarget *target = nullptr;
    ACameraOutputTarget_create(win, &target);
    ACaptureRequest *req = nullptr;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &req);
    ACaptureRequest_addTarget(req, target);

    int seq_id = 0;
    ASSERT_EQ(ACameraCaptureSession_capture(session, nullptr, 1, &req, &seq_id), ACAMERA_OK);

    AImage *img = nullptr;
    ASSERT_EQ(AImageReader_acquireNextImage(reader, &img), AMEDIA_OK);
    ASSERT_NE(img, nullptr);

    uint8_t *data = nullptr;
    int len = 0;
    ASSERT_EQ(AImage_getPlaneData(img, 0, &data, &len), AMEDIA_OK);
    EXPECT_EQ(len, 160 * 120 * 4);

    AImage_delete(img);
    ACaptureRequest_free(req);
    ACameraOutputTarget_free(target);
    ACameraCaptureSession_close(session);
    ACaptureSessionOutputContainer_free(container);
    ACaptureSessionOutput_free(sess_out);
    ACameraDevice_close(device);
    AImageReader_delete(reader);
    ACameraManager_deleteCameraIdList(id_list);
    ACameraManager_delete(mgr);
    cam_mgr.SetForceSynthetic(false);
}

TEST(CameraTest, DirectNativeWindowTargetReceivesPreviewFrames)
{
    auto &cam_mgr = portage::camera::CameraManager::Instance();
    cam_mgr.SetForceSynthetic(true);

    ACameraManager *mgr = ACameraManager_create();
    ASSERT_NE(mgr, nullptr);

    ANativeWindow win;
    win.width = 64;
    win.height = 48;
    win.format = 1; // WINDOW_FORMAT_RGBA_8888
    int post_count = 0;
    win.on_post = [&post_count](ANativeWindow *) { ++post_count; };

    ACameraDevice *device = nullptr;
    ASSERT_EQ(ACameraManager_openCamera(mgr, "0", nullptr, &device), ACAMERA_OK);

    ACaptureSessionOutput *sess_out = nullptr;
    ASSERT_EQ(ACaptureSessionOutput_create(&win, &sess_out), ACAMERA_OK);
    ACaptureSessionOutputContainer *container = nullptr;
    ASSERT_EQ(ACaptureSessionOutputContainer_create(&container), ACAMERA_OK);
    ASSERT_EQ(ACaptureSessionOutputContainer_add(container, sess_out), ACAMERA_OK);

    ACameraCaptureSession *session = nullptr;
    ASSERT_EQ(ACameraDevice_createCaptureSession(device, container, nullptr, &session), ACAMERA_OK);

    ACameraOutputTarget *target = nullptr;
    ASSERT_EQ(ACameraOutputTarget_create(&win, &target), ACAMERA_OK);
    ACaptureRequest *req = nullptr;
    ASSERT_EQ(ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &req), ACAMERA_OK);
    ASSERT_EQ(ACaptureRequest_addTarget(req, target), ACAMERA_OK);

    int seq_id = 0;
    ASSERT_EQ(ACameraCaptureSession_capture(session, nullptr, 1, &req, &seq_id), ACAMERA_OK);

    EXPECT_EQ(post_count, 1);
    EXPECT_EQ(win.frame_index, 1u);
    ASSERT_EQ(win.frontbuffer.size(), static_cast<size_t>(64 * 48 * 4));
    EXPECT_EQ(win.frontbuffer[0], 255u);
    EXPECT_EQ(win.frontbuffer[1], 255u);
    EXPECT_EQ(win.frontbuffer[2], 255u);
    EXPECT_EQ(win.frontbuffer[3], 255u);

    ACaptureRequest_free(req);
    ACameraOutputTarget_free(target);
    ACameraCaptureSession_close(session);
    ACaptureSessionOutputContainer_free(container);
    ACaptureSessionOutput_free(sess_out);
    ACameraDevice_close(device);
    ACameraManager_delete(mgr);
    cam_mgr.SetForceSynthetic(false);
}

TEST(CameraTest, ImageReaderCapturesValidWicJpeg)
{
    std::vector<uint8_t> solid_rgba(16 * 16 * 4, 255);
    std::vector<uint8_t> direct_jpeg;
    EXPECT_TRUE(portage::camera::EncodeRgbaToJpeg(solid_rgba.data(), 16, 16, direct_jpeg));
    ASSERT_GE(direct_jpeg.size(), 4u);
    EXPECT_EQ(direct_jpeg[0], 0xFFu);
    EXPECT_EQ(direct_jpeg[1], 0xD8u);
    EXPECT_EQ(direct_jpeg[direct_jpeg.size() - 2], 0xFFu);
    EXPECT_EQ(direct_jpeg[direct_jpeg.size() - 1], 0xD9u);

    auto &cam_mgr = portage::camera::CameraManager::Instance();
    cam_mgr.SetForceSynthetic(true);

    ACameraManager *mgr = ACameraManager_create();
    ASSERT_NE(mgr, nullptr);

    AImageReader *reader = nullptr;
    ASSERT_EQ(AImageReader_new(160, 120, AIMAGE_FORMAT_JPEG, 2, &reader), AMEDIA_OK);
    ANativeWindow *win = nullptr;
    ASSERT_EQ(AImageReader_getWindow(reader, &win), AMEDIA_OK);

    ACameraDevice *device = nullptr;
    ASSERT_EQ(ACameraManager_openCamera(mgr, "0", nullptr, &device), ACAMERA_OK);

    ACaptureSessionOutput *sess_out = nullptr;
    ACaptureSessionOutput_create(win, &sess_out);
    ACaptureSessionOutputContainer *container = nullptr;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutputContainer_add(container, sess_out);

    ACameraCaptureSession *session = nullptr;
    ASSERT_EQ(ACameraDevice_createCaptureSession(device, container, nullptr, &session), ACAMERA_OK);

    ACameraOutputTarget *target = nullptr;
    ACameraOutputTarget_create(win, &target);
    ACaptureRequest *req = nullptr;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_STILL_CAPTURE, &req);
    ACaptureRequest_addTarget(req, target);

    int seq_id = 0;
    ASSERT_EQ(ACameraCaptureSession_capture(session, nullptr, 1, &req, &seq_id), ACAMERA_OK);

    AImage *img = nullptr;
    ASSERT_EQ(AImageReader_acquireNextImage(reader, &img), AMEDIA_OK);
    ASSERT_NE(img, nullptr);

    int32_t fmt = 0;
    int32_t planes = 0;
    EXPECT_EQ(AImage_getFormat(img, &fmt), AMEDIA_OK);
    EXPECT_EQ(fmt, AIMAGE_FORMAT_JPEG);
    EXPECT_EQ(AImage_getNumberOfPlanes(img, &planes), AMEDIA_OK);
    EXPECT_EQ(planes, 1);

    uint8_t *jpeg_data = nullptr;
    int jpeg_len = 0;
    ASSERT_EQ(AImage_getPlaneData(img, 0, &jpeg_data, &jpeg_len), AMEDIA_OK);
    ASSERT_GE(jpeg_len, 4);
    EXPECT_EQ(jpeg_data[0], 0xFFu);
    EXPECT_EQ(jpeg_data[1], 0xD8u);
    EXPECT_EQ(jpeg_data[jpeg_len - 2], 0xFFu);
    EXPECT_EQ(jpeg_data[jpeg_len - 1], 0xD9u);

    AImage_delete(img);
    ACaptureRequest_free(req);
    ACameraOutputTarget_free(target);
    ACameraCaptureSession_close(session);
    ACaptureSessionOutputContainer_free(container);
    ACaptureSessionOutput_free(sess_out);
    ACameraDevice_close(device);
    AImageReader_delete(reader);
    ACameraManager_delete(mgr);
    cam_mgr.SetForceSynthetic(false);
}

TEST(CameraTest, FiresFullCaptureAndSequenceCallbacks)
{
    struct CallbackTrace {
        int ready_count{0};
        int active_count{0};
        int closed_count{0};
        int started_count{0};
        int completed_count{0};
        int seq_completed_count{0};
        int seq_aborted_count{0};
        int last_completed_seq{-1};
        int last_aborted_seq{-1};
        int64_t last_frame_number{-1};
        int64_t last_started_ts{0};
    };

    auto on_ready = [](void *ctx, ACameraCaptureSession *) __attribute__((sysv_abi)) {
        ++static_cast<CallbackTrace *>(ctx)->ready_count;
    };
    auto on_active = [](void *ctx, ACameraCaptureSession *) __attribute__((sysv_abi)) {
        ++static_cast<CallbackTrace *>(ctx)->active_count;
    };
    auto on_closed = [](void *ctx, ACameraCaptureSession *) __attribute__((sysv_abi)) {
        ++static_cast<CallbackTrace *>(ctx)->closed_count;
    };
    auto on_started = [](void *ctx, ACameraCaptureSession *, const ACaptureRequest *, int64_t ts)
                          __attribute__((sysv_abi)) {
                              auto *t = static_cast<CallbackTrace *>(ctx);
                              ++t->started_count;
                              t->last_started_ts = ts;
                          };
    auto on_completed =
        [](void *ctx, ACameraCaptureSession *, ACaptureRequest *, const ACameraMetadata *)
            __attribute__((sysv_abi)) { ++static_cast<CallbackTrace *>(ctx)->completed_count; };
    auto on_seq_completed = [](void *ctx, ACameraCaptureSession *, int seq_id, int64_t frame_num)
                                __attribute__((sysv_abi)) {
                                    auto *t = static_cast<CallbackTrace *>(ctx);
                                    ++t->seq_completed_count;
                                    t->last_completed_seq = seq_id;
                                    t->last_frame_number = frame_num;
                                };
    auto on_seq_aborted = [](void *ctx, ACameraCaptureSession *, int seq_id)
                              __attribute__((sysv_abi)) {
                                  auto *t = static_cast<CallbackTrace *>(ctx);
                                  ++t->seq_aborted_count;
                                  t->last_aborted_seq = seq_id;
                              };

    auto &cam_mgr = portage::camera::CameraManager::Instance();
    cam_mgr.SetForceSynthetic(true);

    ACameraManager *mgr = ACameraManager_create();
    AImageReader *reader = nullptr;
    ASSERT_EQ(AImageReader_new(64, 64, AIMAGE_FORMAT_RGBA_8888, 4, &reader), AMEDIA_OK);
    ANativeWindow *win = nullptr;
    AImageReader_getWindow(reader, &win);

    ACameraDevice *device = nullptr;
    ASSERT_EQ(ACameraManager_openCamera(mgr, "0", nullptr, &device), ACAMERA_OK);

    ACaptureSessionOutput *sess_out = nullptr;
    ACaptureSessionOutput_create(win, &sess_out);
    ACaptureSessionOutputContainer *container = nullptr;
    ACaptureSessionOutputContainer_create(&container);
    ACaptureSessionOutputContainer_add(container, sess_out);

    CallbackTrace trace;
    ACameraCaptureSession_stateCallbacks state_cb{};
    state_cb.context = &trace;
    state_cb.onReady = on_ready;
    state_cb.onActive = on_active;
    state_cb.onClosed = on_closed;

    ACameraCaptureSession *session = nullptr;
    ASSERT_EQ(ACameraDevice_createCaptureSession(device, container, &state_cb, &session),
              ACAMERA_OK);
    EXPECT_GE(trace.ready_count, 1);

    ACameraOutputTarget *target = nullptr;
    ACameraOutputTarget_create(win, &target);
    ACaptureRequest *req = nullptr;
    ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &req);
    ACaptureRequest_addTarget(req, target);

    ACameraCaptureSession_captureCallbacks cap_cb{};
    cap_cb.context = &trace;
    cap_cb.onCaptureStarted = on_started;
    cap_cb.onCaptureCompleted = on_completed;
    cap_cb.onCaptureSequenceCompleted = on_seq_completed;
    cap_cb.onCaptureSequenceAborted = on_seq_aborted;

    ACaptureRequest *burst[2] = {req, req};
    int burst_seq = 0;
    ASSERT_EQ(ACameraCaptureSession_capture(session, &cap_cb, 2, burst, &burst_seq), ACAMERA_OK);
    EXPECT_EQ(trace.started_count, 2);
    EXPECT_EQ(trace.completed_count, 2);
    EXPECT_GT(trace.last_started_ts, 0);
    EXPECT_EQ(trace.seq_completed_count, 1);
    EXPECT_EQ(trace.last_completed_seq, burst_seq);
    EXPECT_GE(trace.last_frame_number, 2);
    EXPECT_EQ(trace.seq_aborted_count, 0);

    int repeat_seq = 0;
    ASSERT_EQ(ACameraCaptureSession_setRepeatingRequest(session, &cap_cb, 1, &req, &repeat_seq),
              ACAMERA_OK);
    EXPECT_GE(trace.active_count, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(ACameraCaptureSession_abortCaptures(session), ACAMERA_OK);
    EXPECT_EQ(trace.seq_aborted_count, 1);
    EXPECT_EQ(trace.last_aborted_seq, repeat_seq);

    ACaptureRequest_free(req);
    ACameraOutputTarget_free(target);
    ACameraCaptureSession_close(session);
    EXPECT_EQ(trace.closed_count, 1);
    ACaptureSessionOutputContainer_free(container);
    ACaptureSessionOutput_free(sess_out);
    ACameraDevice_close(device);
    AImageReader_delete(reader);
    ACameraManager_delete(mgr);
    cam_mgr.SetForceSynthetic(false);
}
