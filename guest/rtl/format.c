/*
 * printf engine shared by DbgPrint, the kernel logger and the crt style
 * _snprintf family. supports the msvc conventions: %S and %ls are wide
 * strings, %wZ is a PUNICODE_STRING, %Z a PANSI_STRING.
 */
#include "rtl.h"

typedef struct Sink {
    char *buf;
    wchar_t *wbuf;
    size_t size;
    size_t pos;
} Sink;

static void put(Sink *s, unsigned c)
{
    if (s->pos + 1 < s->size) {
        if (s->wbuf)
            s->wbuf[s->pos] = (wchar_t)c;
        else
            s->buf[s->pos] = (char)c;
    }
    s->pos++;
}

/* emits a code point; narrow sinks get utf-8. */
static void put_cp(Sink *s, unsigned c)
{
    if (s->wbuf || c < 0x80) {
        put(s, c);
    } else if (c < 0x800) {
        put(s, 0xC0 | (c >> 6));
        put(s, 0x80 | (c & 0x3F));
    } else {
        put(s, 0xE0 | (c >> 12));
        put(s, 0x80 | ((c >> 6) & 0x3F));
        put(s, 0x80 | (c & 0x3F));
    }
}

static void put_pad(Sink *s, int n, char c)
{
    while (n-- > 0)
        put(s, (unsigned char)c);
}

static void put_number(Sink *s, uint64_t v, int base, bool upper, bool negative, int width, int precision,
                       bool left, char pad, bool alt, bool plus)
{
    char tmp[66];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0 && precision != 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (n < precision)
        tmp[n++] = '0';
    int prefix = 0;
    char pre[3];
    if (negative)
        pre[prefix++] = '-';
    else if (plus)
        pre[prefix++] = '+';
    if (alt && base == 16) {
        pre[prefix++] = '0';
        pre[prefix++] = upper ? 'X' : 'x';
    }
    int total = n + prefix;
    if (!left && pad == ' ')
        put_pad(s, width - total, ' ');
    for (int i = 0; i < prefix; i++)
        put(s, (unsigned char)pre[i]);
    if (!left && pad == '0')
        put_pad(s, width - total, '0');
    while (n)
        put(s, (unsigned char)tmp[--n]);
    if (left)
        put_pad(s, width - total, ' ');
}

static void put_str(Sink *s, const char *str, int len, int width, bool left)
{
    if (!str) {
        str = "(null)";
        len = -1;
    }
    int n = 0;
    if (len < 0)
        while (str[n])
            n++;
    else
        while (n < len && str[n])
            n++;
    if (!left)
        put_pad(s, width - n, ' ');
    for (int i = 0; i < n; i++)
        put(s, (unsigned char)str[i]);
    if (left)
        put_pad(s, width - n, ' ');
}

static void put_wstr(Sink *s, const wchar_t *str, int len, int width, bool left)
{
    if (!str) {
        put_str(s, "(null)", -1, width, left);
        return;
    }
    int n = 0;
    if (len < 0)
        while (str[n])
            n++;
    else
        while (n < len && str[n])
            n++;
    if (!left)
        put_pad(s, width - n, ' ');
    for (int i = 0; i < n; i++)
        put_cp(s, str[i]);
    if (left)
        put_pad(s, width - n, ' ');
}

