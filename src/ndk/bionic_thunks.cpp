#include "ndk/bionic_thunks.hpp"
#include "arch/guest_context.hpp"
#include "ndk/android_log.hpp"
#include "ndk/bionic_locale.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/bionic_stdio.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "thread/thread_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <malloc.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portage::ndk {

namespace {

alignas(16) const uint64_t g_stack_chk_guard = 0x595a4b3c2d1e0f00ULL;
thread_local int g_bionic_errno = 0;

// --- LP64 z_stream side-table adapter ---
std::mutex g_zlib_mutex;
std::unordered_map<uintptr_t, std::unique_ptr<z_stream>> g_zlib_streams;

void SyncGuestToHostZStream(const GuestZStream &gs, z_stream &hs)
{
    hs.next_in = reinterpret_cast<Bytef *>(static_cast<uintptr_t>(gs.next_in));
    hs.avail_in = gs.avail_in;
    hs.next_out = reinterpret_cast<Bytef *>(static_cast<uintptr_t>(gs.next_out));
    hs.avail_out = gs.avail_out;
}

void SyncHostToGuestZStream(const z_stream &hs, GuestZStream &gs)
{
    gs.next_in = reinterpret_cast<uint64_t>(hs.next_in);
    gs.avail_in = hs.avail_in;
    gs.total_in = hs.total_in;
    gs.next_out = reinterpret_cast<uint64_t>(hs.next_out);
    gs.avail_out = hs.avail_out;
    gs.total_out = hs.total_out;
    gs.msg = reinterpret_cast<uint64_t>(hs.msg);
    gs.data_type = hs.data_type;
    gs.adler = hs.adler;
}

} // namespace

// --- SysV printf formatting ---
enum class PrintfLenMod { None, Long, LongLong, SizeT };

uintptr_t VaListArgSource::NextInt()
{
    if (!ap) {
        return 0;
    }
    if (ap->gp_offset <= 40 && ap->reg_save_area) {
        uintptr_t val = 0;
        std::memcpy(&val, static_cast<const uint8_t *>(ap->reg_save_area) + ap->gp_offset,
                    sizeof(uintptr_t));
        ap->gp_offset += 8;
        return val;
    }
    if (ap->overflow_arg_area) {
        uintptr_t val = 0;
        std::memcpy(&val, ap->overflow_arg_area, sizeof(uintptr_t));
        ap->overflow_arg_area = static_cast<uint8_t *>(ap->overflow_arg_area) + 8;
        return val;
    }
    return 0;
}

double VaListArgSource::NextDouble()
{
    if (!ap) {
        return 0.0;
    }
    if (ap->fp_offset <= 160 && ap->reg_save_area) {
        double val = 0.0;
        std::memcpy(&val, static_cast<const uint8_t *>(ap->reg_save_area) + ap->fp_offset,
                    sizeof(double));
        ap->fp_offset += 16;
        return val;
    }
    if (ap->overflow_arg_area) {
        double val = 0.0;
        std::memcpy(&val, ap->overflow_arg_area, sizeof(double));
        ap->overflow_arg_area = static_cast<uint8_t *>(ap->overflow_arg_area) + 8;
        return val;
    }
    return 0.0;
}

std::string FormatSysvPrintfToString(const char *fmt, PrintfArgSource &args)
{
    if (!fmt) {
        return {};
    }

    std::string result;
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            result.push_back(*p++);
            continue;
        }
        const char *spec_start = p++;
        if (*p == '%') {
            result.push_back('%');
            p++;
            continue;
        }

        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        if (*p == '*') {
            (void)args.NextInt();
            p++;
        }
        if (*p == '.') {
            p++;
            if (*p == '*') {
                (void)args.NextInt();
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    p++;
                }
            }
        }

        PrintfLenMod len_mod = PrintfLenMod::None;
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                p++;
                len_mod = PrintfLenMod::LongLong;
            } else {
                len_mod = PrintfLenMod::Long;
            }
        } else if (*p == 'z' || *p == 't') {
            p++;
            len_mod = PrintfLenMod::SizeT;
        } else if (*p == 'h') {
            p++;
            if (*p == 'h') {
                p++;
            }
        }

        char conv = *p;
        if (!conv) {
            break;
        }
        p++;
        std::string subfmt(spec_start, static_cast<size_t>(p - spec_start));
        char chunk[256]{};

        if (conv == 's') {
            const char *s = reinterpret_cast<const char *>(args.NextInt());
            const char *safe = s ? s : "(null)";
            int needed = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), safe);
            if (needed >= static_cast<int>(sizeof(chunk))) {
                std::string big(static_cast<size_t>(needed) + 1, '\0');
                std::snprintf(big.data(), big.size(), subfmt.c_str(), safe);
                result.append(big.data(), static_cast<size_t>(needed));
            } else if (needed > 0) {
                result.append(chunk, static_cast<size_t>(needed));
            }
        } else if (conv == 'c') {
            int c = static_cast<int>(args.NextInt());
            int n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), c);
            if (n > 0) {
                result.append(chunk, static_cast<size_t>(n));
            }
        } else if (conv == 'p') {
            void *ptr = reinterpret_cast<void *>(args.NextInt());
            int n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), ptr);
            if (n > 0) {
                result.append(chunk, static_cast<size_t>(n));
            }
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' ||
                   conv == 'G') {
            double d = args.NextDouble();
            int n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), d);
            if (n > 0) {
                result.append(chunk, static_cast<size_t>(n));
            }
        } else if (conv == 'd' || conv == 'i') {
            uintptr_t raw = args.NextInt();
            int n = 0;
            if (len_mod == PrintfLenMod::None) {
                n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), static_cast<int32_t>(raw));
            } else {
                // On LLP64 Windows, rewrite "%ld" -> "%lld" before passing 64-bit LP64 value
                if (len_mod == PrintfLenMod::Long) {
                    subfmt.replace(subfmt.rfind('l'), 1, "ll");
                }
                n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), static_cast<int64_t>(raw));
            }
            if (n > 0) {
                result.append(chunk, static_cast<size_t>(n));
            }
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            uintptr_t raw = args.NextInt();
            int n = 0;
            if (len_mod == PrintfLenMod::None) {
                n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), static_cast<uint32_t>(raw));
            } else {
                if (len_mod == PrintfLenMod::Long) {
                    subfmt.replace(subfmt.rfind('l'), 1, "ll");
                }
                n = std::snprintf(chunk, sizeof(chunk), subfmt.c_str(), static_cast<uint64_t>(raw));
            }
            if (n > 0) {
                result.append(chunk, static_cast<size_t>(n));
            }
        }
    }
    return result;
}

int FormatSysvPrintf(char *dst, size_t cap, const char *fmt, PrintfArgSource &args)
{
    std::string result = FormatSysvPrintfToString(fmt, args);
    if (dst && cap > 0) {
        size_t copy_len = std::min(cap - 1, result.size());
        if (copy_len > 0) {
            std::memcpy(dst, result.data(), copy_len);
        }
        dst[copy_len] = '\0';
    }
    return static_cast<int>(result.size());
}

