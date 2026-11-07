#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "input/input_manager.hpp"
#include "input/input_types.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace portage;
using namespace portage::input;

class InputManagerTest : public ::testing::Test {
  protected:
    void SetUp() override { InputManager::Instance().Reset(); }

    void TearDown() override { InputManager::Instance().Reset(); }
};

TEST_F(InputManagerTest, VfsResolvesInputDeviceNodes)
{
    fs::Vfs vfs;
    auto r0 = vfs.Resolve("/dev/input/event0");
    EXPECT_EQ(r0.dev_type, fs::DeviceType::InputEvent0);

    auto r1 = vfs.Resolve("/dev/input/event1");
    EXPECT_EQ(r1.dev_type, fs::DeviceType::InputEvent1);

    auto rdir = vfs.Resolve("/dev/input");
    EXPECT_EQ(rdir.dev_type, fs::DeviceType::InputDir);
}

TEST_F(InputManagerTest, FdTableAllocatesAndOpensInputDescriptors)
{
    fs::FileDescriptorTable fd_table;
    int fd0 = fd_table.AllocateInput(0, 0);
    EXPECT_GE(fd0, 3);
    auto desc0 = fd_table.Get(fd0);
    ASSERT_NE(desc0, nullptr);
    EXPECT_EQ(desc0->dev, fs::DeviceType::InputEvent0);
    EXPECT_EQ(desc0->input_device_index, 0);

    int fd1 = fd_table.AllocateInput(1, 00004000); // O_NONBLOCK
    EXPECT_GE(fd1, 3);
    auto desc1 = fd_table.Get(fd1);
    ASSERT_NE(desc1, nullptr);
    EXPECT_EQ(desc1->dev, fs::DeviceType::InputEvent1);
    EXPECT_EQ(desc1->input_device_index, 1);

    EXPECT_EQ(fd_table.Close(fd0), 0);
    EXPECT_EQ(fd_table.Close(fd1), 0);
}

TEST_F(InputManagerTest, IoctlReturnsEvdevVersionAndId)
{
    int version = 0;
    int r = InputManager::Instance().HandleIoctl(0, LINUX_EVIOCGVERSION, &version);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(version, static_cast<int>(LINUX_EV_VERSION));

    LinuxInputId id0{};
    r = InputManager::Instance().HandleIoctl(0, LINUX_EVIOCGID, &id0);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(id0.bustype, LINUX_BUS_USB);
    EXPECT_EQ(id0.vendor, 0x18d1);
    EXPECT_EQ(id0.product, 0x0001);

    LinuxInputId id1{};
    r = InputManager::Instance().HandleIoctl(1, LINUX_EVIOCGID, &id1);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(id1.bustype, LINUX_BUS_USB);
    EXPECT_EQ(id1.vendor, 0x18d1);
    EXPECT_EQ(id1.product, 0x0002);
}

TEST_F(InputManagerTest, IoctlReturnsDeviceNameAndProperties)
{
    char name[128]{};
    uint32_t cmd_name = LinuxIoc(LINUX_IOC_READ, 'E', 0x06, sizeof(name));

    int r = InputManager::Instance().HandleIoctl(0, cmd_name, name);
    EXPECT_GT(r, 0);
    EXPECT_STREQ(name, "Portage Virtual Keyboard");

    std::memset(name, 0, sizeof(name));
    r = InputManager::Instance().HandleIoctl(1, cmd_name, name);
    EXPECT_GT(r, 0);
    EXPECT_STREQ(name, "Portage Virtual Touchscreen");

    uint8_t prop[8]{};
    uint32_t cmd_prop = LinuxIoc(LINUX_IOC_READ, 'E', 0x09, sizeof(prop));

    r = InputManager::Instance().HandleIoctl(0, cmd_prop, prop);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(prop[0], 0);

    r = InputManager::Instance().HandleIoctl(1, cmd_prop, prop);
    EXPECT_EQ(r, 0);
    EXPECT_TRUE((prop[0] & (1u << LINUX_INPUT_PROP_DIRECT)) != 0);
}

