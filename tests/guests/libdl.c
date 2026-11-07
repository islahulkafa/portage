#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

__attribute__((visibility("default"))) void *dlopen(const char *filename, int flags)
{
    (void)filename;
    (void)flags;
    return NULL;
}

__attribute__((visibility("default"))) void *dlsym(void *handle, const char *symbol)
{
    (void)handle;
    (void)symbol;
    return NULL;
}

__attribute__((visibility("default"))) int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

__attribute__((visibility("default"))) char *dlerror(void) { return NULL; }

__attribute__((visibility("default"))) int dladdr(const void *addr, Dl_info *info)
{
    (void)addr;
    (void)info;
    return 0;
}
