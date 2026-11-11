#include "ndk/bionic_locale.hpp"
#include "ndk/android_log.hpp"
#include "ndk/bionic_stdio.hpp"
#include "ndk/libandroid_thunks.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace portage::ndk {

static char g_dot[] = ".";
static char g_empty[] = "";

static BionicLconv g_posix_lconv = {
    .decimal_point = g_dot,
    .thousands_sep = g_empty,
    .grouping = g_empty,
    .int_curr_symbol = g_empty,
    .currency_symbol = g_empty,
    .mon_decimal_point = g_empty,
    .mon_thousands_sep = g_empty,
    .mon_grouping = g_empty,
    .positive_sign = g_empty,
    .negative_sign = g_empty,
    .int_frac_digits = 127,
    .frac_digits = 127,
    .p_cs_precedes = 127,
    .p_sep_by_space = 127,
    .n_cs_precedes = 127,
    .n_sep_by_space = 127,
    .p_sign_posn = 127,
    .n_sign_posn = 127,
    .int_p_cs_precedes = 127,
    .int_p_sep_by_space = 127,
    .int_n_cs_precedes = 127,
    .int_n_sep_by_space = 127,
    .int_p_sign_posn = 127,
    .int_n_sign_posn = 127,
};

static std::string g_abort_message;

