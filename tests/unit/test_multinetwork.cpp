#include "ndk/multinetwork.hpp"
#include "fs/fd_table.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"
#include "poll/epoll_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace portage::ndk;
using namespace portage::package;
using namespace portage::syscall;

class MultinetworkTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        vfs_ = std::make_unique<portage::fs::Vfs>();
        fd_table_ = std::make_unique<portage::fs::FileDescriptorTable>();
        SyscallDispatcher::Initialize(nullptr, vfs_.get(), fd_table_.get(), false);
        MultinetworkManager::Instance().Reset();
    }

    void TearDown() override
    {
        MultinetworkManager::Instance().Reset();
        SyscallDispatcher::Shutdown();
        fd_table_.reset();
        vfs_.reset();
    }

    std::unique_ptr<portage::fs::Vfs> vfs_;
    std::unique_ptr<portage::fs::FileDescriptorTable> fd_table_;
};

TEST_F(MultinetworkTest, ProcessAndSocketNetworkLifecycle)
{
    net_handle_t net = 0;
    int err = android_getprocnetwork(&net);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(net, NET_HANDLE_DEFAULT);

    err = android_setprocnetwork(NET_HANDLE_WIFI);
    EXPECT_EQ(err, 0);

    err = android_getprocnetwork(&net);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(net, NET_HANDLE_WIFI);

    err = android_setprocnetwork(NETWORK_UNSPECIFIED);
    EXPECT_EQ(err, 0);

    err = android_getprocnetwork(&net);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(net, NETWORK_UNSPECIFIED);

    err = android_getprocnetwork(nullptr);
    EXPECT_EQ(err, -22);

    int sock_err = android_setsocknetwork(NET_HANDLE_WIFI, 9999);
    EXPECT_EQ(sock_err, -9);
}

TEST_F(MultinetworkTest, NetworkScopedAddrInfoResolution)
{
    LinuxAddrInfo hints{};
    hints.ai_family = 2;   // AF_INET
    hints.ai_socktype = 1; // SOCK_STREAM

    LinuxAddrInfo *res = nullptr;
    int err = android_getaddrinfofornetwork(NET_HANDLE_DEFAULT, "127.0.0.1", "8080", &hints, &res);
    EXPECT_EQ(err, 0);
    ASSERT_NE(res, nullptr);

    EXPECT_EQ(res->ai_family, 2);
    EXPECT_EQ(res->ai_socktype, 1);
    EXPECT_GE(res->ai_addrlen, 16u);
    ASSERT_NE(res->ai_addr, nullptr);

    auto *sin = reinterpret_cast<const uint8_t *>(res->ai_addr);
    uint16_t port = (static_cast<uint16_t>(sin[2]) << 8) | sin[3];
    EXPECT_EQ(port, 8080);
    EXPECT_EQ(sin[4], 127);
    EXPECT_EQ(sin[5], 0);
    EXPECT_EQ(sin[6], 0);
    EXPECT_EQ(sin[7], 1);

    android_freeaddrinfofornetwork(res);
}

TEST_F(MultinetworkTest, AsyncDnsQueryAndEpollIntegration)
{
    int query_fd = android_res_nquery(NET_HANDLE_DEFAULT, "localhost", 1, 1, 0);
    ASSERT_GE(query_fd, 0);

    auto *table = SyscallDispatcher::GetFdTable();
    ASSERT_NE(table, nullptr);

    int epoll_id = portage::poll::EpollManager::Instance().CreateInstance();
    ASSERT_GE(epoll_id, 0);
    int epoll_fd = table->AllocateEpoll(epoll_id, 0);
    ASSERT_GE(epoll_fd, 0);

    portage::poll::LinuxEpollEvent ev{};
    ev.events = portage::poll::LINUX_EPOLLIN;
    ev.data = static_cast<uint64_t>(query_fd);
    int ctl_res = portage::poll::EpollManager::Instance().Ctl(
        epoll_id, portage::poll::LINUX_EPOLL_CTL_ADD, query_fd, &ev);
    EXPECT_EQ(ctl_res, 0);

    portage::poll::LinuxEpollEvent events[4]{};
    int ready = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (ready == 0 && std::chrono::steady_clock::now() < deadline) {
        ready = portage::poll::EpollManager::Instance().Wait(epoll_id, events, 4, 50, table);
    }

    EXPECT_GT(ready, 0);
    EXPECT_EQ(static_cast<int>(events[0].data), query_fd);
    EXPECT_TRUE((events[0].events & portage::poll::LINUX_EPOLLIN) != 0);

    int rcode = -1;
    int res_len = android_res_nresult(query_fd, &rcode);
    EXPECT_GT(res_len, 12);
    EXPECT_EQ(rcode, 0);

    uint8_t buffer[512]{};
    auto desc = table->Get(query_fd);
    ASSERT_NE(desc, nullptr);
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(desc->handle, buffer, sizeof(buffer), &bytes_read, nullptr);
    EXPECT_TRUE(ok);
    EXPECT_GE(bytes_read, 12u);

    // Header validation (QR=1, RCODE=0, QDCOUNT=1, ANCOUNT>=1)
    EXPECT_EQ((buffer[2] & 0x80), 0x80);
    EXPECT_EQ((buffer[3] & 0x0F), 0);
    uint16_t qdcount = (static_cast<uint16_t>(buffer[4]) << 8) | buffer[5];
    uint16_t ancount = (static_cast<uint16_t>(buffer[6]) << 8) | buffer[7];
    EXPECT_EQ(qdcount, 1);
    EXPECT_GE(ancount, 1);

    table->Close(query_fd);
    table->Close(epoll_fd);
}

