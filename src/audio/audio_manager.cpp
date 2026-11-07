#include "audio/audio_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace portage::audio {

namespace {

inline constexpr GUID PORTAGE_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

[[maybe_unused]] inline constexpr GUID PORTAGE_SUBTYPE_PCM = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

[[maybe_unused]] constexpr int64_t LINUX_EPERM = -1;
[[maybe_unused]] constexpr int64_t LINUX_EBADF = -9;
constexpr int64_t LINUX_EFAULT = -14;
[[maybe_unused]] constexpr int64_t LINUX_EINVAL = -22;
constexpr int64_t LINUX_ENOTTY = -25;
constexpr int64_t LINUX_EBADFD = -77;

} // namespace

struct AudioManager::Impl {
    mutable std::mutex mutex{};
    std::condition_variable cv{};
    std::condition_variable cv_write{};

    // Stream configuration
    uint32_t guest_rate{48000};
    uint32_t guest_channels{2};
    int32_t guest_format{SNDRV_PCM_FORMAT_S16_LE};
    uint32_t guest_period_size{1024};
    uint32_t guest_periods{4};
    uint32_t guest_frame_size{4}; // 2 ch * 2 bytes (S16)
    PcmState state{SNDRV_PCM_STATE_OPEN};

    // Circular ring buffer for guest PCM frames
    std::vector<uint8_t> ring_buffer{};
    size_t ring_head{0};
    size_t ring_tail{0};
    size_t ring_available{0};

    // Statistics
    std::atomic<uint64_t> total_frames_written{0};
    std::atomic<uint64_t> total_frames_rendered{0};

    // Threading & playback engine
    std::atomic<bool> is_running{false};
    std::atomic<bool> stop_requested{false};
    std::thread worker_thread{};

    // WASAPI COM components
    bool com_initialized{false};
    bool is_null_sink{false};
    bool force_null_sink{false};
    IMMDeviceEnumerator *enumerator{nullptr};
    IMMDevice *device{nullptr};
    IAudioClient *audio_client{nullptr};
    IAudioRenderClient *render_client{nullptr};
    WAVEFORMATEX *mix_format{nullptr};
    UINT32 buffer_frame_count{0};
    bool host_is_float{true};
    uint32_t host_rate{48000};
    uint32_t host_channels{2};

    Impl()
    {
        ring_buffer.resize(256 * 1024, 0); // 256 KB default ring
    }

    ~Impl()
    {
        StopPlayback();
        CleanupWasapi();
    }

