#include "package/dex_vm.hpp"
#include "package/jni_env.hpp"
#include "java_runtime_dex_builder.hpp"

#include <gtest/gtest.h>

using namespace portage::package;
using namespace portage::package::test_util;

namespace {

class JavaThreadTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        DexVm::Instance().Reset();
        JniEnvironment::Instance().Reset();
    }
    void TearDown() override
    {
        DexVm::Instance().Reset();
        JniEnvironment::Instance().Reset();
    }
};

} // namespace

TEST_F(JavaThreadTest, TwoThreadsSynchronizedCounterReachesExactTotal)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});

    uint32_t f_count = b.AddField("Lcom/example/Counter;", "count", "I");
    uint32_t c_init = b.AddMethod("Lcom/example/Counter;", "<init>", "V", {});
    uint32_t c_inc = b.AddMethod("Lcom/example/Counter;", "inc", "V", {});
    uint32_t run_m = b.AddMethod("Lcom/example/Worker;", "run", "V", {});
    uint32_t f_counter = b.AddField("Lcom/example/Worker;", "counter", "Lcom/example/Counter;");
    uint32_t w_init = b.AddMethod("Lcom/example/Worker;", "<init>", "V", {"Lcom/example/Counter;"});
    uint32_t main_m = b.AddMethod("Lcom/example/Runner;", "runTwoThreads", "I", {});

    auto &counter_cls = b.AddClass("Lcom/example/Counter;");
    counter_cls.instance_fields.push_back({f_count, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = counter_cls.class_idx;
        m.method_idx = c_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        m.outs_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        counter_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = counter_cls.class_idx;
        m.method_idx = c_inc;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 3;
        m.ins_size = 1;
        m.outs_size = 0;
        m.insns.push_back(Encode11x(OP_MONITOR_ENTER, 2));
        Emit22c(m.insns, OP_IGET, 0, 2, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode11n(OP_CONST_4, 1, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 0, 1));
        Emit22c(m.insns, OP_IPUT, 0, 2, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode11x(OP_MONITOR_EXIT, 2));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        counter_cls.virtual_methods.push_back(std::move(m));
    }

    auto &worker_cls = b.AddClass("Lcom/example/Worker;");
    worker_cls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    worker_cls.instance_fields.push_back({f_counter, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = worker_cls.class_idx;
        m.method_idx = w_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 2;
        m.ins_size = 2;
        m.outs_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT_OBJECT, 1, 0, static_cast<uint16_t>(f_counter));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        worker_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = worker_cls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 4;
        m.ins_size = 1;
        m.outs_size = 1;
        Emit21s(m.insns, OP_CONST_16, 1, 200);
        m.insns.push_back(Encode11n(OP_CONST_4, 0, 0));
        uint16_t loop_pc = static_cast<uint16_t>(m.insns.size());
        Emit22t(m.insns, OP_IF_GE, 0, 1, 10);
        Emit22c(m.insns, OP_IGET_OBJECT, 2, 3, static_cast<uint16_t>(f_counter));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(c_inc), {2});
        m.insns.push_back(Encode11n(OP_CONST_4, 2, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 0, 2));
        size_t goto_idx = m.insns.size();
        int8_t back_off =
            static_cast<int8_t>(static_cast<ptrdiff_t>(loop_pc) - static_cast<ptrdiff_t>(goto_idx));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(back_off) << 8)));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        worker_cls.virtual_methods.push_back(std::move(m));
    }

    auto &runner_cls = b.AddClass("Lcom/example/Runner;");
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = runner_cls.class_idx;
        m.method_idx = main_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 5;
        m.ins_size = 0;
        m.outs_size = 2;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Lcom/example/Counter;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(c_init), {0});

        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Lcom/example/Worker;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {1, 0});

        uint16_t thr_t = static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;"));
        Emit21c(m.insns, OP_NEW_INSTANCE, 2, thr_t);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {2, 1});
        Emit21c(m.insns, OP_NEW_INSTANCE, 3, thr_t);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {3, 1});

        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {2});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {3});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {2});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {3});

        Emit22c(m.insns, OP_IGET, 4, 0, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode11x(OP_RETURN, 4));
        runner_cls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/Runner;", "runTwoThreads", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 400);
}

