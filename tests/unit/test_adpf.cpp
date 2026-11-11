#include "ndk/performance_hint.hpp"
#include "ndk/thermal.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

using namespace portage::ndk;
using namespace portage::package;

class AdpfTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        ThermalManager::Instance().Reset();
        PerformanceHintManager::Instance().Reset();
    }

    void TearDown() override
    {
        ThermalManager::Instance().Reset();
        PerformanceHintManager::Instance().Reset();
    }
};

TEST_F(AdpfTest, ThermalManagerLifecycleAndStatus)
{
    AThermalManager *mgr = AThermalManager_acquireManager();
    ASSERT_NE(mgr, nullptr);

    AThermalStatus status = AThermalManager_getCurrentThermalStatus(mgr);
    EXPECT_GE(static_cast<int>(status), 0);
    EXPECT_LE(static_cast<int>(status), ATHERMAL_STATUS_SHUTDOWN);

    AThermalManager_releaseManager(mgr);
}

TEST_F(AdpfTest, ThermalStatusListenerDispatch)
{
    AThermalManager *mgr = AThermalManager_acquireManager();
    ASSERT_NE(mgr, nullptr);

    struct CallbackState {
        std::atomic<int> calls{0};
        std::atomic<AThermalStatus> last_status{ATHERMAL_STATUS_NONE};

        static void OnThermalStatus(void *data, AThermalStatus status)
        {
            auto *s = static_cast<CallbackState *>(data);
            s->last_status.store(status);
            s->calls.fetch_add(1);
        }
    } state;

    int reg_res =
        AThermalManager_registerThermalStatusListener(mgr, &CallbackState::OnThermalStatus, &state);
    EXPECT_EQ(reg_res, 0);

    ThermalManager::Instance().SetStatusOverride(ATHERMAL_STATUS_SEVERE);
    EXPECT_EQ(state.calls.load(), 1);
    EXPECT_EQ(state.last_status.load(), ATHERMAL_STATUS_SEVERE);

    int unreg_res = AThermalManager_unregisterThermalStatusListener(
        mgr, &CallbackState::OnThermalStatus, &state);
    EXPECT_EQ(unreg_res, 0);

    ThermalManager::Instance().SetStatusOverride(ATHERMAL_STATUS_MODERATE);
    EXPECT_EQ(state.calls.load(), 1);

    AThermalManager_releaseManager(mgr);
}

TEST_F(AdpfTest, ThermalHeadroomCalculation)
{
    AThermalManager *mgr = AThermalManager_acquireManager();
    ASSERT_NE(mgr, nullptr);

    float headroom = AThermalManager_getThermalHeadroom(mgr, 10);
    EXPECT_GE(headroom, 0.05f);
    EXPECT_LE(headroom, 1.25f);

    ThermalManager::Instance().SetHeadroomOverride(0.82f);
    float overridden = AThermalManager_getThermalHeadroom(mgr, 10);
    EXPECT_FLOAT_EQ(overridden, 0.82f);

    AThermalManager_releaseManager(mgr);
}

TEST_F(AdpfTest, PerformanceHintSessionLifecycle)
{
    APerformanceHintManager *mgr = APerformanceHintManager_create();
    ASSERT_NE(mgr, nullptr);

    int64_t update_rate = APerformanceHintManager_getPreferredUpdateRateNanos(mgr);
    EXPECT_EQ(update_rate, 16'666'666LL);

    int32_t tids[] = {101, 102};
    APerformanceHintSession *session =
        APerformanceHintManager_createSession(mgr, tids, 2, 16'666'666LL);
    ASSERT_NE(session, nullptr);

    int update_res =
        APerformanceHintSession_updateTargetWorkDuration(session, 8'333'333LL); // 120 fps target
    EXPECT_EQ(update_res, 0);

    int report_res = APerformanceHintSession_reportActualWorkDuration(session, 7'500'000LL);
    EXPECT_EQ(report_res, 0);

    int32_t new_tids[] = {101, 102, 103};
    int set_threads_res = APerformanceHintSession_setThreads(session, new_tids, 3);
    EXPECT_EQ(set_threads_res, 0);

    APerformanceHintSession_close(session);
    EXPECT_EQ(PerformanceHintManager::Instance().GetActiveSessionCount(), 0u);
}

