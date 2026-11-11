#pragma once

#include "ndk/bionic_thunks.hpp"
#include <cstdio>
#include <cstdint>
#include <string>

namespace portage::ndk {

struct BionicFile {
    int fd{-1};
    int flags{0};
    int unget_char{-1};
    uint8_t is_open{0};
    uint8_t eof{0};
    uint8_t error{0};
    uint8_t own_fd{0};
    std::FILE *host_fp{nullptr};
    uint8_t reserved[152 - 24];
};
static_assert(sizeof(BionicFile) == 152,
              "BionicFile size must be exactly 152 bytes for Bionic LP64 __sF array");

void RegisterBionicStdio(NdkSymbolRegistry &registry);

} // namespace portage::ndk

extern "C" {

portage::ndk::BionicFile *portage_stdin(void);
portage::ndk::BionicFile *portage_stdout(void);
portage::ndk::BionicFile *portage_stderr(void);

__attribute__((sysv_abi)) portage::ndk::BionicFile *portage_fopen(const char *path,
                                                                  const char *mode);
__attribute__((sysv_abi)) portage::ndk::BionicFile *portage_fdopen(int fd, const char *mode);
__attribute__((sysv_abi)) int portage_fclose(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_fileno(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_fflush(portage::ndk::BionicFile *stream);

__attribute__((sysv_abi)) size_t portage_fread(void *ptr, size_t size, size_t nmemb,
                                               portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) size_t portage_fwrite(const void *ptr, size_t size, size_t nmemb,
                                                portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_fseek(portage::ndk::BionicFile *stream, long offset,
                                            int whence);
__attribute__((sysv_abi)) int portage_fseeko(portage::ndk::BionicFile *stream, int64_t offset,
                                             int whence);
__attribute__((sysv_abi)) long portage_ftell(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int64_t portage_ftello(portage::ndk::BionicFile *stream);

__attribute__((sysv_abi)) int portage_fputc(int c, portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_putc(int c, portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_fputs(const char *s, portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_puts(const char *s);
__attribute__((sysv_abi)) int portage_putchar(int c);

__attribute__((sysv_abi)) int portage_getc(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_fgetc(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) char *portage_fgets(char *s, int size, portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_ungetc(int c, portage::ndk::BionicFile *stream);

__attribute__((sysv_abi)) void portage_setbuf(portage::ndk::BionicFile *stream, char *buf);
__attribute__((sysv_abi)) int portage_setvbuf(portage::ndk::BionicFile *stream, char *buf, int mode,
                                              size_t size);
__attribute__((sysv_abi)) int portage_feof(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) int portage_ferror(portage::ndk::BionicFile *stream);
__attribute__((sysv_abi)) void portage_clearerr(portage::ndk::BionicFile *stream);

__attribute__((sysv_abi)) int portage_fprintf(portage::ndk::BionicFile *stream, const char *format,
                                              uintptr_t a0 = 0, uintptr_t a1 = 0, uintptr_t a2 = 0,
                                              uintptr_t a3 = 0, uintptr_t a4 = 0, uintptr_t a5 = 0);
__attribute__((sysv_abi)) int portage_vfprintf(portage::ndk::BionicFile *stream, const char *format,
                                               portage::ndk::SysvVaList *ap);

__attribute__((sysv_abi)) int portage_sscanf(const char *str, const char *format, uintptr_t a0 = 0,
                                             uintptr_t a1 = 0, uintptr_t a2 = 0, uintptr_t a3 = 0,
                                             uintptr_t a4 = 0, uintptr_t a5 = 0);
__attribute__((sysv_abi)) int portage_vsscanf(const char *str, const char *format,
                                              portage::ndk::SysvVaList *ap);

__attribute__((sysv_abi)) int portage_asprintf(char **strp, const char *format, uintptr_t a0 = 0,
                                               uintptr_t a1 = 0, uintptr_t a2 = 0, uintptr_t a3 = 0,
                                               uintptr_t a4 = 0, uintptr_t a5 = 0);
__attribute__((sysv_abi)) int portage_vasprintf(char **strp, const char *format,
                                                portage::ndk::SysvVaList *ap);

} // extern "C"