namespace {

// --- Pthread sync side-table (safe for 40B mutex / 48B cond LP64 storage) ---
struct HostMutexState {
    std::mutex mtx{};
    bool recursive{false};
    DWORD owner_tid{0};
    uint32_t depth{0};
    std::condition_variable unlock_cv{};

    void lock()
    {
        if (!recursive) {
            mtx.lock();
            return;
        }
        DWORD me = GetCurrentThreadId();
        std::unique_lock<std::mutex> lk(mtx);
        unlock_cv.wait(lk, [&]() { return depth == 0 || owner_tid == me; });
        owner_tid = me;
        depth++;
    }

    bool try_lock()
    {
        if (!recursive) {
            return mtx.try_lock();
        }
        DWORD me = GetCurrentThreadId();
        std::lock_guard<std::mutex> lk(mtx);
        if (depth == 0 || owner_tid == me) {
            owner_tid = me;
            depth++;
            return true;
        }
        return false;
    }

    void unlock()
    {
        if (!recursive) {
            mtx.unlock();
            return;
        }
        std::lock_guard<std::mutex> lk(mtx);
        if (depth > 0) {
            depth--;
            if (depth == 0) {
                owner_tid = 0;
                unlock_cv.notify_one();
            }
        }
    }
};

struct HostCondState {
    std::condition_variable_any cv{};
};

std::mutex g_pthread_sync_mutex;
std::unordered_map<uintptr_t, std::shared_ptr<HostMutexState>> g_host_mutexes;
std::unordered_map<uintptr_t, std::shared_ptr<HostCondState>> g_host_conds;
std::unordered_map<uintptr_t, int> g_mutex_attrs;

std::shared_ptr<HostMutexState> GetOrCreateHostMutex(void *mutex)
{
    std::lock_guard<std::mutex> lk(g_pthread_sync_mutex);
    uintptr_t key = reinterpret_cast<uintptr_t>(mutex);
    auto it = g_host_mutexes.find(key);
    if (it != g_host_mutexes.end()) {
        return it->second;
    }
    auto state = std::make_shared<HostMutexState>();
    g_host_mutexes[key] = state;
    return state;
}

std::shared_ptr<HostCondState> GetOrCreateHostCond(void *cond)
{
    std::lock_guard<std::mutex> lk(g_pthread_sync_mutex);
    uintptr_t key = reinterpret_cast<uintptr_t>(cond);
    auto it = g_host_conds.find(key);
    if (it != g_host_conds.end()) {
        return it->second;
    }
    auto state = std::make_shared<HostCondState>();
    g_host_conds[key] = state;
    return state;
}

std::mutex g_once_mutex;
std::condition_variable g_once_cv;

// --- Pthread TLS keys ---
struct KeyEntry {
    __attribute__((sysv_abi)) void (*dtor)(void *){nullptr};
    bool active{true};
};
std::mutex g_keys_mutex;
uint32_t g_next_key = 1;
std::unordered_map<uint32_t, KeyEntry> g_keys;
thread_local std::unordered_map<uint32_t, void *> g_tls_values;

void RunCurrentThreadTlsDestructors()
{
    for (int pass = 0; pass < 4 && !g_tls_values.empty(); ++pass) {
        auto snapshot = g_tls_values;
        bool invoked = false;
        for (const auto &[k, val] : snapshot) {
            if (!val) {
                continue;
            }
            __attribute__((sysv_abi)) void (*dtor)(void *) = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_keys_mutex);
                auto kit = g_keys.find(k);
                if (kit != g_keys.end() && kit->second.active) {
                    dtor = kit->second.dtor;
                }
            }
            if (dtor) {
                g_tls_values[k] = nullptr;
                dtor(val);
                invoked = true;
            }
        }
        if (!invoked) {
            break;
        }
    }
    g_tls_values.clear();
}

// --- Host-managed pthread_create threads ---
struct HostPthreadRecord {
    std::thread thread{};
    void *retval{nullptr};
    bool detached{false};
    portage::thread::BionicTcb *tcb{nullptr};

    ~HostPthreadRecord()
    {
        if (thread.joinable()) {
            thread.join();
        }
        delete tcb;
    }
};

std::mutex g_pthreads_mutex;
std::unordered_map<uintptr_t, std::shared_ptr<HostPthreadRecord>> g_pthreads;

// --- Itanium C++ ABI __cxa_guard ---
std::mutex g_cxa_guard_mutex;
std::condition_variable g_cxa_guard_cv;

} // namespace

