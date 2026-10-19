/*
 * universal crt additions over the rest of msvcrt: the utf-8 conversions of c11, the restartable conversions with the
 * checks, wide character transformations, locale objects of the c locale, the locale forms of collation and case
 * conversion, the time base functions, aligned reallocation and the heap walk.
 */
#include "msvcrt_private.h"

typedef unsigned short char16_t;
typedef unsigned int char32_t;

size_t __cdecl wcstombs(char *dst, const wchar_t *src, size_t n);
errno_t __cdecl wctomb_s(int *len, char *dst, size_t size, wchar_t c);
errno_t __cdecl wcstombs_s(size_t *converted, char *dst, size_t size, const wchar_t *src, size_t count);
errno_t __cdecl mbstowcs_s(size_t *converted, wchar_t *dst, size_t size, const char *src, size_t count);
int __cdecl wctomb(char *dst, wchar_t c);
int __cdecl _stricoll(const char *a, const char *b);
int __cdecl _strncoll(const char *a, const char *b, size_t n);
int __cdecl _strnicoll(const char *a, const char *b, size_t n);
int __cdecl _wcsicoll(const wchar_t *a, const wchar_t *b);
int __cdecl _wcsncoll(const wchar_t *a, const wchar_t *b, size_t n);
int __cdecl _wcsnicoll(const wchar_t *a, const wchar_t *b, size_t n);
char *__cdecl _strlwr(char *s);
char *__cdecl _strupr(char *s);
errno_t __cdecl _strlwr_s(char *s, size_t size);
errno_t __cdecl _strupr_s(char *s, size_t size);
wchar_t *__cdecl _wcslwr(wchar_t *s);
wchar_t *__cdecl _wcsupr(wchar_t *s);
errno_t __cdecl _wcslwr_s(wchar_t *s, size_t size);
errno_t __cdecl _wcsupr_s(wchar_t *s, size_t size);
int __cdecl isblank(int c);
int __cdecl iswblank(wint_t c);
int __cdecl __iswcsym(wint_t c);
int __cdecl __iswcsymf(wint_t c);
wint_t __cdecl towlower(wint_t c);
wint_t __cdecl towupper(wint_t c);
void *__cdecl _aligned_offset_malloc(size_t size, size_t alignment, size_t offset);
void __cdecl _aligned_free(void *p);
void *__cdecl _aligned_offset_realloc(void *p, size_t size, size_t alignment, size_t offset);
size_t __cdecl _msize(void *p);

/* the state of the utf-8 conversions: bytes of a character that is not complete, or the second unit of a pair. */
typedef struct {
    unsigned long Wchar;
    unsigned short Byte, State;
} MBSTATE;

/* ------------------------------------------------------------------ utf-8 */

#define STATE_BYTES 1
#define STATE_PAIR 2

/*
 * one character of utf-8 at s: the code point and the bytes used, with the partial bytes kept in the state. The result
 * is the bytes used from s, (size_t)-2 when s ends in the middle of a character and (size_t)-1 for a malformed one.
 */
static size_t decode_utf8(unsigned *point, const char *s, size_t n, MBSTATE *state)
{
    unsigned have = state->State == STATE_BYTES ? state->Byte >> 8 : 0;
    unsigned long partial = state->State == STATE_BYTES ? state->Wchar : 0;
    unsigned need = state->State == STATE_BYTES ? state->Byte & 0xff : 0;
    size_t used = 0;
    if (!have) {
        if (!n)
            return (size_t)-2;
        unsigned char lead = (unsigned char)s[used++];
        if (lead < 0x80) {
            *point = lead;
            return used;
        }
        if (lead >= 0xc2 && lead < 0xe0) {
            need = 1;
            partial = lead & 0x1f;
        } else if (lead >= 0xe0 && lead < 0xf0) {
            need = 2;
            partial = lead & 0x0f;
        } else if (lead >= 0xf0 && lead < 0xf5) {
            need = 3;
            partial = lead & 0x07;
        } else {
            MsvcrtSetErrno(EILSEQ);
            return (size_t)-1;
        }
        have = 1;
    }
    while (have <= need) {
        if (used == n) {
            state->State = STATE_BYTES;
            state->Wchar = partial;
            state->Byte = (unsigned short)(have << 8 | need);
            return (size_t)-2;
        }
        unsigned char b = (unsigned char)s[used];
        if ((b & 0xc0) != 0x80) {
            state->State = 0;
            MsvcrtSetErrno(EILSEQ);
            return (size_t)-1;
        }
        used++;
        partial = partial << 6 | (b & 0x3f);
        have++;
    }
    state->State = 0;
    unsigned point_value = (unsigned)partial;
    unsigned minimum = need == 1 ? 0x80 : need == 2 ? 0x800 : 0x10000;
    if (point_value < minimum || point_value > 0x10ffff || (point_value >= 0xd800 && point_value < 0xe000)) {
        MsvcrtSetErrno(EILSEQ);
        return (size_t)-1;
    }
    *point = point_value;
    return used;
}

