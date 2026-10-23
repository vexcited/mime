/*
 * the _l and checked multibyte string functions, over the plain ones in string.c. the locale argument is ignored: the
 * code page is the one set by _setmbcp. the Japanese character tests only hold for code page 932, in other pages no
 * byte is a kana and no double byte character is a hiragana.
 */
#include "msvcrt_private.h"

#define MB_UC unsigned char
#define MB_CODEPAGE_JAPANESE 932

unsigned char *__cdecl _mbsdec(const unsigned char *start, const unsigned char *cur);
unsigned char *__cdecl _mbsinc(const unsigned char *s);
unsigned int __cdecl _mbsnextc(const unsigned char *s);
size_t __cdecl _mbslen(const unsigned char *s);
size_t __cdecl _mbstrlen(const char *s);
size_t __cdecl _mbclen(const unsigned char *s);
size_t __cdecl _mbsnbcnt(const unsigned char *s, size_t n);
size_t __cdecl _mbsnccnt(const unsigned char *s, size_t n);
unsigned char *__cdecl _mbschr(const unsigned char *s, unsigned int c);
unsigned char *__cdecl _mbsrchr(const unsigned char *s, unsigned int c);
int __cdecl _mbscmp(const unsigned char *a, const unsigned char *b);
int __cdecl _mbsicmp(const unsigned char *a, const unsigned char *b);
int __cdecl _mbsncmp(const unsigned char *a, const unsigned char *b, size_t n);
int __cdecl _mbsnicmp(const unsigned char *a, const unsigned char *b, size_t n);
int __cdecl _mbsnbcmp(const unsigned char *a, const unsigned char *b, size_t n);
int __cdecl _mbsnbicmp(const unsigned char *a, const unsigned char *b, size_t n);
size_t __cdecl _mbsspn(const unsigned char *s, const unsigned char *set);
size_t __cdecl _mbscspn(const unsigned char *s, const unsigned char *set);
unsigned char *__cdecl _mbsspnp(const unsigned char *s, const unsigned char *set);
unsigned char *__cdecl _mbspbrk(const unsigned char *s, const unsigned char *set);
unsigned char *__cdecl _mbsstr(const unsigned char *hay, const unsigned char *needle);
unsigned char *__cdecl _mbstok(unsigned char *s, const unsigned char *delim);
unsigned char *__cdecl _mbslwr(unsigned char *s);
unsigned char *__cdecl _mbsupr(unsigned char *s);
unsigned char *__cdecl _mbsrev(unsigned char *s);
unsigned char *__cdecl _mbsncpy(unsigned char *dst, const unsigned char *src, size_t n);
unsigned char *__cdecl _mbsnbcpy(unsigned char *dst, const unsigned char *src, size_t n);
unsigned char *__cdecl _mbsncat(unsigned char *dst, const unsigned char *src, size_t n);
unsigned char *__cdecl _mbsnbcat(unsigned char *dst, const unsigned char *src, size_t n);
int __cdecl _mbbtype(unsigned char c, int type);
int __cdecl _ismbblead(unsigned int c);
int __cdecl _ismbbtrail(unsigned int c);
int __cdecl _ismbcdigit(unsigned int c);
int __cdecl _ismbcspace(unsigned int c);
int __cdecl _ismbcalpha(unsigned int c);
int __cdecl _ismbcupper(unsigned int c);
int __cdecl _ismbclower(unsigned int c);
unsigned int __cdecl _mbctolower(unsigned int c);
unsigned int __cdecl _mbctoupper(unsigned int c);
void __cdecl _mbccpy(unsigned char *dst, const unsigned char *src);
int __cdecl _getmbcp(void);
size_t __cdecl mbstowcs(wchar_t *dst, const char *src, size_t n);

/* ------------------------------------------------------------------ locale forms */

#define L1(ret, name, base, t1, a1) \
    ret __cdecl name(t1 a1, void *locale) { return base(a1); }
#define L2(ret, name, base, t1, a1, t2, a2) \
    ret __cdecl name(t1 a1, t2 a2, void *locale) { return base(a1, a2); }
#define L3(ret, name, base, t1, a1, t2, a2, t3, a3) \
    ret __cdecl name(t1 a1, t2 a2, t3 a3, void *locale) { return base(a1, a2, a3); }

