#include <stddef.h>
#include <stdint.h>

#define O_RDONLY 00000000
#define O_NONBLOCK 00004000

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_ANONYMOUS 0x20

#define AF_UNIX 1
#define SOCK_STREAM 1

#define PROP_AREA_MAGIC 0x50524f50
#define PROP_AREA_VERSION 0x00020000
#define PROP_MSG_SETPROP 1

struct PropAreaHeader {
    uint32_t bytes_used;
    uint32_t serial;
    uint32_t magic;
    uint32_t version;
    uint32_t reserved[28];
};

struct PropTrieNode {
    uint8_t namelen;
    uint8_t reserved[3];
    uint32_t prop;
    uint32_t left;
    uint32_t right;
    uint32_t children;
    char name[4];
};

struct PropInfo {
    uint32_t serial;
    char value[92];
    char name[32];
};

struct PropMsgSetProp {
    uint32_t cmd;
    char name[32];
    char value[92];
};

struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[108];
};

static inline int64_t sys_write(int fd, const void *buf, size_t count)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(1), "D"(fd), "S"(buf), "d"(count)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"(code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static inline int64_t sys_openat(int dfd, const char *path, int flags, int mode)
{
    int64_t ret;
    register int r10 __asm__("r10") = mode;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(257), "D"(dfd), "S"(path), "d"(flags), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd)
{
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

static inline void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
                             uint64_t offset)
{
    void *ret;
    register int r10 __asm__("r10") = flags;
    register int r8 __asm__("r8") = fd;
    register uint64_t r9 __asm__("r9") = offset;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(9), "D"(addr), "S"(length), "d"(prot), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_socket(int domain, int type, int protocol)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(41), "D"(domain), "S"(type), "d"(protocol)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_connect(int fd, const void *addr, uint32_t addrlen)
{
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(42), "D"(fd), "S"(addr), "d"(addrlen)
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

static size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

static int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i] || !b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

static void *memset(void *dest, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return dest;
}

static struct PropTrieNode *to_node(uint8_t *data, uint32_t off)
{
    if (off == 0)
        return NULL;
    return (struct PropTrieNode *)(data + off);
}

static struct PropInfo *to_prop_info(uint8_t *data, uint32_t off)
{
    if (off == 0)
        return NULL;
    return (struct PropInfo *)(data + off);
}

void _start(void)
{
    // Open and mmap /dev/__properties__/properties_serial
    const char serial_path[] = "/dev/__properties__/properties_serial";
    int sfd = (int)sys_openat(-100, serial_path, O_RDONLY, 0);
    if (sfd < 0) {
        sys_exit(1);
    }

    void *s_map = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED, sfd, 0);
    if (!s_map || s_map == (void *)-1) {
        sys_exit(2);
    }
    struct PropAreaHeader *s_hdr = (struct PropAreaHeader *)s_map;
    if (s_hdr->magic != PROP_AREA_MAGIC) {
        sys_exit(3);
    }
    uint32_t initial_serial = s_hdr->serial;
    sys_close(sfd);

    // Open and mmap /dev/__properties__/u:object_r:default_prop:s0
    const char default_prop_path[] = "/dev/__properties__/u:object_r:default_prop:s0";
    int pfd = (int)sys_openat(-100, default_prop_path, O_RDONLY, 0);
    if (pfd < 0) {
        sys_exit(4);
    }

    void *p_map = sys_mmap(NULL, 128 * 1024, PROT_READ, MAP_SHARED, pfd, 0);
    if (!p_map || p_map == (void *)-1) {
        sys_exit(5);
    }
    struct PropAreaHeader *p_hdr = (struct PropAreaHeader *)p_map;
    if (p_hdr->magic != PROP_AREA_MAGIC || p_hdr->version != PROP_AREA_VERSION) {
        sys_exit(6);
    }

    uint8_t *data = (uint8_t *)p_map + sizeof(struct PropAreaHeader);
    struct PropTrieNode *root = (struct PropTrieNode *)data;

    // Find ro.build.version.sdk
    const char *segments[] = {"ro", "build", "version", "sdk"};
    struct PropTrieNode *curr = root;

    for (int s = 0; s < 4; s++) {
        if (curr->children == 0) {
            sys_exit(7);
        }
        uint32_t child_off = curr->children;
        struct PropTrieNode *found = NULL;
        while (child_off != 0) {
            struct PropTrieNode *child = to_node(data, child_off);
            if (!child)
                break;
            int cmp = strcmp(segments[s], child->name);
            if (cmp == 0) {
                found = child;
                break;
            } else if (cmp < 0) {
                child_off = child->left;
            } else {
                child_off = child->right;
            }
        }
        if (!found) {
            sys_exit(8);
        }
        curr = found;
    }

    if (curr->prop == 0) {
        sys_exit(9);
    }
    struct PropInfo *pi = to_prop_info(data, curr->prop);
    if (strcmp(pi->value, "34") != 0) {
        sys_exit(10);
    }
    sys_close(pfd);

    // Connect to property_service and set a property
    int sock = (int)sys_socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        sys_exit(11);
    }

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    const char sock_path[] = "/dev/socket/property_service";
    for (size_t i = 0; i < sizeof(sock_path); i++) {
        sun.sun_path[i] = sock_path[i];
    }

    if (sys_connect(sock, &sun, sizeof(sun)) != 0) {
        sys_exit(12);
    }

    struct PropMsgSetProp msg;
    memset(&msg, 0, sizeof(msg));
    msg.cmd = PROP_MSG_SETPROP;
    const char prop_name[] = "debug.portage.guest";
    const char prop_val[] = "verified_val";
    for (size_t i = 0; i < sizeof(prop_name); i++)
        msg.name[i] = prop_name[i];
    for (size_t i = 0; i < sizeof(prop_val); i++)
        msg.value[i] = prop_val[i];

    if (sys_write(sock, &msg, sizeof(msg)) != (int64_t)sizeof(msg)) {
        sys_exit(13);
    }

    uint32_t resp = 0xFFFFFFFF;
    if (sys_read(sock, &resp, sizeof(resp)) != 4 || resp != 0) {
        sys_exit(14);
    }
    sys_close(sock);

    // Verify properties_serial was bumped
    if (s_hdr->serial <= initial_serial) {
        sys_exit(15);
    }

    sys_exit(0);
}
