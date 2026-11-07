#include <gtest/gtest.h>

#include "audio/audio_manager.hpp"
#include "audio/opensles_dispatcher.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace portage::audio;

class OpenSLESTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        AudioManager::Instance().ForceNullSink(true);
        AudioManager::Instance().Reset();
    }

    void TearDown() override { AudioManager::Instance().Reset(); }
};

TEST_F(OpenSLESTest, InitializesEngineAndInterfaces)
{
    SLObjectItf engineObject = nullptr;
    EXPECT_EQ(slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr), SL_RESULT_SUCCESS);
    ASSERT_NE(engineObject, nullptr);

    EXPECT_EQ((*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLEngineItf engineEngine = nullptr;
    EXPECT_EQ((*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine),
              SL_RESULT_SUCCESS);
    ASSERT_NE(engineEngine, nullptr);

    SLObjectItf outputMixObject = nullptr;
    EXPECT_EQ((*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, nullptr, nullptr),
              SL_RESULT_SUCCESS);
    ASSERT_NE(outputMixObject, nullptr);

    EXPECT_EQ((*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    (*outputMixObject)->Destroy(outputMixObject);
    (*engineObject)->Destroy(engineObject);
}

static std::atomic<int32_t> g_opensles_callback_invocations{0};

static void __attribute__((sysv_abi)) TestBqCallback(SLAndroidSimpleBufferQueueItf caller,
                                                     void *context)
{
    (void)caller;
    (void)context;
    g_opensles_callback_invocations++;
}

TEST_F(OpenSLESTest, PlaysAudioViaBufferQueueAndInvokesCallback)
{
    g_opensles_callback_invocations = 0;

    SLObjectItf engineObject = nullptr;
    ASSERT_EQ(slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr), SL_RESULT_SUCCESS);
    ASSERT_EQ((*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLEngineItf engineEngine = nullptr;
    ASSERT_EQ((*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine),
              SL_RESULT_SUCCESS);

    SLObjectItf outputMixObject = nullptr;
    ASSERT_EQ((*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, nullptr, nullptr),
              SL_RESULT_SUCCESS);
    ASSERT_EQ((*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,           2,
        SL_SAMPLINGRATE_48,          SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
        SL_BYTEORDER_LITTLEENDIAN,
    };
    SLDataSource audioSrc = {&loc_bq, &format_pcm};

    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, nullptr};

    SLObjectItf playerObject = nullptr;
    ASSERT_EQ((*engineEngine)
                  ->CreateAudioPlayer(engineEngine, &playerObject, &audioSrc, &audioSnk, 0, nullptr,
                                      nullptr),
              SL_RESULT_SUCCESS);
    ASSERT_NE(playerObject, nullptr);

    ASSERT_EQ((*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLPlayItf playItf = nullptr;
    ASSERT_EQ((*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playItf),
              SL_RESULT_SUCCESS);

    SLAndroidSimpleBufferQueueItf bqItf = nullptr;
    ASSERT_EQ((*playerObject)->GetInterface(playerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bqItf),
              SL_RESULT_SUCCESS);

    ASSERT_EQ((*bqItf)->RegisterCallback(bqItf, TestBqCallback, nullptr), SL_RESULT_SUCCESS);

    ASSERT_EQ((*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING), SL_RESULT_SUCCESS);

    SLuint32 state = 0;
    EXPECT_EQ((*playItf)->GetPlayState(playItf, &state), SL_RESULT_SUCCESS);
    EXPECT_EQ(state, SL_PLAYSTATE_PLAYING);

    std::vector<int16_t> samples(256 * 2, 700);
    EXPECT_EQ((*bqItf)->Enqueue(bqItf, samples.data(),
                                static_cast<SLuint32>(samples.size() * sizeof(int16_t))),
              SL_RESULT_SUCCESS);

    // Wait for the buffer queue worker to process buffer and call callback
    for (int i = 0; i < 50 && g_opensles_callback_invocations.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(g_opensles_callback_invocations.load(), 0);

    (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_STOPPED);
    (*playerObject)->Destroy(playerObject);
    (*outputMixObject)->Destroy(outputMixObject);
    (*engineObject)->Destroy(engineObject);
}
