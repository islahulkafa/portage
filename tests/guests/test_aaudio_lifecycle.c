#include <stdint.h>

#define AAUDIO_OK 0
#define AAUDIO_STREAM_STATE_STARTED 4
#define AAUDIO_STREAM_STATE_PAUSED 6
#define AAUDIO_STREAM_STATE_FLUSHED 8
#define AAUDIO_STREAM_STATE_STOPPED 10
#define AAUDIO_FORMAT_PCM_I16 1
#define AAUDIO_CALLBACK_RESULT_CONTINUE 0
#define AAUDIO_PERFORMANCE_MODE_LOW_LATENCY 12

typedef void AAudioStreamBuilder;
typedef void AAudioStream;
typedef int32_t (*AAudioStream_dataCallback)(AAudioStream *stream, void *userData, void *audioData,
                                             int32_t numFrames);

extern int32_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder);
extern void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *builder, int32_t sampleRate);
extern void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channelCount);
extern void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, int32_t format);
extern void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder, int32_t mode);
extern void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *builder,
                                                         int32_t frames);
extern void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder,
                                                AAudioStream_dataCallback callback, void *userData);
extern int32_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream);
extern int32_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder);
extern int32_t AAudioStream_requestStart(AAudioStream *stream);
extern int32_t AAudioStream_requestPause(AAudioStream *stream);
extern int32_t AAudioStream_requestFlush(AAudioStream *stream);
extern int32_t AAudioStream_requestStop(AAudioStream *stream);
extern int32_t AAudioStream_close(AAudioStream *stream);
extern int32_t AAudioStream_getState(AAudioStream *stream);
extern int64_t AAudioStream_getFramesWritten(AAudioStream *stream);
extern int32_t AAudioStream_getPerformanceMode(AAudioStream *stream);

struct timespec_raw {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"((int64_t)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static inline void sys_sleep_ms(int64_t ms)
{
    struct timespec_raw req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(35), "D"(&req), "S"((uint64_t)0)
                     : "rcx", "r11", "memory");
}

static volatile int32_t g_callback_calls = 0;

static int32_t guest_data_callback(AAudioStream *stream, void *userData, void *audioData,
                                   int32_t numFrames)
{
    (void)stream;
    (void)userData;
    int16_t *pcm = (int16_t *)audioData;
    for (int32_t i = 0; i < numFrames * 2; ++i) {
        pcm[i] = 250;
    }
    __atomic_add_fetch(&g_callback_calls, 1, __ATOMIC_RELAXED);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void _start(void)
{
    AAudioStreamBuilder *builder = (AAudioStreamBuilder *)0;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
        sys_exit(1);
    }

    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 256);
    AAudioStreamBuilder_setDataCallback(builder, guest_data_callback, (void *)0);

    AAudioStream *stream = (AAudioStream *)0;
    if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK || !stream) {
        sys_exit(2);
    }
    AAudioStreamBuilder_delete(builder);

    if (AAudioStream_getPerformanceMode(stream) != AAUDIO_PERFORMANCE_MODE_LOW_LATENCY) {
        sys_exit(3);
    }

    if (AAudioStream_requestStart(stream) != AAUDIO_OK ||
        AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_STARTED) {
        sys_exit(4);
    }

    for (int i = 0; i < 40 && AAudioStream_getFramesWritten(stream) == 0; ++i) {
        sys_sleep_ms(1);
    }
    if (AAudioStream_getFramesWritten(stream) <= 0 ||
        __atomic_load_n(&g_callback_calls, __ATOMIC_RELAXED) <= 0) {
        sys_exit(5);
    }

    if (AAudioStream_requestPause(stream) != AAUDIO_OK ||
        AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_PAUSED) {
        sys_exit(6);
    }

    if (AAudioStream_requestFlush(stream) != AAUDIO_OK ||
        AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_FLUSHED) {
        sys_exit(7);
    }

    if (AAudioStream_requestStart(stream) != AAUDIO_OK ||
        AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_STARTED) {
        sys_exit(8);
    }

    if (AAudioStream_requestStop(stream) != AAUDIO_OK ||
        AAudioStream_getState(stream) != AAUDIO_STREAM_STATE_STOPPED) {
        sys_exit(9);
    }

    if (AAudioStream_close(stream) != AAUDIO_OK) {
        sys_exit(9);
    }

    sys_exit(0);
}
