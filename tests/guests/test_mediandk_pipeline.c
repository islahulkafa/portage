#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void AMediaFormat;
typedef void AMediaExtractor;
typedef void AMediaCodec;
typedef void AMediaMuxer;
typedef void ANativeWindow;

typedef struct AMediaCodecBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;

typedef struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
} ANativeWindow_Buffer;

typedef void (*AMediaCodecOnAsyncInputAvailable)(AMediaCodec *codec, void *userdata, int32_t index);
typedef void (*AMediaCodecOnAsyncOutputAvailable)(AMediaCodec *codec, void *userdata, int32_t index,
                                                  AMediaCodecBufferInfo *bufferInfo);
typedef void (*AMediaCodecOnAsyncFormatChanged)(AMediaCodec *codec, void *userdata,
                                                AMediaFormat *format);
typedef void (*AMediaCodecOnAsyncError)(AMediaCodec *codec, void *userdata, int32_t error,
                                        int32_t actionCode, const char *detail);

typedef struct AMediaCodecOnAsyncNotifyCallback {
    AMediaCodecOnAsyncInputAvailable onAsyncInputAvailable;
    AMediaCodecOnAsyncOutputAvailable onAsyncOutputAvailable;
    AMediaCodecOnAsyncFormatChanged onAsyncFormatChanged;
    AMediaCodecOnAsyncError onAsyncError;
} AMediaCodecOnAsyncNotifyCallback;

extern AMediaFormat *AMediaFormat_new(void);
extern int32_t AMediaFormat_delete(AMediaFormat *format);
extern bool AMediaFormat_getInt32(AMediaFormat *format, const char *name, int32_t *out);
extern void AMediaFormat_setString(AMediaFormat *format, const char *name, const char *value);
extern void AMediaFormat_setInt32(AMediaFormat *format, const char *name, int32_t value);
extern void AMediaFormat_setFloat(AMediaFormat *format, const char *name, float value);

extern AMediaExtractor *AMediaExtractor_new(void);
extern int32_t AMediaExtractor_delete(AMediaExtractor *extractor);
extern int32_t AMediaExtractor_setDataSourceFd(AMediaExtractor *extractor, int fd, int64_t offset,
                                               int64_t length);
extern size_t AMediaExtractor_getTrackCount(AMediaExtractor *extractor);
extern AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *extractor, size_t idx);

extern AMediaCodec *AMediaCodec_createDecoderByType(const char *mime_type);
extern AMediaCodec *AMediaCodec_createEncoderByType(const char *mime_type);
extern int32_t AMediaCodec_delete(AMediaCodec *codec);
extern int32_t AMediaCodec_configure(AMediaCodec *codec, const AMediaFormat *format,
                                     ANativeWindow *surface, void *crypto, uint32_t flags);
extern int32_t AMediaCodec_start(AMediaCodec *codec);
extern int32_t AMediaCodec_stop(AMediaCodec *codec);
extern uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size);
extern int32_t AMediaCodec_queueInputBuffer(AMediaCodec *codec, size_t idx, int64_t offset,
                                            size_t size, uint64_t timeUs, uint32_t flags);
extern int64_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec, AMediaCodecBufferInfo *info,
                                               int64_t timeoutUs);
extern uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec, size_t idx, size_t *out_size);
extern AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *codec);
extern int32_t AMediaCodec_releaseOutputBuffer(AMediaCodec *codec, size_t idx, bool render);
extern int32_t AMediaCodec_signalEndOfInputStream(AMediaCodec *codec);
extern int32_t AMediaCodec_createInputSurface(AMediaCodec *codec, ANativeWindow **surface);
extern int32_t AMediaCodec_setAsyncNotifyCallback(AMediaCodec *codec,
                                                  AMediaCodecOnAsyncNotifyCallback callback,
                                                  void *userdata);

extern AMediaMuxer *AMediaMuxer_new(int fd, int32_t format);
extern int32_t AMediaMuxer_delete(AMediaMuxer *muxer);
extern int64_t AMediaMuxer_addTrack(AMediaMuxer *muxer, const AMediaFormat *format);
extern int32_t AMediaMuxer_start(AMediaMuxer *muxer);
extern int32_t AMediaMuxer_writeSampleData(AMediaMuxer *muxer, size_t trackIdx, const uint8_t *data,
                                           const AMediaCodecBufferInfo *info);
extern int32_t AMediaMuxer_stop(AMediaMuxer *muxer);

extern int32_t ANativeWindow_lock(ANativeWindow *window, ANativeWindow_Buffer *outBuffer,
                                  void *dirty);
extern int32_t ANativeWindow_unlockAndPost(ANativeWindow *window);
extern void ANativeWindow_release(ANativeWindow *window);

