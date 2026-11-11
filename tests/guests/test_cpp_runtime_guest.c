#include <stddef.h>
#include <stdint.h>

typedef void FILE;
typedef void DIR;

struct guest_dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
};

struct guest_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    uint64_t st_atime_nsec;
    int64_t st_mtime;
    uint64_t st_mtime_nsec;
    int64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t __pad1[3];
};

struct guest_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

extern FILE *fopen(const char *path, const char *mode);
extern int fclose(FILE *fp);
extern int fputs(const char *s, FILE *fp);
extern char *fgets(char *s, int size, FILE *fp);
extern int fseek(FILE *fp, long offset, int whence);
extern int sscanf(const char *s, const char *fmt, ...);

extern int openat(int dirfd, const char *pathname, int flags, ...);
extern int close(int fd);
extern int64_t write(int fd, const void *buf, size_t count);
extern int stat(const char *path, void *buf);
extern int unlinkat(int dirfd, const char *pathname, int flags);

extern DIR *opendir(const char *name);
extern struct guest_dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);

extern void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
extern void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
                     int (*compar)(const void *, const void *));

extern size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps);
extern size_t wcslen(const wchar_t *s);
extern int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);

extern int clock_gettime(int clk_id, void *tp);
extern int64_t syscall(long number, ...);

extern void *dlsym(void *handle, const char *symbol);

static inline void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60), "D"((int64_t)code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static int compare_integers(const void *a, const void *b)
{
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    if (val_a < val_b) {
        return -1;
    }
    if (val_a > val_b) {
        return 1;
    }
    return 0;
}

static int string_equals(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

void _start(void)
{
    FILE *fp = fopen("/data/local/tmp/cpp_test.txt", "w+");
    if (!fp) {
        sys_exit(1);
    }
    if (fputs("portage_bionic_stdio_ok\n", fp) < 0) {
        sys_exit(2);
    }
    if (fseek(fp, 0, 0) != 0) {
        sys_exit(3);
    }
    char stdio_buf[64];
    for (size_t i = 0; i < sizeof(stdio_buf); ++i) {
        stdio_buf[i] = 0;
    }
    if (!fgets(stdio_buf, sizeof(stdio_buf), fp)) {
        sys_exit(4);
    }
    if (!string_equals(stdio_buf, "portage_bionic_stdio_ok\n")) {
        sys_exit(5);
    }
    fclose(fp);

    int scanned_int = 0;
    float scanned_float = 0.0f;
    char scanned_str[32];
    for (size_t i = 0; i < sizeof(scanned_str); ++i) {
        scanned_str[i] = 0;
    }
    int sscanf_count =
        sscanf("123 45.5 teststr", "%d %f %s", &scanned_int, &scanned_float, scanned_str);
    if (sscanf_count != 3 || scanned_int != 123 || scanned_float < 45.4f || scanned_float > 45.6f ||
        !string_equals(scanned_str, "teststr")) {
        sys_exit(6);
    }

    int fd = openat(-100, "/data/local/tmp/cpp_posix.txt", 0x42, 0644);
    if (fd < 0) {
        sys_exit(7);
    }
    if (write(fd, "posix", 5) != 5) {
        sys_exit(8);
    }
    close(fd);

    struct guest_stat st;
    for (size_t i = 0; i < sizeof(st); ++i) {
        ((uint8_t *)&st)[i] = 0;
    }
    if (stat("/data/local/tmp/cpp_posix.txt", &st) != 0 || st.st_size != 5) {
        sys_exit(9);
    }
    unlinkat(-100, "/data/local/tmp/cpp_posix.txt", 0);

    DIR *dir = opendir("/data/local/tmp");
    if (!dir) {
        sys_exit(10);
    }
    struct guest_dirent *ent = readdir(dir);
    if (!ent) {
        sys_exit(11);
    }
    closedir(dir);

    int numbers[5] = {42, 7, 99, 1, 13};
    qsort(numbers, 5, sizeof(int), compare_integers);
    if (numbers[0] != 1 || numbers[1] != 7 || numbers[2] != 13 || numbers[3] != 42 ||
        numbers[4] != 99) {
        sys_exit(12);
    }

    int search_key = 42;
    int *found_entry = (int *)bsearch(&search_key, numbers, 5, sizeof(int), compare_integers);
    if (!found_entry || *found_entry != 42) {
        sys_exit(13);
    }

    wchar_t converted_wc = 0;
    size_t mbr_res = mbrtowc(&converted_wc, "Z", 1, NULL);
    if (mbr_res != 1 || converted_wc != (wchar_t)'Z') {
        sys_exit(14);
    }

    const wchar_t wide_text[6] = {(wchar_t)'H', (wchar_t)'e', (wchar_t)'l',
                                  (wchar_t)'l', (wchar_t)'o', 0};
    if (wcslen(wide_text) != 5 || wmemcmp(wide_text, wide_text, 5) != 0) {
        sys_exit(15);
    }

    struct guest_timespec clock_ts;
    clock_ts.tv_sec = 0;
    clock_ts.tv_nsec = 0;
    if (clock_gettime(1, &clock_ts) != 0) {
        sys_exit(16);
    }

    int64_t process_id = syscall(39);
    if (process_id <= 0) {
        sys_exit(17);
    }

    typedef void *(*operator_new_func)(size_t);
    typedef void (*operator_delete_func)(void *);

    operator_new_func new_fn = (operator_new_func)dlsym(NULL, "_Znwm");
    operator_delete_func del_fn = (operator_delete_func)dlsym(NULL, "_ZdlPv");
    if (!new_fn || !del_fn) {
        sys_exit(18);
    }

    void *allocated_block = new_fn(128);
    if (!allocated_block) {
        sys_exit(19);
    }
    del_fn(allocated_block);

    sys_exit(0);
}