TEST_F(InputManagerTest, IoctlReturnsCapabilitiesAndAbsInfo)
{
    uint8_t ev_bits[8]{};
    uint32_t cmd_bit0 = LinuxIoc(LINUX_IOC_READ, 'E', 0x20, sizeof(ev_bits));

    int r = InputManager::Instance().HandleIoctl(0, cmd_bit0, ev_bits);
    EXPECT_GT(r, 0);
    EXPECT_TRUE((ev_bits[0] & (1u << LINUX_EV_SYN)) != 0);
    EXPECT_TRUE((ev_bits[0] & (1u << LINUX_EV_KEY)) != 0);

    std::memset(ev_bits, 0, sizeof(ev_bits));
    r = InputManager::Instance().HandleIoctl(1, cmd_bit0, ev_bits);
    EXPECT_GT(r, 0);
    EXPECT_TRUE((ev_bits[0] & (1u << LINUX_EV_SYN)) != 0);
    EXPECT_TRUE((ev_bits[0] & (1u << LINUX_EV_KEY)) != 0);
    EXPECT_TRUE((ev_bits[0] & (1u << LINUX_EV_ABS)) != 0);

    LinuxInputAbsinfo abs_x{};
    uint32_t cmd_abs_x = LinuxIor('E', 0x40 + LINUX_ABS_MT_POSITION_X, sizeof(LinuxInputAbsinfo));
    r = InputManager::Instance().HandleIoctl(1, cmd_abs_x, &abs_x);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(abs_x.minimum, 0);
    EXPECT_EQ(abs_x.maximum, 32767);
    EXPECT_EQ(abs_x.resolution, 1);

    LinuxInputAbsinfo abs_slot{};
    uint32_t cmd_abs_slot = LinuxIor('E', 0x40 + LINUX_ABS_MT_SLOT, sizeof(LinuxInputAbsinfo));
    r = InputManager::Instance().HandleIoctl(1, cmd_abs_slot, &abs_slot);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(abs_slot.minimum, 0);
    EXPECT_EQ(abs_slot.maximum, 9);
}

TEST_F(InputManagerTest, QueuesKeyEventsAndReadsWithNonblocking)
{
    LinuxInputEvent events[8]{};
    int64_t bytes = InputManager::Instance().ReadEvents(0, events, 8, true);
    EXPECT_EQ(bytes, -11); // -EAGAIN

    EXPECT_FALSE(InputManager::Instance().HasEvents(0));
    InputManager::Instance().PostKeyEvent(LINUX_KEY_A, true);
    EXPECT_TRUE(InputManager::Instance().HasEvents(0));

    bytes = InputManager::Instance().ReadEvents(0, events, 8, true);
    EXPECT_EQ(bytes, static_cast<int64_t>(2 * sizeof(LinuxInputEvent)));

    EXPECT_EQ(events[0].type, LINUX_EV_KEY);
    EXPECT_EQ(events[0].code, LINUX_KEY_A);
    EXPECT_EQ(events[0].value, 1);

    EXPECT_EQ(events[1].type, LINUX_EV_SYN);
    EXPECT_EQ(events[1].code, LINUX_SYN_REPORT);
    EXPECT_EQ(events[1].value, 0);

    EXPECT_FALSE(InputManager::Instance().HasEvents(0));
}

