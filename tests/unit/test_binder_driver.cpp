#include "binder/binder_driver.hpp"
#include "binder/binder_types.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace portage::binder;

class BinderDriverTest : public ::testing::Test {
  protected:
    void SetUp() override { BinderDriver::Instance().Reset(); }

    void TearDown() override { BinderDriver::Instance().Reset(); }
};

TEST_F(BinderDriverTest, ReturnsCorrectProtocolVersion)
{
    auto &driver = BinderDriver::Instance();
    int bid = driver.CreateInstance(false);
    ASSERT_GT(bid, 0);

    BinderVersion ver{};
    EXPECT_EQ(driver.Ioctl(bid, BINDER_VERSION, &ver), 0);
    EXPECT_EQ(ver.protocol_version, BINDER_CURRENT_PROTOCOL_VERSION);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
}

TEST_F(BinderDriverTest, SetsMaxThreadsWithoutError)
{
    auto &driver = BinderDriver::Instance();
    int bid = driver.CreateInstance(false);
    ASSERT_GT(bid, 0);

    uint32_t max_threads = 15;
    EXPECT_EQ(driver.Ioctl(bid, BINDER_SET_MAX_THREADS, &max_threads), 0);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
}

TEST_F(BinderDriverTest, SetsIdleTimeoutWithoutError)
{
    auto &driver = BinderDriver::Instance();
    int bid = driver.CreateInstance(true);
    ASSERT_GT(bid, 0);

    int64_t timeout_ns = 5000000000LL;
    EXPECT_EQ(driver.Ioctl(bid, BINDER_SET_IDLE_TIMEOUT, &timeout_ns), 0);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
}

TEST_F(BinderDriverTest, HandlesBcTransactionWithEmptyBrReply)
{
    auto &driver = BinderDriver::Instance();
    int bid = driver.CreateInstance(false);
    ASSERT_GT(bid, 0);

    std::vector<uint8_t> write_buf(sizeof(uint32_t) + sizeof(BinderTransactionData));
    uint32_t bc_cmd = BC_TRANSACTION_64;
    std::memcpy(write_buf.data(), &bc_cmd, sizeof(uint32_t));

    BinderTransactionData txn{};
    txn.target.handle = 0;
    txn.code = 1;
    txn.flags = 0; // Synchronous (expects reply)
    std::memcpy(write_buf.data() + sizeof(uint32_t), &txn, sizeof(BinderTransactionData));

    std::vector<uint8_t> read_buf(256, 0);

    BinderWriteRead bwr{};
    bwr.write_size = static_cast<int64_t>(write_buf.size());
    bwr.write_consumed = 0;
    bwr.write_buffer = reinterpret_cast<uint64_t>(write_buf.data());
    bwr.read_size = static_cast<int64_t>(read_buf.size());
    bwr.read_consumed = 0;
    bwr.read_buffer = reinterpret_cast<uint64_t>(read_buf.data());

    EXPECT_EQ(driver.Ioctl(bid, BINDER_WRITE_READ_64, &bwr), 0);
    EXPECT_EQ(bwr.write_consumed, bwr.write_size);
    EXPECT_EQ(bwr.read_consumed, static_cast<int64_t>(sizeof(uint32_t) + sizeof(uint32_t) +
                                                      sizeof(BinderTransactionData)));

    uint32_t ret_cmd1 = 0;
    std::memcpy(&ret_cmd1, read_buf.data(), sizeof(uint32_t));
    EXPECT_EQ(ret_cmd1, BR_NOOP);

    uint32_t ret_cmd2 = 0;
    std::memcpy(&ret_cmd2, read_buf.data() + sizeof(uint32_t), sizeof(uint32_t));
    EXPECT_EQ(ret_cmd2, BR_REPLY);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
}

TEST_F(BinderDriverTest, HandlesTfOneWayTransactionWithNoReply)
{
    auto &driver = BinderDriver::Instance();
    int bid = driver.CreateInstance(false);
    ASSERT_GT(bid, 0);

    std::vector<uint8_t> write_buf(sizeof(uint32_t) + sizeof(BinderTransactionData));
    uint32_t bc_cmd = BC_TRANSACTION_64;
    std::memcpy(write_buf.data(), &bc_cmd, sizeof(uint32_t));

    BinderTransactionData txn{};
    txn.target.handle = 0;
    txn.code = 1;
    txn.flags = TF_ONE_WAY; // One-way asynchronous
    std::memcpy(write_buf.data() + sizeof(uint32_t), &txn, sizeof(BinderTransactionData));

    std::vector<uint8_t> read_buf(256, 0);

    BinderWriteRead bwr{};
    bwr.write_size = static_cast<int64_t>(write_buf.size());
    bwr.write_consumed = 0;
    bwr.write_buffer = reinterpret_cast<uint64_t>(write_buf.data());
    bwr.read_size = static_cast<int64_t>(read_buf.size());
    bwr.read_consumed = 0;
    bwr.read_buffer = reinterpret_cast<uint64_t>(read_buf.data());

    EXPECT_EQ(driver.Ioctl(bid, BINDER_WRITE_READ_64, &bwr), 0);
    EXPECT_EQ(bwr.write_consumed, bwr.write_size);
    EXPECT_EQ(bwr.read_consumed, static_cast<int64_t>(sizeof(uint32_t)));

    uint32_t ret_cmd1 = 0;
    std::memcpy(&ret_cmd1, read_buf.data(), sizeof(uint32_t));
    EXPECT_EQ(ret_cmd1, BR_NOOP);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
}

TEST_F(BinderDriverTest, RejectsInvalidArgumentsAndInstances)
{
    auto &driver = BinderDriver::Instance();

    // Invalid instance
    EXPECT_EQ(driver.Ioctl(999, BINDER_VERSION, nullptr), -9);

    int bid = driver.CreateInstance(false);
    ASSERT_GT(bid, 0);

    // Null argument
    EXPECT_EQ(driver.Ioctl(bid, BINDER_VERSION, nullptr), -14);

    // Invalid command
    uint32_t dummy = 0;
    EXPECT_EQ(driver.Ioctl(bid, 0xdeadbeef, &dummy), -22);

    EXPECT_EQ(driver.CloseInstance(bid), 0);
    EXPECT_EQ(driver.CloseInstance(bid), -9);
}
