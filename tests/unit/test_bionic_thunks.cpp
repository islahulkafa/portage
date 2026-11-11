#include "ndk/bionic_thunks.hpp"
#include "ndk/libandroid_thunks.hpp"

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace portage::ndk;

namespace {

__attribute__((sysv_abi)) void *TestWorkerRoutine(void *arg)
{
    auto *ctx = static_cast<std::pair<void *, void *> *>(arg);
    auto *mtx = static_cast<uint8_t *>(ctx->first);
    auto *counter = static_cast<int *>(ctx->second);
    for (int i = 0; i < 100; ++i) {
        portage_pthread_mutex_lock(mtx);
        (*counter)++;
        portage_pthread_mutex_unlock(mtx);
    }
    return reinterpret_cast<void *>(0x1234ULL);
}

static int g_once_counter = 0;
__attribute__((sysv_abi)) void OnceInitRoutine(void) { g_once_counter++; }

} // namespace

TEST(BionicThunksTest, ZlibLp64StreamDeflateAndInflateRoundtrip)
{
    const std::string payload =
        "Portage LP64 z_stream bridge test payload repeated to guarantee compression! "
        "Portage LP64 z_stream bridge test payload repeated to guarantee compression!";
    std::vector<uint8_t> compressed(payload.size() + 128, 0);

    GuestZStream def_s{};
    ASSERT_EQ(portage_deflateInit_(&def_s, -1, "1.3.1", static_cast<int>(sizeof(GuestZStream))), 0);
    def_s.next_in = reinterpret_cast<uint64_t>(payload.data());
    def_s.avail_in = static_cast<uint32_t>(payload.size());
    def_s.next_out = reinterpret_cast<uint64_t>(compressed.data());
    def_s.avail_out = static_cast<uint32_t>(compressed.size());
    EXPECT_EQ(portage_deflate(&def_s, 4 /* Z_FINISH */), 1 /* Z_STREAM_END */);
    EXPECT_EQ(def_s.total_in, payload.size());
    EXPECT_GT(def_s.total_out, 0ULL);
    EXPECT_LT(def_s.total_out, payload.size());
    size_t comp_len = static_cast<size_t>(def_s.total_out);
    EXPECT_EQ(portage_deflateEnd(&def_s), 0);

    std::vector<uint8_t> restored(payload.size() + 16, 0);
    GuestZStream inf_s{};
    ASSERT_EQ(portage_inflateInit_(&inf_s, "1.3.1", static_cast<int>(sizeof(GuestZStream))), 0);
    inf_s.next_in = reinterpret_cast<uint64_t>(compressed.data());
    inf_s.avail_in = static_cast<uint32_t>(comp_len);
    inf_s.next_out = reinterpret_cast<uint64_t>(restored.data());
    inf_s.avail_out = static_cast<uint32_t>(restored.size());
    EXPECT_EQ(portage_inflate(&inf_s, 4), 1 /* Z_STREAM_END */);
    EXPECT_EQ(inf_s.total_out, payload.size());
    EXPECT_EQ(portage_inflateEnd(&inf_s), 0);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(restored.data()), payload.size()),
              payload);

    // compress2 / uncompress / crc32 with LP64 64-bit uLongf lengths
    uint64_t c2_len = compressed.size();
    EXPECT_EQ(portage_compress2(compressed.data(), &c2_len,
                                reinterpret_cast<const uint8_t *>(payload.data()), payload.size(),
                                6),
              0);
    uint64_t u_len = restored.size();
    std::fill(restored.begin(), restored.end(), 0);
    EXPECT_EQ(portage_uncompress(restored.data(), &u_len, compressed.data(), c2_len), 0);
    EXPECT_EQ(u_len, payload.size());
    EXPECT_NE(portage_crc32(0, reinterpret_cast<const uint8_t *>(payload.data()),
                            static_cast<uint32_t>(payload.size())),
              0ULL);
}