/* the format string is narrow or wide depending on the sink. */
static int format(Sink *s, const void *fmt_ptr, bool wide_fmt, va_list ap)
{
    const char *fn = fmt_ptr;
    const wchar_t *fw = fmt_ptr;
    size_t i = 0;
#define FCH(k) (wide_fmt ? (unsigned)fw[(k)] : (unsigned char)fn[(k)])
    for (;;) {
        unsigned c = FCH(i);
        if (!c)
            break;
        if (c != '%') {
            put(s, c);
            i++;
            continue;
        }
        i++;
        bool left = false, alt = false, plus = false;
        char pad = ' ';
        for (;;) {
            c = FCH(i);
            if (c == '-')
                left = true;
            else if (c == '0')
                pad = '0';
            else if (c == '#')
                alt = true;
            else if (c == '+')
                plus = true;
            else if (c == ' ')
                ;
            else
                break;
            i++;
        }
        int width = 0, precision = -1;
        if (FCH(i) == '*') {
            width = va_arg(ap, int);
            i++;
        } else {
            while (FCH(i) >= '0' && FCH(i) <= '9')
                width = width * 10 + (int)(FCH(i++) - '0');
        }
        if (FCH(i) == '.') {
            i++;
            precision = 0;
            if (FCH(i) == '*') {
                precision = va_arg(ap, int);
                i++;
            } else {
                while (FCH(i) >= '0' && FCH(i) <= '9')
                    precision = precision * 10 + (int)(FCH(i++) - '0');
            }
        }
        int size = 0; /* 0 int, 1 long (32 bit), 2 long long, 3 size_t/ptr, -1 short */
        bool wide_arg = false, narrow_arg = false;
        for (;;) {
            c = FCH(i);
            if (c == 'l') {
                if (FCH(i + 1) == 'l') {
                    size = 2;
                    i++;
                } else {
                    size = 1;
                    wide_arg = true;
                }
            } else if (c == 'h') {
                size = -1;
                narrow_arg = true;
            } else if (c == 'z' || c == 'j' || c == 't' || c == 'I') {
                if (c == 'I' && FCH(i + 1) == '6' && FCH(i + 2) == '4') {
                    i += 2;
                    size = 2;
                } else if (c == 'I' && FCH(i + 1) == '3' && FCH(i + 2) == '2') {
                    i += 2;
                    size = 0;
                } else {
                    size = 3;
                }
            } else if (c == 'w') {
                wide_arg = true;
            } else {
                break;
            }
            i++;
        }
        c = FCH(i++);
        switch (c) {
        case 'd':
        case 'i': {
            int64_t v = size >= 2 ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
            if (size == -1)
                v = (short)v;
            put_number(s, (uint64_t)(v < 0 ? -v : v), 10, false, v < 0, width, precision, left, pad, false, plus);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            uint64_t v = size >= 2 ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
            if (size == -1)
                v = (unsigned short)v;
            int base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            put_number(s, v, base, c == 'X', false, width, precision, left, pad, alt, false);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)va_arg(ap, void *);
            put_number(s, v, 16, true, false, width ? width : 16, precision, left, '0', false, false);
            break;
        }
        case 'c': {
            unsigned ch = (unsigned)va_arg(ap, int);
            if (!left)
                put_pad(s, width - 1, ' ');
            put_cp(s, ch & 0xFFFF);
            if (left)
                put_pad(s, width - 1, ' ');
            break;
        }
        case 'C':
            put_cp(s, (unsigned)va_arg(ap, int) & 0xFFFF);
            break;
        case 's': {
            /* %s follows the format width: narrow for printf, wide for wprintf. */
            bool wide = wide_arg || (wide_fmt && !narrow_arg);
            if (wide)
                put_wstr(s, va_arg(ap, const wchar_t *), precision, width, left);
            else
                put_str(s, va_arg(ap, const char *), precision, width, left);
            break;
        }
        case 'S': {
            bool wide = !wide_fmt || wide_arg;
            if (narrow_arg)
                wide = false;
            if (wide)
                put_wstr(s, va_arg(ap, const wchar_t *), precision, width, left);
            else
                put_str(s, va_arg(ap, const char *), precision, width, left);
            break;
        }
        case 'Z': {
            if (wide_arg) {
                const UNICODE_STRING *us = va_arg(ap, const UNICODE_STRING *);
                if (us && us->Buffer)
                    put_wstr(s, us->Buffer, us->Length / 2, width, left);
                else
                    put_str(s, "(null)", -1, width, left);
            } else {
                const ANSI_STRING *as = va_arg(ap, const ANSI_STRING *);
                if (as && as->Buffer)
                    put_str(s, as->Buffer, as->Length, width, left);
                else
                    put_str(s, "(null)", -1, width, left);
            }
            break;
        }
        case '%':
            put(s, '%');
            break;
        default:
            put(s, '%');
            if (c)
                put(s, c);
            else
                i--;
            break;
        }
    }
#undef FCH
    if (s->size) {
        size_t end = s->pos < s->size ? s->pos : s->size - 1;
        if (s->wbuf)
            s->wbuf[end] = 0;
        else
            s->buf[end] = 0;
    }
    return (int)s->pos;
}

int mime_vformat(char *buf, size_t size, const char *fmt, va_list ap)
{
    Sink s = {buf, nullptr, size, 0};
    return format(&s, fmt, false, ap);
}

int mime_format(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = mime_vformat(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int _vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    Sink s = {buf, nullptr, size + 1, 0};
    char dummy;
    if (!buf)
        s.buf = &dummy, s.size = 0;
    int n = format(&s, fmt, false, ap);
    return (size_t)n > size ? -1 : n;
}

int _snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int _vsnwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, va_list ap)
{
    Sink s = {nullptr, buf, count + 1, 0};
    int n = format(&s, fmt, true, ap);
    return (size_t)n > count ? -1 : n;
}

int _snwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(buf, count, fmt, ap);
    va_end(ap);
    return n;
}
