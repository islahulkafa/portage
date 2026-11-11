#include "ndk/bionic_stdio.hpp"
#include "ndk/bionic_posix.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "syscall/linux_types.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace portage::ndk {

BionicFile g_bionic_sF[3] = {
    {.fd = 0,
     .flags = 0,
     .unget_char = -1,
     .is_open = 1,
     .eof = 0,
     .error = 0,
     .own_fd = 0,
     .host_fp = stdin,
     .reserved = {0}},
    {.fd = 1,
     .flags = 0,
     .unget_char = -1,
     .is_open = 1,
     .eof = 0,
     .error = 0,
     .own_fd = 0,
     .host_fp = stdout,
     .reserved = {0}},
    {.fd = 2,
     .flags = 0,
     .unget_char = -1,
     .is_open = 1,
     .eof = 0,
     .error = 0,
     .own_fd = 0,
     .host_fp = stderr,
     .reserved = {0}},
};

BionicFile *g_bionic_stdin_ptr = &g_bionic_sF[0];
BionicFile *g_bionic_stdout_ptr = &g_bionic_sF[1];
BionicFile *g_bionic_stderr_ptr = &g_bionic_sF[2];

void RegisterBionicStdio(NdkSymbolRegistry &registry)
{
    registry.Register("__sF", reinterpret_cast<uintptr_t>(&g_bionic_sF[0]));
    registry.Register("stdin", reinterpret_cast<uintptr_t>(&g_bionic_stdin_ptr));
    registry.Register("stdout", reinterpret_cast<uintptr_t>(&g_bionic_stdout_ptr));
    registry.Register("stderr", reinterpret_cast<uintptr_t>(&g_bionic_stderr_ptr));
    registry.Register("fopen", reinterpret_cast<uintptr_t>(&portage_fopen));
    registry.Register("fdopen", reinterpret_cast<uintptr_t>(&portage_fdopen));
    registry.Register("fclose", reinterpret_cast<uintptr_t>(&portage_fclose));
    registry.Register("fileno", reinterpret_cast<uintptr_t>(&portage_fileno));
    registry.Register("fflush", reinterpret_cast<uintptr_t>(&portage_fflush));
    registry.Register("fread", reinterpret_cast<uintptr_t>(&portage_fread));
    registry.Register("fwrite", reinterpret_cast<uintptr_t>(&portage_fwrite));
    registry.Register("fseek", reinterpret_cast<uintptr_t>(&portage_fseek));
    registry.Register("fseeko", reinterpret_cast<uintptr_t>(&portage_fseeko));
    registry.Register("ftell", reinterpret_cast<uintptr_t>(&portage_ftell));
    registry.Register("ftello", reinterpret_cast<uintptr_t>(&portage_ftello));
    registry.Register("fputc", reinterpret_cast<uintptr_t>(&portage_fputc));
    registry.Register("putc", reinterpret_cast<uintptr_t>(&portage_putc));
    registry.Register("fputs", reinterpret_cast<uintptr_t>(&portage_fputs));
    registry.Register("puts", reinterpret_cast<uintptr_t>(&portage_puts));
    registry.Register("putchar", reinterpret_cast<uintptr_t>(&portage_putchar));
    registry.Register("getc", reinterpret_cast<uintptr_t>(&portage_getc));
    registry.Register("fgetc", reinterpret_cast<uintptr_t>(&portage_fgetc));
    registry.Register("fgets", reinterpret_cast<uintptr_t>(&portage_fgets));
    registry.Register("ungetc", reinterpret_cast<uintptr_t>(&portage_ungetc));
    registry.Register("setbuf", reinterpret_cast<uintptr_t>(&portage_setbuf));
    registry.Register("setvbuf", reinterpret_cast<uintptr_t>(&portage_setvbuf));
    registry.Register("feof", reinterpret_cast<uintptr_t>(&portage_feof));
    registry.Register("ferror", reinterpret_cast<uintptr_t>(&portage_ferror));
    registry.Register("clearerr", reinterpret_cast<uintptr_t>(&portage_clearerr));
    registry.Register("fprintf", reinterpret_cast<uintptr_t>(&portage_fprintf));
    registry.Register("vfprintf", reinterpret_cast<uintptr_t>(&portage_vfprintf));
    registry.Register("sscanf", reinterpret_cast<uintptr_t>(&portage_sscanf));
    registry.Register("vsscanf", reinterpret_cast<uintptr_t>(&portage_vsscanf));
    registry.Register("asprintf", reinterpret_cast<uintptr_t>(&portage_asprintf));
    registry.Register("vasprintf", reinterpret_cast<uintptr_t>(&portage_vasprintf));
}

