#include "ndk/android_log.hpp"
#include <gtest/gtest.h>
#include <sstream>

TEST(AndroidLogTest, FormatsAndBuffersLogcatEntries)
{
    auto &logcat = portage::ndk::LogcatBuffer::Instance();
    logcat.Clear();
    logcat.SetConsoleOutputEnabled(false);

    __android_log_write(ANDROID_LOG_INFO, "TestTag", "Hello Portage");
    __android_log_print(ANDROID_LOG_DEBUG, "MathTag", "Result: %d + %d = %d", 10, 20, 30);
    __android_log_print(ANDROID_LOG_WARN, "WarningTag", "Resource %s missing",
                        reinterpret_cast<uint64_t>("texture.png"));
    __android_log_assert("x != NULL", "AssertTag", "Pointer was null");

    auto entries = logcat.GetEntries();
    ASSERT_EQ(entries.size(), 4U);

    EXPECT_EQ(entries[0].priority, ANDROID_LOG_INFO);
    EXPECT_EQ(entries[0].tag, "TestTag");
    EXPECT_EQ(entries[0].message, "Hello Portage");

    EXPECT_EQ(entries[1].priority, ANDROID_LOG_DEBUG);
    EXPECT_EQ(entries[1].tag, "MathTag");
    EXPECT_EQ(entries[1].message, "Result: 10 + 20 = 30");

    EXPECT_EQ(entries[2].priority, ANDROID_LOG_WARN);
    EXPECT_EQ(entries[2].tag, "WarningTag");
    EXPECT_EQ(entries[2].message, "Resource texture.png missing");

    EXPECT_EQ(entries[3].priority, ANDROID_LOG_FATAL);
    EXPECT_EQ(entries[3].tag, "AssertTag");
    EXPECT_EQ(entries[3].message, "Pointer was null");

    std::ostringstream oss;
    logcat.DumpToStream(oss);
    std::string dump = oss.str();
    EXPECT_NE(dump.find("I TestTag: Hello Portage"), std::string::npos);
    EXPECT_NE(dump.find("D MathTag: Result: 10 + 20 = 30"), std::string::npos);

    logcat.Clear();
    EXPECT_EQ(logcat.GetEntries().size(), 0U);
    logcat.SetConsoleOutputEnabled(true);
}