struct timespec_raw {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"((int64_t)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static inline int sys_openat(const char *path, int flags, int mode)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = mode;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"((int64_t)-100), "S"(path), "d"((int64_t)flags), "r"(r10)
                     : "rcx", "r11", "memory");
    return (int)ret;
}

static inline int sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"((int64_t)fd) : "rcx", "r11", "memory");
    return (int)ret;
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

static uint8_t g_sample_storage[8][32768];
static uint32_t g_sample_sizes[8];
static int64_t g_sample_pts[8];
static uint32_t g_sample_flags[8];
static uint32_t g_sample_count = 0;

static volatile uint32_t g_async_next_idx = 0;
static volatile int32_t g_async_decoded_frames = 0;
static volatile int32_t g_async_done = 0;

static void on_async_in(AMediaCodec *codec, void *userdata, int32_t index)
{
    (void)userdata;
    uint32_t cur = __atomic_load_n(&g_async_next_idx, __ATOMIC_RELAXED);
    if (cur >= g_sample_count) {
        return;
    }
    size_t cap = 0;
    uint8_t *dst = AMediaCodec_getInputBuffer(codec, (size_t)index, &cap);
    uint32_t sz = g_sample_sizes[cur];
    if (!dst || cap < sz) {
        return;
    }
    for (uint32_t i = 0; i < sz; ++i) {
        dst[i] = g_sample_storage[cur][i];
    }
    uint32_t fl = (cur + 1 == g_sample_count) ? 4u : 0u;
    AMediaCodec_queueInputBuffer(codec, (size_t)index, 0, sz, (uint64_t)g_sample_pts[cur], fl);
    __atomic_store_n(&g_async_next_idx, cur + 1, __ATOMIC_RELEASE);
}

static void on_async_out(AMediaCodec *codec, void *userdata, int32_t index,
                         AMediaCodecBufferInfo *info)
{
    (void)userdata;
    if (info && info->size > 0) {
        __atomic_add_fetch(&g_async_decoded_frames, 1, __ATOMIC_RELAXED);
    }
    AMediaCodec_releaseOutputBuffer(codec, (size_t)index, false);
    if (info && (info->flags & 4u) != 0) {
        __atomic_store_n(&g_async_done, 1, __ATOMIC_RELEASE);
    }
}

