#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::audio {

using SLresult = int32_t;
inline constexpr SLresult SL_RESULT_SUCCESS = 0x00000000;
inline constexpr SLresult SL_RESULT_PRECONDITIONS_VIOLATED = 0x00000001;
inline constexpr SLresult SL_RESULT_PARAMETER_INVALID = 0x00000002;
inline constexpr SLresult SL_RESULT_MEMORY_FAILURE = 0x00000003;
inline constexpr SLresult SL_RESULT_RESOURCE_ERROR = 0x00000004;
inline constexpr SLresult SL_RESULT_RESOURCE_LOST = 0x00000005;
inline constexpr SLresult SL_RESULT_IO_ERROR = 0x00000006;
inline constexpr SLresult SL_RESULT_BUFFER_INSUFFICIENT = 0x00000007;
inline constexpr SLresult SL_RESULT_CONTENT_CORRUPTED = 0x00000008;
inline constexpr SLresult SL_RESULT_CONTENT_UNSUPPORTED = 0x00000009;
inline constexpr SLresult SL_RESULT_CONTENT_NOT_FOUND = 0x0000000A;
inline constexpr SLresult SL_RESULT_PERMISSION_DENIED = 0x0000000B;
inline constexpr SLresult SL_RESULT_FEATURE_UNSUPPORTED = 0x0000000C;
inline constexpr SLresult SL_RESULT_INTERNAL_ERROR = 0x0000000D;
inline constexpr SLresult SL_RESULT_UNKNOWN_ERROR = 0x0000000E;
inline constexpr SLresult SL_RESULT_OPERATION_ABORTED = 0x0000000F;
inline constexpr SLresult SL_RESULT_CONTROL_LOST = 0x00000010;

using SLuint32 = uint32_t;
using SLint32 = int32_t;
using SLuint16 = uint16_t;
using SLint16 = int16_t;
using SLuint8 = uint8_t;
using SLint8 = int8_t;
using SLboolean = uint32_t;
using SLmillibel = int16_t;

inline constexpr SLboolean SL_BOOLEAN_FALSE = 0x00000000;
inline constexpr SLboolean SL_BOOLEAN_TRUE = 0x00000001;

inline constexpr SLuint32 SL_PLAYSTATE_STOPPED = 0x00000001;
inline constexpr SLuint32 SL_PLAYSTATE_PAUSED = 0x00000002;
inline constexpr SLuint32 SL_PLAYSTATE_PLAYING = 0x00000003;

inline constexpr SLuint32 SL_DATALOCATOR_BUFFERQUEUE = 0x00000002;
inline constexpr SLuint32 SL_DATALOCATOR_OUTPUTMIX = 0x00000003;
inline constexpr SLuint32 SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE = 0x800007BD;

inline constexpr SLuint32 SL_DATAFORMAT_PCM = 0x00000001;
inline constexpr SLuint32 SL_BYTEORDER_LITTLEENDIAN = 0x00000001;
inline constexpr SLuint32 SL_SPEAKER_FRONT_LEFT = 0x00000001;
inline constexpr SLuint32 SL_SPEAKER_FRONT_RIGHT = 0x00000002;

inline constexpr SLuint32 SL_SAMPLINGRATE_44_1 = 44100000;
inline constexpr SLuint32 SL_SAMPLINGRATE_48 = 48000000;
inline constexpr SLuint16 SL_PCMSAMPLEFORMAT_FIXED_16 = 16;
inline constexpr SLuint16 SL_PCMSAMPLEFORMAT_FIXED_32 = 32;

struct SLInterfaceID_ {
    SLuint32 time_low;
    SLuint16 time_mid;
    SLuint16 time_hi_and_version;
    SLuint8 clock_seq_hi_and_reserved;
    SLuint8 clock_seq_low;
    SLuint8 node[6];
};
using SLInterfaceID = const SLInterfaceID_ *;