L2(unsigned char *, _mbsdec_l, _mbsdec, const MB_UC *, start, const MB_UC *, cur)
L1(unsigned char *, _mbsinc_l, _mbsinc, const MB_UC *, s)
L1(unsigned int, _mbsnextc_l, _mbsnextc, const MB_UC *, s)
L1(size_t, _mbslen_l, _mbslen, const MB_UC *, s)
L1(size_t, _mbclen_l, _mbclen, const MB_UC *, s)
L2(size_t, _mbsnbcnt_l, _mbsnbcnt, const MB_UC *, s, size_t, n)
L2(size_t, _mbsnccnt_l, _mbsnccnt, const MB_UC *, s, size_t, n)
L2(unsigned char *, _mbschr_l, _mbschr, const MB_UC *, s, unsigned int, c)
L2(unsigned char *, _mbsrchr_l, _mbsrchr, const MB_UC *, s, unsigned int, c)
L2(int, _mbscmp_l, _mbscmp, const MB_UC *, a, const MB_UC *, b)
L2(int, _mbsicmp_l, _mbsicmp, const MB_UC *, a, const MB_UC *, b)
L3(int, _mbsncmp_l, _mbsncmp, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnicmp_l, _mbsnicmp, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnbcmp_l, _mbsnbcmp, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnbicmp_l, _mbsnbicmp, const MB_UC *, a, const MB_UC *, b, size_t, n)
L2(size_t, _mbsspn_l, _mbsspn, const MB_UC *, s, const MB_UC *, set)
L2(size_t, _mbscspn_l, _mbscspn, const MB_UC *, s, const MB_UC *, set)
L2(unsigned char *, _mbsspnp_l, _mbsspnp, const MB_UC *, s, const MB_UC *, set)
L2(unsigned char *, _mbspbrk_l, _mbspbrk, const MB_UC *, s, const MB_UC *, set)
L2(unsigned char *, _mbsstr_l, _mbsstr, const MB_UC *, hay, const MB_UC *, needle)
L2(unsigned char *, _mbstok_l, _mbstok, MB_UC *, s, const MB_UC *, delim)
L1(unsigned char *, _mbslwr_l, _mbslwr, MB_UC *, s)
L1(unsigned char *, _mbsupr_l, _mbsupr, MB_UC *, s)
L1(unsigned char *, _mbsrev_l, _mbsrev, MB_UC *, s)
L3(unsigned char *, _mbsncpy_l, _mbsncpy, MB_UC *, dst, const MB_UC *, src, size_t, n)
L3(unsigned char *, _mbsnbcpy_l, _mbsnbcpy, MB_UC *, dst, const MB_UC *, src, size_t, n)
L3(unsigned char *, _mbsncat_l, _mbsncat, MB_UC *, dst, const MB_UC *, src, size_t, n)
L3(unsigned char *, _mbsnbcat_l, _mbsnbcat, MB_UC *, dst, const MB_UC *, src, size_t, n)
L2(int, _mbbtype_l, _mbbtype, unsigned char, c, int, type)
L1(unsigned int, _mbctolower_l, _mbctolower, unsigned int, c)
L1(unsigned int, _mbctoupper_l, _mbctoupper, unsigned int, c)
L1(int, _ismbcdigit_l, _ismbcdigit, unsigned int, c)
L1(int, _ismbcspace_l, _ismbcspace, unsigned int, c)
L1(int, _ismbcalpha_l, _ismbcalpha, unsigned int, c)
L1(int, _ismbcupper_l, _ismbcupper, unsigned int, c)
L1(int, _ismbclower_l, _ismbclower, unsigned int, c)
L1(int, _ismbblead_l, _ismbblead, unsigned int, c)
L1(int, _ismbbtrail_l, _ismbbtrail, unsigned int, c)

/* multibyte collation compares characters as they are, like the c locale. */
int __cdecl _mbscoll(const unsigned char *a, const unsigned char *b)
{
    return _mbscmp(a, b);
}

int __cdecl _mbsicoll(const unsigned char *a, const unsigned char *b)
{
    return _mbsicmp(a, b);
}

int __cdecl _mbsncoll(const unsigned char *a, const unsigned char *b, size_t n)
{
    return _mbsncmp(a, b, n);
}

int __cdecl _mbsnicoll(const unsigned char *a, const unsigned char *b, size_t n)
{
    return _mbsnicmp(a, b, n);
}

int __cdecl _mbsnbcoll(const unsigned char *a, const unsigned char *b, size_t n)
{
    return _mbsnbcmp(a, b, n);
}