TEST_F(JavaThreadTest, EightThreadsAccSynchronizedCounterReachesExactTotal)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});

    uint32_t f_count = b.AddField("Lcom/example/SyncCounter;", "count", "I");
    uint32_t c_init = b.AddMethod("Lcom/example/SyncCounter;", "<init>", "V", {});
    uint32_t c_inc = b.AddMethod("Lcom/example/SyncCounter;", "inc", "V", {});
    uint32_t f_c = b.AddField("Lcom/example/SyncWorker;", "c", "Lcom/example/SyncCounter;");
    uint32_t w_init =
        b.AddMethod("Lcom/example/SyncWorker;", "<init>", "V", {"Lcom/example/SyncCounter;"});
    uint32_t w_run = b.AddMethod("Lcom/example/SyncWorker;", "run", "V", {});

    auto &ccls = b.AddClass("Lcom/example/SyncCounter;");
    ccls.instance_fields.push_back({f_count, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = ccls.class_idx;
        m.method_idx = c_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        ccls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = ccls.class_idx;
        m.method_idx = c_inc;
        m.access_flags = ACC_PUBLIC | ACC_SYNCHRONIZED;
        m.registers_size = 3;
        m.ins_size = 1;
        Emit22c(m.insns, OP_IGET, 0, 2, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode11n(OP_CONST_4, 1, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 0, 1));
        Emit22c(m.insns, OP_IPUT, 0, 2, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        ccls.virtual_methods.push_back(std::move(m));
    }

    auto &wcls = b.AddClass("Lcom/example/SyncWorker;");
    wcls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    wcls.instance_fields.push_back({f_c, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 2;
        m.ins_size = 2;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT_OBJECT, 1, 0, static_cast<uint16_t>(f_c));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 4;
        m.ins_size = 1;
        Emit21s(m.insns, OP_CONST_16, 1, 100);
        m.insns.push_back(Encode11n(OP_CONST_4, 0, 0));
        uint16_t loop_pc = static_cast<uint16_t>(m.insns.size());
        Emit22t(m.insns, OP_IF_GE, 0, 1, 10);
        Emit22c(m.insns, OP_IGET_OBJECT, 2, 3, static_cast<uint16_t>(f_c));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(c_inc), {2});
        m.insns.push_back(Encode11n(OP_CONST_4, 2, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 0, 2));
        size_t goto_idx = m.insns.size();
        int8_t back_off =
            static_cast<int8_t>(static_cast<ptrdiff_t>(loop_pc) - static_cast<ptrdiff_t>(goto_idx));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(back_off) << 8)));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.virtual_methods.push_back(std::move(m));
    }

    auto &rcls = b.AddClass("Lcom/example/EightRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/EightRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = rcls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 7;
        uint16_t t_arr = static_cast<uint16_t>(b.AddType("[Ljava/lang/Thread;"));
        uint16_t thr_t = static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;"));
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Lcom/example/SyncCounter;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(c_init), {0});
        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Lcom/example/SyncWorker;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {1, 0});

        Emit21s(m.insns, OP_CONST_16, 3, 8);
        Emit22c(m.insns, OP_NEW_ARRAY, 2, 3, t_arr);
        m.insns.push_back(Encode11n(OP_CONST_4, 4, 0));
        uint16_t s_loop = static_cast<uint16_t>(m.insns.size());
        size_t s_exit = m.insns.size();
        Emit22t(m.insns, OP_IF_GE, 4, 3, 0);
        Emit21c(m.insns, OP_NEW_INSTANCE, 5, thr_t);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {5, 1});
        Emit23x(m.insns, OP_APUT_OBJECT, 5, 2, 4);
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {5});
        m.insns.push_back(Encode11n(OP_CONST_4, 6, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 4, 6));
        int8_t s_back = static_cast<int8_t>(static_cast<ptrdiff_t>(s_loop) -
                                            static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(s_back) << 8)));
        m.insns[s_exit + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - s_exit));

        m.insns.push_back(Encode11n(OP_CONST_4, 4, 0));
        uint16_t j_loop = static_cast<uint16_t>(m.insns.size());
        size_t j_exit = m.insns.size();
        Emit22t(m.insns, OP_IF_GE, 4, 3, 0);
        Emit23x(m.insns, OP_AGET_OBJECT, 5, 2, 4);
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {5});
        m.insns.push_back(Encode11n(OP_CONST_4, 6, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 4, 6));
        int8_t j_back = static_cast<int8_t>(static_cast<ptrdiff_t>(j_loop) -
                                            static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(j_back) << 8)));
        m.insns[j_exit + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - j_exit));

        Emit22c(m.insns, OP_IGET, 4, 0, static_cast<uint16_t>(f_count));
        m.insns.push_back(Encode11x(OP_RETURN, 4));
        rcls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/EightRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 800);
}

