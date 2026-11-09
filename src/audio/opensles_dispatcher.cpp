#include "audio/opensles_dispatcher.hpp"
#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace portage::audio {

namespace {

bool MatchIID(SLInterfaceID a, SLInterfaceID b)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return a->time_low == b->time_low && a->time_mid == b->time_mid &&
           a->time_hi_and_version == b->time_hi_and_version &&
           a->clock_seq_hi_and_reserved == b->clock_seq_hi_and_reserved &&
           a->clock_seq_low == b->clock_seq_low &&
           std::memcmp(a->node, b->node, sizeof(a->node)) == 0;
}

// Forward declarations
struct EngineObjectImpl;
struct OutputMixObjectImpl;
struct AudioPlayerObjectImpl;
struct AudioRecorderObjectImpl;

// Engine Object VTables
SLresult __attribute__((sysv_abi)) Engine_Realize(SLObjectItf self, SLboolean async)
{
    (void)self;
    (void)async;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Engine_Resume(SLObjectItf self, SLboolean async)
{
    (void)self;
    (void)async;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Engine_GetState(SLObjectItf self, SLuint32 *pState)
{
    (void)self;
    if (pState) {
        *pState = 2; // SL_OBJECT_STATE_REALIZED
    }
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Engine_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                       void *pInterface);

SLresult __attribute__((sysv_abi))
Engine_RegisterCallback(SLObjectItf self, slObjectCallback callback, void *pContext)
{
    (void)self;
    (void)callback;
    (void)pContext;
    return SL_RESULT_SUCCESS;
}

void __attribute__((sysv_abi)) Engine_AbortAsyncOperation(SLObjectItf self) { (void)self; }

void __attribute__((sysv_abi)) Engine_Destroy(SLObjectItf self);

SLresult __attribute__((sysv_abi)) Engine_SetPriority(SLObjectItf self, SLint32 priority,
                                                      SLboolean preemptable)
{
    (void)self;
    (void)priority;
    (void)preemptable;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Engine_GetPriority(SLObjectItf self, SLint32 *pPriority,
                                                      SLboolean *pPreemptable)
{
    (void)self;
    if (pPriority)
        *pPriority = 0;
    if (pPreemptable)
        *pPreemptable = SL_BOOLEAN_FALSE;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Engine_SetLossOfControlInterfaces(SLObjectItf self,
                                                                     SLint32 numInterfaces,
                                                                     SLInterfaceID *pInterfaceIDs,
                                                                     SLboolean enabled)
{
    (void)self;
    (void)numInterfaces;
    (void)pInterfaceIDs;
    (void)enabled;
    return SL_RESULT_SUCCESS;
}

const SLObjectItf_ g_EngineObjectVTable = {
    Engine_Realize,          Engine_Resume,
    Engine_GetState,         Engine_GetInterface,
    Engine_RegisterCallback, Engine_AbortAsyncOperation,
    Engine_Destroy,          Engine_SetPriority,
    Engine_GetPriority,      Engine_SetLossOfControlInterfaces,
};

// SLEngineItf methods
SLresult __attribute__((sysv_abi)) EngineItf_CreateOutputMix(SLEngineItf self, SLObjectItf *pMix,
                                                             SLuint32 numInterfaces,
                                                             const SLInterfaceID *pInterfaceIds,
                                                             const SLboolean *pInterfaceRequired);

SLresult __attribute__((sysv_abi))
EngineItf_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pPlayer, SLDataSource *pAudioSrc,
                            SLDataSink *pAudioSnk, SLuint32 numInterfaces,
                            const SLInterfaceID *pInterfaceIds,
                            const SLboolean *pInterfaceRequired);

SLresult __attribute__((sysv_abi))
EngineItf_CreateAudioRecorder(SLEngineItf self, SLObjectItf *pRecorder, SLDataSource *pAudioSrc,
                              SLDataSink *pAudioSnk, SLuint32 numInterfaces,
                              const SLInterfaceID *pInterfaceIds,
                              const SLboolean *pInterfaceRequired);

const SLEngineItf_ g_EngineItfVTable = {
    nullptr, // CreateLEDDevice
    nullptr, // CreateVibraDevice
    EngineItf_CreateAudioPlayer,
    EngineItf_CreateAudioRecorder,
    nullptr, // CreateMidiPlayer
    nullptr, // CreateListener
    nullptr, // Create3DEnvironment
    EngineItf_CreateOutputMix,
};

struct EngineObjectImpl {
    const SLObjectItf_ *obj_vtbl{&g_EngineObjectVTable};
    const SLEngineItf_ *engine_vtbl{&g_EngineItfVTable};
};

SLresult __attribute__((sysv_abi)) Engine_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                       void *pInterface)
{
    if (!self || !pInterface) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<EngineObjectImpl *>(self);
    if (MatchIID(iid, SL_IID_ENGINE)) {
        *reinterpret_cast<const SLEngineItf_ ***>(pInterface) = &impl->engine_vtbl;
        return SL_RESULT_SUCCESS;
    }
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

void __attribute__((sysv_abi)) Engine_Destroy(SLObjectItf self)
{
    if (self) {
        auto *impl = reinterpret_cast<EngineObjectImpl *>(self);
        delete impl;
    }
}

// OutputMix Object
void __attribute__((sysv_abi)) OutputMix_Destroy(SLObjectItf self);

const SLObjectItf_ g_OutputMixObjectVTable = {
    Engine_Realize,          Engine_Resume,
    Engine_GetState,         Engine_GetInterface,
    Engine_RegisterCallback, Engine_AbortAsyncOperation,
    OutputMix_Destroy,       Engine_SetPriority,
    Engine_GetPriority,      Engine_SetLossOfControlInterfaces,
};

struct OutputMixObjectImpl {
    const SLObjectItf_ *obj_vtbl{&g_OutputMixObjectVTable};
};

void __attribute__((sysv_abi)) OutputMix_Destroy(SLObjectItf self)
{
    if (self) {
        auto *impl = reinterpret_cast<OutputMixObjectImpl *>(self);
        delete impl;
    }
}

SLresult __attribute__((sysv_abi)) EngineItf_CreateOutputMix(SLEngineItf self, SLObjectItf *pMix,
                                                             SLuint32 numInterfaces,
                                                             const SLInterfaceID *pInterfaceIds,
                                                             const SLboolean *pInterfaceRequired)
{
    (void)self;
    (void)numInterfaces;
    (void)pInterfaceIds;
    (void)pInterfaceRequired;
    if (!pMix) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *mix = new OutputMixObjectImpl();
    *pMix = reinterpret_cast<SLObjectItf>(&mix->obj_vtbl);
    return SL_RESULT_SUCCESS;
}

// Audio Player Object
struct BufferQueueEntry {
    std::vector<uint8_t> data;
};

struct AudioPlayerObjectImpl;

SLresult __attribute__((sysv_abi)) Player_Realize(SLObjectItf self, SLboolean async)
{
    (void)self;
    (void)async;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Player_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                       void *pInterface);

void __attribute__((sysv_abi)) Player_Destroy(SLObjectItf self);

const SLObjectItf_ g_PlayerObjectVTable = {
    Player_Realize,          Engine_Resume,
    Engine_GetState,         Player_GetInterface,
    Engine_RegisterCallback, Engine_AbortAsyncOperation,
    Player_Destroy,          Engine_SetPriority,
    Engine_GetPriority,      Engine_SetLossOfControlInterfaces,
};

// PlayItf
SLresult __attribute__((sysv_abi)) Play_SetPlayState(SLPlayItf self, SLuint32 state);
SLresult __attribute__((sysv_abi)) Play_GetPlayState(SLPlayItf self, SLuint32 *pState);

const SLPlayItf_ g_PlayItfVTable = {
    Play_SetPlayState, Play_GetPlayState,
    nullptr, // GetDuration
    nullptr, // GetPosition
    nullptr, // RegisterCallback
    nullptr, // SetCallbackEventsMask
    nullptr, // GetCallbackEventsMask
    nullptr, // SetMarkerPosition
    nullptr, // ClearMarkerPosition
    nullptr, // GetMarkerPosition
    nullptr, // SetPositionUpdatePeriod
    nullptr, // GetPositionUpdatePeriod
};

// BufferQueueItf
SLresult __attribute__((sysv_abi)) BufferQueue_Enqueue(SLAndroidSimpleBufferQueueItf self,
                                                       const void *pBuffer, SLuint32 size);
SLresult __attribute__((sysv_abi)) BufferQueue_Clear(SLAndroidSimpleBufferQueueItf self);
SLresult __attribute__((sysv_abi)) BufferQueue_GetState(SLAndroidSimpleBufferQueueItf self,
                                                        SLAndroidSimpleBufferQueueState *pState);
SLresult __attribute__((sysv_abi))
BufferQueue_RegisterCallback(SLAndroidSimpleBufferQueueItf self,
                             slAndroidSimpleBufferQueueCallback callback, void *pContext);

const SLAndroidSimpleBufferQueueItf_ g_BufferQueueItfVTable = {
    BufferQueue_Enqueue,
    BufferQueue_Clear,
    BufferQueue_GetState,
    BufferQueue_RegisterCallback,
};

// VolumeItf
SLresult __attribute__((sysv_abi)) Volume_SetVolumeLevel(SLVolumeItf self, SLmillibel level)
{
    (void)self;
    (void)level;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Volume_GetVolumeLevel(SLVolumeItf self, SLmillibel *pLevel)
{
    (void)self;
    if (pLevel)
        *pLevel = 0;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Volume_GetMaxVolumeLevel(SLVolumeItf self, SLmillibel *pMaxLevel)
{
    (void)self;
    if (pMaxLevel)
        *pMaxLevel = 0;
    return SL_RESULT_SUCCESS;
}

const SLVolumeItf_ g_VolumeItfVTable = {
    Volume_SetVolumeLevel,
    Volume_GetVolumeLevel,
    Volume_GetMaxVolumeLevel,
    nullptr, // SetMute
    nullptr, // GetMute
    nullptr, // EnableStereoPosition
    nullptr, // IsEnabledStereoPosition
    nullptr, // SetStereoPosition
    nullptr, // GetStereoPosition
};

struct AudioPlayerObjectImpl {
    const SLObjectItf_ *obj_vtbl{&g_PlayerObjectVTable};
    const SLPlayItf_ *play_vtbl{&g_PlayItfVTable};
    const SLAndroidSimpleBufferQueueItf_ *bq_vtbl{&g_BufferQueueItfVTable};
    const SLVolumeItf_ *vol_vtbl{&g_VolumeItfVTable};

    uint32_t sample_rate{48000};
    uint32_t channels{2};
    int32_t format{SNDRV_PCM_FORMAT_S16_LE};

    std::atomic<SLuint32> play_state{SL_PLAYSTATE_STOPPED};

    slAndroidSimpleBufferQueueCallback bq_callback{nullptr};
    void *bq_context{nullptr};

    std::mutex queue_mutex{};
    std::condition_variable queue_cv{};
    std::deque<BufferQueueEntry> queue{};
    std::atomic<bool> worker_running{true};
    std::thread worker_thread{};

    AudioPlayerObjectImpl()
    {
        worker_thread = std::thread([this]() { WorkerLoop(); });
    }

    ~AudioPlayerObjectImpl()
    {
        worker_running = false;
        queue_cv.notify_all();
        if (worker_thread.joinable()) {
            if (worker_thread.get_id() != std::this_thread::get_id()) {
                worker_thread.join();
            }
        }
    }

    void WorkerLoop()
    {
        while (worker_running.load()) {
            BufferQueueEntry entry;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] {
                    return !worker_running.load() ||
                           (!queue.empty() && play_state.load() == SL_PLAYSTATE_PLAYING);
                });
                if (!worker_running.load()) {
                    break;
                }
                entry = std::move(queue.front());
                queue.pop_front();
            }

            if (!entry.data.empty()) {
                AudioManager::Instance().WritePcmBytes(entry.data.data(), entry.data.size());
            }

            slAndroidSimpleBufferQueueCallback cb = bq_callback;
            void *ctx = bq_context;
            if (cb && play_state.load() == SL_PLAYSTATE_PLAYING) {
                cb(reinterpret_cast<SLAndroidSimpleBufferQueueItf>(&bq_vtbl), ctx);
            }
        }
    }
};

SLresult __attribute__((sysv_abi)) Player_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                       void *pInterface)
{
    if (!self || !pInterface) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(self);
    if (MatchIID(iid, SL_IID_PLAY)) {
        *reinterpret_cast<const SLPlayItf_ ***>(pInterface) = &impl->play_vtbl;
        return SL_RESULT_SUCCESS;
    }
    if (MatchIID(iid, SL_IID_ANDROIDSIMPLEBUFFERQUEUE) || MatchIID(iid, SL_IID_BUFFERQUEUE)) {
        *reinterpret_cast<const SLAndroidSimpleBufferQueueItf_ ***>(pInterface) = &impl->bq_vtbl;
        return SL_RESULT_SUCCESS;
    }
    if (MatchIID(iid, SL_IID_VOLUME)) {
        *reinterpret_cast<const SLVolumeItf_ ***>(pInterface) = &impl->vol_vtbl;
        return SL_RESULT_SUCCESS;
    }
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

void __attribute__((sysv_abi)) Player_Destroy(SLObjectItf self)
{
    if (self) {
        auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(self);
        delete impl;
    }
}

SLresult __attribute__((sysv_abi)) Play_SetPlayState(SLPlayItf self, SLuint32 state)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, play_vtbl));
    impl->play_state.store(state);
    if (state == SL_PLAYSTATE_PLAYING) {
        AudioManager::Instance().ConfigureStream(impl->sample_rate, impl->channels, impl->format);
        impl->queue_cv.notify_one();
    }
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Play_GetPlayState(SLPlayItf self, SLuint32 *pState)
{
    if (!self || !pState) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, play_vtbl));
    *pState = impl->play_state.load();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) BufferQueue_Enqueue(SLAndroidSimpleBufferQueueItf self,
                                                       const void *pBuffer, SLuint32 size)
{
    if (!self || !pBuffer || size == 0) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, bq_vtbl));

    {
        std::lock_guard<std::mutex> lock(impl->queue_mutex);
        const auto *ptr = static_cast<const uint8_t *>(pBuffer);
        impl->queue.push_back(BufferQueueEntry{.data = std::vector<uint8_t>(ptr, ptr + size)});
    }
    impl->queue_cv.notify_one();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) BufferQueue_Clear(SLAndroidSimpleBufferQueueItf self)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, bq_vtbl));
    std::lock_guard<std::mutex> lock(impl->queue_mutex);
    impl->queue.clear();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) BufferQueue_GetState(SLAndroidSimpleBufferQueueItf self,
                                                        SLAndroidSimpleBufferQueueState *pState)
{
    if (!self || !pState) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, bq_vtbl));
    std::lock_guard<std::mutex> lock(impl->queue_mutex);
    pState->count = static_cast<SLuint32>(impl->queue.size());
    pState->index = 0;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi))
