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
