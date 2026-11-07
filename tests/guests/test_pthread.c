#include <stddef.h>
#include <stdint.h>

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_CHILD_CLEARTID 0x00200000

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static inline int64_t sys_clone(uint64_t flags, void *child_stack, int *ptid, int *ctid,
                                unsigned long tls)
{
    int64_t ret;
    register int *r10 __asm__("r10") = ctid;
    register unsigned long r8 __asm__("r8") = tls;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(56), "D"(flags), "S"(child_stack), "d"(ptid), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_futex(volatile int *uaddr, int op, int val, const void *timeout)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(202), "D"(uaddr), "S"(op), "d"(val), "r"(timeout), "r"(0), "r"(0)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static inline void sys_write(int fd, const void *buf, size_t count)
{
    __asm__ volatile("syscall" : : "a"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
}

typedef struct {
    volatile int val;
} pthread_mutex_t;

static inline void pthread_mutex_init(pthread_mutex_t *mutex) { mutex->val = 0; }

static inline void pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int c = __sync_val_compare_and_swap(&mutex->val, 0, 1);
    if (c != 0) {
        if (c != 2) {
            c = __sync_lock_test_and_set(&mutex->val, 2);
        }
        while (c != 0) {
            sys_futex(&mutex->val, FUTEX_WAIT, 2, NULL);
            c = __sync_lock_test_and_set(&mutex->val, 2);
        }
    }
}

static inline void pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (__sync_sub_and_fetch(&mutex->val, 1) != 0) {
        mutex->val = 0;
        sys_futex(&mutex->val, FUTEX_WAKE, 1, NULL);
    }
}

#define THREAD_STACK_SIZE 65536
#define NUM_THREADS 4
#define ITERATIONS_PER_THREAD 1000

typedef struct {
    volatile int tid;
    void *(*fn)(void *);
    void *arg;
    void *retval;
    uint8_t stack[THREAD_STACK_SIZE] __attribute__((aligned(16)));
} pthread_entry_t;

static pthread_entry_t g_threads[NUM_THREADS];
static pthread_mutex_t g_mutex;
static volatile int g_shared_counter = 0;

static void *worker_routine(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        pthread_mutex_lock(&g_mutex);
        g_shared_counter++;
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
}

static int pthread_spawn(pthread_entry_t *entry, void *(*fn)(void *), void *arg)
{
    entry->fn = fn;
    entry->arg = arg;
    entry->tid = 1;

    uint64_t clone_flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_CHILD_CLEARTID;
    void *stack_top = entry->stack + sizeof(entry->stack);

    int64_t ret = sys_clone(clone_flags, stack_top, NULL, (int *)&entry->tid, 0);
    if (ret == 0) {
        // Child thread
        void *r = entry->fn(entry->arg);
        entry->retval = r;
        sys_exit(0);
    }
    return (ret > 0) ? 0 : -1;
}

static int pthread_wait_join(pthread_entry_t *entry)
{
    while (entry->tid != 0) {
        sys_futex(&entry->tid, FUTEX_WAIT, entry->tid, NULL);
    }
    return 0;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    pthread_mutex_init(&g_mutex);

    const char start_msg[] = "[pthread] spawning 4 threads\n";
    sys_write(1, start_msg, sizeof(start_msg) - 1);

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_spawn(&g_threads[i], worker_routine, NULL) != 0) {
            sys_exit(1);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_wait_join(&g_threads[i]);
    }

    if (g_shared_counter == NUM_THREADS * ITERATIONS_PER_THREAD) {
        const char pass_msg[] = "[pthread] all threads joined, counter validated (4000)\n";
        sys_write(1, pass_msg, sizeof(pass_msg) - 1);
        sys_exit(0);
    }

    sys_exit(2);
}
