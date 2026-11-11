#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace portage::ndk {

class NdkSymbolRegistry;

// LP64 Android x86_64 z_stream layout (112 bytes; differs from Windows LLP64 uLong = 4B)
struct GuestZStream {
    uint64_t next_in;
    uint32_t avail_in;
    uint32_t pad0;
    uint64_t total_in;
    uint64_t next_out;
    uint32_t avail_out;
    uint32_t pad1;
    uint64_t total_out;
    uint64_t msg;
    uint64_t state;
    uint64_t zalloc;
    uint64_t zfree;
    uint64_t opaque;
    int32_t data_type;
    uint32_t pad2;
    uint64_t adler;
    uint64_t reserved;
};

static_assert(sizeof(GuestZStream) == 112, "GuestZStream must match Android LP64 layout");

struct SysvVaList {
    uint32_t gp_offset;
    uint32_t fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
};

struct GuestTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

class PrintfArgSource {
  public:
    virtual ~PrintfArgSource() = default;
    virtual uintptr_t NextInt() = 0;
    virtual double NextDouble() = 0;
};

class RegArrayArgSource : public PrintfArgSource {
  public:
    RegArrayArgSource(const uintptr_t *ints, size_t int_count, const double *fps = nullptr,
                      size_t fp_count = 0)
        : ints_(ints), int_count_(int_count), fps_(fps), fp_count_(fp_count)
    {
    }

    uintptr_t NextInt() override { return (int_idx_ < int_count_) ? ints_[int_idx_++] : 0; }

    double NextDouble() override { return (fp_idx_ < fp_count_) ? fps_[fp_idx_++] : 0.0; }

  private:
    const uintptr_t *ints_{nullptr};
    size_t int_count_{0};
    size_t int_idx_{0};
    const double *fps_{nullptr};
    size_t fp_count_{0};
    size_t fp_idx_{0};
};

class VaListArgSource : public PrintfArgSource {
  public:
    explicit VaListArgSource(SysvVaList *list) : ap(list) {}
    uintptr_t NextInt() override;
    double NextDouble() override;

  private:
    SysvVaList *ap{nullptr};
};

int FormatSysvPrintf(char *dst, size_t cap, const char *fmt, PrintfArgSource &args);
std::string FormatSysvPrintfToString(const char *fmt, PrintfArgSource &args);

void RegisterBionicAndRuntimeThunks(NdkSymbolRegistry &registry);

} // namespace portage::ndk

