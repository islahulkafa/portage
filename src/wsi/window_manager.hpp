#pragma once

#include "ndk/input_queue.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

namespace portage::wsi {

struct WindowStats {
    uint32_t width{0};
    uint32_t height{0};
    bool is_open{false};
    bool is_headless{false};
    uint64_t frames_presented{0};
};

class WindowManager {
  public:
    static WindowManager &Instance() noexcept;

    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;

    bool EnsureWindow(uint32_t width = 1280, uint32_t height = 720,
                      std::string_view title = "Portage Android Application");
    void DestroyWindow() noexcept;

    [[nodiscard]] HWND GetWindowHandle() const noexcept;
    [[nodiscard]] HINSTANCE GetInstanceHandle() const noexcept;
    [[nodiscard]] uint32_t GetWidth() const noexcept;
    [[nodiscard]] uint32_t GetHeight() const noexcept;

    using MouseEventCallback =
        std::function<void(int32_t x, int32_t y, int32_t w, int32_t h, int32_t action)>;
    using MultiTouchCallback =
        std::function<void(int32_t action, const std::vector<ndk::InputPointer> &pointers)>;
    using KeyEventCallback = std::function<void(uint32_t vk_code, bool pressed)>;
    using ResizeEventCallback = std::function<void(uint32_t width, uint32_t height)>;
    using CloseEventCallback = std::function<void()>;

    void SetMouseCallback(MouseEventCallback cb);
    void SetMultiTouchCallback(MultiTouchCallback cb);
    void SetKeyCallback(KeyEventCallback cb);
    void SetResizeCallback(ResizeEventCallback cb);
    void SetCloseCallback(CloseEventCallback cb);
    void ClearCallbacks() noexcept;

    void DispatchMouse(int32_t x, int32_t y, int32_t w, int32_t h, int32_t action);
    void DispatchMultiTouch(int32_t action, const std::vector<ndk::InputPointer> &pointers);
    void DispatchKey(uint32_t vk_code, bool pressed);
    void DispatchResize(uint32_t width, uint32_t height);
    void DispatchClose();

    void WaitUntilClosed() noexcept;

    void SetHeadless(bool headless) noexcept;
    [[nodiscard]] bool IsHeadless() const noexcept;

    void RecordFramePresented() noexcept;
    [[nodiscard]] WindowStats GetStats() const noexcept;

    void Reset() noexcept;

  private:
    WindowManager();
    ~WindowManager();

    void WindowThreadFunc(uint32_t width, uint32_t height, std::wstring title);

    mutable std::mutex m_mutex{};
    std::condition_variable m_cv{};
    std::thread m_window_thread{};
    HWND m_hwnd{NULL};
    HINSTANCE m_hinstance{NULL};
    uint32_t m_width{1280};
    uint32_t m_height{720};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_window_ready{false};
    std::atomic<bool> m_close_dispatched{false};
    std::atomic<bool> m_headless{false};
    std::atomic<uint64_t> m_frames_presented{0};

    MouseEventCallback m_mouse_callback{};
    MultiTouchCallback m_multitouch_callback{};
    KeyEventCallback m_key_callback{};
    ResizeEventCallback m_resize_callback{};
    CloseEventCallback m_close_callback{};
};

} // namespace portage::wsi