static int ParseSysvSscanf(const char *str, const char *format, PrintfArgSource &args)
{
    if (!str || !format) {
        return 0;
    }
    const char *s = str;
    const char *f = format;
    int matched = 0;

    while (*f && *s) {
        if (std::isspace(static_cast<unsigned char>(*f))) {
            while (*f && std::isspace(static_cast<unsigned char>(*f))) {
                f++;
            }
            while (*s && std::isspace(static_cast<unsigned char>(*s))) {
                s++;
            }
            continue;
        }

        if (*f != '%') {
            if (*f == *s) {
                f++;
                s++;
                continue;
            }
            break;
        }

        f++;
        if (*f == '%') {
            if (*s == '%') {
                f++;
                s++;
                continue;
            }
            break;
        }

        bool suppress = false;
        if (*f == '*') {
            suppress = true;
            f++;
        }

        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        bool is_long_long = false;
        bool is_long = false;
        if (*f == 'l') {
            f++;
            if (*f == 'l') {
                f++;
                is_long_long = true;
            } else {
                is_long = true;
            }
        } else if (*f == 'z') {
            f++;
            is_long = true;
        } else if (*f == 'h') {
            f++;
            if (*f == 'h') {
                f++;
            }
        }

        char conv = *f;
        if (!conv) {
            break;
        }
        f++;

        if (conv != 'c') {
            while (*s && std::isspace(static_cast<unsigned char>(*s))) {
                s++;
            }
            if (!*s) {
                break;
            }
        }

        uintptr_t target_ptr = suppress ? 0 : args.NextInt();
        char *endp = nullptr;

        if (conv == 'd' || conv == 'i') {
            long long val = std::strtoll(s, &endp, (conv == 'i') ? 0 : 10);
            if (endp == s) {
                break;
            }
            s = endp;
            if (!suppress && target_ptr) {
                if (is_long_long || is_long) {
                    *reinterpret_cast<int64_t *>(target_ptr) = val;
                } else {
                    *reinterpret_cast<int32_t *>(target_ptr) = static_cast<int32_t>(val);
                }
                matched++;
            }
        } else if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            int base = 10;
            if (conv == 'x' || conv == 'X') {
                base = 16;
            } else if (conv == 'o') {
                base = 8;
            }
            unsigned long long val = std::strtoull(s, &endp, base);
            if (endp == s) {
                break;
            }
            s = endp;
            if (!suppress && target_ptr) {
                if (is_long_long || is_long) {
                    *reinterpret_cast<uint64_t *>(target_ptr) = val;
                } else {
                    *reinterpret_cast<uint32_t *>(target_ptr) = static_cast<uint32_t>(val);
                }
                matched++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G') {
            if (is_long) {
                double val = std::strtod(s, &endp);
                if (endp == s) {
                    break;
                }
                s = endp;
                if (!suppress && target_ptr) {
                    *reinterpret_cast<double *>(target_ptr) = val;
                    matched++;
                }
            } else {
                float val = std::strtof(s, &endp);
                if (endp == s) {
                    break;
                }
                s = endp;
                if (!suppress && target_ptr) {
                    *reinterpret_cast<float *>(target_ptr) = val;
                    matched++;
                }
            }
        } else if (conv == 's') {
            char *dst = reinterpret_cast<char *>(target_ptr);
            int count = 0;
            while (*s && !std::isspace(static_cast<unsigned char>(*s)) &&
                   (width == 0 || count < width)) {
                if (!suppress && dst) {
                    dst[count] = *s;
                }
                count++;
                s++;
            }
            if (!suppress && dst) {
                dst[count] = '\0';
                matched++;
            }
        } else if (conv == 'c') {
            int max_chars = (width > 0) ? width : 1;
            char *dst = reinterpret_cast<char *>(target_ptr);
            int count = 0;
            while (*s && count < max_chars) {
                if (!suppress && dst) {
                    dst[count] = *s;
                }
                count++;
                s++;
            }
            if (!suppress && dst) {
                matched++;
            }
        }
    }
    return matched;
}

} // namespace portage::ndk