extern "C" {

// zlib exports
__attribute__((sysv_abi)) const char *portage_zlibVersion(void);
__attribute__((sysv_abi)) int portage_inflateInit_(portage::ndk::GuestZStream *strm,
                                                   const char *version, int stream_size);
__attribute__((sysv_abi)) int portage_inflateInit2_(portage::ndk::GuestZStream *strm,
                                                    int windowBits, const char *version,
                                                    int stream_size);
__attribute__((sysv_abi)) int portage_inflate(portage::ndk::GuestZStream *strm, int flush);
__attribute__((sysv_abi)) int portage_inflateEnd(portage::ndk::GuestZStream *strm);
__attribute__((sysv_abi)) int portage_inflateReset(portage::ndk::GuestZStream *strm);
__attribute__((sysv_abi)) int portage_deflateInit_(portage::ndk::GuestZStream *strm, int level,
                                                   const char *version, int stream_size);
__attribute__((sysv_abi)) int portage_deflateInit2_(portage::ndk::GuestZStream *strm, int level,
                                                    int method, int windowBits, int memLevel,
                                                    int strategy, const char *version,
                                                    int stream_size);
__attribute__((sysv_abi)) int portage_deflate(portage::ndk::GuestZStream *strm, int flush);
__attribute__((sysv_abi)) int portage_deflateEnd(portage::ndk::GuestZStream *strm);
__attribute__((sysv_abi)) int portage_compress(uint8_t *dest, uint64_t *destLen,
                                               const uint8_t *source, uint64_t sourceLen);
__attribute__((sysv_abi)) int portage_compress2(uint8_t *dest, uint64_t *destLen,
                                                const uint8_t *source, uint64_t sourceLen,
                                                int level);
__attribute__((sysv_abi)) uint64_t portage_compressBound(uint64_t sourceLen);
__attribute__((sysv_abi)) int portage_uncompress(uint8_t *dest, uint64_t *destLen,
                                                 const uint8_t *source, uint64_t sourceLen);
__attribute__((sysv_abi)) uint64_t portage_crc32(uint64_t crc, const uint8_t *buf, uint32_t len);
__attribute__((sysv_abi)) uint64_t portage_adler32(uint64_t adler, const uint8_t *buf,
                                                   uint32_t len);

// libm exports
__attribute__((sysv_abi)) double portage_sin(double x);
__attribute__((sysv_abi)) float portage_sinf(float x);
__attribute__((sysv_abi)) double portage_cos(double x);
__attribute__((sysv_abi)) float portage_cosf(float x);
__attribute__((sysv_abi)) double portage_tan(double x);
__attribute__((sysv_abi)) float portage_tanf(float x);
__attribute__((sysv_abi)) double portage_asin(double x);
__attribute__((sysv_abi)) float portage_asinf(float x);
__attribute__((sysv_abi)) double portage_acos(double x);
__attribute__((sysv_abi)) float portage_acosf(float x);
__attribute__((sysv_abi)) double portage_atan(double x);
__attribute__((sysv_abi)) float portage_atanf(float x);
__attribute__((sysv_abi)) double portage_atan2(double y, double x);
__attribute__((sysv_abi)) float portage_atan2f(float y, float x);
__attribute__((sysv_abi)) double portage_sqrt(double x);
__attribute__((sysv_abi)) float portage_sqrtf(float x);
__attribute__((sysv_abi)) double portage_pow(double b, double e);
__attribute__((sysv_abi)) float portage_powf(float b, float e);
__attribute__((sysv_abi)) double portage_exp(double x);
__attribute__((sysv_abi)) float portage_expf(float x);
__attribute__((sysv_abi)) double portage_log(double x);
__attribute__((sysv_abi)) float portage_logf(float x);
__attribute__((sysv_abi)) double portage_log10(double x);
__attribute__((sysv_abi)) float portage_log10f(float x);
__attribute__((sysv_abi)) double portage_floor(double x);
__attribute__((sysv_abi)) float portage_floorf(float x);
__attribute__((sysv_abi)) double portage_ceil(double x);
__attribute__((sysv_abi)) float portage_ceilf(float x);
__attribute__((sysv_abi)) double portage_round(double x);
__attribute__((sysv_abi)) float portage_roundf(float x);
__attribute__((sysv_abi)) double portage_trunc(double x);
__attribute__((sysv_abi)) float portage_truncf(float x);
__attribute__((sysv_abi)) double portage_fmod(double x, double y);
__attribute__((sysv_abi)) float portage_fmodf(float x, float y);

// libc core & fortify exports
__attribute__((sysv_abi)) int *portage_errno(void);
__attribute__((sysv_abi)) void portage_stack_chk_fail(void);
__attribute__((sysv_abi)) uint32_t portage_arc4random(void);
__attribute__((sysv_abi)) void portage_arc4random_buf(void *buf, size_t nbytes);
__attribute__((sysv_abi)) uint32_t portage_arc4random_uniform(uint32_t upper_bound);
__attribute__((sysv_abi)) uint64_t portage_getauxval(uint64_t type);
__attribute__((sysv_abi)) void portage_abort(void);
__attribute__((sysv_abi)) const char *portage_strerror(int errnum);
__attribute__((sysv_abi)) int portage_strerror_r(int errnum, char *buf, size_t buflen);

__attribute__((sysv_abi)) void *portage_malloc(size_t size);
__attribute__((sysv_abi)) void *portage_calloc(size_t nmemb, size_t size);
__attribute__((sysv_abi)) void *portage_realloc(void *ptr, size_t size);
__attribute__((sysv_abi)) void portage_free(void *ptr);
__attribute__((sysv_abi)) int portage_posix_memalign(void **memptr, size_t alignment, size_t size);
__attribute__((sysv_abi)) void *portage_aligned_alloc(size_t alignment, size_t size);

__attribute__((sysv_abi)) void *portage_memcpy(void *dst, const void *src, size_t n);
__attribute__((sysv_abi)) void *portage_memmove(void *dst, const void *src, size_t n);
__attribute__((sysv_abi)) void *portage_memset(void *dst, int c, size_t n);
__attribute__((sysv_abi)) int portage_memcmp(const void *s1, const void *s2, size_t n);
__attribute__((sysv_abi)) size_t portage_strlen(const char *s);
__attribute__((sysv_abi)) size_t portage_strnlen(const char *s, size_t maxlen);
__attribute__((sysv_abi)) int portage_strcmp(const char *s1, const char *s2);
__attribute__((sysv_abi)) int portage_strncmp(const char *s1, const char *s2, size_t n);
__attribute__((sysv_abi)) char *portage_strcpy(char *dst, const char *src);
__attribute__((sysv_abi)) char *portage_strncpy(char *dst, const char *src, size_t n);

__attribute__((sysv_abi)) void *portage_memcpy_chk(void *dst, const void *src, size_t n,
                                                   size_t dst_len);
__attribute__((sysv_abi)) void *portage_memmove_chk(void *dst, const void *src, size_t n,
                                                    size_t dst_len);
__attribute__((sysv_abi)) void *portage_memset_chk(void *dst, int c, size_t n, size_t dst_len);
__attribute__((sysv_abi)) char *portage_strcpy_chk(char *dst, const char *src, size_t dst_len);
__attribute__((sysv_abi)) char *portage_strncpy_chk(char *dst, const char *src, size_t n,
                                                    size_t dst_len);
__attribute__((sysv_abi)) size_t portage_strlen_chk(const char *s, size_t s_len);

__attribute__((sysv_abi)) int portage_snprintf(char *dst, size_t cap, const char *fmt,
                                               uintptr_t a0 = 0, uintptr_t a1 = 0, uintptr_t a2 = 0,
                                               uintptr_t a3 = 0, uintptr_t a4 = 0,
                                               uintptr_t a5 = 0);
__attribute__((sysv_abi)) int portage_vsnprintf(char *dst, size_t cap, const char *fmt,
                                                portage::ndk::SysvVaList *ap);
__attribute__((sysv_abi)) int portage_snprintf_chk(char *dst, size_t cap, int flag, size_t osz,
                                                   const char *fmt, uintptr_t a0 = 0,
                                                   uintptr_t a1 = 0, uintptr_t a2 = 0,
                                                   uintptr_t a3 = 0, uintptr_t a4 = 0);
__attribute__((sysv_abi)) int portage_vsnprintf_chk(char *dst, size_t cap, int flag, size_t osz,
                                                    const char *fmt, portage::ndk::SysvVaList *ap);

// pthread sync & threads
__attribute__((sysv_abi)) int portage_pthread_once(int32_t *once_control,
                                                   __attribute__((sysv_abi)) void (*init_fn)(void));
__attribute__((sysv_abi)) int portage_pthread_mutexattr_init(void *attr);
__attribute__((sysv_abi)) int portage_pthread_mutexattr_destroy(void *attr);
__attribute__((sysv_abi)) int portage_pthread_mutexattr_settype(void *attr, int type);
__attribute__((sysv_abi)) int portage_pthread_mutex_init(void *mutex, const void *attr);
__attribute__((sysv_abi)) int portage_pthread_mutex_destroy(void *mutex);
__attribute__((sysv_abi)) int portage_pthread_mutex_lock(void *mutex);
__attribute__((sysv_abi)) int portage_pthread_mutex_trylock(void *mutex);
__attribute__((sysv_abi)) int portage_pthread_mutex_unlock(void *mutex);
__attribute__((sysv_abi)) int portage_pthread_cond_init(void *cond, const void *attr);
__attribute__((sysv_abi)) int portage_pthread_cond_destroy(void *cond);
__attribute__((sysv_abi)) int portage_pthread_cond_wait(void *cond, void *mutex);
__attribute__((sysv_abi)) int
portage_pthread_cond_timedwait(void *cond, void *mutex, const portage::ndk::GuestTimespec *abstime);
__attribute__((sysv_abi)) int portage_pthread_cond_signal(void *cond);
__attribute__((sysv_abi)) int portage_pthread_cond_broadcast(void *cond);
__attribute__((sysv_abi)) int
portage_pthread_key_create(uint32_t *key, __attribute__((sysv_abi)) void (*dtor)(void *));
__attribute__((sysv_abi)) int portage_pthread_key_delete(uint32_t key);
__attribute__((sysv_abi)) void *portage_pthread_getspecific(uint32_t key);
__attribute__((sysv_abi)) int portage_pthread_setspecific(uint32_t key, const void *value);
__attribute__((sysv_abi)) int portage_pthread_create(uintptr_t *thread_out, const void *attr,
                                                     void *start_routine, void *arg);
__attribute__((sysv_abi)) int portage_pthread_join(uintptr_t thread, void **retval);
__attribute__((sysv_abi)) int portage_pthread_detach(uintptr_t thread);

// libc++_shared ABI exports
__attribute__((sysv_abi)) void *portage_cxx_new(size_t size);
__attribute__((sysv_abi)) void *portage_cxx_new_array(size_t size);
__attribute__((sysv_abi)) void portage_cxx_delete(void *ptr);
__attribute__((sysv_abi)) void portage_cxx_delete_sized(void *ptr, size_t size);
__attribute__((sysv_abi)) int portage_cxa_guard_acquire(uint64_t *guard);
__attribute__((sysv_abi)) void portage_cxa_guard_release(uint64_t *guard);
__attribute__((sysv_abi)) void portage_cxa_guard_abort(uint64_t *guard);
__attribute__((sysv_abi)) void portage_cxa_pure_virtual(void);
__attribute__((sysv_abi)) void *portage_cxa_allocate_exception(size_t thrown_size);
__attribute__((sysv_abi)) void portage_cxa_free_exception(void *thrown_exception);
__attribute__((sysv_abi)) void portage_cxa_throw(void *thrown_exception, void *tinfo,
                                                 void (*dest)(void *));
__attribute__((sysv_abi)) void *portage_cxa_begin_catch(void *exception_object);
__attribute__((sysv_abi)) void portage_cxa_end_catch(void);
__attribute__((sysv_abi)) void portage_unwind_resume(void *exception_object);
__attribute__((sysv_abi)) int portage_gxx_personality_v0(int version, int actions,
                                                         uint64_t exceptionClass,
                                                         void *exceptionObject, void *context);

} // extern "C"
