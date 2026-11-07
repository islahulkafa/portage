#pragma once

#include "fs/dirent.hpp"
#include "fs/vfs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace portage::fs {

struct FileDescriptor {
    int fd{-1};
    HANDLE handle{INVALID_HANDLE_VALUE};
    int flags{0};
    DeviceType dev{DeviceType::Regular};
    uint64_t offset{0};
    std::string guest_path{};
    bool is_pipe_or_console{false};

    // Synthetic virtual file support
    std::string synthetic_data{};

    // Directory enumeration support
    std::vector<DirEntry> dir_entries{};
    size_t dir_index{0};

    // Epoll instance ID
    int epoll_id{-1};

    // Binder instance ID
    int binder_id{-1};

    // Input device index (0 = keyboard, 1 = touchscreen)
    int input_device_index{-1};
};

class FileDescriptorTable {
  public:
    FileDescriptorTable();
    ~FileDescriptorTable();

    FileDescriptorTable(const FileDescriptorTable &) = delete;
    FileDescriptorTable &operator=(const FileDescriptorTable &) = delete;

    int Allocate(HANDLE handle, int flags, DeviceType dev = DeviceType::Regular,
                 std::string_view path = "", bool is_pipe_or_console = false);

    int AllocateSynthetic(int flags, DeviceType dev, std::string_view path, std::string data);

    int AllocateDirectory(int flags, std::string_view path, std::vector<DirEntry> entries);

    int AllocateSocket(UINT_PTR s, int flags);

    int AllocateEpoll(int epoll_id, int flags);

    int AllocateBinder(int binder_id, DeviceType dev, int flags);

    int AllocateAudio(DeviceType dev, int flags);

    int AllocateInput(int device_index, int flags);

    int AllocateInotify(int flags);

    int CreatePipe(int fds[2], int flags = 0);

    [[nodiscard]] std::shared_ptr<FileDescriptor> Get(int fd);

    int Close(int fd);

    int Duplicate(int oldfd, int target_fd = -1, int flags = 0);

    void CloseOnExec();

  private:
    std::mutex m_mutex{};
    std::unordered_map<int, std::shared_ptr<FileDescriptor>> m_descriptors{};
    int m_next_fd{3};
};

} // namespace portage::fs
