#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::input {

#define LINUX_IOC_NRBITS 8
#define LINUX_IOC_TYPEBITS 8
#define LINUX_IOC_SIZEBITS 14
#define LINUX_IOC_DIRBITS 2

#define LINUX_IOC_NRMASK ((1u << LINUX_IOC_NRBITS) - 1)
#define LINUX_IOC_TYPEMASK ((1u << LINUX_IOC_TYPEBITS) - 1)
#define LINUX_IOC_SIZEMASK ((1u << LINUX_IOC_SIZEBITS) - 1)
#define LINUX_IOC_DIRMASK ((1u << LINUX_IOC_DIRBITS) - 1)

#define LINUX_IOC_NRSHIFT 0
#define LINUX_IOC_TYPESHIFT (LINUX_IOC_NRSHIFT + LINUX_IOC_NRBITS)
#define LINUX_IOC_SIZESHIFT (LINUX_IOC_TYPESHIFT + LINUX_IOC_TYPEBITS)
#define LINUX_IOC_DIRSHIFT (LINUX_IOC_SIZESHIFT + LINUX_IOC_SIZEBITS)

#define LINUX_IOC_NONE 0u
#define LINUX_IOC_WRITE 1u
#define LINUX_IOC_READ 2u

constexpr uint32_t LinuxIoc(uint32_t dir, uint32_t type, uint32_t nr, uint32_t size) noexcept
{
    return (dir << LINUX_IOC_DIRSHIFT) | (type << LINUX_IOC_TYPESHIFT) | (nr << LINUX_IOC_NRSHIFT) |
           (size << LINUX_IOC_SIZESHIFT);
}

constexpr uint32_t LinuxIor(uint32_t type, uint32_t nr, uint32_t size) noexcept
{
    return LinuxIoc(LINUX_IOC_READ, type, nr, size);
}

// Event Types
constexpr uint16_t LINUX_EV_SYN = 0x00;
constexpr uint16_t LINUX_EV_KEY = 0x01;
constexpr uint16_t LINUX_EV_REL = 0x02;
constexpr uint16_t LINUX_EV_ABS = 0x03;
constexpr uint16_t LINUX_EV_MSC = 0x04;
constexpr uint16_t LINUX_EV_SW = 0x05;
constexpr uint16_t LINUX_EV_LED = 0x11;
constexpr uint16_t LINUX_EV_SND = 0x12;
constexpr uint16_t LINUX_EV_REP = 0x14;
constexpr uint16_t LINUX_EV_FF = 0x15;
constexpr uint16_t LINUX_EV_PWR = 0x16;
constexpr uint16_t LINUX_EV_MAX = 0x1f;

// Synchronization Codes
constexpr uint16_t LINUX_SYN_REPORT = 0;
constexpr uint16_t LINUX_SYN_CONFIG = 1;
constexpr uint16_t LINUX_SYN_MT_REPORT = 2;
constexpr uint16_t LINUX_SYN_DROPPED = 3;

// Absolute Axes
constexpr uint16_t LINUX_ABS_X = 0x00;
constexpr uint16_t LINUX_ABS_Y = 0x01;
constexpr uint16_t LINUX_ABS_PRESSURE = 0x18;
constexpr uint16_t LINUX_ABS_MT_SLOT = 0x2f;
constexpr uint16_t LINUX_ABS_MT_TOUCH_MAJOR = 0x30;
constexpr uint16_t LINUX_ABS_MT_TOUCH_MINOR = 0x31;
constexpr uint16_t LINUX_ABS_MT_WIDTH_MAJOR = 0x32;
constexpr uint16_t LINUX_ABS_MT_WIDTH_MINOR = 0x33;
constexpr uint16_t LINUX_ABS_MT_ORIENTATION = 0x34;
constexpr uint16_t LINUX_ABS_MT_POSITION_X = 0x35;
constexpr uint16_t LINUX_ABS_MT_POSITION_Y = 0x36;
constexpr uint16_t LINUX_ABS_MT_TOOL_TYPE = 0x37;
constexpr uint16_t LINUX_ABS_MT_BLOB_ID = 0x38;
constexpr uint16_t LINUX_ABS_MT_TRACKING_ID = 0x39;
constexpr uint16_t LINUX_ABS_MT_PRESSURE = 0x3a;
constexpr uint16_t LINUX_ABS_MAX = 0x3f;

// Relative Axes
constexpr uint16_t LINUX_REL_X = 0x00;
constexpr uint16_t LINUX_REL_Y = 0x01;
constexpr uint16_t LINUX_REL_WHEEL = 0x08;
constexpr uint16_t LINUX_REL_MAX = 0x0f;