extern "C" {

using namespace portage::ndk;
using namespace portage::syscall;

BionicFile *portage_stdin(void) { return portage::ndk::g_bionic_stdin_ptr; }
BionicFile *portage_stdout(void) { return portage::ndk::g_bionic_stdout_ptr; }
BionicFile *portage_stderr(void) { return portage::ndk::g_bionic_stderr_ptr; }

__attribute__((sysv_abi)) BionicFile *portage_fopen(const char *path, const char *mode)
{
    if (!path || !mode) {
        return nullptr;
    }

    if (auto *vfs = SyscallDispatcher::GetVfs(); vfs != nullptr) {
        int flags = 0;
        if (std::strchr(mode, '+')) {
            flags = LINUX_O_RDWR;
        } else if (mode[0] == 'r') {
            flags = LINUX_O_RDONLY;
        } else if (mode[0] == 'w' || mode[0] == 'a') {
            flags = LINUX_O_WRONLY;
        }
        if (mode[0] == 'w') {
            flags |= LINUX_O_CREAT | LINUX_O_TRUNC;
        } else if (mode[0] == 'a') {
            flags |= LINUX_O_CREAT | LINUX_O_APPEND;
        }
        int fd = portage_open(path, flags, 0666);
        if (fd < 0) {
            return nullptr;
        }
        auto *bf = new BionicFile();
        bf->fd = fd;
        bf->is_open = true;
        bf->own_fd = true;
        return bf;
    }

    std::FILE *fp = std::fopen(path, mode);
    if (!fp) {
        return nullptr;
    }
    auto *bf = new BionicFile();
    bf->host_fp = fp;
    bf->is_open = true;
    bf->own_fd = true;
    return bf;
}

__attribute__((sysv_abi)) BionicFile *portage_fdopen(int fd, const char *)
{
    if (fd < 0) {
        return nullptr;
    }
    auto *bf = new BionicFile();
    bf->fd = fd;
    bf->is_open = true;
    bf->own_fd = false;
    return bf;
}

__attribute__((sysv_abi)) int portage_fclose(BionicFile *stream)
{
    if (!stream) {
        return -1;
    }
    if (stream->host_fp) {
        std::fclose(stream->host_fp);
        stream->host_fp = nullptr;
    } else if (stream->own_fd && stream->fd >= 0) {
        SyscallDispatcher::Dispatch(3 /* SYS_close */, stream->fd, 0, 0, 0, 0, 0);
    }
    stream->is_open = false;
    if (stream != &g_bionic_sF[0] && stream != &g_bionic_sF[1] && stream != &g_bionic_sF[2]) {
        delete stream;
    }
    return 0;
}

__attribute__((sysv_abi)) int portage_fileno(BionicFile *stream)
{
    if (!stream) {
        return -1;
    }
    if (stream->host_fp) {
        return _fileno(stream->host_fp);
    }
    return stream->fd;
}

__attribute__((sysv_abi)) int portage_fflush(BionicFile *stream)
{
    if (!stream) {
        return 0;
    }
    if (stream->host_fp) {
        return std::fflush(stream->host_fp);
    }
    if (stream->fd >= 0) {
        SyscallDispatcher::Dispatch(74 /* SYS_fsync */, stream->fd, 0, 0, 0, 0, 0);
    }
    return 0;
}

__attribute__((sysv_abi)) size_t portage_fread(void *ptr, size_t size, size_t nmemb,
                                               BionicFile *stream)
{
    if (!ptr || size == 0 || nmemb == 0 || !stream) {
        return 0;
    }
    if (stream->host_fp) {
        if (stream->unget_char != -1) {
            unsigned char c = static_cast<unsigned char>(stream->unget_char);
            stream->unget_char = -1;
            reinterpret_cast<unsigned char *>(ptr)[0] = c;
            size_t rem = std::fread(reinterpret_cast<unsigned char *>(ptr) + 1, 1,
                                    (size * nmemb) - 1, stream->host_fp);
            return (rem + 1) / size;
        }
        size_t n = std::fread(ptr, size, nmemb, stream->host_fp);
        if (n < nmemb) {
            if (std::feof(stream->host_fp)) {
                stream->eof = true;
            }
            if (std::ferror(stream->host_fp)) {
                stream->error = true;
            }
        }
        return n;
    }

    size_t total = size * nmemb;
    size_t read_bytes = 0;
    if (stream->unget_char != -1) {
        reinterpret_cast<char *>(ptr)[0] = static_cast<char>(stream->unget_char);
        stream->unget_char = -1;
        read_bytes = 1;
    }

    if (read_bytes < total) {
        int64_t r = SyscallDispatcher::Dispatch(0 /* SYS_read */, stream->fd,
                                                reinterpret_cast<uint64_t>(ptr) + read_bytes,
                                                total - read_bytes, 0, 0, 0);
        if (r > 0) {
            read_bytes += static_cast<size_t>(r);
        } else if (r == 0) {
            stream->eof = true;
        } else {
            stream->error = true;
        }
    }
    return read_bytes / size;
}

__attribute__((sysv_abi)) size_t portage_fwrite(const void *ptr, size_t size, size_t nmemb,
                                                BionicFile *stream)
{
    if (!ptr || size == 0 || nmemb == 0 || !stream) {
        return 0;
    }
    if (stream->host_fp) {
        size_t n = std::fwrite(ptr, size, nmemb, stream->host_fp);
        if (n < nmemb) {
            stream->error = true;
        }
        return n;
    }
    size_t total = size * nmemb;
    int64_t w = SyscallDispatcher::Dispatch(1 /* SYS_write */, stream->fd,
                                            reinterpret_cast<uint64_t>(ptr), total, 0, 0, 0);
    if (w < 0) {
        stream->error = true;
        return 0;
    }
    return static_cast<size_t>(w) / size;
}

__attribute__((sysv_abi)) int portage_fseek(BionicFile *stream, long offset, int whence)
{
    return portage_fseeko(stream, offset, whence);
}

__attribute__((sysv_abi)) int portage_fseeko(BionicFile *stream, int64_t offset, int whence)
{
    if (!stream) {
        return -1;
    }
    if (stream->host_fp) {
        int res = std::fseek(stream->host_fp, static_cast<long>(offset), whence);
        stream->eof = false;
        stream->unget_char = -1;
        return res;
    }
    int64_t pos =
        SyscallDispatcher::Dispatch(8 /* SYS_lseek */, stream->fd, offset, whence, 0, 0, 0);
    if (pos < 0) {
        *portage_errno() = static_cast<int>(-pos);
        return -1;
    }
    stream->eof = false;
    stream->unget_char = -1;
    return 0;
}

__attribute__((sysv_abi)) long portage_ftell(BionicFile *stream)
{
    return static_cast<long>(portage_ftello(stream));
}

__attribute__((sysv_abi)) int64_t portage_ftello(BionicFile *stream)
{
    if (!stream) {
        return -1;
    }
    if (stream->host_fp) {
        return std::ftell(stream->host_fp);
    }
    int64_t pos =
        SyscallDispatcher::Dispatch(8 /* SYS_lseek */, stream->fd, 0, 1 /* SEEK_CUR */, 0, 0, 0);
    if (pos < 0) {
        *portage_errno() = static_cast<int>(-pos);
        return -1;
    }
    return pos;
}

__attribute__((sysv_abi)) int portage_fputc(int c, BionicFile *stream)
{
    unsigned char ch = static_cast<unsigned char>(c);
    return (portage_fwrite(&ch, 1, 1, stream) == 1) ? c : -1;
}

__attribute__((sysv_abi)) int portage_putc(int c, BionicFile *stream)
{
    return portage_fputc(c, stream);
}

__attribute__((sysv_abi)) int portage_fputs(const char *s, BionicFile *stream)
{
    if (!s) {
        return -1;
    }
    size_t len = std::strlen(s);
    return (portage_fwrite(s, 1, len, stream) == len) ? 0 : -1;
}

__attribute__((sysv_abi)) int portage_puts(const char *s)
{
    if (portage_fputs(s, g_bionic_stdout_ptr) < 0) {
        return -1;
    }
    return portage_fputc('\n', g_bionic_stdout_ptr);
}

__attribute__((sysv_abi)) int portage_putchar(int c)
{
    return portage_fputc(c, g_bionic_stdout_ptr);
}

__attribute__((sysv_abi)) int portage_getc(BionicFile *stream) { return portage_fgetc(stream); }

__attribute__((sysv_abi)) int portage_fgetc(BionicFile *stream)
{
    if (!stream) {
        return -1;
    }
    if (stream->unget_char != -1) {
        int c = stream->unget_char;
        stream->unget_char = -1;
        return c;
    }
    unsigned char ch = 0;
    return (portage_fread(&ch, 1, 1, stream) == 1) ? ch : -1;
}

__attribute__((sysv_abi)) char *portage_fgets(char *s, int size, BionicFile *stream)
{
    if (!s || size <= 0 || !stream) {
        return nullptr;
    }
    int idx = 0;
    while (idx < size - 1) {
        int c = portage_fgetc(stream);
        if (c == -1) {
            if (idx == 0) {
                return nullptr;
            }
            break;
        }
        s[idx++] = static_cast<char>(c);
        if (c == '\n') {
            break;
        }
    }
    s[idx] = '\0';
    return s;
}

__attribute__((sysv_abi)) int portage_ungetc(int c, BionicFile *stream)
{
    if (!stream || c == -1) {
        return -1;
    }
    stream->unget_char = c;
    stream->eof = false;
    return c;
}

__attribute__((sysv_abi)) void portage_setbuf(BionicFile *, char *) {}

__attribute__((sysv_abi)) int portage_setvbuf(BionicFile *, char *, int, size_t) { return 0; }

__attribute__((sysv_abi)) int portage_feof(BionicFile *stream)
{
    return stream ? (stream->eof ? 1 : 0) : 0;
}

__attribute__((sysv_abi)) int portage_ferror(BionicFile *stream)
{
    return stream ? (stream->error ? 1 : 0) : 0;
}

__attribute__((sysv_abi)) void portage_clearerr(BionicFile *stream)
{
    if (stream) {
        stream->eof = false;
        stream->error = false;
    }
}

__attribute__((sysv_abi)) int portage_fprintf(BionicFile *stream, const char *format, uintptr_t a0,
                                              uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                              uintptr_t a4, uintptr_t a5)
{
    if (!stream || !format) {
        return -1;
    }
    double fp[4]{};
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movsd %%xmm0, %0\n\t"
                     "movsd %%xmm1, %1\n\t"
                     "movsd %%xmm2, %2\n\t"
                     "movsd %%xmm3, %3"
                     : "=m"(fp[0]), "=m"(fp[1]), "=m"(fp[2]), "=m"(fp[3]));
#endif
    uintptr_t ints[6] = {a0, a1, a2, a3, a4, a5};
    RegArrayArgSource src(ints, 6, fp, 4);
    std::string formatted = FormatSysvPrintfToString(format, src);
    size_t written = portage_fwrite(formatted.data(), 1, formatted.size(), stream);
    return static_cast<int>(written);
}

