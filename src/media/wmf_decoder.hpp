#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "media/media_format.hpp"
#include "media/media_types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace portage::media {

struct MediaTrackInfo {
    size_t index{0};
    std::string mime{};
    int32_t width{0};
    int32_t height{0};
    int32_t stride{0};
    float frame_rate{30.0f};
    int32_t sample_rate{44100};
    int32_t channel_count{2};
    int64_t duration_us{0};
    std::vector<uint8_t> csd0{};
};

class WmfManager {
  public:
    static WmfManager &Instance() noexcept;

    bool Initialize() noexcept;
    void Shutdown() noexcept;

    void DispatchSync(std::function<void()> task);

  private:
    WmfManager() = default;
    ~WmfManager();

    WmfManager(const WmfManager &) = delete;
    WmfManager &operator=(const WmfManager &) = delete;

    void WorkerLoop();

    std::atomic<bool> initialized_{false};
    std::mutex mutex_{};
    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    std::condition_variable done_cv_{};
    std::deque<std::function<void()>> queue_{};
    bool stop_worker_{false};
    std::thread worker_{};
};

class WmfSourceReaderWrapper {
  public:
    WmfSourceReaderWrapper() = default;
    ~WmfSourceReaderWrapper();

    bool OpenUrl(const std::wstring &url);
    bool OpenFile(const std::string &path);
    bool OpenFd(int fd, int64_t offset, int64_t length);
    bool OpenSynthetic(std::string_view mime, int32_t width = 128, int32_t height = 128,
                       int64_t duration_us = 5000000);
    void Close();

    [[nodiscard]] size_t GetTrackCount() const noexcept;
    [[nodiscard]] const MediaTrackInfo *GetTrack(size_t index) const noexcept;

    bool SelectTrack(size_t index);
    bool UnselectTrack(size_t index);

    int64_t ReadSample(uint8_t *buffer, size_t capacity, size_t *out_track, int64_t *out_pts_us,
                       uint32_t *out_flags);
    bool SeekTo(int64_t time_us);

  private:
    void ProbeTracks();

    IMFSourceReader *reader_{nullptr};
    std::vector<MediaTrackInfo> tracks_{};
    std::vector<bool> selected_tracks_{};
    size_t current_selected_track_{0};
    int64_t current_pts_us_{0};
    bool is_synthetic_{false};
    size_t synthetic_frame_index_{0};
};

} // namespace portage::media