BufferQueue_RegisterCallback(SLAndroidSimpleBufferQueueItf self,
                             slAndroidSimpleBufferQueueCallback callback, void *pContext)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioPlayerObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioPlayerObjectImpl, bq_vtbl));
    impl->bq_callback = callback;
    impl->bq_context = pContext;
    return SL_RESULT_SUCCESS;
}

struct RecordBufferQueueEntry {
    void *buffer{nullptr};
    SLuint32 size{0};
};

struct AudioRecorderObjectImpl;

SLresult __attribute__((sysv_abi)) Recorder_Realize(SLObjectItf self, SLboolean async)
{
    (void)self;
    (void)async;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Recorder_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                         void *pInterface);
void __attribute__((sysv_abi)) Recorder_Destroy(SLObjectItf self);

const SLObjectItf_ g_RecorderObjectVTable = {
    Recorder_Realize,        Engine_Resume,
    Engine_GetState,         Recorder_GetInterface,
    Engine_RegisterCallback, Engine_AbortAsyncOperation,
    Recorder_Destroy,        Engine_SetPriority,
    Engine_GetPriority,      Engine_SetLossOfControlInterfaces,
};

SLresult __attribute__((sysv_abi)) Record_SetRecordState(SLRecordItf self, SLuint32 state);
SLresult __attribute__((sysv_abi)) Record_GetRecordState(SLRecordItf self, SLuint32 *pState);