    void InitWasapi()
    {
        if (force_null_sink) {
            is_null_sink = true;
            return;
        }

        if (audio_client) {
            return;
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
            com_initialized = true;
        }

        hr =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
        if (FAILED(hr) || !enumerator) {
            is_null_sink = true;
            return;
        }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr) || !device) {
            is_null_sink = true;
            return;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&audio_client));
        if (FAILED(hr) || !audio_client) {
            is_null_sink = true;
            return;
        }

        hr = audio_client->GetMixFormat(&mix_format);
        if (FAILED(hr) || !mix_format) {
            is_null_sink = true;
            return;
        }

        host_rate = mix_format->nSamplesPerSec;
        host_channels = mix_format->nChannels;

        if (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            host_is_float = true;
        } else if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(mix_format);
            host_is_float =
                (std::memcmp(&ext->SubFormat, &PORTAGE_SUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0);
        } else {
            host_is_float = false;
        }

        REFERENCE_TIME duration = 500000; // 50ms in 100ns units
        hr =
            audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, duration, 0, mix_format, nullptr);
        if (FAILED(hr)) {
            is_null_sink = true;
            return;
        }

        hr = audio_client->GetBufferSize(&buffer_frame_count);
        if (FAILED(hr)) {
            is_null_sink = true;
            return;
        }

        hr = audio_client->GetService(__uuidof(IAudioRenderClient),
                                      reinterpret_cast<void **>(&render_client));
        if (FAILED(hr) || !render_client) {
            is_null_sink = true;
            return;
        }

        is_null_sink = false;
    }

    void CleanupWasapi()
    {
        if (render_client) {
            render_client->Release();
            render_client = nullptr;
        }
        if (mix_format) {
            CoTaskMemFree(mix_format);
            mix_format = nullptr;
        }
        if (audio_client) {
            audio_client->Release();
            audio_client = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
        if (enumerator) {
            enumerator->Release();
            enumerator = nullptr;
        }
        if (com_initialized) {
            CoUninitialize();
            com_initialized = false;
        }
    }

    void StartPlayback()
    {
        if (is_running) {
            return;
        }

        InitWasapi();

        stop_requested = false;
        is_running = true;

        if (!is_null_sink && audio_client) {
            audio_client->Start();
        }

        worker_thread = std::thread([this] { PlaybackWorker(); });
    }

    void StopPlayback()
    {
        if (!is_running) {
            return;
        }

        stop_requested = true;
        cv.notify_all();
        cv_write.notify_all();

        if (worker_thread.joinable()) {
            if (worker_thread.get_id() != std::this_thread::get_id()) {
                worker_thread.join();
            }
        }

        if (!is_null_sink && audio_client) {
            audio_client->Stop();
        }

        is_running = false;
    }

    void ResetRingBuffer()
    {
        ring_head = 0;
        ring_tail = 0;
        ring_available = 0;
        std::fill(ring_buffer.begin(), ring_buffer.end(), 0);
    }

    void PlaybackWorker()
    {
        while (!stop_requested) {
            if (is_null_sink) {
                // Drain ring buffer paced to guest sample rate
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::milliseconds(10),
                            [this] { return stop_requested.load(); });
                if (stop_requested) {
                    break;
                }

                size_t drain_frames = (guest_rate * 10) / 1000;
                if (drain_frames == 0)
                    drain_frames = 1;
                size_t drain_bytes = drain_frames * guest_frame_size;
                size_t to_drain = std::min(drain_bytes, ring_available);

                if (to_drain > 0) {
                    ring_tail = (ring_tail + to_drain) % ring_buffer.size();
                    ring_available -= to_drain;
                    total_frames_rendered += (to_drain / guest_frame_size);
                    cv_write.notify_all();
                }
            } else {
                // WASAPI shared mode render loop
                UINT32 padding = 0;
                HRESULT hr = audio_client->GetCurrentPadding(&padding);
                if (FAILED(hr)) {
                    break;
                }

                UINT32 frames_needed = buffer_frame_count - padding;
                if (frames_needed > 0) {
                    BYTE *pData = nullptr;
                    hr = render_client->GetBuffer(frames_needed, &pData);
                    if (SUCCEEDED(hr)) {
                        RenderFrames(pData, frames_needed);
                        render_client->ReleaseBuffer(frames_needed, 0);
                    }
                }

                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::milliseconds(5),
                            [this] { return stop_requested.load(); });
            }
        }
    }

    float ReadGuestSample(const uint8_t *frame_ptr, uint32_t channel) const
    {
        if (channel >= guest_channels) {
            channel = guest_channels - 1;
        }

        if (guest_format == SNDRV_PCM_FORMAT_S16_LE) {
            const auto *s16 = reinterpret_cast<const int16_t *>(frame_ptr);
            return static_cast<float>(s16[channel]) / 32768.0f;
        }
        if (guest_format == SNDRV_PCM_FORMAT_FLOAT_LE) {
            const auto *flt = reinterpret_cast<const float *>(frame_ptr);
            return flt[channel];
        }
        if (guest_format == SNDRV_PCM_FORMAT_S32_LE) {
            const auto *s32 = reinterpret_cast<const int32_t *>(frame_ptr);
            return static_cast<float>(s32[channel]) / 2147483648.0f;
        }
        if (guest_format == SNDRV_PCM_FORMAT_U8) {
            const auto *u8 = frame_ptr;
            return (static_cast<float>(u8[channel]) - 128.0f) / 128.0f;
        }

        return 0.0f;
    }

    void RenderFrames(BYTE *out_buffer, UINT32 frames_needed)
    {
        std::lock_guard<std::mutex> lock(mutex);

        size_t available_frames = ring_available / guest_frame_size;
        if (available_frames == 0) {
            // Buffer underrun: fill with silence
            UINT32 out_bytes =
                frames_needed * host_channels * (host_is_float ? sizeof(float) : sizeof(int16_t));
            std::memset(out_buffer, 0, out_bytes);
            return;
        }

        // Resample and convert from ring buffer
        for (UINT32 i = 0; i < frames_needed; ++i) {
            double src_pos = static_cast<double>(i) * guest_rate / host_rate;
            size_t idx0 = static_cast<size_t>(src_pos);
            size_t idx1 = idx0 + 1;
            float frac = static_cast<float>(src_pos - idx0);

            float left = 0.0f;
            float right = 0.0f;

            if (idx0 < available_frames) {
                size_t byte_off0 = (ring_tail + idx0 * guest_frame_size) % ring_buffer.size();
                const uint8_t *ptr0 = &ring_buffer[byte_off0];

                float l0 = ReadGuestSample(ptr0, 0);
                float r0 = (guest_channels > 1) ? ReadGuestSample(ptr0, 1) : l0;

                if (idx1 < available_frames) {
                    size_t byte_off1 = (ring_tail + idx1 * guest_frame_size) % ring_buffer.size();
                    const uint8_t *ptr1 = &ring_buffer[byte_off1];

                    float l1 = ReadGuestSample(ptr1, 0);
                    float r1 = (guest_channels > 1) ? ReadGuestSample(ptr1, 1) : l1;

                    left = l0 * (1.0f - frac) + l1 * frac;
                    right = r0 * (1.0f - frac) + r1 * frac;
                } else {
                    left = l0;
                    right = r0;
                }
            }

            // Write to host output buffer
            if (host_is_float) {
                auto *f_out = reinterpret_cast<float *>(out_buffer) + (i * host_channels);
                f_out[0] = left;
                if (host_channels > 1) {
                    f_out[1] = right;
                }
                for (uint32_t c = 2; c < host_channels; ++c) {
                    f_out[c] = 0.0f;
                }
            } else {
                auto *s_out = reinterpret_cast<int16_t *>(out_buffer) + (i * host_channels);
                s_out[0] = static_cast<int16_t>(std::clamp(left, -1.0f, 1.0f) * 32767.0f);
                if (host_channels > 1) {
                    s_out[1] = static_cast<int16_t>(std::clamp(right, -1.0f, 1.0f) * 32767.0f);
                }
                for (uint32_t c = 2; c < host_channels; ++c) {
                    s_out[c] = 0;
                }
            }
        }

        // Advance ring buffer tail
        double consumed_guest_frames = static_cast<double>(frames_needed) * guest_rate / host_rate;
        size_t frames_to_consume =
            std::min(available_frames, static_cast<size_t>(consumed_guest_frames));
        size_t bytes_to_consume = frames_to_consume * guest_frame_size;

        ring_tail = (ring_tail + bytes_to_consume) % ring_buffer.size();
        ring_available -= bytes_to_consume;
        total_frames_rendered += frames_to_consume;
        cv_write.notify_all();
    }
};

