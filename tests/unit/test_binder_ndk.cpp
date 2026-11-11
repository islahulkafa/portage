#include "binder/binder_ndk.hpp"
#include "binder/binder_parcel.hpp"
#include "binder/binder_status.hpp"
#include "binder/service_manager.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace portage::binder {

class BinderNdkTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        as_ = std::make_unique<mem::AddressSpace>();
        vfs_ = std::make_unique<fs::Vfs>();
        fd_table_ = std::make_unique<fs::FileDescriptorTable>();
        syscall::SyscallDispatcher::Initialize(as_.get(), vfs_.get(), fd_table_.get(), false);
        ServiceManager::Instance().Reset();
    }

    void TearDown() override
    {
        ServiceManager::Instance().Reset();
        syscall::SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
        as_.reset();
    }

    std::unique_ptr<mem::AddressSpace> as_;
    std::unique_ptr<fs::Vfs> vfs_;
    std::unique_ptr<fs::FileDescriptorTable> fd_table_;
};

TEST_F(BinderNdkTest, CreatesAndManipulatesStatus)
{
    AStatus *ok = AStatus_newOk();
    ASSERT_NE(ok, nullptr);
    EXPECT_TRUE(AStatus_isOk(ok));
    EXPECT_EQ(AStatus_getStatus(ok), STATUS_OK);
    EXPECT_EQ(AStatus_getExceptionCode(ok), EX_NONE);
    EXPECT_STREQ(AStatus_getDescription(ok), "Status: OK");
    AStatus_delete(ok);

    AStatus *ex = AStatus_fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT, "Invalid argument");
    ASSERT_NE(ex, nullptr);
    EXPECT_FALSE(AStatus_isOk(ex));
    EXPECT_EQ(AStatus_getExceptionCode(ex), EX_ILLEGAL_ARGUMENT);
    EXPECT_STREQ(AStatus_getMessage(ex), "Invalid argument");
    const char *desc = AStatus_getDescription(ex);
    EXPECT_NE(std::strstr(desc, "Invalid argument"), nullptr);
    AStatus_delete(ex);

    AStatus *svc = AStatus_fromServiceSpecificError(404);
    ASSERT_NE(svc, nullptr);
    EXPECT_EQ(AStatus_getExceptionCode(svc), EX_SERVICE_SPECIFIC);
    EXPECT_EQ(AStatus_getServiceSpecificError(svc), 404);
    AStatus_delete(svc);

    AStatus *st = AStatus_fromStatus(STATUS_DEAD_OBJECT);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(AStatus_getStatus(st), STATUS_DEAD_OBJECT);
    AStatus_delete(st);
}

TEST_F(BinderNdkTest, SerializesAndDeserializesPrimitivesInParcel)
{
    AParcel *p = AParcel_create();
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(AParcel_writeInt32(p, 42), STATUS_OK);
    EXPECT_EQ(AParcel_writeUint32(p, 0x12345678u), STATUS_OK);
    EXPECT_EQ(AParcel_writeInt64(p, 0x1122334455667788LL), STATUS_OK);
    EXPECT_EQ(AParcel_writeUint64(p, 0xAABBCCDDEEFF0011ULL), STATUS_OK);
    EXPECT_EQ(AParcel_writeFloat(p, 3.14159f), STATUS_OK);
    EXPECT_EQ(AParcel_writeDouble(p, 2.718281828459), STATUS_OK);
    EXPECT_EQ(AParcel_writeBool(p, true), STATUS_OK);
    EXPECT_EQ(AParcel_writeChar(p, u'Z'), STATUS_OK);

    EXPECT_GT(AParcel_getDataSize(p), 0);
    EXPECT_EQ(AParcel_setDataPosition(p, 0), STATUS_OK);

    int32_t i32 = 0;
    EXPECT_EQ(AParcel_readInt32(p, &i32), STATUS_OK);
    EXPECT_EQ(i32, 42);

    uint32_t u32 = 0;
    EXPECT_EQ(AParcel_readUint32(p, &u32), STATUS_OK);
    EXPECT_EQ(u32, 0x12345678u);

    int64_t i64 = 0;
    EXPECT_EQ(AParcel_readInt64(p, &i64), STATUS_OK);
    EXPECT_EQ(i64, 0x1122334455667788LL);

    uint64_t u64 = 0;
    EXPECT_EQ(AParcel_readUint64(p, &u64), STATUS_OK);
    EXPECT_EQ(u64, 0xAABBCCDDEEFF0011ULL);

    float f = 0.0f;
    EXPECT_EQ(AParcel_readFloat(p, &f), STATUS_OK);
    EXPECT_FLOAT_EQ(f, 3.14159f);

    double d = 0.0;
    EXPECT_EQ(AParcel_readDouble(p, &d), STATUS_OK);
    EXPECT_DOUBLE_EQ(d, 2.718281828459);

    bool b = false;
    EXPECT_EQ(AParcel_readBool(p, &b), STATUS_OK);
    EXPECT_TRUE(b);

    char16_t c = 0;
    EXPECT_EQ(AParcel_readChar(p, &c), STATUS_OK);
    EXPECT_EQ(c, u'Z');

    AParcel_delete(p);
}

