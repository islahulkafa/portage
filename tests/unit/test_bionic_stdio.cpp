#include "ndk/bionic_stdio.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace portage::ndk;

TEST(BionicStdioTest, StandardStreamsAndDescriptors)
{
    BionicFile *s_in = portage_stdin();
    BionicFile *s_out = portage_stdout();
    BionicFile *s_err = portage_stderr();

    ASSERT_NE(s_in, nullptr);
    ASSERT_NE(s_out, nullptr);
    ASSERT_NE(s_err, nullptr);

    EXPECT_EQ(portage_fileno(s_in), 0);
    EXPECT_EQ(portage_fileno(s_out), 1);
    EXPECT_EQ(portage_fileno(s_err), 2);

    // Verify __sF array indexing: stdin=&__sF[0], stdout=&__sF[1], stderr=&__sF[2]
    uintptr_t sF_addr = NdkSymbolRegistry::Instance().Lookup("__sF");
    ASSERT_NE(sF_addr, 0ULL);
    auto *sF = reinterpret_cast<BionicFile *>(sF_addr);
    EXPECT_EQ(&sF[0], s_in);
    EXPECT_EQ(&sF[1], s_out);
    EXPECT_EQ(&sF[2], s_err);
    EXPECT_EQ(sizeof(BionicFile), 152U);

    // Symbols in registry
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("stdin"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("stdout"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("stderr"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("fopen"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("fclose"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("fread"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("fwrite"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("fprintf"), 0ULL);
    EXPECT_NE(NdkSymbolRegistry::Instance().Lookup("sscanf"), 0ULL);
}

TEST(BionicStdioTest, FileWriteReadSeekRoundtrip)
{
    const char *test_path = "portage_stdio_test.tmp";
    BionicFile *fp = portage_fopen(test_path, "wb+");
    ASSERT_NE(fp, nullptr);

    const char test_data[] = "Hello Portage Stdio 2026\nSecond line data\n";
    size_t written = portage_fwrite(test_data, 1, sizeof(test_data) - 1, fp);
    EXPECT_EQ(written, sizeof(test_data) - 1);

    int64_t pos = portage_ftello(fp);
    EXPECT_EQ(pos, static_cast<int64_t>(sizeof(test_data) - 1));

    // Seek back to start
    EXPECT_EQ(portage_fseeko(fp, 0, SEEK_SET), 0);
    EXPECT_EQ(portage_ftello(fp), 0);

    // Read back line by line using fgets
    char line_buf[64]{};
    char *res = portage_fgets(line_buf, sizeof(line_buf), fp);
    ASSERT_NE(res, nullptr);
    EXPECT_STREQ(line_buf, "Hello Portage Stdio 2026\n");

    // Test ungetc and getc
    int c = portage_fgetc(fp);
    EXPECT_EQ(c, 'S');
    EXPECT_EQ(portage_ungetc('X', fp), 'X');
    EXPECT_EQ(portage_fgetc(fp), 'X');

    EXPECT_EQ(portage_fclose(fp), 0);

    // Reopen read-only
    fp = portage_fopen(test_path, "rb");
    ASSERT_NE(fp, nullptr);
    char full_buf[128]{};
    size_t read_bytes = portage_fread(full_buf, 1, sizeof(full_buf), fp);
    EXPECT_EQ(read_bytes, sizeof(test_data) - 1);
    EXPECT_EQ(std::memcmp(full_buf, test_data, sizeof(test_data) - 1), 0);
    EXPECT_NE(portage_feof(fp), 0);
    EXPECT_EQ(portage_fclose(fp), 0);

    std::remove(test_path);
}

TEST(BionicStdioTest, SscanfNumericAndStringExtraction)
{
    const char *src = "42 0x2a 3.14159 portage_rocks 9999999999";
    int d = 0;
    int hex = 0;
    float f = 0.0f;
    char word[32]{};
    int64_t lld = 0;

    int matched =
        portage_sscanf(src, "%d %x %f %s %lld", reinterpret_cast<uintptr_t>(&d),
                       reinterpret_cast<uintptr_t>(&hex), reinterpret_cast<uintptr_t>(&f),
                       reinterpret_cast<uintptr_t>(word), reinterpret_cast<uintptr_t>(&lld));
    EXPECT_EQ(matched, 5);
    EXPECT_EQ(d, 42);
    EXPECT_EQ(hex, 0x2a);
    EXPECT_NEAR(f, 3.14159f, 0.001f);
    EXPECT_STREQ(word, "portage_rocks");
    EXPECT_EQ(lld, 9999999999LL);
}

TEST(BionicStdioTest, FprintfAndVasprintf)
{
    const char *test_path = "portage_fprintf_test.tmp";
    BionicFile *fp = portage_fopen(test_path, "w");
    ASSERT_NE(fp, nullptr);

    int count =
        portage_fprintf(fp, "Value: %d, Str: %s\n", 1234, reinterpret_cast<uintptr_t>("test_val"));
    EXPECT_GT(count, 0);
    EXPECT_EQ(portage_fclose(fp), 0);

    char *allocated = nullptr;
    using SysvAsprintfFn = __attribute__((sysv_abi)) int (*)(char **, const char *, ...);
    auto asprintf_fn =
        reinterpret_cast<SysvAsprintfFn>(reinterpret_cast<void *>(&portage_asprintf));
    int as_len = asprintf_fn(&allocated, "Formatted %s with code %d", "string", 42);
    EXPECT_GT(as_len, 0);
    ASSERT_NE(allocated, nullptr);
    EXPECT_STREQ(allocated, "Formatted string with code 42");
    std::free(allocated);

    std::remove(test_path);
}
