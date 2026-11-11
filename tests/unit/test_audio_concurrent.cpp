#include <gtest/gtest.h>

#include "audio/aaudio_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/opensles_dispatcher.hpp"

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using namespace portage::audio;

class AudioConcurrentTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        AudioManager::Instance().ForceNullSink(true);
        AudioManager::Instance().Reset();
    }

    void TearDown() override { AudioManager::Instance().Reset(); }
};

TEST_F(AudioConcurrentTest, ConcurrentAAudioStreamsOpenAndClose)
{
    constexpr int kThreads = 4;
    std::barrier sync_point(kThreads);
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&sync_point, &failures]() {
            sync_point.arrive_and_wait();

            AAudioStreamBuilder *builder = nullptr;
            if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
                failures.fetch_add(1);
                return;
            }
            AAudioStreamBuilder_setSampleRate(builder, 48000);
            AAudioStreamBuilder_setChannelCount(builder, 2);
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

            AAudioStream *stream = nullptr;
            if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK || !stream) {
                failures.fetch_add(1);
                AAudioStreamBuilder_delete(builder);
                return;
            }
            AAudioStreamBuilder_delete(builder);

            if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
                failures.fetch_add(1);
            }

            std::vector<int16_t> samples(128 * 2, 400);
            if (AAudioStream_write(stream, samples.data(), 128, 0) != 128) {
                failures.fetch_add(1);
            }

            if (AAudioStream_requestStop(stream) != AAUDIO_OK) {
                failures.fetch_add(1);
            }
            if (AAudioStream_close(stream) != AAUDIO_OK) {
                failures.fetch_add(1);
            }
        });
    }

    for (auto &w : workers) {
        w.join();
    }

    EXPECT_EQ(failures.load(), 0);
}

TEST_F(AudioConcurrentTest, ConcurrentOpenSLESAndAAudioStreams)
{
    std::barrier start_barrier(2);
    std::barrier started_barrier(2);
    std::atomic<int> failures{0};

    std::thread opensles_thread([&start_barrier, &started_barrier, &failures]() {
        SLObjectItf engineObject = nullptr;
        if (slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) {
            failures.fetch_add(1);
            start_barrier.arrive_and_wait();
            started_barrier.arrive_and_wait();
            return;
        }
        (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);

        SLEngineItf engineItf = nullptr;
        (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineItf);

        SLObjectItf outputMixObject = nullptr;
        (*engineItf)->CreateOutputMix(engineItf, &outputMixObject, 0, nullptr, nullptr);
        (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);

        SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                         2};
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
        (*engineItf)
            ->CreateAudioPlayer(engineItf, &playerObject, &audioSrc, &audioSnk, 0, nullptr,
                                nullptr);
        (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);

        SLPlayItf playItf = nullptr;
        (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playItf);

        start_barrier.arrive_and_wait();
        if ((*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS) {
            failures.fetch_add(1);
        }
        started_barrier.arrive_and_wait();

        SLuint32 play_state = 0;
        if ((*playItf)->GetPlayState(playItf, &play_state) != SL_RESULT_SUCCESS ||
            play_state != SL_PLAYSTATE_PLAYING) {
            failures.fetch_add(1);
        }

        if ((*playItf)->SetPlayState(playItf, SL_PLAYSTATE_STOPPED) != SL_RESULT_SUCCESS) {
            failures.fetch_add(1);
        }
        (*playerObject)->Destroy(playerObject);
        (*outputMixObject)->Destroy(outputMixObject);
        (*engineObject)->Destroy(engineObject);
    });

    std::thread aaudio_thread([&start_barrier, &started_barrier, &failures]() {
        AAudioStreamBuilder *builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
            failures.fetch_add(1);
            start_barrier.arrive_and_wait();
            started_barrier.arrive_and_wait();
            return;
        }
        AAudioStreamBuilder_setSampleRate(builder, 48000);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

        AAudioStream *stream = nullptr;
        if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK || !stream) {
            failures.fetch_add(1);
            AAudioStreamBuilder_delete(builder);
            start_barrier.arrive_and_wait();
            started_barrier.arrive_and_wait();
            return;
        }
        AAudioStreamBuilder_delete(builder);

        start_barrier.arrive_and_wait();
        if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
            failures.fetch_add(1);
        }
        started_barrier.arrive_and_wait();

        if (AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_STARTED) {
            failures.fetch_add(1);
        }

        if (AAudioStream_requestStop(stream) != AAUDIO_OK) {
            failures.fetch_add(1);
        }
        if (AAudioStream_close(stream) != AAUDIO_OK) {
            failures.fetch_add(1);
        }
    });

    opensles_thread.join();
    aaudio_thread.join();

    EXPECT_EQ(failures.load(), 0);
}
