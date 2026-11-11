#include <gtest/gtest.h>

#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/opensles_dispatcher.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace portage::audio;
using namespace portage::package;
using namespace portage::ndk;

class AudioRecordingTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        AudioManager::Instance().ForceCaptureNullSource(true);
        AudioManager::Instance().SetCaptureToneFrequency(440.0f);
        AudioManager::Instance().ConfigureCaptureStream(48000, 1, SNDRV_PCM_FORMAT_S16_LE);
        AudioManager::Instance().CloseCaptureStream();
    }

    void TearDown() override
    {
        AudioManager::Instance().CloseCaptureStream();
        AudioManager::Instance().Reset();
    }
};

TEST_F(AudioRecordingTest, AudioManagerCaptureLifecycle)
{
    EXPECT_TRUE(AudioManager::Instance().ConfigureCaptureStream(48000, 1, SNDRV_PCM_FORMAT_S16_LE));
    EXPECT_TRUE(AudioManager::Instance().StartCapture());

    std::vector<int16_t> pcm_samples(512, 0);
    int64_t frames = AudioManager::Instance().ReadiFrames(pcm_samples.data(), 512, 200'000'000LL);
    EXPECT_GT(frames, 0);

    bool non_zero = false;
    for (int16_t s : pcm_samples) {
        if (s != 0) {
            non_zero = true;
            break;
        }
    }
    EXPECT_TRUE(non_zero);

    AudioManager::Instance().StopCapture();

    auto stats = AudioManager::Instance().GetCaptureStats();
    EXPECT_FALSE(stats.is_running);
    EXPECT_GT(stats.total_frames_captured, 0u);
    EXPECT_GT(stats.total_frames_read, 0u);
}

TEST_F(AudioRecordingTest, AAudioInputRecordingSync)
{
    AAudioStreamBuilder *builder = nullptr;
    EXPECT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    ASSERT_NE(builder, nullptr);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_NE(stream, nullptr);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_getState(stream), AAUDIO_STREAM_STATE_STARTED);

    std::vector<int16_t> audio_buffer(256, 0);
    int32_t frames_read = AAudioStream_read(stream, audio_buffer.data(), 256, 150'000'000LL);
    EXPECT_GT(frames_read, 0);

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);
}

static std::atomic<int32_t> g_aaudio_callback_count{0};
static std::atomic<bool> g_aaudio_received_data{false};

