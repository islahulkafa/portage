#pragma once

#include "audio/audio_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace portage::audio {

struct AudioStats {
    uint64_t total_frames_written{0};
    uint64_t total_frames_rendered{0};
    uint32_t sample_rate{48000};
    uint32_t channels{2};
    int32_t format{SNDRV_PCM_FORMAT_S16_LE};
    PcmState current_state{SNDRV_PCM_STATE_OPEN};
    bool is_null_sink{false};
    bool is_running{false};
};

class AudioManager {
  public:
    static AudioManager &Instance() noexcept;

    AudioManager(const AudioManager &) = delete;
    AudioManager &operator=(const AudioManager &) = delete;

    int64_t HandlePcmIoctl(uint32_t cmd, void *arg);
    int64_t HandleControlIoctl(uint32_t cmd, void *arg);

    int64_t WritePcmBytes(const void *buf, size_t count);
    int64_t WriteiFrames(const void *buf, uint64_t frames);

    bool ConfigureStream(uint32_t sample_rate, uint32_t channels, int32_t format);

    void CloseStream();
    void Reset();

    void ForceNullSink(bool enable);
    [[nodiscard]] AudioStats GetStats() const;

  private:
    AudioManager();
    ~AudioManager();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace portage::audio