static size_t encode_utf8(char *out, unsigned point)
{
    if (point < 0x80) {
        out[0] = (char)point;
        return 1;
    }
    if (point < 0x800) {
        out[0] = (char)(0xc0 | point >> 6);
        out[1] = (char)(0x80 | (point & 0x3f));
        return 2;
    }
    if (point < 0x10000) {
        out[0] = (char)(0xe0 | point >> 12);
        out[1] = (char)(0x80 | (point >> 6 & 0x3f));
        out[2] = (char)(0x80 | (point & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | point >> 18);
    out[1] = (char)(0x80 | (point >> 12 & 0x3f));
    out[2] = (char)(0x80 | (point >> 6 & 0x3f));
    out[3] = (char)(0x80 | (point & 0x3f));
    return 4;
}

static MBSTATE g_mbrtoc_state;
static MBSTATE g_c16rtomb_state;
static MBSTATE g_c32rtomb_state;

/*
 * mbrtoc16: the next utf-16 unit of a utf-8 string. A character beyond the basic plane takes two calls, the second
 * of which gives its low surrogate and uses no bytes, which is told by (size_t)-3.
 */
size_t __cdecl mbrtoc16(char16_t *out, const char *s, size_t n, MBSTATE *state)
{
    if (!state)
        state = &g_mbrtoc_state;
    if (!s) {
        s = "";
        n = 1;
        out = nullptr;
    }
    if (state->State == STATE_PAIR) {
        if (out)
            *out = (char16_t)state->Wchar;
        state->State = 0;
        return (size_t)-3;
    }
    unsigned point;
    size_t used = decode_utf8(&point, s, n, state);
    if (used == (size_t)-1 || used == (size_t)-2)
        return used;
    if (point > 0xffff) {
        unsigned v = point - 0x10000;
        if (out)
            *out = (char16_t)(0xd800 | v >> 10);
        state->State = STATE_PAIR;
        state->Wchar = 0xdc00 | (v & 0x3ff);
        return used;
    }
    if (out)
        *out = (char16_t)point;
    return point ? used : 0;
}

size_t __cdecl mbrtoc32(char32_t *out, const char *s, size_t n, MBSTATE *state)
{
    if (!state)
        state = &g_mbrtoc_state;
    if (!s) {
        s = "";
        n = 1;
        out = nullptr;
    }
    unsigned point;
    size_t used = decode_utf8(&point, s, n, state);
    if (used == (size_t)-1 || used == (size_t)-2)
        return used;
    if (out)
        *out = point;
    return point ? used : 0;
}

/* c16rtomb: the high surrogate is kept until the low one comes and the pair is written as one character. */
size_t __cdecl c16rtomb(char *out, char16_t c, MBSTATE *state)
{
    if (!state)
        state = &g_c16rtomb_state;
    if (!out) {
        state->State = 0;
        return 1;
    }
    unsigned point = c;
    if (state->State == STATE_PAIR) {
        state->State = 0;
        if (c < 0xdc00 || c >= 0xe000) {
            MsvcrtSetErrno(EILSEQ);
            return (size_t)-1;
        }
        point = 0x10000 + ((unsigned)(state->Wchar - 0xd800) << 10) + (c - 0xdc00);
    } else if (c >= 0xd800 && c < 0xdc00) {
        state->State = STATE_PAIR;
        state->Wchar = c;
        return 0;
    } else if (c >= 0xdc00 && c < 0xe000) {
        MsvcrtSetErrno(EILSEQ);
        return (size_t)-1;
    }
    return encode_utf8(out, point);
}

size_t __cdecl c32rtomb(char *out, char32_t c, MBSTATE *state)
{
    if (!state)
        state = &g_c32rtomb_state;
    if (!out) {
        state->State = 0;
        return 1;
    }
    if (c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) {
        MsvcrtSetErrno(EILSEQ);
        return (size_t)-1;
    }
    return encode_utf8(out, c);
}

/* ------------------------------------------------------------------ restartable conversions */

/*
 * the _s forms of mbsrtowcs and wcsrtombs: the string at *src, up to count characters, into a buffer of size units.
 * Without a destination the size needed is given. A string that did not end is left at the next character and the
 * result is terminated; one that did sets *src to null. _TRUNCATE fits what can be and gives STRUNCATE.
 */
errno_t __cdecl mbsrtowcs_s(size_t *converted, wchar_t *dst, size_t size, const char **src, size_t count, MBSTATE *state)
{
    if (converted)
        *converted = 0;
    if ((!dst && size) || !src || !*src) {
        if (dst && size)
            *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    const char *s = *src;
    size_t length = strlen(s);
    if (!dst) {
        if (converted)
            *converted = (count < length ? count : length) + 1;
        return 0;
    }
    size_t n = count < length ? count : length;
    errno_t r = 0;
    if (n >= size) {
        if (count == (size_t)-1) {
            n = size - 1;
            r = STRUNCATE;
        } else {
            *dst = 0;
            MSVCRT_INVALID(ERANGE);
            return ERANGE;
        }
    }
    for (size_t i = 0; i < n; i++)
        dst[i] = (unsigned char)s[i];
    dst[n] = 0;
    *src = n == length ? nullptr : s + n;
    if (converted)
        *converted = n + 1;
    return r;
}

errno_t __cdecl wcsrtombs_s(size_t *converted, char *dst, size_t size, const wchar_t **src, size_t count, MBSTATE *state)
{
    if (converted)
        *converted = 0;
    if ((!dst && size) || !src || !*src) {
        if (dst && size)
            *dst = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    const wchar_t *s = *src;
    size_t length = wcslen(s);
    size_t n = count < length ? count : length;
    for (size_t i = 0; i < n; i++) {
        if (s[i] > 0xff) {
            if (dst && size)
                *dst = 0;
            MsvcrtSetErrno(EILSEQ);
            return EILSEQ;
        }
    }
    if (!dst) {
        if (converted)
            *converted = n + 1;
        return 0;
    }
    errno_t r = 0;
    if (n >= size) {
        if (count == (size_t)-1) {
            n = size - 1;
            r = STRUNCATE;
        } else {
            *dst = 0;
            MSVCRT_INVALID(ERANGE);
            return ERANGE;
        }
    }
    for (size_t i = 0; i < n; i++)
        dst[i] = (char)s[i];
    dst[n] = 0;
    *src = n == length ? nullptr : s + n;
    if (converted)
        *converted = n + 1;
    return r;
}

int __cdecl _wctomb_l(char *dst, wchar_t c, void *locale)
{
    return wctomb(dst, c);
}

errno_t __cdecl _wctomb_s_l(int *len, char *dst, size_t size, wchar_t c, void *locale)
{
    return wctomb_s(len, dst, size, c);
}

errno_t __cdecl _wcstombs_s_l(size_t *converted, char *dst, size_t size, const wchar_t *src, size_t count, void *locale)
{
    return wcstombs_s(converted, dst, size, src, count);
}

errno_t __cdecl wmemcpy_s(wchar_t *dst, size_t size, const wchar_t *src, size_t count)
{
    if (!count)
        return 0;
    if (!dst) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (!src) {
        memset(dst, 0, size * sizeof(wchar_t));
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (size < count) {
        memset(dst, 0, size * sizeof(wchar_t));
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(dst, src, count * sizeof(wchar_t));
    return 0;
}

errno_t __cdecl wmemmove_s(wchar_t *dst, size_t size, const wchar_t *src, size_t count)
{
    if (!count)
        return 0;
    if (!dst || !src) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (size < count) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memmove(dst, src, count * sizeof(wchar_t));
    return 0;
}

/* the two transformations of wctrans: toupper is 1 and tolower 2. */
unsigned short __cdecl wctrans(const char *name)
{
    if (!strcmp(name, "tolower"))
        return 2;
    if (!strcmp(name, "toupper"))
        return 1;
    return 0;
}

wint_t __cdecl towctrans(wint_t c, unsigned short transform)
{
    return transform == 1 ? towupper(c) : towlower(c);
}

/* ------------------------------------------------------------------ locales */

/* the locale object holds two pointers to the information of the locale and its multibyte code page. */
typedef struct {
    void *locinfo;
    void *mbcinfo;
} LOCALE_OBJECT;

static unsigned char g_locale_data[512];
static LOCALE_OBJECT g_current_locale = {g_locale_data, g_locale_data};

static LOCALE_OBJECT *make_locale(int category, int wide, const void *name)
{
    if (category < 0 || category > 5) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    /* only the c locale exists, and the user's, which is the same here. */
    int known = !name || (wide ? (!*(const wchar_t *)name || !wcscmp(name, L"C")) : (!*(const char *)name || !strcmp(name, "C")));
    if (!known)
        return nullptr;
    LOCALE_OBJECT *l = malloc(sizeof(*l));
    if (l) {
        l->locinfo = g_locale_data;
        l->mbcinfo = g_locale_data;
    }
    return l;
}

void *__cdecl _create_locale(int category, const char *name)
{
    return make_locale(category, 0, name);
}

void *__cdecl _wcreate_locale(int category, const wchar_t *name)
{
    return make_locale(category, 1, name);
}

void __cdecl _free_locale(void *locale)
{
    if (locale && locale != &g_current_locale)
        free(locale);
}

void *__cdecl _get_current_locale(void)
{
    LOCALE_OBJECT *l = malloc(sizeof(*l));
    if (l)
        *l = g_current_locale;
    return l;
}

#define L1(ret, name, base, t1, a1) \
    ret __cdecl name(t1 a1, void *locale) { return base(a1); }
#define L2(ret, name, base, t1, a1, t2, a2) \
    ret __cdecl name(t1 a1, t2 a2, void *locale) { return base(a1, a2); }
#define L3(ret, name, base, t1, a1, t2, a2, t3, a3) \
    ret __cdecl name(t1 a1, t2 a2, t3 a3, void *locale) { return base(a1, a2, a3); }

L2(int, _stricoll_l, _stricoll, const char *, a, const char *, b)
L3(int, _strncoll_l, _strncoll, const char *, a, const char *, b, size_t, n)
L3(int, _strnicoll_l, _strnicoll, const char *, a, const char *, b, size_t, n)
L2(int, _wcsicoll_l, _wcsicoll, const wchar_t *, a, const wchar_t *, b)
L3(int, _wcsncoll_l, _wcsncoll, const wchar_t *, a, const wchar_t *, b, size_t, n)
L3(int, _wcsnicoll_l, _wcsnicoll, const wchar_t *, a, const wchar_t *, b, size_t, n)
L1(char *, _strlwr_l, _strlwr, char *, s)
L1(char *, _strupr_l, _strupr, char *, s)
L2(errno_t, _strlwr_s_l, _strlwr_s, char *, s, size_t, size)
L2(errno_t, _strupr_s_l, _strupr_s, char *, s, size_t, size)
L1(wchar_t *, _wcslwr_l, _wcslwr, wchar_t *, s)
L1(wchar_t *, _wcsupr_l, _wcsupr, wchar_t *, s)
L2(errno_t, _wcslwr_s_l, _wcslwr_s, wchar_t *, s, size_t, size)
L2(errno_t, _wcsupr_s_l, _wcsupr_s, wchar_t *, s, size_t, size)
L1(int, _isblank_l, isblank, int, c)
L1(int, _iswblank_l, iswblank, wint_t, c)
L1(int, _iswcsym_l, __iswcsym, wint_t, c)
L1(int, _iswcsymf_l, __iswcsymf, wint_t, c)

/* ------------------------------------------------------------------ time */

typedef struct {
    long long tv_sec;
    long tv_nsec;
} TIMESPEC64;

typedef struct {
    long tv_sec;
    long tv_nsec;
} TIMESPEC32;

/* the time base 1, utc, from the system clock; any other base gives 0. */
int __cdecl _timespec64_get(TIMESPEC64 *ts, int base)
{
    if (!ts) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    if (base != 1)
        return 0;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    unsigned long long ticks = (unsigned long long)now.dwHighDateTime << 32 | now.dwLowDateTime;
    ticks -= 116444736000000000ull;
    ts->tv_sec = (long long)(ticks / 10000000ull);
    ts->tv_nsec = (long)(ticks % 10000000ull) * 100;
    return base;
}

int __cdecl _timespec32_get(TIMESPEC32 *ts, int base)
{
    TIMESPEC64 wide;
    if (!ts) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    if (!_timespec64_get(&wide, base))
        return 0;
    ts->tv_sec = (long)wide.tv_sec;
    ts->tv_nsec = wide.tv_nsec;
    return base;
}

/* ------------------------------------------------------------------ memory */

/*
 * the bytes of an aligned block that the program asked for or a little more: what the block holds less the room for the
 * alignment and the pointer that finds it, so that a reallocation that clears the new bytes starts where the old ones end.
 */
size_t __cdecl _aligned_msize(void *p, size_t alignment, size_t offset)
{
    if (!p || !alignment || (alignment & (alignment - 1))) {
        MSVCRT_INVALID(EINVAL);
        return (size_t)-1;
    }
    uintptr_t q = (uintptr_t)p & ~(sizeof(void *) - 1);
    uintptr_t raw = ((uintptr_t *)q)[-1];
    size_t whole = _msize((void *)raw), gap = (0 - offset) & (sizeof(void *) - 1);
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    return whole - (alignment - 1) - sizeof(void *) - gap;
}

void *__cdecl _aligned_offset_recalloc(void *p, size_t count, size_t size, size_t alignment, size_t offset)
{
    if (size && count > (size_t)-32 / size) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    size_t total = count * size, old = p ? _aligned_msize(p, alignment, offset) : 0;
    void *q = _aligned_offset_realloc(p, total, alignment, offset);
    if (q && total > old)
        memset((char *)q + old, 0, total - old);
    return q;
}

void *__cdecl _aligned_recalloc(void *p, size_t count, size_t size, size_t alignment)
{
    return _aligned_offset_recalloc(p, count, size, alignment, 0);
}

/* the blocks of the heap one at a time; the entry says where it left off. */
typedef struct {
    int *_pentry;
    size_t _size;
    int _useflag;
} HEAPINFO;

int __cdecl _heapwalk(HEAPINFO *entry)
{
    if (!entry) {
        MSVCRT_INVALID(EINVAL);
        return -4;
    }
    PROCESS_HEAP_ENTRY block = {};
    block.lpData = entry->_pentry;
    for (;;) {
        if (!HeapWalk(MsvcrtHeap, &block))
            return GetLastError() == ERROR_NO_MORE_ITEMS ? -5 : -4;
        if (block.wFlags & (PROCESS_HEAP_REGION | PROCESS_HEAP_UNCOMMITTED_RANGE))
            continue;
        break;
    }
    entry->_pentry = block.lpData;
    entry->_size = block.cbData;
    entry->_useflag = (block.wFlags & PROCESS_HEAP_ENTRY_BUSY) ? 1 : 0;
    return -2;
}

/* ------------------------------------------------------------------ searching */

void *__cdecl _lfind_s(const void *key, const void *base, unsigned int *number, size_t width,
                       int(__cdecl *compare)(void *, const void *, const void *), void *context)
{
    if (!key || !base || !number || !width || !compare) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    const char *p = base;
    for (unsigned i = 0; i < *number; i++, p += width)
        if (!compare(context, key, p))
            return (void *)p;
    return nullptr;
}

void *__cdecl _lsearch_s(const void *key, void *base, unsigned int *number, size_t width,
                         int(__cdecl *compare)(void *, const void *, const void *), void *context)
{
    void *found = _lfind_s(key, base, number, width, compare, context);
    if (found || !key || !base || !number || !width || !compare)
        return found;
    char *end = (char *)base + (size_t)*number * width;
    memcpy(end, key, width);
    ++*number;
    return end;
}
