#include "binder/binder_ndk.hpp"
#include "binder/binder_parcel.hpp"
#include "binder/binder_status.hpp"
#include "binder/service_manager.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "package/jni_env.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace portage::binder {

class BinderExpandedTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);
        ServiceManager::Instance().Reset();
    }

    void TearDown() override
    {
        ServiceManager::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        as_.reset();
    }

    std::unique_ptr<mem::AddressSpace> as_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

static __attribute__((sysv_abi)) binder_status_t DummyTransact(AIBinder *binder,
                                                               transaction_code_t code,
                                                               const AParcel *in, AParcel *out)
{
    (void)binder;
    (void)code;
    (void)in;
    if (out) {
        AParcel_writeInt32(out, 123);
    }
    return STATUS_OK;
}

static std::atomic<int> g_death_calls{0};
static void *g_last_cookie{nullptr};
static std::mutex g_death_mutex;
static std::condition_variable g_death_cv;

static __attribute__((sysv_abi)) void OnTestBinderDied(void *cookie)
{
    std::lock_guard<std::mutex> lock(g_death_mutex);
    g_last_cookie = cookie;
    g_death_calls.fetch_add(1);
    g_death_cv.notify_all();
}

TEST_F(BinderExpandedTest, DeathRecipientLifecycleAndAsyncNotification)
{
    g_death_calls.store(0);
    g_last_cookie = nullptr;

    auto *clazz = AIBinder_Class_define("test.IDeathInterface", nullptr, nullptr, &DummyTransact);
    ASSERT_NE(clazz, nullptr);

    AIBinder *binder = AIBinder_new(clazz, nullptr);
    ASSERT_NE(binder, nullptr);
    EXPECT_TRUE(AIBinder_isAlive(binder));

    AIBinder_DeathRecipient *recipient = AIBinder_DeathRecipient_new(&OnTestBinderDied);
    ASSERT_NE(recipient, nullptr);

    int cookie_val = 0xbeef;
    void *cookie = &cookie_val;
    EXPECT_EQ(AIBinder_linkToDeath(binder, recipient, cookie), STATUS_OK);

    // Linking again with same recipient & cookie returns STATUS_ALREADY_EXISTS
    EXPECT_EQ(AIBinder_linkToDeath(binder, recipient, cookie), STATUS_ALREADY_EXISTS);

    // Destroying the binder triggers async death notification
    AIBinder_decStrong(binder);

    {
        std::unique_lock<std::mutex> lock(g_death_mutex);
        bool received = g_death_cv.wait_for(lock, std::chrono::seconds(2),
                                            [] { return g_death_calls.load() == 1; });
        EXPECT_TRUE(received);
        EXPECT_EQ(g_last_cookie, cookie);
    }

    AIBinder_DeathRecipient_delete(recipient);
    delete clazz;
}

TEST_F(BinderExpandedTest, UnlinkToDeathPreventsNotification)
{
    g_death_calls.store(0);
    g_last_cookie = nullptr;

    auto *clazz = AIBinder_Class_define("test.IUnlinkInterface", nullptr, nullptr, &DummyTransact);
    ASSERT_NE(clazz, nullptr);

    AIBinder *binder = AIBinder_new(clazz, nullptr);
    ASSERT_NE(binder, nullptr);

    AIBinder_DeathRecipient *recipient = AIBinder_DeathRecipient_new(&OnTestBinderDied);
    ASSERT_NE(recipient, nullptr);

    int cookie_val = 0xcafe;
    void *cookie = &cookie_val;
    EXPECT_EQ(AIBinder_linkToDeath(binder, recipient, cookie), STATUS_OK);

    // Unlink before destruction
    EXPECT_EQ(AIBinder_unlinkToDeath(binder, recipient, cookie), STATUS_OK);

    // Unlink non-existent link returns STATUS_NAME_NOT_FOUND
    EXPECT_EQ(AIBinder_unlinkToDeath(binder, recipient, cookie), STATUS_NAME_NOT_FOUND);

    AIBinder_decStrong(binder);

    // Give time to ensure no spurious callback occurs
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(g_death_calls.load(), 0);

    AIBinder_DeathRecipient_delete(recipient);
    delete clazz;
}

TEST_F(BinderExpandedTest, PingAndExtensionAPIs)
{
    auto *clazz = AIBinder_Class_define("test.IPingInterface", nullptr, nullptr, &DummyTransact);
    ASSERT_NE(clazz, nullptr);

    AIBinder *binder = AIBinder_new(clazz, nullptr);
    ASSERT_NE(binder, nullptr);

    EXPECT_EQ(AIBinder_ping(binder), STATUS_OK);
    EXPECT_EQ(AIBinder_ping(nullptr), STATUS_UNEXPECTED_NULL);

    AIBinder *ext_binder = AIBinder_new(clazz, nullptr);
    EXPECT_EQ(AIBinder_setExtension(binder, ext_binder), STATUS_OK);

    AIBinder *retrieved_ext = nullptr;
    EXPECT_EQ(AIBinder_getExtension(binder, &retrieved_ext), STATUS_OK);
    EXPECT_EQ(retrieved_ext, ext_binder);

    AIBinder_decStrong(ext_binder);
    AIBinder_decStrong(binder);
    delete clazz;
}