TEST_F(JavaThreadTest, ObjectWaitAndNotifyProducerConsumer)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t obj_wait = b.AddMethod("Ljava/lang/Object;", "wait", "V", {});
    uint32_t obj_notifyAll = b.AddMethod("Ljava/lang/Object;", "notifyAll", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});

    uint32_t f_ready = b.AddField("Lcom/example/MailBox;", "ready", "I");
    uint32_t f_val = b.AddField("Lcom/example/MailBox;", "val", "I");
    uint32_t mb_init = b.AddMethod("Lcom/example/MailBox;", "<init>", "V", {});
    uint32_t f_box = b.AddField("Lcom/example/Producer;", "box", "Lcom/example/MailBox;");
    uint32_t p_init =
        b.AddMethod("Lcom/example/Producer;", "<init>", "V", {"Lcom/example/MailBox;"});
    uint32_t p_run = b.AddMethod("Lcom/example/Producer;", "run", "V", {});

    auto &mb_cls = b.AddClass("Lcom/example/MailBox;");
    mb_cls.instance_fields.push_back({f_ready, ACC_PUBLIC});
    mb_cls.instance_fields.push_back({f_val, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = mb_cls.class_idx;
        m.method_idx = mb_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        mb_cls.direct_methods.push_back(std::move(m));
    }

    auto &p_cls = b.AddClass("Lcom/example/Producer;");
    p_cls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    p_cls.instance_fields.push_back({f_box, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = p_cls.class_idx;
        m.method_idx = p_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 2;
        m.ins_size = 2;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT_OBJECT, 1, 0, static_cast<uint16_t>(f_box));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        p_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = p_cls.class_idx;
        m.method_idx = p_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 3; // p0(v2)=this
        m.ins_size = 1;
        Emit22c(m.insns, OP_IGET_OBJECT, 0, 2, static_cast<uint16_t>(f_box));
        m.insns.push_back(Encode11x(OP_MONITOR_ENTER, 0));
        Emit21s(m.insns, OP_CONST_16, 1, 77);
        Emit22c(m.insns, OP_IPUT, 1, 0, static_cast<uint16_t>(f_val));
        Emit21s(m.insns, OP_CONST_16, 1, 1);
        Emit22c(m.insns, OP_IPUT, 1, 0, static_cast<uint16_t>(f_ready));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(obj_notifyAll), {0});
        m.insns.push_back(Encode11x(OP_MONITOR_EXIT, 0));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        p_cls.virtual_methods.push_back(std::move(m));
    }

    auto &r_cls = b.AddClass("Lcom/example/WaitRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/WaitRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = r_cls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 5;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Lcom/example/MailBox;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(mb_init), {0});
        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Lcom/example/Producer;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(p_init), {1, 0});
        Emit21c(m.insns, OP_NEW_INSTANCE, 2,
                static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {2, 1});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {2});

        m.insns.push_back(Encode11x(OP_MONITOR_ENTER, 0));
        uint16_t w_loop = static_cast<uint16_t>(m.insns.size());
        Emit22c(m.insns, OP_IGET, 3, 0, static_cast<uint16_t>(f_ready));
        size_t w_done = m.insns.size();
        Emit22t(m.insns, OP_IF_NEZ, 3, 0, 0);
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(obj_wait), {0});
        int8_t w_back = static_cast<int8_t>(static_cast<ptrdiff_t>(w_loop) -
                                            static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(w_back) << 8)));
        m.insns[w_done + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - w_done));
        Emit22c(m.insns, OP_IGET, 4, 0, static_cast<uint16_t>(f_val));
        m.insns.push_back(Encode11x(OP_MONITOR_EXIT, 0));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {2});
        m.insns.push_back(Encode11x(OP_RETURN, 4));
        r_cls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/WaitRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 77);
}

