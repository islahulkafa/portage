#include <stddef.h>
#include <stdint.h>

#define AF_INET 2
#define SOCK_STREAM 1

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_CHILD_CLEARTID 0x00200000

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    struct {
        uint32_t s_addr;
    } sin_addr;
    uint8_t sin_zero[8];
};

static inline int64_t sys_socket(int domain, int type, int protocol)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(41), "D"(domain), "S"(type), "d"(protocol)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_connect(int fd, const struct sockaddr_in *addr, uint32_t addrlen)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(42), "D"(fd), "S"(addr), "d"(addrlen)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_accept(int fd, struct sockaddr_in *addr, uint32_t *addrlen)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(43), "D"(fd), "S"(addr), "d"(addrlen)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_bind(int fd, const struct sockaddr_in *addr, uint32_t addrlen)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(49), "D"(fd), "S"(addr), "d"(addrlen)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_listen(int fd, int backlog)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(50), "D"(fd), "S"(backlog)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_getsockname(int fd, struct sockaddr_in *addr, uint32_t *addrlen)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(51), "D"(fd), "S"(addr), "d"(addrlen)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_read(int fd, void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(0), "D"(fd), "S"(buf), "d"(count)
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

static inline int64_t sys_clone(uint64_t flags, void *child_stack, int *ptid, int *ctid,
                                unsigned long tls)
{
    int64_t ret;
    register int *r10 __asm__("r10") = ctid;
    register unsigned long r8 __asm__("r8") = tls;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(56), "D"(flags), "S"(child_stack), "d"(ptid), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_futex(int *uaddr, int op, int val, const void *timeout)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(202), "D"(uaddr), "S"(op), "d"(val), "r"(timeout), "r"(0), "r"(0)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
}

static uint8_t child_stack[65536] __attribute__((aligned(16)));
static volatile int child_tid = 0;
static volatile uint16_t server_port = 0;

static void client_worker(void)
{
    while (server_port == 0) {
    }

    int cfd = sys_socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) {
        sys_exit(11);
    }

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port = server_port;
    srv.sin_addr.s_addr = 0x0100007F; // 127.0.0.1 in network byte order (little endian host)

    if (sys_connect(cfd, &srv, sizeof(srv)) < 0) {
        sys_exit(12);
    }

    const char ping[] = "PING";
    if (sys_write(cfd, ping, 4) != 4) {
        sys_exit(13);
    }

    char buf[8] = {0};
    if (sys_read(cfd, buf, 4) != 4) {
        sys_exit(14);
    }

    if (buf[0] != 'P' || buf[1] != 'O' || buf[2] != 'N' || buf[3] != 'G') {
        sys_exit(15);
    }

    sys_close(cfd);
    sys_exit(0);
}

void _start(void)
{
    int sfd = sys_socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        sys_exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = 0x0100007F; // 127.0.0.1

    if (sys_bind(sfd, &addr, sizeof(addr)) < 0) {
        sys_exit(2);
    }

    if (sys_listen(sfd, 5) < 0) {
        sys_exit(3);
    }

    struct sockaddr_in bound = {0};
    uint32_t bound_len = sizeof(bound);
    if (sys_getsockname(sfd, &bound, &bound_len) < 0) {
        sys_exit(4);
    }

    // Publish port to client
    server_port = bound.sin_port;

    // Set up child stack
    void *stack_top = &child_stack[65536 - 16];
    uint64_t flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_CHILD_CLEARTID;
    int64_t res = sys_clone(flags, stack_top, NULL, (int *)&child_tid, 0);
    if (res < 0) {
        sys_exit(5);
    }
    if (res == 0) {
        client_worker();
        sys_exit(0);
    }
    child_tid = (int)res;

    struct sockaddr_in client_addr = {0};
    uint32_t client_len = sizeof(client_addr);
    int client_conn = sys_accept(sfd, &client_addr, &client_len);
    if (client_conn < 0) {
        sys_exit(6);
    }

    char buf[8] = {0};
    if (sys_read(client_conn, buf, 4) != 4) {
        sys_exit(7);
    }

    if (buf[0] != 'P' || buf[1] != 'I' || buf[2] != 'N' || buf[3] != 'G') {
        sys_exit(8);
    }

    const char pong[] = "PONG";
    if (sys_write(client_conn, pong, 4) != 4) {
        sys_exit(9);
    }

    sys_close(client_conn);
    sys_close(sfd);

    // Join child thread
    while (child_tid != 0) {
        sys_futex((int *)&child_tid, FUTEX_WAIT, child_tid, NULL);
    }

    sys_exit(0);
}