const SLRecordItf_ g_RecordItfVTable = {
    Record_SetRecordState, Record_GetRecordState,
    nullptr, // SetDurationLimit
    nullptr, // GetPosition
    nullptr, // RegisterCallback
    nullptr, // SetCallbackEventsMask
    nullptr, // GetCallbackEventsMask
    nullptr, // SetMarkerPosition
    nullptr, // ClearMarkerPosition
    nullptr, // GetMarkerPosition
    nullptr, // SetPositionUpdatePeriod
    nullptr, // GetPositionUpdatePeriod
};

SLresult __attribute__((sysv_abi)) RecordBufferQueue_Enqueue(SLAndroidSimpleBufferQueueItf self,
                                                             const void *pBuffer, SLuint32 size);
SLresult __attribute__((sysv_abi)) RecordBufferQueue_Clear(SLAndroidSimpleBufferQueueItf self);
SLresult __attribute__((sysv_abi)) RecordBufferQueue_GetState(SLAndroidSimpleBufferQueueItf self,
                                                              SLAndroidSimpleBufferQueueState *pState);
SLresult __attribute__((sysv_abi))
RecordBufferQueue_RegisterCallback(SLAndroidSimpleBufferQueueItf self,
                                   slAndroidSimpleBufferQueueCallback callback, void *pContext);