int __cdecl _mbsnbicoll(const unsigned char *a, const unsigned char *b, size_t n)
{
    return _mbsnbicmp(a, b, n);
}

L2(int, _mbscoll_l, _mbscoll, const MB_UC *, a, const MB_UC *, b)
L2(int, _mbsicoll_l, _mbsicoll, const MB_UC *, a, const MB_UC *, b)
L3(int, _mbsncoll_l, _mbsncoll, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnicoll_l, _mbsnicoll, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnbcoll_l, _mbsnbcoll, const MB_UC *, a, const MB_UC *, b, size_t, n)
L3(int, _mbsnbicoll_l, _mbsnbicoll, const MB_UC *, a, const MB_UC *, b, size_t, n)

unsigned char *__cdecl _mbsninc(const unsigned char *s, size_t n)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    while (n-- && *s)
        s = _mbsinc(s);
    return (unsigned char *)s;
}

L2(unsigned char *, _mbsninc_l, _mbsninc, const MB_UC *, s, size_t, n)

size_t __cdecl _mbsnlen(const unsigned char *s, size_t max)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    size_t n = 0;
    const unsigned char *p = s;
    while ((size_t)(p - s) < max && *p) {
        p = _mbsinc(p);
        n++;
    }
    return n;
}

L2(size_t, _mbsnlen_l, _mbsnlen, const MB_UC *, s, size_t, max)

/* character count of a string of at most max bytes, or -1 when it ends in the middle of a character. */
size_t __cdecl _mbstrnlen(const char *s, size_t max)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    size_t n = 0, i = 0;
    while (i < max && s[i]) {
        if (MsvcrtMbLead((unsigned char)s[i])) {
            if (i + 1 >= max || !s[i + 1]) {
                MsvcrtSetErrno(EILSEQ);
                return (size_t)-1;
            }
            i += 2;
        } else {
            i++;
        }
        n++;
    }
    return n;
}

L2(size_t, _mbstrnlen_l, _mbstrnlen, const char *, s, size_t, max)
L1(size_t, _mbstrlen_l, _mbstrlen, const char *, s)

/* ------------------------------------------------------------------ checked forms */

/*
 * the checks of the _s copies: a destination and its size, a source. on error the destination becomes the empty
 * string, as the standard asks, with EINVAL for a bad argument and ERANGE for no room. a copy of count bytes stops at
 * the end of the source and never splits a character.
 */
