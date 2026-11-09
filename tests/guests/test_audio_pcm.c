#include <stddef.h>
#include <stdint.h>

#define AT_FDCWD -100
#define O_RDWR 2

#define SNDRV_PCM_VERSION 0x0002000fu
#define SNDRV_CTL_VERSION 0x00020000u

#define SNDRV_PCM_STREAM_PLAYBACK 0
#define SNDRV_PCM_STATE_RUNNING 3

#define SNDRV_PCM_FORMAT_S16_LE 2
#define SNDRV_PCM_ACCESS_RW_INTERLEAVED 3

#define SNDRV_PCM_HW_PARAM_ACCESS 0
#define SNDRV_PCM_HW_PARAM_FORMAT 1
#define SNDRV_PCM_HW_PARAM_CHANNELS 5
#define SNDRV_PCM_HW_PARAM_RATE 6
#define SNDRV_PCM_HW_PARAM_PERIOD_SIZE 8
#define SNDRV_PCM_HW_PARAM_PERIODS 10

#define SNDRV_PCM_IOCTL_PVERSION 0x80044100u
#define SNDRV_PCM_IOCTL_INFO 0x81204101u
#define SNDRV_PCM_IOCTL_HW_PARAMS 0xc2604111u
#define SNDRV_PCM_IOCTL_STATUS 0x80984120u
#define SNDRV_PCM_IOCTL_PREPARE 0x00004140u
#define SNDRV_PCM_IOCTL_START 0x00004142u
#define SNDRV_PCM_IOCTL_DROP 0x00004143u
#define SNDRV_PCM_IOCTL_DRAIN 0x00004144u
#define SNDRV_PCM_IOCTL_WRITEI_FRAMES 0x40184150u

#define SNDRV_CTL_IOCTL_PVERSION 0x80045500u
#define SNDRV_CTL_IOCTL_CARD_INFO 0x81685501u

struct snd_mask {
    uint32_t bits[8];
};

struct snd_interval {
    uint32_t min;
    uint32_t max;
    uint32_t openmin : 1;
    uint32_t openmax : 1;
    uint32_t integer : 1;
    uint32_t empty : 1;
};

struct snd_pcm_hw_params {
    uint32_t flags;
    struct snd_mask masks[3];
    struct snd_mask mres[5];
    struct snd_interval intervals[12];
    struct snd_interval ires[9];
    uint32_t rmask;
    uint32_t cmask;
    uint32_t info;
    uint32_t msbits;
    uint32_t rate_num;
    uint32_t rate_den;
    uint64_t fifo_size;
    uint8_t reserved[64];
};

struct snd_pcm_info {
    uint32_t device;
    uint32_t subdevice;
    int32_t stream;
    int32_t card;
    uint8_t id[64];
    uint8_t name[80];
    uint8_t subname[32];
    int32_t dev_class;
    int32_t dev_subclass;
    uint32_t subdevices_count;
    uint32_t subdevices_avail;
    uint8_t sync[16];
    uint8_t reserved[64];
};

struct snd_ctl_card_info {
    int32_t card;
    int32_t pad;
    uint8_t id[16];
    uint8_t driver[16];
    uint8_t name[32];
    uint8_t longname[80];
    uint8_t mixername[80];
    uint8_t components[128];
};

struct snd_xferi {
    int64_t result;
    void *buf;
    uint64_t frames;
};

struct snd_pcm_status {
    int32_t state;
    int32_t pad;
    int64_t trigger_tstamp_sec;
    int64_t trigger_tstamp_nsec;
    int64_t tstamp_sec;
    int64_t tstamp_nsec;
    uint64_t appl_ptr;
    uint64_t hw_ptr;
    int64_t delay;
    uint64_t avail;
    uint64_t avail_max;
    uint64_t overrange;
    int32_t suspended_state;
    uint32_t audio_tstamp_data;
    int64_t audio_tstamp_sec;
    int64_t audio_tstamp_nsec;
    int64_t driver_tstamp_sec;
    int64_t driver_tstamp_nsec;
    uint32_t audio_tstamp_accuracy;
    uint8_t reserved[52 - 2 * sizeof(int64_t)];
};

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(mode)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_ioctl(int fd, uint32_t cmd, void *arg)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(16), "D"(fd), "S"(cmd), "d"(arg)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_write(int fd, const void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

void *memset(void *dest, int val, size_t len)
{
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < len; ++i) {
        p[i] = (uint8_t)val;
    }
    return dest;
}