const SLAndroidSimpleBufferQueueItf_ g_RecordBufferQueueItfVTable = {
    RecordBufferQueue_Enqueue,
    RecordBufferQueue_Clear,
    RecordBufferQueue_GetState,
    RecordBufferQueue_RegisterCallback,
};

struct AudioRecorderObjectImpl {
    const SLObjectItf_ *obj_vtbl{&g_RecorderObjectVTable};
    const SLRecordItf_ *rec_vtbl{&g_RecordItfVTable};
    const SLAndroidSimpleBufferQueueItf_ *bq_vtbl{&g_RecordBufferQueueItfVTable};

    uint32_t sample_rate{48000};
    uint32_t channels{1};
    int32_t format{SNDRV_PCM_FORMAT_S16_LE};

    std::atomic<SLuint32> record_state{SL_RECORDSTATE_STOPPED};

    std::mutex queue_mutex{};
    std::condition_variable queue_cv{};
    std::deque<RecordBufferQueueEntry> queue{};
    slAndroidSimpleBufferQueueCallback bq_callback{nullptr};
    void *bq_context{nullptr};

    std::thread worker_thread{};
    std::atomic<bool> worker_running{true};

    AudioRecorderObjectImpl()
    {
        worker_thread = std::thread([this]() { WorkerLoop(); });
    }