static errno_t mb_copy_s(unsigned char *dst, size_t size, const unsigned char *src, size_t count, int bytes, int append,
                         int plain)
{
    if (!dst || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    unsigned char *at = dst;
    size_t room = size;
    if (append) {
        while (room && *at) {
            at++;
            room--;
        }
        if (!room) {
            *dst = 0;
            MSVCRT_INVALID(EINVAL);
            return EINVAL;
        }
    }
    if (!src) {
        *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t length = 0;
    int truncate = !plain && count == (size_t)-1;
    if (plain) {
        length = strlen((const char *)src);
    } else if (bytes) {
        while (length < count && src[length]) {
            size_t step = MsvcrtMbLead(src[length]) && src[length + 1] ? 2 : 1;
            if (length + step > count)
                break;
            length += step;
        }
    } else {
        length = _mbsnbcnt(src, count);
    }
    if (length >= room) {
        if (truncate) {
            /* as much as fits, without splitting a character. */
            size_t keep = room - 1;
            if (keep && MsvcrtMbLead(src[keep - 1]) && _mbsnbcnt(src, keep) < keep)
                keep--;
            while (keep && _mbsnbcnt(src, _mbsnccnt(src, keep)) > keep)
                keep--;
            memcpy(at, src, keep);
            at[keep] = 0;
            return 80;
        }
        *dst = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(at, src, length);
    at[length] = 0;
    return 0;
}

errno_t __cdecl _mbscpy_s(unsigned char *dst, size_t size, const unsigned char *src)
{
    return mb_copy_s(dst, size, src, 0, 0, 0, 1);
}

errno_t __cdecl _mbscat_s(unsigned char *dst, size_t size, const unsigned char *src)
{
    return mb_copy_s(dst, size, src, 0, 0, 1, 1);
}

errno_t __cdecl _mbsncpy_s(unsigned char *dst, size_t size, const unsigned char *src, size_t count)
{
    return mb_copy_s(dst, size, src, count, 0, 0, 0);
}

errno_t __cdecl _mbsncat_s(unsigned char *dst, size_t size, const unsigned char *src, size_t count)
{
    return mb_copy_s(dst, size, src, count, 0, 1, 0);
}

errno_t __cdecl _mbsnbcpy_s(unsigned char *dst, size_t size, const unsigned char *src, size_t count)
{
    return mb_copy_s(dst, size, src, count, 1, 0, 0);
}

errno_t __cdecl _mbsnbcat_s(unsigned char *dst, size_t size, const unsigned char *src, size_t count)
{
    return mb_copy_s(dst, size, src, count, 1, 1, 0);
}

#define S2(name, base, t2, a2) \
    errno_t __cdecl name(unsigned char *dst, size_t size, const unsigned char *src, void *locale) { return base(dst, size, src); }
S2(_mbscpy_s_l, _mbscpy_s, 0, 0)
S2(_mbscat_s_l, _mbscat_s, 0, 0)
#undef S2

errno_t __cdecl _mbsncpy_s_l(unsigned char *dst, size_t size, const unsigned char *src, size_t count, void *locale)
{
    return _mbsncpy_s(dst, size, src, count);
}

errno_t __cdecl _mbsncat_s_l(unsigned char *dst, size_t size, const unsigned char *src, size_t count, void *locale)
{
    return _mbsncat_s(dst, size, src, count);
}

errno_t __cdecl _mbsnbcpy_s_l(unsigned char *dst, size_t size, const unsigned char *src, size_t count, void *locale)
{
    return _mbsnbcpy_s(dst, size, src, count);
}

errno_t __cdecl _mbsnbcat_s_l(unsigned char *dst, size_t size, const unsigned char *src, size_t count, void *locale)
{
    return _mbsnbcat_s(dst, size, src, count);
}

/* copies the character at src as one or two bytes; copied says how many. */
errno_t __cdecl _mbccpy_s(unsigned char *dst, size_t size, int *copied, const unsigned char *src)
{
    if (copied)
        *copied = 0;
    if (!dst || !size || !src) {
        if (dst && size)
            *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = MsvcrtMbLead(src[0]) ? 2 : 1;
    if (n == 2 && !src[1]) {
        *dst = 0;
        MsvcrtSetErrno(EILSEQ);
        return EILSEQ;
    }
    if (size < n) {
        *dst = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(dst, src, n);
    if (copied)
        *copied = (int)n;
    return 0;
}

errno_t __cdecl _mbccpy_s_l(unsigned char *dst, size_t size, int *copied, const unsigned char *src, void *locale)
{
    return _mbccpy_s(dst, size, copied, src);
}

void __cdecl _mbccpy_l(unsigned char *dst, const unsigned char *src, void *locale)
{
    _mbccpy(dst, src);
}

errno_t __cdecl _mbslwr_s(unsigned char *s, size_t size)
{
    if (!s) {
        if (size) {
            MSVCRT_INVALID(EINVAL);
            return EINVAL;
        }
        return 0;
    }
    size_t n = 0;
    while (n < size && s[n])
        n++;
    if (n == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    _mbslwr(s);
    return 0;
}

errno_t __cdecl _mbsupr_s(unsigned char *s, size_t size)
{
    if (!s) {
        if (size) {
            MSVCRT_INVALID(EINVAL);
            return EINVAL;
        }
        return 0;
    }
    size_t n = 0;
    while (n < size && s[n])
        n++;
    if (n == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    _mbsupr(s);
    return 0;
}

errno_t __cdecl _mbslwr_s_l(unsigned char *s, size_t size, void *locale)
{
    return _mbslwr_s(s, size);
}

errno_t __cdecl _mbsupr_s_l(unsigned char *s, size_t size, void *locale)
{
    return _mbsupr_s(s, size);
}

/* fills a string with c, to the end or for count characters. */
unsigned char *__cdecl _mbsnset(unsigned char *s, unsigned int c, size_t count)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    unsigned char *p = s;
    size_t width = c > 0xff ? 2 : 1;
    if (width == 2 && !MsvcrtMbLead(c >> 8)) {
        MSVCRT_INVALID(EINVAL);
        return s;
    }
    while (count-- && *p) {
        size_t old = MsvcrtMbLead(*p) && p[1] ? 2 : 1;
        if (width == 2) {
            if (old == 1 && !p[1])
                break;
            if (old == 1) {
                p[0] = (unsigned char)(c >> 8);
                p[1] = (unsigned char)c;
                p += 2;
                continue;
            }
            p[0] = (unsigned char)(c >> 8);
            p[1] = (unsigned char)c;
            p += 2;
        } else {
            if (old == 2)
                p[1] = ' ';
            p[0] = (unsigned char)c;
            p += old;
        }
    }
    return s;
}

unsigned char *__cdecl _mbsset(unsigned char *s, unsigned int c)
{
    return _mbsnset(s, c, (size_t)-1);
}

/* the same counted in bytes, never leaving half a character. */
unsigned char *__cdecl _mbsnbset(unsigned char *s, unsigned int c, size_t count)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    size_t width = c > 0xff ? 2 : 1;
    unsigned char *p = s;
    if (width == 2 && !MsvcrtMbLead(c >> 8)) {
        MSVCRT_INVALID(EINVAL);
        return s;
    }
    while (count && *p) {
        size_t old = MsvcrtMbLead(*p) && p[1] ? 2 : 1;
        if (width == 2) {
            if (count < 2) {
                *p = ' ';
                break;
            }
            p[0] = (unsigned char)(c >> 8);
            p[1] = (unsigned char)c;
            p += 2;
            count -= 2;
        } else {
            if (old == 2) {
                p[1] = ' ';
            }
            p[0] = (unsigned char)c;
            p += old;
            count -= count < old ? count : old;
        }
    }
    return s;
}

static errno_t mb_set_s(unsigned char *s, size_t size, unsigned int c, size_t count, int bytes)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t n = 0;
    while (n < size && s[n])
        n++;
    if (n == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (c > 0xff && !MsvcrtMbLead(c >> 8)) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (bytes)
        _mbsnbset(s, c, count);
    else
        _mbsnset(s, c, count);
    return 0;
}

errno_t __cdecl _mbsset_s(unsigned char *s, size_t size, unsigned int c)
{
    return mb_set_s(s, size, c, (size_t)-1, 0);
}

errno_t __cdecl _mbsnset_s(unsigned char *s, size_t size, unsigned int c, size_t count)
{
    return mb_set_s(s, size, c, count, 0);
}

errno_t __cdecl _mbsnbset_s(unsigned char *s, size_t size, unsigned int c, size_t count)
{
    return mb_set_s(s, size, c, count, 1);
}

unsigned char *__cdecl _mbsset_l(unsigned char *s, unsigned int c, void *locale)
{
    return _mbsset(s, c);
}

unsigned char *__cdecl _mbsnset_l(unsigned char *s, unsigned int c, size_t count, void *locale)
{
    return _mbsnset(s, c, count);
}

unsigned char *__cdecl _mbsnbset_l(unsigned char *s, unsigned int c, size_t count, void *locale)
{
    return _mbsnbset(s, c, count);
}

errno_t __cdecl _mbsset_s_l(unsigned char *s, size_t size, unsigned int c, void *locale)
{
    return _mbsset_s(s, size, c);
}

errno_t __cdecl _mbsnset_s_l(unsigned char *s, size_t size, unsigned int c, size_t count, void *locale)
{
    return _mbsnset_s(s, size, c, count);
}

errno_t __cdecl _mbsnbset_s_l(unsigned char *s, size_t size, unsigned int c, size_t count, void *locale)
{
    return _mbsnbset_s(s, size, c, count);
}

unsigned char *__cdecl _mbstok_s(unsigned char *s, const unsigned char *delim, unsigned char **context)
{
    if (!delim || !context || (!s && !*context)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!s)
        s = *context;
    while (*s && _mbschr(delim, _mbsnextc(s)))
        s = _mbsinc(s);
    if (!*s) {
        *context = s;
        return nullptr;
    }
    unsigned char *token = s;
    while (*s && !_mbschr(delim, _mbsnextc(s)))
        s = _mbsinc(s);
    if (*s) {
        unsigned char *next = _mbsinc(s);
        *s = 0;
        if (next > s + 1)
            s[1] = 0;
        s = next;
    }
    *context = s;
    return token;
}

unsigned char *__cdecl _mbstok_s_l(unsigned char *s, const unsigned char *delim, unsigned char **context, void *locale)
{
    return _mbstok_s(s, delim, context);
}

errno_t __cdecl _mbstowcs_s_l(size_t *converted, wchar_t *dst, size_t size, const char *src, size_t max, void *locale)
{
    extern errno_t __cdecl mbstowcs_s(size_t *, wchar_t *, size_t, const char *, size_t);
    return mbstowcs_s(converted, dst, size, src, max);
}

/* ------------------------------------------------------------------ single bytes */

static int japanese(void)
{
    return _getmbcp() == MB_CODEPAGE_JAPANESE;
}

static int is_kana(unsigned c)
{
    return japanese() && c >= 0xa1 && c <= 0xdf;
}

static int is_kana_punct(unsigned c)
{
    return is_kana(c) && (c <= 0xa5 || c == 0xb0 || c >= 0xde);
}

int __cdecl _ismbbkana(unsigned int c)
{
    return is_kana(c & 0xff) && c < 0x100 ? 1 : 0;
}

int __cdecl _ismbbkalnum(unsigned int c)
{
    return c < 0x100 && is_kana(c) && !is_kana_punct(c);
}

int __cdecl _ismbbkpunct(unsigned int c)
{
    return c < 0x100 && is_kana_punct(c);
}

int __cdecl _ismbbkprint(unsigned int c)
{
    return c < 0x100 && is_kana(c);
}

int __cdecl _ismbbalnum(unsigned int c)
{
    return c < 0x100 && ((MSVCRT_CTYPE(c) & (_UPPER | _LOWER | _DIGIT)) || _ismbbkalnum(c));
}

int __cdecl _ismbbalpha(unsigned int c)
{
    return c < 0x100 && ((MSVCRT_CTYPE(c) & (_UPPER | _LOWER)) || _ismbbkalnum(c));
}

int __cdecl _ismbbblank(unsigned int c)
{
    return c == ' ' || c == '\t';
}

int __cdecl _ismbbgraph(unsigned int c)
{
    return c < 0x100 && ((MSVCRT_CTYPE(c) & (_UPPER | _LOWER | _DIGIT | _PUNCT)) || is_kana(c));
}

int __cdecl _ismbbprint(unsigned int c)
{
    return c < 0x100 && ((MSVCRT_CTYPE(c) & (_UPPER | _LOWER | _DIGIT | _PUNCT | _BLANK)) || is_kana(c));
}

int __cdecl _ismbbpunct(unsigned int c)
{
    return c < 0x100 && ((MSVCRT_CTYPE(c) & _PUNCT) || _ismbbkpunct(c));
}

L1(int, _ismbbkana_l, _ismbbkana, unsigned int, c)
L1(int, _ismbbkalnum_l, _ismbbkalnum, unsigned int, c)
L1(int, _ismbbkpunct_l, _ismbbkpunct, unsigned int, c)
L1(int, _ismbbkprint_l, _ismbbkprint, unsigned int, c)
L1(int, _ismbbalnum_l, _ismbbalnum, unsigned int, c)
L1(int, _ismbbalpha_l, _ismbbalpha, unsigned int, c)
L1(int, _ismbbblank_l, _ismbbblank, unsigned int, c)
L1(int, _ismbbgraph_l, _ismbbgraph, unsigned int, c)
L1(int, _ismbbprint_l, _ismbbprint, unsigned int, c)
L1(int, _ismbbpunct_l, _ismbbpunct, unsigned int, c)

/* the type of byte s[n]: 0 single, 1 lead, 2 trail, -1 for a lead without a trail or a position past the end. */
int __cdecl _mbsbtype(const unsigned char *s, size_t n)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    size_t i = 0;
    while (i < n) {
        if (!s[i])
            return -1;
        i += MsvcrtMbLead(s[i]) && s[i + 1] ? 2 : 1;
    }
    if (i > n)
        return 2;
    if (!s[n])
        return 0;
    if (MsvcrtMbLead(s[n]))
        return s[n + 1] ? 1 : -1;
    return 0;
}

L2(int, _mbsbtype_l, _mbsbtype, const MB_UC *, s, size_t, n)

/* ------------------------------------------------------------------ double byte characters */

static int is_legal_cp932(unsigned c)
{
    unsigned hi = c >> 8, lo = c & 0xff;
    return ((hi >= 0x81 && hi <= 0x9f) || (hi >= 0xe0 && hi <= 0xfc)) && ((lo >= 0x40 && lo <= 0x7e) || (lo >= 0x80 && lo <= 0xfc));
}

int __cdecl _ismbclegal(unsigned int c)
{
    if (c < 0x100)
        return 0;
    return japanese() ? is_legal_cp932(c) : MsvcrtMbLead(c >> 8) && _ismbbtrail(c & 0xff);
}

int __cdecl _ismbchira(unsigned int c)
{
    return japanese() && c >= 0x829f && c <= 0x82f1;
}

int __cdecl _ismbckata(unsigned int c)
{
    if (c < 0x100)
        return _ismbbkana(c);
    return japanese() && c >= 0x8340 && c <= 0x8396 && c != 0x837f;
}

int __cdecl _ismbcsymbol(unsigned int c)
{
    return japanese() && c >= 0x8141 && c <= 0x81ac && c != 0x817f;
}

int __cdecl _ismbcl0(unsigned int c)
{
    return japanese() && c >= 0x8140 && c <= 0x889e && !_ismbchira(c) && !_ismbckata(c);
}

int __cdecl _ismbcl1(unsigned int c)
{
    return japanese() && c >= 0x889f && c <= 0x9872;
}

int __cdecl _ismbcl2(unsigned int c)
{
    return japanese() && c >= 0x989f && c <= 0xeaa4;
}

int __cdecl _ismbcalnum(unsigned int c)
{
    if (c < 0x100)
        return _ismbbalnum(c);
    return japanese() && (_ismbchira(c) || _ismbckata(c) || (c >= 0x824f && c <= 0x8258) || (c >= 0x8260 && c <= 0x8279) ||
                          (c >= 0x8281 && c <= 0x829a) || _ismbcl1(c) || _ismbcl2(c));
}

int __cdecl _ismbcgraph(unsigned int c)
{
    if (c < 0x100)
        return _ismbbgraph(c);
    return _ismbclegal(c);
}

int __cdecl _ismbcprint(unsigned int c)
{
    if (c < 0x100)
        return _ismbbprint(c);
    return _ismbclegal(c);
}

int __cdecl _ismbcpunct(unsigned int c)
{
    if (c < 0x100)
        return _ismbbpunct(c);
    return _ismbclegal(c) && !_ismbcalnum(c);
}

int __cdecl _ismbcblank(unsigned int c)
{
    return c < 0x100 ? _ismbbblank(c) : c == 0x8140;
}

L1(int, _ismbclegal_l, _ismbclegal, unsigned int, c)
L1(int, _ismbchira_l, _ismbchira, unsigned int, c)
L1(int, _ismbckata_l, _ismbckata, unsigned int, c)
L1(int, _ismbcsymbol_l, _ismbcsymbol, unsigned int, c)
L1(int, _ismbcl0_l, _ismbcl0, unsigned int, c)
L1(int, _ismbcl1_l, _ismbcl1, unsigned int, c)
L1(int, _ismbcl2_l, _ismbcl2, unsigned int, c)
L1(int, _ismbcalnum_l, _ismbcalnum, unsigned int, c)
L1(int, _ismbcgraph_l, _ismbcgraph, unsigned int, c)
L1(int, _ismbcprint_l, _ismbcprint, unsigned int, c)
L1(int, _ismbcpunct_l, _ismbcpunct, unsigned int, c)
L1(int, _ismbcblank_l, _ismbcblank, unsigned int, c)

/* lead and trail tests for a byte inside a string; a lead must start a complete double byte character. */
int __cdecl _ismbslead(const unsigned char *start, const unsigned char *cur)
{
    if (!start || !cur) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const unsigned char *p = start;
    while (p < cur && *p)
        p = _mbsinc(p);
    return p == cur && MsvcrtMbLead(*cur) && cur[1] ? 1 : 0;
}

int __cdecl _ismbstrail(const unsigned char *start, const unsigned char *cur)
{
    if (!start || !cur) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    const unsigned char *p = start;
    while (p < cur && *p) {
        const unsigned char *next = _mbsinc(p);
        if (next > cur)
            return p != cur && next == p + 2 ? 1 : 0;
        p = next;
    }
    return 0;
}

L2(int, _ismbslead_l, _ismbslead, const MB_UC *, start, const MB_UC *, cur)
L2(int, _ismbstrail_l, _ismbstrail, const MB_UC *, start, const MB_UC *, cur)

/* single byte characters to their full width double byte forms and back, for code page 932. */
unsigned int __cdecl _mbbtombc(unsigned int c)
{
    if (!japanese())
        return c;
    if (c >= 0x20 && c <= 0x7e) {
        static const unsigned short ascii[] = {
            0x8140, 0x8149, 0x8168, 0x8194, 0x8190, 0x8193, 0x8195, 0x8166, 0x8169, 0x816a, 0x8196, 0x817b, 0x8143,
            0x817c, 0x8144, 0x815e, 0x824f, 0x8250, 0x8251, 0x8252, 0x8253, 0x8254, 0x8255, 0x8256, 0x8257, 0x8258,
            0x8146, 0x8147, 0x8183, 0x8181, 0x8184, 0x8148, 0x8197, 0x8260, 0x8261, 0x8262, 0x8263, 0x8264, 0x8265,
            0x8266, 0x8267, 0x8268, 0x8269, 0x826a, 0x826b, 0x826c, 0x826d, 0x826e, 0x826f, 0x8270, 0x8271, 0x8272,
            0x8273, 0x8274, 0x8275, 0x8276, 0x8277, 0x8278, 0x8279, 0x816d, 0x818f, 0x816e, 0x814f, 0x8151, 0x8165,
            0x8281, 0x8282, 0x8283, 0x8284, 0x8285, 0x8286, 0x8287, 0x8288, 0x8289, 0x828a, 0x828b, 0x828c, 0x828d,
            0x828e, 0x828f, 0x8290, 0x8291, 0x8292, 0x8293, 0x8294, 0x8295, 0x8296, 0x8297, 0x8298, 0x8299, 0x829a,
            0x816f, 0x8162, 0x8170, 0x8150};
        return ascii[c - 0x20];
    }
    return c;
}

unsigned int __cdecl _mbctombb(unsigned int c)
{
    if (!japanese() || c < 0x100)
        return c;
    for (unsigned b = 0x20; b <= 0x7e; b++)
        if (_mbbtombc(b) == c)
            return b;
    return c;
}

L1(unsigned int, _mbbtombc_l, _mbbtombc, unsigned int, c)
L1(unsigned int, _mbctombb_l, _mbctombb, unsigned int, c)

/* hiragana and katakana of the same sound are 0x60 apart in code page 932, from the small a to the small ke. */
unsigned int __cdecl _mbctohira(unsigned int c)
{
    if (c >= 0x8340 && c <= 0x837e && japanese())
        return c - 0xa1;
    if (c >= 0x8380 && c <= 0x8393 && japanese())
        return c - 0xa2;
    return c;
}

unsigned int __cdecl _mbctokata(unsigned int c)
{
    if (c >= 0x829f && c <= 0x82dd && japanese())
        return c + 0xa1;
    if (c >= 0x82de && c <= 0x82f1 && japanese())
        return c + 0xa2;
    return c;
}

L1(unsigned int, _mbctohira_l, _mbctohira, unsigned int, c)
L1(unsigned int, _mbctokata_l, _mbctokata, unsigned int, c)

/* shift jis to jis x 0208 and back: two rows of 94 characters fold into one. */
unsigned int __cdecl _mbcjmstojis(unsigned int c)
{
    if (!japanese() || !is_legal_cp932(c))
        return 0;
    unsigned lead = c >> 8, trail = c & 0xff;
    lead -= lead <= 0x9f ? 0x71 : 0xb1;
    lead = lead * 2 + 1;
    if (trail > 0x7f)
        trail--;
    if (trail >= 0x9e) {
        trail -= 0x7d;
        lead++;
    } else {
        trail -= 0x1f;
    }
    return lead << 8 | trail;
}

unsigned int __cdecl _mbcjistojms(unsigned int c)
{
    unsigned hi = c >> 8, lo = c & 0xff;
    if (!japanese() || hi < 0x21 || hi > 0x7e || lo < 0x21 || lo > 0x7e)
        return 0;
    unsigned lead = ((hi - 0x21) >> 1) + 0x81;
    if (lead > 0x9f)
        lead += 0x40;
    unsigned trail;
    if (hi & 1) {
        trail = lo + 0x1f;
        if (trail >= 0x7f)
            trail++;
    } else {
        trail = lo + 0x7e;
    }
    return lead << 8 | trail;
}

L1(unsigned int, _mbcjmstojis_l, _mbcjmstojis, unsigned int, c)
L1(unsigned int, _mbcjistojms_l, _mbcjistojms, unsigned int, c)

/* ------------------------------------------------------------------ tables */

extern unsigned char _mbctype[257];
extern unsigned char _mbcasemap[256];

/* the tables are built for the code page on first use. */
unsigned char *__cdecl __p__mbctype(void)
{
    _getmbcp();
    return _mbctype;
}

unsigned char *__cdecl __p__mbcasemap(void)
{
    _getmbcp();
    return _mbcasemap;
}
