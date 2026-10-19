/*
 * the printf family over msvcrt's formatting engine (format.h).
 */
#include "msvcrt_private.h"

unsigned int MsvcrtOutputFormat;
static int g_count_output;

void MsvcrtFormatDouble(double value, int fmt, int prec, int caps, int alt, char *buf);

#define FL_SIGN 0x0001
#define FL_SIGNSP 0x0002
#define FL_LEFT 0x0004
#define FL_LEADZERO 0x0008
#define FL_LONG 0x0010
#define FL_SHORT 0x0020
#define FL_SIGNED 0x0040
#define FL_ALTERNATE 0x0080
#define FL_NEGATIVE 0x0100
#define FL_FORCEOCTAL 0x0200
#define FL_WIDECHAR 0x0800
#define FL_LONGLONG 0x1000
#define FL_I64 0x8000

enum { ST_NORMAL, ST_PERCENT, ST_FLAG, ST_WIDTH, ST_DOT, ST_PRECIS, ST_SIZE, ST_TYPE };
enum { CL_OTHER, CL_PERCENT, CL_DOT, CL_STAR, CL_ZERO, CL_DIGIT, CL_FLAG, CL_SIZE, CL_TYPE };

/* character classes from ' ' to 'z' and the state transitions, read from msvcrt's __lookuptable. */
static constexpr unsigned char g_class['z' - ' ' + 1] = {
    /*   ! " # $ % & ' ( ) * + , - . / */
    6, 0, 0, 6, 0, 1, 0, 0, 0, 0, 3, 6, 0, 6, 2, 0,
    /* 0 1 2 3 4 5 6 7 8 9 : ; < = > ? */
    4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0,
    /* @ A B C D E F G H I J K L M N O */
    0, 8, 0, 8, 0, 8, 7, 8, 0, 7, 0, 0, 7, 0, 7, 0,
    /* P Q R S T U V W X Y Z [ \ ] ^ _ */
    0, 0, 0, 8, 0, 0, 0, 0, 8, 0, 8, 0, 0, 0, 0, 0,
    /* ` a b c d e f g h i j k l m n o */
    0, 8, 0, 8, 8, 8, 8, 8, 7, 8, 7, 0, 7, 0, 8, 8,
    /* p q r s t u v w x y z */
    8, 0, 0, 8, 7, 8, 0, 7, 8, 0, 7,
};

static constexpr unsigned char g_next[9][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 1}, {0, 4, 4, 4, 0, 0, 0, 0},
    {0, 3, 3, 0, 5, 0, 0, 0}, {0, 2, 2, 3, 5, 5, 0, 0}, {0, 3, 3, 3, 5, 5, 0, 0},
    {0, 2, 2, 0, 0, 0, 0, 0}, {0, 6, 6, 6, 6, 6, 6, 0}, {0, 7, 7, 7, 7, 7, 7, 0},
};

/* where formatted characters go: a stream, a string, or nowhere but counted. */
typedef union {
    uint64_t u;
    double d;
    void *p;
} PARG;

typedef struct {
    FILE *stream;
    void *buf;
    size_t cnt;
    int count_only;
    int count;
    /* the arguments in the order the format takes them, when positional parameters were put in order. */
    const PARG *seq;
    int seq_at;
} SINK;

/* the next argument of the engine: from the va_list, or from the ordered table of a positional format. */
#define ARG_INT() (out->seq ? (int)(unsigned)out->seq[out->seq_at++].u : va_arg(ap, int))
#define ARG_U64() (out->seq ? out->seq[out->seq_at++].u : va_arg(ap, uint64_t))
#define ARG_PTR() (out->seq ? out->seq[out->seq_at++].p : va_arg(ap, void *))
#define ARG_DOUBLE() (out->seq ? out->seq[out->seq_at++].d : va_arg(ap, double))

static void sink_put_a(SINK *s, char c)
{
    if (s->count < 0)
        return;
    if (s->stream) {
        FILE *f = s->stream;
        if (--f->_cnt >= 0)
            *f->_ptr++ = c;
        else if (MsvcrtFlsbuf((unsigned char)c, f) == EOF) {
            s->count = -1;
            return;
        }
    } else if (!s->count_only) {
        if (!s->cnt) {
            s->count = -1;
            return;
        }
        *(char *)s->buf = c;
        s->buf = (char *)s->buf + 1;
        s->cnt--;
    }
    s->count++;
}

