#include "wsi/window_manager.hpp"
#include "input/input_manager.hpp"

#include <chrono>
#include <string>

namespace portage::wsi {

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_KEYDOWN:
        input::InputManager::Instance().PostWin32Key(static_cast<uint32_t>(wParam), true);
        return 0;
    case WM_KEYUP:
        input::InputManager::Instance().PostWin32Key(static_cast<uint32_t>(wParam), false);
        return 0;
    case WM_LBUTTONDOWN: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int32_t w = rc.right - rc.left;
        int32_t h = rc.bottom - rc.top;
        int32_t x = static_cast<short>(LOWORD(lParam));
        int32_t y = static_cast<short>(HIWORD(lParam));
        input::InputManager::Instance().PostPointerEvent(x, y, w, h, true);
        return 0;
    }
    case WM_LBUTTONUP: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int32_t w = rc.right - rc.left;
        int32_t h = rc.bottom - rc.top;
        int32_t x = static_cast<short>(LOWORD(lParam));
        int32_t y = static_cast<short>(HIWORD(lParam));
        input::InputManager::Instance().PostPointerEvent(x, y, w, h, false);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (wParam & MK_LBUTTON) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int32_t w = rc.right - rc.left;
            int32_t h = rc.bottom - rc.top;
            int32_t x = static_cast<short>(LOWORD(lParam));
            int32_t y = static_cast<short>(HIWORD(lParam));
            input::InputManager::Instance().PostPointerEvent(x, y, w, h, true);
        }
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

} // namespace

WindowManager &WindowManager::Instance() noexcept
{
    static WindowManager instance;
    return instance;
}

WindowManager::WindowManager() { m_hinstance = GetModuleHandleW(nullptr); }

WindowManager::~WindowManager() { DestroyWindow(); }

void WindowManager::SetHeadless(bool headless) noexcept { m_headless.store(headless); }

bool WindowManager::IsHeadless() const noexcept { return m_headless.load(); }

HWND WindowManager::GetWindowHandle() const noexcept { return m_hwnd; }

HINSTANCE WindowManager::GetInstanceHandle() const noexcept { return m_hinstance; }

uint32_t WindowManager::GetWidth() const noexcept { return m_width; }

uint32_t WindowManager::GetHeight() const noexcept { return m_height; }

void WindowManager::RecordFramePresented() noexcept { m_frames_presented.fetch_add(1); }

WindowStats WindowManager::GetStats() const noexcept
{
    return WindowStats{.width = m_width,
                       .height = m_height,
                       .is_open = (m_hwnd != NULL || m_headless.load()),
                       .is_headless = m_headless.load(),
                       .frames_presented = m_frames_presented.load()};
}

void WindowManager::Reset() noexcept
{
    DestroyWindow();
    m_headless.store(false);
    m_frames_presented.store(0);
    m_width = 1280;
    m_height = 720;
}

bool WindowManager::EnsureWindow(uint32_t width, uint32_t height, std::string_view title)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    m_width = width;
    m_height = height;

    if (m_headless.load()) {
        return true;
    }

    if (m_hwnd != NULL) {
        return true;
    }

    std::wstring wtitle(title.begin(), title.end());
    m_running.store(true);
    m_window_ready.store(false);

    m_window_thread = std::thread(&WindowManager::WindowThreadFunc, this, width, height, wtitle);

    m_cv.wait(lock, [this] { return m_window_ready.load(); });

    return m_hwnd != NULL;
}

void WindowManager::WindowThreadFunc(uint32_t width, uint32_t height, std::wstring title)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hinstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PortageWindowClass";

    RegisterClassExW(&wc);

    RECT r{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, L"PortageWindowClass", title.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, m_hinstance, nullptr);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hwnd = hwnd;
        m_window_ready.store(true);
    }
    m_cv.notify_all();

    if (!hwnd) {
        m_running.store(false);
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (m_running.load()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running.store(false);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ::DestroyWindow(hwnd);
    UnregisterClassW(L"PortageWindowClass", m_hinstance);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hwnd = nullptr;
    }
}

void WindowManager::DestroyWindow() noexcept
{
    if (!m_running.load() && m_hwnd == nullptr) {
        return;
    }

    m_running.store(false);

    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    if (m_window_thread.joinable()) {
        m_window_thread.join();
    }

    m_hwnd = nullptr;
}

} // namespace portage::wsi
