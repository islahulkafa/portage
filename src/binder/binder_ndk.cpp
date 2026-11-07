#include "binder_ndk.hpp"

#include <cstring>

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
        return STATUS_UNKNOWN_TRANSACTION;
    }

    // Rewind in-parcel position to beginning for reading
    in->data_pos = 0;
    if (out) {
        out->buffer.clear();
        out->data_pos = 0;
    }

    binder_status_t status = binder->clazz->onTransact(binder, code, in, out);

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
        if (binder->clazz && binder->clazz->onDestroy && binder->user_data) {
            binder->clazz->onDestroy(binder->user_data);
            binder->user_data = nullptr;
        }
        delete binder;
    }
}

__attribute__((sysv_abi)) int32_t AIBinder_getCallingPid(void)
{
    return 1000;
}

__attribute__((sysv_abi)) int32_t AIBinder_getCallingUid(void)
{
    return 10000;
}

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

__attribute__((sysv_abi)) const char *
AIBinder_Class_getDescriptor(const AIBinder_Class *clazz)
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

} // extern "C"