__attribute__((force_align_arg_pointer)) void _start(void)
{
    // Control device verification
    int ctl_fd = (int)sys_openat(AT_FDCWD, "/dev/snd/controlC0", O_RDWR, 0);
    if (ctl_fd < 0) {
        sys_exit(1);
    }

    uint32_t ctl_ver = 0;
    if (sys_ioctl(ctl_fd, SNDRV_CTL_IOCTL_PVERSION, &ctl_ver) != 0 ||
        ctl_ver != SNDRV_CTL_VERSION) {
        sys_exit(2);
    }

    struct snd_ctl_card_info card_info;
    memset(&card_info, 0, sizeof(card_info));
    if (sys_ioctl(ctl_fd, SNDRV_CTL_IOCTL_CARD_INFO, &card_info) != 0 || card_info.card != 0) {
        sys_exit(3);
    }
    sys_close(ctl_fd);

    // PCM playback device verification
    int pcm_fd = (int)sys_openat(AT_FDCWD, "/dev/snd/pcmC0D0p", O_RDWR, 0);
    if (pcm_fd < 0) {
        sys_exit(4);
    }

    uint32_t pcm_ver = 0;
    if (sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_PVERSION, &pcm_ver) != 0 ||
        pcm_ver != SNDRV_PCM_VERSION) {
        sys_exit(5);
    }

    struct snd_pcm_info pcm_info;
    memset(&pcm_info, 0, sizeof(pcm_info));
    if (sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_INFO, &pcm_info) != 0 ||
        pcm_info.stream != SNDRV_PCM_STREAM_PLAYBACK) {
        sys_exit(6);
    }

    // Configure HW parameters (48000 Hz, 2 ch, S16_LE)
    struct snd_pcm_hw_params params;
    memset(&params, 0, sizeof(params));
    params.masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0] = (1u << SNDRV_PCM_FORMAT_S16_LE);
    params.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min = 2;
    params.intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min = 48000;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min = 512;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min = 4;

    if (sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params) != 0) {
        sys_exit(7);
    }

    // Prepare stream
    if (sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0) != 0) {
        sys_exit(8);
    }

    // Generate 512 frames of synthetic 440 Hz audio (1024 samples of int16_t)
    int16_t audio_buf[1024];
    for (int i = 0; i < 512; ++i) {
        int16_t val = (int16_t)(((i % 100) - 50) * 400);
        audio_buf[i * 2 + 0] = val;
        audio_buf[i * 2 + 1] = val;
    }

    // Test sys_write transfer path (512 frames * 4 bytes = 2048 bytes)
    int64_t written_bytes = sys_write(pcm_fd, audio_buf, sizeof(audio_buf));
    if (written_bytes != (int64_t)sizeof(audio_buf)) {
        sys_exit(9);
    }

    // Test SNDRV_PCM_IOCTL_WRITEI_FRAMES transfer path
    struct snd_xferi xfer;
    xfer.result = 0;
    xfer.buf = audio_buf;
    xfer.frames = 512;
    int64_t written_frames = sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer);
    if (written_frames != 512 || xfer.result != 512) {
        sys_exit(10);
    }

    // Verify status
    struct snd_pcm_status status;
    memset(&status, 0, sizeof(status));
    if (sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_STATUS, &status) != 0 ||
        status.state != SNDRV_PCM_STATE_RUNNING) {
        sys_exit(11);
    }

    // Drop and close
    sys_ioctl(pcm_fd, SNDRV_PCM_IOCTL_DROP, 0);
    sys_close(pcm_fd);

    sys_exit(0);
}