// Keys and Buttons
constexpr uint16_t LINUX_KEY_RESERVED = 0;
constexpr uint16_t LINUX_KEY_ESC = 1;
constexpr uint16_t LINUX_KEY_1 = 2;
constexpr uint16_t LINUX_KEY_2 = 3;
constexpr uint16_t LINUX_KEY_3 = 4;
constexpr uint16_t LINUX_KEY_4 = 5;
constexpr uint16_t LINUX_KEY_5 = 6;
constexpr uint16_t LINUX_KEY_6 = 7;
constexpr uint16_t LINUX_KEY_7 = 8;
constexpr uint16_t LINUX_KEY_8 = 9;
constexpr uint16_t LINUX_KEY_9 = 10;
constexpr uint16_t LINUX_KEY_0 = 11;
constexpr uint16_t LINUX_KEY_MINUS = 12;
constexpr uint16_t LINUX_KEY_EQUAL = 13;
constexpr uint16_t LINUX_KEY_BACKSPACE = 14;
constexpr uint16_t LINUX_KEY_TAB = 15;
constexpr uint16_t LINUX_KEY_Q = 16;
constexpr uint16_t LINUX_KEY_W = 17;
constexpr uint16_t LINUX_KEY_E = 18;
constexpr uint16_t LINUX_KEY_R = 19;
constexpr uint16_t LINUX_KEY_T = 20;
constexpr uint16_t LINUX_KEY_Y = 21;
constexpr uint16_t LINUX_KEY_U = 22;
constexpr uint16_t LINUX_KEY_I = 23;
constexpr uint16_t LINUX_KEY_O = 24;
constexpr uint16_t LINUX_KEY_P = 25;
constexpr uint16_t LINUX_KEY_LEFTBRACE = 26;
constexpr uint16_t LINUX_KEY_RIGHTBRACE = 27;
constexpr uint16_t LINUX_KEY_ENTER = 28;
constexpr uint16_t LINUX_KEY_LEFTCTRL = 29;
constexpr uint16_t LINUX_KEY_A = 30;
constexpr uint16_t LINUX_KEY_S = 31;
constexpr uint16_t LINUX_KEY_D = 32;
constexpr uint16_t LINUX_KEY_F = 33;
constexpr uint16_t LINUX_KEY_G = 34;
constexpr uint16_t LINUX_KEY_H = 35;
constexpr uint16_t LINUX_KEY_J = 36;
constexpr uint16_t LINUX_KEY_K = 37;
constexpr uint16_t LINUX_KEY_L = 38;
constexpr uint16_t LINUX_KEY_SEMICOLON = 39;
constexpr uint16_t LINUX_KEY_APOSTROPHE = 40;
constexpr uint16_t LINUX_KEY_GRAVE = 41;
constexpr uint16_t LINUX_KEY_LEFTSHIFT = 42;
constexpr uint16_t LINUX_KEY_BACKSLASH = 43;
constexpr uint16_t LINUX_KEY_Z = 44;
constexpr uint16_t LINUX_KEY_X = 45;
constexpr uint16_t LINUX_KEY_C = 46;
constexpr uint16_t LINUX_KEY_V = 47;
constexpr uint16_t LINUX_KEY_B = 48;
constexpr uint16_t LINUX_KEY_N = 49;
constexpr uint16_t LINUX_KEY_M = 50;
constexpr uint16_t LINUX_KEY_COMMA = 51;
constexpr uint16_t LINUX_KEY_DOT = 52;
constexpr uint16_t LINUX_KEY_SLASH = 53;
constexpr uint16_t LINUX_KEY_RIGHTSHIFT = 54;
constexpr uint16_t LINUX_KEY_KPASTERISK = 55;
constexpr uint16_t LINUX_KEY_LEFTALT = 56;
constexpr uint16_t LINUX_KEY_SPACE = 57;
constexpr uint16_t LINUX_KEY_CAPSLOCK = 58;
constexpr uint16_t LINUX_KEY_UP = 103;
constexpr uint16_t LINUX_KEY_LEFT = 105;
constexpr uint16_t LINUX_KEY_RIGHT = 106;
constexpr uint16_t LINUX_KEY_DOWN = 108;
constexpr uint16_t LINUX_KEY_MAX = 0x2ff;

// Buttons
constexpr uint16_t LINUX_BTN_MISC = 0x100;
constexpr uint16_t LINUX_BTN_LEFT = 0x110;
constexpr uint16_t LINUX_BTN_RIGHT = 0x111;
constexpr uint16_t LINUX_BTN_MIDDLE = 0x112;
constexpr uint16_t LINUX_BTN_TOUCH = 0x14a;

// Device Properties
constexpr uint16_t LINUX_INPUT_PROP_POINTER = 0x00;
constexpr uint16_t LINUX_INPUT_PROP_DIRECT = 0x01;
constexpr uint16_t LINUX_INPUT_PROP_MAX = 0x1f;

// Bus types
constexpr uint16_t LINUX_BUS_USB = 0x03;
constexpr uint16_t LINUX_BUS_VIRTUAL = 0x06;

// Structures
#pragma pack(push, 1)
struct LinuxTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct LinuxInputEvent {
    LinuxTimeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct LinuxInputId {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

struct LinuxInputAbsinfo {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};
#pragma pack(pop)

static_assert(sizeof(LinuxInputEvent) == 24,
              "LinuxInputEvent must be 24 bytes on x86_64 Linux ABI");

// Evdev IOCTL Definitions
constexpr uint32_t LINUX_EVIOCGVERSION = LinuxIor('E', 0x01, sizeof(int));
constexpr uint32_t LINUX_EVIOCGID = LinuxIor('E', 0x02, sizeof(LinuxInputId));

constexpr uint32_t LINUX_EV_VERSION = 0x010001;

} // namespace portage::input
