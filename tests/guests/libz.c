#include <stddef.h>
#include <stdint.h>

__attribute__((visibility("default"))) const char *zlibVersion(void) { return "1.3.1"; }
__attribute__((visibility("default"))) int inflateInit_(void *s, const char *v, int sz)
{
    (void)s;
    (void)v;
    (void)sz;
    return 0;
}
__attribute__((visibility("default"))) int inflateInit2_(void *s, int w, const char *v, int sz)
{
    (void)s;
    (void)w;
    (void)v;
    (void)sz;
    return 0;
}
__attribute__((visibility("default"))) int inflate(void *s, int f)
{
    (void)s;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int inflateEnd(void *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int inflateReset(void *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int deflateInit_(void *s, int l, const char *v, int sz)
{
    (void)s;
    (void)l;
    (void)v;
    (void)sz;
    return 0;
}
__attribute__((visibility("default"))) int deflateInit2_(void *s, int l, int m, int w, int ml,
                                                         int st, const char *v, int sz)
{
    (void)s;
    (void)l;
    (void)m;
    (void)w;
    (void)ml;
    (void)st;
    (void)v;
    (void)sz;
    return 0;
}
__attribute__((visibility("default"))) int deflate(void *s, int f)
{
    (void)s;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int deflateEnd(void *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int compress(uint8_t *d, uint64_t *dl, const uint8_t *s,
                                                    uint64_t sl)
{
    (void)d;
    (void)dl;
    (void)s;
    (void)sl;
    return 0;
}
__attribute__((visibility("default"))) int compress2(uint8_t *d, uint64_t *dl, const uint8_t *s,
                                                     uint64_t sl, int l)
{
    (void)d;
    (void)dl;
    (void)s;
    (void)sl;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) uint64_t compressBound(uint64_t sl)
{
    (void)sl;
    return 0;
}
__attribute__((visibility("default"))) int uncompress(uint8_t *d, uint64_t *dl, const uint8_t *s,
                                                      uint64_t sl)
{
    (void)d;
    (void)dl;
    (void)s;
    (void)sl;
    return 0;
}
__attribute__((visibility("default"))) uint64_t crc32(uint64_t c, const uint8_t *b, uint32_t l)
{
    (void)c;
    (void)b;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) uint64_t adler32(uint64_t a, const uint8_t *b, uint32_t l)
{
    (void)a;
    (void)b;
    (void)l;
    return 0;
}