TEST_F(BinderNdkTest, SerializesAndDeserializesStringsAndByteArrays)
{
    AParcel *p = AParcel_create();
    ASSERT_NE(p, nullptr);

    const char *text = "Hello Portage Binder";
    EXPECT_EQ(AParcel_writeString(p, text, -1), STATUS_OK);

    const int8_t raw_bytes[] = {10, 20, 30, 40, 50};
    EXPECT_EQ(AParcel_writeByteArray(p, raw_bytes, sizeof(raw_bytes)), STATUS_OK);

    EXPECT_EQ(AParcel_setDataPosition(p, 0), STATUS_OK);

    std::string read_str;
    auto str_allocator = [](void *stringData, int32_t length, char **outBuffer)
                             __attribute__((sysv_abi)) -> bool {
        if (length < 0) {
            return true;
        }
        auto *target = static_cast<std::string *>(stringData);
        target->resize(static_cast<size_t>(length));
        *outBuffer = target->data();
        return true;
    };

    EXPECT_EQ(AParcel_readString(p, &read_str, str_allocator), STATUS_OK);
    EXPECT_EQ(read_str, text);

    std::vector<int8_t> read_vec;
    auto byte_allocator = [](void *arrayData, int32_t length, int8_t **outBuffer)
                              __attribute__((sysv_abi)) -> bool {
        if (length < 0) {
            return true;
        }
        auto *target = static_cast<std::vector<int8_t> *>(arrayData);
        target->resize(static_cast<size_t>(length));
        *outBuffer = target->data();
        return true;
    };

    EXPECT_EQ(AParcel_readByteArray(p, &read_vec, byte_allocator), STATUS_OK);
    ASSERT_EQ(read_vec.size(), sizeof(raw_bytes));
    EXPECT_EQ(std::memcmp(read_vec.data(), raw_bytes, sizeof(raw_bytes)), 0);

    AParcel_delete(p);
}

TEST_F(BinderNdkTest, PassesParcelFileDescriptors)
{
    AParcel *p = AParcel_create();
    ASSERT_NE(p, nullptr);

    // Create a pipe descriptor pair
    int pipe_fds[2] = {-1, -1};
    int pipe_res = fd_table_->CreatePipe(pipe_fds);
    ASSERT_EQ(pipe_res, 0);
    ASSERT_GE(pipe_fds[0], 0);

    EXPECT_EQ(AParcel_writeParcelFileDescriptor(p, pipe_fds[0]), STATUS_OK);
    EXPECT_EQ(AParcel_setDataPosition(p, 0), STATUS_OK);

    int read_fd = -1;
    EXPECT_EQ(AParcel_readParcelFileDescriptor(p, &read_fd), STATUS_OK);
    EXPECT_GE(read_fd, 0);
    EXPECT_NE(read_fd, pipe_fds[0]); // Duplicate FD

    fd_table_->Close(read_fd);
    fd_table_->Close(pipe_fds[0]);
    fd_table_->Close(pipe_fds[1]);

    AParcel_delete(p);
}