void RegisterBionicAndRuntimeThunks(NdkSymbolRegistry &registry)
{
    RegisterBionicStdio(registry);
    RegisterBionicPosix(registry);
    RegisterBionicLocale(registry);

    // libz
    registry.Register("zlibVersion", reinterpret_cast<uintptr_t>(&portage_zlibVersion));
    registry.Register("inflateInit_", reinterpret_cast<uintptr_t>(&portage_inflateInit_));
    registry.Register("inflateInit2_", reinterpret_cast<uintptr_t>(&portage_inflateInit2_));
    registry.Register("inflate", reinterpret_cast<uintptr_t>(&portage_inflate));
    registry.Register("inflateEnd", reinterpret_cast<uintptr_t>(&portage_inflateEnd));
    registry.Register("inflateReset", reinterpret_cast<uintptr_t>(&portage_inflateReset));
    registry.Register("deflateInit_", reinterpret_cast<uintptr_t>(&portage_deflateInit_));
    registry.Register("deflateInit2_", reinterpret_cast<uintptr_t>(&portage_deflateInit2_));
    registry.Register("deflate", reinterpret_cast<uintptr_t>(&portage_deflate));
    registry.Register("deflateEnd", reinterpret_cast<uintptr_t>(&portage_deflateEnd));
    registry.Register("compress", reinterpret_cast<uintptr_t>(&portage_compress));
    registry.Register("compress2", reinterpret_cast<uintptr_t>(&portage_compress2));
    registry.Register("compressBound", reinterpret_cast<uintptr_t>(&portage_compressBound));
    registry.Register("uncompress", reinterpret_cast<uintptr_t>(&portage_uncompress));
    registry.Register("crc32", reinterpret_cast<uintptr_t>(&portage_crc32));
    registry.Register("adler32", reinterpret_cast<uintptr_t>(&portage_adler32));

    // libm
    registry.Register("sin", reinterpret_cast<uintptr_t>(&portage_sin));
    registry.Register("sinf", reinterpret_cast<uintptr_t>(&portage_sinf));
    registry.Register("cos", reinterpret_cast<uintptr_t>(&portage_cos));
    registry.Register("cosf", reinterpret_cast<uintptr_t>(&portage_cosf));
    registry.Register("tan", reinterpret_cast<uintptr_t>(&portage_tan));
    registry.Register("tanf", reinterpret_cast<uintptr_t>(&portage_tanf));
    registry.Register("asin", reinterpret_cast<uintptr_t>(&portage_asin));
    registry.Register("asinf", reinterpret_cast<uintptr_t>(&portage_asinf));
    registry.Register("acos", reinterpret_cast<uintptr_t>(&portage_acos));
    registry.Register("acosf", reinterpret_cast<uintptr_t>(&portage_acosf));
    registry.Register("atan", reinterpret_cast<uintptr_t>(&portage_atan));
    registry.Register("atanf", reinterpret_cast<uintptr_t>(&portage_atanf));
    registry.Register("atan2", reinterpret_cast<uintptr_t>(&portage_atan2));
    registry.Register("atan2f", reinterpret_cast<uintptr_t>(&portage_atan2f));
    registry.Register("sqrt", reinterpret_cast<uintptr_t>(&portage_sqrt));
    registry.Register("sqrtf", reinterpret_cast<uintptr_t>(&portage_sqrtf));
    registry.Register("pow", reinterpret_cast<uintptr_t>(&portage_pow));
    registry.Register("powf", reinterpret_cast<uintptr_t>(&portage_powf));
    registry.Register("exp", reinterpret_cast<uintptr_t>(&portage_exp));
    registry.Register("expf", reinterpret_cast<uintptr_t>(&portage_expf));
    registry.Register("log", reinterpret_cast<uintptr_t>(&portage_log));
    registry.Register("logf", reinterpret_cast<uintptr_t>(&portage_logf));
    registry.Register("log10", reinterpret_cast<uintptr_t>(&portage_log10));
    registry.Register("log10f", reinterpret_cast<uintptr_t>(&portage_log10f));
    registry.Register("floor", reinterpret_cast<uintptr_t>(&portage_floor));
    registry.Register("floorf", reinterpret_cast<uintptr_t>(&portage_floorf));
    registry.Register("ceil", reinterpret_cast<uintptr_t>(&portage_ceil));
    registry.Register("ceilf", reinterpret_cast<uintptr_t>(&portage_ceilf));
    registry.Register("round", reinterpret_cast<uintptr_t>(&portage_round));
    registry.Register("roundf", reinterpret_cast<uintptr_t>(&portage_roundf));
    registry.Register("trunc", reinterpret_cast<uintptr_t>(&portage_trunc));
    registry.Register("truncf", reinterpret_cast<uintptr_t>(&portage_truncf));
    registry.Register("fmod", reinterpret_cast<uintptr_t>(&portage_fmod));
    registry.Register("fmodf", reinterpret_cast<uintptr_t>(&portage_fmodf));

    // libc core & fortify
    registry.Register("__errno", reinterpret_cast<uintptr_t>(&portage_errno));
    registry.Register("__stack_chk_guard", reinterpret_cast<uintptr_t>(&g_stack_chk_guard));
    registry.Register("__stack_chk_fail", reinterpret_cast<uintptr_t>(&portage_stack_chk_fail));
    registry.Register("arc4random", reinterpret_cast<uintptr_t>(&portage_arc4random));
    registry.Register("arc4random_buf", reinterpret_cast<uintptr_t>(&portage_arc4random_buf));
    registry.Register("arc4random_uniform",
                      reinterpret_cast<uintptr_t>(&portage_arc4random_uniform));
    registry.Register("getauxval", reinterpret_cast<uintptr_t>(&portage_getauxval));
    registry.Register("abort", reinterpret_cast<uintptr_t>(&portage_abort));
    registry.Register("strerror", reinterpret_cast<uintptr_t>(&portage_strerror));
    registry.Register("strerror_r", reinterpret_cast<uintptr_t>(&portage_strerror_r));

    registry.Register("malloc", reinterpret_cast<uintptr_t>(&portage_malloc));
    registry.Register("calloc", reinterpret_cast<uintptr_t>(&portage_calloc));
    registry.Register("realloc", reinterpret_cast<uintptr_t>(&portage_realloc));
    registry.Register("free", reinterpret_cast<uintptr_t>(&portage_free));
    registry.Register("posix_memalign", reinterpret_cast<uintptr_t>(&portage_posix_memalign));
    registry.Register("aligned_alloc", reinterpret_cast<uintptr_t>(&portage_aligned_alloc));
    registry.Register("memalign", reinterpret_cast<uintptr_t>(&portage_aligned_alloc));

    registry.Register("memcpy", reinterpret_cast<uintptr_t>(&portage_memcpy));
    registry.Register("memmove", reinterpret_cast<uintptr_t>(&portage_memmove));
    registry.Register("memset", reinterpret_cast<uintptr_t>(&portage_memset));
    registry.Register("memcmp", reinterpret_cast<uintptr_t>(&portage_memcmp));
    registry.Register("strlen", reinterpret_cast<uintptr_t>(&portage_strlen));
    registry.Register("strnlen", reinterpret_cast<uintptr_t>(&portage_strnlen));
    registry.Register("strcmp", reinterpret_cast<uintptr_t>(&portage_strcmp));
    registry.Register("strncmp", reinterpret_cast<uintptr_t>(&portage_strncmp));
    registry.Register("strcpy", reinterpret_cast<uintptr_t>(&portage_strcpy));
    registry.Register("strncpy", reinterpret_cast<uintptr_t>(&portage_strncpy));

    registry.Register("__memcpy_chk", reinterpret_cast<uintptr_t>(&portage_memcpy_chk));
    registry.Register("__memmove_chk", reinterpret_cast<uintptr_t>(&portage_memmove_chk));
    registry.Register("__memset_chk", reinterpret_cast<uintptr_t>(&portage_memset_chk));
    registry.Register("__strcpy_chk", reinterpret_cast<uintptr_t>(&portage_strcpy_chk));
    registry.Register("__strncpy_chk", reinterpret_cast<uintptr_t>(&portage_strncpy_chk));
    registry.Register("__strlen_chk", reinterpret_cast<uintptr_t>(&portage_strlen_chk));
    registry.Register("snprintf", reinterpret_cast<uintptr_t>(&portage_snprintf));
    registry.Register("vsnprintf", reinterpret_cast<uintptr_t>(&portage_vsnprintf));
    registry.Register("__snprintf_chk", reinterpret_cast<uintptr_t>(&portage_snprintf_chk));
    registry.Register("__vsnprintf_chk", reinterpret_cast<uintptr_t>(&portage_vsnprintf_chk));

    // pthread synchronization & threads
    registry.Register("pthread_once", reinterpret_cast<uintptr_t>(&portage_pthread_once));
    registry.Register("pthread_mutexattr_init",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutexattr_init));
    registry.Register("pthread_mutexattr_destroy",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutexattr_destroy));
    registry.Register("pthread_mutexattr_settype",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutexattr_settype));
    registry.Register("pthread_mutex_init",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutex_init));
    registry.Register("pthread_mutex_destroy",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutex_destroy));
    registry.Register("pthread_mutex_lock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutex_lock));
    registry.Register("pthread_mutex_trylock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutex_trylock));
    registry.Register("pthread_mutex_unlock",
                      reinterpret_cast<uintptr_t>(&portage_pthread_mutex_unlock));
    registry.Register("pthread_cond_init", reinterpret_cast<uintptr_t>(&portage_pthread_cond_init));
    registry.Register("pthread_cond_destroy",
                      reinterpret_cast<uintptr_t>(&portage_pthread_cond_destroy));
    registry.Register("pthread_cond_wait", reinterpret_cast<uintptr_t>(&portage_pthread_cond_wait));
    registry.Register("pthread_cond_timedwait",
                      reinterpret_cast<uintptr_t>(&portage_pthread_cond_timedwait));
    registry.Register("pthread_cond_signal",
                      reinterpret_cast<uintptr_t>(&portage_pthread_cond_signal));
    registry.Register("pthread_cond_broadcast",
                      reinterpret_cast<uintptr_t>(&portage_pthread_cond_broadcast));
    registry.Register("pthread_key_create",
                      reinterpret_cast<uintptr_t>(&portage_pthread_key_create));
    registry.Register("pthread_key_delete",
                      reinterpret_cast<uintptr_t>(&portage_pthread_key_delete));
    registry.Register("pthread_getspecific",
                      reinterpret_cast<uintptr_t>(&portage_pthread_getspecific));
    registry.Register("pthread_setspecific",
                      reinterpret_cast<uintptr_t>(&portage_pthread_setspecific));
    registry.Register("pthread_create", reinterpret_cast<uintptr_t>(&portage_pthread_create));
    registry.Register("pthread_join", reinterpret_cast<uintptr_t>(&portage_pthread_join));
    registry.Register("pthread_detach", reinterpret_cast<uintptr_t>(&portage_pthread_detach));

    // libc++_shared ABI fallback stubs
    registry.RegisterFallback("_Znwm", reinterpret_cast<uintptr_t>(&portage_cxx_new));
    registry.RegisterFallback("_Znam", reinterpret_cast<uintptr_t>(&portage_cxx_new_array));
    registry.RegisterFallback("_ZdlPv", reinterpret_cast<uintptr_t>(&portage_cxx_delete));
    registry.RegisterFallback("_ZdaPv", reinterpret_cast<uintptr_t>(&portage_cxx_delete));
    registry.RegisterFallback("_ZdlPvm", reinterpret_cast<uintptr_t>(&portage_cxx_delete_sized));
    registry.RegisterFallback("_ZdaPvm", reinterpret_cast<uintptr_t>(&portage_cxx_delete_sized));
    registry.RegisterFallback("__cxa_guard_acquire",
                              reinterpret_cast<uintptr_t>(&portage_cxa_guard_acquire));
    registry.RegisterFallback("__cxa_guard_release",
                              reinterpret_cast<uintptr_t>(&portage_cxa_guard_release));
    registry.RegisterFallback("__cxa_guard_abort",
                              reinterpret_cast<uintptr_t>(&portage_cxa_guard_abort));
    registry.RegisterFallback("__cxa_pure_virtual",
                              reinterpret_cast<uintptr_t>(&portage_cxa_pure_virtual));
    registry.RegisterFallback("__cxa_allocate_exception",
                              reinterpret_cast<uintptr_t>(&portage_cxa_allocate_exception));
    registry.RegisterFallback("__cxa_free_exception",
                              reinterpret_cast<uintptr_t>(&portage_cxa_free_exception));
    registry.RegisterFallback("__cxa_throw", reinterpret_cast<uintptr_t>(&portage_cxa_throw));
    registry.RegisterFallback("__cxa_begin_catch",
                              reinterpret_cast<uintptr_t>(&portage_cxa_begin_catch));
    registry.RegisterFallback("__cxa_end_catch",
                              reinterpret_cast<uintptr_t>(&portage_cxa_end_catch));
    registry.RegisterFallback("_Unwind_Resume",
                              reinterpret_cast<uintptr_t>(&portage_unwind_resume));
    registry.RegisterFallback("__gxx_personality_v0",
                              reinterpret_cast<uintptr_t>(&portage_gxx_personality_v0));
}

} // namespace portage::ndk