static void sink_put_w(SINK *s, wchar_t c)
{
    if (s->count < 0)
        return;
    if (s->stream) {
        if (MsvcrtFputwcNolock(c, s->stream) == WEOF) {
            s->count = -1;
            return;
        }
    } else if (!s->count_only) {
        if (!s->cnt) {
            s->count = -1;
            return;
        }
        *(wchar_t *)s->buf = c;
        s->buf = (wchar_t *)s->buf + 1;
        s->cnt--;
    }
    s->count++;
}

static void sink_repeat_a(SINK *s, char c, int n)
{
    while (n-- > 0 && s->count >= 0)
        sink_put_a(s, c);
}

static void sink_repeat_w(SINK *s, wchar_t c, int n)
{
    while (n-- > 0 && s->count >= 0)
        sink_put_w(s, c);
}

#define FORMAT_NAME format_a
#define FCHAR char
#define FORMAT_WIDE 0
#define sink_put sink_put_a
#define sink_repeat sink_repeat_a
#include "format.h"
#undef FORMAT_NAME
#undef FCHAR
#undef FORMAT_WIDE
#undef sink_put
#undef sink_repeat

#define FORMAT_NAME format_w
#define FCHAR wchar_t
#define FORMAT_WIDE 1
#define sink_put sink_put_w
#define sink_repeat sink_repeat_w
#include "format.h"
#undef FORMAT_NAME
#undef FCHAR
#undef FORMAT_WIDE
#undef sink_put
#undef sink_repeat

#define ARGMAX 99

enum { PK_NONE, PK_INT, PK_I64, PK_PTR, PK_DOUBLE };

typedef struct {
    int kind[ARGMAX + 1];
    int highest;
} POSITIONS;

#define POSITIONAL_NAME positional_a
#define FCHAR char
#include "positional.h"
#undef POSITIONAL_NAME
#undef FCHAR

#define POSITIONAL_NAME positional_w
#define FCHAR wchar_t
#include "positional.h"
#undef POSITIONAL_NAME
#undef FCHAR

/* ------------------------------------------------------------------ engine entry points */

int MsvcrtVsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    SINK s = {nullptr, buf, size > INT_MAX ? INT_MAX : size, !buf, 0};
    return format_a(&s, fmt, ap);
}

int MsvcrtVsnwprintf(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap)
{
    SINK s = {nullptr, buf, size > INT_MAX / 2 ? INT_MAX / 2 : size, !buf, 0};
    return format_w(&s, fmt, ap);
}

int MsvcrtFormatA(FILE *f, const char *fmt, va_list ap)
{
    if (!(f->_flag & _IOSTRG) && MsvcrtFd(f->_file)->textmode != TM_ANSI) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {f, nullptr, 0, 0, 0};
    return format_a(&s, fmt, ap);
}

int MsvcrtFormatW(FILE *f, const wchar_t *fmt, va_list ap)
{
    SINK s = {f, nullptr, 0, 0, 0};
    return format_w(&s, fmt, ap);
}

/* ------------------------------------------------------------------ streams */

