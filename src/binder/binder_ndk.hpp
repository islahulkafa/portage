#pragma once

#include "binder_parcel.hpp"
#include "binder_status.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

typedef uint32_t transaction_code_t;
typedef uint32_t binder_flags_t;

struct AIBinder;
struct AIBinder_Class;
struct AIBinder_DeathRecipient;

typedef void *(*AIBinder_Class_onCreate)(void *args) __attribute__((sysv_abi));
typedef void (*AIBinder_Class_onDestroy)(void *userData) __attribute__((sysv_abi));
typedef binder_status_t (*AIBinder_Class_onTransact)(AIBinder *binder, transaction_code_t code,
                                                     const AParcel *in, AParcel *out)
    __attribute__((sysv_abi));

typedef void (*AIBinder_DeathRecipient_onBinderDied)(void *cookie) __attribute__((sysv_abi));

struct AIBinder_Class {
    std::string descriptor{};
    AIBinder_Class_onCreate onCreate{nullptr};
    AIBinder_Class_onDestroy onDestroy{nullptr};
    AIBinder_Class_onTransact onTransact{nullptr};
};

struct AIBinder_DeathRecipient {
    AIBinder_DeathRecipient_onBinderDied onDied{nullptr};
};

struct DeathLink {
    AIBinder_DeathRecipient *recipient{nullptr};
    void *cookie{nullptr};
};

struct AIBinder {
    std::atomic<uint32_t> ref_count{1};
    const AIBinder_Class *clazz{nullptr};
    void *user_data{nullptr};
    std::string descriptor{};
    bool is_remote{false};
    int32_t handle{0};
    AIBinder *extension{nullptr};
    std::mutex death_mutex{};
    std::vector<DeathLink> death_links{};
};

extern "C" {

__attribute__((sysv_abi)) AIBinder_Class *
AIBinder_Class_define(const char *interfaceDescriptor, AIBinder_Class_onCreate onCreate,
                      AIBinder_Class_onDestroy onDestroy, AIBinder_Class_onTransact onTransact);

__attribute__((sysv_abi)) AIBinder *AIBinder_new(const AIBinder_Class *clazz, void *args);

__attribute__((sysv_abi)) binder_status_t AIBinder_transact(AIBinder *binder,
                                                            transaction_code_t code,
                                                            const AParcel *in, AParcel *out,
                                                            binder_flags_t flags);

__attribute__((sysv_abi)) void AIBinder_incStrong(AIBinder *binder);

__attribute__((sysv_abi)) void AIBinder_decStrong(AIBinder *binder);

__attribute__((sysv_abi)) int32_t AIBinder_getCallingPid(void);

__attribute__((sysv_abi)) int32_t AIBinder_getCallingUid(void);

__attribute__((sysv_abi)) bool AIBinder_isAlive(const AIBinder *binder);

__attribute__((sysv_abi)) bool AIBinder_isRemote(const AIBinder *binder);

__attribute__((sysv_abi)) void *AIBinder_getUserData(AIBinder *binder);

__attribute__((sysv_abi)) const char *AIBinder_Class_getDescriptor(const AIBinder_Class *clazz);

__attribute__((sysv_abi)) bool AIBinder_associateClass(AIBinder *binder,
                                                       const AIBinder_Class *clazz);

__attribute__((sysv_abi)) const AIBinder_Class *AIBinder_getClass(AIBinder *binder);

__attribute__((sysv_abi)) AIBinder_DeathRecipient *
AIBinder_DeathRecipient_new(AIBinder_DeathRecipient_onBinderDied onDied);

__attribute__((sysv_abi)) void AIBinder_DeathRecipient_delete(AIBinder_DeathRecipient *recipient);

__attribute__((sysv_abi)) binder_status_t AIBinder_linkToDeath(AIBinder *binder,
                                                               AIBinder_DeathRecipient *recipient,
                                                               void *cookie);

__attribute__((sysv_abi)) binder_status_t AIBinder_unlinkToDeath(AIBinder *binder,
                                                                 AIBinder_DeathRecipient *recipient,
                                                                 void *cookie);

__attribute__((sysv_abi)) binder_status_t AIBinder_ping(AIBinder *binder);

__attribute__((sysv_abi)) binder_status_t AIBinder_dump(AIBinder *binder, int fd, const char **args,
                                                        uint32_t numArgs);

__attribute__((sysv_abi)) binder_status_t AIBinder_getExtension(AIBinder *binder,
                                                                AIBinder **outExtension);

__attribute__((sysv_abi)) binder_status_t AIBinder_setExtension(AIBinder *binder,
                                                                AIBinder *extension);

} // extern "C"
