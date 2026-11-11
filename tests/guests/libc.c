#include <stddef.h>
#include <stdint.h>

__attribute__((visibility("default"))) uintptr_t pthread_self(void) { return 0; }
__attribute__((visibility("default"))) int pthread_equal(uintptr_t t1, uintptr_t t2)
{
    return t1 == t2;
}
__attribute__((visibility("default"))) int pthread_setname_np(uintptr_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}
__attribute__((visibility("default"))) int pthread_getname_np(uintptr_t thread, char *buf,
                                                                size_t len)
{
    (void)thread;
    (void)buf;
    (void)len;
    return 0;
}
__attribute__((visibility("default"))) int pthread_getattr_np(uintptr_t thread, void *attr)
{
    (void)thread;
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_init(void *attr)
{
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_destroy(void *attr)
{
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getstack(const void *attr, void **stackaddr,
                                                                   size_t *stacksize)
{
    (void)attr;
    (void)stackaddr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setstack(void *attr, void *stackaddr,
                                                                   size_t stacksize)
{
    (void)attr;
    (void)stackaddr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getstacksize(const void *attr,
                                                                       size_t *stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setstacksize(void *attr, size_t stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getguardsize(const void *attr,
                                                                       size_t *guardsize)
{
    (void)attr;
    (void)guardsize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setguardsize(void *attr, size_t guardsize)
{
    (void)attr;
    (void)guardsize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getdetachstate(const void *attr,
                                                                         int *detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setdetachstate(void *attr, int detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}
__attribute__((visibility("default"))) int android_get_device_api_level(void) { return 34; }
__attribute__((visibility("default"))) int android_get_application_target_sdk_version(void)
{
    return 34;
}
