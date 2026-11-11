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
static const struct SLInterfaceID_ s_iid_volume = {
    0x09e85e00, 0x51ca, 0x11d8, 0x9c, 0xb8, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
static const struct SLInterfaceID_ s_iid_bufferqueue = {
    0x3ab16fb0, 0x51ca, 0x11d8, 0xa7, 0x88, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
static const struct SLInterfaceID_ s_iid_record = {
    0xc5742e80, 0x51c9, 0x11d8, 0x98, 0x07, {0x00, 0x20, 0xaf, 0x0b, 0x41, 0x05}};
static const struct SLInterfaceID_ s_iid_mutesolo = {
    0x5a28ebe0, 0xddd7, 0x11db, 0x82, 0x20, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
static const struct SLInterfaceID_ s_iid_androidconfiguration = {
    0x89f6a7e0, 0xbeac, 0x11df, 0x8b, 0x5c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};

__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_ENGINE =
    &s_iid_engine;
__attribute__((visibility("default")))
const struct SLInterfaceID_ *const SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &s_iid_bq;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_PLAY = &s_iid_play;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_VOLUME =
    &s_iid_volume;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_BUFFERQUEUE =
    &s_iid_bufferqueue;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_RECORD =
    &s_iid_record;
__attribute__((visibility("default"))) const struct SLInterfaceID_ *const SL_IID_MUTESOLO =
    &s_iid_mutesolo;
__attribute__((visibility("default")))
const struct SLInterfaceID_ *const SL_IID_ANDROIDCONFIGURATION = &s_iid_androidconfiguration;

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