TEST_F(BinderNdkTest, DefinesClassAndInstantiatesBinder)
{
    static bool destroyed = false;
    destroyed = false;

    auto on_create = [](void *args) __attribute__((sysv_abi)) -> void * { return args; };
    auto on_destroy = [](void *userData) __attribute__((sysv_abi)) -> void {
        (void)userData;
        destroyed = true;
    };
    auto on_transact = [](AIBinder *binder, transaction_code_t code, const AParcel *in,
                          AParcel *out) __attribute__((sysv_abi)) -> binder_status_t {
        (void)binder;
        (void)code;
        (void)in;
        (void)out;
        return STATUS_OK;
    };

    AIBinder_Class *clazz =
        AIBinder_Class_define("com.portage.ITestService", on_create, on_destroy, on_transact);
    ASSERT_NE(clazz, nullptr);
    EXPECT_STREQ(AIBinder_Class_getDescriptor(clazz), "com.portage.ITestService");

    int dummy_data = 12345;
    AIBinder *binder = AIBinder_new(clazz, &dummy_data);
    ASSERT_NE(binder, nullptr);
    EXPECT_EQ(AIBinder_getUserData(binder), &dummy_data);
    EXPECT_TRUE(AIBinder_isAlive(binder));
    EXPECT_FALSE(AIBinder_isRemote(binder));
    EXPECT_EQ(AIBinder_getClass(binder), clazz);

    AIBinder_incStrong(binder);
    AIBinder_decStrong(binder);
    EXPECT_FALSE(destroyed);

    AIBinder_decStrong(binder);
    EXPECT_TRUE(destroyed);

    delete clazz;
}

TEST_F(BinderNdkTest, DispatchesInProcessTransaction)
{
    auto on_transact = [](AIBinder *binder, transaction_code_t code, const AParcel *in,
                          AParcel *out) __attribute__((sysv_abi)) -> binder_status_t {
        (void)binder;
        if (code == 100) {
            int32_t val = 0;
            if (AParcel_readInt32(in, &val) != STATUS_OK) {
                return STATUS_BAD_VALUE;
            }
            AParcel_writeInt32(out, val + 1000);
            return STATUS_OK;
        }
        return STATUS_UNKNOWN_TRANSACTION;
    };

    AIBinder_Class *clazz =
        AIBinder_Class_define("com.portage.ICalculator", nullptr, nullptr, on_transact);
    ASSERT_NE(clazz, nullptr);

    AIBinder *binder = AIBinder_new(clazz, nullptr);
    ASSERT_NE(binder, nullptr);

    AParcel *in = AParcel_create();
    AParcel *out = AParcel_create();
    AParcel_writeInt32(in, 42);

    binder_status_t s = AIBinder_transact(binder, 100, in, out, 0);
    EXPECT_EQ(s, STATUS_OK);

    int32_t result = 0;
    EXPECT_EQ(AParcel_readInt32(out, &result), STATUS_OK);
    EXPECT_EQ(result, 1042);

    AParcel_delete(in);
    AParcel_delete(out);
    AIBinder_decStrong(binder);
    delete clazz;
}

