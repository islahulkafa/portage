#include <stddef.h>
#include <stdint.h>

#define AT_FDCWD -100
#define O_RDWR 2

#define BINDER_CURRENT_PROTOCOL_VERSION 8
#define TF_ONE_WAY 0x01u

#define BINDER_WRITE_READ_64 0xc0306201u
#define BINDER_SET_MAX_THREADS 0x40046205u
#define BINDER_VERSION 0xc0046209u
#define BINDER_SET_IDLE_TIMEOUT 0x40086203u

#define BC_TRANSACTION_64 0x40406300u
#define BR_NOOP 0x720cu
#define BR_REPLY 0x80407203u

struct binder_write_read {
    int64_t write_size;
    int64_t write_consumed;
    uint64_t write_buffer;
    int64_t read_size;
    int64_t read_consumed;
    uint64_t read_buffer;
};

struct binder_version {
    int32_t protocol_version;
};

struct binder_transaction_data {
    union {
        uint32_t handle;
        uint64_t ptr;
    } target;
    uint64_t cookie;
    uint32_t code;
    uint32_t flags;
    int32_t sender_pid;
    uint32_t sender_euid;
    uint64_t data_size;
    uint64_t offsets_size;
    union {
        struct {
            uint64_t buffer;
            uint64_t offsets;
        } ptr;
        uint8_t buf[8];
    } data;
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

void _start(void)
{
    // Open /dev/binder
    int fd = (int)sys_openat(AT_FDCWD, "/dev/binder", O_RDWR, 0);
    if (fd < 0) {
        sys_exit(1);
    }

    // Query BINDER_VERSION
    struct binder_version ver = {0};
    if (sys_ioctl(fd, BINDER_VERSION, &ver) != 0) {
        sys_exit(2);
    }
    if (ver.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION) {
        sys_exit(3);
    }

    // Set max threads
    uint32_t max_threads = 15;
    if (sys_ioctl(fd, BINDER_SET_MAX_THREADS, &max_threads) != 0) {
        sys_exit(4);
    }

    // Open /dev/hwbinder and verify its properties
    int hw_fd = (int)sys_openat(AT_FDCWD, "/dev/hwbinder", O_RDWR, 0);
    if (hw_fd < 0) {
        sys_exit(5);
    }

    struct binder_version hw_ver = {0};
    if (sys_ioctl(hw_fd, BINDER_VERSION, &hw_ver) != 0 ||
        hw_ver.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION) {
        sys_exit(6);
    }

    int64_t idle_timeout = 5000000000LL;
    if (sys_ioctl(hw_fd, BINDER_SET_IDLE_TIMEOUT, &idle_timeout) != 0) {
        sys_exit(7);
    }

    // Test BINDER_WRITE_READ transaction on /dev/binder
    uint8_t write_buf[sizeof(uint32_t) + sizeof(struct binder_transaction_data)] = {0};
    uint32_t *cmd_ptr = (uint32_t *)write_buf;
    *cmd_ptr = BC_TRANSACTION_64;

    struct binder_transaction_data *txn =
        (struct binder_transaction_data *)(write_buf + sizeof(uint32_t));
    txn->target.handle = 0;
    txn->code = 1;
    txn->flags = 0; // expects reply

    uint8_t read_buf[256] = {0};
    struct binder_write_read bwr = {0};
    bwr.write_size = (int64_t)sizeof(write_buf);
    bwr.write_consumed = 0;
    bwr.write_buffer = (uint64_t)write_buf;
    bwr.read_size = (int64_t)sizeof(read_buf);
    bwr.read_consumed = 0;
    bwr.read_buffer = (uint64_t)read_buf;

    if (sys_ioctl(fd, BINDER_WRITE_READ_64, &bwr) != 0) {
        sys_exit(8);
    }
    if (bwr.write_consumed != bwr.write_size) {
        sys_exit(9);
    }

    uint32_t expected_min_read =
        (uint32_t)(sizeof(uint32_t) * 2 + sizeof(struct binder_transaction_data));
    if ((uint64_t)bwr.read_consumed < (uint64_t)expected_min_read) {
        sys_exit(10);
    }

    uint32_t br0 = *(uint32_t *)(read_buf);
    uint32_t br1 = *(uint32_t *)(read_buf + sizeof(uint32_t));
    if (br0 != BR_NOOP) {
        sys_exit(11);
    }
    if (br1 != BR_REPLY) {
        sys_exit(12);
    }

    sys_close(hw_fd);
    sys_close(fd);
    sys_exit(0);
}
