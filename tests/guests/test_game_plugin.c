#include <stdint.h>

static volatile int g_plugin_init = 0;
static volatile int g_plugin_fini = 0;

__attribute__((constructor)) static void plugin_constructor(void) { g_plugin_init = 777; }

__attribute__((destructor)) static void plugin_destructor(void) { g_plugin_fini = 888; }

__attribute__((visibility("default"))) int plugin_compute(int x) { return x * 10 + 5; }

__attribute__((visibility("default"))) int plugin_get_init_status(void) { return g_plugin_init; }

__attribute__((visibility("default"))) int plugin_get_fini_status(void) { return g_plugin_fini; }
