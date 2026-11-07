#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "fs/fd_table.hpp"
#include "audio/audio_manager.hpp"
#include "binder/binder_driver.hpp"
#include "poll/epoll_manager.hpp"
#include <windows.h>
#include <winsock2.h>

namespace portage::fs {

FileDescriptorTable::FileDescriptorTable()
{
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE h_err = GetStdHandle(STD_ERROR_HANDLE);

    auto fd0 = std::make_shared<FileDescriptor>();
    fd0->fd = 0;
    fd0->handle = h_in;
    fd0->flags = 0; // O_RDONLY
    fd0->dev = DeviceType::Regular;
    fd0->is_pipe_or_console = true;
    m_descriptors[0] = std::move(fd0);

    auto fd1 = std::make_shared<FileDescriptor>();
    fd1->fd = 1;
    fd1->handle = h_out;
    fd1->flags = 1; // O_WRONLY
    fd1->dev = DeviceType::Regular;
    fd1->is_pipe_or_console = true;
    m_descriptors[1] = std::move(fd1);

    auto fd2 = std::make_shared<FileDescriptor>();
    fd2->fd = 2;
    fd2->handle = h_err;
    fd2->flags = 1; // O_WRONLY
    fd2->dev = DeviceType::Regular;
    fd2->is_pipe_or_console = true;
    m_descriptors[2] = std::move(fd2);
}

FileDescriptorTable::~FileDescriptorTable()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[fd, desc] : m_descriptors) {
        if (fd > 2 && desc && desc->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(desc->handle);
            desc->handle = INVALID_HANDLE_VALUE;
        }
    }
    m_descriptors.clear();
}

