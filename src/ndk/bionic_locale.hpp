#pragma once

#include "ndk/bionic_thunks.hpp"
#include <cstddef>
#include <cstdint>

namespace portage::ndk {

using BionicWchar = uint32_t;
static_assert(sizeof(BionicWchar) == 4, "BionicWchar must be exactly 4 bytes (UTF-32)");

struct BionicLconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char int_p_cs_precedes;
    char int_p_sep_by_space;
    char int_n_cs_precedes;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
};

void RegisterBionicLocale(NdkSymbolRegistry &registry);

} // namespace portage::ndk

extern "C" {

using SysvComparFn = __attribute__((sysv_abi)) int (*)(const void *, const void *);

// Sorting & Searching with SysV ABI comparator
__attribute__((sysv_abi)) void portage_qsort(void *base, size_t nmemb, size_t size,
                                             SysvComparFn compar);
__attribute__((sysv_abi)) void *portage_bsearch(const void *key, const void *base, size_t nmemb,
                                                size_t size, SysvComparFn compar);

// Number Parsing
__attribute__((sysv_abi)) long portage_strtol(const char *nptr, char **endptr, int base);
__attribute__((sysv_abi)) unsigned long portage_strtoul(const char *nptr, char **endptr, int base);
__attribute__((sysv_abi)) int64_t portage_strtoll(const char *nptr, char **endptr, int base);
__attribute__((sysv_abi)) uint64_t portage_strtoull(const char *nptr, char **endptr, int base);
__attribute__((sysv_abi)) float portage_strtof(const char *nptr, char **endptr);
__attribute__((sysv_abi)) double portage_strtod(const char *nptr, char **endptr);
__attribute__((sysv_abi)) long double portage_strtold(const char *nptr, char **endptr);

// Locale variants of number parsing
__attribute__((sysv_abi)) long portage_strtol_l(const char *nptr, char **endptr, int base,
                                                void *loc);
__attribute__((sysv_abi)) unsigned long portage_strtoul_l(const char *nptr, char **endptr, int base,
                                                          void *loc);
__attribute__((sysv_abi)) int64_t portage_strtoll_l(const char *nptr, char **endptr, int base,
                                                    void *loc);
__attribute__((sysv_abi)) uint64_t portage_strtoull_l(const char *nptr, char **endptr, int base,
                                                      void *loc);
__attribute__((sysv_abi)) float portage_strtof_l(const char *nptr, char **endptr, void *loc);
__attribute__((sysv_abi)) double portage_strtod_l(const char *nptr, char **endptr, void *loc);
__attribute__((sysv_abi)) long double portage_strtold_l(const char *nptr, char **endptr, void *loc);

// Locale APIs
__attribute__((sysv_abi)) size_t portage_ctype_get_mb_cur_max(void);
__attribute__((sysv_abi)) portage::ndk::BionicLconv *portage_localeconv(void);
__attribute__((sysv_abi)) void *portage_newlocale(int category_mask, const char *locale,
                                                  void *base);
__attribute__((sysv_abi)) void *portage_uselocale(void *newloc);
__attribute__((sysv_abi)) void portage_freelocale(void *loc);
__attribute__((sysv_abi)) char *portage_setlocale(int category, const char *locale);
__attribute__((sysv_abi)) char *portage_nl_langinfo(int item);
__attribute__((sysv_abi)) char *portage_nl_langinfo_l(int item, void *loc);

// UTF-32 (4-byte) Wide-Character APIs
__attribute__((sysv_abi)) size_t portage_mbrtowc(portage::ndk::BionicWchar *pwc, const char *s,
                                                 size_t n, void *ps);
__attribute__((sysv_abi)) size_t portage_wcrtomb(char *s, portage::ndk::BionicWchar wc, void *ps);
__attribute__((sysv_abi)) size_t portage_mbsnrtowcs(portage::ndk::BionicWchar *dst,
                                                    const char **src, size_t nmc, size_t len,
                                                    void *ps);
__attribute__((sysv_abi)) size_t portage_wcsnrtombs(char *dst,
                                                    const portage::ndk::BionicWchar **src,
                                                    size_t nwc, size_t len, void *ps);
__attribute__((sysv_abi)) size_t portage_wcslen(const portage::ndk::BionicWchar *s);
__attribute__((sysv_abi)) const portage::ndk::BionicWchar *
portage_wmemchr(const portage::ndk::BionicWchar *s, portage::ndk::BionicWchar c, size_t n);
__attribute__((sysv_abi)) int portage_wmemcmp(const portage::ndk::BionicWchar *s1,
                                              const portage::ndk::BionicWchar *s2, size_t n);
__attribute__((sysv_abi)) portage::ndk::BionicWchar *
portage_wmemcpy(portage::ndk::BionicWchar *d, const portage::ndk::BionicWchar *s, size_t n);
__attribute__((sysv_abi)) portage::ndk::BionicWchar *
portage_wmemmove(portage::ndk::BionicWchar *d, const portage::ndk::BionicWchar *s, size_t n);
__attribute__((sysv_abi)) portage::ndk::BionicWchar *
portage_wmemset(portage::ndk::BionicWchar *s, portage::ndk::BionicWchar c, size_t n);
__attribute__((sysv_abi)) int portage_wcscmp(const portage::ndk::BionicWchar *s1,
                                             const portage::ndk::BionicWchar *s2);
__attribute__((sysv_abi)) portage::ndk::BionicWchar *
portage_wcscpy(portage::ndk::BionicWchar *d, const portage::ndk::BionicWchar *s);
__attribute__((sysv_abi)) portage::ndk::BionicWchar *
portage_wcsncpy(portage::ndk::BionicWchar *d, const portage::ndk::BionicWchar *s, size_t n);
__attribute__((sysv_abi)) long portage_wcstol(const portage::ndk::BionicWchar *nptr,
                                              portage::ndk::BionicWchar **endptr, int base);
__attribute__((sysv_abi)) int64_t portage_wcstoll(const portage::ndk::BionicWchar *nptr,
                                                  portage::ndk::BionicWchar **endptr, int base);
__attribute__((sysv_abi)) unsigned long portage_wcstoul(const portage::ndk::BionicWchar *nptr,
                                                        portage::ndk::BionicWchar **endptr,
                                                        int base);
__attribute__((sysv_abi)) uint64_t portage_wcstoull(const portage::ndk::BionicWchar *nptr,
                                                    portage::ndk::BionicWchar **endptr, int base);
__attribute__((sysv_abi)) float portage_wcstof(const portage::ndk::BionicWchar *nptr,
                                               portage::ndk::BionicWchar **endptr);
__attribute__((sysv_abi)) double portage_wcstod(const portage::ndk::BionicWchar *nptr,
                                                portage::ndk::BionicWchar **endptr);
__attribute__((sysv_abi)) long double portage_wcstold(const portage::ndk::BionicWchar *nptr,
                                                      portage::ndk::BionicWchar **endptr);
__attribute__((sysv_abi)) int portage_wctob(portage::ndk::BionicWchar c);
__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_btowc(int c);

// Diagnostics & Abort
__attribute__((sysv_abi)) void portage_android_set_abort_message(const char *msg);
const char *portage_get_abort_message(void);

// Logging
__attribute__((sysv_abi)) void portage_openlog(const char *ident, int option, int facility);
__attribute__((sysv_abi)) void portage_syslog(int priority, const char *format, uintptr_t a0 = 0,
                                              uintptr_t a1 = 0, uintptr_t a2 = 0, uintptr_t a3 = 0);
__attribute__((sysv_abi)) void portage_closelog(void);

// Memory searching
__attribute__((sysv_abi)) void *portage_memchr(const void *s, int c, size_t n);

// Wide-character classification & case mapping
__attribute__((sysv_abi)) int portage_iswalpha_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswblank_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswcntrl_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswdigit_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswlower_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswprint_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswpunct_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswspace_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswupper_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) int portage_iswxdigit_l(portage::ndk::BionicWchar wc, void *loc);
__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_towlower_l(portage::ndk::BionicWchar wc,
                                                                       void *loc);
