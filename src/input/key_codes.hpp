#pragma once

#include <cstdint>

namespace portage::input {

enum AndroidKeyCode : int32_t {
    AKEYCODE_UNKNOWN = 0,
    AKEYCODE_SOFT_LEFT = 1,
    AKEYCODE_SOFT_RIGHT = 2,
    AKEYCODE_HOME = 3,
    AKEYCODE_BACK = 4,
    AKEYCODE_CALL = 5,
    AKEYCODE_ENDCALL = 6,
    AKEYCODE_0 = 7,
    AKEYCODE_1 = 8,
    AKEYCODE_2 = 9,
    AKEYCODE_3 = 10,
    AKEYCODE_4 = 11,
    AKEYCODE_5 = 12,
    AKEYCODE_6 = 13,
    AKEYCODE_7 = 14,
    AKEYCODE_8 = 15,
    AKEYCODE_9 = 16,
    AKEYCODE_STAR = 17,
    AKEYCODE_POUND = 18,
    AKEYCODE_DPAD_UP = 19,
    AKEYCODE_DPAD_DOWN = 20,
    AKEYCODE_DPAD_LEFT = 21,
    AKEYCODE_DPAD_RIGHT = 22,
    AKEYCODE_DPAD_CENTER = 23,
    AKEYCODE_VOLUME_UP = 24,
    AKEYCODE_VOLUME_DOWN = 25,
    AKEYCODE_POWER = 26,
    AKEYCODE_CAMERA = 27,
    AKEYCODE_CLEAR = 28,
    AKEYCODE_A = 29,
    AKEYCODE_B = 30,
    AKEYCODE_C = 31,
    AKEYCODE_D = 32,
    AKEYCODE_E = 33,
    AKEYCODE_F = 34,
    AKEYCODE_G = 35,
    AKEYCODE_H = 36,
    AKEYCODE_I = 37,
    AKEYCODE_J = 38,
    AKEYCODE_K = 39,
    AKEYCODE_L = 40,
    AKEYCODE_M = 41,
    AKEYCODE_N = 42,
    AKEYCODE_O = 43,
    AKEYCODE_P = 44,
    AKEYCODE_Q = 45,
    AKEYCODE_R = 46,
    AKEYCODE_S = 47,
    AKEYCODE_T = 48,
    AKEYCODE_U = 49,
    AKEYCODE_V = 50,
    AKEYCODE_W = 51,
    AKEYCODE_X = 52,
    AKEYCODE_Y = 53,
    AKEYCODE_Z = 54,
    AKEYCODE_COMMA = 55,
    AKEYCODE_PERIOD = 56,
    AKEYCODE_ALT_LEFT = 57,
    AKEYCODE_ALT_RIGHT = 58,
    AKEYCODE_SHIFT_LEFT = 59,
    AKEYCODE_SHIFT_RIGHT = 60,
    AKEYCODE_TAB = 61,
    AKEYCODE_SPACE = 62,
    AKEYCODE_SYM = 63,
    AKEYCODE_EXPLORER = 64,
    AKEYCODE_ENVELOPE = 65,
    AKEYCODE_ENTER = 66,
    AKEYCODE_DEL = 67,
    AKEYCODE_GRAVE = 68,
    AKEYCODE_MINUS = 69,
    AKEYCODE_EQUALS = 70,
    AKEYCODE_LEFT_BRACKET = 71,
    AKEYCODE_RIGHT_BRACKET = 72,
    AKEYCODE_BACKSLASH = 73,
    AKEYCODE_SEMICOLON = 74,
    AKEYCODE_APOSTROPHE = 75,
    AKEYCODE_SLASH = 76,
    AKEYCODE_AT = 77,
    AKEYCODE_NUM = 78,
    AKEYCODE_HEADSETHOOK = 79,
    AKEYCODE_FOCUS = 80,
    AKEYCODE_PLUS = 81,
    AKEYCODE_MENU = 82,
    AKEYCODE_NOTIFICATION = 83,
    AKEYCODE_SEARCH = 84,
    AKEYCODE_MEDIA_PLAY_PAUSE = 85,
    AKEYCODE_MEDIA_STOP = 86,
    AKEYCODE_MEDIA_NEXT = 87,
    AKEYCODE_MEDIA_PREVIOUS = 88,
    AKEYCODE_MEDIA_REWIND = 89,
    AKEYCODE_MEDIA_FAST_FORWARD = 90,
    AKEYCODE_MUTE = 91,
    AKEYCODE_PAGE_UP = 92,
    AKEYCODE_PAGE_DOWN = 93,
    AKEYCODE_PICTSYMBOLS = 94,
    AKEYCODE_SWITCH_CHARSET = 95,
    AKEYCODE_BUTTON_A = 96,
    AKEYCODE_BUTTON_B = 97,
    AKEYCODE_BUTTON_C = 98,
    AKEYCODE_BUTTON_X = 99,
    AKEYCODE_BUTTON_Y = 100,
    AKEYCODE_BUTTON_Z = 101,
    AKEYCODE_BUTTON_L1 = 102,
    AKEYCODE_BUTTON_R1 = 103,
    AKEYCODE_BUTTON_L2 = 104,
    AKEYCODE_BUTTON_R2 = 105,
    AKEYCODE_BUTTON_THUMBL = 106,
    AKEYCODE_BUTTON_THUMBR = 107,
    AKEYCODE_BUTTON_START = 108,
    AKEYCODE_BUTTON_SELECT = 109,
    AKEYCODE_BUTTON_MODE = 110,
    AKEYCODE_ESCAPE = 111,
    AKEYCODE_FORWARD_DEL = 112,
    AKEYCODE_CTRL_LEFT = 113,
    AKEYCODE_CTRL_RIGHT = 114,
    AKEYCODE_CAPS_LOCK = 115,
    AKEYCODE_SCROLL_LOCK = 116,
    AKEYCODE_META_LEFT = 117,
    AKEYCODE_META_RIGHT = 118,
    AKEYCODE_FUNCTION = 119,
    AKEYCODE_SYSRQ = 120,
    AKEYCODE_BREAK = 121,
    AKEYCODE_MOVE_HOME = 122,
    AKEYCODE_MOVE_END = 123,
    AKEYCODE_INSERT = 124,
    AKEYCODE_FORWARD = 125,
    AKEYCODE_MEDIA_PLAY = 126,
    AKEYCODE_MEDIA_PAUSE = 127,
    AKEYCODE_MEDIA_CLOSE = 128,
    AKEYCODE_MEDIA_EJECT = 129,
    AKEYCODE_MEDIA_RECORD = 130,
    AKEYCODE_F1 = 131,
    AKEYCODE_F2 = 132,
    AKEYCODE_F3 = 133,
    AKEYCODE_F4 = 134,
    AKEYCODE_F5 = 135,
    AKEYCODE_F6 = 136,
    AKEYCODE_F7 = 137,
    AKEYCODE_F8 = 138,
    AKEYCODE_F9 = 139,
    AKEYCODE_F10 = 140,
    AKEYCODE_F11 = 141,
    AKEYCODE_F12 = 142,
    AKEYCODE_NUM_LOCK = 143,
};

[[nodiscard]] constexpr int32_t Win32VkToAndroidKeyCode(uint32_t vk) noexcept
{
    // Letters 'A' through 'Z'
    if (vk >= 0x41 && vk <= 0x5A) {
        return AKEYCODE_A + static_cast<int32_t>(vk - 0x41);
    }
    // Numbers '0' through '9'
    if (vk >= 0x30 && vk <= 0x39) {
        return AKEYCODE_0 + static_cast<int32_t>(vk - 0x30);
    }
    // Numpad '0' through '9' (VK_NUMPAD0 = 0x60)
    if (vk >= 0x60 && vk <= 0x69) {
        return AKEYCODE_0 + static_cast<int32_t>(vk - 0x60);
    }
    // Function keys F1 - F12 (VK_F1 = 0x70)
    if (vk >= 0x70 && vk <= 0x7B) {
        return AKEYCODE_F1 + static_cast<int32_t>(vk - 0x70);
    }

    switch (vk) {
    case 0x1B: // VK_ESCAPE
        return AKEYCODE_BACK;
    case 0x0D: // VK_RETURN
        return AKEYCODE_ENTER;
    case 0x20: // VK_SPACE
        return AKEYCODE_SPACE;
    case 0x08: // VK_BACK
        return AKEYCODE_DEL;
    case 0x2E: // VK_DELETE
        return AKEYCODE_FORWARD_DEL;
    case 0x09: // VK_TAB
        return AKEYCODE_TAB;
    case 0x25: // VK_LEFT
        return AKEYCODE_DPAD_LEFT;
    case 0x26: // VK_UP
        return AKEYCODE_DPAD_UP;
    case 0x27: // VK_RIGHT
        return AKEYCODE_DPAD_RIGHT;
    case 0x28: // VK_DOWN
        return AKEYCODE_DPAD_DOWN;
    case 0x24: // VK_HOME
        return AKEYCODE_MOVE_HOME;
    case 0x23: // VK_END
        return AKEYCODE_MOVE_END;
    case 0x21: // VK_PRIOR (Page Up)
        return AKEYCODE_PAGE_UP;
    case 0x22: // VK_NEXT (Page Down)
        return AKEYCODE_PAGE_DOWN;
    case 0x2D: // VK_INSERT
        return AKEYCODE_INSERT;
    case 0x10: // VK_SHIFT
    case 0xA0: // VK_LSHIFT
        return AKEYCODE_SHIFT_LEFT;
    case 0xA1: // VK_RSHIFT
        return AKEYCODE_SHIFT_RIGHT;
    case 0x11: // VK_CONTROL
    case 0xA2: // VK_LCONTROL
        return AKEYCODE_CTRL_LEFT;
    case 0xA3: // VK_RCONTROL
        return AKEYCODE_CTRL_RIGHT;
    case 0x12: // VK_MENU (Alt)
    case 0xA4: // VK_LMENU
        return AKEYCODE_ALT_LEFT;
    case 0xA5: // VK_RMENU
        return AKEYCODE_ALT_RIGHT;
    case 0x14: // VK_CAPITAL
        return AKEYCODE_CAPS_LOCK;
    case 0x90: // VK_NUMLOCK
        return AKEYCODE_NUM_LOCK;
    case 0x91: // VK_SCROLL
        return AKEYCODE_SCROLL_LOCK;
    case 0xBC: // VK_OEM_COMMA
        return AKEYCODE_COMMA;
    case 0xBE: // VK_OEM_PERIOD
        return AKEYCODE_PERIOD;
    case 0xBD: // VK_OEM_MINUS
        return AKEYCODE_MINUS;
    case 0xBB: // VK_OEM_PLUS
        return AKEYCODE_EQUALS;
    case 0xBA: // VK_OEM_1 (;: in US)
        return AKEYCODE_SEMICOLON;
    case 0xBF: // VK_OEM_2 (/? in US)
        return AKEYCODE_SLASH;
    case 0xC0: // VK_OEM_3 (`~ in US)
        return AKEYCODE_GRAVE;
    case 0xDB: // VK_OEM_4 ([{ in US)
        return AKEYCODE_LEFT_BRACKET;
    case 0xDC: // VK_OEM_5 (\| in US)
        return AKEYCODE_BACKSLASH;
    case 0xDD: // VK_OEM_6 (]} in US)
        return AKEYCODE_RIGHT_BRACKET;
    case 0xDE: // VK_OEM_7 ('" in US)
        return AKEYCODE_APOSTROPHE;
    default:
        return AKEYCODE_UNKNOWN;
    }
}

} // namespace portage::input
