#include <stdint.h>

struct SLInterfaceID_ {
    uint32_t time_low;
    uint16_t time_mid;
    uint16_t time_hi_and_version;
    uint8_t clock_seq_hi_and_reserved;
    uint8_t clock_seq_low;
    uint8_t node[6];
};

static const struct SLInterfaceID_ s_iid_engine = {
    0x8d2adf00, 0x51c9, 0x11d8, 0x9e, 0x55, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
static const struct SLInterfaceID_ s_iid_bq = {
    0x198e1ea0, 0x4143, 0x11df, 0x8b, 0x1c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
static const struct SLInterfaceID_ s_iid_play = {
    0xef064d60, 0x51c9, 0x11d8, 0xa4, 0x0d, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};

__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_ENGINE =
    &s_iid_engine;
__attribute__((visibility("default")))
const struct SLInterfaceID_ *const SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &s_iid_bq;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_PLAY = &s_iid_play;

__attribute__((visibility("default"))) int32_t slCreateEngine(void **pEngine, uint32_t numOptions,
                                                              const void *pOptions,
                                                              uint32_t numInterfaces,
                                                              const void *pInterfaceIds,
                                                              const void *pInterfaceRequired)
{
    (void)pEngine;
    (void)numOptions;
    (void)pOptions;
    (void)numInterfaces;
    (void)pInterfaceIds;
    (void)pInterfaceRequired;
    return 0;
}
