#include "thread/thread_manager.hpp"
#include "arch/guest_context.hpp"
#include "elf/link_map.hpp"
#include "process/namespace_manager.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "sync/futex.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <synchapi.h>

namespace portage::thread {

namespace {

struct ThreadStartParams {
    int tid{0};
    uint64_t flags{0};
    uintptr_t child_stack{0};
    uintptr_t child_tid_ptr{0};
    uintptr_t tls{0};
    uintptr_t child_rip{0};
};

DWORD WINAPI WorkerThreadThunk(LPVOID lpParam)
{
    auto *params = static_cast<ThreadStartParams *>(lpParam);
    int tid = params->tid;
    uint64_t flags = params->flags;
    uintptr_t child_stack = params->child_stack;
    uintptr_t child_tid_ptr = params->child_tid_ptr;
    uintptr_t tls = params->tls;
    uintptr_t child_rip = params->child_rip;
    delete params;

    if (tls != 0) {
        arch::SetActiveTcb(tls);
    } else {
        ThreadManager::Instance().EnsureCurrentThreadTcb();
    }

    if ((flags & LINUX_CLONE_CHILD_SETTID) && child_tid_ptr != 0) {
        *reinterpret_cast<int *>(child_tid_ptr) = tid;
    }

    if ((flags & LINUX_CLONE_CHILD_CLEARTID) && child_tid_ptr != 0) {
        ThreadManager::Instance().SetClearChildTid(tid, child_tid_ptr);
    }

    uint64_t host_stack_base = __readgsqword(0x08);
    uint64_t host_stack_limit = __readgsqword(0x10);
    uint64_t host_dealloc_stack = __readgsqword(0x1478);

    auto *host_ctx = arch::GetThreadHostContext();
    host_ctx->stack_base = host_stack_base;
    host_ctx->stack_limit = host_stack_limit;
    host_ctx->deallocation_stack = host_dealloc_stack;
    arch::SetThreadHostContextActive(true);

    uintptr_t g_stack_base = child_stack;
    uintptr_t g_stack_limit =
        (child_stack > 8 * 1024 * 1024) ? (child_stack - 8 * 1024 * 1024) : 0x1000;

    int exit_code = 0;
    if (child_rip != 0) {
        __writegsqword(0x08, g_stack_base);
        __writegsqword(0x10, g_stack_limit);
        __writegsqword(0x1478, g_stack_limit);

        exit_code = arch::portage_save_and_enter(reinterpret_cast<void *>(child_rip),
                                                 reinterpret_cast<void *>(child_stack), host_ctx);

        __writegsqword(0x08, host_stack_base);
        __writegsqword(0x10, host_stack_limit);
        __writegsqword(0x1478, host_dealloc_stack);
    }

    ThreadManager::Instance().OnThreadExit(tid, exit_code);
    return static_cast<DWORD>(exit_code);
}

} // namespace

ThreadManager &ThreadManager::Instance() noexcept
{
    static ThreadManager instance;
    return instance;
}

uintptr_t ThreadManager::EnsureCurrentThreadTcb() noexcept
{
    arch::EnsureVectoredHandlerInstalled();
    uintptr_t current = arch::GetActiveTcb();
    if (current != 0) {
        return current;
    }

    static thread_local BionicTcb tl_tcb;
    tl_tcb.self = &tl_tcb;
    tl_tcb.stack_guard = 0x595e9fbd94fda766ULL;
    uintptr_t tcb_addr = reinterpret_cast<uintptr_t>(&tl_tcb);

    arch::SetActiveTcb(tcb_addr);

    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        auto tit = m_threads.find(it->second);
        if (tit != m_threads.end() && tit->second) {
            tit->second->tcb = tcb_addr;
        }
    }

    return tcb_addr;
}

int ThreadManager::RegisterMainThread() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DWORD win32_tid = GetCurrentThreadId();

    auto thread = std::make_shared<GuestThread>();
    thread->tid = static_cast<int>(GetCurrentProcessId());
    thread->win32_handle = GetCurrentThread();
    thread->win32_thread_id = win32_tid;
    uint64_t sb = __readgsqword(0x08);
    uint64_t sl = __readgsqword(0x10);
    thread->stack_limit = sl;
    thread->stack_size = (sb > sl) ? static_cast<size_t>(sb - sl) : (8 * 1024 * 1024);

    static thread_local BionicTcb tl_tcb;
    tl_tcb.self = &tl_tcb;
    tl_tcb.stack_guard = 0x595e9fbd94fda766ULL;
    uintptr_t tcb_addr = reinterpret_cast<uintptr_t>(&tl_tcb);
    thread->tcb = tcb_addr;
    arch::EnsureVectoredHandlerInstalled();
    arch::SetActiveTcb(tcb_addr);

    m_threads[thread->tid] = thread;
    m_win32_to_tid[win32_tid] = thread->tid;

    thread->tgid = thread->tid;
    thread->ppid = 1;
    auto &ns_mgr = process::NamespaceManager::Instance();
    thread->pid_ns = ns_mgr.GetRootPidNamespace();
    thread->mnt_ns = ns_mgr.GetRootMountNamespace();
    thread->uts_ns = ns_mgr.GetRootUtsNamespace();
    if (thread->pid_ns) {
        thread->pid_ns->AllocatePid(thread->tgid);
    }

    return thread->tid;
}