    ~AudioRecorderObjectImpl()
    {
        worker_running = false;
        queue_cv.notify_all();
        if (worker_thread.joinable()) {
            if (worker_thread.get_id() != std::this_thread::get_id()) {
                worker_thread.join();
            }
        }
    }

    void WorkerLoop()
    {
        while (worker_running.load()) {
            RecordBufferQueueEntry entry;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] {
                    return !worker_running.load() ||
                           (!queue.empty() && record_state.load() == SL_RECORDSTATE_RECORDING);
                });
                if (!worker_running.load()) {
                    break;
                }
                entry = queue.front();
                queue.pop_front();
            }

            if (entry.buffer && entry.size > 0) {
                int64_t bytes_read = 0;
                while (bytes_read < static_cast<int64_t>(entry.size) &&
                       record_state.load() == SL_RECORDSTATE_RECORDING &&
                       worker_running.load()) {
                    int64_t r = AudioManager::Instance().ReadPcmBytes(
                        static_cast<uint8_t *>(entry.buffer) + bytes_read,
                        entry.size - bytes_read);
                    if (r > 0) {
                        bytes_read += r;
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
            }

            slAndroidSimpleBufferQueueCallback cb = bq_callback;
            void *ctx = bq_context;
            if (cb && record_state.load() == SL_RECORDSTATE_RECORDING) {
                cb(reinterpret_cast<SLAndroidSimpleBufferQueueItf>(&bq_vtbl), ctx);
            }
        }
    }
};

