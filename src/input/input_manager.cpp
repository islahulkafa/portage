#include "input_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace portage::input {

InputManager &InputManager::Instance() noexcept
{
    static InputManager instance;
    return instance;
}

InputManager::InputManager() noexcept { Reset(); }

void InputManager::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queues[0].clear();
    m_queues[1].clear();
    m_pointer_pressed = false;
    m_current_tracking_id = 0;
    m_next_tracking_id = 1;
    m_stats = {};
}

LinuxTimeval InputManager::CurrentTimestamp() noexcept
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(now).count() % 1'000'000;
    return LinuxTimeval{.tv_sec = sec, .tv_usec = usec};
}

void InputManager::PostKeyEvent(uint16_t linux_key, bool pressed) noexcept
{
    LinuxTimeval ts = CurrentTimestamp();
    LinuxInputEvent ev{
        .time = ts, .type = LINUX_EV_KEY, .code = linux_key, .value = pressed ? 1 : 0};
    LinuxInputEvent syn{.time = ts, .type = LINUX_EV_SYN, .code = LINUX_SYN_REPORT, .value = 0};

    std::lock_guard<std::mutex> lock(m_mutex);
    auto &q = m_queues[0];
    if (q.size() + 2 > MAX_QUEUE_CAPACITY) {
        q.pop_front();
        q.pop_front();
    }
    q.push_back(ev);
    q.push_back(syn);
    m_stats.key_events_posted++;
    m_cv.notify_all();
}

void InputManager::PostPointerEvent(int32_t client_x, int32_t client_y, int32_t win_w,
                                    int32_t win_h, bool pressed) noexcept
{
    LinuxTimeval ts = CurrentTimestamp();
    int32_t norm_x = std::clamp((client_x * 32767) / std::max(win_w, 1), 0, 32767);
    int32_t norm_y = std::clamp((client_y * 32767) / std::max(win_h, 1), 0, 32767);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (pressed && !m_pointer_pressed) {
        m_current_tracking_id = m_next_tracking_id++;
        m_pointer_pressed = true;
    } else if (!pressed && m_pointer_pressed) {
        m_current_tracking_id = -1;
        m_pointer_pressed = false;
    }

    auto &q = m_queues[1];
    auto push = [&](LinuxInputEvent event) {
        if (q.size() >= MAX_QUEUE_CAPACITY) {
            q.pop_front();
        }
        q.push_back(event);
    };

    push(LinuxInputEvent{.time = ts, .type = LINUX_EV_ABS, .code = LINUX_ABS_MT_SLOT, .value = 0});
    push(LinuxInputEvent{.time = ts,
                         .type = LINUX_EV_ABS,
                         .code = LINUX_ABS_MT_TRACKING_ID,
                         .value = m_current_tracking_id});

    if (pressed) {
        push(LinuxInputEvent{
            .time = ts, .type = LINUX_EV_ABS, .code = LINUX_ABS_MT_POSITION_X, .value = norm_x});
        push(LinuxInputEvent{
            .time = ts, .type = LINUX_EV_ABS, .code = LINUX_ABS_MT_POSITION_Y, .value = norm_y});
        push(LinuxInputEvent{
            .time = ts, .type = LINUX_EV_ABS, .code = LINUX_ABS_X, .value = norm_x});
        push(LinuxInputEvent{
            .time = ts, .type = LINUX_EV_ABS, .code = LINUX_ABS_Y, .value = norm_y});
        push(
            LinuxInputEvent{.time = ts, .type = LINUX_EV_KEY, .code = LINUX_BTN_TOUCH, .value = 1});
    } else {
        push(
            LinuxInputEvent{.time = ts, .type = LINUX_EV_KEY, .code = LINUX_BTN_TOUCH, .value = 0});
    }

    push(LinuxInputEvent{.time = ts, .type = LINUX_EV_SYN, .code = LINUX_SYN_REPORT, .value = 0});
    m_stats.pointer_events_posted++;
    m_cv.notify_all();
}

