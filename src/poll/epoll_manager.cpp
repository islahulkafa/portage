#include "poll/epoll_manager.hpp"
#include "fs/inotify_state.hpp"
#include "input/input_manager.hpp"
#include "net/netlink_socket.hpp"
#include "signal/signal_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#include <algorithm>
#include <chrono>

namespace portage::poll {

EpollManager &EpollManager::Instance() noexcept
{
    static EpollManager instance;
    return instance;
}

int EpollManager::CreateInstance() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int epfd = m_next_epfd++;
    m_instances[epfd] = EpollInstance{.epfd = epfd, .items = {}};
    return epfd;
}

int EpollManager::CloseInstance(int epfd) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(epfd);
    if (it != m_instances.end()) {
        m_instances.erase(it);
        return 0;
    }
    return -9; // -EBADF
}

int EpollManager::Ctl(int epfd, int op, int fd, const LinuxEpollEvent *event) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(epfd);
    if (it == m_instances.end()) {
        return -9; // -EBADF
    }

    auto &instance = it->second;
    if (op == LINUX_EPOLL_CTL_ADD) {
        if (!event)
            return -14; // -EFAULT
        if (instance.items.contains(fd)) {
            return -17; // -EEXIST
        }
        instance.items[fd] = EpollItem{.fd = fd, .events = event->events, .data = event->data};
        return 0;
    }

    if (op == LINUX_EPOLL_CTL_DEL) {
        if (!instance.items.contains(fd)) {
            return -2; // -ENOENT
        }
        instance.items.erase(fd);
        return 0;
    }

    if (op == LINUX_EPOLL_CTL_MOD) {
        if (!event)
            return -14; // -EFAULT
        auto item_it = instance.items.find(fd);
        if (item_it == instance.items.end()) {
            return -2; // -ENOENT
        }
        item_it->second.events = event->events;
        item_it->second.data = event->data;
        return 0;
    }

    return -22; // -EINVAL
}