SLresult __attribute__((sysv_abi)) Recorder_GetInterface(SLObjectItf self, const SLInterfaceID iid,
                                                         void *pInterface)
{
    if (!self || !pInterface) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(self);
    if (MatchIID(iid, SL_IID_RECORD)) {
        *reinterpret_cast<const SLRecordItf_ ***>(pInterface) = &impl->rec_vtbl;
        return SL_RESULT_SUCCESS;
    }
    if (MatchIID(iid, SL_IID_ANDROIDSIMPLEBUFFERQUEUE) || MatchIID(iid, SL_IID_BUFFERQUEUE)) {
        *reinterpret_cast<const SLAndroidSimpleBufferQueueItf_ ***>(pInterface) = &impl->bq_vtbl;
        return SL_RESULT_SUCCESS;
    }
    return SL_RESULT_FEATURE_UNSUPPORTED;
}

void __attribute__((sysv_abi)) Recorder_Destroy(SLObjectItf self)
{
    if (self) {
        auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(self);
        delete impl;
    }
}

SLresult __attribute__((sysv_abi)) Record_SetRecordState(SLRecordItf self, SLuint32 state)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, rec_vtbl));
    impl->record_state.store(state);
    if (state == SL_RECORDSTATE_RECORDING) {
        AudioManager::Instance().ConfigureCaptureStream(impl->sample_rate, impl->channels,
                                                        impl->format);
        AudioManager::Instance().StartCapture();
        impl->queue_cv.notify_one();
    } else {
        AudioManager::Instance().StopCapture();
    }
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) Record_GetRecordState(SLRecordItf self, SLuint32 *pState)
{
    if (!self || !pState) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, rec_vtbl));
    *pState = impl->record_state.load();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) RecordBufferQueue_Enqueue(SLAndroidSimpleBufferQueueItf self,
                                                             const void *pBuffer, SLuint32 size)
{
    if (!self || !pBuffer || size == 0) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, bq_vtbl));
    {
        std::lock_guard<std::mutex> lock(impl->queue_mutex);
        impl->queue.push_back(RecordBufferQueueEntry{
            .buffer = const_cast<void *>(pBuffer),
            .size = size,
        });
    }
    impl->queue_cv.notify_one();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) RecordBufferQueue_Clear(SLAndroidSimpleBufferQueueItf self)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, bq_vtbl));
    std::lock_guard<std::mutex> lock(impl->queue_mutex);
    impl->queue.clear();
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi)) RecordBufferQueue_GetState(SLAndroidSimpleBufferQueueItf self,
                                                              SLAndroidSimpleBufferQueueState *pState)
{
    if (!self || !pState) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, bq_vtbl));
    std::lock_guard<std::mutex> lock(impl->queue_mutex);
    pState->count = static_cast<SLuint32>(impl->queue.size());
    pState->index = 0;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi))