TEST_F(JavaThreadTest, ThreadSleepInterruptThrowsInterruptedException)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_interrupt = b.AddMethod("Ljava/lang/Thread;", "interrupt", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});
    uint32_t thr_sleep = b.AddMethod("Ljava/lang/Thread;", "sleep", "V", {"J"});

    uint32_t f_caught = b.AddField("Lcom/example/Sleeper;", "caught", "I");
    uint32_t f_started = b.AddField("Lcom/example/Sleeper;", "started", "I");
    uint32_t s_init = b.AddMethod("Lcom/example/Sleeper;", "<init>", "V", {});
    uint32_t s_run = b.AddMethod("Lcom/example/Sleeper;", "run", "V", {});

    auto &s_cls = b.AddClass("Lcom/example/Sleeper;");
    s_cls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    s_cls.instance_fields.push_back({f_caught, ACC_PUBLIC});
    s_cls.instance_fields.push_back({f_started, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = s_cls.class_idx;
        m.method_idx = s_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        s_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = s_cls.class_idx;
        m.method_idx = s_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 3; // p0(v2)=this
        m.ins_size = 1;
        Emit21s(m.insns, OP_CONST_16, 0, 1);
        Emit22c(m.insns, OP_IPUT, 0, 2, static_cast<uint16_t>(f_started));
        Emit51l(m.insns, OP_CONST_WIDE, 0, 5000ULL);
        uint32_t try_start = static_cast<uint32_t>(m.insns.size());
        Emit35c(m.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(thr_sleep), {0, 1});
        uint32_t try_end = static_cast<uint32_t>(m.insns.size());
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        uint32_t catch_pc = static_cast<uint32_t>(m.insns.size());
        m.insns.push_back(Encode11x(OP_MOVE_EXCEPTION, 0));
        Emit21s(m.insns, OP_CONST_16, 0, 1);
        Emit22c(m.insns, OP_IPUT, 0, 2, static_cast<uint16_t>(f_caught));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        RuntimeDexBuilder::TrySpec ts;
        ts.start_addr = try_start;
        ts.insn_count = static_cast<uint16_t>(try_end - try_start);
        ts.handlers.push_back({"Ljava/lang/InterruptedException;", catch_pc});
        m.tries.push_back(std::move(ts));
        s_cls.virtual_methods.push_back(std::move(m));
    }

    auto &r_cls = b.AddClass("Lcom/example/SleepRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/SleepRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = r_cls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 4;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Lcom/example/Sleeper;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(s_init), {0});
        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {1, 0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {1});
        // Interrupt immediately (ThreadRecord sets interrupted flag or wakes sleep cleanly)
        Emit51l(m.insns, OP_CONST_WIDE, 2, 10ULL);
        Emit35c(m.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(thr_sleep), {2, 3});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_interrupt), {1});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {1});
        Emit22c(m.insns, OP_IGET, 2, 0, static_cast<uint16_t>(f_caught));
        m.insns.push_back(Encode11x(OP_RETURN, 2));
        r_cls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/SleepRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 1);
}

TEST_F(JavaThreadTest, ExecutorsFixedPoolAndFutureGet)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t m_newFixed = b.AddMethod("Ljava/util/concurrent/Executors;", "newFixedThreadPool",
                                      "Ljava/util/concurrent/ExecutorService;", {"I"});
    uint32_t m_submit =
        b.AddMethod("Ljava/util/concurrent/ExecutorService;", "submit",
                    "Ljava/util/concurrent/Future;", {"Ljava/util/concurrent/Callable;"});
    uint32_t m_shutdown =
        b.AddMethod("Ljava/util/concurrent/ExecutorService;", "shutdown", "V", {});
    uint32_t m_awaitTerm = b.AddMethod("Ljava/util/concurrent/ExecutorService;", "awaitTermination",
                                       "Z", {"J", "Ljava/util/concurrent/TimeUnit;"});
    uint32_t m_fut_get =
        b.AddMethod("Ljava/util/concurrent/Future;", "get", "Ljava/lang/Object;", {});
    uint32_t m_int_valOf =
        b.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    uint32_t m_int_intVal = b.AddMethod("Ljava/lang/Integer;", "intValue", "I", {});
    uint32_t f_sec =
        b.AddField("Ljava/util/concurrent/TimeUnit;", "SECONDS", "Ljava/util/concurrent/TimeUnit;");

    auto &t_cls = b.AddClass("Lcom/example/ComputeTask;");
    t_cls.interfaces.push_back(b.AddType("Ljava/util/concurrent/Callable;"));
    uint32_t t_init = b.AddMethod("Lcom/example/ComputeTask;", "<init>", "V", {});
    uint32_t t_call = b.AddMethod("Lcom/example/ComputeTask;", "call", "Ljava/lang/Object;", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = t_cls.class_idx;
        m.method_idx = t_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        t_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = t_cls.class_idx;
        m.method_idx = t_call;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 2;
        m.ins_size = 1;
        Emit21s(m.insns, OP_CONST_16, 0, 1234);
        Emit35c(m.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));
        m.insns.push_back(Encode11x(OP_RETURN_OBJECT, 0));
        t_cls.virtual_methods.push_back(std::move(m));
    }

    auto &r_cls = b.AddClass("Lcom/example/ExecRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/ExecRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = r_cls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 6;
        Emit21s(m.insns, OP_CONST_16, 0, 2);
        Emit35c(m.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_newFixed), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 0));

        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Lcom/example/ComputeTask;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(t_init), {1});
        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_submit), {0, 1});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 2));

        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_fut_get), {2});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_int_intVal), {3});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT, 3));

        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_shutdown), {0});
        Emit51l(m.insns, OP_CONST_WIDE, 4, 2ULL);
        Emit21c(m.insns, OP_SGET_OBJECT, 1, static_cast<uint16_t>(f_sec));
        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_awaitTerm), {0, 4, 5, 1});
        m.insns.push_back(Encode11x(OP_RETURN, 3));
        r_cls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/ExecRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 1234);
}

