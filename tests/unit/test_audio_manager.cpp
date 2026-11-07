#include <gtest/gtest.h>

#include "audio/audio_manager.hpp"
#include "audio/audio_types.hpp"
#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"

#include <cstring>
#include <thread>
#include <vector>

namespace portage::audio {

class AudioManagerTest : public ::testing::Test {
  protected:
    void SetUp() override { AudioManager::Instance().Reset(); }

    void TearDown() override { AudioManager::Instance().Reset(); }
};

TEST_F(AudioManagerTest, VfsResolvesAudioDeviceNodes)
{
    fs::Vfs vfs;
    auto resolved_pcm = vfs.Resolve("/dev/snd/pcmC0D0p");
    EXPECT_EQ(resolved_pcm.dev_type, fs::DeviceType::AlsaPcm);

    auto resolved_ctl = vfs.Resolve("/dev/snd/controlC0");
    EXPECT_EQ(resolved_ctl.dev_type, fs::DeviceType::AlsaControl);
}

TEST_F(AudioManagerTest, FdTableAllocatesAndClosesAudioDescriptors)
{
    fs::FileDescriptorTable fds;
    int pcm_fd = fds.AllocateAudio(fs::DeviceType::AlsaPcm, 0);
    EXPECT_GE(pcm_fd, 3);

    auto desc = fds.Get(pcm_fd);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->dev, fs::DeviceType::AlsaPcm);

    int ctl_fd = fds.AllocateAudio(fs::DeviceType::AlsaControl, 0);
    EXPECT_GE(ctl_fd, 3);
    EXPECT_NE(ctl_fd, pcm_fd);

    EXPECT_EQ(fds.Close(pcm_fd), 0);
    EXPECT_EQ(fds.Close(ctl_fd), 0);
}

TEST_F(AudioManagerTest, ControlIoctlsReturnCardInfo)
{
    auto &mgr = AudioManager::Instance();

    uint32_t version = 0;
    EXPECT_EQ(mgr.HandleControlIoctl(SNDRV_CTL_IOCTL_PVERSION, &version), 0);
    EXPECT_EQ(version, SNDRV_CTL_VERSION);

    snd_ctl_card_info info{};
    EXPECT_EQ(mgr.HandleControlIoctl(SNDRV_CTL_IOCTL_CARD_INFO, &info), 0);
    EXPECT_EQ(info.card, 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(info.id), "PortageCard0");
    EXPECT_STREQ(reinterpret_cast<const char *>(info.driver), "PortageWASAPI");
    EXPECT_STREQ(reinterpret_cast<const char *>(info.name), "Portage Audio");
}

TEST_F(AudioManagerTest, PcmProtocolVersionAndInfoIoctl)
{
    auto &mgr = AudioManager::Instance();

    uint32_t pversion = 0;
    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_PVERSION, &pversion), 0);
    EXPECT_EQ(pversion, SNDRV_PCM_VERSION);

    snd_pcm_info info{};
    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_INFO, &info), 0);
    EXPECT_EQ(info.card, 0);
    EXPECT_EQ(info.device, 0u);
    EXPECT_EQ(info.stream, SNDRV_PCM_STREAM_PLAYBACK);
    EXPECT_STREQ(reinterpret_cast<const char *>(info.id), "PortagePCM");
}

TEST_F(AudioManagerTest, PcmHardwareParametersNegotiation)
{
    auto &mgr = AudioManager::Instance();

    snd_pcm_hw_params params{};
    params.masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0] = (1u << SNDRV_PCM_FORMAT_S16_LE);
    params.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min = 2;
    params.intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min = 44100;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min = 512;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min = 4;

    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_HW_PARAMS, &params), 0);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.current_state, SNDRV_PCM_STATE_SETUP);
    EXPECT_EQ(stats.sample_rate, 44100u);
    EXPECT_EQ(stats.channels, 2u);
    EXPECT_EQ(stats.format, SNDRV_PCM_FORMAT_S16_LE);

    // Prepare stream
    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_PREPARE, nullptr), 0);
    stats = mgr.GetStats();
    EXPECT_EQ(stats.current_state, SNDRV_PCM_STATE_PREPARED);
}

TEST_F(AudioManagerTest, StreamingPcmDataAndFrames)
{
    auto &mgr = AudioManager::Instance();
    mgr.ForceNullSink(true);

    snd_pcm_hw_params params{};
    params.masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0] = (1u << SNDRV_PCM_FORMAT_S16_LE);
    params.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min = 2;
    params.intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min = 48000;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min = 256;
    params.intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min = 4;

    ASSERT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_HW_PARAMS, &params), 0);
    ASSERT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_PREPARE, nullptr), 0);

    // Write 512 frames of stereo S16 (512 * 4 = 2048 bytes)
    std::vector<int16_t> samples(512 * 2, 1000);
    int64_t bytes_written = mgr.WritePcmBytes(samples.data(), samples.size() * sizeof(int16_t));
    EXPECT_EQ(bytes_written, static_cast<int64_t>(samples.size() * sizeof(int16_t)));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.total_frames_written, 512u);
    EXPECT_EQ(stats.current_state, SNDRV_PCM_STATE_RUNNING);

    // Write another 256 frames via SNDRV_PCM_IOCTL_WRITEI_FRAMES
    snd_xferi xfer{};
    xfer.buf = samples.data();
    xfer.frames = 256;
    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer), 256);
    EXPECT_EQ(xfer.result, 256);

    stats = mgr.GetStats();
    EXPECT_EQ(stats.total_frames_written, 768u);

    // Drop and stop playback
    EXPECT_EQ(mgr.HandlePcmIoctl(SNDRV_PCM_IOCTL_DROP, nullptr), 0);
    stats = mgr.GetStats();
    EXPECT_EQ(stats.current_state, SNDRV_PCM_STATE_SETUP);
}

} // namespace portage::audio
