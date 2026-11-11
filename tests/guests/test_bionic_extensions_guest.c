#include <stddef.h>
#include <stdint.h>

typedef uintptr_t bionic_pthread_t;

typedef struct {
    uint64_t __reserved[7];
} bionic_pthread_attr_t;

typedef struct {
    uint64_t flags;
    void *reserved_addr;
    size_t reserved_size;
    int relro_fd;
    int library_fd;
    int64_t library_fd_offset;
    void *library_namespace;
} android_dlextinfo;

struct link_map_raw {
    uint64_t l_addr;
    const char *l_name;
    uint64_t l_ld;
    struct link_map_raw *l_next;
    struct link_map_raw *l_prev;
};

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_DI_LINKMAP 2
#define RTLD_DI_ORIGIN 6
#define ANDROID_DLEXT_USE_LIBRARY_FD 0x10

extern bionic_pthread_t pthread_self(void);
extern int pthread_equal(bionic_pthread_t t1, bionic_pthread_t t2);
extern int pthread_setname_np(bionic_pthread_t thread, const char *name);
extern int pthread_getname_np(bionic_pthread_t thread, char *buf, size_t len);
extern int pthread_getattr_np(bionic_pthread_t thread, bionic_pthread_attr_t *attr);
extern int pthread_attr_init(bionic_pthread_attr_t *attr);
extern int pthread_attr_destroy(bionic_pthread_attr_t *attr);
extern int pthread_attr_getstack(const bionic_pthread_attr_t *attr, void **stackaddr,
                                 size_t *stacksize);
extern int pthread_attr_setstack(bionic_pthread_attr_t *attr, void *stackaddr, size_t stacksize);
extern int pthread_attr_getstacksize(const bionic_pthread_attr_t *attr, size_t *stacksize);
extern int pthread_attr_setstacksize(bionic_pthread_attr_t *attr, size_t stacksize);
extern int pthread_attr_getguardsize(const bionic_pthread_attr_t *attr, size_t *guardsize);
extern int pthread_attr_setguardsize(bionic_pthread_attr_t *attr, size_t guardsize);
extern int pthread_attr_getdetachstate(const bionic_pthread_attr_t *attr, int *detachstate);
extern int pthread_attr_setdetachstate(bionic_pthread_attr_t *attr, int detachstate);

extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern void *android_dlopen_ext(const char *filename, int flag, const android_dlextinfo *extinfo);
extern int dlinfo(void *handle, int request, void *info);

static inline int64_t sys_openat(int dirfd, const char *pathname, int flags, int mode)
{
    int64_t ret;
    register int r10 __asm__("r10") = mode;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"((int64_t)dirfd), "S"(pathname), "d"((int64_t)flags), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"((int64_t)fd) : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"((int64_t)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static int str_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

void _start(void)
{
    bionic_pthread_t self = pthread_self();
    if (self == 0 || !pthread_equal(self, self)) {
        sys_exit(1);
    }

    if (pthread_setname_np(self, "bionic_guest") != 0) {
        sys_exit(2);
    }

    char name_buf[32];
    for (size_t i = 0; i < sizeof(name_buf); ++i) {
        name_buf[i] = 0;
    }
    if (pthread_getname_np(self, name_buf, sizeof(name_buf)) != 0 ||
        !str_eq(name_buf, "bionic_guest")) {
        sys_exit(3);
    }

    bionic_pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        sys_exit(4);
    }
    if (pthread_getattr_np(self, &attr) != 0) {
        sys_exit(5);
    }

    void *stack_base = NULL;
    size_t stack_size = 0;
    if (pthread_attr_getstack(&attr, &stack_base, &stack_size) != 0) {
        sys_exit(6);
    }
    uintptr_t local_addr = (uintptr_t)&attr;
    uintptr_t base_addr = (uintptr_t)stack_base;
    if (stack_base == NULL || stack_size < (1024u * 1024u) || local_addr < base_addr ||
        local_addr >= base_addr + stack_size) {
        sys_exit(7);
    }

    if (pthread_attr_setstacksize(&attr, 2u * 1024u * 1024u) != 0) {
        sys_exit(8);
    }
    size_t queried_sz = 0;
    if (pthread_attr_getstacksize(&attr, &queried_sz) != 0 || queried_sz != (2u * 1024u * 1024u)) {
        sys_exit(9);
    }
    if (pthread_attr_setguardsize(&attr, 8192u) != 0) {
        sys_exit(10);
    }
    size_t guard_sz = 0;
    if (pthread_attr_getguardsize(&attr, &guard_sz) != 0 || guard_sz != 8192u) {
        sys_exit(11);
    }
    if (pthread_attr_setdetachstate(&attr, 1) != 0) {
        sys_exit(12);
    }
    int detach_state = 0;
    if (pthread_attr_getdetachstate(&attr, &detach_state) != 0 || detach_state != 1) {
        sys_exit(13);
    }
    if (pthread_attr_destroy(&attr) != 0) {
        sys_exit(14);
    }

    struct link_map_raw *lm = NULL;
    if (dlinfo(NULL, RTLD_DI_LINKMAP, &lm) != 0 || lm == NULL || lm->l_name == NULL) {
        sys_exit(15);
    }

    int64_t math_fd = sys_openat(-100, "/system/lib64/libgame_math.so", 0, 0);
    if (math_fd < 0) {
        sys_exit(16);
    }

    android_dlextinfo ext = {0};
    ext.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
    ext.library_fd = (int)math_fd;

    void *math_handle = android_dlopen_ext("fd_game_math", RTLD_LAZY, &ext);
    sys_close((int)math_fd);
    if (!math_handle) {
        sys_exit(17);
    }

    typedef int (*add_fn_t)(int, int);
    add_fn_t add_fn = (add_fn_t)dlsym(math_handle, "math_add");
    if (!add_fn || add_fn(19, 23) != 42) {
        sys_exit(18);
    }

    if (dlclose(math_handle) != 0) {
        sys_exit(19);
    }

    sys_exit(0);
}