extern "C" {

// --- zlib ---
__attribute__((sysv_abi)) const char *portage_zlibVersion(void) { return zlibVersion(); }

__attribute__((sysv_abi)) int portage_inflateInit2_(portage::ndk::GuestZStream *strm,
                                                    int windowBits, const char *version,
                                                    int stream_size)
{
    if (!strm || stream_size <= 0) {
        return Z_STREAM_ERROR;
    }
    auto hs = std::make_unique<z_stream>();
    std::memset(hs.get(), 0, sizeof(z_stream));
    int rc = inflateInit2_(hs.get(), windowBits, version ? version : ZLIB_VERSION,
                           static_cast<int>(sizeof(z_stream)));
    if (rc == Z_OK) {
        portage::ndk::SyncHostToGuestZStream(*hs, *strm);
        strm->state = reinterpret_cast<uint64_t>(hs.get());
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        portage::ndk::g_zlib_streams[reinterpret_cast<uintptr_t>(strm)] = std::move(hs);
    }
    return rc;
}

__attribute__((sysv_abi)) int portage_inflateInit_(portage::ndk::GuestZStream *strm,
                                                   const char *version, int stream_size)
{
    return portage_inflateInit2_(strm, MAX_WBITS, version, stream_size);
}

__attribute__((sysv_abi)) int portage_inflate(portage::ndk::GuestZStream *strm, int flush)
{
    if (!strm) {
        return Z_STREAM_ERROR;
    }
    z_stream *hs = nullptr;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        auto it = portage::ndk::g_zlib_streams.find(reinterpret_cast<uintptr_t>(strm));
        if (it != portage::ndk::g_zlib_streams.end()) {
            hs = it->second.get();
        }
    }
    if (!hs) {
        return Z_STREAM_ERROR;
    }
    portage::ndk::SyncGuestToHostZStream(*strm, *hs);
    int rc = inflate(hs, flush);
    portage::ndk::SyncHostToGuestZStream(*hs, *strm);
    return rc;
}

__attribute__((sysv_abi)) int portage_inflateEnd(portage::ndk::GuestZStream *strm)
{
    if (!strm) {
        return Z_STREAM_ERROR;
    }
    std::unique_ptr<z_stream> hs;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        auto it = portage::ndk::g_zlib_streams.find(reinterpret_cast<uintptr_t>(strm));
        if (it != portage::ndk::g_zlib_streams.end()) {
            hs = std::move(it->second);
            portage::ndk::g_zlib_streams.erase(it);
        }
    }
    if (!hs) {
        return Z_STREAM_ERROR;
    }
    int rc = inflateEnd(hs.get());
    strm->state = 0;
    return rc;
}