TEST_F(BinderNdkTest, RegistersAndRetrievesServicesFromServiceManager)
{
    // Query synthetic power manager
    AIBinder *power = AServiceManager_getService("power");
    ASSERT_NE(power, nullptr);

    AParcel *in = AParcel_create();
    AParcel *out = AParcel_create();
    EXPECT_EQ(AIBinder_transact(power, 3, in, out, 0), STATUS_OK);

    int32_t exception = -1;
    EXPECT_EQ(AParcel_readInt32(out, &exception), STATUS_OK);
    EXPECT_EQ(exception, 0);

    bool interactive = false;
    EXPECT_EQ(AParcel_readBool(out, &interactive), STATUS_OK);
    EXPECT_TRUE(interactive);

    AIBinder_decStrong(power);
    AParcel_delete(in);
    AParcel_delete(out);

    // Register a custom service
    auto on_transact = [](AIBinder *binder, transaction_code_t code, const AParcel *in,
                          AParcel *out) __attribute__((sysv_abi)) -> binder_status_t {
        (void)binder;
        (void)code;
        (void)in;
        if (out) {
            AParcel_writeInt32(out, 0xCAFEBABE);
        }
        return STATUS_OK;
    };

    AIBinder_Class *clazz =
        AIBinder_Class_define("com.portage.ICustomService", nullptr, nullptr, on_transact);
    ASSERT_NE(clazz, nullptr);
    AIBinder *custom = AIBinder_new(clazz, nullptr);

    EXPECT_EQ(AServiceManager_addService(custom, "custom_service"), STATUS_OK);

    AIBinder *retrieved = AServiceManager_checkService("custom_service");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, custom);

    AParcel *c_in = AParcel_create();
    AParcel *c_out = AParcel_create();
    EXPECT_EQ(AIBinder_transact(retrieved, 1, c_in, c_out, 0), STATUS_OK);

    int32_t magic = 0;
    EXPECT_EQ(AParcel_readInt32(c_out, &magic), STATUS_OK);
    EXPECT_EQ(magic, static_cast<int32_t>(0xCAFEBABE));

    AParcel_delete(c_in);
    AParcel_delete(c_out);
    AIBinder_decStrong(retrieved);
    AIBinder_decStrong(custom);
    delete clazz;
}

TEST_F(BinderNdkTest, RegistersAllNdkSymbols)
{
    auto &registry = ndk::NdkSymbolRegistry::Instance();

    const char *binder_symbols[] = {
        "AStatus_newOk",
        "AStatus_fromExceptionCode",
        "AStatus_fromExceptionCodeWithMessage",
        "AStatus_fromServiceSpecificError",
        "AStatus_fromServiceSpecificErrorWithMessage",
        "AStatus_fromStatus",
        "AStatus_delete",
        "AStatus_isOk",
        "AStatus_getExceptionCode",
        "AStatus_getServiceSpecificError",
        "AStatus_getStatus",
        "AStatus_getMessage",
        "AStatus_getDescription",
        "AParcel_create",
        "AParcel_delete",
        "AParcel_reset",
        "AParcel_setDataPosition",
        "AParcel_getDataPosition",
        "AParcel_getDataSize",
        "AParcel_setDataSize",
        "AParcel_writeInt32",
        "AParcel_writeUint32",
        "AParcel_writeInt64",
        "AParcel_writeUint64",
        "AParcel_writeFloat",
        "AParcel_writeDouble",
        "AParcel_writeBool",
        "AParcel_writeChar",
        "AParcel_writeString",
        "AParcel_writeByteArray",
        "AParcel_writeParcelFileDescriptor",
        "AParcel_writeStrongBinder",
        "AParcel_readInt32",
        "AParcel_readUint32",
        "AParcel_readInt64",
        "AParcel_readUint64",
        "AParcel_readFloat",
        "AParcel_readDouble",
        "AParcel_readBool",
        "AParcel_readChar",
        "AParcel_readString",
        "AParcel_readByteArray",
        "AParcel_readParcelFileDescriptor",
        "AParcel_readStrongBinder",
        "AIBinder_Class_define",
        "AIBinder_new",
        "AIBinder_transact",
        "AIBinder_incStrong",
        "AIBinder_decStrong",
        "AIBinder_getCallingPid",
        "AIBinder_getCallingUid",
        "AIBinder_isAlive",
        "AIBinder_isRemote",
        "AIBinder_getUserData",
        "AIBinder_Class_getDescriptor",
        "AIBinder_associateClass",
        "AIBinder_getClass",
        "AServiceManager_addService",
        "AServiceManager_checkService",
        "AServiceManager_getService",
        "AServiceManager_waitForService",
    };

    for (const char *sym : binder_symbols) {
        uintptr_t addr = registry.Lookup(sym);
        EXPECT_NE(addr, 0u) << "Symbol missing in registry: " << sym;
    }
}

} // namespace portage::binder