void _start(void)
{
    AMediaCodec *enc = AMediaCodec_createEncoderByType("video/avc");
    if (!enc) {
        sys_exit(1);
    }
    AMediaFormat *vfmt = AMediaFormat_new();
    AMediaFormat_setString(vfmt, "mime", "video/avc");
    AMediaFormat_setInt32(vfmt, "width", 128);
    AMediaFormat_setInt32(vfmt, "height", 128);
    AMediaFormat_setInt32(vfmt, "bitrate", 800000);
    AMediaFormat_setFloat(vfmt, "frame-rate", 30.0f);
    if (AMediaCodec_configure(enc, vfmt, (ANativeWindow *)0, (void *)0, 1u) != 0) {
        sys_exit(2);
    }
    ANativeWindow *surf = (ANativeWindow *)0;
    if (AMediaCodec_createInputSurface(enc, &surf) != 0 || !surf) {
        sys_exit(3);
    }
    if (AMediaCodec_start(enc) != 0) {
        sys_exit(4);
    }

    for (int f = 0; f < 3; ++f) {
        ANativeWindow_Buffer wb;
        if (ANativeWindow_lock(surf, &wb, (void *)0) != 0 || !wb.bits) {
            sys_exit(5);
        }
        uint8_t *px = (uint8_t *)wb.bits;
        for (int32_t y = 0; y < wb.height; ++y) {
            uint8_t *row = px + (size_t)(y * wb.stride) * 4u;
            for (int32_t x = 0; x < wb.width; ++x) {
                row[x * 4 + 0] = 20;
                row[x * 4 + 1] = 200;
                row[x * 4 + 2] = 40;
                row[x * 4 + 3] = 255;
            }
        }
        if (ANativeWindow_unlockAndPost(surf) != 0) {
            sys_exit(6);
        }
    }
    AMediaCodec_signalEndOfInputStream(enc);

    const char *mp4_path = "/data/local/tmp/guest_out.mp4";
    int out_fd = sys_openat(mp4_path, 01102, 0644);
    if (out_fd < 0) {
        sys_exit(7);
    }
    AMediaMuxer *mux = AMediaMuxer_new(out_fd, 0);
    if (!mux) {
        sys_exit(8);
    }

    AMediaFormat *out_vfmt = (AMediaFormat *)0;
    for (int spin = 0; spin < 20 && g_sample_count < 8; ++spin) {
        AMediaCodecBufferInfo info;
        int64_t idx = AMediaCodec_dequeueOutputBuffer(enc, &info, 20000);
        if (idx == -2) {
            if (out_vfmt) {
                AMediaFormat_delete(out_vfmt);
            }
            out_vfmt = AMediaCodec_getOutputFormat(enc);
            continue;
        }
        if (idx < 0) {
            break;
        }
        if (!out_vfmt) {
            out_vfmt = AMediaCodec_getOutputFormat(enc);
        }
        size_t cap = 0;
        uint8_t *buf = AMediaCodec_getOutputBuffer(enc, (size_t)idx, &cap);
        if (buf && info.size > 0 && (size_t)info.size <= sizeof(g_sample_storage[0])) {
            uint32_t slot = g_sample_count++;
            g_sample_sizes[slot] = (uint32_t)info.size;
            g_sample_pts[slot] = info.presentationTimeUs;
            g_sample_flags[slot] = info.flags;
            for (int32_t k = 0; k < info.size; ++k) {
                g_sample_storage[slot][k] = buf[info.offset + k];
            }
        }
        AMediaCodec_releaseOutputBuffer(enc, (size_t)idx, false);
        if ((info.flags & 4u) != 0) {
            break;
        }
    }
    if (g_sample_count == 0 || !out_vfmt) {
        sys_exit(9);
    }
    int64_t tid = AMediaMuxer_addTrack(mux, out_vfmt);
    if (tid < 0 || AMediaMuxer_start(mux) != 0) {
        sys_exit(10);
    }
    for (uint32_t s = 0; s < g_sample_count; ++s) {
        AMediaCodecBufferInfo wi;
        wi.offset = 0;
        wi.size = (int32_t)g_sample_sizes[s];
        wi.presentationTimeUs = g_sample_pts[s];
        wi.flags = g_sample_flags[s];
        if (AMediaMuxer_writeSampleData(mux, (size_t)tid, g_sample_storage[s], &wi) != 0) {
            sys_exit(11);
        }
    }
    if (AMediaMuxer_stop(mux) != 0) {
        sys_exit(12);
    }
    AMediaMuxer_delete(mux);
    sys_close(out_fd);
    AMediaFormat_delete(out_vfmt);
    AMediaCodec_stop(enc);
    AMediaCodec_delete(enc);
    ANativeWindow_release(surf);

    int in_fd = sys_openat(mp4_path, 0, 0);
    if (in_fd < 0) {
        sys_exit(13);
    }
    AMediaExtractor *ex = AMediaExtractor_new();
    if (AMediaExtractor_setDataSourceFd(ex, in_fd, 0, -1) != 0 ||
        AMediaExtractor_getTrackCount(ex) < 1) {
        sys_exit(14);
    }
    AMediaFormat *ex_fmt = AMediaExtractor_getTrackFormat(ex, 0);
    int32_t ex_w = 0;
    if (!ex_fmt || !AMediaFormat_getInt32(ex_fmt, "width", &ex_w) || ex_w != 128) {
        sys_exit(15);
    }
    AMediaFormat_delete(ex_fmt);
    AMediaExtractor_delete(ex);
    sys_close(in_fd);

    AMediaCodec *dec = AMediaCodec_createDecoderByType("video/avc");
    if (!dec || AMediaCodec_configure(dec, vfmt, (ANativeWindow *)0, (void *)0, 0) != 0) {
        sys_exit(16);
    }
    AMediaCodecOnAsyncNotifyCallback cb;
    cb.onAsyncInputAvailable = on_async_in;
    cb.onAsyncOutputAvailable = on_async_out;
    cb.onAsyncFormatChanged = (AMediaCodecOnAsyncFormatChanged)0;
    cb.onAsyncError = (AMediaCodecOnAsyncError)0;
    if (AMediaCodec_setAsyncNotifyCallback(dec, cb, (void *)0) != 0 ||
        AMediaCodec_start(dec) != 0) {
        sys_exit(17);
    }

    for (int wait_it = 0; wait_it < 400; ++wait_it) {
        if (__atomic_load_n(&g_async_done, __ATOMIC_ACQUIRE) != 0) {
            break;
        }
        sys_sleep_ms(5);
    }
    AMediaCodec_stop(dec);
    AMediaCodec_delete(dec);
    AMediaFormat_delete(vfmt);

    if (__atomic_load_n(&g_async_done, __ATOMIC_ACQUIRE) == 0 ||
        __atomic_load_n(&g_async_decoded_frames, __ATOMIC_RELAXED) == 0) {
        sys_exit(18);
    }

    sys_exit(0);
}