void RegisterBionicLocale(NdkSymbolRegistry &registry)
{
    registry.Register("qsort", reinterpret_cast<uintptr_t>(&portage_qsort));
    registry.Register("bsearch", reinterpret_cast<uintptr_t>(&portage_bsearch));

    registry.Register("strtol", reinterpret_cast<uintptr_t>(&portage_strtol));
    registry.Register("strtoul", reinterpret_cast<uintptr_t>(&portage_strtoul));
    registry.Register("strtoll", reinterpret_cast<uintptr_t>(&portage_strtoll));
    registry.Register("strtoull", reinterpret_cast<uintptr_t>(&portage_strtoull));
    registry.Register("strtof", reinterpret_cast<uintptr_t>(&portage_strtof));
    registry.Register("strtod", reinterpret_cast<uintptr_t>(&portage_strtod));
    registry.Register("strtold", reinterpret_cast<uintptr_t>(&portage_strtold));

    registry.Register("strtol_l", reinterpret_cast<uintptr_t>(&portage_strtol_l));
    registry.Register("strtoul_l", reinterpret_cast<uintptr_t>(&portage_strtoul_l));
    registry.Register("strtoll_l", reinterpret_cast<uintptr_t>(&portage_strtoll_l));
    registry.Register("strtoull_l", reinterpret_cast<uintptr_t>(&portage_strtoull_l));
    registry.Register("strtof_l", reinterpret_cast<uintptr_t>(&portage_strtof_l));
    registry.Register("strtod_l", reinterpret_cast<uintptr_t>(&portage_strtod_l));
    registry.Register("strtold_l", reinterpret_cast<uintptr_t>(&portage_strtold_l));

    registry.Register("__ctype_get_mb_cur_max",
                      reinterpret_cast<uintptr_t>(&portage_ctype_get_mb_cur_max));
    registry.Register("localeconv", reinterpret_cast<uintptr_t>(&portage_localeconv));
    registry.Register("newlocale", reinterpret_cast<uintptr_t>(&portage_newlocale));
    registry.Register("uselocale", reinterpret_cast<uintptr_t>(&portage_uselocale));
    registry.Register("freelocale", reinterpret_cast<uintptr_t>(&portage_freelocale));
    registry.Register("setlocale", reinterpret_cast<uintptr_t>(&portage_setlocale));
    registry.Register("nl_langinfo", reinterpret_cast<uintptr_t>(&portage_nl_langinfo));
    registry.Register("nl_langinfo_l", reinterpret_cast<uintptr_t>(&portage_nl_langinfo_l));

    registry.Register("mbrtowc", reinterpret_cast<uintptr_t>(&portage_mbrtowc));
    registry.Register("wcrtomb", reinterpret_cast<uintptr_t>(&portage_wcrtomb));
    registry.Register("mbsnrtowcs", reinterpret_cast<uintptr_t>(&portage_mbsnrtowcs));
    registry.Register("wcsnrtombs", reinterpret_cast<uintptr_t>(&portage_wcsnrtombs));
    registry.Register("wcslen", reinterpret_cast<uintptr_t>(&portage_wcslen));
    registry.Register("wmemchr", reinterpret_cast<uintptr_t>(&portage_wmemchr));
    registry.Register("wmemcmp", reinterpret_cast<uintptr_t>(&portage_wmemcmp));
    registry.Register("wmemcpy", reinterpret_cast<uintptr_t>(&portage_wmemcpy));
    registry.Register("wmemmove", reinterpret_cast<uintptr_t>(&portage_wmemmove));
    registry.Register("wmemset", reinterpret_cast<uintptr_t>(&portage_wmemset));
    registry.Register("wcscmp", reinterpret_cast<uintptr_t>(&portage_wcscmp));
    registry.Register("wcscpy", reinterpret_cast<uintptr_t>(&portage_wcscpy));
    registry.Register("wcsncpy", reinterpret_cast<uintptr_t>(&portage_wcsncpy));
    registry.Register("wcstol", reinterpret_cast<uintptr_t>(&portage_wcstol));
    registry.Register("wcstoll", reinterpret_cast<uintptr_t>(&portage_wcstoll));
    registry.Register("wcstoul", reinterpret_cast<uintptr_t>(&portage_wcstoul));
    registry.Register("wcstoull", reinterpret_cast<uintptr_t>(&portage_wcstoull));
    registry.Register("wcstof", reinterpret_cast<uintptr_t>(&portage_wcstof));
    registry.Register("wcstod", reinterpret_cast<uintptr_t>(&portage_wcstod));
    registry.Register("wcstold", reinterpret_cast<uintptr_t>(&portage_wcstold));
    registry.Register("wctob", reinterpret_cast<uintptr_t>(&portage_wctob));
    registry.Register("btowc", reinterpret_cast<uintptr_t>(&portage_btowc));

    registry.Register("closelog", reinterpret_cast<uintptr_t>(&portage_closelog));
    registry.Register("fputwc", reinterpret_cast<uintptr_t>(&portage_fputwc));
    registry.Register("getwc", reinterpret_cast<uintptr_t>(&portage_getwc));
    registry.Register("iswalpha_l", reinterpret_cast<uintptr_t>(&portage_iswalpha_l));
    registry.Register("iswblank_l", reinterpret_cast<uintptr_t>(&portage_iswblank_l));
    registry.Register("iswcntrl_l", reinterpret_cast<uintptr_t>(&portage_iswcntrl_l));
    registry.Register("iswdigit_l", reinterpret_cast<uintptr_t>(&portage_iswdigit_l));
    registry.Register("iswlower_l", reinterpret_cast<uintptr_t>(&portage_iswlower_l));
    registry.Register("iswprint_l", reinterpret_cast<uintptr_t>(&portage_iswprint_l));
    registry.Register("iswpunct_l", reinterpret_cast<uintptr_t>(&portage_iswpunct_l));
    registry.Register("iswspace_l", reinterpret_cast<uintptr_t>(&portage_iswspace_l));
    registry.Register("iswupper_l", reinterpret_cast<uintptr_t>(&portage_iswupper_l));
    registry.Register("iswxdigit_l", reinterpret_cast<uintptr_t>(&portage_iswxdigit_l));
    registry.Register("mbrlen", reinterpret_cast<uintptr_t>(&portage_mbrlen));
    registry.Register("mbsrtowcs", reinterpret_cast<uintptr_t>(&portage_mbsrtowcs));
    registry.Register("mbtowc", reinterpret_cast<uintptr_t>(&portage_mbtowc));
    registry.Register("memchr", reinterpret_cast<uintptr_t>(&portage_memchr));
    registry.Register("openlog", reinterpret_cast<uintptr_t>(&portage_openlog));
    registry.Register("strcoll_l", reinterpret_cast<uintptr_t>(&portage_strcoll_l));
    registry.Register("strftime_l", reinterpret_cast<uintptr_t>(&portage_strftime_l));
    registry.Register("strxfrm_l", reinterpret_cast<uintptr_t>(&portage_strxfrm_l));
    registry.Register("swprintf", reinterpret_cast<uintptr_t>(&portage_swprintf));
    registry.Register("syslog", reinterpret_cast<uintptr_t>(&portage_syslog));
    registry.Register("towlower_l", reinterpret_cast<uintptr_t>(&portage_towlower_l));
    registry.Register("towupper_l", reinterpret_cast<uintptr_t>(&portage_towupper_l));
    registry.Register("ungetwc", reinterpret_cast<uintptr_t>(&portage_ungetwc));
    registry.Register("wcscoll_l", reinterpret_cast<uintptr_t>(&portage_wcscoll_l));
    registry.Register("wcsxfrm_l", reinterpret_cast<uintptr_t>(&portage_wcsxfrm_l));

    registry.Register("android_set_abort_message",
                      reinterpret_cast<uintptr_t>(&portage_android_set_abort_message));
}

} // namespace portage::ndk