AudioManager::AudioManager() : m_impl(std::make_unique<Impl>()) {}

AudioManager::~AudioManager() = default;

AudioManager &AudioManager::Instance() noexcept
{
    static AudioManager instance;
    return instance;
}

void AudioManager::ForceNullSink(bool enable)
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->force_null_sink = enable;
    if (enable) {
        m_impl->is_null_sink = true;
    }
}

bool AudioManager::ConfigureStream(uint32_t sample_rate, uint32_t channels, int32_t format)
{
    m_impl->StopPlayback();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->guest_rate = (sample_rate > 0) ? sample_rate : 48000;
    m_impl->guest_channels = (channels >= 1 && channels <= 2) ? channels : 2;
    m_impl->guest_format = format;
    uint32_t sample_bytes = (format == SNDRV_PCM_FORMAT_S16_LE) ? 2 : 4;
    m_impl->guest_frame_size = m_impl->guest_channels * sample_bytes;
    m_impl->ResetRingBuffer();
    m_impl->state = SNDRV_PCM_STATE_SETUP;
    return true;
}

void AudioManager::CloseStream()
{
    m_impl->StopPlayback();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->state = SNDRV_PCM_STATE_OPEN;
}

void AudioManager::Reset()
{
    m_impl->StopPlayback();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->ResetRingBuffer();
    m_impl->guest_rate = 48000;
    m_impl->guest_channels = 2;
    m_impl->guest_format = SNDRV_PCM_FORMAT_S16_LE;
    m_impl->guest_period_size = 1024;
    m_impl->guest_periods = 4;
    m_impl->guest_frame_size = 4;
    m_impl->total_frames_written = 0;
    m_impl->total_frames_rendered = 0;
    m_impl->state = SNDRV_PCM_STATE_OPEN;
}