static std::atomic<int> g_notif_calls{0};
static std::string g_last_notif_instance{};
static void *g_last_notif_cookie{nullptr};

static __attribute__((sysv_abi)) void OnTestServiceNotification(const char *instance, void *cookie)
{
    g_notif_calls.fetch_add(1);
    g_last_notif_instance = instance ? instance : "";
    g_last_notif_cookie = cookie;
}

TEST_F(BinderExpandedTest, ServiceManagerIsDeclaredCheck)
{
    EXPECT_TRUE(AServiceManager_isDeclared("surfaceflinger"));
    EXPECT_TRUE(AServiceManager_isDeclared("power"));
    EXPECT_TRUE(AServiceManager_isDeclared("android.hardware.graphics.composer/default"));
    EXPECT_FALSE(AServiceManager_isDeclared("nonexistent.bogus.service"));
    EXPECT_FALSE(AServiceManager_isDeclared(nullptr));
}

TEST_F(BinderExpandedTest, ServiceManagerNotificationsImmediateAndReactive)
{
    g_notif_calls.store(0);
    g_last_notif_instance.clear();
    g_last_notif_cookie = nullptr;

    int cookie1 = 111;
    void *reg1 =
        AServiceManager_registerForNotifications("power", &OnTestServiceNotification, &cookie1);
    ASSERT_NE(reg1, nullptr);
    EXPECT_EQ(g_notif_calls.load(), 1);
    EXPECT_EQ(g_last_notif_instance, "power");
    EXPECT_EQ(g_last_notif_cookie, &cookie1);

    AServiceManager_unregisterForNotifications(reg1);

    // Register for a future service
    int cookie2 = 222;
    void *reg2 = AServiceManager_registerForNotifications("test.future.service",
                                                          &OnTestServiceNotification, &cookie2);
    ASSERT_NE(reg2, nullptr);
    EXPECT_EQ(g_notif_calls.load(), 1); // No new call yet

    auto *clazz = AIBinder_Class_define("test.IFuture", nullptr, nullptr, &DummyTransact);
    AIBinder *b = AIBinder_new(clazz, nullptr);

    EXPECT_EQ(AServiceManager_addService(b, "test.future.service"), STATUS_OK);
    EXPECT_EQ(g_notif_calls.load(), 2);
    EXPECT_EQ(g_last_notif_instance, "test.future.service");
    EXPECT_EQ(g_last_notif_cookie, &cookie2);

    AServiceManager_unregisterForNotifications(reg2);

    // Adding again should not invoke callback since it was unregistered
    EXPECT_EQ(AServiceManager_addService(b, "test.future.service"), STATUS_OK);
    EXPECT_EQ(g_notif_calls.load(), 2);

    AIBinder_decStrong(b);
    delete clazz;
}

TEST_F(BinderExpandedTest, SyntheticServicesRegistrationAndTransactions)
{
    // ActivityManager
    AIBinder *activity = AServiceManager_checkService("activity");
    ASSERT_NE(activity, nullptr);
    EXPECT_EQ(AIBinder_ping(activity), STATUS_OK);

    AParcel *in = AParcel_create();
    AParcel *out = AParcel_create();
    // isLowRamDevice (code 1)
    EXPECT_EQ(AIBinder_transact(activity, 1, in, out, 0), STATUS_OK);
    int32_t status = -1;
    bool is_low_ram = true;
    EXPECT_EQ(AParcel_readInt32(out, &status), STATUS_OK);
    EXPECT_EQ(status, 0);
    EXPECT_EQ(AParcel_readBool(out, &is_low_ram), STATUS_OK);
    EXPECT_FALSE(is_low_ram);

    // DisplayManager
    AIBinder *display = AServiceManager_checkService("display");
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(AIBinder_ping(display), STATUS_OK);
    // getDisplayInfo (code 2)
    AParcel_reset(in);
    AParcel_reset(out);
    EXPECT_EQ(AIBinder_transact(display, 2, in, out, 0), STATUS_OK);
    EXPECT_EQ(AParcel_readInt32(out, &status), STATUS_OK);
    EXPECT_EQ(status, 0);
    int32_t width = 0;
    int32_t height = 0;
    float fps = 0.0f;
    EXPECT_EQ(AParcel_readInt32(out, &width), STATUS_OK);
    EXPECT_EQ(AParcel_readInt32(out, &height), STATUS_OK);
    EXPECT_EQ(AParcel_readFloat(out, &fps), STATUS_OK);
    EXPECT_EQ(width, 1920);
    EXPECT_EQ(height, 1080);
    EXPECT_EQ(fps, 60.0f);

    // WindowManager
    AIBinder *window = AServiceManager_checkService("window");
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(AIBinder_ping(window), STATUS_OK);
    // getDefaultDisplayRotation (code 1)
    AParcel_reset(in);
    AParcel_reset(out);
    EXPECT_EQ(AIBinder_transact(window, 1, in, out, 0), STATUS_OK);
    EXPECT_EQ(AParcel_readInt32(out, &status), STATUS_OK);
    EXPECT_EQ(status, 0);
    int32_t rotation = -1;
    EXPECT_EQ(AParcel_readInt32(out, &rotation), STATUS_OK);
    EXPECT_EQ(rotation, 0);

    // AudioService
    AIBinder *audio = AServiceManager_checkService("audio");
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(AIBinder_ping(audio), STATUS_OK);
    // getStreamVolume (code 1)
    AParcel_reset(in);
    AParcel_reset(out);
    EXPECT_EQ(AIBinder_transact(audio, 1, in, out, 0), STATUS_OK);
    EXPECT_EQ(AParcel_readInt32(out, &status), STATUS_OK);
    EXPECT_EQ(status, 0);
    int32_t volume = 0;
    EXPECT_EQ(AParcel_readInt32(out, &volume), STATUS_OK);
    EXPECT_EQ(volume, 100);

    AParcel_delete(in);
    AParcel_delete(out);
    AIBinder_decStrong(activity);
    AIBinder_decStrong(display);
    AIBinder_decStrong(window);
    AIBinder_decStrong(audio);
}