TEST_F(JavaThreadTest, CountDownLatchAndAtomics)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t m_ai_init =
        b.AddMethod("Ljava/util/concurrent/atomic/AtomicInteger;", "<init>", "V", {"I"});
    uint32_t m_ai_inc =
        b.AddMethod("Ljava/util/concurrent/atomic/AtomicInteger;", "incrementAndGet", "I", {});
    uint32_t m_ai_get = b.AddMethod("Ljava/util/concurrent/atomic/AtomicInteger;", "get", "I", {});
    uint32_t m_cdl_init =
        b.AddMethod("Ljava/util/concurrent/CountDownLatch;", "<init>", "V", {"I"});
    uint32_t m_cdl_down =
        b.AddMethod("Ljava/util/concurrent/CountDownLatch;", "countDown", "V", {});
    uint32_t m_cdl_await = b.AddMethod("Ljava/util/concurrent/CountDownLatch;", "await", "V", {});

    uint32_t f_ai = b.AddField("Lcom/example/LatchWorker;", "ai",
                               "Ljava/util/concurrent/atomic/AtomicInteger;");
    uint32_t f_latch =
        b.AddField("Lcom/example/LatchWorker;", "latch", "Ljava/util/concurrent/CountDownLatch;");
    uint32_t w_init = b.AddMethod(
        "Lcom/example/LatchWorker;", "<init>", "V",
        {"Ljava/util/concurrent/atomic/AtomicInteger;", "Ljava/util/concurrent/CountDownLatch;"});
    uint32_t w_run = b.AddMethod("Lcom/example/LatchWorker;", "run", "V", {});

    auto &wcls = b.AddClass("Lcom/example/LatchWorker;");
    wcls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    wcls.instance_fields.push_back({f_ai, ACC_PUBLIC});
    wcls.instance_fields.push_back({f_latch, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 3;
        m.ins_size = 3;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT_OBJECT, 1, 0, static_cast<uint16_t>(f_ai));
        Emit22c(m.insns, OP_IPUT_OBJECT, 2, 0, static_cast<uint16_t>(f_latch));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 2; // p0(v1)=this
        m.ins_size = 1;
        Emit22c(m.insns, OP_IGET_OBJECT, 0, 1, static_cast<uint16_t>(f_ai));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ai_inc), {0});
        Emit22c(m.insns, OP_IGET_OBJECT, 0, 1, static_cast<uint16_t>(f_latch));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_cdl_down), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.virtual_methods.push_back(std::move(m));
    }

    auto &rcls = b.AddClass("Lcom/example/LatchRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/LatchRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = rcls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 6;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Ljava/util/concurrent/atomic/AtomicInteger;")));
        Emit21s(m.insns, OP_CONST_16, 3, 0);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_ai_init), {0, 3});
        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Ljava/util/concurrent/CountDownLatch;")));
        Emit21s(m.insns, OP_CONST_16, 3, 3);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_cdl_init), {1, 3});
        Emit21c(m.insns, OP_NEW_INSTANCE, 2,
                static_cast<uint16_t>(b.AddType("Lcom/example/LatchWorker;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {2, 0, 1});

        m.insns.push_back(Encode11n(OP_CONST_4, 4, 0));
        uint16_t loop = static_cast<uint16_t>(m.insns.size());
        size_t exit_j = m.insns.size();
        Emit22t(m.insns, OP_IF_GE, 4, 3, 0);
        Emit21c(m.insns, OP_NEW_INSTANCE, 5,
                static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {5, 2});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {5});
        m.insns.push_back(Encode11n(OP_CONST_4, 5, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 4, 5));
        int8_t back = static_cast<int8_t>(static_cast<ptrdiff_t>(loop) -
                                          static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(back) << 8)));
        m.insns[exit_j + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - exit_j));

        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_cdl_await), {1});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_ai_get), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));
        m.insns.push_back(Encode11x(OP_RETURN, 0));
        rcls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/LatchRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 3);
}