int FileDescriptorTable::Allocate(HANDLE handle, int flags, DeviceType dev, std::string_view path,
                                  bool is_pipe_or_console)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = handle;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->is_pipe_or_console = is_pipe_or_console;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSynthetic(int flags, DeviceType dev, std::string_view path,
                                           std::string data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->synthetic_data = std::move(data);
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateDirectory(int flags, std::string_view path,
                                           std::vector<DirEntry> entries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Directory;
    desc->offset = 0;
    desc->guest_path = std::string(path);
    desc->dir_entries = std::move(entries);
    desc->dir_index = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSocket(UINT_PTR s, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = reinterpret_cast<HANDLE>(s);
    desc->flags = flags;
    desc->dev = DeviceType::Socket;
    desc->offset = 0;
    desc->is_pipe_or_console = true;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateEpoll(int epoll_id, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Epoll;
    desc->epoll_id = epoll_id;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateBinder(int binder_id, DeviceType dev, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->binder_id = binder_id;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateAudio(DeviceType dev, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateInput(int device_index, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = (device_index == 0) ? DeviceType::InputEvent0 : DeviceType::InputEvent1;
    desc->input_device_index = device_index;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateInotify(int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = DeviceType::Inotify;
    desc->offset = 0;
    desc->is_pipe_or_console = false;

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::AllocateSharedMemory(std::shared_ptr<portage::mem::SharedMemoryRegion> shm,
                                              DeviceType dev, int flags, std::string_view path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int assigned_fd = m_next_fd++;
    while (m_descriptors.contains(assigned_fd)) {
        assigned_fd = m_next_fd++;
    }

    auto desc = std::make_shared<FileDescriptor>();
    desc->fd = assigned_fd;
    desc->handle = INVALID_HANDLE_VALUE;
    desc->flags = flags;
    desc->dev = dev;
    desc->guest_path = std::string(path);
    desc->shared_memory = std::move(shm);

    m_descriptors[assigned_fd] = std::move(desc);
    return assigned_fd;
}

int FileDescriptorTable::CreatePipe(int fds[2], int flags)
{
    if (!fds) {
        return -14; // -EFAULT
    }

    HANDLE h_read = INVALID_HANDLE_VALUE;
    HANDLE h_write = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!::CreatePipe(&h_read, &h_write, &sa, 0)) {
        return -24; // -EMFILE
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    int r_fd = m_next_fd++;
    while (m_descriptors.contains(r_fd)) {
        r_fd = m_next_fd++;
    }

    auto r_desc = std::make_shared<FileDescriptor>();
    r_desc->fd = r_fd;
    r_desc->handle = h_read;
    r_desc->flags = 0 | flags; // O_RDONLY
    r_desc->dev = DeviceType::Pipe;
    r_desc->is_pipe_or_console = true;
    m_descriptors[r_fd] = std::move(r_desc);

    int w_fd = m_next_fd++;
    while (m_descriptors.contains(w_fd)) {
        w_fd = m_next_fd++;
    }

    auto w_desc = std::make_shared<FileDescriptor>();
    w_desc->fd = w_fd;
    w_desc->handle = h_write;
    w_desc->flags = 1 | flags; // O_WRONLY
    w_desc->dev = DeviceType::Pipe;
    w_desc->is_pipe_or_console = true;
    m_descriptors[w_fd] = std::move(w_desc);

    fds[0] = r_fd;
    fds[1] = w_fd;
    return 0;
}

std::shared_ptr<FileDescriptor> FileDescriptorTable::Get(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(fd);
    if (it != m_descriptors.end()) {
        return it->second;
    }
    return nullptr;
}

int FileDescriptorTable::Close(int fd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(fd);
    if (it == m_descriptors.end()) {
        return -9; // -EBADF
    }

    auto desc = it->second;
    m_descriptors.erase(it);

    if (desc) {
        if (desc->dev == DeviceType::Socket) {
            if (desc->handle != INVALID_HANDLE_VALUE) {
                ::closesocket(reinterpret_cast<SOCKET>(desc->handle));
                desc->handle = INVALID_HANDLE_VALUE;
            }
        } else if (desc->dev == DeviceType::Epoll) {
            poll::EpollManager::Instance().CloseInstance(desc->epoll_id);
        } else if (desc->dev == DeviceType::Binder || desc->dev == DeviceType::HwBinder) {
            binder::BinderDriver::Instance().CloseInstance(desc->binder_id);
        } else if (desc->dev == DeviceType::AlsaPcm) {
            audio::AudioManager::Instance().CloseStream();
        } else if (fd > 2 && desc->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(desc->handle);
            desc->handle = INVALID_HANDLE_VALUE;
        }
    }

    return 0;
}

int FileDescriptorTable::Duplicate(int oldfd, int target_fd, int flags)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_descriptors.find(oldfd);
    if (it == m_descriptors.end() || !it->second) {
        return -9; // -EBADF
    }

    auto old_desc = it->second;
    HANDLE dup_handle = INVALID_HANDLE_VALUE;

    if (old_desc->handle != INVALID_HANDLE_VALUE) {
        HANDLE current_proc = GetCurrentProcess();
        if (!DuplicateHandle(current_proc, old_desc->handle, current_proc, &dup_handle, 0, TRUE,
                             DUPLICATE_SAME_ACCESS)) {
            return -9; // -EBADF
        }
    }

    int assigned_fd = target_fd;
    if (assigned_fd < 0) {
        assigned_fd = m_next_fd++;
        while (m_descriptors.contains(assigned_fd)) {
            assigned_fd = m_next_fd++;
        }
    } else {
        auto existing = m_descriptors.find(assigned_fd);
        if (existing != m_descriptors.end()) {
            if (existing->second) {
                if (existing->second->dev == DeviceType::Socket) {
                    if (existing->second->handle != INVALID_HANDLE_VALUE) {
                        ::closesocket(reinterpret_cast<SOCKET>(existing->second->handle));
                    }
                } else if (existing->second->dev == DeviceType::Epoll) {
                    poll::EpollManager::Instance().CloseInstance(existing->second->epoll_id);
                } else if (existing->second->dev == DeviceType::Binder ||
                           existing->second->dev == DeviceType::HwBinder) {
                    binder::BinderDriver::Instance().CloseInstance(existing->second->binder_id);
                } else if (assigned_fd > 2 && existing->second->handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(existing->second->handle);
                }
            }
            m_descriptors.erase(existing);
        }
    }

    auto new_desc = std::make_shared<FileDescriptor>();
    new_desc->fd = assigned_fd;
    new_desc->handle = dup_handle;
    new_desc->flags = old_desc->flags | flags;
    new_desc->dev = old_desc->dev;
    new_desc->offset = old_desc->offset;
    new_desc->guest_path = old_desc->guest_path;
    new_desc->is_pipe_or_console = old_desc->is_pipe_or_console;
    new_desc->synthetic_data = old_desc->synthetic_data;
    new_desc->dir_entries = old_desc->dir_entries;
    new_desc->dir_index = old_desc->dir_index;
    new_desc->epoll_id = old_desc->epoll_id;
    new_desc->binder_id = old_desc->binder_id;
    new_desc->input_device_index = old_desc->input_device_index;
    new_desc->shared_memory = old_desc->shared_memory;

    m_descriptors[assigned_fd] = std::move(new_desc);
    return assigned_fd;
}

void FileDescriptorTable::CloseOnExec()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int> to_close;
    for (const auto &[fd, desc] : m_descriptors) {
        if (fd > 2 && desc && (desc->flags & 02000000)) { // O_CLOEXEC
            to_close.push_back(fd);
        }
    }
    for (int fd : to_close) {
        auto it = m_descriptors.find(fd);
        if (it != m_descriptors.end()) {
            auto desc = it->second;
            m_descriptors.erase(it);
            if (desc) {
                if (desc->dev == DeviceType::Socket) {
                    if (desc->handle != INVALID_HANDLE_VALUE) {
                        ::closesocket(reinterpret_cast<SOCKET>(desc->handle));
                        desc->handle = INVALID_HANDLE_VALUE;
                    }
                } else if (desc->dev == DeviceType::Epoll) {
                    poll::EpollManager::Instance().CloseInstance(desc->epoll_id);
                } else if (desc->dev == DeviceType::Binder || desc->dev == DeviceType::HwBinder) {
                    binder::BinderDriver::Instance().CloseInstance(desc->binder_id);
                } else if (desc->handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(desc->handle);
                    desc->handle = INVALID_HANDLE_VALUE;
                }
            }
        }
    }
}

} // namespace portage::fs