RecordBufferQueue_RegisterCallback(SLAndroidSimpleBufferQueueItf self,
                                   slAndroidSimpleBufferQueueCallback callback, void *pContext)
{
    if (!self) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *impl = reinterpret_cast<AudioRecorderObjectImpl *>(
        reinterpret_cast<char *>(self) - offsetof(AudioRecorderObjectImpl, bq_vtbl));
    impl->bq_callback = callback;
    impl->bq_context = pContext;
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi))
EngineItf_CreateAudioRecorder(SLEngineItf self, SLObjectItf *pRecorder, SLDataSource *pAudioSrc,
                              SLDataSink *pAudioSnk, SLuint32 numInterfaces,
                              const SLInterfaceID *pInterfaceIds,
                              const SLboolean *pInterfaceRequired)
{
    (void)self;
    (void)pAudioSrc;
    (void)numInterfaces;
    (void)pInterfaceIds;
    (void)pInterfaceRequired;
    if (!pRecorder || !pAudioSnk) {
        return SL_RESULT_PARAMETER_INVALID;
    }

    auto *recorder = new AudioRecorderObjectImpl();

    if (pAudioSnk->pFormat) {
        auto *pcm = static_cast<SLDataFormat_PCM *>(pAudioSnk->pFormat);
        if (pcm->formatType == SL_DATAFORMAT_PCM) {
            recorder->channels =
                (pcm->numChannels >= 1 && pcm->numChannels <= 2) ? pcm->numChannels : 1;
            uint32_t srate = pcm->samplesPerSec;
            if (srate >= 1000000) {
                srate /= 1000;
            }
            recorder->sample_rate = (srate > 0) ? srate : 48000;
            recorder->format =
                (pcm->bitsPerSample == 32) ? SNDRV_PCM_FORMAT_FLOAT_LE : SNDRV_PCM_FORMAT_S16_LE;
        }
    }

    *pRecorder = reinterpret_cast<SLObjectItf>(&recorder->obj_vtbl);
    return SL_RESULT_SUCCESS;
}

SLresult __attribute__((sysv_abi))
EngineItf_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pPlayer, SLDataSource *pAudioSrc,
                            SLDataSink *pAudioSnk, SLuint32 numInterfaces,
                            const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired)
{
    (void)self;
    (void)pAudioSnk;
    (void)numInterfaces;
    (void)pInterfaceIds;
    (void)pInterfaceRequired;
    if (!pPlayer || !pAudioSrc) {
        return SL_RESULT_PARAMETER_INVALID;
    }

    auto *player = new AudioPlayerObjectImpl();

    if (pAudioSrc->pFormat) {
        auto *pcm = static_cast<SLDataFormat_PCM *>(pAudioSrc->pFormat);
        if (pcm->formatType == SL_DATAFORMAT_PCM) {
            player->channels =
                (pcm->numChannels >= 1 && pcm->numChannels <= 2) ? pcm->numChannels : 2;
            uint32_t srate = pcm->samplesPerSec;
            if (srate >= 1000000) {
                srate /= 1000;
            }
            player->sample_rate = (srate > 0) ? srate : 48000;
            player->format =
                (pcm->bitsPerSample == 32) ? SNDRV_PCM_FORMAT_FLOAT_LE : SNDRV_PCM_FORMAT_S16_LE;
        }
    }

    *pPlayer = reinterpret_cast<SLObjectItf>(&player->obj_vtbl);
    return SL_RESULT_SUCCESS;
}

} // namespace

extern "C" {

SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions,
                        const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
                        const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired)
{
    (void)numOptions;
    (void)pEngineOptions;
    (void)numInterfaces;
    (void)pInterfaceIds;
    (void)pInterfaceRequired;
    if (!pEngine) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    auto *engine = new EngineObjectImpl();
    *pEngine = reinterpret_cast<SLObjectItf>(&engine->obj_vtbl);
    return SL_RESULT_SUCCESS;
}
}

} // namespace portage::audio