TEST(BionicThunksTest, MathFortifyAndFormatSysvCalls)
{
    EXPECT_NEAR(portage_sinf(1.5707963f), 1.0f, 1e-4f);
    EXPECT_NEAR(portage_cosf(0.0f), 1.0f, 1e-5f);
    EXPECT_NEAR(portage_sqrtf(9.0f), 3.0f, 1e-5f);
    EXPECT_NEAR(portage_pow(2.0, 10.0), 1024.0, 1e-6);
    EXPECT_LT(portage_arc4random_uniform(17), 17U);

    void *aligned_ptr = nullptr;
    ASSERT_EQ(portage_posix_memalign(&aligned_ptr, 64, 256), 0);
    ASSERT_NE(aligned_ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned_ptr) % 64, 0U);
    portage_free(aligned_ptr);

    char buf[64]{};
    const char src[] = "hello_portage";
    portage_memcpy_chk(buf, src, sizeof(src), sizeof(buf));
    EXPECT_STREQ(buf, "hello_portage");

    char fmt_buf[128]{};
    using SysvSnprintfFn = __attribute__((sysv_abi)) int (*)(char *, size_t, const char *, ...);
    auto snprintf_fn =
        reinterpret_cast<SysvSnprintfFn>(reinterpret_cast<void *>(&portage_snprintf));
    int written = snprintf_fn(fmt_buf, sizeof(fmt_buf), "id=%d tag=%s hex=0x%x", 42, "ndk", 0xab);
    EXPECT_EQ(written, 22);
    EXPECT_STREQ(fmt_buf, "id=42 tag=ndk hex=0xab");

    *portage_errno() = 22;
    EXPECT_EQ(*portage_errno(), 22);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("__stack_chk_guard"), 0ULL);
    EXPECT_EQ(NdkSymbolRegistry::Instance().Lookup("_Znwm"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().LookupFallback("_Znwm"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("zlibVersion"), 0ULL);
}

TEST(BionicThunksTest, PthreadMutexCondOnceAndThreadsWorkInGuest40ByteStorage)
{
    // Bionic LP64 sizes: pthread_mutex_t = 40 bytes, pthread_cond_t = 48 bytes
    uint8_t mutex_storage[40]{};
    uint8_t cond_storage[48]{};
    int32_t once_ctrl = 0;

    g_once_counter = 0;
    EXPECT_EQ(portage_pthread_once(&once_ctrl, OnceInitRoutine), 0);
    EXPECT_EQ(portage_pthread_once(&once_ctrl, OnceInitRoutine), 0);
    EXPECT_EQ(g_once_counter, 1);

    // Static zero-initialized mutex works without explicit pthread_mutex_init
    int shared_counter = 0;
    std::pair<void *, void *> ctx{mutex_storage, &shared_counter};
    uintptr_t t1 = 0, t2 = 0;
    ASSERT_EQ(
        portage_pthread_create(&t1, nullptr, reinterpret_cast<void *>(&TestWorkerRoutine), &ctx),
        0);
    ASSERT_EQ(
        portage_pthread_create(&t2, nullptr, reinterpret_cast<void *>(&TestWorkerRoutine), &ctx),
        0);

    void *r1 = nullptr;
    void *r2 = nullptr;
    EXPECT_EQ(portage_pthread_join(t1, &r1), 0);
    EXPECT_EQ(portage_pthread_join(t2, &r2), 0);
    EXPECT_EQ(r1, reinterpret_cast<void *>(0x1234ULL));
    EXPECT_EQ(r2, reinterpret_cast<void *>(0x1234ULL));
    EXPECT_EQ(shared_counter, 200);

    EXPECT_EQ(portage_pthread_cond_init(cond_storage, nullptr), 0);
    EXPECT_EQ(portage_pthread_cond_signal(cond_storage), 0);
    EXPECT_EQ(portage_pthread_cond_destroy(cond_storage), 0);
    EXPECT_EQ(portage_pthread_mutex_destroy(mutex_storage), 0);

    // C++ static guard test (__cxa_guard_acquire / release)
    uint64_t guard = 0;
    EXPECT_EQ(portage_cxa_guard_acquire(&guard), 1);
    portage_cxa_guard_release(&guard);
    EXPECT_EQ(portage_cxa_guard_acquire(&guard), 0);
}