TEST_F(InputManagerTest, TranslatesWin32Keycodes)
{
    InputManager::Instance().PostWin32Key('Z', true);
    LinuxInputEvent events[2]{};
    int64_t bytes = InputManager::Instance().ReadEvents(0, events, 2, true);
    EXPECT_EQ(bytes, static_cast<int64_t>(2 * sizeof(LinuxInputEvent)));
    EXPECT_EQ(events[0].type, LINUX_EV_KEY);
    EXPECT_EQ(events[0].code, LINUX_KEY_Z);
    EXPECT_EQ(events[0].value, 1);

    InputManager::Instance().PostWin32Key(0x0D /* VK_RETURN */, false);
    bytes = InputManager::Instance().ReadEvents(0, events, 2, true);
    EXPECT_EQ(bytes, static_cast<int64_t>(2 * sizeof(LinuxInputEvent)));
    EXPECT_EQ(events[0].type, LINUX_EV_KEY);
    EXPECT_EQ(events[0].code, LINUX_KEY_ENTER);
    EXPECT_EQ(events[0].value, 0);
}

TEST_F(InputManagerTest, ScalesPointerCoordinatesToMultiTouch)
{
    EXPECT_FALSE(InputManager::Instance().HasEvents(1));

    // Touch down in center of 800x600 window -> (16383, 16383)
    InputManager::Instance().PostPointerEvent(400, 300, 800, 600, true);
    EXPECT_TRUE(InputManager::Instance().HasEvents(1));

    LinuxInputEvent events[16]{};
    int64_t bytes = InputManager::Instance().ReadEvents(1, events, 16, true);
    size_t count = bytes / sizeof(LinuxInputEvent);
    EXPECT_GE(count, 6u);

    bool found_slot = false;
    bool found_tracking_id = false;
    bool found_pos_x = false;
    bool found_pos_y = false;
    bool found_touch = false;
    bool found_syn = false;

    for (size_t i = 0; i < count; i++) {
        if (events[i].type == LINUX_EV_ABS && events[i].code == LINUX_ABS_MT_SLOT) {
            EXPECT_EQ(events[i].value, 0);
            found_slot = true;
        } else if (events[i].type == LINUX_EV_ABS && events[i].code == LINUX_ABS_MT_TRACKING_ID) {
            EXPECT_GT(events[i].value, 0);
            found_tracking_id = true;
        } else if (events[i].type == LINUX_EV_ABS && events[i].code == LINUX_ABS_MT_POSITION_X) {
            EXPECT_NEAR(events[i].value, 16383, 10);
            found_pos_x = true;
        } else if (events[i].type == LINUX_EV_ABS && events[i].code == LINUX_ABS_MT_POSITION_Y) {
            EXPECT_NEAR(events[i].value, 16383, 10);
            found_pos_y = true;
        } else if (events[i].type == LINUX_EV_KEY && events[i].code == LINUX_BTN_TOUCH) {
            EXPECT_EQ(events[i].value, 1);
            found_touch = true;
        } else if (events[i].type == LINUX_EV_SYN && events[i].code == LINUX_SYN_REPORT) {
            found_syn = true;
        }
    }

    EXPECT_TRUE(found_slot);
    EXPECT_TRUE(found_tracking_id);
    EXPECT_TRUE(found_pos_x);
    EXPECT_TRUE(found_pos_y);
    EXPECT_TRUE(found_touch);
    EXPECT_TRUE(found_syn);

    // Touch up
    InputManager::Instance().PostPointerEvent(400, 300, 800, 600, false);
    bytes = InputManager::Instance().ReadEvents(1, events, 16, true);
    count = bytes / sizeof(LinuxInputEvent);
    EXPECT_GE(count, 3u);

    bool found_up_tracking = false;
    bool found_up_touch = false;

    for (size_t i = 0; i < count; i++) {
        if (events[i].type == LINUX_EV_ABS && events[i].code == LINUX_ABS_MT_TRACKING_ID) {
            EXPECT_EQ(events[i].value, -1);
            found_up_tracking = true;
        } else if (events[i].type == LINUX_EV_KEY && events[i].code == LINUX_BTN_TOUCH) {
            EXPECT_EQ(events[i].value, 0);
            found_up_touch = true;
        }
    }

    EXPECT_TRUE(found_up_tracking);
    EXPECT_TRUE(found_up_touch);
}