__attribute__((sysv_abi)) int portage_inflateReset(portage::ndk::GuestZStream *strm)
{
    if (!strm) {
        return Z_STREAM_ERROR;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
    auto it = portage::ndk::g_zlib_streams.find(reinterpret_cast<uintptr_t>(strm));
    if (it == portage::ndk::g_zlib_streams.end()) {
        return Z_STREAM_ERROR;
    }
    int rc = inflateReset(it->second.get());
    portage::ndk::SyncHostToGuestZStream(*it->second, *strm);
    return rc;
}

__attribute__((sysv_abi)) int portage_deflateInit2_(portage::ndk::GuestZStream *strm, int level,
                                                    int method, int windowBits, int memLevel,
                                                    int strategy, const char *version,
                                                    int stream_size)
{
    if (!strm || stream_size <= 0) {
        return Z_STREAM_ERROR;
    }
    auto hs = std::make_unique<z_stream>();
    std::memset(hs.get(), 0, sizeof(z_stream));
    int rc = deflateInit2_(hs.get(), level, method, windowBits, memLevel, strategy,
                           version ? version : ZLIB_VERSION, static_cast<int>(sizeof(z_stream)));
    if (rc == Z_OK) {
        portage::ndk::SyncHostToGuestZStream(*hs, *strm);
        strm->state = reinterpret_cast<uint64_t>(hs.get());
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        portage::ndk::g_zlib_streams[reinterpret_cast<uintptr_t>(strm)] = std::move(hs);
    }
    return rc;
}

__attribute__((sysv_abi)) int portage_deflateInit_(portage::ndk::GuestZStream *strm, int level,
                                                   const char *version, int stream_size)
{
    return portage_deflateInit2_(strm, level, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY, version,
                                 stream_size);
}

__attribute__((sysv_abi)) int portage_deflate(portage::ndk::GuestZStream *strm, int flush)
{
    if (!strm) {
        return Z_STREAM_ERROR;
    }
    z_stream *hs = nullptr;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        auto it = portage::ndk::g_zlib_streams.find(reinterpret_cast<uintptr_t>(strm));
        if (it != portage::ndk::g_zlib_streams.end()) {
            hs = it->second.get();
        }
    }
    if (!hs) {
        return Z_STREAM_ERROR;
    }
    portage::ndk::SyncGuestToHostZStream(*strm, *hs);
    int rc = deflate(hs, flush);
    portage::ndk::SyncHostToGuestZStream(*hs, *strm);
    return rc;
}

__attribute__((sysv_abi)) int portage_deflateEnd(portage::ndk::GuestZStream *strm)
{
    if (!strm) {
        return Z_STREAM_ERROR;
    }
    std::unique_ptr<z_stream> hs;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_zlib_mutex);
        auto it = portage::ndk::g_zlib_streams.find(reinterpret_cast<uintptr_t>(strm));
        if (it != portage::ndk::g_zlib_streams.end()) {
            hs = std::move(it->second);
            portage::ndk::g_zlib_streams.erase(it);
        }
    }
    if (!hs) {
        return Z_STREAM_ERROR;
    }
    int rc = deflateEnd(hs.get());
    strm->state = 0;
    return rc;
}

__attribute__((sysv_abi)) int portage_compress2(uint8_t *dest, uint64_t *destLen,
                                                const uint8_t *source, uint64_t sourceLen,
                                                int level)
{
    if (!dest || !destLen) {
        return Z_BUF_ERROR;
    }
    uLongf host_dest_len = static_cast<uLongf>(*destLen);
    int rc = compress2(dest, &host_dest_len, source, static_cast<uLong>(sourceLen), level);
    *destLen = static_cast<uint64_t>(host_dest_len);
    return rc;
}

__attribute__((sysv_abi)) int portage_compress(uint8_t *dest, uint64_t *destLen,
                                               const uint8_t *source, uint64_t sourceLen)
{
    return portage_compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

__attribute__((sysv_abi)) uint64_t portage_compressBound(uint64_t sourceLen)
{
    return static_cast<uint64_t>(compressBound(static_cast<uLong>(sourceLen)));
}

__attribute__((sysv_abi)) int portage_uncompress(uint8_t *dest, uint64_t *destLen,
                                                 const uint8_t *source, uint64_t sourceLen)
{
    if (!dest || !destLen) {
        return Z_BUF_ERROR;
    }
    uLongf host_dest_len = static_cast<uLongf>(*destLen);
    int rc = uncompress(dest, &host_dest_len, source, static_cast<uLong>(sourceLen));
    *destLen = static_cast<uint64_t>(host_dest_len);
    return rc;
}

__attribute__((sysv_abi)) uint64_t portage_crc32(uint64_t crc, const uint8_t *buf, uint32_t len)
{
    return static_cast<uint64_t>(crc32(static_cast<uLong>(crc), buf, len));
}

__attribute__((sysv_abi)) uint64_t portage_adler32(uint64_t adler, const uint8_t *buf, uint32_t len)
{
    return static_cast<uint64_t>(adler32(static_cast<uLong>(adler), buf, len));
}

// --- libm ---
__attribute__((sysv_abi)) double portage_sin(double x) { return std::sin(x); }
__attribute__((sysv_abi)) float portage_sinf(float x) { return std::sinf(x); }
__attribute__((sysv_abi)) double portage_cos(double x) { return std::cos(x); }
__attribute__((sysv_abi)) float portage_cosf(float x) { return std::cosf(x); }
__attribute__((sysv_abi)) double portage_tan(double x) { return std::tan(x); }
__attribute__((sysv_abi)) float portage_tanf(float x) { return std::tanf(x); }
__attribute__((sysv_abi)) double portage_asin(double x) { return std::asin(x); }
__attribute__((sysv_abi)) float portage_asinf(float x) { return std::asinf(x); }
__attribute__((sysv_abi)) double portage_acos(double x) { return std::acos(x); }
__attribute__((sysv_abi)) float portage_acosf(float x) { return std::acosf(x); }
__attribute__((sysv_abi)) double portage_atan(double x) { return std::atan(x); }
__attribute__((sysv_abi)) float portage_atanf(float x) { return std::atanf(x); }
__attribute__((sysv_abi)) double portage_atan2(double y, double x) { return std::atan2(y, x); }
__attribute__((sysv_abi)) float portage_atan2f(float y, float x) { return std::atan2f(y, x); }
__attribute__((sysv_abi)) double portage_sqrt(double x) { return std::sqrt(x); }
__attribute__((sysv_abi)) float portage_sqrtf(float x) { return std::sqrtf(x); }
__attribute__((sysv_abi)) double portage_pow(double b, double e) { return std::pow(b, e); }
__attribute__((sysv_abi)) float portage_powf(float b, float e) { return std::powf(b, e); }
__attribute__((sysv_abi)) double portage_exp(double x) { return std::exp(x); }
__attribute__((sysv_abi)) float portage_expf(float x) { return std::expf(x); }
__attribute__((sysv_abi)) double portage_log(double x) { return std::log(x); }
__attribute__((sysv_abi)) float portage_logf(float x) { return std::logf(x); }
__attribute__((sysv_abi)) double portage_log10(double x) { return std::log10(x); }
__attribute__((sysv_abi)) float portage_log10f(float x) { return std::log10f(x); }
__attribute__((sysv_abi)) double portage_floor(double x) { return std::floor(x); }
__attribute__((sysv_abi)) float portage_floorf(float x) { return std::floorf(x); }
__attribute__((sysv_abi)) double portage_ceil(double x) { return std::ceil(x); }
__attribute__((sysv_abi)) float portage_ceilf(float x) { return std::ceilf(x); }
__attribute__((sysv_abi)) double portage_round(double x) { return std::round(x); }
__attribute__((sysv_abi)) float portage_roundf(float x) { return std::roundf(x); }
__attribute__((sysv_abi)) double portage_trunc(double x) { return std::trunc(x); }
__attribute__((sysv_abi)) float portage_truncf(float x) { return std::truncf(x); }
__attribute__((sysv_abi)) double portage_fmod(double x, double y) { return std::fmod(x, y); }
__attribute__((sysv_abi)) float portage_fmodf(float x, float y) { return std::fmodf(x, y); }

// --- libc core & fortify ---
__attribute__((sysv_abi)) int *portage_errno(void) { return &portage::ndk::g_bionic_errno; }

__attribute__((sysv_abi)) void portage_stack_chk_fail(void)
{
    portage::ndk::LogcatBuffer::Instance().Append(ANDROID_LOG_FATAL, "portage-libc",
                                                  "stack smashing detected");
    std::abort();
}

__attribute__((sysv_abi)) uint32_t portage_arc4random(void)
{
    uint32_t val = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&val), sizeof(val),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return val;
}

