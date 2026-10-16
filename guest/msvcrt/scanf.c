/*
 * the scanf family over msvcrt's input engine (scan.h).
 */
#include "msvcrt_private.h"

enum { SZ_SHORT = 1, SZ_LONG, SZ_LONGLONG, SZ_LONGDOUBLE };

/* where characters come from: a stream or a counted string. */
typedef struct {
    FILE *stream;
    const void *str;
    size_t len;
    size_t pos;
    int wide;
    int consumed;
    int pushed;
    int secure;
} SOURCE;

static int src_get(SOURCE *s)
{
    int c;
    if (s->pushed != EOF) {
        c = s->pushed;
        s->pushed = EOF;
    } else if (s->stream) {
        if (s->wide) {
            wint_t w = MsvcrtFgetwcNolock(s->stream);
            c = w == WEOF ? EOF : w;
        } else {
            c = --s->stream->_cnt >= 0 ? (unsigned char)*s->stream->_ptr++ : _filbuf(s->stream);
        }
    } else {
        if (s->pos >= s->len)
            return EOF;
        c = s->wide ? ((const wchar_t *)s->str)[s->pos] : ((const unsigned char *)s->str)[s->pos];
        if (!c)
            return EOF;
        s->pos++;
    }
    s->consumed++;
    return c;
}

int __cdecl _ungetc_nolock(int c, FILE *f);
wint_t __cdecl ungetwc(wint_t c, FILE *f);

static void src_unget(SOURCE *s, int c)
{
    if (c == EOF)
        return;
    s->consumed--;
    s->pushed = c;
}

/* gives a pushed back character back to the stream at the end. */
static void src_finish(SOURCE *s)
{
    if (s->pushed == EOF || !s->stream)
        return;
    if (s->wide && (s->stream->_flag & _IOSTRG || !(MsvcrtFd(s->stream->_file)->osfile & FTEXT) ||
                    MsvcrtFd(s->stream->_file)->textmode != TM_ANSI)) {
        _ungetc_nolock(s->pushed >> 8, s->stream);
        _ungetc_nolock(s->pushed & 0xff, s->stream);
    } else {
        _ungetc_nolock(s->pushed & 0xff, s->stream);
    }
    s->pushed = EOF;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* stores character c through *dst as a narrow or wide character, converting between them. */
static void store_char(void **dst, int c, int wide_out)
{
    if (wide_out) {
        wchar_t *p = *dst;
        *p++ = (wchar_t)c;
        *dst = p;
    } else {
        char *p = *dst;
        if (c > 0xff) {
            char mb[8];
            wchar_t w = (wchar_t)c;
            int n = WideCharToMultiByte(CP_ACP, 0, &w, 1, mb, sizeof(mb), nullptr, nullptr);
            for (int i = 0; i < n; i++)
                *p++ = mb[i];
        } else {
            *p++ = (char)c;
        }
        *dst = p;
    }
}

#define SCAN_NAME scan_a
#define SCHAR char
#define SCHAR_U unsigned char
#define SCAN_WIDE 0
#define SCAN_ISSPACE(c) MsvcrtIsSpace((unsigned char)(c))
#include "scan.h"
#undef SCAN_NAME
#undef SCHAR
#undef SCHAR_U
#undef SCAN_WIDE
#undef SCAN_ISSPACE

#define SCAN_NAME scan_w
#define SCHAR wchar_t
#define SCHAR_U wchar_t
#define SCAN_WIDE 1
#define SCAN_ISSPACE(c) ((MsvcrtWctype((wint_t)(c)) & _SPACE) != 0)
#include "scan.h"
#undef SCAN_NAME
#undef SCHAR
#undef SCHAR_U
#undef SCAN_WIDE
#undef SCAN_ISSPACE

static int scan_any_a(FILE *stream, const char *str, size_t len, const char *fmt, va_list ap, int secure)
{
    SOURCE s = {stream, str, len, 0, 0, 0, EOF, secure};
    int r = scan_a(&s, fmt, ap);
    src_finish(&s);
    return r;
}

static int scan_any_w(FILE *stream, const wchar_t *str, size_t len, const wchar_t *fmt, va_list ap, int secure)
{
    SOURCE s = {stream, str, len, 0, 1, 0, EOF, secure};
    int r = scan_w(&s, fmt, ap);
    src_finish(&s);
    return r;
}

int MsvcrtScanA(FILE *stream, const char *str, size_t len, const char *fmt, va_list ap)
{
    return scan_any_a(stream, str, len, fmt, ap, 0);
}

int MsvcrtScanW(FILE *stream, const wchar_t *str, size_t len, const wchar_t *fmt, va_list ap)
{
    return scan_any_w(stream, str, len, fmt, ap, 0);
}

/* ------------------------------------------------------------------ strings */

int __cdecl vsscanf(const char *str, const char *fmt, va_list ap)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtScanA(nullptr, str, (size_t)-1, fmt, ap);
}

int __cdecl vswscanf(const wchar_t *str, const wchar_t *fmt, va_list ap)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtScanW(nullptr, str, (size_t)-1, fmt, ap);
}

int __cdecl sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl swscanf(const wchar_t *str, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vswscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl sscanf_s(const char *str, const char *fmt, ...)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = scan_any_a(nullptr, str, (size_t)-1, fmt, ap, 1);
    va_end(ap);
    return r;
}

int __cdecl swscanf_s(const wchar_t *str, const wchar_t *fmt, ...)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = scan_any_w(nullptr, str, (size_t)-1, fmt, ap, 1);
    va_end(ap);
    return r;
}

int __cdecl _snscanf(const char *str, size_t len, const char *fmt, ...)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = MsvcrtScanA(nullptr, str, len, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _snwscanf(const wchar_t *str, size_t len, const wchar_t *fmt, ...)
{
    if (!str || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = MsvcrtScanW(nullptr, str, len, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _sscanf_l(const char *str, const char *fmt, void *locale, ...)
{
    va_list ap;
    va_start(ap, locale);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ streams */

int __cdecl vfscanf(FILE *f, const char *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = MsvcrtScanA(f, nullptr, 0, fmt, ap);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl vfwscanf(FILE *f, const wchar_t *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = MsvcrtScanW(f, nullptr, 0, fmt, ap);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl fwscanf(FILE *f, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

static int vfscanf_s(FILE *f, const char *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = scan_any_a(f, nullptr, 0, fmt, ap, 1);
    MsvcrtUnlockFile(f);
    return r;
}

static int vfwscanf_s(FILE *f, const wchar_t *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = scan_any_w(f, nullptr, 0, fmt, ap, 1);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl fscanf_s(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf_s(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl fwscanf_s(FILE *f, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwscanf_s(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(msvcrt_stdin, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl wscanf(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwscanf(msvcrt_stdin, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl scanf_s(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf_s(msvcrt_stdin, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl wscanf_s(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwscanf_s(msvcrt_stdin, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl vscanf(const char *fmt, va_list ap)
{
    return vfscanf(msvcrt_stdin, fmt, ap);
}

int __cdecl vwscanf(const wchar_t *fmt, va_list ap)
{
    return vfwscanf(msvcrt_stdin, fmt, ap);
}