extern "C" {

using namespace portage::ndk;

static void InsertionSortSysv(uint8_t *base, size_t nmemb, size_t size, SysvComparFn compar)
{
    std::vector<uint8_t> tmp(size);
    for (size_t i = 1; i < nmemb; ++i) {
        std::memcpy(tmp.data(), base + i * size, size);
        size_t j = i;
        while (j > 0 && compar(base + (j - 1) * size, tmp.data()) > 0) {
            std::memcpy(base + j * size, base + (j - 1) * size, size);
            j--;
        }
        std::memcpy(base + j * size, tmp.data(), size);
    }
}

static void QuickSortSysv(uint8_t *base, size_t nmemb, size_t size, SysvComparFn compar)
{
    if (nmemb <= 1) {
        return;
    }
    if (nmemb <= 16) {
        InsertionSortSysv(base, nmemb, size, compar);
        return;
    }

    uint8_t *pivot = base + (nmemb / 2) * size;
    std::vector<uint8_t> pval(size);
    std::memcpy(pval.data(), pivot, size);

    size_t i = 0;
    size_t j = nmemb - 1;
    std::vector<uint8_t> tmp(size);

    while (i <= j) {
        while (compar(base + i * size, pval.data()) < 0) {
            i++;
        }
        while (compar(base + j * size, pval.data()) > 0) {
            if (j == 0) {
                break;
            }
            j--;
        }
        if (i <= j) {
            std::memcpy(tmp.data(), base + i * size, size);
            std::memcpy(base + i * size, base + j * size, size);
            std::memcpy(base + j * size, tmp.data(), size);
            i++;
            if (j == 0) {
                break;
            }
            j--;
        }
    }
    if (j > 0) {
        QuickSortSysv(base, j + 1, size, compar);
    }
    if (i < nmemb) {
        QuickSortSysv(base + i * size, nmemb - i, size, compar);
    }
}

__attribute__((sysv_abi)) void portage_qsort(void *base, size_t nmemb, size_t size,
                                             SysvComparFn compar)
{
    if (!base || nmemb <= 1 || size == 0 || !compar) {
        return;
    }
    QuickSortSysv(static_cast<uint8_t *>(base), nmemb, size, compar);
}

__attribute__((sysv_abi)) void *portage_bsearch(const void *key, const void *base, size_t nmemb,
                                                size_t size, SysvComparFn compar)
{
    if (!key || !base || nmemb == 0 || size == 0 || !compar) {
        return nullptr;
    }
    const auto *bytes = static_cast<const uint8_t *>(base);
    size_t low = 0;
    size_t high = nmemb;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const void *mid_ptr = bytes + mid * size;
        int cmp = compar(key, mid_ptr);
        if (cmp == 0) {
            return const_cast<void *>(mid_ptr);
        }
        if (cmp < 0) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return nullptr;
}

__attribute__((sysv_abi)) long portage_strtol(const char *nptr, char **endptr, int base)
{
    return static_cast<long>(portage_strtoll(nptr, endptr, base));
}

__attribute__((sysv_abi)) unsigned long portage_strtoul(const char *nptr, char **endptr, int base)
{
    return static_cast<unsigned long>(portage_strtoull(nptr, endptr, base));
}

__attribute__((sysv_abi)) int64_t portage_strtoll(const char *nptr, char **endptr, int base)
{
    return std::strtoll(nptr, endptr, base);
}

__attribute__((sysv_abi)) uint64_t portage_strtoull(const char *nptr, char **endptr, int base)
{
    return std::strtoull(nptr, endptr, base);
}

__attribute__((sysv_abi)) float portage_strtof(const char *nptr, char **endptr)
{
    return std::strtof(nptr, endptr);
}

__attribute__((sysv_abi)) double portage_strtod(const char *nptr, char **endptr)
{
    return std::strtod(nptr, endptr);
}

__attribute__((sysv_abi)) long double portage_strtold(const char *nptr, char **endptr)
{
    return std::strtold(nptr, endptr);
}

__attribute__((sysv_abi)) long portage_strtol_l(const char *nptr, char **endptr, int base, void *)
{
    return portage_strtol(nptr, endptr, base);
}

__attribute__((sysv_abi)) unsigned long portage_strtoul_l(const char *nptr, char **endptr, int base,
                                                          void *)
{
    return portage_strtoul(nptr, endptr, base);
}

__attribute__((sysv_abi)) int64_t portage_strtoll_l(const char *nptr, char **endptr, int base,
                                                    void *)
{
    return portage_strtoll(nptr, endptr, base);
}

__attribute__((sysv_abi)) uint64_t portage_strtoull_l(const char *nptr, char **endptr, int base,
                                                      void *)
{
    return portage_strtoull(nptr, endptr, base);
}

__attribute__((sysv_abi)) float portage_strtof_l(const char *nptr, char **endptr, void *)
{
    return portage_strtof(nptr, endptr);
}

__attribute__((sysv_abi)) double portage_strtod_l(const char *nptr, char **endptr, void *)
{
    return portage_strtod(nptr, endptr);
}

__attribute__((sysv_abi)) long double portage_strtold_l(const char *nptr, char **endptr, void *)
{
    return portage_strtold(nptr, endptr);
}

__attribute__((sysv_abi)) size_t portage_ctype_get_mb_cur_max(void) { return 4; }

__attribute__((sysv_abi)) BionicLconv *portage_localeconv(void) { return &g_posix_lconv; }

__attribute__((sysv_abi)) void *portage_newlocale(int, const char *, void *)
{
    return reinterpret_cast<void *>(0x1000);
}

__attribute__((sysv_abi)) void *portage_uselocale(void *)
{
    return reinterpret_cast<void *>(0x1000);
}

__attribute__((sysv_abi)) void portage_freelocale(void *) {}

__attribute__((sysv_abi)) char *portage_setlocale(int, const char *)
{
    static char c_locale[] = "C.UTF-8";
    return c_locale;
}

__attribute__((sysv_abi)) char *portage_nl_langinfo(int item)
{
    static char utf8[] = "UTF-8";
    static char empty[] = "";
    if (item == 14) { // CODESET
        return utf8;
    }
    return empty;
}

__attribute__((sysv_abi)) char *portage_nl_langinfo_l(int item, void *)
{
    return portage_nl_langinfo(item);
}

__attribute__((sysv_abi)) size_t portage_mbrtowc(BionicWchar *pwc, const char *s, size_t n, void *)
{
    if (!s) {
        return 0;
    }
    if (n == 0) {
        return static_cast<size_t>(-2);
    }

    uint8_t c0 = static_cast<uint8_t>(s[0]);
    if (c0 == 0) {
        if (pwc) {
            *pwc = 0;
        }
        return 0;
    }
    if ((c0 & 0x80) == 0) {
        if (pwc) {
            *pwc = c0;
        }
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0) {
        if (n < 2) {
            return static_cast<size_t>(-2);
        }
        uint8_t c1 = static_cast<uint8_t>(s[1]);
        if ((c1 & 0xC0) != 0x80) {
            *portage_errno() = 84; // EILSEQ
            return static_cast<size_t>(-1);
        }
        if (pwc) {
            *pwc = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        }
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (n < 3) {
            return static_cast<size_t>(-2);
        }
        uint8_t c1 = static_cast<uint8_t>(s[1]);
        uint8_t c2 = static_cast<uint8_t>(s[2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            *portage_errno() = 84;
            return static_cast<size_t>(-1);
        }
        if (pwc) {
            *pwc = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        }
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (n < 4) {
            return static_cast<size_t>(-2);
        }
        uint8_t c1 = static_cast<uint8_t>(s[1]);
        uint8_t c2 = static_cast<uint8_t>(s[2]);
        uint8_t c3 = static_cast<uint8_t>(s[3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            *portage_errno() = 84;
            return static_cast<size_t>(-1);
        }
        if (pwc) {
            *pwc = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
        return 4;
    }

    *portage_errno() = 84;
    return static_cast<size_t>(-1);
}

__attribute__((sysv_abi)) size_t portage_wcrtomb(char *s, BionicWchar wc, void *)
{
    if (!s) {
        return 1;
    }
    if (wc <= 0x7F) {
        s[0] = static_cast<char>(wc);
        return 1;
    }
    if (wc <= 0x7FF) {
        s[0] = static_cast<char>(0xC0 | ((wc >> 6) & 0x1F));
        s[1] = static_cast<char>(0x80 | (wc & 0x3F));
        return 2;
    }
    if (wc <= 0xFFFF) {
        s[0] = static_cast<char>(0xE0 | ((wc >> 12) & 0x0F));
        s[1] = static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
        s[2] = static_cast<char>(0x80 | (wc & 0x3F));
        return 3;
    }
    if (wc <= 0x10FFFF) {
        s[0] = static_cast<char>(0xF0 | ((wc >> 18) & 0x07));
        s[1] = static_cast<char>(0x80 | ((wc >> 12) & 0x3F));
        s[2] = static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
        s[3] = static_cast<char>(0x80 | (wc & 0x3F));
        return 4;
    }
    *portage_errno() = 84;
    return static_cast<size_t>(-1);
}

__attribute__((sysv_abi)) size_t portage_mbsnrtowcs(BionicWchar *dst, const char **src, size_t nmc,
                                                    size_t len, void *ps)
{
    if (!src || !*src) {
        return 0;
    }
    const char *s = *src;
    size_t in_bytes = 0;
    size_t out_chars = 0;

    while (in_bytes < nmc && (!dst || out_chars < len)) {
        BionicWchar wc = 0;
        size_t consumed = portage_mbrtowc(&wc, s + in_bytes, nmc - in_bytes, ps);
        if (consumed == 0) {
            if (dst) {
                dst[out_chars] = 0;
            }
            *src = nullptr;
            return out_chars;
        }
        if (consumed == static_cast<size_t>(-1) || consumed == static_cast<size_t>(-2)) {
            *src = s + in_bytes;
            return static_cast<size_t>(-1);
        }
        if (dst) {
            dst[out_chars] = wc;
        }
        out_chars++;
        in_bytes += consumed;
    }
    *src = s + in_bytes;
    return out_chars;
}

__attribute__((sysv_abi)) size_t portage_wcsnrtombs(char *dst, const BionicWchar **src, size_t nwc,
                                                    size_t len, void *ps)
{
    if (!src || !*src) {
        return 0;
    }
    const BionicWchar *ws = *src;
    size_t in_chars = 0;
    size_t out_bytes = 0;
    char buf[8]{};

    while (in_chars < nwc) {
        BionicWchar wc = ws[in_chars];
        if (wc == 0) {
            if (dst && out_bytes < len) {
                dst[out_bytes] = '\0';
            }
            *src = nullptr;
            return out_bytes;
        }
        size_t n = portage_wcrtomb(buf, wc, ps);
        if (n == static_cast<size_t>(-1)) {
            *src = ws + in_chars;
            return static_cast<size_t>(-1);
        }
        if (dst) {
            if (out_bytes + n > len) {
                break;
            }
            std::memcpy(dst + out_bytes, buf, n);
        }
        out_bytes += n;
        in_chars++;
    }
    *src = ws + in_chars;
    return out_bytes;
}

__attribute__((sysv_abi)) size_t portage_wcslen(const BionicWchar *s)
{
    if (!s) {
        return 0;
    }
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

__attribute__((sysv_abi)) const BionicWchar *portage_wmemchr(const BionicWchar *s, BionicWchar c,
                                                             size_t n)
{
    if (!s) {
        return nullptr;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == c) {
            return s + i;
        }
    }
    return nullptr;
}

__attribute__((sysv_abi)) int portage_wmemcmp(const BionicWchar *s1, const BionicWchar *s2,
                                              size_t n)
{
    if (!s1 || !s2 || n == 0) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i]) {
            return (s1[i] > s2[i]) ? 1 : -1;
        }
    }
    return 0;
}

__attribute__((sysv_abi)) BionicWchar *portage_wmemcpy(BionicWchar *d, const BionicWchar *s,
                                                       size_t n)
{
    if (d && s && n > 0) {
        std::memcpy(d, s, n * sizeof(BionicWchar));
    }
    return d;
}

__attribute__((sysv_abi)) BionicWchar *portage_wmemmove(BionicWchar *d, const BionicWchar *s,
                                                        size_t n)
{
    if (d && s && n > 0) {
        std::memmove(d, s, n * sizeof(BionicWchar));
    }
    return d;
}

__attribute__((sysv_abi)) BionicWchar *portage_wmemset(BionicWchar *s, BionicWchar c, size_t n)
{
    if (s && n > 0) {
        for (size_t i = 0; i < n; ++i) {
            s[i] = c;
        }
    }
    return s;
}

__attribute__((sysv_abi)) int portage_wcscmp(const BionicWchar *s1, const BionicWchar *s2)
{
    if (!s1 || !s2) {
        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    }
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (*s1 > *s2) - (*s1 < *s2);
}

__attribute__((sysv_abi)) BionicWchar *portage_wcscpy(BionicWchar *d, const BionicWchar *s)
{
    if (!d || !s) {
        return d;
    }
    BionicWchar *orig = d;
    while ((*d++ = *s++)) {
    }
    return orig;
}

__attribute__((sysv_abi)) BionicWchar *portage_wcsncpy(BionicWchar *d, const BionicWchar *s,
                                                       size_t n)
{
    if (!d || !s || n == 0) {
        return d;
    }
    size_t i = 0;
    for (; i < n && s[i] != 0; ++i) {
        d[i] = s[i];
    }
    for (; i < n; ++i) {
        d[i] = 0;
    }
    return d;
}

static std::string WcsToUtf8(const BionicWchar *ws)
{
    if (!ws) {
        return {};
    }
    std::string res;
    char buf[8]{};
    while (*ws) {
        size_t n = portage_wcrtomb(buf, *ws++, nullptr);
        if (n != static_cast<size_t>(-1)) {
            res.append(buf, n);
        }
    }
    return res;
}

__attribute__((sysv_abi)) long portage_wcstol(const BionicWchar *nptr, BionicWchar **endptr,
                                              int base)
{
    return static_cast<long>(portage_wcstoll(nptr, endptr, base));
}

__attribute__((sysv_abi)) int64_t portage_wcstoll(const BionicWchar *nptr, BionicWchar **endptr,
                                                  int base)
{
    std::string s = WcsToUtf8(nptr);
    char *c_end = nullptr;
    int64_t val = std::strtoll(s.c_str(), &c_end, base);
    if (endptr && nptr) {
        size_t consumed_chars = 0;
        if (c_end) {
            consumed_chars = static_cast<size_t>(c_end - s.c_str());
        }
        *endptr = const_cast<BionicWchar *>(nptr + consumed_chars);
    }
    return val;
}

__attribute__((sysv_abi)) unsigned long portage_wcstoul(const BionicWchar *nptr,
                                                        BionicWchar **endptr, int base)
{
    return static_cast<unsigned long>(portage_wcstoull(nptr, endptr, base));
}

__attribute__((sysv_abi)) uint64_t portage_wcstoull(const BionicWchar *nptr, BionicWchar **endptr,
                                                    int base)
{
    std::string s = WcsToUtf8(nptr);
    char *c_end = nullptr;
    uint64_t val = std::strtoull(s.c_str(), &c_end, base);
    if (endptr && nptr) {
        size_t consumed_chars = 0;
        if (c_end) {
            consumed_chars = static_cast<size_t>(c_end - s.c_str());
        }
        *endptr = const_cast<BionicWchar *>(nptr + consumed_chars);
    }
    return val;
}

__attribute__((sysv_abi)) float portage_wcstof(const BionicWchar *nptr, BionicWchar **endptr)
{
    std::string s = WcsToUtf8(nptr);
    char *c_end = nullptr;
    float val = std::strtof(s.c_str(), &c_end);
    if (endptr && nptr) {
        size_t consumed_chars = 0;
        if (c_end) {
            consumed_chars = static_cast<size_t>(c_end - s.c_str());
        }
        *endptr = const_cast<BionicWchar *>(nptr + consumed_chars);
    }
    return val;
}

__attribute__((sysv_abi)) double portage_wcstod(const BionicWchar *nptr, BionicWchar **endptr)
{
    std::string s = WcsToUtf8(nptr);
    char *c_end = nullptr;
    double val = std::strtod(s.c_str(), &c_end);
    if (endptr && nptr) {
        size_t consumed_chars = 0;
        if (c_end) {
            consumed_chars = static_cast<size_t>(c_end - s.c_str());
        }
        *endptr = const_cast<BionicWchar *>(nptr + consumed_chars);
    }
    return val;
}

__attribute__((sysv_abi)) long double portage_wcstold(const BionicWchar *nptr, BionicWchar **endptr)
{
    std::string s = WcsToUtf8(nptr);
    char *c_end = nullptr;
    long double val = std::strtold(s.c_str(), &c_end);
    if (endptr && nptr) {
        size_t consumed_chars = 0;
        if (c_end) {
            consumed_chars = static_cast<size_t>(c_end - s.c_str());
        }
        *endptr = const_cast<BionicWchar *>(nptr + consumed_chars);
    }
    return val;
}

__attribute__((sysv_abi)) int portage_wctob(BionicWchar c)
{
    return (c <= 0x7F) ? static_cast<int>(c) : -1;
}

__attribute__((sysv_abi)) BionicWchar portage_btowc(int c)
{
    return (c >= 0 && c <= 0x7F) ? static_cast<BionicWchar>(c) : 0xFFFFFFFFU;
}

__attribute__((sysv_abi)) void portage_android_set_abort_message(const char *msg)
{
    if (msg) {
        g_abort_message = msg;
        LogcatBuffer::Instance().Append(ANDROID_LOG_FATAL, "libc", msg);
    }
}

const char *portage_get_abort_message(void)
{
    return g_abort_message.empty() ? nullptr : g_abort_message.c_str();
}

__attribute__((sysv_abi)) void portage_openlog(const char *ident, int option, int facility)
{
    (void)ident;
    (void)option;
    (void)facility;
}

__attribute__((sysv_abi)) void portage_syslog(int priority, const char *format, uintptr_t a0,
                                              uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
    (void)priority;
    (void)format;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
}

__attribute__((sysv_abi)) void portage_closelog(void) {}

__attribute__((sysv_abi)) void *portage_memchr(const void *s, int c, size_t n)
{
    if (!s || n == 0) {
        return nullptr;
    }
    return const_cast<void *>(std::memchr(s, c, n));
}

__attribute__((sysv_abi)) int portage_iswalpha_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isalpha(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswblank_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isblank(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswcntrl_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::iscntrl(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswdigit_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isdigit(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswlower_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::islower(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswprint_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isprint(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswpunct_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::ispunct(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswspace_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isspace(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswupper_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isupper(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) int portage_iswxdigit_l(portage::ndk::BionicWchar wc, void *loc)
{
    (void)loc;
    return (wc < 128) ? (std::isxdigit(static_cast<int>(wc)) != 0) : 0;
}

__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_towlower_l(portage::ndk::BionicWchar wc,
                                                                       void *loc)
{
    (void)loc;
    return (wc < 128) ? static_cast<portage::ndk::BionicWchar>(std::tolower(static_cast<int>(wc)))
                      : wc;
}

__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_towupper_l(portage::ndk::BionicWchar wc,
                                                                       void *loc)
{
    (void)loc;
    return (wc < 128) ? static_cast<portage::ndk::BionicWchar>(std::toupper(static_cast<int>(wc)))
                      : wc;
}

__attribute__((sysv_abi)) int portage_strcoll_l(const char *s1, const char *s2, void *loc)
{
    (void)loc;
    if (!s1 || !s2) {
        return 0;
    }
    return std::strcmp(s1, s2);
}

__attribute__((sysv_abi)) size_t portage_strxfrm_l(char *dest, const char *src, size_t n, void *loc)
{
    (void)loc;
    if (!src) {
        return 0;
    }
    size_t len = std::strlen(src);
    if (dest && n > 0) {
        size_t cp = (len < n - 1) ? len : (n - 1);
        std::memcpy(dest, src, cp);
        dest[cp] = '\0';
    }
    return len;
}

__attribute__((sysv_abi)) int portage_wcscoll_l(const portage::ndk::BionicWchar *ws1,
                                                const portage::ndk::BionicWchar *ws2, void *loc)
{
    (void)loc;
    return portage_wcscmp(ws1, ws2);
}

__attribute__((sysv_abi)) size_t portage_wcsxfrm_l(portage::ndk::BionicWchar *dest,
                                                   const portage::ndk::BionicWchar *src, size_t n,
                                                   void *loc)
{
    (void)loc;
    if (!src) {
        return 0;
    }
    size_t len = portage_wcslen(src);
    if (dest && n > 0) {
        size_t cp = (len < n - 1) ? len : (n - 1);
        for (size_t i = 0; i < cp; ++i) {
            dest[i] = src[i];
        }
        dest[cp] = 0;
    }
    return len;
}

__attribute__((sysv_abi)) size_t portage_strftime_l(char *s, size_t max, const char *format,
                                                    const void *tm, void *loc)
{
    (void)loc;
    if (!s || max == 0 || !tm) {
        return 0;
    }
    return std::strftime(s, max, format ? format : "", reinterpret_cast<const struct tm *>(tm));
}

__attribute__((sysv_abi)) size_t portage_mbrlen(const char *s, size_t n, void *ps)
{
    return portage_mbrtowc(nullptr, s, n, ps);
}

__attribute__((sysv_abi)) size_t portage_mbsrtowcs(portage::ndk::BionicWchar *dest,
                                                   const char **src, size_t len, void *ps)
{
    return portage_mbsnrtowcs(dest, src, static_cast<size_t>(-1), len, ps);
}

__attribute__((sysv_abi)) int portage_mbtowc(portage::ndk::BionicWchar *pwc, const char *s,
                                             size_t n)
{
    if (!s) {
        return 0;
    }
    size_t r = portage_mbrtowc(pwc, s, n, nullptr);
    if (r == static_cast<size_t>(-1) || r == static_cast<size_t>(-2)) {
        return -1;
    }
    return static_cast<int>(r);
}

__attribute__((sysv_abi)) int portage_swprintf(portage::ndk::BionicWchar *s, size_t n,
                                               const portage::ndk::BionicWchar *format,
                                               uintptr_t a0, uintptr_t a1, uintptr_t a2,
                                               uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    if (!s || n == 0) {
        return -1;
    }
    if (!format) {
        s[0] = 0;
        return 0;
    }
    size_t flen = portage_wcslen(format);
    size_t cp = (flen < n - 1) ? flen : (n - 1);
    for (size_t i = 0; i < cp; ++i) {
        s[i] = format[i];
    }
    s[cp] = 0;
    return static_cast<int>(cp);
}

__attribute__((sysv_abi)) portage::ndk::BionicWchar portage_fputwc(portage::ndk::BionicWchar c,
                                                                   void *stream)
{
    char mb[8];
    size_t len = portage_wcrtomb(mb, c, nullptr);
    if (len > 0) {
        portage_fwrite(mb, 1, len, reinterpret_cast<portage::ndk::BionicFile *>(stream));
    }
    return c;
}

__attribute__((sysv_abi)) int32_t portage_getwc(void *stream)
{
    int ch = portage_fgetc(reinterpret_cast<portage::ndk::BionicFile *>(stream));
    return (ch == -1) ? -1 : static_cast<int32_t>(ch);
}

__attribute__((sysv_abi)) int32_t portage_ungetwc(portage::ndk::BionicWchar c, void *stream)
{
    return portage_ungetc(static_cast<int>(c & 0xFF),
                          reinterpret_cast<portage::ndk::BionicFile *>(stream));
}

} // extern "C"