int __cdecl vfprintf(FILE *f, const char *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    int r = MsvcrtFormatA(f, fmt, ap);
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl vfwprintf(FILE *f, const wchar_t *fmt, va_list ap)
{
    if (!f || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    int r = MsvcrtFormatW(f, fmt, ap);
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl fwprintf(FILE *f, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl vprintf(const char *fmt, va_list ap)
{
    return vfprintf(msvcrt_stdout, fmt, ap);
}

int __cdecl vwprintf(const wchar_t *fmt, va_list ap)
{
    return vfwprintf(msvcrt_stdout, fmt, ap);
}

int __cdecl printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(msvcrt_stdout, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl wprintf(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(msvcrt_stdout, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl vfprintf_s(FILE *f, const char *fmt, va_list ap)
{
    return vfprintf(f, fmt, ap);
}

int __cdecl vfwprintf_s(FILE *f, const wchar_t *fmt, va_list ap)
{
    return vfwprintf(f, fmt, ap);
}

int __cdecl vprintf_s(const char *fmt, va_list ap)
{
    return vfprintf(msvcrt_stdout, fmt, ap);
}

int __cdecl vwprintf_s(const wchar_t *fmt, va_list ap)
{
    return vfwprintf(msvcrt_stdout, fmt, ap);
}

int __cdecl fprintf_s(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl fwprintf_s(FILE *f, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl printf_s(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(msvcrt_stdout, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl wprintf_s(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(msvcrt_stdout, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _fprintf_l(FILE *f, const char *fmt, void *locale, ...)
{
    va_list ap;
    va_start(ap, locale);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _printf_l(const char *fmt, void *locale, ...)
{
    va_list ap;
    va_start(ap, locale);
    int r = vfprintf(msvcrt_stdout, fmt, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ strings */

/* msvcrt's _vsnprintf: -1 when the output does not fit, no terminator when it fits exactly. */
int __cdecl _vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (!fmt || (size && !buf)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size > INT_MAX ? INT_MAX : size, !buf, 0};
    int r = format_a(&s, fmt, ap);
    if (buf && s.cnt && r >= 0)
        *(char *)s.buf = 0;
    return r;
}

int __cdecl _vsnwprintf(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap)
{
    if (!fmt || (size && !buf)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size > INT_MAX / 2 ? INT_MAX / 2 : size, !buf, 0};
    int r = format_w(&s, fmt, ap);
    if (buf && s.cnt && r >= 0)
        *(wchar_t *)s.buf = 0;
    return r;
}

int __cdecl _snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _snwprintf(wchar_t *buf, size_t size, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnwprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl vsprintf(char *buf, const char *fmt, va_list ap)
{
    if (!buf || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return _vsnprintf(buf, INT_MAX, fmt, ap);
}

int __cdecl sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

/* msvcrt's swprintf and vswprintf are the old forms without a size. */
int __cdecl vswprintf(wchar_t *buf, const wchar_t *fmt, va_list ap)
{
    if (!buf || !fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return _vsnwprintf(buf, INT_MAX / 2, fmt, ap);
}

int __cdecl swprintf(wchar_t *buf, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _swprintf(wchar_t *buf, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _vswprintf(wchar_t *buf, const wchar_t *fmt, va_list ap)
{
    return vswprintf(buf, fmt, ap);
}

int __cdecl _vscprintf(const char *fmt, va_list ap)
{
    if (!fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return MsvcrtVsnprintf(nullptr, 0, fmt, ap);
}

int __cdecl _vscwprintf(const wchar_t *fmt, va_list ap)
{
    if (!fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return MsvcrtVsnwprintf(nullptr, 0, fmt, ap);
}

int __cdecl _scprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vscprintf(fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _scwprintf(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vscwprintf(fmt, ap);
    va_end(ap);
    return r;
}

#define _TRUNCATE ((size_t)-1)

/* the secure forms: truncation on request, otherwise an empty string and ERANGE. */
int __cdecl _vsnprintf_s(char *buf, size_t size, size_t count, const char *fmt, va_list ap)
{
    if (!fmt || !buf || !size) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    size_t limit = count == _TRUNCATE || count >= size ? size - 1 : count;
    SINK s = {nullptr, buf, limit, 0, 0};
    int r = format_a(&s, fmt, ap);
    if (r >= 0) {
        *(char *)s.buf = 0;
        return r;
    }
    if (count == _TRUNCATE || count < size) {
        buf[limit] = 0;
        return -1;
    }
    *buf = 0;
    MSVCRT_INVALID(ERANGE);
    return -1;
}

int __cdecl _vsnwprintf_s(wchar_t *buf, size_t size, size_t count, const wchar_t *fmt, va_list ap)
{
    if (!fmt || !buf || !size) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    size_t limit = count == _TRUNCATE || count >= size ? size - 1 : count;
    SINK s = {nullptr, buf, limit, 0, 0};
    int r = format_w(&s, fmt, ap);
    if (r >= 0) {
        *(wchar_t *)s.buf = 0;
        return r;
    }
    if (count == _TRUNCATE || count < size) {
        buf[limit] = 0;
        return -1;
    }
    *buf = 0;
    MSVCRT_INVALID(ERANGE);
    return -1;
}

int __cdecl vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (!fmt || !buf || !size) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size - 1, 0, 0};
    int r = format_a(&s, fmt, ap);
    if (r < 0) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return -1;
    }
    *(char *)s.buf = 0;
    return r;
}

int __cdecl vswprintf_s(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap)
{
    if (!fmt || !buf || !size) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size - 1, 0, 0};
    int r = format_w(&s, fmt, ap);
    if (r < 0) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return -1;
    }
    *(wchar_t *)s.buf = 0;
    return r;
}

int __cdecl sprintf_s(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf_s(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl swprintf_s(wchar_t *buf, size_t size, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf_s(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _snprintf_s(char *buf, size_t size, size_t count, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnprintf_s(buf, size, count, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _snwprintf_s(wchar_t *buf, size_t size, size_t count, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnwprintf_s(buf, size, count, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _vsnprintf_l(char *buf, size_t size, const char *fmt, void *locale, va_list ap)
{
    return _vsnprintf(buf, size, fmt, ap);
}

int __cdecl _vsnwprintf_l(wchar_t *buf, size_t size, const wchar_t *fmt, void *locale, va_list ap)
{
    return _vsnwprintf(buf, size, fmt, ap);
}

int __cdecl _sprintf_l(char *buf, const char *fmt, void *locale, ...)
{
    va_list ap;
    va_start(ap, locale);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _snprintf_l(char *buf, size_t size, const char *fmt, void *locale, ...)
{
    va_list ap;
    va_start(ap, locale);
    int r = _vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ settings */

unsigned int __cdecl _set_output_format(unsigned int format)
{
    unsigned int old = MsvcrtOutputFormat;
    MsvcrtOutputFormat = format & 1;
    return old;
}

unsigned int __cdecl _get_output_format(void)
{
    return MsvcrtOutputFormat;
}

int __cdecl _set_printf_count_output(int enable)
{
    int old = g_count_output;
    g_count_output = enable != 0;
    return old;
}

int __cdecl _get_printf_count_output(void)
{
    return g_count_output;
}

/* ------------------------------------------------------------------ positional parameters */

/*
 * the _p functions: the format may number its arguments with n$, also the ones of a width or precision written *n$. A
 * format that cannot be put in order is an invalid parameter. The output goes where the plain function's would, the
 * string forms with the checks of the _s forms.
 */
int MsvcrtFormatPositionalA(FILE *f, const char *fmt, va_list ap)
{
    char *rewritten = nullptr;
    PARG *seq = nullptr;
    if (!f || !fmt || positional_a(fmt, ap, &rewritten, &seq) < 0) {
        free(rewritten);
        free(seq);
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    SINK s = {f, nullptr, 0, 0, 0, seq, 0};
    int r = format_a(&s, rewritten, ap);
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    free(rewritten);
    free(seq);
    return r;
}

int MsvcrtFormatPositionalW(FILE *f, const wchar_t *fmt, va_list ap)
{
    wchar_t *rewritten = nullptr;
    PARG *seq = nullptr;
    if (!f || !fmt || positional_w(fmt, ap, &rewritten, &seq) < 0) {
        free(rewritten);
        free(seq);
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    SINK s = {f, nullptr, 0, 0, 0, seq, 0};
    int r = format_w(&s, rewritten, ap);
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    free(rewritten);
    free(seq);
    return r;
}

int MsvcrtVsprintfPositionalA(char *buf, size_t size, const char *fmt, va_list ap)
{
    char *rewritten = nullptr;
    PARG *seq = nullptr;
    if (!fmt || !buf || !size || positional_a(fmt, ap, &rewritten, &seq) < 0) {
        if (buf && size)
            *buf = 0;
        free(rewritten);
        free(seq);
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size - 1, 0, 0, seq, 0};
    int r = format_a(&s, rewritten, ap);
    free(rewritten);
    free(seq);
    if (r < 0) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return -1;
    }
    *(char *)s.buf = 0;
    return r;
}

int MsvcrtVsprintfPositionalW(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap)
{
    wchar_t *rewritten = nullptr;
    PARG *seq = nullptr;
    if (!fmt || !buf || !size || positional_w(fmt, ap, &rewritten, &seq) < 0) {
        if (buf && size)
            *buf = 0;
        free(rewritten);
        free(seq);
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    SINK s = {nullptr, buf, size - 1, 0, 0, seq, 0};
    int r = format_w(&s, rewritten, ap);
    free(rewritten);
    free(seq);
    if (r < 0) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return -1;
    }
    *(wchar_t *)s.buf = 0;
    return r;
}
