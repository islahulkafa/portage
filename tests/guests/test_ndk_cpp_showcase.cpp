#include <android/log.h>
#include <android/native_activity.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

extern "C" __attribute__((visibility("default"))) void
ANativeActivity_onCreate(ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    (void)activity;
    (void)savedState;
    (void)savedStateSize;

    std::vector<int> numbers = {42, 17, 99, 1, 5};
    std::sort(numbers.begin(), numbers.end());
    if (numbers.front() != 1 || numbers.back() != 99 || numbers.size() != 5) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Vector sort verification failed");
        return;
    }

    std::unordered_map<std::string, int> scores;
    scores["alpha"] = 10;
    scores["beta"] = 20;
    scores["gamma"] = 30;
    if (scores.at("beta") != 20 || scores.size() != 3) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Unordered map verification failed");
        return;
    }

    int thread_result = 0;
    std::thread worker([&thread_result]() {
        int accumulator = 0;
        for (int i = 1; i <= 10; ++i) {
            accumulator += i;
        }
        thread_result = accumulator;
    });
    worker.join();
    if (thread_result != 55) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Thread join calculation failed");
        return;
    }

    const char *test_path = "/data/local/tmp/ndk_cpp_test.txt";
    FILE *fp = std::fopen(test_path, "w+");
    if (!fp) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase", "File open for write failed");
        return;
    }
    const char *test_payload = "portage_ndk_cpp_runtime_ok";
    std::fputs(test_payload, fp);
    std::fclose(fp);

    std::error_code ec;
    if (!fs::exists(test_path, ec)) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Filesystem exists check failed");
        return;
    }
    auto file_sz = fs::file_size(test_path, ec);
    if (file_sz != 26) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Filesystem file size check failed");
        return;
    }
    fs::remove(test_path, ec);

    bool exception_caught = false;
    try {
        throw std::runtime_error("Portage C++ Exception Unwinding Works");
    } catch (const std::runtime_error &e) {
        if (std::string_view(e.what()).find("Portage") != std::string_view::npos) {
            exception_caught = true;
        }
    } catch (...) {
        exception_caught = false;
    }

    if (!exception_caught) {
        __android_log_print(ANDROID_LOG_ERROR, "ndk-cpp-showcase",
                            "Exception unwinding and catch failed");
        return;
    }

    __android_log_print(ANDROID_LOG_INFO, "ndk-cpp-showcase",
                        "[ndk-cpp-showcase] ALL CHECKS PASSED\n");
}
