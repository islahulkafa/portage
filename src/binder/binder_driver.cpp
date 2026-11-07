#include "binder_driver.hpp"

#include <cstring>

namespace portage::binder {

namespace {
constexpr int64_t LINUX_EBADF = -9;
constexpr int64_t LINUX_EFAULT = -14;
constexpr int64_t LINUX_EINVAL = -22;
} // namespace

BinderDriver &BinderDriver::Instance() noexcept
{
    static BinderDriver instance;
    return instance;
}

int BinderDriver::CreateInstance(bool is_hw) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_next_id++;
    while (m_instances.contains(id)) {
        id = m_next_id++;
    }

    BinderInstance inst{};
    inst.id = id;
    inst.is_hw = is_hw;
    inst.max_threads = 15;
    inst.idle_timeout_ns = 0;

    m_instances[id] = inst;
    return id;
}

int BinderDriver::CloseInstance(int binder_id) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(binder_id);
    if (it == m_instances.end()) {
        return static_cast<int>(LINUX_EBADF);
    }
    m_instances.erase(it);
    return 0;
}

void BinderDriver::Reset() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_instances.clear();
    m_next_id = 1;
}

size_t BinderDriver::ActiveInstances() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_instances.size();
}

int64_t BinderDriver::Ioctl(int binder_id, uint32_t cmd, void *arg) noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(binder_id);
    if (it == m_instances.end()) {
        return LINUX_EBADF;
    }

    if (!arg) {
        return LINUX_EFAULT;
    }

    auto &inst = it->second;

    switch (cmd) {
    case BINDER_VERSION: {
        auto *ver = static_cast<BinderVersion *>(arg);
        ver->protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;
        return 0;
    }

    case BINDER_SET_MAX_THREADS: {
        const auto *threads = static_cast<const uint32_t *>(arg);
        inst.max_threads = *threads;
        return 0;
    }

    case BINDER_SET_IDLE_TIMEOUT: {
        const auto *timeout = static_cast<const int64_t *>(arg);
        inst.idle_timeout_ns = *timeout;
        return 0;
    }

    case BINDER_WRITE_READ_64:
    case BINDER_WRITE_READ_32: {
        auto *bwr = static_cast<BinderWriteRead *>(arg);

        bool had_transaction = false;
        bool is_one_way = false;

        if (bwr->write_size > 0 && bwr->write_buffer != 0) {
            const auto *write_ptr = reinterpret_cast<const uint8_t *>(bwr->write_buffer);
            size_t write_left = static_cast<size_t>(bwr->write_size);

            while (write_left >= sizeof(uint32_t)) {
                uint32_t bc_cmd = 0;
                std::memcpy(&bc_cmd, write_ptr, sizeof(uint32_t));
                write_ptr += sizeof(uint32_t);
                write_left -= sizeof(uint32_t);

                bool is_txn = (bc_cmd == BC_TRANSACTION_64 || bc_cmd == BC_TRANSACTION_RAW ||
                               (bc_cmd & 0xffff) == 0x6300 || bc_cmd == 0x63u);
                bool is_reply = (bc_cmd == BC_REPLY_64 || bc_cmd == BC_REPLY_RAW ||
                                 (bc_cmd & 0xffff) == 0x6301 || bc_cmd == 0x64u);

                if (is_txn || is_reply) {
                    had_transaction = true;
                    if (write_left >= sizeof(BinderTransactionData)) {
                        BinderTransactionData txn{};
                        std::memcpy(&txn, write_ptr, sizeof(BinderTransactionData));
                        write_ptr += sizeof(BinderTransactionData);
                        write_left -= sizeof(BinderTransactionData);

                        if (txn.flags & TF_ONE_WAY) {
                            is_one_way = true;
                        }
                    } else {
                        write_left = 0;
                    }
                } else if (bc_cmd == BC_FREE_BUFFER_RAW || (bc_cmd & 0xff) == 3) {
                    if (write_left >= sizeof(uint64_t)) {
                        write_ptr += sizeof(uint64_t);
                        write_left -= sizeof(uint64_t);
                    } else {
                        write_left = 0;
                    }
                } else if (bc_cmd == BC_ACQUIRE_RAW || bc_cmd == BC_RELEASE_RAW ||
                           bc_cmd == BC_INCREFS_RAW || bc_cmd == BC_DECREFS_RAW ||
                           ((bc_cmd & 0xff) >= 4 && (bc_cmd & 0xff) <= 7)) {
                    if (write_left >= sizeof(uint32_t)) {
                        write_ptr += sizeof(uint32_t);
                        write_left -= sizeof(uint32_t);
                    } else {
                        write_left = 0;
                    }
                } else {
                    write_left = 0;
                }
            }
            bwr->write_consumed = bwr->write_size;
        }

        if (bwr->read_size > 0 && bwr->read_buffer != 0) {
            auto *read_ptr = reinterpret_cast<uint8_t *>(bwr->read_buffer);
            size_t read_space = static_cast<size_t>(bwr->read_size);
            size_t total_written = 0;

            if (read_space >= sizeof(uint32_t)) {
                uint32_t br_noop = BR_NOOP;
                std::memcpy(read_ptr + total_written, &br_noop, sizeof(uint32_t));
                total_written += sizeof(uint32_t);
            }

            if (had_transaction && !is_one_way &&
                (read_space >= total_written + sizeof(uint32_t) + sizeof(BinderTransactionData))) {
                uint32_t br_reply = BR_REPLY;
                std::memcpy(read_ptr + total_written, &br_reply, sizeof(uint32_t));
                total_written += sizeof(uint32_t);

                BinderTransactionData reply_txn{};
                std::memcpy(read_ptr + total_written, &reply_txn, sizeof(BinderTransactionData));
                total_written += sizeof(BinderTransactionData);
            }

            bwr->read_consumed = static_cast<int64_t>(total_written);
        }

        return 0;
    }

    default:
        return LINUX_EINVAL;
    }
}

} // namespace portage::binder