TEST_F(MultinetworkTest, RawDnsSendAndResultParsing)
{
    // Build DNS query packet for "localhost" (ID=0x4321, QTYPE=1, QCLASS=1)
    std::vector<uint8_t> query_msg = {
        0x43, 0x21,                                               // ID
        0x01, 0x00,                                               // RD=1
        0x00, 0x01,                                               // QDCOUNT=1
        0x00, 0x00,                                               // ANCOUNT=0
        0x00, 0x00,                                               // NSCOUNT=0
        0x00, 0x00,                                               // ARCOUNT=0
        0x09, 'l',  'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0x00, // QNAME
        0x00, 0x01,                                               // QTYPE=A
        0x00, 0x01                                                // QCLASS=IN
    };

    int query_fd = android_res_nsend(NET_HANDLE_DEFAULT, query_msg.data(), query_msg.size(), 0);
    ASSERT_GE(query_fd, 0);

    int rcode = -1;
    int res_len = android_res_nresult(query_fd, &rcode);
    EXPECT_GT(res_len, 12);
    EXPECT_EQ(rcode, 0);

    android_res_cancel(query_fd);
}

TEST_F(MultinetworkTest, QueryCancellation)
{
    int query_fd = android_res_nquery(NET_HANDLE_DEFAULT, "cancellation.domain.test", 1, 1, 0);
    ASSERT_GE(query_fd, 0);

    android_res_cancel(query_fd);

    int rcode = -1;
    int res_len = android_res_nresult(query_fd, &rcode);
    EXPECT_LT(res_len, 0);
}

TEST_F(MultinetworkTest, ConnectivityManagerJniReflection)
{
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jstring conn_str = env.CreateString("connectivity");
    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jobject cm_obj = jenv->functions->CallObjectMethod(jenv, nullptr, get_svc_mid, conn_str);
    ASSERT_NE(cm_obj, nullptr);

    jmethodID get_active_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getActiveNetwork"));
    jobject net_obj = jenv->functions->CallObjectMethod(jenv, cm_obj, get_active_mid);
    ASSERT_NE(net_obj, nullptr);

    jmethodID get_handle_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getNetworkHandle"));
    jlong handle = jenv->functions->CallLongMethod(jenv, net_obj, get_handle_mid);
    EXPECT_EQ(static_cast<net_handle_t>(handle), NET_HANDLE_DEFAULT);

    jmethodID get_caps_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getNetworkCapabilities"));
    jobject caps_obj = jenv->functions->CallObjectMethod(jenv, cm_obj, get_caps_mid, net_obj);
    ASSERT_NE(caps_obj, nullptr);

    jmethodID has_cap_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasCapability"));
    jboolean has_net = jenv->functions->CallBooleanMethod(jenv, caps_obj, has_cap_mid, 12);
    EXPECT_EQ(has_net, JNI_TRUE);

    jmethodID has_trans_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasTransport"));
    jboolean has_wifi = jenv->functions->CallBooleanMethod(jenv, caps_obj, has_trans_mid, 1);
    EXPECT_EQ(has_wifi, JNI_TRUE);
    jboolean has_unknown = jenv->functions->CallBooleanMethod(jenv, caps_obj, has_trans_mid, 99);
    EXPECT_EQ(has_unknown, JNI_FALSE);

    jmethodID get_lp_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getLinkProperties"));
    jobject lp_obj = jenv->functions->CallObjectMethod(jenv, cm_obj, get_lp_mid, net_obj);
    ASSERT_NE(lp_obj, nullptr);

    jmethodID get_iface_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getInterfaceName"));
    jstring iface_str =
        static_cast<jstring>(jenv->functions->CallObjectMethod(jenv, lp_obj, get_iface_mid));
    ASSERT_NE(iface_str, nullptr);
    EXPECT_STREQ(env.GetStringChars(iface_str), "portage0");

    jmethodID get_ni_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getActiveNetworkInfo"));
    jobject ni_obj = jenv->functions->CallObjectMethod(jenv, cm_obj, get_ni_mid);
    ASSERT_NE(ni_obj, nullptr);

    jmethodID is_conn_mid = reinterpret_cast<jmethodID>(const_cast<char *>("isConnected"));
    jboolean is_conn = jenv->functions->CallBooleanMethod(jenv, ni_obj, is_conn_mid);
    EXPECT_EQ(is_conn, JNI_TRUE);

    jmethodID get_type_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getType"));
    jint net_type = jenv->functions->CallIntMethod(jenv, ni_obj, get_type_mid);
    EXPECT_EQ(net_type, 1);
}

TEST_F(MultinetworkTest, NdkSymbolRegistryLookup)
{
    auto &reg = NdkSymbolRegistry::Instance();
    EXPECT_NE(reg.Lookup("android_setsocknetwork"), 0u);
    EXPECT_NE(reg.Lookup("android_setprocnetwork"), 0u);
    EXPECT_NE(reg.Lookup("android_getprocnetwork"), 0u);
    EXPECT_NE(reg.Lookup("android_getaddrinfofornetwork"), 0u);
    EXPECT_NE(reg.Lookup("android_freeaddrinfofornetwork"), 0u);
    EXPECT_NE(reg.Lookup("android_res_nquery"), 0u);
    EXPECT_NE(reg.Lookup("android_res_nsearch"), 0u);
    EXPECT_NE(reg.Lookup("android_res_nsend"), 0u);
    EXPECT_NE(reg.Lookup("android_res_nresult"), 0u);
    EXPECT_NE(reg.Lookup("android_res_cancel"), 0u);
    EXPECT_NE(reg.Lookup("android_freeaddrinfo"), 0u);
    EXPECT_NE(reg.Lookup("freeaddrinfo"), 0u);
}