static aaudio_data_callback_result_t __attribute__((sysv_abi))
TestAAudioCaptureCallback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames)
{
    (void)stream;
    (void)userData;
    g_aaudio_callback_count++;

    const auto *samples = static_cast<const int16_t *>(audioData);
    for (int32_t i = 0; i < numFrames; ++i) {
        if (samples[i] != 0) {
            g_aaudio_received_data = true;
            break;
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

TEST_F(AudioRecordingTest, AAudioInputDataCallback)
{
    g_aaudio_callback_count = 0;
    g_aaudio_received_data = false;

    AAudioStreamBuilder *builder = nullptr;
    EXPECT_EQ(AAudio_createStreamBuilder(&builder), AAUDIO_OK);
    ASSERT_NE(builder, nullptr);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 256);
    AAudioStreamBuilder_setDataCallback(builder, TestAAudioCaptureCallback, nullptr);

    AAudioStream *stream = nullptr;
    EXPECT_EQ(AAudioStreamBuilder_openStream(builder, &stream), AAUDIO_OK);
    ASSERT_NE(stream, nullptr);

    EXPECT_EQ(AAudioStream_requestStart(stream), AAUDIO_OK);

    for (int i = 0; i < 50; ++i) {
        if (g_aaudio_callback_count > 0 && g_aaudio_received_data) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(g_aaudio_callback_count.load(), 0);
    EXPECT_TRUE(g_aaudio_received_data.load());

    EXPECT_EQ(AAudioStream_requestStop(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStream_close(stream), AAUDIO_OK);
    EXPECT_EQ(AAudioStreamBuilder_delete(builder), AAUDIO_OK);
}

static std::atomic<int32_t> g_recorder_callback_invocations{0};

static void __attribute__((sysv_abi)) TestRecorderBqCallback(SLAndroidSimpleBufferQueueItf caller,
                                                             void *context)
{
    (void)caller;
    (void)context;
    g_recorder_callback_invocations++;
}

TEST_F(AudioRecordingTest, OpenSlesAudioRecorderLifecycle)
{
    g_recorder_callback_invocations = 0;

    SLObjectItf engineObject = nullptr;
    EXPECT_EQ(slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr), SL_RESULT_SUCCESS);
    ASSERT_NE(engineObject, nullptr);
    EXPECT_EQ((*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLEngineItf engineEngine = nullptr;
    EXPECT_EQ((*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine),
              SL_RESULT_SUCCESS);
    ASSERT_NE(engineEngine, nullptr);

    SLDataLocator_IODevice loc_dev = {
        .locatorType = SL_DATALOCATOR_IODEVICE,
        .deviceType = SL_IODEVICE_AUDIOINPUT,
        .deviceID = 0,
        .device = nullptr,
    };
    SLDataSource audioSrc = {&loc_dev, nullptr};

    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
        .locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
        .numBuffers = 2,
    };
    SLDataFormat_PCM format_pcm = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = 1,
        .samplesPerSec = 48000 * 1000,
        .bitsPerSample = 16,
        .containerSize = 16,
        .channelMask = 1,
        .endianness = 1,
    };
    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    SLObjectItf recorderObject = nullptr;
    EXPECT_EQ((*engineEngine)
                  ->CreateAudioRecorder(engineEngine, &recorderObject, &audioSrc, &audioSnk, 0,
                                        nullptr, nullptr),
              SL_RESULT_SUCCESS);
    ASSERT_NE(recorderObject, nullptr);

    EXPECT_EQ((*recorderObject)->Realize(recorderObject, SL_BOOLEAN_FALSE), SL_RESULT_SUCCESS);

    SLRecordItf recordItf = nullptr;
    EXPECT_EQ((*recorderObject)->GetInterface(recorderObject, SL_IID_RECORD, &recordItf),
              SL_RESULT_SUCCESS);
    ASSERT_NE(recordItf, nullptr);

    SLAndroidSimpleBufferQueueItf bqItf = nullptr;
    EXPECT_EQ(
        (*recorderObject)->GetInterface(recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bqItf),
        SL_RESULT_SUCCESS);
    ASSERT_NE(bqItf, nullptr);

    EXPECT_EQ((*bqItf)->RegisterCallback(bqItf, TestRecorderBqCallback, nullptr),
              SL_RESULT_SUCCESS);

    std::vector<int16_t> record_buf(512, 0);
    EXPECT_EQ((*bqItf)->Enqueue(bqItf, record_buf.data(), record_buf.size() * sizeof(int16_t)),
              SL_RESULT_SUCCESS);

    EXPECT_EQ((*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_RECORDING), SL_RESULT_SUCCESS);

    SLuint32 state = 0;
    EXPECT_EQ((*recordItf)->GetRecordState(recordItf, &state), SL_RESULT_SUCCESS);
    EXPECT_EQ(state, SL_RECORDSTATE_RECORDING);

    for (int i = 0; i < 50; ++i) {
        if (g_recorder_callback_invocations > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_GT(g_recorder_callback_invocations.load(), 0);

    bool has_audio = false;
    for (int16_t s : record_buf) {
        if (s != 0) {
            has_audio = true;
            break;
        }
    }
    EXPECT_TRUE(has_audio);

    EXPECT_EQ((*recordItf)->SetRecordState(recordItf, SL_RECORDSTATE_STOPPED), SL_RESULT_SUCCESS);

    (*recorderObject)->Destroy(recorderObject);
    (*engineObject)->Destroy(engineObject);
}

TEST_F(AudioRecordingTest, SyntheticAudioRecordJni)
{
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jclass ar_class = reinterpret_cast<jclass>(const_cast<char *>("android/media/AudioRecord"));
    jmethodID min_buf_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getMinBufferSize"));
    jint min_buf = jenv->functions->CallStaticIntMethod(jenv, ar_class, min_buf_mid, 48000, 16, 2);
    EXPECT_EQ(min_buf, 4096);

    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject ar_obj = jenv->functions->NewObject(jenv, ar_class, ctor_mid, 1, 48000, 16, 2, min_buf);
    ASSERT_NE(ar_obj, nullptr);

    jmethodID get_state_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getState"));
    jint state = jenv->functions->CallIntMethod(jenv, ar_obj, get_state_mid);
    EXPECT_EQ(state, 1);

    jmethodID get_rec_state_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getRecordingState"));
    jint rec_state = jenv->functions->CallIntMethod(jenv, ar_obj, get_rec_state_mid);
    EXPECT_EQ(rec_state, 1);

    jmethodID start_rec_mid = reinterpret_cast<jmethodID>(const_cast<char *>("startRecording"));
    jenv->functions->CallVoidMethod(jenv, ar_obj, start_rec_mid);

    rec_state = jenv->functions->CallIntMethod(jenv, ar_obj, get_rec_state_mid);
    EXPECT_EQ(rec_state, 3);

    jshortArray short_arr = jenv->functions->NewShortArray(jenv, 512);
    ASSERT_NE(short_arr, nullptr);

    jmethodID read_mid = reinterpret_cast<jmethodID>(const_cast<char *>("read"));
    jint read_res = jenv->functions->CallIntMethod(jenv, ar_obj, read_mid, short_arr, 0, 256);
    EXPECT_GT(read_res, 0);

    jmethodID stop_mid = reinterpret_cast<jmethodID>(const_cast<char *>("stop"));
    jenv->functions->CallVoidMethod(jenv, ar_obj, stop_mid);

    rec_state = jenv->functions->CallIntMethod(jenv, ar_obj, get_rec_state_mid);
    EXPECT_EQ(rec_state, 1);

    jmethodID release_mid = reinterpret_cast<jmethodID>(const_cast<char *>("release"));
    jenv->functions->CallVoidMethod(jenv, ar_obj, release_mid);

    state = jenv->functions->CallIntMethod(jenv, ar_obj, get_state_mid);
    EXPECT_EQ(state, 0);
}

TEST_F(AudioRecordingTest, NdkSymbolRegistryLookup)
{
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("AAudioStream_read"), 0u);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("SL_IID_RECORD"), 0u);
}
