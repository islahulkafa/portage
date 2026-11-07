#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "property/property_manager.hpp"
#include "property/property_types.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace portage;
using namespace portage::property;

class PropertyManagerTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        PropertyManager::Instance().Reset();
        ASSERT_TRUE(PropertyManager::Instance().Initialize());
    }

    void TearDown() override { PropertyManager::Instance().Reset(); }
};

TEST_F(PropertyManagerTest, InitializesDefaultPropertiesAndHeader)
{
    auto &mgr = PropertyManager::Instance();
    EXPECT_EQ(mgr.GetProperty("ro.build.version.sdk"), "34");
    EXPECT_EQ(mgr.GetProperty("ro.build.version.release"), "14");
    EXPECT_EQ(mgr.GetProperty("ro.product.cpu.abi"), "x86_64");
    EXPECT_EQ(mgr.GetProperty("ro.debuggable"), "1");
    EXPECT_EQ(mgr.GetProperty("sys.boot_completed"), "1");

    const uint8_t *buf = mgr.GetPropertyAreaBuffer();
    ASSERT_NE(buf, nullptr);

    const auto *header = reinterpret_cast<const PropAreaHeader *>(buf);
    EXPECT_EQ(header->magic, PROP_AREA_MAGIC);
    EXPECT_EQ(header->version, PROP_AREA_VERSION);
    EXPECT_GT(header->bytes_used, sizeof(PropTrieNode));

    const uint8_t *serial_buf = mgr.GetSerialBuffer();
    ASSERT_NE(serial_buf, nullptr);
    const auto *serial_header = reinterpret_cast<const PropAreaHeader *>(serial_buf);
    EXPECT_EQ(serial_header->magic, PROP_AREA_MAGIC);
    EXPECT_GE(serial_header->serial, 1u);
}

TEST_F(PropertyManagerTest, SetsAndUpdatesPropertiesDynamically)
{
    auto &mgr = PropertyManager::Instance();

    EXPECT_TRUE(mgr.SetProperty("test.portage.mode", "active"));
    EXPECT_EQ(mgr.GetProperty("test.portage.mode"), "active");

    const auto *serial_header = reinterpret_cast<const PropAreaHeader *>(mgr.GetSerialBuffer());
    uint32_t serial1 = serial_header->serial;

    EXPECT_TRUE(mgr.SetProperty("test.portage.mode", "passive"));
    EXPECT_EQ(mgr.GetProperty("test.portage.mode"), "passive");

    uint32_t serial2 = serial_header->serial;
    EXPECT_GT(serial2, serial1);
}

TEST_F(PropertyManagerTest, HandlesSetPropMessageViaServiceProtocol)
{
    auto &mgr = PropertyManager::Instance();

    PropMsgSetProp msg{};
    msg.cmd = PROP_MSG_SETPROP;
    std::strncpy(msg.name, "persist.portage.unit", PROP_NAME_MAX - 1);
    std::strncpy(msg.value, "success_value", PROP_VALUE_MAX - 1);

    std::span<const uint8_t> payload(reinterpret_cast<const uint8_t *>(&msg), sizeof(msg));
    uint32_t result = mgr.HandleSetPropMessage(payload);

    EXPECT_EQ(result, PROP_SUCCESS);
    EXPECT_EQ(mgr.GetProperty("persist.portage.unit"), "success_value");

    // Invalid command
    msg.cmd = 999;
    EXPECT_NE(mgr.HandleSetPropMessage(payload), PROP_SUCCESS);
}

TEST_F(PropertyManagerTest, ParsesBuildPropFileFromRootfs)
{
    auto temp_dir = std::filesystem::temp_directory_path() / "portage_prop_test";
    std::filesystem::create_directories(temp_dir / "system");

    std::ofstream out(temp_dir / "system" / "build.prop");
    out << "# Portage Virtual Android build.prop\n";
    out << "ro.custom.property=custom_val_123\n";
    out << "ro.test.flag=true\n";
    out.close();

    PropertyManager::Instance().Reset();
    ASSERT_TRUE(PropertyManager::Instance().Initialize(temp_dir));

    EXPECT_EQ(PropertyManager::Instance().GetProperty("ro.custom.property"), "custom_val_123");
    EXPECT_EQ(PropertyManager::Instance().GetProperty("ro.test.flag"), "true");
    EXPECT_EQ(PropertyManager::Instance().GetProperty("ro.build.version.sdk"), "34");

    std::filesystem::remove_all(temp_dir);
}

TEST_F(PropertyManagerTest, VfsResolvesPropertyPathsCorrectly)
{
    fs::Vfs vfs;

    auto r_dir = vfs.Resolve("/dev/__properties__");
    EXPECT_EQ(r_dir.dev_type, fs::DeviceType::PropAreaDir);

    auto r_serial = vfs.Resolve("/dev/__properties__/properties_serial");
    EXPECT_EQ(r_serial.dev_type, fs::DeviceType::PropSerial);

    auto r_info = vfs.Resolve("/dev/__properties__/property_info");
    EXPECT_EQ(r_info.dev_type, fs::DeviceType::PropInfo);

    auto r_context = vfs.Resolve("/dev/__properties__/u:object_r:default_prop:s0");
    EXPECT_EQ(r_context.dev_type, fs::DeviceType::PropContext);

    auto r_socket = vfs.Resolve("/dev/socket/property_service");
    EXPECT_EQ(r_socket.dev_type, fs::DeviceType::PropServiceSocket);
}