__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_towupper_l(portage::ndk::BionicWchar wc,
                                                                       void *loc);

// String/wide-string collation & transform
__attribute__((sysv_abi)) int portage_strcoll_l(const char *s1, const char *s2, void *loc);
__attribute__((sysv_abi)) size_t portage_strxfrm_l(char *dest, const char *src, size_t n,
                                                   void *loc);
__attribute__((sysv_abi)) int portage_wcscoll_l(const portage::ndk::BionicWchar *ws1,
                                                const portage::ndk::BionicWchar *ws2, void *loc);
__attribute__((sysv_abi)) size_t portage_wcsxfrm_l(portage::ndk::BionicWchar *dest,
                                                   const portage::ndk::BionicWchar *src, size_t n,
                                                   void *loc);
__attribute__((sysv_abi)) size_t portage_strftime_l(char *s, size_t max, const char *format,
                                                    const void *tm, void *loc);

// Additional multibyte conversions & wide I/O
__attribute__((sysv_abi)) size_t portage_mbrlen(const char *s, size_t n, void *ps);
__attribute__((sysv_abi)) size_t portage_mbsrtowcs(portage::ndk::BionicWchar *dest,
                                                   const char **src, size_t len, void *ps);
__attribute__((sysv_abi)) int portage_mbtowc(portage::ndk::BionicWchar *pwc, const char *s,
                                             size_t n);
__attribute__((sysv_abi)) int portage_swprintf(portage::ndk::BionicWchar *s, size_t n,
                                               const portage::ndk::BionicWchar *format,
                                               uintptr_t a0 = 0, uintptr_t a1 = 0, uintptr_t a2 = 0,
                                               uintptr_t a3 = 0, uintptr_t a4 = 0,
                                               uintptr_t a5 = 0);
__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_fputwc(portage::ndk::BionicWchar c,
                                                                   void *stream);
__attribute__((sysv_abi)) int32_t portage_getwc(void *stream);
__attribute__((sysv_abi)) int32_t portage_ungetwc(portage::ndk::BionicWchar c, void *stream);

} // extern "C"