int ThreadManager::CreateGuestThread(uint64_t flags, uintptr_t child_stack,
                                     uintptr_t parent_tid_ptr, uintptr_t child_tid_ptr,
                                     uintptr_t tls, uintptr_t child_rip) noexcept
{
    int assigned_tid = m_next_tid.fetch_add(1);
    auto parent = GetCurrentGuestThread();
    auto &ns_mgr = process::NamespaceManager::Instance();

    if ((flags & LINUX_CLONE_PARENT_SETTID) && parent_tid_ptr != 0) {
        *reinterpret_cast<int *>(parent_tid_ptr) = assigned_tid;
    }

    auto thread = std::make_shared<GuestThread>();
    thread->tid = assigned_tid;
    thread->child_stack = child_stack;
    thread->stack_size = 8 * 1024 * 1024;
    thread->stack_limit =
        (child_stack > thread->stack_size) ? (child_stack - thread->stack_size) : 0x1000;
    thread->tcb = tls;

    if (flags & LINUX_CLONE_THREAD) {
        thread->tgid = parent ? parent->tgid : assigned_tid;
        thread->ppid = parent ? parent->ppid : 1;
        thread->pid_ns = (parent && parent->pid_ns) ? parent->pid_ns : ns_mgr.GetRootPidNamespace();
        thread->mnt_ns =
            (parent && parent->mnt_ns) ? parent->mnt_ns : ns_mgr.GetRootMountNamespace();
        thread->uts_ns = (parent && parent->uts_ns) ? parent->uts_ns : ns_mgr.GetRootUtsNamespace();
    } else {
        thread->tgid = assigned_tid;
        thread->ppid = parent ? parent->tgid : 1;

        if (flags & process::LINUX_CLONE_NEWPID) {
            auto p_ns = (parent && parent->pid_ns) ? parent->pid_ns : ns_mgr.GetRootPidNamespace();
            thread->pid_ns = ns_mgr.CreateChildPidNamespace(p_ns);
        } else if (parent && parent->next_child_pid_ns) {
            thread->pid_ns = parent->next_child_pid_ns;
        } else {
            thread->pid_ns =
                (parent && parent->pid_ns) ? parent->pid_ns : ns_mgr.GetRootPidNamespace();
        }
        if (thread->pid_ns) {
            thread->pid_ns->AllocatePid(thread->tgid);
        }

        if (flags & process::LINUX_CLONE_NEWNS) {
            auto p_mnt =
                (parent && parent->mnt_ns) ? parent->mnt_ns : ns_mgr.GetRootMountNamespace();
            thread->mnt_ns = ns_mgr.CloneMountNamespace(*p_mnt);
        } else {
            thread->mnt_ns =
                (parent && parent->mnt_ns) ? parent->mnt_ns : ns_mgr.GetRootMountNamespace();
        }

        if (flags & process::LINUX_CLONE_NEWUTS) {
            auto p_uts = (parent && parent->uts_ns) ? parent->uts_ns : ns_mgr.GetRootUtsNamespace();
            thread->uts_ns = ns_mgr.CloneUtsNamespace(*p_uts);
        } else {
            thread->uts_ns =
                (parent && parent->uts_ns) ? parent->uts_ns : ns_mgr.GetRootUtsNamespace();
        }
    }

    auto *params = new ThreadStartParams{.tid = assigned_tid,
                                         .flags = flags,
                                         .child_stack = child_stack,
                                         .child_tid_ptr = child_tid_ptr,
                                         .tls = tls,
                                         .child_rip = child_rip};

    DWORD win32_id = 0;
    HANDLE hThread = CreateThread(nullptr, 0, WorkerThreadThunk, params, 0, &win32_id);

    if (!hThread) {
        delete params;
        return -11; // -EAGAIN
    }

    thread->win32_handle = hThread;
    thread->win32_thread_id = win32_id;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threads[assigned_tid] = thread;
        m_win32_to_tid[win32_id] = assigned_tid;
    }

    return assigned_tid;
}