AudioStats AudioManager::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return AudioStats{.total_frames_written = m_impl->total_frames_written.load(),
                      .total_frames_rendered = m_impl->total_frames_rendered.load(),
                      .sample_rate = m_impl->guest_rate,
                      .channels = m_impl->guest_channels,
                      .format = m_impl->guest_format,
                      .current_state = m_impl->state,
                      .is_null_sink = m_impl->is_null_sink,
                      .is_running = m_impl->is_running.load()};
}

int64_t AudioManager::WritePcmBytes(const void *buf, size_t count)
{
    if (!buf) {
        return LINUX_EFAULT;
    }

    std::unique_lock<std::mutex> lock(m_impl->mutex);

    if (m_impl->state == SNDRV_PCM_STATE_PREPARED || m_impl->state == SNDRV_PCM_STATE_SETUP ||
        m_impl->state == SNDRV_PCM_STATE_OPEN) {
        m_impl->state = SNDRV_PCM_STATE_RUNNING;
        m_impl->StartPlayback();
    } else if (m_impl->state != SNDRV_PCM_STATE_RUNNING) {
        return LINUX_EBADFD;
    }

    const auto *src = static_cast<const uint8_t *>(buf);
    size_t written = 0;

    while (written < count) {
        size_t free_space = m_impl->ring_buffer.size() - m_impl->ring_available;
        if (free_space == 0) {
            // Drop oldest frames to prevent deadlock, advance tail
            size_t drop = std::min<size_t>(m_impl->guest_frame_size * 256, m_impl->ring_available);
            m_impl->ring_tail = (m_impl->ring_tail + drop) % m_impl->ring_buffer.size();
            m_impl->ring_available -= drop;
            free_space += drop;
        }

        size_t chunk = std::min(count - written, free_space);
        size_t first_part = std::min(chunk, m_impl->ring_buffer.size() - m_impl->ring_head);
        std::memcpy(&m_impl->ring_buffer[m_impl->ring_head], src + written, first_part);

        if (chunk > first_part) {
            std::memcpy(&m_impl->ring_buffer[0], src + written + first_part, chunk - first_part);
        }

        m_impl->ring_head = (m_impl->ring_head + chunk) % m_impl->ring_buffer.size();
        m_impl->ring_available += chunk;
        written += chunk;
    }

    uint64_t frames = count / m_impl->guest_frame_size;
    m_impl->total_frames_written += frames;

    m_impl->cv.notify_one();
    return static_cast<int64_t>(count);
}

int64_t AudioManager::WriteiFrames(const void *buf, uint64_t frames)
{
    if (!buf) {
        return LINUX_EFAULT;
    }
    size_t bytes = frames * m_impl->guest_frame_size;
    int64_t written_bytes = WritePcmBytes(buf, bytes);
    if (written_bytes < 0) {
        return written_bytes;
    }
    return static_cast<int64_t>(frames);
}