void InputManager::PostWin32Key(uint32_t vk_code, bool pressed) noexcept
{
    uint16_t linux_key = LINUX_KEY_RESERVED;

    if (vk_code >= '0' && vk_code <= '9') {
        static constexpr uint16_t num_keys[] = {LINUX_KEY_0, LINUX_KEY_1, LINUX_KEY_2, LINUX_KEY_3,
                                                LINUX_KEY_4, LINUX_KEY_5, LINUX_KEY_6, LINUX_KEY_7,
                                                LINUX_KEY_8, LINUX_KEY_9};
        linux_key = num_keys[vk_code - '0'];
    } else if (vk_code >= 'A' && vk_code <= 'Z') {
        static constexpr uint16_t alpha_keys[] = {
            LINUX_KEY_A, LINUX_KEY_B, LINUX_KEY_C, LINUX_KEY_D, LINUX_KEY_E, LINUX_KEY_F,
            LINUX_KEY_G, LINUX_KEY_H, LINUX_KEY_I, LINUX_KEY_J, LINUX_KEY_K, LINUX_KEY_L,
            LINUX_KEY_M, LINUX_KEY_N, LINUX_KEY_O, LINUX_KEY_P, LINUX_KEY_Q, LINUX_KEY_R,
            LINUX_KEY_S, LINUX_KEY_T, LINUX_KEY_U, LINUX_KEY_V, LINUX_KEY_W, LINUX_KEY_X,
            LINUX_KEY_Y, LINUX_KEY_Z};
        linux_key = alpha_keys[vk_code - 'A'];
    } else {
        switch (vk_code) {
        case 0x1B:
            linux_key = LINUX_KEY_ESC;
            break; // VK_ESCAPE
        case 0x0D:
            linux_key = LINUX_KEY_ENTER;
            break; // VK_RETURN
        case 0x20:
            linux_key = LINUX_KEY_SPACE;
            break; // VK_SPACE
        case 0x08:
            linux_key = LINUX_KEY_BACKSPACE;
            break; // VK_BACK
        case 0x09:
            linux_key = LINUX_KEY_TAB;
            break; // VK_TAB
        case 0x10:
            linux_key = LINUX_KEY_LEFTSHIFT;
            break; // VK_SHIFT
        case 0x11:
            linux_key = LINUX_KEY_LEFTCTRL;
            break; // VK_CONTROL
        case 0x12:
            linux_key = LINUX_KEY_LEFTALT;
            break; // VK_MENU
        case 0x25:
            linux_key = LINUX_KEY_LEFT;
            break; // VK_LEFT
        case 0x26:
            linux_key = LINUX_KEY_UP;
            break; // VK_UP
        case 0x27:
            linux_key = LINUX_KEY_RIGHT;
            break; // VK_RIGHT
        case 0x28:
            linux_key = LINUX_KEY_DOWN;
            break; // VK_DOWN
        default:
            return;
        }
    }

    PostKeyEvent(linux_key, pressed);
}

