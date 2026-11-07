#include <stdint.h>

static volatile int g_math_init_flag = 0;
static volatile int g_math_fini_flag = 0;

__attribute__((constructor)) static void math_constructor(void) { g_math_init_flag = 12345; }

__attribute__((destructor)) static void math_destructor(void) { g_math_fini_flag = 54321; }

__attribute__((visibility("default"))) int math_add(int a, int b) { return a + b; }

__attribute__((visibility("default"))) int math_mul(int a, int b) { return a * b; }

__attribute__((visibility("default"))) int math_get_init_status(void) { return g_math_init_flag; }

__attribute__((visibility("default"))) int math_get_fini_status(void) { return g_math_fini_flag; }
