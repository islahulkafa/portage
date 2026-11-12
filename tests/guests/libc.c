#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t wint_t;

__attribute__((visibility("default"))) uintptr_t pthread_self(void) { return 0; }
__attribute__((visibility("default"))) int pthread_equal(uintptr_t t1, uintptr_t t2)
{
    return t1 == t2;
}
__attribute__((visibility("default"))) int pthread_setname_np(uintptr_t thread, const char *name)
{
    (void)thread;
    (void)name;
    return 0;
}
__attribute__((visibility("default"))) int pthread_getname_np(uintptr_t thread, char *buf,
                                                              size_t len)
{
    (void)thread;
    (void)buf;
    (void)len;
    return 0;
}
__attribute__((visibility("default"))) int pthread_getattr_np(uintptr_t thread, void *attr)
{
    (void)thread;
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_init(void *attr)
{
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_destroy(void *attr)
{
    (void)attr;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getstack(const void *attr, void **stackaddr,
                                                                 size_t *stacksize)
{
    (void)attr;
    (void)stackaddr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setstack(void *attr, void *stackaddr,
                                                                 size_t stacksize)
{
    (void)attr;
    (void)stackaddr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getstacksize(const void *attr,
                                                                     size_t *stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setstacksize(void *attr, size_t stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getguardsize(const void *attr,
                                                                     size_t *guardsize)
{
    (void)attr;
    (void)guardsize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setguardsize(void *attr, size_t guardsize)
{
    (void)attr;
    (void)guardsize;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_getdetachstate(const void *attr,
                                                                       int *detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}
__attribute__((visibility("default"))) int pthread_attr_setdetachstate(void *attr, int detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}
__attribute__((visibility("default"))) int android_get_device_api_level(void) { return 34; }
__attribute__((visibility("default"))) int android_get_application_target_sdk_version(void)
{
    return 34;
}

__attribute__((visibility("default"))) const uint64_t __stack_chk_guard = 0x595a4b3c2d1e0f00ULL;
__attribute__((visibility("default"))) int *__errno(void) { return (void *)0; }
__attribute__((visibility("default"))) void __stack_chk_fail(void) {}
__attribute__((visibility("default"))) uint32_t arc4random(void) { return 0; }
__attribute__((visibility("default"))) void arc4random_buf(void *b, size_t n)
{
    (void)b;
    (void)n;
}
__attribute__((visibility("default"))) uint32_t arc4random_uniform(uint32_t u)
{
    (void)u;
    return 0;
}
__attribute__((visibility("default"))) uint64_t getauxval(uint64_t t)
{
    (void)t;
    return 0;
}
__attribute__((visibility("default"))) void abort(void)
{
    for (;;) {
    }
}
__attribute__((visibility("default"))) char *strerror(int e)
{
    (void)e;
    return (char *)"";
}
__attribute__((visibility("default"))) int strerror_r(int e, char *b, size_t l)
{
    (void)e;
    (void)b;
    (void)l;
    return 0;
}

__attribute__((visibility("default"))) void *malloc(size_t s)
{
    (void)s;
    return (void *)0;
}
__attribute__((visibility("default"))) void *calloc(size_t n, size_t s)
{
    (void)n;
    (void)s;
    return (void *)0;
}
__attribute__((visibility("default"))) void *realloc(void *p, size_t s)
{
    (void)p;
    (void)s;
    return (void *)0;
}
__attribute__((visibility("default"))) void free(void *p) { (void)p; }
__attribute__((visibility("default"))) int posix_memalign(void **m, size_t a, size_t s)
{
    (void)m;
    (void)a;
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) void *aligned_alloc(size_t a, size_t s)
{
    (void)a;
    (void)s;
    return (void *)0;
}
__attribute__((visibility("default"))) void *memalign(size_t a, size_t s)
{
    (void)a;
    (void)s;
    return (void *)0;
}

__attribute__((visibility("default"))) void *memcpy(void *d, const void *s, size_t n)
{
    (void)s;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) void *memmove(void *d, const void *s, size_t n)
{
    (void)s;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) void *memset(void *d, int c, size_t n)
{
    (void)c;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) int memcmp(const void *a, const void *b, size_t n)
{
    (void)a;
    (void)b;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) size_t strlen(const char *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) size_t strnlen(const char *s, size_t m)
{
    (void)s;
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int strcmp(const char *a, const char *b)
{
    (void)a;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) int strncmp(const char *a, const char *b, size_t n)
{
    (void)a;
    (void)b;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) char *strcpy(char *d, const char *s)
{
    (void)s;
    return d;
}
__attribute__((visibility("default"))) char *strncpy(char *d, const char *s, size_t n)
{
    (void)s;
    (void)n;
    return d;
}

__attribute__((visibility("default"))) void *__memcpy_chk(void *d, const void *s, size_t n,
                                                          size_t dl)
{
    (void)s;
    (void)n;
    (void)dl;
    return d;
}
__attribute__((visibility("default"))) void *__memmove_chk(void *d, const void *s, size_t n,
                                                           size_t dl)
{
    (void)s;
    (void)n;
    (void)dl;
    return d;
}
__attribute__((visibility("default"))) void *__memset_chk(void *d, int c, size_t n, size_t dl)
{
    (void)c;
    (void)n;
    (void)dl;
    return d;
}
__attribute__((visibility("default"))) char *__strcpy_chk(char *d, const char *s, size_t dl)
{
    (void)s;
    (void)dl;
    return d;
}
__attribute__((visibility("default"))) char *__strncpy_chk(char *d, const char *s, size_t n,
                                                           size_t dl)
{
    (void)s;
    (void)n;
    (void)dl;
    return d;
}
__attribute__((visibility("default"))) size_t __strlen_chk(const char *s, size_t sl)
{
    (void)s;
    (void)sl;
    return 0;
}
__attribute__((visibility("default"))) int snprintf(char *d, size_t n, const char *f, ...)
{
    (void)d;
    (void)n;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int vsnprintf(char *d, size_t n, const char *f, va_list a)
{
    (void)d;
    (void)n;
    (void)f;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int __snprintf_chk(char *d, size_t n, int fl, size_t osz,
                                                          const char *f, ...)
{
    (void)d;
    (void)n;
    (void)fl;
    (void)osz;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int __vsnprintf_chk(char *d, size_t n, int fl, size_t osz,
                                                           const char *f, va_list a)
{
    (void)d;
    (void)n;
    (void)fl;
    (void)osz;
    (void)f;
    (void)a;
    return 0;
}

__attribute__((visibility("default"))) int pthread_once(int32_t *c, void (*f)(void))
{
    (void)c;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutexattr_init(void *a)
{
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutexattr_destroy(void *a)
{
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutexattr_settype(void *a, int t)
{
    (void)a;
    (void)t;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutex_init(void *m, const void *a)
{
    (void)m;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutex_destroy(void *m)
{
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutex_lock(void *m)
{
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutex_trylock(void *m)
{
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int pthread_mutex_unlock(void *m)
{
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_init(void *c, const void *a)
{
    (void)c;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_destroy(void *c)
{
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_wait(void *c, void *m)
{
    (void)c;
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_timedwait(void *c, void *m, const void *ts)
{
    (void)c;
    (void)m;
    (void)ts;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_signal(void *c)
{
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) int pthread_cond_broadcast(void *c)
{
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) int pthread_key_create(uint32_t *k, void (*d)(void *))
{
    (void)k;
    (void)d;
    return 0;
}
__attribute__((visibility("default"))) int pthread_key_delete(uint32_t k)
{
    (void)k;
    return 0;
}
__attribute__((visibility("default"))) void *pthread_getspecific(uint32_t k)
{
    (void)k;
    return (void *)0;
}
__attribute__((visibility("default"))) int pthread_setspecific(uint32_t k, const void *v)
{
    (void)k;
    (void)v;
    return 0;
}
__attribute__((visibility("default"))) int pthread_create(uintptr_t *t, const void *a,
                                                          void *(*fn)(void *), void *arg)
{
    (void)t;
    (void)a;
    (void)fn;
    (void)arg;
    return 0;
}
__attribute__((visibility("default"))) int pthread_join(uintptr_t t, void **r)
{
    (void)t;
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_detach(uintptr_t t)
{
    (void)t;
    return 0;
}

typedef struct {
    uint8_t opaque[152];
} FILE;
__attribute__((visibility("default"))) FILE __sF[3];
__attribute__((visibility("default"))) FILE *stdin = &__sF[0];
__attribute__((visibility("default"))) FILE *stdout = &__sF[1];
__attribute__((visibility("default"))) FILE *stderr = &__sF[2];

__attribute__((visibility("default"))) FILE *fopen(const char *p, const char *m)
{
    (void)p;
    (void)m;
    return (FILE *)0;
}
__attribute__((visibility("default"))) FILE *fdopen(int fd, const char *m)
{
    (void)fd;
    (void)m;
    return (FILE *)0;
}
__attribute__((visibility("default"))) int fclose(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int fileno(FILE *f)
{
    (void)f;
    return -1;
}
__attribute__((visibility("default"))) int fflush(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) size_t fread(void *p, size_t s, size_t n, FILE *f)
{
    (void)p;
    (void)s;
    (void)n;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) size_t fwrite(const void *p, size_t s, size_t n, FILE *f)
{
    (void)p;
    (void)s;
    (void)n;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int fseek(FILE *f, long o, int w)
{
    (void)f;
    (void)o;
    (void)w;
    return 0;
}
__attribute__((visibility("default"))) int fseeko(FILE *f, int64_t o, int w)
{
    (void)f;
    (void)o;
    (void)w;
    return 0;
}
__attribute__((visibility("default"))) long ftell(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int64_t ftello(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int fputc(int c, FILE *f)
{
    (void)f;
    return c;
}
__attribute__((visibility("default"))) int putc(int c, FILE *f)
{
    (void)f;
    return c;
}
__attribute__((visibility("default"))) int fputs(const char *s, FILE *f)
{
    (void)s;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int puts(const char *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int putchar(int c) { return c; }
__attribute__((visibility("default"))) int getc(FILE *f)
{
    (void)f;
    return -1;
}
__attribute__((visibility("default"))) int fgetc(FILE *f)
{
    (void)f;
    return -1;
}
__attribute__((visibility("default"))) char *fgets(char *s, int n, FILE *f)
{
    (void)s;
    (void)n;
    (void)f;
    return s;
}
__attribute__((visibility("default"))) int ungetc(int c, FILE *f)
{
    (void)f;
    return c;
}
__attribute__((visibility("default"))) void setbuf(FILE *f, char *b)
{
    (void)f;
    (void)b;
}
__attribute__((visibility("default"))) int setvbuf(FILE *f, char *b, int m, size_t s)
{
    (void)f;
    (void)b;
    (void)m;
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int feof(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int ferror(FILE *f)
{
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) void clearerr(FILE *f) { (void)f; }
__attribute__((visibility("default"))) int fprintf(FILE *f, const char *fmt, ...)
{
    (void)f;
    (void)fmt;
    return 0;
}
__attribute__((visibility("default"))) int vfprintf(FILE *f, const char *fmt, va_list a)
{
    (void)f;
    (void)fmt;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int sscanf(const char *s, const char *fmt, ...)
{
    (void)s;
    (void)fmt;
    return 0;
}
__attribute__((visibility("default"))) int vsscanf(const char *s, const char *fmt, va_list a)
{
    (void)s;
    (void)fmt;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int asprintf(char **p, const char *fmt, ...)
{
    (void)p;
    (void)fmt;
    return 0;
}
__attribute__((visibility("default"))) int vasprintf(char **p, const char *fmt, va_list a)
{
    (void)p;
    (void)fmt;
    (void)a;
    return 0;
}

__attribute__((visibility("default"))) int open(const char *p, int f, ...)
{
    (void)p;
    (void)f;
    return -1;
}
__attribute__((visibility("default"))) int openat(int d, const char *p, int f, ...)
{
    (void)d;
    (void)p;
    (void)f;
    return -1;
}
__attribute__((visibility("default"))) int close(int fd)
{
    (void)fd;
    return 0;
}
__attribute__((visibility("default"))) int64_t read(int fd, void *b, size_t c)
{
    (void)fd;
    (void)b;
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) int64_t write(int fd, const void *b, size_t c)
{
    (void)fd;
    (void)b;
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) int64_t lseek(int fd, int64_t o, int w)
{
    (void)fd;
    (void)o;
    (void)w;
    return 0;
}
__attribute__((visibility("default"))) int stat(const char *p, void *b)
{
    (void)p;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) int lstat(const char *p, void *b)
{
    (void)p;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) int fstat(int fd, void *b)
{
    (void)fd;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) int ftruncate(int fd, int64_t l)
{
    (void)fd;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int truncate(const char *p, int64_t l)
{
    (void)p;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int chdir(const char *p)
{
    (void)p;
    return 0;
}
__attribute__((visibility("default"))) char *getcwd(char *b, size_t s)
{
    (void)b;
    (void)s;
    return (char *)0;
}
__attribute__((visibility("default"))) int mkdir(const char *p, int m)
{
    (void)p;
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int unlink(const char *p)
{
    (void)p;
    return 0;
}
__attribute__((visibility("default"))) int unlinkat(int d, const char *p, int f)
{
    (void)d;
    (void)p;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int remove(const char *p)
{
    (void)p;
    return 0;
}
__attribute__((visibility("default"))) int rename(const char *o, const char *n)
{
    (void)o;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) int64_t readlink(const char *p, char *b, size_t s)
{
    (void)p;
    (void)b;
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) char *realpath(const char *p, char *r)
{
    (void)p;
    return r;
}
__attribute__((visibility("default"))) int symlink(const char *t, const char *l)
{
    (void)t;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int symlinkat(const char *t, int d, const char *l)
{
    (void)t;
    (void)d;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int link(const char *o, const char *n)
{
    (void)o;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) int linkat(int od, const char *op, int nd, const char *np,
                                                  int f)
{
    (void)od;
    (void)op;
    (void)nd;
    (void)np;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) long readlinkat(int d, const char *p, char *b,
                                                       unsigned long s)
{
    (void)d;
    (void)p;
    (void)b;
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) int chmod(const char *p, int m)
{
    (void)p;
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int fchmod(int fd, int m)
{
    (void)fd;
    (void)m;
    return 0;
}
__attribute__((visibility("default"))) int fchmodat(int d, const char *p, int m, int f)
{
    (void)d;
    (void)p;
    (void)m;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int isatty(int fd)
{
    (void)fd;
    return 1;
}
__attribute__((visibility("default"))) int ioctl(int fd, uint64_t r, ...)
{
    (void)fd;
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int statvfs(const char *p, void *b)
{
    (void)p;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) int utimensat(int d, const char *p, const void *t, int f)
{
    (void)d;
    (void)p;
    (void)t;
    (void)f;
    return 0;
}
__attribute__((visibility("default"))) int64_t sendfile(int o, int i, int64_t *off, size_t c)
{
    (void)o;
    (void)i;
    (void)off;
    (void)c;
    return 0;
}
__attribute__((visibility("default"))) long sysconf(int n)
{
    (void)n;
    return 4096;
}
__attribute__((visibility("default"))) long pathconf(const char *p, int n)
{
    (void)p;
    (void)n;
    return 4096;
}
__attribute__((visibility("default"))) const char *getenv(const char *n)
{
    (void)n;
    return (const char *)0;
}

typedef struct {
    uint8_t opaque[280];
} dirent;
typedef struct {
    int fd;
} DIR;
__attribute__((visibility("default"))) DIR *opendir(const char *n)
{
    (void)n;
    return (DIR *)0;
}
__attribute__((visibility("default"))) DIR *fdopendir(int fd)
{
    (void)fd;
    return (DIR *)0;
}
__attribute__((visibility("default"))) dirent *readdir(DIR *d)
{
    (void)d;
    return (dirent *)0;
}
__attribute__((visibility("default"))) dirent *readdir64(DIR *d)
{
    (void)d;
    return (dirent *)0;
}
__attribute__((visibility("default"))) int closedir(DIR *d)
{
    (void)d;
    return 0;
}

__attribute__((visibility("default"))) int clock_gettime(int c, void *t)
{
    (void)c;
    (void)t;
    return 0;
}
__attribute__((visibility("default"))) int gettimeofday(void *v, void *z)
{
    (void)v;
    (void)z;
    return 0;
}
__attribute__((visibility("default"))) int64_t time(int64_t *t)
{
    (void)t;
    return 0;
}
__attribute__((visibility("default"))) int nanosleep(const void *req, void *rem)
{
    (void)req;
    (void)rem;
    return 0;
}
__attribute__((visibility("default"))) int usleep(uint32_t u)
{
    (void)u;
    return 0;
}
__attribute__((visibility("default"))) int64_t syscall(long n, ...)
{
    (void)n;
    return 0;
}

__attribute__((visibility("default"))) int pthread_rwlock_init(void *r, const void *a)
{
    (void)r;
    (void)a;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_destroy(void *r)
{
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_rdlock(void *r)
{
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_tryrdlock(void *r)
{
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_wrlock(void *r)
{
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_trywrlock(void *r)
{
    (void)r;
    return 0;
}
__attribute__((visibility("default"))) int pthread_rwlock_unlock(void *r)
{
    (void)r;
    return 0;
}

__attribute__((visibility("default"))) void qsort(void *b, size_t n, size_t s,
                                                  int (*c)(const void *, const void *))
{
    (void)b;
    (void)n;
    (void)s;
    (void)c;
}
__attribute__((visibility("default"))) void *bsearch(const void *k, const void *b, size_t n,
                                                     size_t s, int (*c)(const void *, const void *))
{
    (void)k;
    (void)b;
    (void)n;
    (void)s;
    (void)c;
    return (void *)0;
}

__attribute__((visibility("default"))) long strtol(const char *n, char **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) unsigned long strtoul(const char *n, char **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) long long strtoll(const char *n, char **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) unsigned long long strtoull(const char *n, char **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) float strtof(const char *n, char **e)
{
    (void)n;
    (void)e;
    return 0.0f;
}
__attribute__((visibility("default"))) double strtod(const char *n, char **e)
{
    (void)n;
    (void)e;
    return 0.0;
}
__attribute__((visibility("default"))) long double strtold(const char *n, char **e)
{
    (void)n;
    (void)e;
    return 0.0L;
}

__attribute__((visibility("default"))) long strtol_l(const char *n, char **e, int b, void *l)
{
    (void)n;
    (void)e;
    (void)b;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) unsigned long strtoul_l(const char *n, char **e, int b,
                                                               void *l)
{
    (void)n;
    (void)e;
    (void)b;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) long long strtoll_l(const char *n, char **e, int b, void *l)
{
    (void)n;
    (void)e;
    (void)b;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) unsigned long long strtoull_l(const char *n, char **e, int b,
                                                                     void *l)
{
    (void)n;
    (void)e;
    (void)b;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) float strtof_l(const char *n, char **e, void *l)
{
    (void)n;
    (void)e;
    (void)l;
    return 0.0f;
}
__attribute__((visibility("default"))) double strtod_l(const char *n, char **e, void *l)
{
    (void)n;
    (void)e;
    (void)l;
    return 0.0;
}
__attribute__((visibility("default"))) long double strtold_l(const char *n, char **e, void *l)
{
    (void)n;
    (void)e;
    (void)l;
    return 0.0L;
}

__attribute__((visibility("default"))) size_t __ctype_get_mb_cur_max(void) { return 4; }
__attribute__((visibility("default"))) void *localeconv(void) { return (void *)0; }
__attribute__((visibility("default"))) void *newlocale(int m, const char *l, void *b)
{
    (void)m;
    (void)l;
    (void)b;
    return (void *)0;
}
__attribute__((visibility("default"))) void *uselocale(void *l) { return l; }
__attribute__((visibility("default"))) void freelocale(void *l) { (void)l; }
__attribute__((visibility("default"))) char *setlocale(int c, const char *l)
{
    (void)c;
    (void)l;
    return (char *)0;
}
__attribute__((visibility("default"))) char *nl_langinfo(int i)
{
    (void)i;
    return (char *)"";
}
__attribute__((visibility("default"))) char *nl_langinfo_l(int i, void *l)
{
    (void)i;
    (void)l;
    return (char *)"";
}

__attribute__((visibility("default"))) size_t mbrtowc(wchar_t *p, const char *s, size_t n, void *ps)
{
    (void)p;
    (void)s;
    (void)n;
    (void)ps;
    return 0;
}
__attribute__((visibility("default"))) size_t wcrtomb(char *s, wchar_t w, void *ps)
{
    (void)s;
    (void)w;
    (void)ps;
    return 1;
}
__attribute__((visibility("default"))) size_t mbsnrtowcs(wchar_t *d, const char **s, size_t m,
                                                         size_t l, void *ps)
{
    (void)d;
    (void)s;
    (void)m;
    (void)l;
    (void)ps;
    return 0;
}
__attribute__((visibility("default"))) size_t wcsnrtombs(char *d, const wchar_t **s, size_t w,
                                                         size_t l, void *ps)
{
    (void)d;
    (void)s;
    (void)w;
    (void)l;
    (void)ps;
    return 0;
}
__attribute__((visibility("default"))) size_t wcslen(const wchar_t *s)
{
    (void)s;
    return 0;
}
__attribute__((visibility("default"))) wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    (void)s;
    (void)c;
    (void)n;
    return (wchar_t *)0;
}
__attribute__((visibility("default"))) int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    (void)a;
    (void)b;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n)
{
    (void)d;
    (void)s;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) wchar_t *wmemmove(wchar_t *d, const wchar_t *s, size_t n)
{
    (void)d;
    (void)s;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n)
{
    (void)s;
    (void)c;
    (void)n;
    return s;
}
__attribute__((visibility("default"))) int wcscmp(const wchar_t *a, const wchar_t *b)
{
    (void)a;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) wchar_t *wcscpy(wchar_t *d, const wchar_t *s)
{
    (void)s;
    return d;
}
__attribute__((visibility("default"))) wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n)
{
    (void)s;
    (void)n;
    return d;
}
__attribute__((visibility("default"))) long wcstol(const wchar_t *n, wchar_t **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) long long wcstoll(const wchar_t *n, wchar_t **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) unsigned long wcstoul(const wchar_t *n, wchar_t **e, int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) unsigned long long wcstoull(const wchar_t *n, wchar_t **e,
                                                                   int b)
{
    (void)n;
    (void)e;
    (void)b;
    return 0;
}
__attribute__((visibility("default"))) float wcstof(const wchar_t *n, wchar_t **e)
{
    (void)n;
    (void)e;
    return 0.0f;
}
__attribute__((visibility("default"))) double wcstod(const wchar_t *n, wchar_t **e)
{
    (void)n;
    (void)e;
    return 0.0;
}
__attribute__((visibility("default"))) long double wcstold(const wchar_t *n, wchar_t **e)
{
    (void)n;
    (void)e;
    return 0.0L;
}
__attribute__((visibility("default"))) int wctob(wchar_t c) { return (int)c; }
__attribute__((visibility("default"))) wchar_t btowc(int c) { return (wchar_t)c; }

__attribute__((visibility("default"))) void openlog(const char *i, int o, int f)
{
    (void)i;
    (void)o;
    (void)f;
}
__attribute__((visibility("default"))) void syslog(int p, const char *f, ...)
{
    (void)p;
    (void)f;
}
__attribute__((visibility("default"))) void closelog(void) {}

__attribute__((visibility("default"))) void *memchr(const void *s, int c, size_t n)
{
    (void)s;
    (void)c;
    (void)n;
    return (void *)0;
}

__attribute__((visibility("default"))) int iswalpha_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswblank_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswcntrl_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswdigit_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswlower_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswprint_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswpunct_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswspace_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswupper_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int iswxdigit_l(wint_t w, void *l)
{
    (void)w;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) wint_t towlower_l(wint_t w, void *l)
{
    (void)l;
    return w;
}
__attribute__((visibility("default"))) wint_t towupper_l(wint_t w, void *l)
{
    (void)l;
    return w;
}

__attribute__((visibility("default"))) int strcoll_l(const char *s1, const char *s2, void *l)
{
    (void)s1;
    (void)s2;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) size_t strxfrm_l(char *d, const char *s, size_t n, void *l)
{
    (void)d;
    (void)s;
    (void)n;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) int wcscoll_l(const wchar_t *s1, const wchar_t *s2, void *l)
{
    (void)s1;
    (void)s2;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) size_t wcsxfrm_l(wchar_t *d, const wchar_t *s, size_t n,
                                                        void *l)
{
    (void)d;
    (void)s;
    (void)n;
    (void)l;
    return 0;
}
__attribute__((visibility("default"))) size_t strftime_l(char *s, size_t m, const char *f,
                                                         const void *t, void *l)
{
    (void)s;
    (void)m;
    (void)f;
    (void)t;
    (void)l;
    return 0;
}

__attribute__((visibility("default"))) size_t mbrlen(const char *s, size_t n, void *ps)
{
    (void)s;
    (void)n;
    (void)ps;
    return 0;
}
__attribute__((visibility("default"))) size_t mbsrtowcs(wchar_t *d, const char **s, size_t l,
                                                        void *ps)
{
    (void)d;
    (void)s;
    (void)l;
    (void)ps;
    return 0;
}
__attribute__((visibility("default"))) int mbtowc(wchar_t *p, const char *s, size_t n)
{
    (void)p;
    (void)s;
    (void)n;
    return 0;
}
__attribute__((visibility("default"))) int swprintf(wchar_t *s, size_t n, const wchar_t *f, ...)
{
    (void)s;
    (void)n;
    (void)f;
    return 0;
}

__attribute__((visibility("default"))) wint_t fputwc(wchar_t c, void *s)
{
    (void)s;
    return c;
}
__attribute__((visibility("default"))) wint_t getwc(void *s)
{
    (void)s;
    return -1;
}
__attribute__((visibility("default"))) wint_t ungetwc(wint_t c, void *s)
{
    (void)s;
    return c;
}

__attribute__((visibility("default"))) void android_set_abort_message(const char *m) { (void)m; }

__attribute__((visibility("default"))) int sigwaitinfo(const void *set, void *info)
{
    (void)set;
    (void)info;
    return 0;
}
__attribute__((visibility("default"))) int sigtimedwait(const void *set, void *info, const void *ts)
{
    (void)set;
    (void)info;
    (void)ts;
    return 0;
}
__attribute__((visibility("default"))) int sigqueue(int pid, int sig, uintptr_t val)
{
    (void)pid;
    (void)sig;
    (void)val;
    return 0;
}
__attribute__((visibility("default"))) int signalfd(int fd, const void *mask, int flags)
{
    (void)fd;
    (void)mask;
    (void)flags;
    return 0;
}
__attribute__((visibility("default"))) int signalfd4(int fd, const void *mask, size_t s, int flags)
{
    (void)fd;
    (void)mask;
    (void)s;
    (void)flags;
    return 0;
}
__attribute__((visibility("default"))) int sigsuspend(const void *mask)
{
    (void)mask;
    return -1;
}
