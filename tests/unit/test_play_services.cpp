#include "binder/binder_ndk.hpp"
#include "binder/binder_parcel.hpp"
#include "binder/binder_status.hpp"
#include "binder/play_billing_service.hpp"
#include "binder/play_licensing_service.hpp"
#include "binder/service_manager.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "package/jni_env.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace portage::binder {

namespace {

static bool __attribute__((sysv_abi)) StringAllocHelper(void *data, int32_t len, char **out_buf)
{
    if (len < 0) {
        return true;
    }
    auto *s = reinterpret_cast<std::string *>(data);
    s->resize(static_cast<size_t>(len));
    *out_buf = s->data();
    return true;
}

struct CallbackResult {
    std::mutex mtx{};
    std::condition_variable cv{};
    bool called{false};
    int32_t response_code{-1};
    std::string signed_data{};
    std::string signature{};
};

static binder_status_t __attribute__((sysv_abi))
MockListenerTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *)
{
    if (code == 1 && in) {
        auto *res = reinterpret_cast<CallbackResult *>(AIBinder_getUserData(binder));
        int32_t resp = -1;
        AParcel_readInt32(in, &resp);
        std::string signed_data;
        AParcel_readString(in, &signed_data, StringAllocHelper);
        std::string signature;
        AParcel_readString(in, &signature, StringAllocHelper);

        if (res) {
            std::lock_guard<std::mutex> lock(res->mtx);
            res->response_code = resp;
            res->signed_data = signed_data;
            res->signature = signature;
            res->called = true;
            res->cv.notify_all();
        }
        return STATUS_OK;
    }
    return STATUS_UNKNOWN_TRANSACTION;
}

} // namespace

class PlayServicesTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);

        ServiceManager::Instance().Reset();
        PlayLicensingService::Instance().Reset();
        PlayBillingService::Instance().Reset();

        package::JniEnvironment::Instance().Initialize("com.portage.gameengine",
                                                       "/data/data/com.portage.gameengine/files",
                                                       "/data/data/com.portage.gameengine/cache");
    }

    void TearDown() override
    {
        PlayLicensingService::Instance().Reset();
        PlayBillingService::Instance().Reset();
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

TEST_F(PlayServicesTest, LicensingServiceDispatchesVerificationCallback)
{
    AIBinder *lic_svc =
        ServiceManager::Instance().GetService("com.android.vending.licensing.ILicensingService");
    ASSERT_NE(lic_svc, nullptr);

    CallbackResult cb_result;

    AIBinder_Class *listener_class =
        AIBinder_Class_define("com.android.vending.licensing.ILicenseResultListener", nullptr,
                              nullptr, MockListenerTransact);
    ASSERT_NE(listener_class, nullptr);

    AIBinder *listener = AIBinder_new(listener_class, &cb_result);
    ASSERT_NE(listener, nullptr);

    AParcel *in = AParcel_create();
    AParcel *out = AParcel_create();

    int64_t nonce = 1234567890123LL;
    AParcel_writeInt64(in, nonce);
    AParcel_writeString(in, "com.portage.gameengine", -1);
    AParcel_writeStrongBinder(in, listener);

    binder_status_t status = AIBinder_transact(lic_svc, 1 /* checkLicense */, in, out, 0);
    EXPECT_EQ(status, STATUS_OK);

    {
        std::unique_lock<std::mutex> lock(cb_result.mtx);
        bool ok = cb_result.cv.wait_for(lock, std::chrono::seconds(3),
                                        [&cb_result] { return cb_result.called; });
        EXPECT_TRUE(ok);
    }

    EXPECT_EQ(cb_result.response_code, 0);
    EXPECT_NE(cb_result.signed_data.find("1234567890123"), std::string::npos);
    EXPECT_NE(cb_result.signed_data.find("com.portage.gameengine"), std::string::npos);
    EXPECT_FALSE(cb_result.signature.empty());

    AParcel_delete(in);
    AParcel_delete(out);
    AIBinder_decStrong(listener);
    AIBinder_decStrong(lic_svc);
    delete listener_class;
}

TEST_F(PlayServicesTest, LicensingServiceHonorsResponseCodeOverride)
{
    AIBinder *lic_svc =
        ServiceManager::Instance().GetService("com.android.vending.licensing.ILicensingService");
    ASSERT_NE(lic_svc, nullptr);

    PlayLicensingService::Instance().SetLicenseResponseCode(1);

    CallbackResult cb_result;

    AIBinder_Class *listener_class =
        AIBinder_Class_define("com.android.vending.licensing.ILicenseResultListener2", nullptr,
                              nullptr, MockListenerTransact);
    AIBinder *listener = AIBinder_new(listener_class, &cb_result);

    AParcel *in = AParcel_create();
    AParcel *out = AParcel_create();

    AParcel_writeInt64(in, 999LL);
    AParcel_writeString(in, "com.portage.gameengine", -1);
    AParcel_writeStrongBinder(in, listener);

    binder_status_t status = AIBinder_transact(lic_svc, 1, in, out, 0);
    EXPECT_EQ(status, STATUS_OK);

    {
        std::unique_lock<std::mutex> lock(cb_result.mtx);
        bool ok = cb_result.cv.wait_for(lock, std::chrono::seconds(3),
                                        [&cb_result] { return cb_result.called; });
        EXPECT_TRUE(ok);
    }

    EXPECT_EQ(cb_result.response_code, 1);
    EXPECT_NE(cb_result.signed_data.find("1|999|"), std::string::npos);

    AParcel_delete(in);
    AParcel_delete(out);
    AIBinder_decStrong(listener);
    AIBinder_decStrong(lic_svc);
    delete listener_class;
}

TEST_F(PlayServicesTest, BillingServiceSupportsBillingAndQueriesDetails)
{
    AIBinder *billing_svc =
        ServiceManager::Instance().GetService("com.android.vending.billing.IInAppBillingService");
    ASSERT_NE(billing_svc, nullptr);

    {
        AParcel *in = AParcel_create();
        AParcel *out = AParcel_create();

        AParcel_writeInt32(in, 3);
        AParcel_writeString(in, "com.portage.gameengine", -1);
        AParcel_writeString(in, "inapp", -1);

        EXPECT_EQ(AIBinder_transact(billing_svc, 1, in, out, 0), STATUS_OK);

        int32_t ex = -1;
        int32_t res = -1;
        EXPECT_EQ(AParcel_readInt32(out, &ex), STATUS_OK);
        EXPECT_EQ(ex, 0);
        EXPECT_EQ(AParcel_readInt32(out, &res), STATUS_OK);
        EXPECT_EQ(res, 0);

        AParcel_delete(in);
        AParcel_delete(out);
    }

    {
        AParcel *in = AParcel_create();
        AParcel *out = AParcel_create();

        AParcel_writeInt32(in, 3);
        AParcel_writeString(in, "com.portage.gameengine", -1);
        AParcel_writeString(in, "inapp", -1);

        EXPECT_EQ(AIBinder_transact(billing_svc, 2, in, out, 0), STATUS_OK);

        int32_t ex = -1;
        EXPECT_EQ(AParcel_readInt32(out, &ex), STATUS_OK);
        EXPECT_EQ(ex, 0);

        int32_t has_bundle = 0;
        EXPECT_EQ(AParcel_readInt32(out, &has_bundle), STATUS_OK);
        EXPECT_EQ(has_bundle, 1);

        int32_t bundle_len = 0;
        int32_t bundle_magic = 0;
        EXPECT_EQ(AParcel_readInt32(out, &bundle_len), STATUS_OK);
        EXPECT_EQ(AParcel_readInt32(out, &bundle_magic), STATUS_OK);
        EXPECT_EQ(bundle_magic, 0x4C444E42);

        AParcel_delete(in);
        AParcel_delete(out);
    }

    {
        AParcel *in = AParcel_create();
        AParcel *out = AParcel_create();

        AParcel_writeInt32(in, 3);
        AParcel_writeString(in, "com.portage.gameengine", -1);
        AParcel_writeString(in, "full_game_unlock", -1);
        AParcel_writeString(in, "inapp", -1);
        AParcel_writeString(in, "dev_payload_123", -1);

        EXPECT_EQ(AIBinder_transact(billing_svc, 3, in, out, 0), STATUS_OK);

        int32_t ex = -1;
        EXPECT_EQ(AParcel_readInt32(out, &ex), STATUS_OK);
        EXPECT_EQ(ex, 0);

        int32_t has_bundle = 0;
        EXPECT_EQ(AParcel_readInt32(out, &has_bundle), STATUS_OK);
        EXPECT_EQ(has_bundle, 1);

        AParcel_delete(in);
        AParcel_delete(out);
    }

    {
        AParcel *in = AParcel_create();
        AParcel *out = AParcel_create();

        AParcel_writeInt32(in, 3);
        AParcel_writeString(in, "com.portage.gameengine", -1);
        AParcel_writeString(in, "inapp", -1);
        AParcel_writeString(in, "", -1);

        EXPECT_EQ(AIBinder_transact(billing_svc, 4, in, out, 0), STATUS_OK);

        int32_t ex = -1;
        EXPECT_EQ(AParcel_readInt32(out, &ex), STATUS_OK);
        EXPECT_EQ(ex, 0);

        int32_t has_bundle = 0;
        EXPECT_EQ(AParcel_readInt32(out, &has_bundle), STATUS_OK);
        EXPECT_EQ(has_bundle, 1);

        AParcel_delete(in);
        AParcel_delete(out);
    }

    {
        EXPECT_TRUE(PlayBillingService::Instance().HasPurchase("full_game_unlock"));

        AParcel *in = AParcel_create();
        AParcel *out = AParcel_create();

        AParcel_writeInt32(in, 3);
        AParcel_writeString(in, "com.portage.gameengine", -1);
        AParcel_writeString(in, "token_full_game_unlock_123", -1);

        EXPECT_EQ(AIBinder_transact(billing_svc, 5, in, out, 0), STATUS_OK);

        int32_t ex = -1;
        int32_t res = -1;
        EXPECT_EQ(AParcel_readInt32(out, &ex), STATUS_OK);
        EXPECT_EQ(ex, 0);
        EXPECT_EQ(AParcel_readInt32(out, &res), STATUS_OK);
        EXPECT_EQ(res, 0);

        EXPECT_FALSE(PlayBillingService::Instance().HasPurchase("full_game_unlock"));

        AParcel_delete(in);
        AParcel_delete(out);
    }

    AIBinder_decStrong(billing_svc);
}

TEST_F(PlayServicesTest, SyntheticPlayServicesJniStubs)
{
    auto &jni = package::JniEnvironment::Instance();
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    auto gapi_cls =
        env->functions->FindClass(env, "com/google/android/gms/common/GoogleApiAvailability");
    ASSERT_NE(gapi_cls, nullptr);

    auto get_instance_mid = env->functions->GetStaticMethodID(
        env, gapi_cls, "getInstance", "()Lcom/google/android/gms/common/GoogleApiAvailability;");
    ASSERT_NE(get_instance_mid, nullptr);

    auto gapi_inst = env->functions->CallStaticObjectMethod(env, gapi_cls, get_instance_mid);
    ASSERT_NE(gapi_inst, nullptr);

    auto check_mid = env->functions->GetMethodID(env, gapi_cls, "isGooglePlayServicesAvailable",
                                                 "(Landroid/content/Context;)I");
    ASSERT_NE(check_mid, nullptr);

    package::jobject fake_ctx = reinterpret_cast<package::jobject>(0x9999);
    int res = env->functions->CallIntMethod(env, gapi_inst, check_mid, fake_ctx);
    EXPECT_EQ(res, 0);

    auto review_cls =
        env->functions->FindClass(env, "com/google/android/play/core/review/ReviewManagerFactory");
    ASSERT_NE(review_cls, nullptr);

    auto rcreate_mid = env->functions->GetStaticMethodID(
        env, review_cls, "create",
        "(Landroid/content/Context;)Lcom/google/android/play/core/review/ReviewManager;");
    ASSERT_NE(rcreate_mid, nullptr);

    auto review_mgr =
        env->functions->CallStaticObjectMethod(env, review_cls, rcreate_mid, fake_ctx);
    ASSERT_NE(review_mgr, nullptr);

    auto req_mid = env->functions->GetMethodID(env, review_cls, "requestReviewFlow",
                                               "()Lcom/google/android/gms/tasks/Task;");
    ASSERT_NE(req_mid, nullptr);

    auto task = env->functions->CallObjectMethod(env, review_mgr, req_mid);
    ASSERT_NE(task, nullptr);
}

} // namespace portage::binder
