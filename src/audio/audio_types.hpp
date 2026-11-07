#pragma once

#include <cstddef>
#include <cstdint>

namespace portage::audio {

// ALSA Protocol versions
inline constexpr uint32_t SNDRV_PCM_VERSION = 0x0002000fu;
inline constexpr uint32_t SNDRV_CTL_VERSION = 0x00020000u;

// ALSA Stream direction
inline constexpr int SNDRV_PCM_STREAM_PLAYBACK = 0;
inline constexpr int SNDRV_PCM_STREAM_CAPTURE = 1;

// ALSA Stream states
enum PcmState : int32_t {
    SNDRV_PCM_STATE_OPEN = 0,
    SNDRV_PCM_STATE_SETUP = 1,
    SNDRV_PCM_STATE_PREPARED = 2,
    SNDRV_PCM_STATE_RUNNING = 3,
    SNDRV_PCM_STATE_XRUN = 4,
    SNDRV_PCM_STATE_DRAINING = 5,
    SNDRV_PCM_STATE_PAUSED = 6,
    SNDRV_PCM_STATE_SUSPENDED = 7,
    SNDRV_PCM_STATE_DISCONNECTED = 8,
};

// ALSA Formats
inline constexpr int SNDRV_PCM_FORMAT_S8 = 0;
inline constexpr int SNDRV_PCM_FORMAT_U8 = 1;
inline constexpr int SNDRV_PCM_FORMAT_S16_LE = 2;
inline constexpr int SNDRV_PCM_FORMAT_S16_BE = 3;
inline constexpr int SNDRV_PCM_FORMAT_U16_LE = 4;
inline constexpr int SNDRV_PCM_FORMAT_U16_BE = 5;
inline constexpr int SNDRV_PCM_FORMAT_S24_LE = 6;
inline constexpr int SNDRV_PCM_FORMAT_S24_BE = 7;
inline constexpr int SNDRV_PCM_FORMAT_U24_LE = 8;
inline constexpr int SNDRV_PCM_FORMAT_U24_BE = 9;
inline constexpr int SNDRV_PCM_FORMAT_S32_LE = 10;
inline constexpr int SNDRV_PCM_FORMAT_S32_BE = 11;
inline constexpr int SNDRV_PCM_FORMAT_U32_LE = 12;
inline constexpr int SNDRV_PCM_FORMAT_U32_BE = 13;
inline constexpr int SNDRV_PCM_FORMAT_FLOAT_LE = 14;
inline constexpr int SNDRV_PCM_FORMAT_FLOAT_BE = 15;

// ALSA Access modes
inline constexpr int SNDRV_PCM_ACCESS_MMAP_INTERLEAVED = 0;
inline constexpr int SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED = 1;
inline constexpr int SNDRV_PCM_ACCESS_MMAP_COMPLEX = 2;
inline constexpr int SNDRV_PCM_ACCESS_RW_INTERLEAVED = 3;
inline constexpr int SNDRV_PCM_ACCESS_RW_NONINTERLEAVED = 4;

// ALSA HW Param indices
inline constexpr int SNDRV_PCM_HW_PARAM_ACCESS = 0;
inline constexpr int SNDRV_PCM_HW_PARAM_FORMAT = 1;
inline constexpr int SNDRV_PCM_HW_PARAM_SUBFORMAT = 2;
inline constexpr int SNDRV_PCM_HW_PARAM_FIRST_MASK = SNDRV_PCM_HW_PARAM_ACCESS;
inline constexpr int SNDRV_PCM_HW_PARAM_LAST_MASK = SNDRV_PCM_HW_PARAM_SUBFORMAT;

inline constexpr int SNDRV_PCM_HW_PARAM_SAMPLE_BITS = 3;
inline constexpr int SNDRV_PCM_HW_PARAM_FRAME_BITS = 4;
inline constexpr int SNDRV_PCM_HW_PARAM_CHANNELS = 5;
inline constexpr int SNDRV_PCM_HW_PARAM_RATE = 6;
inline constexpr int SNDRV_PCM_HW_PARAM_PERIOD_TIME = 7;
inline constexpr int SNDRV_PCM_HW_PARAM_PERIOD_SIZE = 8;
inline constexpr int SNDRV_PCM_HW_PARAM_PERIOD_BYTES = 9;
inline constexpr int SNDRV_PCM_HW_PARAM_PERIODS = 10;
inline constexpr int SNDRV_PCM_HW_PARAM_BUFFER_TIME = 11;
inline constexpr int SNDRV_PCM_HW_PARAM_BUFFER_SIZE = 12;
inline constexpr int SNDRV_PCM_HW_PARAM_BUFFER_BYTES = 13;
inline constexpr int SNDRV_PCM_HW_PARAM_TICK_TIME = 14;
inline constexpr int SNDRV_PCM_HW_PARAM_FIRST_INTERVAL = SNDRV_PCM_HW_PARAM_SAMPLE_BITS;
inline constexpr int SNDRV_PCM_HW_PARAM_LAST_INTERVAL = SNDRV_PCM_HW_PARAM_TICK_TIME;

inline constexpr size_t SNDRV_MASK_MAX = 256;

struct snd_mask {
    uint32_t bits[(SNDRV_MASK_MAX + 31) / 32]{0};
};

struct snd_interval {
    uint32_t min{0};
    uint32_t max{0};
    uint32_t openmin : 1 {0};
    uint32_t openmax : 1 {0};
    uint32_t integer : 1 {0};
    uint32_t empty : 1 {0};
};

struct snd_pcm_hw_params {
    uint32_t flags{0};
    snd_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK + 1]{};
    snd_mask mres[5]{};
    snd_interval
        intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1]{};
    snd_interval ires[9]{};
    uint32_t rmask{0};
    uint32_t cmask{0};
    uint32_t info{0};
    uint32_t msbits{0};
    uint32_t rate_num{0};
    uint32_t rate_den{0};
    uint64_t fifo_size{0};
    uint8_t reserved[64]{0};
};