TEST_F(AdpfTest, PerformanceHintThreadQoSPacing)
{
    APerformanceHintManager *mgr = APerformanceHintManager_create();
    ASSERT_NE(mgr, nullptr);

    int32_t tids[] = {2001};
    APerformanceHintSession *session =
        APerformanceHintManager_createSession(mgr, tids, 1, 16'000'000LL);
    ASSERT_NE(session, nullptr);

    auto *sess = reinterpret_cast<PerformanceHintSession *>(session);
    EXPECT_FALSE(sess->IsBoosted());

    // Heavy overshoot: 22ms on a 16ms target (> 15% overshoot)
    APerformanceHintSession_reportActualWorkDuration(session, 22'000'000LL);
    EXPECT_TRUE(sess->IsBoosted());
    EXPECT_EQ(sess->GetOvershootCount(), 1u);

    // Fast frame: 12ms on a 16ms target
    APerformanceHintSession_reportActualWorkDuration(session, 12'000'000LL);
    EXPECT_FALSE(sess->IsBoosted());

    APerformanceHintSession_close(session);
}

TEST_F(AdpfTest, SyntheticAdpfJniReflection)
{
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    // PowerManager
    jstring power_str = env.CreateString("power");
    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jobject pm_obj = jenv->functions->CallObjectMethod(jenv, nullptr, get_svc_mid, power_str);
    ASSERT_NE(pm_obj, nullptr);

    ThermalManager::Instance().SetHeadroomOverride(0.77f);
    jmethodID get_headroom_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getThermalHeadroom"));
    jfloat headroom = jenv->functions->CallFloatMethod(jenv, pm_obj, get_headroom_mid, 10);
    EXPECT_FLOAT_EQ(headroom, 0.77f);

    ThermalManager::Instance().SetStatusOverride(ATHERMAL_STATUS_MODERATE);
    jmethodID get_status_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getCurrentThermalStatus"));
    jint status = jenv->functions->CallIntMethod(jenv, pm_obj, get_status_mid);
    EXPECT_EQ(status, static_cast<jint>(ATHERMAL_STATUS_MODERATE));

    // PerformanceHintManager
    jstring hint_str = env.CreateString("performance_hint");
    jobject phm_obj = jenv->functions->CallObjectMethod(jenv, nullptr, get_svc_mid, hint_str);
    ASSERT_NE(phm_obj, nullptr);

    jmethodID create_session_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("createHintSession"));
    jobject sess_obj = jenv->functions->CallObjectMethod(jenv, phm_obj, create_session_mid);
    ASSERT_NE(sess_obj, nullptr);

    jmethodID report_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("reportActualWorkDuration"));
    jint rep_res = jenv->functions->CallIntMethod(jenv, sess_obj, report_mid, 16000000LL);
    EXPECT_EQ(rep_res, 0);
}

TEST_F(AdpfTest, NdkSymbolRegistryLookup)
{
    auto &reg = NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("AThermalManager_acquireManager"), 0u);
    EXPECT_NE(reg.Lookup("AThermalManager_releaseManager"), 0u);
    EXPECT_NE(reg.Lookup("AThermalManager_getCurrentThermalStatus"), 0u);
    EXPECT_NE(reg.Lookup("AThermalManager_registerThermalStatusListener"), 0u);
    EXPECT_NE(reg.Lookup("AThermalManager_unregisterThermalStatusListener"), 0u);
    EXPECT_NE(reg.Lookup("AThermalManager_getThermalHeadroom"), 0u);

    EXPECT_NE(reg.Lookup("APerformanceHintManager_create"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintManager_createSession"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintManager_getPreferredUpdateRateNanos"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintSession_updateTargetWorkDuration"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintSession_reportActualWorkDuration"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintSession_close"), 0u);
    EXPECT_NE(reg.Lookup("APerformanceHintSession_setThreads"), 0u);
}
