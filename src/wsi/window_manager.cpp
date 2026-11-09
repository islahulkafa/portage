#include "wsi/window_manager.hpp"
#include "clipboard/clipboard_manager.hpp"
#include "input/input_manager.hpp"
#include "ndk/sensor.hpp"

#include <shellapi.h>
#include <chrono>
#include <string>

namespace portage::wsi {

namespace {

int32_t g_last_rbutton_x{0};
int32_t g_last_rbutton_y{0};
bool g_rbutton_down{false};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_KEYDOWN:
        input::InputManager::Instance().PostWin32Key(static_cast<uint32_t>(wParam), true);
        WindowManager::Instance().DispatchKey(static_cast<uint32_t>(wParam), true);
        return 0;
    case WM_KEYUP:
        input::InputManager::Instance().PostWin32Key(static_cast<uint32_t>(wParam), false);
        WindowManager::Instance().DispatchKey(static_cast<uint32_t>(wParam), false);
        return 0;
    case WM_LBUTTONDOWN: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int32_t w = rc.right - rc.left;
        int32_t h = rc.bottom - rc.top;
        int32_t x = static_cast<short>(LOWORD(lParam));
        int32_t y = static_cast<short>(HIWORD(lParam));
        input::InputManager::Instance().PostPointerEvent(x, y, w, h, true);
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            float norm_x0 = static_cast<float>(x);
            float norm_y0 = static_cast<float>(y);
            float norm_x1 = static_cast<float>(std::max(0, w - x));
            float norm_y1 = static_cast<float>(std::max(0, h - y));
            std::vector<ndk::InputPointer> ptrs = {
                {.id = 0,
                 .x = norm_x0,
                 .y = norm_y0,
                 .raw_x = norm_x0,
                 .raw_y = norm_y0,
                 .pressure = 1.0f,
                 .size = 1.0f},
                {.id = 1,
                 .x = norm_x1,
                 .y = norm_y1,
                 .raw_x = norm_x1,
                 .raw_y = norm_y1,
                 .pressure = 1.0f,
                 .size = 1.0f}};
            WindowManager::Instance().DispatchMultiTouch(AMOTION_EVENT_ACTION_DOWN, ptrs);
        } else {
            WindowManager::Instance().DispatchMouse(x, y, w, h, 0);
        }
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
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            float norm_x0 = static_cast<float>(x);
            float norm_y0 = static_cast<float>(y);
            float norm_x1 = static_cast<float>(std::max(0, w - x));
            float norm_y1 = static_cast<float>(std::max(0, h - y));
            std::vector<ndk::InputPointer> ptrs = {
                {.id = 0,
                 .x = norm_x0,
                 .y = norm_y0,
                 .raw_x = norm_x0,
                 .raw_y = norm_y0,
                 .pressure = 0.0f,
                 .size = 1.0f},
                {.id = 1,
                 .x = norm_x1,
                 .y = norm_y1,
                 .raw_x = norm_x1,
                 .raw_y = norm_y1,
                 .pressure = 0.0f,
                 .size = 1.0f}};
            WindowManager::Instance().DispatchMultiTouch(AMOTION_EVENT_ACTION_UP, ptrs);
        } else {
            WindowManager::Instance().DispatchMouse(x, y, w, h, 1);
        }
        return 0;
    }
    case WM_RBUTTONDOWN: {
        g_last_rbutton_x = static_cast<short>(LOWORD(lParam));
        g_last_rbutton_y = static_cast<short>(HIWORD(lParam));
        g_rbutton_down = true;
        SetCapture(hwnd);
        return 0;
    }
    case WM_RBUTTONUP: {
        g_rbutton_down = false;
        ReleaseCapture();
        return 0;
    }
    case WM_MBUTTONDOWN: {
        ndk::SensorManagerImpl::Instance().ResetPose();
        return 0;
    }
    case WM_MOUSEMOVE: {
        int32_t x = static_cast<short>(LOWORD(lParam));
        int32_t y = static_cast<short>(HIWORD(lParam));
        if (wParam & MK_LBUTTON) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int32_t w = rc.right - rc.left;
            int32_t h = rc.bottom - rc.top;
            input::InputManager::Instance().PostPointerEvent(x, y, w, h, true);
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                float norm_x0 = static_cast<float>(x);
                float norm_y0 = static_cast<float>(y);
                float norm_x1 = static_cast<float>(std::max(0, w - x));
                float norm_y1 = static_cast<float>(std::max(0, h - y));
                std::vector<ndk::InputPointer> ptrs = {
                    {.id = 0,
                     .x = norm_x0,
                     .y = norm_y0,
                     .raw_x = norm_x0,
                     .raw_y = norm_y0,
                     .pressure = 1.0f,
                     .size = 1.0f},
                    {.id = 1,
                     .x = norm_x1,
                     .y = norm_y1,
                     .raw_x = norm_x1,
                     .raw_y = norm_y1,
                     .pressure = 1.0f,
                     .size = 1.0f}};
                WindowManager::Instance().DispatchMultiTouch(AMOTION_EVENT_ACTION_MOVE, ptrs);
            } else {
                WindowManager::Instance().DispatchMouse(x, y, w, h, 2);
            }
        }
        if (g_rbutton_down && (wParam & MK_RBUTTON)) {
            int32_t dx = x - g_last_rbutton_x;
            int32_t dy = y - g_last_rbutton_y;
            g_last_rbutton_x = x;
            g_last_rbutton_y = y;
            float delta_pitch = static_cast<float>(dy) * 0.005f;
            float delta_roll = static_cast<float>(dx) * 0.005f;
            ndk::SensorManagerImpl::Instance().AdjustInteractiveTilt(delta_pitch, delta_roll);
        }
        return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
        UINT32 pointer_id = LOWORD(wParam);
        POINTER_TOUCH_INFO ti{};
        POINT pt{};
        float pressure = 1.0f;
        float touch_w = 0.0f;
        float touch_h = 0.0f;
        float orientation = 0.0f;

        if (GetPointerTouchInfo(pointer_id, &ti)) {
            pt = ti.pointerInfo.ptPixelLocation;
            ScreenToClient(hwnd, &pt);
            if (ti.pressure > 0) {
                pressure = static_cast<float>(ti.pressure) / 1024.0f;
            }
            touch_w = static_cast<float>(ti.rcContact.right - ti.rcContact.left);
            touch_h = static_cast<float>(ti.rcContact.bottom - ti.rcContact.top);
            orientation = static_cast<float>(ti.orientation);
        } else {
            POINTER_INFO pi{};
            if (GetPointerInfo(pointer_id, &pi)) {
                pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
            } else {
                return 0;
            }
        }

        ndk::InputPointer ptr{
            .id = static_cast<int32_t>(pointer_id),
            .x = static_cast<float>(pt.x),
            .y = static_cast<float>(pt.y),
            .raw_x = static_cast<float>(pt.x),
            .raw_y = static_cast<float>(pt.y),
            .pressure = pressure,
            .size = 1.0f,
            .touch_major = std::max(touch_w, touch_h),
            .touch_minor = std::min(touch_w, touch_h),
            .tool_major = 0.0f,
            .tool_minor = 0.0f,
            .orientation = orientation,
            .axes = {},
        };

        int32_t action = AMOTION_EVENT_ACTION_MOVE;
        if (uMsg == WM_POINTERDOWN) {
            action = AMOTION_EVENT_ACTION_DOWN;
        } else if (uMsg == WM_POINTERUP) {
            action = AMOTION_EVENT_ACTION_UP;
        }
        WindowManager::Instance().DispatchMultiTouch(action, {ptr});
        return 0;
    }
    case WM_SIZE: {
        uint32_t w = LOWORD(lParam);
        uint32_t h = HIWORD(lParam);
        WindowManager::Instance().DispatchResize(w, h);
        return 0;
    }
    case WM_CLIPBOARDUPDATE:
        clipboard::ClipboardManager::Instance().OnHostClipboardUpdated();
        return 0;
    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        if (hDrop) {
            POINT pt;
            DragQueryPoint(hDrop, &pt);
            UINT file_count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < file_count; ++i) {
                wchar_t path[MAX_PATH];
                if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
                    char utf8_path[MAX_PATH * 3];
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8_path, sizeof(utf8_path),
                                        nullptr, nullptr);
                    clipboard::ClipboardManager::Instance().DispatchDropEvent(
                        static_cast<float>(pt.x), static_cast<float>(pt.y), utf8_path);
                }
            }
            DragFinish(hDrop);
        }
        return 0;
    }
    case WM_CLOSE:
        WindowManager::Instance().DispatchClose();
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        WindowManager::Instance().DispatchClose();
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