__attribute__((sysv_abi)) int portage_vfprintf(BionicFile *stream, const char *format,
                                               SysvVaList *ap)
{
    if (!stream || !format) {
        return -1;
    }
    VaListArgSource src(ap);
    std::string formatted = FormatSysvPrintfToString(format, src);
    size_t written = portage_fwrite(formatted.data(), 1, formatted.size(), stream);
    return static_cast<int>(written);
}

__attribute__((sysv_abi)) int portage_sscanf(const char *str, const char *format, uintptr_t a0,
                                             uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
                                             uintptr_t a5)
{
    uintptr_t ints[6] = {a0, a1, a2, a3, a4, a5};
    RegArrayArgSource src(ints, 6);
    return ParseSysvSscanf(str, format, src);
}

__attribute__((sysv_abi)) int portage_vsscanf(const char *str, const char *format, SysvVaList *ap)
{
    VaListArgSource src(ap);
    return ParseSysvSscanf(str, format, src);
}

__attribute__((sysv_abi)) int portage_asprintf(char **strp, const char *format, uintptr_t a0,
                                               uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                               uintptr_t a4, uintptr_t a5)
{
    if (!strp || !format) {
        return -1;
    }
    double fp[4]{};
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movsd %%xmm0, %0\n\t"
                     "movsd %%xmm1, %1\n\t"
                     "movsd %%xmm2, %2\n\t"
                     "movsd %%xmm3, %3"
                     : "=m"(fp[0]), "=m"(fp[1]), "=m"(fp[2]), "=m"(fp[3]));
#endif
    uintptr_t ints[6] = {a0, a1, a2, a3, a4, a5};
    RegArrayArgSource src(ints, 6, fp, 4);
    std::string formatted = FormatSysvPrintfToString(format, src);
    char *buf = static_cast<char *>(std::malloc(formatted.size() + 1));
    if (!buf) {
        return -1;
    }
    std::memcpy(buf, formatted.data(), formatted.size());
    buf[formatted.size()] = '\0';
    *strp = buf;
    return static_cast<int>(formatted.size());
}

__attribute__((sysv_abi)) int portage_vasprintf(char **strp, const char *format, SysvVaList *ap)
{
    if (!strp || !format) {
        return -1;
    }
    VaListArgSource src(ap);
    std::string formatted = FormatSysvPrintfToString(format, src);
    char *buf = static_cast<char *>(std::malloc(formatted.size() + 1));
    if (!buf) {
        return -1;
    }
    std::memcpy(buf, formatted.data(), formatted.size());
    buf[formatted.size()] = '\0';
    *strp = buf;
    return static_cast<int>(formatted.size());
}

} // extern "C"