TEST_F(JavaThreadTest, ReentrantLockAndConditionSignaling)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});

    uint32_t m_rl_init =
        b.AddMethod("Ljava/util/concurrent/locks/ReentrantLock;", "<init>", "V", {});
    uint32_t m_rl_lock = b.AddMethod("Ljava/util/concurrent/locks/ReentrantLock;", "lock", "V", {});
    uint32_t m_rl_unlock =
        b.AddMethod("Ljava/util/concurrent/locks/ReentrantLock;", "unlock", "V", {});
    uint32_t m_rl_holds =
        b.AddMethod("Ljava/util/concurrent/locks/ReentrantLock;", "getHoldCount", "I", {});
    uint32_t m_rl_newCond =
        b.AddMethod("Ljava/util/concurrent/locks/ReentrantLock;", "newCondition",
                    "Ljava/util/concurrent/locks/Condition;", {});
    uint32_t m_c_await = b.AddMethod("Ljava/util/concurrent/locks/Condition;", "await", "V", {});
    uint32_t m_c_sigAll =
        b.AddMethod("Ljava/util/concurrent/locks/Condition;", "signalAll", "V", {});

    uint32_t f_lk =
        b.AddField("Lcom/example/CondWorker;", "lk", "Ljava/util/concurrent/locks/ReentrantLock;");
    uint32_t f_cond =
        b.AddField("Lcom/example/CondWorker;", "cond", "Ljava/util/concurrent/locks/Condition;");
    uint32_t f_done = b.AddField("Lcom/example/CondWorker;", "done", "I");
    uint32_t w_init = b.AddMethod(
        "Lcom/example/CondWorker;", "<init>", "V",
        {"Ljava/util/concurrent/locks/ReentrantLock;", "Ljava/util/concurrent/locks/Condition;"});
    uint32_t w_run = b.AddMethod("Lcom/example/CondWorker;", "run", "V", {});

    auto &wcls = b.AddClass("Lcom/example/CondWorker;");
    wcls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    wcls.instance_fields.push_back({f_lk, ACC_PUBLIC});
    wcls.instance_fields.push_back({f_cond, ACC_PUBLIC});
    wcls.instance_fields.push_back({f_done, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 3;
        m.ins_size = 3;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT_OBJECT, 1, 0, static_cast<uint16_t>(f_lk));
        Emit22c(m.insns, OP_IPUT_OBJECT, 2, 0, static_cast<uint16_t>(f_cond));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = wcls.class_idx;
        m.method_idx = w_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 4; // p0(v3)=this
        m.ins_size = 1;
        Emit22c(m.insns, OP_IGET_OBJECT, 0, 3, static_cast<uint16_t>(f_lk));
        Emit22c(m.insns, OP_IGET_OBJECT, 1, 3, static_cast<uint16_t>(f_cond));
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_lock), {0});
        Emit21s(m.insns, OP_CONST_16, 2, 1);
        Emit22c(m.insns, OP_IPUT, 2, 3, static_cast<uint16_t>(f_done));
        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_c_sigAll), {1});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_unlock), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        wcls.virtual_methods.push_back(std::move(m));
    }

    auto &rcls = b.AddClass("Lcom/example/CondRunner;");
    uint32_t run_m = b.AddMethod("Lcom/example/CondRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = rcls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 6;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Ljava/util/concurrent/locks/ReentrantLock;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_rl_init), {0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_newCond), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 1));

        Emit21c(m.insns, OP_NEW_INSTANCE, 2,
                static_cast<uint16_t>(b.AddType("Lcom/example/CondWorker;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {2, 0, 1});
        Emit21c(m.insns, OP_NEW_INSTANCE, 3,
                static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {3, 2});

        // Reentrant lock * 2, holdCount == 2, await preserves holdCount == 2 after signal!
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_lock), {0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_lock), {0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {3});

        uint16_t loop = static_cast<uint16_t>(m.insns.size());
        Emit22c(m.insns, OP_IGET, 4, 2, static_cast<uint16_t>(f_done));
        size_t done_j = m.insns.size();
        Emit22t(m.insns, OP_IF_NEZ, 4, 0, 0);
        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_c_await), {1});
        int8_t back = static_cast<int8_t>(static_cast<ptrdiff_t>(loop) -
                                          static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(back) << 8)));
        m.insns[done_j + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - done_j));

        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_holds), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT, 5)); // expected 2
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_unlock), {0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_rl_unlock), {0});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {3});
        m.insns.push_back(Encode11x(OP_RETURN, 5));
        rcls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/CondRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 2);
}