struct snd_pcm_sw_params {
    int32_t tstamp_mode{0};
    uint32_t period_step{0};
    uint32_t sleep_min{0};
    uint64_t avail_min{0};
    uint64_t xfer_align{0};
    uint64_t start_threshold{0};
    uint64_t stop_threshold{0};
    uint64_t silence_threshold{0};
    uint64_t silence_size{0};
    uint64_t boundary{0};
    uint32_t proto{0};
    uint32_t tstamp_type{0};
    uint8_t reserved[56]{0};
};

struct snd_pcm_info {
    uint32_t device{0};
    uint32_t subdevice{0};
    int32_t stream{0};
    int32_t card{0};
    uint8_t id[64]{0};
    uint8_t name[80]{0};
    uint8_t subname[32]{0};
    int32_t dev_class{0};
    int32_t dev_subclass{0};
    uint32_t subdevices_count{0};
    uint32_t subdevices_avail{0};
    uint8_t sync[16]{0};
    uint8_t reserved[64]{0};
};

struct snd_ctl_card_info {
    int32_t card{0};
    int32_t pad{0};
    uint8_t id[16]{0};
    uint8_t driver[16]{0};
    uint8_t name[32]{0};
    uint8_t longname[80]{0};
    uint8_t mixername[80]{0};
    uint8_t components[128]{0};
};

struct snd_xferi {
    int64_t result{0};
    void *buf{nullptr};
    uint64_t frames{0};
};

struct snd_pcm_status {
    int32_t state{0};
    int32_t pad{0};
    int64_t trigger_tstamp_sec{0};
    int64_t trigger_tstamp_nsec{0};
    int64_t tstamp_sec{0};
    int64_t tstamp_nsec{0};
    uint64_t appl_ptr{0};
    uint64_t hw_ptr{0};
    int64_t delay{0};
    uint64_t avail{0};
    uint64_t avail_max{0};
    uint64_t overrange{0};
    int32_t suspended_state{0};
    uint32_t audio_tstamp_data{0};
    int64_t audio_tstamp_sec{0};
    int64_t audio_tstamp_nsec{0};
    int64_t driver_tstamp_sec{0};
    int64_t driver_tstamp_nsec{0};
    uint32_t audio_tstamp_accuracy{0};
    uint8_t reserved[52 - 2 * sizeof(int64_t)]{0};
};

// IOCTL command numbers
inline constexpr uint32_t SNDRV_PCM_IOCTL_PVERSION = 0x80044100u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_INFO = 0x81204101u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_HW_REFINE = 0xc2604110u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_HW_PARAMS = 0xc2604111u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_SW_PARAMS = 0xc0884113u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_STATUS = 0x80984120u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_DELAY = 0x80084121u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_PREPARE = 0x00004140u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_RESET = 0x00004141u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_START = 0x00004142u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_DROP = 0x00004143u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_DRAIN = 0x00004144u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_PAUSE = 0x40044145u;
inline constexpr uint32_t SNDRV_PCM_IOCTL_WRITEI_FRAMES = 0x40184150u;

inline constexpr uint32_t SNDRV_CTL_IOCTL_PVERSION = 0x80045500u;
inline constexpr uint32_t SNDRV_CTL_IOCTL_CARD_INFO = 0x81685501u;

} // namespace portage::audio