int64_t InputManager::ReadEvents(int device_index, LinuxInputEvent *out_events, size_t max_events,
                                 bool nonblocking) noexcept
{
    if (device_index < 0 || device_index >= 2 || !out_events || max_events == 0) {
        return -22; // EINVAL
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    auto &q = m_queues[device_index];

    if (q.empty()) {
        if (nonblocking) {
            return -11; // EAGAIN
        }
        m_cv.wait_for(lock, std::chrono::milliseconds(50), [&] { return !q.empty(); });
        if (q.empty()) {
            return -11; // EAGAIN
        }
    }

    size_t count = std::min(max_events, q.size());
    for (size_t i = 0; i < count; i++) {
        out_events[i] = q.front();
        q.pop_front();
    }
    m_stats.events_read += count;
    return static_cast<int64_t>(count * sizeof(LinuxInputEvent));
}

bool InputManager::HasEvents(int device_index) const noexcept
{
    if (device_index < 0 || device_index >= 2)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queues[device_index].empty();
}

int InputManager::HandleIoctl(int device_index, uint32_t cmd, void *arg) noexcept
{
    if (device_index < 0 || device_index >= 2) {
        return -19; // ENODEV
    }

    uint32_t type = (cmd >> LINUX_IOC_TYPESHIFT) & LINUX_IOC_TYPEMASK;
    uint32_t nr = (cmd >> LINUX_IOC_NRSHIFT) & LINUX_IOC_NRMASK;
    uint32_t size = (cmd >> LINUX_IOC_SIZESHIFT) & LINUX_IOC_SIZEMASK;

    if (type != 'E') {
        return -22; // EINVAL
    }

    if (nr == 0x01) { // EVIOCGVERSION
        if (!arg)
            return -14; // EFAULT
        *reinterpret_cast<int *>(arg) = static_cast<int>(LINUX_EV_VERSION);
        return 0;
    }

    if (nr == 0x02) { // EVIOCGID
        if (!arg)
            return -14;
        auto *id = reinterpret_cast<LinuxInputId *>(arg);
        id->bustype = LINUX_BUS_USB;
        id->vendor = 0x18d1;
        id->product = (device_index == 0) ? 0x0001 : 0x0002;
        id->version = 1;
        return 0;
    }

    if (nr == 0x06) { // EVIOCGNAME
        if (!arg || size == 0)
            return -14;
        const char *name =
            (device_index == 0) ? "Portage Virtual Keyboard" : "Portage Virtual Touchscreen";
        std::strncpy(reinterpret_cast<char *>(arg), name, size);
        reinterpret_cast<char *>(arg)[size - 1] = '\0';
        return static_cast<int>(std::strlen(reinterpret_cast<char *>(arg)));
    }

    if (nr == 0x09) { // EVIOCGPROP
        if (!arg || size == 0)
            return -14;
        std::memset(arg, 0, size);
        if (device_index == 1) {
            reinterpret_cast<uint8_t *>(arg)[0] |= (1u << LINUX_INPUT_PROP_DIRECT);
        }
        return 0;
    }

    // EVIOCGBIT(ev, len) (nr = 0x20 + ev)
    if (nr >= 0x20 && nr < 0x40) {
        uint32_t ev = nr - 0x20;
        if (!arg || size == 0)
            return -14;
        std::memset(arg, 0, size);
        uint8_t *bits = reinterpret_cast<uint8_t *>(arg);
        auto set_bit = [&](uint32_t b) {
            if (b / 8 < size) {
                bits[b / 8] |= static_cast<uint8_t>(1u << (b % 8));
            }
        };

        if (ev == 0) { // Supported event types bitmask
            set_bit(LINUX_EV_SYN);
            set_bit(LINUX_EV_KEY);
            if (device_index == 0) {
                set_bit(LINUX_EV_REP);
            } else {
                set_bit(LINUX_EV_ABS);
            }
            return static_cast<int>(size);
        }

        if (ev == LINUX_EV_KEY) {
            if (device_index == 0) {
                set_bit(LINUX_KEY_ESC);
                for (uint32_t k = LINUX_KEY_1; k <= LINUX_KEY_0; ++k)
                    set_bit(k);
                for (uint32_t k = LINUX_KEY_Q; k <= LINUX_KEY_P; ++k)
                    set_bit(k);
                for (uint32_t k = LINUX_KEY_A; k <= LINUX_KEY_L; ++k)
                    set_bit(k);
                for (uint32_t k = LINUX_KEY_Z; k <= LINUX_KEY_M; ++k)
                    set_bit(k);
                set_bit(LINUX_KEY_ENTER);
                set_bit(LINUX_KEY_SPACE);
                set_bit(LINUX_KEY_BACKSPACE);
                set_bit(LINUX_KEY_TAB);
                set_bit(LINUX_KEY_UP);
                set_bit(LINUX_KEY_DOWN);
                set_bit(LINUX_KEY_LEFT);
                set_bit(LINUX_KEY_RIGHT);
            } else {
                set_bit(LINUX_BTN_TOUCH);
            }
            return static_cast<int>(size);
        }

        if (ev == LINUX_EV_ABS && device_index == 1) {
            set_bit(LINUX_ABS_X);
            set_bit(LINUX_ABS_Y);
            set_bit(LINUX_ABS_PRESSURE);
            set_bit(LINUX_ABS_MT_SLOT);
            set_bit(LINUX_ABS_MT_TOUCH_MAJOR);
            set_bit(LINUX_ABS_MT_POSITION_X);
            set_bit(LINUX_ABS_MT_POSITION_Y);
            set_bit(LINUX_ABS_MT_TRACKING_ID);
            set_bit(LINUX_ABS_MT_PRESSURE);
            return static_cast<int>(size);
        }

        return static_cast<int>(size);
    }

    // EVIOCGABS(abs) (nr = 0x40 + abs)
    if (nr >= 0x40 && nr < 0x80) {
        uint32_t abs = nr - 0x40;
        if (!arg)
            return -14;
        auto *info = reinterpret_cast<LinuxInputAbsinfo *>(arg);
        std::memset(info, 0, sizeof(LinuxInputAbsinfo));

        if (device_index == 1) {
            if (abs == LINUX_ABS_X || abs == LINUX_ABS_MT_POSITION_X) {
                info->minimum = 0;
                info->maximum = 32767;
                info->resolution = 1;
            } else if (abs == LINUX_ABS_Y || abs == LINUX_ABS_MT_POSITION_Y) {
                info->minimum = 0;
                info->maximum = 32767;
                info->resolution = 1;
            } else if (abs == LINUX_ABS_MT_SLOT) {
                info->minimum = 0;
                info->maximum = 9;
            } else if (abs == LINUX_ABS_MT_TRACKING_ID) {
                info->minimum = 0;
                info->maximum = 65535;
            } else if (abs == LINUX_ABS_PRESSURE || abs == LINUX_ABS_MT_PRESSURE) {
                info->minimum = 0;
                info->maximum = 255;
            } else if (abs == LINUX_ABS_MT_TOUCH_MAJOR) {
                info->minimum = 0;
                info->maximum = 255;
            }
        }
        return 0;
    }

    return -22; // EINVAL
}

InputDeviceStats InputManager::GetStats() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

} // namespace portage::input