TEST_F(JavaThreadTest, ConcurrentHashMapAndHandlerPostFromWorkerThread)
{
    RuntimeDexBuilder b;
    uint32_t obj_init = b.AddMethod("Ljava/lang/Object;", "<init>", "V", {});
    uint32_t thr_init = b.AddMethod("Ljava/lang/Thread;", "<init>", "V", {"Ljava/lang/Runnable;"});
    uint32_t thr_start = b.AddMethod("Ljava/lang/Thread;", "start", "V", {});
    uint32_t thr_join = b.AddMethod("Ljava/lang/Thread;", "join", "V", {});

    uint32_t m_chm_init =
        b.AddMethod("Ljava/util/concurrent/ConcurrentHashMap;", "<init>", "V", {});
    uint32_t m_map_put = b.AddMethod("Ljava/util/Map;", "put", "Ljava/lang/Object;",
                                     {"Ljava/lang/Object;", "Ljava/lang/Object;"});
    uint32_t m_map_size = b.AddMethod("Ljava/util/Map;", "size", "I", {});
    uint32_t m_int_valOf =
        b.AddMethod("Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"});
    uint32_t m_hdl_init = b.AddMethod("Landroid/os/Handler;", "<init>", "V", {});
    uint32_t m_hdl_post =
        b.AddMethod("Landroid/os/Handler;", "post", "Z", {"Ljava/lang/Runnable;"});

    uint32_t f_map =
        b.AddField("Lcom/example/ChmRunner;", "sMap", "Ljava/util/concurrent/ConcurrentHashMap;");
    uint32_t f_posted = b.AddField("Lcom/example/ChmRunner;", "sPosted", "I");
    uint32_t f_hdl = b.AddField("Lcom/example/ChmRunner;", "sHdl", "Landroid/os/Handler;");

    // PostedRunnable sets ChmRunner.sPosted = 55
    auto &pr_cls = b.AddClass("Lcom/example/PostedRunnable;");
    pr_cls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    uint32_t pr_init = b.AddMethod("Lcom/example/PostedRunnable;", "<init>", "V", {});
    uint32_t pr_run = b.AddMethod("Lcom/example/PostedRunnable;", "run", "V", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = pr_cls.class_idx;
        m.method_idx = pr_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 1;
        m.ins_size = 1;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        pr_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = pr_cls.class_idx;
        m.method_idx = pr_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 2;
        m.ins_size = 1;
        Emit21s(m.insns, OP_CONST_16, 0, 55);
        Emit21c(m.insns, OP_SPUT, 0, static_cast<uint16_t>(f_posted));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        pr_cls.virtual_methods.push_back(std::move(m));
    }

    // Worker inserts keys base..base+9 into sMap and calls sHdl.post(new PostedRunnable())
    auto &w_cls = b.AddClass("Lcom/example/MapWorker;");
    w_cls.interfaces.push_back(b.AddType("Ljava/lang/Runnable;"));
    uint32_t f_base = b.AddField("Lcom/example/MapWorker;", "base", "I");
    uint32_t w_init = b.AddMethod("Lcom/example/MapWorker;", "<init>", "V", {"I"});
    uint32_t w_run = b.AddMethod("Lcom/example/MapWorker;", "run", "V", {});
    w_cls.instance_fields.push_back({f_base, ACC_PUBLIC});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = w_cls.class_idx;
        m.method_idx = w_init;
        m.access_flags = ACC_PUBLIC | ACC_CONSTRUCTOR;
        m.registers_size = 2;
        m.ins_size = 2;
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(obj_init), {0});
        Emit22c(m.insns, OP_IPUT, 1, 0, static_cast<uint16_t>(f_base));
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        w_cls.direct_methods.push_back(std::move(m));
    }
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = w_cls.class_idx;
        m.method_idx = w_run;
        m.access_flags = ACC_PUBLIC;
        m.registers_size = 6; // p0(v5)=this
        m.ins_size = 1;
        Emit21c(m.insns, OP_SGET_OBJECT, 0, static_cast<uint16_t>(f_map));
        Emit22c(m.insns, OP_IGET, 1, 5, static_cast<uint16_t>(f_base));
        Emit21s(m.insns, OP_CONST_16, 2, 10);
        Emit23x(m.insns, OP_ADD_INT, 2, 1, 2);
        uint16_t loop = static_cast<uint16_t>(m.insns.size());
        size_t exit_j = m.insns.size();
        Emit22t(m.insns, OP_IF_GE, 1, 2, 0);
        Emit35c(m.insns, OP_INVOKE_STATIC, static_cast<uint16_t>(m_int_valOf), {1});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT_OBJECT, 3));
        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_put), {0, 3, 3});
        m.insns.push_back(Encode11n(OP_CONST_4, 4, 1));
        m.insns.push_back(Encode12x(OP_ADD_INT_2ADDR, 1, 4));
        int8_t back = static_cast<int8_t>(static_cast<ptrdiff_t>(loop) -
                                          static_cast<ptrdiff_t>(m.insns.size()));
        m.insns.push_back(static_cast<uint16_t>(OP_GOTO | (static_cast<uint8_t>(back) << 8)));
        m.insns[exit_j + 1] = static_cast<uint16_t>(static_cast<int16_t>(m.insns.size() - exit_j));

        Emit21c(m.insns, OP_SGET_OBJECT, 0, static_cast<uint16_t>(f_hdl));
        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Lcom/example/PostedRunnable;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(pr_init), {1});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(m_hdl_post), {0, 1});
        m.insns.push_back(Encode10x(OP_RETURN_VOID));
        w_cls.virtual_methods.push_back(std::move(m));
    }

    auto &r_cls = b.AddClass("Lcom/example/ChmRunner;");
    r_cls.static_fields.push_back({f_map, ACC_PUBLIC | ACC_STATIC});
    r_cls.static_fields.push_back({f_posted, ACC_PUBLIC | ACC_STATIC});
    r_cls.static_fields.push_back({f_hdl, ACC_PUBLIC | ACC_STATIC});
    uint32_t run_m = b.AddMethod("Lcom/example/ChmRunner;", "run", "I", {});
    {
        RuntimeDexBuilder::MethodSpec m{};
        m.class_idx = r_cls.class_idx;
        m.method_idx = run_m;
        m.access_flags = ACC_PUBLIC | ACC_STATIC;
        m.registers_size = 5;
        Emit21c(m.insns, OP_NEW_INSTANCE, 0,
                static_cast<uint16_t>(b.AddType("Ljava/util/concurrent/ConcurrentHashMap;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_chm_init), {0});
        Emit21c(m.insns, OP_SPUT_OBJECT, 0, static_cast<uint16_t>(f_map));

        Emit21c(m.insns, OP_NEW_INSTANCE, 1,
                static_cast<uint16_t>(b.AddType("Landroid/os/Handler;")));
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(m_hdl_init), {1});
        Emit21c(m.insns, OP_SPUT_OBJECT, 1, static_cast<uint16_t>(f_hdl));

        // Launch 2 workers with non-overlapping keys 0..9 and 10..19 -> map size = 20
        uint16_t w_t = static_cast<uint16_t>(b.AddType("Lcom/example/MapWorker;"));
        uint16_t thr_t = static_cast<uint16_t>(b.AddType("Ljava/lang/Thread;"));
        Emit21c(m.insns, OP_NEW_INSTANCE, 1, w_t);
        Emit21s(m.insns, OP_CONST_16, 4, 0);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {1, 4});
        Emit21c(m.insns, OP_NEW_INSTANCE, 2, thr_t);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {2, 1});

        Emit21c(m.insns, OP_NEW_INSTANCE, 1, w_t);
        Emit21s(m.insns, OP_CONST_16, 4, 10);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(w_init), {1, 4});
        Emit21c(m.insns, OP_NEW_INSTANCE, 3, thr_t);
        Emit35c(m.insns, OP_INVOKE_DIRECT, static_cast<uint16_t>(thr_init), {3, 1});

        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {2});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_start), {3});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {2});
        Emit35c(m.insns, OP_INVOKE_VIRTUAL, static_cast<uint16_t>(thr_join), {3});

        Emit35c(m.insns, OP_INVOKE_INTERFACE, static_cast<uint16_t>(m_map_size), {0});
        m.insns.push_back(Encode11x(OP_MOVE_RESULT, 0));
        m.insns.push_back(Encode11x(OP_RETURN, 0));
        r_cls.direct_methods.push_back(std::move(m));
    }

    auto raw = b.Build();
    ASSERT_TRUE(DexVm::Instance().LoadDex(raw));
    auto resolved = DexVm::Instance().ResolveMethod("Lcom/example/ChmRunner;", "run", "()I");
    ASSERT_TRUE(resolved.has_value());
    DexValue res = DexVm::Instance().ExecuteMethod(resolved->dex, resolved->method, {});
    EXPECT_EQ(static_cast<int32_t>(res.raw), 20);
    DexVm::Instance().DrainMainLooper(16);
    EXPECT_EQ(static_cast<int32_t>(
                  DexVm::Instance().GetStaticField("Lcom/example/ChmRunner;", "sPosted")),
              55);
}