int64_t AudioManager::HandlePcmIoctl(uint32_t cmd, void *arg)
{
    uint32_t nr = cmd & 0xFF;

    if (cmd == SNDRV_PCM_IOCTL_PVERSION || nr == 0x00) {
        if (!arg)
            return LINUX_EFAULT;
        *reinterpret_cast<uint32_t *>(arg) = SNDRV_PCM_VERSION;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_INFO || nr == 0x01) {
        if (!arg)
            return LINUX_EFAULT;
        auto *info = reinterpret_cast<snd_pcm_info *>(arg);
        std::memset(info, 0, sizeof(snd_pcm_info));
        info->device = 0;
        info->subdevice = 0;
        info->stream = SNDRV_PCM_STREAM_PLAYBACK;
        info->card = 0;
        std::strncpy(reinterpret_cast<char *>(info->id), "PortagePCM", sizeof(info->id) - 1);
        std::strncpy(reinterpret_cast<char *>(info->name), "Portage ALSA PCM Playback",
                     sizeof(info->name) - 1);
        std::strncpy(reinterpret_cast<char *>(info->subname), "subdevice #0",
                     sizeof(info->subname) - 1);
        info->subdevices_count = 1;
        info->subdevices_avail = 1;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_HW_REFINE || nr == 0x10) {
        if (!arg)
            return LINUX_EFAULT;
        auto *params = reinterpret_cast<snd_pcm_hw_params *>(arg);
        params->masks[SNDRV_PCM_HW_PARAM_ACCESS].bits[0] =
            (1u << SNDRV_PCM_ACCESS_RW_INTERLEAVED) | (1u << SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
        params->masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0] = (1u << SNDRV_PCM_FORMAT_S16_LE) |
                                                           (1u << SNDRV_PCM_FORMAT_FLOAT_LE) |
                                                           (1u << SNDRV_PCM_FORMAT_S32_LE);
        params->masks[SNDRV_PCM_HW_PARAM_SUBFORMAT].bits[0] = 1u;

        params->intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min = 1;
        params->intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].max = 2;
        params->intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min = 8000;
        params->intervals[SNDRV_PCM_HW_PARAM_RATE - 3].max = 96000;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min = 64;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].max = 65536;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min = 2;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].max = 16;
        params->intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - 3].min = 128;
        params->intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - 3].max = 131072;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_HW_PARAMS || nr == 0x11) {
        if (!arg)
            return LINUX_EFAULT;
        auto *params = reinterpret_cast<snd_pcm_hw_params *>(arg);

        std::lock_guard<std::mutex> lock(m_impl->mutex);

        // Detect requested format
        uint32_t format_mask = params->masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0];
        if (format_mask & (1u << SNDRV_PCM_FORMAT_FLOAT_LE)) {
            m_impl->guest_format = SNDRV_PCM_FORMAT_FLOAT_LE;
        } else if (format_mask & (1u << SNDRV_PCM_FORMAT_S32_LE)) {
            m_impl->guest_format = SNDRV_PCM_FORMAT_S32_LE;
        } else {
            m_impl->guest_format = SNDRV_PCM_FORMAT_S16_LE;
        }

        // Channels
        uint32_t ch = params->intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min;
        m_impl->guest_channels = (ch >= 1 && ch <= 2) ? ch : 2;

        // Rate
        uint32_t rate = params->intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min;
        m_impl->guest_rate = (rate >= 8000 && rate <= 192000) ? rate : 48000;

        // Period size & periods
        uint32_t psize = params->intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min;
        m_impl->guest_period_size = (psize > 0) ? psize : 1024;

        uint32_t periods = params->intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min;
        m_impl->guest_periods = (periods > 0) ? periods : 4;

        uint32_t sample_bytes = (m_impl->guest_format == SNDRV_PCM_FORMAT_S16_LE) ? 2 : 4;
        m_impl->guest_frame_size = m_impl->guest_channels * sample_bytes;

        // Echo back confirmed parameters
        params->masks[SNDRV_PCM_HW_PARAM_ACCESS].bits[0] = (1u << SNDRV_PCM_ACCESS_RW_INTERLEAVED);
        params->masks[SNDRV_PCM_HW_PARAM_FORMAT].bits[0] = (1u << m_impl->guest_format);
        params->intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].min = m_impl->guest_channels;
        params->intervals[SNDRV_PCM_HW_PARAM_CHANNELS - 3].max = m_impl->guest_channels;
        params->intervals[SNDRV_PCM_HW_PARAM_RATE - 3].min = m_impl->guest_rate;
        params->intervals[SNDRV_PCM_HW_PARAM_RATE - 3].max = m_impl->guest_rate;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].min = m_impl->guest_period_size;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - 3].max = m_impl->guest_period_size;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].min = m_impl->guest_periods;
        params->intervals[SNDRV_PCM_HW_PARAM_PERIODS - 3].max = m_impl->guest_periods;
        params->intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - 3].min =
            m_impl->guest_period_size * m_impl->guest_periods;
        params->intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - 3].max =
            m_impl->guest_period_size * m_impl->guest_periods;

        size_t required_ring_bytes =
            m_impl->guest_period_size * m_impl->guest_periods * m_impl->guest_frame_size * 4;
        if (required_ring_bytes > m_impl->ring_buffer.size()) {
            m_impl->ring_buffer.resize(required_ring_bytes, 0);
        }

        m_impl->state = SNDRV_PCM_STATE_SETUP;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_SW_PARAMS || nr == 0x13) {
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_STATUS || nr == 0x20) {
        if (!arg)
            return LINUX_EFAULT;
        auto *status = reinterpret_cast<snd_pcm_status *>(arg);
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        std::memset(status, 0, sizeof(snd_pcm_status));
        status->state = m_impl->state;
        status->delay = static_cast<int64_t>(m_impl->ring_available / m_impl->guest_frame_size);
        status->avail =
            (m_impl->ring_buffer.size() - m_impl->ring_available) / m_impl->guest_frame_size;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_DELAY || nr == 0x21) {
        if (!arg)
            return LINUX_EFAULT;
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        *reinterpret_cast<int64_t *>(arg) =
            static_cast<int64_t>(m_impl->ring_available / m_impl->guest_frame_size);
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_PREPARE || nr == 0x40) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->ResetRingBuffer();
        m_impl->state = SNDRV_PCM_STATE_PREPARED;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_RESET || nr == 0x41) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->ResetRingBuffer();
        m_impl->state = SNDRV_PCM_STATE_SETUP;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_START || nr == 0x42) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->state = SNDRV_PCM_STATE_RUNNING;
        m_impl->StartPlayback();
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_DROP || nr == 0x43) {
        m_impl->StopPlayback();
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->ResetRingBuffer();
        m_impl->state = SNDRV_PCM_STATE_SETUP;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_DRAIN || nr == 0x44) {
        {
            std::unique_lock<std::mutex> lock(m_impl->mutex);
            if (m_impl->state == SNDRV_PCM_STATE_RUNNING) {
                m_impl->cv_write.wait_for(lock, std::chrono::milliseconds(100),
                                          [this] { return m_impl->ring_available == 0; });
            }
        }
        m_impl->StopPlayback();
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->state = SNDRV_PCM_STATE_SETUP;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_PAUSE || nr == 0x45) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->state = SNDRV_PCM_STATE_PAUSED;
        return 0;
    }

    if (cmd == SNDRV_PCM_IOCTL_WRITEI_FRAMES || nr == 0x50) {
        if (!arg)
            return LINUX_EFAULT;
        auto *xferi = reinterpret_cast<snd_xferi *>(arg);
        if (!xferi->buf)
            return LINUX_EFAULT;
        int64_t res = WriteiFrames(xferi->buf, xferi->frames);
        if (res >= 0) {
            xferi->result = res;
            return res;
        }
        return res;
    }

    return LINUX_ENOTTY;
}