__attribute__((sysv_abi)) void portage_arc4random_buf(void *buf, size_t nbytes)
{
    if (buf && nbytes > 0) {
        BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), static_cast<ULONG>(nbytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    }
}

__attribute__((sysv_abi)) uint32_t portage_arc4random_uniform(uint32_t upper_bound)
{
    if (upper_bound < 2) {
        return 0;
    }
    uint32_t min = (-upper_bound) % upper_bound;
    uint32_t r = 0;
    do {
        r = portage_arc4random();
    } while (r < min);
    return r % upper_bound;
}

__attribute__((sysv_abi)) uint64_t portage_getauxval(uint64_t type)
{
    if (type == 6 /* AT_PAGESZ */) {
        return 4096ULL;
    }
    if (type == 17 /* AT_CLKTCK */) {
        return 100ULL;
    }
    if (type == 25 /* AT_RANDOM */) {
        static uint8_t random_bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        return reinterpret_cast<uint64_t>(random_bytes);
    }
    return 0ULL;
}

__attribute__((sysv_abi)) void portage_abort(void) { std::abort(); }

__attribute__((sysv_abi)) const char *portage_strerror(int errnum) { return std::strerror(errnum); }

__attribute__((sysv_abi)) int portage_strerror_r(int errnum, char *buf, size_t buflen)
{
    if (!buf || buflen == 0) {
        return 22;
    }
    const char *msg = std::strerror(errnum);
    if (!msg) {
        return 22;
    }
    std::snprintf(buf, buflen, "%s", msg);
    return 0;
}

__attribute__((sysv_abi)) void *portage_malloc(size_t size) { return std::malloc(size); }

__attribute__((sysv_abi)) void *portage_calloc(size_t nmemb, size_t size)
{
    return std::calloc(nmemb, size);
}

__attribute__((sysv_abi)) void *portage_realloc(void *ptr, size_t size)
{
    return std::realloc(ptr, size);
}

namespace {
std::mutex g_aligned_allocs_mutex;
std::unordered_map<uintptr_t, void *> g_aligned_blocks;
} // namespace

__attribute__((sysv_abi)) void portage_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    void *base = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_aligned_allocs_mutex);
        auto it = g_aligned_blocks.find(reinterpret_cast<uintptr_t>(ptr));
        if (it != g_aligned_blocks.end()) {
            base = it->second;
            g_aligned_blocks.erase(it);
        }
    }
    std::free(base ? base : ptr);
}

__attribute__((sysv_abi)) int portage_posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!memptr || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return 22;
    }
    size_t total = size + alignment;
    void *base = std::malloc(total > 0 ? total : 1);
    if (!base) {
        return 12;
    }
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(base) + alignment - 1) & ~(alignment - 1);
    {
        std::lock_guard<std::mutex> lk(g_aligned_allocs_mutex);
        g_aligned_blocks[aligned] = base;
    }
    *memptr = reinterpret_cast<void *>(aligned);
    return 0;
}

__attribute__((sysv_abi)) void *portage_aligned_alloc(size_t alignment, size_t size)
{
    void *out = nullptr;
    if (portage_posix_memalign(&out, std::max(alignment, sizeof(void *)), size) == 0) {
        return out;
    }
    return nullptr;
}

__attribute__((sysv_abi)) void *portage_memcpy(void *dst, const void *src, size_t n)
{
    return std::memcpy(dst, src, n);
}

__attribute__((sysv_abi)) void *portage_memmove(void *dst, const void *src, size_t n)
{
    return std::memmove(dst, src, n);
}

__attribute__((sysv_abi)) void *portage_memset(void *dst, int c, size_t n)
{
    return std::memset(dst, c, n);
}

__attribute__((sysv_abi)) int portage_memcmp(const void *s1, const void *s2, size_t n)
{
    return std::memcmp(s1, s2, n);
}

__attribute__((sysv_abi)) size_t portage_strlen(const char *s) { return s ? std::strlen(s) : 0; }

__attribute__((sysv_abi)) size_t portage_strnlen(const char *s, size_t maxlen)
{
    if (!s) {
        return 0;
    }
    size_t i = 0;
    while (i < maxlen && s[i] != '\0') {
        i++;
    }
    return i;
}

__attribute__((sysv_abi)) int portage_strcmp(const char *s1, const char *s2)
{
    return std::strcmp(s1, s2);
}

__attribute__((sysv_abi)) int portage_strncmp(const char *s1, const char *s2, size_t n)
{
    return std::strncmp(s1, s2, n);
}

__attribute__((sysv_abi)) char *portage_strcpy(char *dst, const char *src)
{
    return std::strcpy(dst, src);
}

__attribute__((sysv_abi)) char *portage_strncpy(char *dst, const char *src, size_t n)
{
    return std::strncpy(dst, src, n);
}

__attribute__((sysv_abi)) void *portage_memcpy_chk(void *dst, const void *src, size_t n,
                                                   size_t dst_len)
{
    if (n > dst_len) {
        portage_stack_chk_fail();
    }
    return std::memcpy(dst, src, n);
}

__attribute__((sysv_abi)) void *portage_memmove_chk(void *dst, const void *src, size_t n,
                                                    size_t dst_len)
{
    if (n > dst_len) {
        portage_stack_chk_fail();
    }
    return std::memmove(dst, src, n);
}

__attribute__((sysv_abi)) void *portage_memset_chk(void *dst, int c, size_t n, size_t dst_len)
{
    if (n > dst_len) {
        portage_stack_chk_fail();
    }
    return std::memset(dst, c, n);
}

__attribute__((sysv_abi)) char *portage_strcpy_chk(char *dst, const char *src, size_t dst_len)
{
    size_t len = std::strlen(src) + 1;
    if (len > dst_len) {
        portage_stack_chk_fail();
    }
    return std::strcpy(dst, src);
}

__attribute__((sysv_abi)) char *portage_strncpy_chk(char *dst, const char *src, size_t n,
                                                    size_t dst_len)
{
    if (n > dst_len) {
        portage_stack_chk_fail();
    }
    return std::strncpy(dst, src, n);
}

