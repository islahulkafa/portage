#include <stdbool.h>
#include <stdint.h>

__attribute__((visibility("default"))) void SwappyGL_init(void *env, void *activity)
{
    (void)env;
    (void)activity;
}

__attribute__((visibility("default"))) void SwappyGL_setWindow(void *window) { (void)window; }

__attribute__((visibility("default"))) bool SwappyGL_swap(void *display, void *surface)
{
    (void)display;
    (void)surface;
    return true;
}

__attribute__((visibility("default"))) void SwappyGL_destroy(void) {}

__attribute__((visibility("default"))) void SwappyGL_setTargetFrameRateHz(uint32_t targetFps)
{
    (void)targetFps;
}

__attribute__((visibility("default"))) int64_t SwappyGL_getSwapIntervalNS(void)
{
    return 16666667LL;
}

__attribute__((visibility("default"))) void SwappyGL_setAutoSwapInterval(bool enabled)
{
    (void)enabled;
}

__attribute__((visibility("default"))) bool SwappyGL_isEnabled(void) { return true; }