inline constexpr SLInterfaceID_ SL_IID_ENGINE_VAL = {
    0x8d2adf00, 0x51c9, 0x11d8, 0x9e, 0x55, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
inline constexpr SLInterfaceID_ SL_IID_ANDROIDSIMPLEBUFFERQUEUE_VAL = {
    0x198e1ea0, 0x4143, 0x11df, 0x8b, 0x1c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
inline constexpr SLInterfaceID_ SL_IID_PLAY_VAL = {
    0xef064d60, 0x51c9, 0x11d8, 0xa4, 0x0d, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
inline constexpr SLInterfaceID_ SL_IID_VOLUME_VAL = {
    0x09e85e00, 0x51ca, 0x11d8, 0x9c, 0xb8, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
inline constexpr SLInterfaceID_ SL_IID_BUFFERQUEUE_VAL = {
    0x3ab16fb0, 0x51ca, 0x11d8, 0xa7, 0x88, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};

inline constexpr SLInterfaceID SL_IID_ENGINE = &SL_IID_ENGINE_VAL;
inline constexpr SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE =
    &SL_IID_ANDROIDSIMPLEBUFFERQUEUE_VAL;
inline constexpr SLInterfaceID SL_IID_PLAY = &SL_IID_PLAY_VAL;
inline constexpr SLInterfaceID SL_IID_VOLUME = &SL_IID_VOLUME_VAL;
inline constexpr SLInterfaceID SL_IID_BUFFERQUEUE = &SL_IID_BUFFERQUEUE_VAL;

struct SLObjectItf_;
using SLObjectItf = const struct SLObjectItf_ **;

struct SLEngineItf_;
using SLEngineItf = const struct SLEngineItf_ **;

struct SLPlayItf_;
using SLPlayItf = const struct SLPlayItf_ **;

struct SLAndroidSimpleBufferQueueItf_;
using SLAndroidSimpleBufferQueueItf = const struct SLAndroidSimpleBufferQueueItf_ **;

struct SLVolumeItf_;
using SLVolumeItf = const struct SLVolumeItf_ **;

using slObjectCallback = __attribute__((sysv_abi)) void (*)(SLObjectItf caller, const void *context,
                                                            SLuint32 event, SLresult result,
                                                            SLuint32 param, void *interface);

using slAndroidSimpleBufferQueueCallback =
    __attribute__((sysv_abi)) void (*)(SLAndroidSimpleBufferQueueItf caller, void *context);

struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf self, SLboolean async) __attribute__((sysv_abi));
    SLresult (*Resume)(SLObjectItf self, SLboolean async) __attribute__((sysv_abi));
    SLresult (*GetState)(SLObjectItf self, SLuint32 *pState) __attribute__((sysv_abi));
    SLresult (*GetInterface)(SLObjectItf self, const SLInterfaceID iid, void *pInterface)
        __attribute__((sysv_abi));
    SLresult (*RegisterCallback)(SLObjectItf self, slObjectCallback callback, void *pContext)
        __attribute__((sysv_abi));
    void (*AbortAsyncOperation)(SLObjectItf self) __attribute__((sysv_abi));
    void (*Destroy)(SLObjectItf self) __attribute__((sysv_abi));
    SLresult (*SetPriority)(SLObjectItf self, SLint32 priority, SLboolean preemptable)
        __attribute__((sysv_abi));
    SLresult (*GetPriority)(SLObjectItf self, SLint32 *pPriority, SLboolean *pPreemptable)
        __attribute__((sysv_abi));
    SLresult (*SetLossOfControlInterfaces)(SLObjectItf self, SLint32 numInterfaces,
                                           SLInterfaceID *pInterfaceIDs, SLboolean enabled)
        __attribute__((sysv_abi));
};

struct SLEngineOption {
    SLuint32 feature;
    SLuint32 data;
};

struct SLDataLocator_BufferQueue {
    SLuint32 locatorType;
    SLuint32 numBuffers;
};

struct SLDataLocator_AndroidSimpleBufferQueue {
    SLuint32 locatorType;
    SLuint32 numBuffers;
};

struct SLDataLocator_OutputMix {
    SLuint32 locatorType;
    SLObjectItf outputMix;
};

struct SLDataFormat_PCM {
    SLuint32 formatType;
    SLuint32 numChannels;
    SLuint32 samplesPerSec;
    SLuint32 bitsPerSample;
    SLuint32 containerSize;
    SLuint32 channelMask;
    SLuint32 endianness;
};

struct SLDataSource {
    void *pLocator;
    void *pFormat;
};

struct SLDataSink {
    void *pLocator;
    void *pFormat;
};

struct SLEngineItf_ {
    SLresult (*CreateLEDDevice)(SLEngineItf self, SLObjectItf *pDevice, SLuint32 deviceID,
                                SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateVibraDevice)(SLEngineItf self, SLObjectItf *pDevice, SLuint32 deviceID,
                                  SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                  const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateAudioPlayer)(SLEngineItf self, SLObjectItf *pPlayer, SLDataSource *pAudioSrc,
                                  SLDataSink *pAudioSnk, SLuint32 numInterfaces,
                                  const SLInterfaceID *pInterfaceIds,
                                  const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateAudioRecorder)(SLEngineItf self, SLObjectItf *pRecorder,
                                    SLDataSource *pAudioSrc, SLDataSink *pAudioSnk,
                                    SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                    const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateMidiPlayer)(SLEngineItf self, SLObjectItf *pPlayer, SLDataSource *pAudioSrc,
                                 SLDataSource *pBankSrc, SLDataSink *pAudioSnk,
                                 SLObjectItf pOutputMix, const SLInterfaceID *pInterfaceIds,
                                 const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateListener)(SLEngineItf self, SLObjectItf *pListener, SLuint32 numInterfaces,
                               const SLInterfaceID *pInterfaceIds,
                               const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*Create3DEnvironment)(SLEngineItf self, SLObjectItf *pEnvironment,
                                    SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                    const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
    SLresult (*CreateOutputMix)(SLEngineItf self, SLObjectItf *pMix, SLuint32 numInterfaces,
                                const SLInterfaceID *pInterfaceIds,
                                const SLboolean *pInterfaceRequired) __attribute__((sysv_abi));
};

struct SLPlayItf_ {
    SLresult (*SetPlayState)(SLPlayItf self, SLuint32 state) __attribute__((sysv_abi));
    SLresult (*GetPlayState)(SLPlayItf self, SLuint32 *pState) __attribute__((sysv_abi));
    SLresult (*GetDuration)(SLPlayItf self, SLuint32 *pMsec) __attribute__((sysv_abi));
    SLresult (*GetPosition)(SLPlayItf self, SLuint32 *pMsec) __attribute__((sysv_abi));
    SLresult (*RegisterCallback)(SLPlayItf self, void *callback, void *pContext)
        __attribute__((sysv_abi));
    SLresult (*SetCallbackEventsMask)(SLPlayItf self, SLuint32 eventFlags)
        __attribute__((sysv_abi));
    SLresult (*GetCallbackEventsMask)(SLPlayItf self, SLuint32 *pEventFlags)
        __attribute__((sysv_abi));
    SLresult (*SetMarkerPosition)(SLPlayItf self, SLuint32 mSec) __attribute__((sysv_abi));
    SLresult (*ClearMarkerPosition)(SLPlayItf self) __attribute__((sysv_abi));
    SLresult (*GetMarkerPosition)(SLPlayItf self, SLuint32 *pMsec) __attribute__((sysv_abi));
    SLresult (*SetPositionUpdatePeriod)(SLPlayItf self, SLuint32 mSec) __attribute__((sysv_abi));
    SLresult (*GetPositionUpdatePeriod)(SLPlayItf self, SLuint32 *pMsec) __attribute__((sysv_abi));
};

struct SLAndroidSimpleBufferQueueState {
    SLuint32 count;
    SLuint32 index;
};

struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf self, const void *pBuffer, SLuint32 size)
        __attribute__((sysv_abi));
    SLresult (*Clear)(SLAndroidSimpleBufferQueueItf self) __attribute__((sysv_abi));
    SLresult (*GetState)(SLAndroidSimpleBufferQueueItf self,
                         SLAndroidSimpleBufferQueueState *pState) __attribute__((sysv_abi));
    SLresult (*RegisterCallback)(SLAndroidSimpleBufferQueueItf self,
                                 slAndroidSimpleBufferQueueCallback callback, void *pContext)
        __attribute__((sysv_abi));
};

struct SLVolumeItf_ {
    SLresult (*SetVolumeLevel)(SLVolumeItf self, SLmillibel level) __attribute__((sysv_abi));
    SLresult (*GetVolumeLevel)(SLVolumeItf self, SLmillibel *pLevel) __attribute__((sysv_abi));
    SLresult (*GetMaxVolumeLevel)(SLVolumeItf self, SLmillibel *pMaxLevel)
        __attribute__((sysv_abi));
    SLresult (*SetMute)(SLVolumeItf self, SLboolean mute) __attribute__((sysv_abi));
    SLresult (*GetMute)(SLVolumeItf self, SLboolean *pMute) __attribute__((sysv_abi));
    SLresult (*EnableStereoPosition)(SLVolumeItf self, SLboolean enable) __attribute__((sysv_abi));
    SLresult (*IsEnabledStereoPosition)(SLVolumeItf self, SLboolean *pEnable)
        __attribute__((sysv_abi));
    SLresult (*SetStereoPosition)(SLVolumeItf self, SLmillibel position) __attribute__((sysv_abi));
    SLresult (*GetStereoPosition)(SLVolumeItf self, SLmillibel *pPosition)
        __attribute__((sysv_abi));
};

} // namespace portage::audio
