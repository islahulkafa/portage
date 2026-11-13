#include "binder_ndk.hpp"

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class DeathNotificationQueue {
  public:
    static DeathNotificationQueue &Instance()
    {
        static DeathNotificationQueue instance;
        return instance;
    }

    void Enqueue(AIBinder_DeathRecipient_onBinderDied onDied, void *cookie)
    {
        if (!onDied) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back({onDied, cookie});
        }
        cv_.notify_one();
    }

    ~DeathNotificationQueue()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

  private:
    struct Task {
        AIBinder_DeathRecipient_onBinderDied onDied;
        void *cookie;
    };

    DeathNotificationQueue() : worker_([this] { WorkerLoop(); }) {}

    void WorkerLoop()
    {
        while (true) {
            std::vector<Task> local_tasks;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    break;
                }
                local_tasks.swap(tasks_);
            }
            for (const auto &t : local_tasks) {
                if (t.onDied) {
                    t.onDied(t.cookie);
                }
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
    std::vector<Task> tasks_;
    std::thread worker_;
};

} // namespace

extern "C" {

__attribute__((sysv_abi)) AIBinder_Class *
AIBinder_Class_define(const char *interfaceDescriptor, AIBinder_Class_onCreate onCreate,
                      AIBinder_Class_onDestroy onDestroy, AIBinder_Class_onTransact onTransact)
{
    if (!interfaceDescriptor || !onTransact) {
        return nullptr;
    }
    auto *cl = new AIBinder_Class();
    cl->descriptor = interfaceDescriptor;
    cl->onCreate = onCreate;
    cl->onDestroy = onDestroy;
    cl->onTransact = onTransact;
    return cl;
}

__attribute__((sysv_abi)) AIBinder *AIBinder_new(const AIBinder_Class *clazz, void *args)
{
    if (!clazz) {
        return nullptr;
    }
    auto *b = new AIBinder();
    b->ref_count = 1;
    b->clazz = clazz;
    b->descriptor = clazz->descriptor;
    if (clazz->onCreate) {
        b->user_data = clazz->onCreate(args);
    } else {
        b->user_data = args;
    }
    return b;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_transact(AIBinder *binder,
                                                            transaction_code_t code,
                                                            const AParcel *in, AParcel *out,
                                                            binder_flags_t flags)
{
    (void)flags;
    if (!binder || !in) {
        return STATUS_BAD_VALUE;
    }

    if (!binder->clazz || !binder->clazz->onTransact) {
        if (code == 0x5f504e47) {
            return STATUS_OK;
        }
        return STATUS_UNKNOWN_TRANSACTION;
    }

    // Rewind in-parcel position to beginning for reading
    in->data_pos = 0;
    if (out) {
        out->buffer.clear();
        out->data_pos = 0;
    }

    binder_status_t status = binder->clazz->onTransact(binder, code, in, out);
    if (code == 0x5f504e47 && status == STATUS_UNKNOWN_TRANSACTION) {
        status = STATUS_OK;
    }

    // Rewind out-parcel so caller can read response from the start
    if (out) {
        out->data_pos = 0;
    }
    return status;
}

__attribute__((sysv_abi)) void AIBinder_incStrong(AIBinder *binder)
{
    if (binder) {
        binder->ref_count.fetch_add(1, std::memory_order_relaxed);
    }
}

__attribute__((sysv_abi)) void AIBinder_decStrong(AIBinder *binder)
{
    if (!binder) {
        return;
    }
    uint32_t prev = binder->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 1) {
        {
            std::lock_guard<std::mutex> lock(binder->death_mutex);
            for (const auto &link : binder->death_links) {
                if (link.recipient && link.recipient->onDied) {
                    DeathNotificationQueue::Instance().Enqueue(link.recipient->onDied, link.cookie);
                }
            }
            binder->death_links.clear();
        }

        if (binder->extension) {
            AIBinder_decStrong(binder->extension);
            binder->extension = nullptr;
        }

        if (binder->clazz && binder->clazz->onDestroy && binder->user_data) {
            binder->clazz->onDestroy(binder->user_data);
            binder->user_data = nullptr;
        }
        delete binder;
    }
}

__attribute__((sysv_abi)) int32_t AIBinder_getCallingPid(void) { return 1000; }

__attribute__((sysv_abi)) int32_t AIBinder_getCallingUid(void) { return 10000; }

__attribute__((sysv_abi)) bool AIBinder_isAlive(const AIBinder *binder)
{
    return binder != nullptr && binder->ref_count.load(std::memory_order_relaxed) > 0;
}

__attribute__((sysv_abi)) bool AIBinder_isRemote(const AIBinder *binder)
{
    return binder && binder->is_remote;
}

__attribute__((sysv_abi)) void *AIBinder_getUserData(AIBinder *binder)
{
    return binder ? binder->user_data : nullptr;
}

__attribute__((sysv_abi)) const char *AIBinder_Class_getDescriptor(const AIBinder_Class *clazz)
{
    return clazz ? clazz->descriptor.c_str() : "";
}

__attribute__((sysv_abi)) bool AIBinder_associateClass(AIBinder *binder,
                                                       const AIBinder_Class *clazz)
{
    if (!binder || !clazz) {
        return false;
    }
    if (!binder->clazz) {
        binder->clazz = clazz;
        binder->descriptor = clazz->descriptor;
        return true;
    }
    return binder->clazz == clazz || binder->descriptor == clazz->descriptor;
}

__attribute__((sysv_abi)) const AIBinder_Class *AIBinder_getClass(AIBinder *binder)
{
    return binder ? binder->clazz : nullptr;
}

__attribute__((sysv_abi)) AIBinder_DeathRecipient *
AIBinder_DeathRecipient_new(AIBinder_DeathRecipient_onBinderDied onDied)
{
    if (!onDied) {
        return nullptr;
    }
    auto *rec = new AIBinder_DeathRecipient();
    rec->onDied = onDied;
    return rec;
}

__attribute__((sysv_abi)) void AIBinder_DeathRecipient_delete(AIBinder_DeathRecipient *recipient)
{
    delete recipient;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_linkToDeath(AIBinder *binder,
                                                               AIBinder_DeathRecipient *recipient,
                                                               void *cookie)
{
    if (!binder || !recipient || !recipient->onDied) {
        return STATUS_UNEXPECTED_NULL;
    }
    if (!AIBinder_isAlive(binder)) {
        return STATUS_DEAD_OBJECT;
    }
    std::lock_guard<std::mutex> lock(binder->death_mutex);
    for (const auto &link : binder->death_links) {
        if (link.recipient == recipient && link.cookie == cookie) {
            return STATUS_ALREADY_EXISTS;
        }
    }
    binder->death_links.push_back({recipient, cookie});
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_unlinkToDeath(AIBinder *binder,
                                                                 AIBinder_DeathRecipient *recipient,
                                                                 void *cookie)
{
    if (!binder || !recipient) {
        return STATUS_UNEXPECTED_NULL;
    }
    std::lock_guard<std::mutex> lock(binder->death_mutex);
    for (auto it = binder->death_links.begin(); it != binder->death_links.end(); ++it) {
        if (it->recipient == recipient && it->cookie == cookie) {
            binder->death_links.erase(it);
            return STATUS_OK;
        }
    }
    return STATUS_NAME_NOT_FOUND;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_ping(AIBinder *binder)
{
    if (!binder) {
        return STATUS_UNEXPECTED_NULL;
    }
    if (!AIBinder_isAlive(binder)) {
        return STATUS_DEAD_OBJECT;
    }
    AParcel in;
    AParcel out;
    binder_status_t s = AIBinder_transact(binder, 0x5f504e47, &in, &out, 0);
    if (s == STATUS_UNKNOWN_TRANSACTION || s == STATUS_OK) {
        return STATUS_OK;
    }
    return s;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_dump(AIBinder *binder, int fd, const char **args,
                                                        uint32_t numArgs)
{
    if (!binder) {
        return STATUS_UNEXPECTED_NULL;
    }
    if (fd < 0) {
        return STATUS_BAD_VALUE;
    }
    (void)args;
    (void)numArgs;
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_getExtension(AIBinder *binder,
                                                                AIBinder **outExtension)
{
    if (!binder || !outExtension) {
        return STATUS_UNEXPECTED_NULL;
    }
    *outExtension = binder->extension;
    if (binder->extension) {
        AIBinder_incStrong(binder->extension);
    }
    return STATUS_OK;
}

__attribute__((sysv_abi)) binder_status_t AIBinder_setExtension(AIBinder *binder,
                                                                AIBinder *extension)
{
    if (!binder) {
        return STATUS_UNEXPECTED_NULL;
    }
    if (binder->extension) {
        AIBinder_decStrong(binder->extension);
    }
    binder->extension = extension;
    if (extension) {
        AIBinder_incStrong(extension);
    }
    return STATUS_OK;
}

} // extern "C"
