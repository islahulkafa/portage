#include <stdint.h>

__attribute__((visibility("default"))) int32_t AAudio_createStreamBuilder(void **builder)
{
    (void)builder;
    return 0;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setSampleRate(void *b, int32_t r)
{
    (void)b;
    (void)r;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setChannelCount(void *b, int32_t c)
{
    (void)b;
    (void)c;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setFormat(void *b, int32_t f)
{
    (void)b;
    (void)f;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setDirection(void *b, int32_t d)
{
    (void)b;
    (void)d;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setDataCallback(void *b, void *cb,
                                                                                void *u)
{
    (void)b;
    (void)cb;
    (void)u;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setErrorCallback(void *b, void *cb,
                                                                                 void *u)
{
    (void)b;
    (void)cb;
    (void)u;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setFramesPerDataCallback(void *b,
                                                                                         int32_t f)
{
    (void)b;
    (void)f;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setBufferCapacityInFrames(void *b,
                                                                                          int32_t n)
{
    (void)b;
    (void)n;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setPerformanceMode(void *b,
                                                                                   int32_t m)
{
    (void)b;
    (void)m;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setSharingMode(void *b, int32_t m)
{
    (void)b;
    (void)m;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setUsage(void *b, int32_t usage)
{
    (void)b;
    (void)usage;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setContentType(void *b,
                                                                               int32_t type)
{
    (void)b;
    (void)type;
}

__attribute__((visibility("default"))) void AAudioStreamBuilder_setInputPreset(void *b,
                                                                               int32_t preset)
{
    (void)b;
    (void)preset;
}

__attribute__((visibility("default"))) int32_t AAudioStreamBuilder_openStream(void *b, void **s)
{
    (void)b;
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStreamBuilder_delete(void *b)
{
    (void)b;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_requestStart(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_requestPause(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_requestFlush(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_requestStop(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_close(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_waitForStateChange(void *s,
                                                                               int32_t inputState,
                                                                               int32_t *nextState,
                                                                               int64_t timeoutNs)
{
    (void)s;
    (void)inputState;
    (void)timeoutNs;
    if (nextState) {
        *nextState = inputState;
    }
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_write(void *s, const void *b, int32_t n,
                                                                  int64_t t)
{
    (void)s;
    (void)b;
    (void)t;
    return n;
}

__attribute__((visibility("default"))) int32_t AAudioStream_read(void *s, void *b, int32_t n,
                                                                 int64_t t)
{
    (void)s;
    (void)b;
    (void)t;
    return n;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getSampleRate(void *s)
{
    (void)s;
    return 48000;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getChannelCount(void *s)
{
    (void)s;
    return 2;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getFormat(void *s)
{
    (void)s;
    return 1;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getState(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getFramesPerDataCallback(void *s)
{
    (void)s;
    return 512;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getBufferSizeInFrames(void *s)
{
    (void)s;
    return 4096;
}

__attribute__((visibility("default"))) int64_t AAudioStream_getFramesWritten(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int64_t AAudioStream_getFramesRead(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getTimestamp(void *s, int32_t clockid,
                                                                         int64_t *framePosition,
                                                                         int64_t *timeNanoseconds)
{
    (void)s;
    (void)clockid;
    (void)framePosition;
    (void)timeNanoseconds;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getXRunCount(void *s)
{
    (void)s;
    return 0;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getPerformanceMode(void *s)
{
    (void)s;
    return 10;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getSharingMode(void *s)
{
    (void)s;
    return 1;
}

__attribute__((visibility("default"))) int32_t AAudioStream_getDeviceId(void *s)
{
    (void)s;
    return 0;
}