TEST_F(BinderExpandedTest, JavaParcelAndServiceManagerReflectionBridge)
{
    auto &jni = package::JniEnvironment::Instance();
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    package::jclass parcel_class = env->functions->FindClass(env, "android/os/Parcel");
    ASSERT_NE(parcel_class, nullptr);

    package::jmethodID obtain_mid =
        env->functions->GetStaticMethodID(env, parcel_class, "obtain", "()Landroid/os/Parcel;");
    ASSERT_NE(obtain_mid, nullptr);

    package::jobject java_parcel =
        env->functions->CallStaticObjectMethod(env, parcel_class, obtain_mid);
    ASSERT_NE(java_parcel, nullptr);

    AParcel *native_p = AParcel_fromJavaParcel(env, java_parcel);
    ASSERT_NE(native_p, nullptr);

    EXPECT_EQ(AParcel_writeInt32(native_p, 42), STATUS_OK);
    EXPECT_EQ(AParcel_writeString(native_p, "Hello Portage", -1), STATUS_OK);

    package::jmethodID set_pos_mid =
        env->functions->GetMethodID(env, parcel_class, "setDataPosition", "(I)V");
    ASSERT_NE(set_pos_mid, nullptr);
    env->functions->CallVoidMethod(env, java_parcel, set_pos_mid, 0);

    package::jmethodID read_int_mid =
        env->functions->GetMethodID(env, parcel_class, "readInt", "()I");
    ASSERT_NE(read_int_mid, nullptr);
    package::jint val = env->functions->CallIntMethod(env, java_parcel, read_int_mid);
    EXPECT_EQ(val, 42);

    package::jmethodID read_str_mid =
        env->functions->GetMethodID(env, parcel_class, "readString", "()Ljava/lang/String;");
    ASSERT_NE(read_str_mid, nullptr);
    package::jstring jstr = env->functions->CallObjectMethod(env, java_parcel, read_str_mid);
    ASSERT_NE(jstr, nullptr);
    const char *chars = env->functions->GetStringUTFChars(env, jstr, nullptr);
    EXPECT_STREQ(chars, "Hello Portage");
    env->functions->ReleaseStringUTFChars(env, jstr, chars);

    // Convert native parcel back to Java Parcel
    package::jobject roundtrip_parcel = AParcel_toJavaParcel(native_p, env);
    ASSERT_NE(roundtrip_parcel, nullptr);
    EXPECT_EQ(AParcel_fromJavaParcel(env, roundtrip_parcel), native_p);

    // ServiceManager.getService("surfaceflinger")
    package::jclass sm_class = env->functions->FindClass(env, "android/os/ServiceManager");
    ASSERT_NE(sm_class, nullptr);
    package::jmethodID get_svc_mid = env->functions->GetStaticMethodID(
        env, sm_class, "getService", "(Ljava/lang/String;)Landroid/os/IBinder;");
    ASSERT_NE(get_svc_mid, nullptr);

    package::jstring sf_name = env->functions->NewStringUTF(env, "surfaceflinger");
    package::jobject binder_obj =
        env->functions->CallStaticObjectMethod(env, sm_class, get_svc_mid, sf_name);
    ASSERT_NE(binder_obj, nullptr);

    auto binder_syn = jni.LookupObject(binder_obj);
    ASSERT_NE(binder_syn, nullptr);
    EXPECT_NE(binder_syn->native_ptr, nullptr);

    // Recycle parcel
    package::jmethodID recycle_mid =
        env->functions->GetMethodID(env, parcel_class, "recycle", "()V");
    ASSERT_NE(recycle_mid, nullptr);
    env->functions->CallVoidMethod(env, java_parcel, recycle_mid);
}

} // namespace portage::binder