void WindowManager::SetMouseCallback(MouseEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mouse_callback = std::move(cb);
}

void WindowManager::SetMultiTouchCallback(MultiTouchCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_multitouch_callback = std::move(cb);
}

void WindowManager::SetKeyCallback(KeyEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_key_callback = std::move(cb);
}

void WindowManager::SetResizeCallback(ResizeEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_resize_callback = std::move(cb);
}

void WindowManager::SetCloseCallback(CloseEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_close_callback = std::move(cb);
}

void WindowManager::ClearCallbacks() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mouse_callback = nullptr;
    m_multitouch_callback = nullptr;
    m_key_callback = nullptr;
    m_resize_callback = nullptr;
    m_close_callback = nullptr;
}

void WindowManager::DispatchMouse(int32_t x, int32_t y, int32_t w, int32_t h, int32_t action)
{
    MouseEventCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_mouse_callback;
    }
    if (cb) {
        cb(x, y, w, h, action);
    }
}

void WindowManager::DispatchMultiTouch(int32_t action,
                                       const std::vector<ndk::InputPointer> &pointers)
{
    MultiTouchCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_multitouch_callback;
    }
    if (cb) {
        cb(action, pointers);
    }
}

void WindowManager::DispatchKey(uint32_t vk_code, bool pressed)
{
    KeyEventCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_key_callback;
    }
    if (cb) {
        cb(vk_code, pressed);
    }
}

void WindowManager::DispatchResize(uint32_t width, uint32_t height)
{
    ResizeEventCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_width = width;
        m_height = height;
        cb = m_resize_callback;
    }
    if (cb) {
        cb(width, height);
    }
}

void WindowManager::DispatchClose()
{
    CloseEventCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_close_callback;
        m_running.store(false);
    }
    if (cb) {
        cb();
    }
    m_cv.notify_all();
}

void WindowManager::WaitUntilClosed() noexcept
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !m_running.load() || m_hwnd == nullptr; });
}

void WindowManager::Reset() noexcept
{
    DestroyWindow();
    ClearCallbacks();
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
    DragAcceptFiles(hwnd, TRUE);
    AddClipboardFormatListener(hwnd);

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

    RemoveClipboardFormatListener(hwnd);
    ::DestroyWindow(hwnd);
    UnregisterClassW(L"PortageWindowClass", m_hinstance);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hwnd = nullptr;
    }
    m_cv.notify_all();
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
