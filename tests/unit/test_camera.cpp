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
    media_status_t mstatus =
        AImageReader_new(640, 480, AIMAGE_FORMAT_RGBA_8888, 3, &reader);
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
    media_status_t mstatus =
        AImageReader_new(320, 240, AIMAGE_FORMAT_YUV_420_888, 2, &reader);
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
}