__attribute__((sysv_abi)) size_t portage_strlen_chk(const char *s, size_t s_len)
{
    size_t len = std::strlen(s);
    if (len >= s_len) {
        portage_stack_chk_fail();
    }
    return len;
}

__attribute__((sysv_abi)) int portage_snprintf(char *dst, size_t cap, const char *fmt, uintptr_t a0,
                                               uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                               uintptr_t a4, uintptr_t a5)
{
    double fp[4]{};
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movsd %%xmm0, %0\n\t"
                     "movsd %%xmm1, %1\n\t"
                     "movsd %%xmm2, %2\n\t"
                     "movsd %%xmm3, %3"
                     : "=m"(fp[0]), "=m"(fp[1]), "=m"(fp[2]), "=m"(fp[3]));
#endif
    uintptr_t ints[6] = {a0, a1, a2, a3, a4, a5};
    portage::ndk::RegArrayArgSource src(ints, 6, fp, 4);
    return portage::ndk::FormatSysvPrintf(dst, cap, fmt, src);
}

__attribute__((sysv_abi)) int portage_vsnprintf(char *dst, size_t cap, const char *fmt,
                                                portage::ndk::SysvVaList *ap)
{
    portage::ndk::VaListArgSource src(ap);
    return portage::ndk::FormatSysvPrintf(dst, cap, fmt, src);
}

__attribute__((sysv_abi)) int portage_snprintf_chk(char *dst, size_t cap, int flag, size_t osz,
                                                   const char *fmt, uintptr_t a0, uintptr_t a1,
                                                   uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
    (void)flag;
    if (osz != static_cast<size_t>(-1) && cap > osz) {
        portage_stack_chk_fail();
    }
    double fp[4]{};
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movsd %%xmm0, %0\n\t"
                     "movsd %%xmm1, %1\n\t"
                     "movsd %%xmm2, %2\n\t"
                     "movsd %%xmm3, %3"
                     : "=m"(fp[0]), "=m"(fp[1]), "=m"(fp[2]), "=m"(fp[3]));
#endif
    uintptr_t ints[5] = {a0, a1, a2, a3, a4};
    portage::ndk::RegArrayArgSource src(ints, 5, fp, 4);
    return portage::ndk::FormatSysvPrintf(dst, cap, fmt, src);
}

__attribute__((sysv_abi)) int portage_vsnprintf_chk(char *dst, size_t cap, int flag, size_t osz,
                                                    const char *fmt, portage::ndk::SysvVaList *ap)
{
    (void)flag;
    if (osz != static_cast<size_t>(-1) && cap > osz) {
        portage_stack_chk_fail();
    }
    return portage_vsnprintf(dst, cap, fmt, ap);
}

