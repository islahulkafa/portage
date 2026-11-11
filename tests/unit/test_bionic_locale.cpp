#include "ndk/bionic_locale.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace portage::ndk;

TEST(BionicLocaleTest, Utf8ToUtf32ConversionAnd4ByteWchar)
{
    EXPECT_EQ(sizeof(BionicWchar), 4U);

    // Convert UTF-8 multi-byte to UTF-32 (e.g. Greek Omega: UTF-8 "\xce\xa9" -> U+03A9)
    const char utf8_greek[] = "\xce\xa9";
    BionicWchar wc = 0;
    size_t consumed = portage_mbrtowc(&wc, utf8_greek, 2, nullptr);
    EXPECT_EQ(consumed, 2U);
    EXPECT_EQ(wc, 0x03A9U);

    // Convert back to UTF-8 via wcrtomb
    char out_utf8[8]{};
    size_t written = portage_wcrtomb(out_utf8, wc, nullptr);
    EXPECT_EQ(written, 2U);
    EXPECT_EQ(std::memcmp(out_utf8, utf8_greek, 2), 0);

    // Test 4-byte emoji: UTF-8 "\xf0\x9f\x98\x80" -> U+1F600
    const char utf8_emoji[] = "\xf0\x9f\x98\x80";
    consumed = portage_mbrtowc(&wc, utf8_emoji, 4, nullptr);
    EXPECT_EQ(consumed, 4U);
    EXPECT_EQ(wc, 0x1F600U);

    // Test wcslen and wmemchr with 4-byte units
    BionicWchar wstr[4] = {0x03A9U, 0x1F600U, 'A', 0};
    EXPECT_EQ(portage_wcslen(wstr), 3U);
    const BionicWchar *found = portage_wmemchr(wstr, 0x1F600U, 3);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 0x1F600U);
}

static __attribute__((sysv_abi)) int CompareIntsSysv(const void *a, const void *b)
{
    int val_a = *static_cast<const int *>(a);
    int val_b = *static_cast<const int *>(b);
    return (val_a > val_b) - (val_a < val_b);
}

TEST(BionicLocaleTest, QsortAndBsearchWithSysvCallback)
{
    int array[6] = {42, 7, 100, -5, 18, 0};
    portage_qsort(array, 6, sizeof(int), CompareIntsSysv);

    EXPECT_EQ(array[0], -5);
    EXPECT_EQ(array[1], 0);
    EXPECT_EQ(array[2], 7);
    EXPECT_EQ(array[3], 18);
    EXPECT_EQ(array[4], 42);
    EXPECT_EQ(array[5], 100);

    int key = 18;
    void *found = portage_bsearch(&key, array, 6, sizeof(int), CompareIntsSysv);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*static_cast<int *>(found), 18);

    int missing = 99;
    EXPECT_EQ(portage_bsearch(&missing, array, 6, sizeof(int), CompareIntsSysv), nullptr);
}

TEST(BionicLocaleTest, NumberParsingAndLocaleVariants)
{
    const char *int_str = "  -9876543210123 tail";
    char *endp = nullptr;
    int64_t val = portage_strtoll_l(int_str, &endp, 10, nullptr);
    EXPECT_EQ(val, -9876543210123LL);
    EXPECT_STREQ(endp, " tail");

    const char *flt_str = "123.456789";
    double dval = portage_strtod_l(flt_str, nullptr, nullptr);
    EXPECT_NEAR(dval, 123.456789, 0.00001);

    // Localeconv decimal point
    BionicLconv *lc = portage_localeconv();
    ASSERT_NE(lc, nullptr);
    EXPECT_STREQ(lc->decimal_point, ".");

    EXPECT_EQ(portage_ctype_get_mb_cur_max(), 4U);
}

TEST(BionicLocaleTest, AbortMessageAndDiagnostics)
{
    portage_android_set_abort_message("Test fatal abort message");
    const char *msg = portage_get_abort_message();
    ASSERT_NE(msg, nullptr);
    EXPECT_STREQ(msg, "Test fatal abort message");
}