void ThreadManager::SetClearChildTid(int tid, uintptr_t ctid_ptr) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_threads.find(tid);
    if (it != m_threads.end()) {
        it->second->clear_child_tid = ctid_ptr;
    }
}

int ThreadManager::RegisterThreadAtExit(__attribute__((sysv_abi)) void (*dtor)(void *), void *obj,
                                        void *dso_handle) noexcept
{
    if (dtor == nullptr) {
        return -1;
    }

    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    std::shared_ptr<GuestThread> thread;
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        auto tit = m_threads.find(it->second);
        if (tit != m_threads.end()) {
            thread = tit->second;
        }
    }

    if (!thread) {
        int synthetic_tid = static_cast<int>(win32_tid);
        thread = std::make_shared<GuestThread>();
        thread->tid = synthetic_tid;
        thread->win32_handle = GetCurrentThread();
        thread->win32_thread_id = win32_tid;
        m_threads[synthetic_tid] = thread;
        m_win32_to_tid[win32_tid] = synthetic_tid;
    }

    thread->thread_atexit_entries.push_back(ThreadAtExitEntry{dtor, obj, dso_handle});
    return 0;
}

void ThreadManager::RunCurrentThreadAtExit() noexcept
{
    int tid = GetCurrentTid();
    while (true) {
        ThreadAtExitEntry entry{};
        bool has_entry = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_threads.find(tid);
            if (it != m_threads.end() && !it->second->thread_atexit_entries.empty()) {
                entry = it->second->thread_atexit_entries.back();
                it->second->thread_atexit_entries.pop_back();
                has_entry = true;
            }
        }
        if (!has_entry) {
            break;
        }
        if (entry.dtor != nullptr) {
            entry.dtor(entry.obj);
        }
    }
}

void ThreadManager::OnThreadExit(int tid, int exit_code) noexcept
{
    while (true) {
        ThreadAtExitEntry entry{};
        bool has_entry = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_threads.find(tid);
            if (it != m_threads.end() && !it->second->thread_atexit_entries.empty()) {
                entry = it->second->thread_atexit_entries.back();
                it->second->thread_atexit_entries.pop_back();
                has_entry = true;
            }
        }
        if (!has_entry) {
            break;
        }
        if (entry.dtor != nullptr) {
            entry.dtor(entry.obj);
        }
    }

    RunCurrentThreadAtExit();
    elf::LinkMap::Instance().CleanupCurrentThreadTls();

    uintptr_t clear_tid_ptr = 0;
    HANDLE hThread = INVALID_HANDLE_VALUE;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_threads.find(tid);
        if (it != m_threads.end()) {
            it->second->is_exited = true;
            it->second->exit_code = exit_code;
            clear_tid_ptr = it->second->clear_child_tid;
            it->second->clear_child_tid = 0;
            hThread = it->second->win32_handle;
            it->second->win32_handle = INVALID_HANDLE_VALUE;
        }
    }

    if (clear_tid_ptr != 0) {
        *reinterpret_cast<int *>(clear_tid_ptr) = 0;
        syscall::SyscallDispatcher::GetFutexManager().Futex(
            reinterpret_cast<uint32_t *>(clear_tid_ptr), sync::LINUX_FUTEX_WAKE, 1, nullptr,
            nullptr, 0);
    }

    if (hThread != nullptr && hThread != INVALID_HANDLE_VALUE && hThread != GetCurrentThread()) {
        CloseHandle(hThread);
    }
}

std::shared_ptr<GuestThread> ThreadManager::GetThread(int tid) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_threads.find(tid);
    if (it != m_threads.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<GuestThread> ThreadManager::GetCurrentGuestThread() noexcept
{
    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        auto t_it = m_threads.find(it->second);
        if (t_it != m_threads.end()) {
            return t_it->second;
        }
    }
    return nullptr;
}

std::vector<int> ThreadManager::GetActiveTids() const noexcept
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(m_mutex));
    std::vector<int> tids;
    tids.reserve(m_threads.size());
    for (const auto &[tid, th] : m_threads) {
        if (th && !th->is_exited) {
            tids.push_back(tid);
        }
    }
    std::sort(tids.begin(), tids.end());
    return tids;
}

int ThreadManager::GetCurrentTid() noexcept
{
    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        return it->second;
    }
    return static_cast<int>(win32_tid);
}