// --- pthread sync & threads ---
__attribute__((sysv_abi)) int portage_pthread_once(int32_t *once_control,
                                                   __attribute__((sysv_abi)) void (*init_fn)(void))
{
    if (!once_control || !init_fn) {
        return 22;
    }
    std::unique_lock<std::mutex> lk(portage::ndk::g_once_mutex);
    while (*once_control == 1) {
        portage::ndk::g_once_cv.wait(lk);
    }
    if (*once_control == 2) {
        return 0;
    }
    *once_control = 1;
    lk.unlock();
    init_fn();
    lk.lock();
    *once_control = 2;
    portage::ndk::g_once_cv.notify_all();
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutexattr_init(void *attr)
{
    if (!attr) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
    portage::ndk::g_mutex_attrs[reinterpret_cast<uintptr_t>(attr)] = 0;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutexattr_destroy(void *attr)
{
    if (!attr) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
    portage::ndk::g_mutex_attrs.erase(reinterpret_cast<uintptr_t>(attr));
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutexattr_settype(void *attr, int type)
{
    if (!attr) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
    portage::ndk::g_mutex_attrs[reinterpret_cast<uintptr_t>(attr)] = type;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutex_init(void *mutex, const void *attr)
{
    if (!mutex) {
        return 22;
    }
    int type = 0;
    if (attr) {
        std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
        auto it = portage::ndk::g_mutex_attrs.find(reinterpret_cast<uintptr_t>(attr));
        if (it != portage::ndk::g_mutex_attrs.end()) {
            type = it->second;
        }
    }
    auto state = portage::ndk::GetOrCreateHostMutex(mutex);
    state->recursive = (type == 1 /* PTHREAD_MUTEX_RECURSIVE on Bionic */);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutex_destroy(void *mutex)
{
    if (!mutex) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
    portage::ndk::g_host_mutexes.erase(reinterpret_cast<uintptr_t>(mutex));
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutex_lock(void *mutex)
{
    if (!mutex) {
        return 22;
    }
    portage::ndk::GetOrCreateHostMutex(mutex)->lock();
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_mutex_trylock(void *mutex)
{
    if (!mutex) {
        return 22;
    }
    return portage::ndk::GetOrCreateHostMutex(mutex)->try_lock() ? 0 : 16 /* EBUSY */;
}

__attribute__((sysv_abi)) int portage_pthread_mutex_unlock(void *mutex)
{
    if (!mutex) {
        return 22;
    }
    portage::ndk::GetOrCreateHostMutex(mutex)->unlock();
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_cond_init(void *cond, const void *)
{
    if (!cond) {
        return 22;
    }
    (void)portage::ndk::GetOrCreateHostCond(cond);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_cond_destroy(void *cond)
{
    if (!cond) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthread_sync_mutex);
    portage::ndk::g_host_conds.erase(reinterpret_cast<uintptr_t>(cond));
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_cond_wait(void *cond, void *mutex)
{
    if (!cond || !mutex) {
        return 22;
    }
    auto cstate = portage::ndk::GetOrCreateHostCond(cond);
    auto mstate = portage::ndk::GetOrCreateHostMutex(mutex);
    std::unique_lock<portage::ndk::HostMutexState> lk(*mstate, std::adopt_lock);
    cstate->cv.wait(lk);
    lk.release();
    return 0;
}

__attribute__((sysv_abi)) int
portage_pthread_cond_timedwait(void *cond, void *mutex, const portage::ndk::GuestTimespec *abstime)
{
    if (!cond || !mutex || !abstime) {
        return 22;
    }
    auto cstate = portage::ndk::GetOrCreateHostCond(cond);
    auto mstate = portage::ndk::GetOrCreateHostMutex(mutex);
    auto dur = std::chrono::seconds(abstime->tv_sec) + std::chrono::nanoseconds(abstime->tv_nsec);
    auto tp = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(dur));
    std::unique_lock<portage::ndk::HostMutexState> lk(*mstate, std::adopt_lock);
    auto status = cstate->cv.wait_until(lk, tp);
    lk.release();
    return (status == std::cv_status::timeout) ? 110 /* ETIMEDOUT */ : 0;
}

__attribute__((sysv_abi)) int portage_pthread_cond_signal(void *cond)
{
    if (!cond) {
        return 22;
    }
    portage::ndk::GetOrCreateHostCond(cond)->cv.notify_one();
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_cond_broadcast(void *cond)
{
    if (!cond) {
        return 22;
    }
    portage::ndk::GetOrCreateHostCond(cond)->cv.notify_all();
    return 0;
}

__attribute__((sysv_abi)) int
portage_pthread_key_create(uint32_t *key, __attribute__((sysv_abi)) void (*dtor)(void *))
{
    if (!key) {
        return 22;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_keys_mutex);
    uint32_t k = portage::ndk::g_next_key++;
    portage::ndk::g_keys[k] = portage::ndk::KeyEntry{dtor, true};
    *key = k;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_key_delete(uint32_t key)
{
    std::lock_guard<std::mutex> lk(portage::ndk::g_keys_mutex);
    auto it = portage::ndk::g_keys.find(key);
    if (it == portage::ndk::g_keys.end()) {
        return 22;
    }
    portage::ndk::g_keys.erase(it);
    return 0;
}

__attribute__((sysv_abi)) void *portage_pthread_getspecific(uint32_t key)
{
    auto it = portage::ndk::g_tls_values.find(key);
    return (it != portage::ndk::g_tls_values.end()) ? it->second : nullptr;
}

__attribute__((sysv_abi)) int portage_pthread_setspecific(uint32_t key, const void *value)
{
    portage::ndk::g_tls_values[key] = const_cast<void *>(value);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_create(uintptr_t *thread_out, const void *,
                                                     void *start_routine, void *arg)
{
    if (!thread_out || !start_routine) {
        return 22;
    }

    using StartFn = __attribute__((sysv_abi)) void *(*)(void *);
    auto fn = reinterpret_cast<StartFn>(start_routine);

    auto rec = std::make_shared<portage::ndk::HostPthreadRecord>();
    auto *tcb = new portage::thread::BionicTcb();
    tcb->self = tcb;
    tcb->stack_guard = 0x595e9fbd94fda766ULL;
    rec->tcb = tcb;

    uintptr_t handle = reinterpret_cast<uintptr_t>(tcb);

    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_pthreads_mutex);
        portage::ndk::g_pthreads[handle] = rec;
    }

    rec->thread = std::thread([rec, fn, arg, handle]() mutable {
        portage::arch::EnsureVectoredHandlerInstalled();
        portage::arch::SetActiveTcb(handle);
        rec->retval = fn(arg);
        portage::ndk::RunCurrentThreadTlsDestructors();
        portage::thread::ThreadManager::Instance().RunCurrentThreadAtExit();
    });

    *thread_out = handle;
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_join(uintptr_t thread, void **retval)
{
    std::shared_ptr<portage::ndk::HostPthreadRecord> rec;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_pthreads_mutex);
        auto it = portage::ndk::g_pthreads.find(thread);
        if (it == portage::ndk::g_pthreads.end()) {
            return 3 /* ESRCH */;
        }
        rec = it->second;
    }
    if (rec->thread.joinable()) {
        rec->thread.join();
    }
    if (retval) {
        *retval = rec->retval;
    }
    std::lock_guard<std::mutex> lk(portage::ndk::g_pthreads_mutex);
    portage::ndk::g_pthreads.erase(thread);
    return 0;
}

__attribute__((sysv_abi)) int portage_pthread_detach(uintptr_t thread)
{
    std::shared_ptr<portage::ndk::HostPthreadRecord> rec;
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_pthreads_mutex);
        auto it = portage::ndk::g_pthreads.find(thread);
        if (it == portage::ndk::g_pthreads.end()) {
            return 3 /* ESRCH */;
        }
        rec = it->second;
        rec->detached = true;
        if (rec->thread.joinable()) {
            rec->thread.detach();
        }
        portage::ndk::g_pthreads.erase(it);
    }
    return 0;
}

// --- libc++_shared ABI ---
__attribute__((sysv_abi)) void *portage_cxx_new(size_t size)
{
    return std::malloc(size > 0 ? size : 1);
}

__attribute__((sysv_abi)) void *portage_cxx_new_array(size_t size)
{
    return std::malloc(size > 0 ? size : 1);
}

__attribute__((sysv_abi)) void portage_cxx_delete(void *ptr) { portage_free(ptr); }

__attribute__((sysv_abi)) void portage_cxx_delete_sized(void *ptr, size_t) { portage_free(ptr); }

__attribute__((sysv_abi)) int portage_cxa_guard_acquire(uint64_t *guard)
{
    if (!guard) {
        return 0;
    }
    auto *byte_ptr = reinterpret_cast<uint8_t *>(guard);
    std::unique_lock<std::mutex> lk(portage::ndk::g_cxa_guard_mutex);
    while (true) {
        if (byte_ptr[0] != 0) {
            return 0;
        }
        if ((*guard & 0x100ULL) == 0) {
            *guard |= 0x100ULL;
            return 1;
        }
        portage::ndk::g_cxa_guard_cv.wait(lk);
    }
}

__attribute__((sysv_abi)) void portage_cxa_guard_release(uint64_t *guard)
{
    if (!guard) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_cxa_guard_mutex);
        *guard = 1ULL;
    }
    portage::ndk::g_cxa_guard_cv.notify_all();
}

__attribute__((sysv_abi)) void portage_cxa_guard_abort(uint64_t *guard)
{
    if (!guard) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(portage::ndk::g_cxa_guard_mutex);
        *guard = 0ULL;
    }
    portage::ndk::g_cxa_guard_cv.notify_all();
}

__attribute__((sysv_abi)) void portage_cxa_pure_virtual(void)
{
    portage::ndk::LogcatBuffer::Instance().Append(ANDROID_LOG_FATAL, "portage-cxx",
                                                  "Pure virtual function called");
    std::abort();
}

__attribute__((sysv_abi)) void *portage_cxa_allocate_exception(size_t thrown_size)
{
    return std::malloc(thrown_size ? thrown_size : 1);
}

__attribute__((sysv_abi)) void portage_cxa_free_exception(void *thrown_exception)
{
    std::free(thrown_exception);
}

__attribute__((sysv_abi)) void portage_cxa_throw(void *, void *, void (*)(void *))
{
    portage::ndk::LogcatBuffer::Instance().Append(
        ANDROID_LOG_FATAL, "portage-cxx",
        "Unhandled C++ exception thrown in virtual fallback runtime");
    std::abort();
}

__attribute__((sysv_abi)) void *portage_cxa_begin_catch(void *exception_object)
{
    return exception_object;
}

__attribute__((sysv_abi)) void portage_cxa_end_catch(void) {}

__attribute__((sysv_abi)) void portage_unwind_resume(void *)
{
    portage::ndk::LogcatBuffer::Instance().Append(
        ANDROID_LOG_FATAL, "portage-cxx", "_Unwind_Resume invoked in virtual fallback runtime");
    std::abort();
}

__attribute__((sysv_abi)) int portage_gxx_personality_v0(int, int, uint64_t, void *, void *)
{
    return 9; // _URC_FATAL_PHASE1_ERROR
}

} // extern "C"