int EpollManager::Wait(int epfd, LinuxEpollEvent *events, int maxevents, int timeout_ms,
                       fs::FileDescriptorTable *fd_table) noexcept
{
    if (!events || maxevents <= 0) {
        return -22; // -EINVAL
    }

    std::vector<EpollItem> items_copy;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_instances.find(epfd);
        if (it == m_instances.end()) {
            return -9; // -EBADF
        }
        for (const auto &[_, item] : it->second.items) {
            items_copy.push_back(item);
        }
    }

    if (items_copy.empty()) {
        if (timeout_ms > 0) {
            Sleep(static_cast<DWORD>(timeout_ms));
        }
        return 0;
    }

    std::vector<WSAPOLLFD> poll_fds;
    std::vector<size_t> poll_item_indices;
    int ready_count = 0;

    for (size_t i = 0; i < items_copy.size() && ready_count < maxevents; ++i) {
        const auto &item = items_copy[i];
        if (!fd_table)
            continue;

        auto desc = fd_table->Get(item.fd);
        if (!desc)
            continue;

        if (desc->dev == fs::DeviceType::InputEvent0 || desc->dev == fs::DeviceType::InputEvent1) {
            int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
            if (input::InputManager::Instance().HasEvents(dev_idx) &&
                (item.events & LINUX_EPOLLIN)) {
                events[ready_count].events = LINUX_EPOLLIN;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::EventFd && desc->eventfd) {
            uint32_t revents = 0;
            {
                std::lock_guard<std::mutex> ev_lock(desc->eventfd->mutex);
                if (desc->eventfd->counter > 0 && (item.events & LINUX_EPOLLIN)) {
                    revents |= LINUX_EPOLLIN;
                }
                if (desc->eventfd->counter < 0xFFFFFFFFFFFFFFFFULL - 1 &&
                    (item.events & LINUX_EPOLLOUT)) {
                    revents |= LINUX_EPOLLOUT;
                }
            }
            if (revents != 0) {
                events[ready_count].events = revents;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::TimerFd && desc->timerfd) {
            if (desc->timerfd->IsReady() && (item.events & LINUX_EPOLLIN)) {
                events[ready_count].events = LINUX_EPOLLIN;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::SyncFence && desc->sync_fence) {
            if (desc->sync_fence->IsSignaled() && (item.events & LINUX_EPOLLIN)) {
                events[ready_count].events = LINUX_EPOLLIN;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::Signalfd && desc->signalfd) {
            if (signal::SignalManager::Instance().HasPendingSignal(desc->signalfd->mask) &&
                (item.events & LINUX_EPOLLIN)) {
                events[ready_count].events = LINUX_EPOLLIN;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::Inotify && desc->inotify) {
            if (!desc->inotify->IsEmpty() && (item.events & LINUX_EPOLLIN)) {
                events[ready_count].events = LINUX_EPOLLIN;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::Netlink && desc->netlink_socket) {
            uint32_t revents = 0;
            if (desc->netlink_socket->HasData() && (item.events & LINUX_EPOLLIN)) {
                revents |= LINUX_EPOLLIN;
            }
            if (item.events & LINUX_EPOLLOUT) {
                revents |= LINUX_EPOLLOUT;
            }
            if (revents != 0) {
                events[ready_count].events = revents;
                events[ready_count].data = item.data;
                ready_count++;
                continue;
            }
        }

        if (desc->dev == fs::DeviceType::Pipe && desc->handle != INVALID_HANDLE_VALUE) {
            DWORD avail = 0;
            if (PeekNamedPipe(desc->handle, nullptr, 0, nullptr, &avail, nullptr)) {
                if (avail > 0 && (item.events & LINUX_EPOLLIN)) {
                    events[ready_count].events = LINUX_EPOLLIN;
                    events[ready_count].data = item.data;
                    ready_count++;
                    continue;
                }
            }
        }

        if (desc->dev == fs::DeviceType::Socket && desc->handle != INVALID_HANDLE_VALUE) {
            WSAPOLLFD pfd{};
            pfd.fd = reinterpret_cast<SOCKET>(desc->handle);
            pfd.events = 0;
            if (item.events & LINUX_EPOLLIN)
                pfd.events |= (POLLRDNORM | POLLRDBAND);
            if (item.events & LINUX_EPOLLOUT)
                pfd.events |= POLLWRNORM;
            pfd.revents = 0;

            poll_fds.push_back(pfd);
            poll_item_indices.push_back(i);
        }
    }

    if (ready_count > 0) {
        return ready_count;
    }

    if (poll_fds.empty()) {
        if (timeout_ms != 0) {
            auto start = std::chrono::steady_clock::now();
            while (true) {
                for (size_t i = 0; i < items_copy.size() && ready_count < maxevents; ++i) {
                    const auto &item = items_copy[i];
                    auto desc = fd_table->Get(item.fd);
                    if (!desc)
                        continue;
                    if (desc->dev == fs::DeviceType::InputEvent0 ||
                        desc->dev == fs::DeviceType::InputEvent1) {
                        int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
                        if (input::InputManager::Instance().HasEvents(dev_idx) &&
                            (item.events & LINUX_EPOLLIN)) {
                            events[ready_count].events = LINUX_EPOLLIN;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::EventFd && desc->eventfd) {
                        uint32_t revents = 0;
                        {
                            std::lock_guard<std::mutex> ev_lock(desc->eventfd->mutex);
                            if (desc->eventfd->counter > 0 && (item.events & LINUX_EPOLLIN)) {
                                revents |= LINUX_EPOLLIN;
                            }
                            if (desc->eventfd->counter < 0xFFFFFFFFFFFFFFFFULL - 1 &&
                                (item.events & LINUX_EPOLLOUT)) {
                                revents |= LINUX_EPOLLOUT;
                            }
                        }
                        if (revents != 0) {
                            events[ready_count].events = revents;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::TimerFd && desc->timerfd) {
                        if (desc->timerfd->IsReady() && (item.events & LINUX_EPOLLIN)) {
                            events[ready_count].events = LINUX_EPOLLIN;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::SyncFence && desc->sync_fence) {
                        if (desc->sync_fence->IsSignaled() && (item.events & LINUX_EPOLLIN)) {
                            events[ready_count].events = LINUX_EPOLLIN;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Signalfd && desc->signalfd) {
                        if (signal::SignalManager::Instance().HasPendingSignal(
                                desc->signalfd->mask) &&
                            (item.events & LINUX_EPOLLIN)) {
                            events[ready_count].events = LINUX_EPOLLIN;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Inotify && desc->inotify) {
                        if (!desc->inotify->IsEmpty() && (item.events & LINUX_EPOLLIN)) {
                            events[ready_count].events = LINUX_EPOLLIN;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Netlink && desc->netlink_socket) {
                        uint32_t revents = 0;
                        if (desc->netlink_socket->HasData() && (item.events & LINUX_EPOLLIN)) {
                            revents |= LINUX_EPOLLIN;
                        }
                        if (item.events & LINUX_EPOLLOUT) {
                            revents |= LINUX_EPOLLOUT;
                        }
                        if (revents != 0) {
                            events[ready_count].events = revents;
                            events[ready_count].data = item.data;
                            ready_count++;
                        }
                    }
                }
                if (ready_count > 0)
                    return ready_count;
                if (timeout_ms > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
                    if (elapsed >= timeout_ms)
                        break;
                }
                Sleep(1);
            }
        }
        return 0;
    }

    int poll_res = WSAPoll(poll_fds.data(), static_cast<ULONG>(poll_fds.size()), timeout_ms);
    if (poll_res <= 0) {
        return 0;
    }

    for (size_t i = 0; i < poll_fds.size() && ready_count < maxevents; ++i) {
        if (poll_fds[i].revents != 0) {
            uint32_t revents = 0;
            if (poll_fds[i].revents & (POLLRDNORM | POLLRDBAND))
                revents |= LINUX_EPOLLIN;
            if (poll_fds[i].revents & POLLWRNORM)
                revents |= LINUX_EPOLLOUT;
            if (poll_fds[i].revents & POLLERR)
                revents |= LINUX_EPOLLERR;
            if (poll_fds[i].revents & POLLHUP)
                revents |= LINUX_EPOLLHUP;

            size_t orig_idx = poll_item_indices[i];
            events[ready_count].events = revents;
            events[ready_count].data = items_copy[orig_idx].data;
            ready_count++;
        }
    }

    return ready_count;
}

int EpollManager::Poll(LinuxPollFd *fds, size_t nfds, int timeout_ms,
                       fs::FileDescriptorTable *fd_table) noexcept
{
    if (!fds && nfds > 0) {
        return -14; // -EFAULT
    }
    if (nfds == 0) {
        if (timeout_ms > 0) {
            Sleep(static_cast<DWORD>(timeout_ms));
        }
        return 0;
    }

    std::vector<WSAPOLLFD> poll_fds;
    std::vector<size_t> socket_indices;
    int ready_count = 0;

    for (size_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        int fd = fds[i].fd;
        if (fd < 0)
            continue;

        if (!fd_table) {
            fds[i].revents = LINUX_POLLNVAL;
            ready_count++;
            continue;
        }

        auto desc = fd_table->Get(fd);
        if (!desc ||
            (desc->dev != fs::DeviceType::InputEvent0 && desc->dev != fs::DeviceType::InputEvent1 &&
             desc->dev != fs::DeviceType::EventFd && desc->dev != fs::DeviceType::TimerFd &&
             desc->dev != fs::DeviceType::SyncFence && desc->dev != fs::DeviceType::Signalfd &&
             desc->dev != fs::DeviceType::Inotify && desc->handle == INVALID_HANDLE_VALUE)) {
            fds[i].revents = LINUX_POLLNVAL;
            ready_count++;
            continue;
        }

        if (desc->dev == fs::DeviceType::InputEvent0 || desc->dev == fs::DeviceType::InputEvent1) {
            int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
            if (input::InputManager::Instance().HasEvents(dev_idx) &&
                (fds[i].events & LINUX_POLLIN)) {
                fds[i].revents |= LINUX_POLLIN;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::EventFd && desc->eventfd) {
            int16_t revents = 0;
            {
                std::lock_guard<std::mutex> ev_lock(desc->eventfd->mutex);
                if (desc->eventfd->counter > 0 && (fds[i].events & LINUX_POLLIN)) {
                    revents |= LINUX_POLLIN;
                }
                if (desc->eventfd->counter < 0xFFFFFFFFFFFFFFFFULL - 1 &&
                    (fds[i].events & LINUX_POLLOUT)) {
                    revents |= LINUX_POLLOUT;
                }
            }
            if (revents != 0) {
                fds[i].revents |= revents;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::TimerFd && desc->timerfd) {
            if (desc->timerfd->IsReady() && (fds[i].events & LINUX_POLLIN)) {
                fds[i].revents |= LINUX_POLLIN;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::SyncFence && desc->sync_fence) {
            if (desc->sync_fence->IsSignaled() && (fds[i].events & LINUX_POLLIN)) {
                fds[i].revents |= LINUX_POLLIN;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::Signalfd && desc->signalfd) {
            if (signal::SignalManager::Instance().HasPendingSignal(desc->signalfd->mask) &&
                (fds[i].events & LINUX_POLLIN)) {
                fds[i].revents |= LINUX_POLLIN;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::Inotify && desc->inotify) {
            if (!desc->inotify->IsEmpty() && (fds[i].events & LINUX_POLLIN)) {
                fds[i].revents |= LINUX_POLLIN;
                ready_count++;
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::Pipe) {
            DWORD avail = 0;
            if (PeekNamedPipe(desc->handle, nullptr, 0, nullptr, &avail, nullptr)) {
                if (avail > 0 && (fds[i].events & LINUX_POLLIN)) {
                    fds[i].revents |= LINUX_POLLIN;
                    ready_count++;
                }
            }
            continue;
        }

        if (desc->dev == fs::DeviceType::Socket) {
            WSAPOLLFD pfd{};
            pfd.fd = reinterpret_cast<SOCKET>(desc->handle);
            pfd.events = 0;
            if (fds[i].events & LINUX_POLLIN)
                pfd.events |= (POLLRDNORM | POLLRDBAND);
            if (fds[i].events & LINUX_POLLOUT)
                pfd.events |= POLLWRNORM;
            pfd.revents = 0;

            poll_fds.push_back(pfd);
            socket_indices.push_back(i);
        }
    }

    if (ready_count > 0) {
        return ready_count;
    }

    if (poll_fds.empty()) {
        if (timeout_ms != 0) {
            auto start = std::chrono::steady_clock::now();
            while (true) {
                for (size_t i = 0; i < nfds; ++i) {
                    int fd = fds[i].fd;
                    if (fd < 0 || !fd_table)
                        continue;
                    auto desc = fd_table->Get(fd);
                    if (!desc)
                        continue;
                    if (desc->dev == fs::DeviceType::InputEvent0 ||
                        desc->dev == fs::DeviceType::InputEvent1) {
                        int dev_idx = (desc->dev == fs::DeviceType::InputEvent0) ? 0 : 1;
                        if (input::InputManager::Instance().HasEvents(dev_idx) &&
                            (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::EventFd && desc->eventfd) {
                        int16_t revents = 0;
                        {
                            std::lock_guard<std::mutex> ev_lock(desc->eventfd->mutex);
                            if (desc->eventfd->counter > 0 && (fds[i].events & LINUX_POLLIN)) {
                                revents |= LINUX_POLLIN;
                            }
                            if (desc->eventfd->counter < 0xFFFFFFFFFFFFFFFFULL - 1 &&
                                (fds[i].events & LINUX_POLLOUT)) {
                                revents |= LINUX_POLLOUT;
                            }
                        }
                        if (revents != 0) {
                            fds[i].revents |= revents;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::TimerFd && desc->timerfd) {
                        if (desc->timerfd->IsReady() && (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::SyncFence && desc->sync_fence) {
                        if (desc->sync_fence->IsSignaled() && (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Signalfd && desc->signalfd) {
                        if (signal::SignalManager::Instance().HasPendingSignal(
                                desc->signalfd->mask) &&
                            (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Inotify && desc->inotify) {
                        if (!desc->inotify->IsEmpty() && (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                    }
                    if (desc->dev == fs::DeviceType::Netlink && desc->netlink_socket) {
                        if (desc->netlink_socket->HasData() && (fds[i].events & LINUX_POLLIN)) {
                            fds[i].revents |= LINUX_POLLIN;
                            ready_count++;
                        }
                        if (fds[i].events & LINUX_POLLOUT) {
                            fds[i].revents |= LINUX_POLLOUT;
                            ready_count++;
                        }
                    }
                }
                if (ready_count > 0)
                    return ready_count;
                if (timeout_ms > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
                    if (elapsed >= timeout_ms)
                        break;
                }
                Sleep(1);
            }
        }
        return 0;
    }

    int res = WSAPoll(poll_fds.data(), static_cast<ULONG>(poll_fds.size()), timeout_ms);
    if (res <= 0) {
        return 0;
    }

    for (size_t i = 0; i < poll_fds.size(); ++i) {
        if (poll_fds[i].revents != 0) {
            size_t orig_idx = socket_indices[i];
            int16_t revents = 0;
            if (poll_fds[i].revents & (POLLRDNORM | POLLRDBAND))
                revents |= LINUX_POLLIN;
            if (poll_fds[i].revents & POLLWRNORM)
                revents |= LINUX_POLLOUT;
            if (poll_fds[i].revents & POLLERR)
                revents |= LINUX_POLLERR;
            if (poll_fds[i].revents & POLLHUP)
                revents |= LINUX_POLLHUP;

            fds[orig_idx].revents = revents;
            ready_count++;
        }
    }

    return ready_count;
}

} // namespace portage::poll