void ThreadManager::SetCurrentThreadTcb(uintptr_t tcb) noexcept
{
    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        auto tit = m_threads.find(it->second);
        if (tit != m_threads.end() && tit->second) {
            tit->second->tcb = tcb;
        }
    }
}

void ThreadManager::SetCurrentThreadStack(uintptr_t stack_limit, size_t stack_size) noexcept
{
    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_win32_to_tid.find(win32_tid);
    if (it != m_win32_to_tid.end()) {
        auto tit = m_threads.find(it->second);
        if (tit != m_threads.end() && tit->second) {
            tit->second->stack_limit = stack_limit;
            tit->second->stack_size = stack_size;
        }
    }
}

std::shared_ptr<GuestThread> ThreadManager::ResolvePthreadHandle(uintptr_t handle) noexcept
{
    DWORD win32_tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);

    auto get_or_register_current = [&]() -> std::shared_ptr<GuestThread> {
        auto it = m_win32_to_tid.find(win32_tid);
        if (it != m_win32_to_tid.end()) {
            auto tit = m_threads.find(it->second);
            if (tit != m_threads.end()) {
                return tit->second;
            }
        }
        int synthetic_tid = static_cast<int>(win32_tid);
        auto thread = std::make_shared<GuestThread>();
        thread->tid = synthetic_tid;
        thread->win32_handle = GetCurrentThread();
        thread->win32_thread_id = win32_tid;
        uint64_t sb = __readgsqword(0x08);
        uint64_t sl = __readgsqword(0x10);
        thread->stack_limit = sl;
        thread->stack_size = (sb > sl) ? static_cast<size_t>(sb - sl) : (8 * 1024 * 1024);
        m_threads[synthetic_tid] = thread;
        m_win32_to_tid[win32_tid] = synthetic_tid;
        return thread;
    };

    if (handle == 0) {
        return get_or_register_current();
    }

    for (const auto &[tid, th] : m_threads) {
        if (th &&
            ((th->tcb != 0 && th->tcb == handle) || static_cast<uintptr_t>(th->tid) == handle)) {
            return th;
        }
    }

    uintptr_t active_tcb = arch::GetActiveTcb();
    if (active_tcb != 0 && handle == active_tcb) {
        return get_or_register_current();
    }
    if (handle == static_cast<uintptr_t>(win32_tid)) {
        return get_or_register_current();
    }

    return nullptr;
}

uintptr_t ThreadManager::GetCurrentPthreadHandle() noexcept
{
    uintptr_t tcb = arch::GetActiveTcb();
    if (tcb != 0) {
        return tcb;
    }
    auto cur = GetCurrentGuestThread();
    if (cur && cur->tcb != 0) {
        return cur->tcb;
    }
    return static_cast<uintptr_t>(GetCurrentTid());
}

int ThreadManager::SetThreadName(uintptr_t pthread_handle, std::string_view name) noexcept
{
    constexpr size_t TASK_COMM_LEN = 16;
    if (name.size() >= TASK_COMM_LEN) {
        return 34; // ERANGE
    }
    auto thread = ResolvePthreadHandle(pthread_handle);
    if (!thread) {
        return 3; // ESRCH
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    thread->name.assign(name.data(), name.size());
    return 0;
}

int ThreadManager::GetThreadName(uintptr_t pthread_handle, char *buf, size_t len) noexcept
{
    if (buf == nullptr || len == 0) {
        return 22; // EINVAL
    }
    auto thread = ResolvePthreadHandle(pthread_handle);
    if (!thread) {
        return 3; // ESRCH
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (thread->name.size() + 1 > len) {
        return 34; // ERANGE
    }
    std::memcpy(buf, thread->name.c_str(), thread->name.size() + 1);
    return 0;
}

bool ThreadManager::GetThreadStack(uintptr_t pthread_handle, void **stack_addr,
                                   size_t *stack_size) noexcept
{
    auto thread = ResolvePthreadHandle(pthread_handle);
    if (!thread) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    uintptr_t sl = thread->stack_limit;
    size_t sz = thread->stack_size;
    if (sl == 0) {
        uint64_t teb_base = __readgsqword(0x08);
        uint64_t teb_limit = __readgsqword(0x10);
        sl = teb_limit;
        sz = (teb_base > teb_limit) ? static_cast<size_t>(teb_base - teb_limit) : sz;
    }
    if (stack_addr) {
        *stack_addr = reinterpret_cast<void *>(sl);
    }
    if (stack_size) {
        *stack_size = sz;
    }
    return true;
}

} // namespace portage::thread
