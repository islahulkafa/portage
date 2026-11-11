#include <stdint.h>

#define EXPORT __attribute__((visibility("default"))) __attribute__((sysv_abi))

EXPORT void *eglGetDisplay(void *display_id)
{
    (void)display_id;
    return (void *)0;
}
EXPORT unsigned int eglInitialize(void *dpy, int32_t *major, int32_t *minor)
{
    (void)dpy;
    (void)major;
    (void)minor;
    return 1;
}
EXPORT unsigned int eglTerminate(void *dpy)
{
    (void)dpy;
    return 1;
}
EXPORT const char *eglQueryString(void *dpy, int32_t name)
{
    (void)dpy;
    (void)name;
    return "";
}
EXPORT unsigned int eglGetConfigs(void *dpy, void **configs, int32_t config_size,
                                  int32_t *num_config)
{
    (void)dpy;
    (void)configs;
    (void)config_size;
    (void)num_config;
    return 1;
}
EXPORT unsigned int eglChooseConfig(void *dpy, const int32_t *attrib_list, void **configs,
                                    int32_t config_size, int32_t *num_config)
{
    (void)dpy;
    (void)attrib_list;
    (void)configs;
    (void)config_size;
    (void)num_config;
    return 1;
}
EXPORT unsigned int eglGetConfigAttrib(void *dpy, void *config, int32_t attribute, int32_t *value)
{
    (void)dpy;
    (void)config;
    (void)attribute;
    (void)value;
    return 1;
}
EXPORT void *eglCreateWindowSurface(void *dpy, void *config, void *win, const int32_t *attrib_list)
{
    (void)dpy;
    (void)config;
    (void)win;
    (void)attrib_list;
    return (void *)0;
}
EXPORT void *eglCreatePbufferSurface(void *dpy, void *config, const int32_t *attrib_list)
{
    (void)dpy;
    (void)config;
    (void)attrib_list;
    return (void *)0;
}
EXPORT unsigned int eglDestroySurface(void *dpy, void *surface)
{
    (void)dpy;
    (void)surface;
    return 1;
}
EXPORT unsigned int eglQuerySurface(void *dpy, void *surface, int32_t attribute, int32_t *value)
{
    (void)dpy;
    (void)surface;
    (void)attribute;
    (void)value;
    return 1;
}
EXPORT void *eglCreateContext(void *dpy, void *config, void *share_context,
                              const int32_t *attrib_list)
{
    (void)dpy;
    (void)config;
    (void)share_context;
    (void)attrib_list;
    return (void *)0;
}
EXPORT unsigned int eglDestroyContext(void *dpy, void *ctx)
{
    (void)dpy;
    (void)ctx;
    return 1;
}
EXPORT unsigned int eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx)
{
    (void)dpy;
    (void)draw;
    (void)read;
    (void)ctx;
    return 1;
}
EXPORT void *eglGetCurrentContext(void) { return (void *)0; }
EXPORT void *eglGetCurrentSurface(int32_t readdraw)
{
    (void)readdraw;
    return (void *)0;
}
EXPORT void *eglGetCurrentDisplay(void) { return (void *)0; }
EXPORT unsigned int eglSwapBuffers(void *dpy, void *surface)
{
    (void)dpy;
    (void)surface;
    return 1;
}
EXPORT int32_t eglGetError(void) { return 0x3000; }
EXPORT void *eglGetProcAddress(const char *procname)
{
    (void)procname;
    return (void *)0;
}
EXPORT void *eglGetNativeClientBufferANDROID(const void *buffer)
{
    (void)buffer;
    return (void *)0;
}
EXPORT void *eglCreateImageKHR(void *dpy, void *ctx, unsigned int target, void *buffer,
                               const int32_t *attrib_list)
{
    (void)dpy;
    (void)ctx;
    (void)target;
    (void)buffer;
    (void)attrib_list;
    return (void *)0;
}
EXPORT unsigned int eglDestroyImageKHR(void *dpy, void *image)
{
    (void)dpy;
    (void)image;
    return 1;
}
EXPORT unsigned int eglSwapInterval(void *dpy, int32_t interval)
{
    (void)dpy;
    (void)interval;
    return 1;
}
EXPORT void *eglCreateSync(void *dpy, unsigned int type, const intptr_t *attrib_list)
{
    (void)dpy;
    (void)type;
    (void)attrib_list;
    return (void *)0;
}
EXPORT unsigned int eglDestroySync(void *dpy, void *sync)
{
    (void)dpy;
    (void)sync;
    return 1;
}
EXPORT int32_t eglClientWaitSync(void *dpy, void *sync, int32_t flags, uint64_t timeout)
{
    (void)dpy;
    (void)sync;
    (void)flags;
    (void)timeout;
    return 0x30F6;
}
EXPORT unsigned int eglWaitSync(void *dpy, void *sync, int32_t flags)
{
    (void)dpy;
    (void)sync;
    (void)flags;
    return 1;
}
EXPORT unsigned int eglGetSyncAttrib(void *dpy, void *sync, int32_t attribute, intptr_t *value)
{
    (void)dpy;
    (void)sync;
    (void)attribute;
    (void)value;
    return 1;
}
EXPORT void *eglCreateSyncKHR(void *dpy, unsigned int type, const int32_t *attrib_list)
{
    (void)dpy;
    (void)type;
    (void)attrib_list;
    return (void *)0;
}
EXPORT unsigned int eglDestroySyncKHR(void *dpy, void *sync)
{
    (void)dpy;
    (void)sync;
    return 1;
}
EXPORT int32_t eglClientWaitSyncKHR(void *dpy, void *sync, int32_t flags, uint64_t timeout)
{
    (void)dpy;
    (void)sync;
    (void)flags;
    (void)timeout;
    return 0x30F6;
}
EXPORT unsigned int eglWaitSyncKHR(void *dpy, void *sync, int32_t flags)
{
    (void)dpy;
    (void)sync;
    (void)flags;
    return 1;
}
EXPORT unsigned int eglGetSyncAttribKHR(void *dpy, void *sync, int32_t attribute, int32_t *value)
{
    (void)dpy;
    (void)sync;
    (void)attribute;
    (void)value;
    return 1;
}
EXPORT int32_t eglDupNativeFenceFDANDROID(void *dpy, void *sync)
{
    (void)dpy;
    (void)sync;
    return -1;
}