int64_t AudioManager::HandleControlIoctl(uint32_t cmd, void *arg)
{
    uint32_t nr = cmd & 0xFF;

    if (cmd == SNDRV_CTL_IOCTL_PVERSION || nr == 0x00) {
        if (!arg)
            return LINUX_EFAULT;
        *reinterpret_cast<uint32_t *>(arg) = SNDRV_CTL_VERSION;
        return 0;
    }

    if (cmd == SNDRV_CTL_IOCTL_CARD_INFO || nr == 0x01) {
        if (!arg)
            return LINUX_EFAULT;
        auto *info = reinterpret_cast<snd_ctl_card_info *>(arg);
        std::memset(info, 0, sizeof(snd_ctl_card_info));
        info->card = 0;
        std::strncpy(reinterpret_cast<char *>(info->id), "PortageCard0", sizeof(info->id) - 1);
        std::strncpy(reinterpret_cast<char *>(info->driver), "PortageWASAPI",
                     sizeof(info->driver) - 1);
        std::strncpy(reinterpret_cast<char *>(info->name), "Portage Audio", sizeof(info->name) - 1);
        std::strncpy(reinterpret_cast<char *>(info->longname), "Portage WASAPI / ALSA Sound Card",
                     sizeof(info->longname) - 1);
        std::strncpy(reinterpret_cast<char *>(info->mixername), "Portage Mixer",
                     sizeof(info->mixername) - 1);
        std::strncpy(reinterpret_cast<char *>(info->components), "WASAPI PCM",
                     sizeof(info->components) - 1);
        return 0;
    }

    return LINUX_ENOTTY;
}

} // namespace portage::audio
