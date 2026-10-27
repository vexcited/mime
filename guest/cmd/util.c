/* memory, strings, console and file output. */
#include "cmd.h"

STATE g;
IO g_io;

/* ------------------------------------------------------------------ memory */

static void out_of_memory(void)
{
    static const char text[] = "Out of memory.\r\n";
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, sizeof(text) - 1, &written, nullptr);
    ExitProcess(1);
}

void *xalloc(SIZE_T bytes)
{
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
    if (!p)
        out_of_memory();
    return p;
}

void *xrealloc(void *p, SIZE_T bytes)
{
    void *n = p ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, bytes ? bytes : 1)
                : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
    if (!n)
        out_of_memory();
    return n;
}

void xfree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

WCHAR *xdupn(const WCHAR *s, SIZE_T n)
{
    WCHAR *d = xalloc((n + 1) * sizeof(WCHAR));
    memcpy(d, s, n * sizeof(WCHAR));
    return d;
}

WCHAR *xdup(const WCHAR *s)
{
    return xdupn(s, wcslen(s));
}

/* ------------------------------------------------------------------ growing strings */

void str_init(STR *s)
{
    s->p = nullptr;
    s->len = s->cap = 0;
}

void str_free(STR *s)
{
    xfree(s->p);
    str_init(s);
}

void str_reset(STR *s)
{
    s->len = 0;
    if (s->p)
        s->p[0] = 0;
}

static void str_grow(STR *s, SIZE_T extra)
{
    SIZE_T need = s->len + extra + 1;
    if (need <= s->cap)
        return;
    SIZE_T cap = s->cap ? s->cap : 64;
    while (cap < need)
        cap *= 2;
    s->p = xrealloc(s->p, cap * sizeof(WCHAR));
    s->cap = cap;
}

void str_addc(STR *s, WCHAR c)
{
    str_grow(s, 1);
    s->p[s->len++] = c;
    s->p[s->len] = 0;
}

void str_addn(STR *s, const WCHAR *t, SIZE_T n)
{
    str_grow(s, n);
    memcpy(s->p + s->len, t, n * sizeof(WCHAR));
    s->len += n;
    s->p[s->len] = 0;
}

void str_adds(STR *s, const WCHAR *t)
{
    str_addn(s, t, wcslen(t));
}

void str_fmt(STR *s, const WCHAR *fmt, ...)
{
    WCHAR buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(buffer, NUM(buffer) - 1, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)NUM(buffer) - 1)
        n = NUM(buffer) - 1;
    str_addn(s, buffer, (SIZE_T)n);
}

WCHAR *str_take(STR *s)
{
    WCHAR *p = s->p ? s->p : xdup(L"");
    str_init(s);
    return p;
}

const WCHAR *str_get(STR *s)
{
    if (!s->p)
        str_grow(s, 0), s->p[0] = 0;
    return s->p;
}

BOOL is_space(WCHAR c)
{
    return c == L' ' || (c >= 9 && c <= 13);
}

BOOL is_digit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

WCHAR *skip_space(const WCHAR *p)
{
    while (*p && is_space(*p))
        p++;
    return (WCHAR *)p;
}

WCHAR upcase(WCHAR c)
{
    return towupper(c);
}

BOOL starts_with_i(const WCHAR *s, const WCHAR *prefix)
{
    return !_wcsnicmp(s, prefix, wcslen(prefix));
}

void trim_right(STR *s)
{
    while (s->len && is_space(s->p[s->len - 1]))
        s->p[--s->len] = 0;
}

/* strtol with base detection: 0x hex, a leading 0 octal, the sign, saturating on overflow. */
LONG parse_long(const WCHAR *s, const WCHAR **end)
{
    const WCHAR *p = s;
    while (is_space(*p))
        p++;
    BOOL negative = FALSE;
    if (*p == L'-' || *p == L'+') {
        negative = *p == L'-';
        p++;
    }
    int base = 10;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X') &&
        ((p[2] >= L'0' && p[2] <= L'9') || ((p[2] | 0x20) >= L'a' && (p[2] | 0x20) <= L'f'))) {
        base = 16;
        p += 2;
    } else if (p[0] == L'0') {
        base = 8;
    }
    const WCHAR *digits = p;
    ULONGLONG v = 0;
    BOOL overflow = FALSE;
    for (;; p++) {
        int d;
        if (*p >= L'0' && *p <= L'9')
            d = *p - L'0';
        else if ((*p | 0x20) >= L'a' && (*p | 0x20) <= L'f')
            d = (*p | 0x20) - L'a' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * (ULONGLONG)base + (ULONGLONG)d;
        if (v > 0x7FFFFFFFFFull)
            overflow = TRUE, v = 0x7FFFFFFFFFull;
    }
    if (p == digits) {
        if (end)
            *end = s;
        return 0;
    }
    if (end)
        *end = p;
    if (negative) {
        if (overflow || v > 0x80000000ull)
            return (LONG)0x80000000;
        return (LONG)(0 - (LONG)v);
    }
    if (overflow || v > 0x7FFFFFFFull)
        return 0x7FFFFFFF;
    return (LONG)v;
}

BOOL all_numeric_long(const WCHAR *s, LONG *value)
{
    const WCHAR *end;
    LONG v = parse_long(s, &end);
    if (end == s || *end)
        return FALSE;
    *value = v;
    return TRUE;
}

void strip_quotes_inplace(WCHAR *s)
{
    WCHAR *d = s;
    for (; *s; s++)
        if (*s != L'"')
            *d++ = *s;
    *d = 0;
}

/* ------------------------------------------------------------------ writing */

void io_set(IO *io, int fd, HANDLE h)
{
    DWORD mode;
    io->h[fd] = h;
    io->con[fd] = h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
}

void io_init_std(void)
{
    io_set(&g_io, 0, GetStdHandle(STD_INPUT_HANDLE));
    io_set(&g_io, 1, GetStdHandle(STD_OUTPUT_HANDLE));
    io_set(&g_io, 2, GetStdHandle(STD_ERROR_HANDLE));
    g.out_cp = GetConsoleOutputCP();
    if (!g.out_cp)
        g.out_cp = GetOEMCP();
}

void write_to(HANDLE h, BOOL console, const WCHAR *s, SIZE_T n)
{
    if (!n || !h || h == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    if (console) {
        while (n) {
            DWORD chunk = n > 16384 ? 16384 : (DWORD)n;
            if (!WriteConsoleW(h, s, chunk, &written, nullptr))
                return;
            s += chunk;
            n -= chunk;
        }
        return;
    }
    if (g.unicode_out) {
        WriteFile(h, s, (DWORD)(n * sizeof(WCHAR)), &written, nullptr);
        return;
    }
    char stack[1024];
    while (n) {
        SIZE_T part = n > 256 ? 256 : n;
        /* a surrogate pair must not be split. */
        if (part < n && s[part - 1] >= 0xD800 && s[part - 1] < 0xDC00)
            part--;
        int bytes = WideCharToMultiByte(g.out_cp, 0, s, (int)part, stack, sizeof(stack), nullptr, nullptr);
        if (bytes <= 0) {
            int need = WideCharToMultiByte(g.out_cp, 0, s, (int)part, nullptr, 0, nullptr, nullptr);
            char *big = need > 0 ? xalloc((SIZE_T)need) : nullptr;
            if (big) {
                bytes = WideCharToMultiByte(g.out_cp, 0, s, (int)part, big, need, nullptr, nullptr);
                WriteFile(h, big, (DWORD)bytes, &written, nullptr);
                xfree(big);
            }
        } else {
            WriteFile(h, stack, (DWORD)bytes, &written, nullptr);
        }
        s += part;
        n -= part;
    }
}

void out_n(const WCHAR *s, SIZE_T n)
{
    write_to(g_io.h[1], g_io.con[1], s, n);
}

void out_s(const WCHAR *s)
{
    out_n(s, wcslen(s));
}

static void vformat(int fd, const WCHAR *fmt, va_list ap)
{
    WCHAR stack[1024];
    va_list copy;
    va_copy(copy, ap);
    int n = _vsnwprintf(stack, NUM(stack) - 1, fmt, ap);
    if (n >= 0 && n <= (int)NUM(stack) - 1) {
        write_to(g_io.h[fd], g_io.con[fd], stack, (SIZE_T)n);
    } else {
        SIZE_T cap = 8192;
        for (;;) {
            WCHAR *big = xalloc(cap * sizeof(WCHAR));
            va_list again;
            va_copy(again, copy);
            n = _vsnwprintf(big, cap - 1, fmt, again);
            va_end(again);
            if (n >= 0 && (SIZE_T)n < cap - 1) {
                write_to(g_io.h[fd], g_io.con[fd], big, (SIZE_T)n);
                xfree(big);
                break;
            }
            xfree(big);
            cap *= 4;
            if (cap > (1u << 24))
                break;
        }
    }
    va_end(copy);
}

void out_fmt(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vformat(1, fmt, ap);
    va_end(ap);
}

void err_fmt(const WCHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vformat(2, fmt, ap);
    va_end(ap);
}

void err_s(const WCHAR *s)
{
    write_to(g_io.h[2], g_io.con[2], s, wcslen(s));
}

/* ------------------------------------------------------------------ reading */

static BOOL read_byte(HANDLE h, unsigned char *b)
{
    DWORD got;
    return ReadFile(h, b, 1, &got, nullptr) && got == 1;
}

/*
 * a line from standard input: a console gives a whole edited line, anything else is read a byte at a time so a
 * program started later sees the rest of the input.
 */
BOOL read_stdin_line(STR *line, BOOL prompt_echo)
{
    str_reset(line);
    HANDLE in = g_io.h[0];
    if (!in || in == INVALID_HANDLE_VALUE)
        return FALSE;
    if (g_io.con[0]) {
        WCHAR buffer[LINE_LIMIT + 3];
        DWORD got = 0;
        if (!ReadConsoleW(in, buffer, LINE_LIMIT + 2, &got, nullptr) || got == 0)
            return FALSE;
        while (got && (buffer[got - 1] == L'\n' || buffer[got - 1] == L'\r'))
            got--;
        str_addn(line, buffer, got);
        str_get(line);
        return TRUE;
    }
    char bytes[LINE_LIMIT * 4 + 4];
    SIZE_T n = 0;
    unsigned char b;
    BOOL any = FALSE;
    while (read_byte(in, &b)) {
        any = TRUE;
        if (b == '\n')
            break;
        if (n < sizeof(bytes))
            bytes[n++] = (char)b;
    }
    if (!any)
        return FALSE;
    while (n && bytes[n - 1] == '\r')
        n--;
    if (n) {
        int chars = MultiByteToWideChar(g.in_cp, 0, bytes, (int)n, nullptr, 0);
        WCHAR *w = xalloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
        MultiByteToWideChar(g.in_cp, 0, bytes, (int)n, w, chars);
        str_addn(line, w, (SIZE_T)chars);
        xfree(w);
    }
    str_get(line);
    return TRUE;
}

/* one key press without echo or line editing. */
BOOL read_key(WCHAR *c)
{
    HANDLE in = g_io.h[0];
    if (!in || in == INVALID_HANDLE_VALUE)
        return FALSE;
    if (g_io.con[0]) {
        DWORD mode, got;
        GetConsoleMode(in, &mode);
        SetConsoleMode(in, 0);
        WCHAR ch = 0;
        BOOL ok = ReadConsoleW(in, &ch, 1, &got, nullptr) && got == 1;
        SetConsoleMode(in, mode);
        *c = ch;
        return ok;
    }
    unsigned char b;
    if (!read_byte(in, &b))
        return FALSE;
    *c = b;
    return TRUE;
}

/* ------------------------------------------------------------------ messages */

void print_win_error(DWORD error)
{
    WCHAR text[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text,
                             NUM(text), nullptr);
    while (n && (text[n - 1] == L'\n' || text[n - 1] == L'\r'))
        n--;
    if (!n)
        err_fmt(L"Windows error %lu.\r\n", error);
    else
        err_fmt(L"%.*ls\r\n", (int)n, text);
}

void msg_not_recognized(const WCHAR *name)
{
    err_fmt(L"'%ls' is not recognized as an internal or external command,\r\noperable program or batch file.\r\n",
            name);
}

void msg_syntax(void)
{
    err_s(L"The syntax of the command is incorrect.\r\n");
}

void msg_unexpected(const WCHAR *what)
{
    err_fmt(L"%ls was unexpected at this time.\r\n", what);
}

void msg_file_not_found(void)
{
    err_s(L"The system cannot find the file specified.\r\n");
}

void msg_error(const WCHAR *text)
{
    err_fmt(L"%ls\r\n", text);
}

/* ------------------------------------------------------------------ small helpers */

BOOL has_wildcards(const WCHAR *s)
{
    for (; *s; s++)
        if (*s == L'*' || *s == L'?')
            return TRUE;
    return FALSE;
}

/* names match a pattern with * and ?, ignoring case. a trailing .* also matches a name without an extension. */
BOOL wild_match(const WCHAR *pattern, const WCHAR *name)
{
    const WCHAR *star_p = nullptr, *star_n = nullptr;
    while (*name || *pattern) {
        if (*pattern == L'*') {
            star_p = ++pattern;
            star_n = name;
            continue;
        }
        if (*pattern == L'?' && *name) {
            pattern++;
            name++;
            continue;
        }
        if (*pattern && *name && towupper(*pattern) == towupper(*name)) {
            pattern++;
            name++;
            continue;
        }
        /* "name.*" and "name." also match "name". */
        if (!*name && *pattern == L'.') {
            const WCHAR *q = pattern + 1;
            while (*q == L'*' || *q == L'?')
                q++;
            if (!*q)
                return TRUE;
        }
        if (star_p && *star_n) {
            pattern = star_p;
            name = ++star_n;
            continue;
        }
        return FALSE;
    }
    return TRUE;
}

void path_split_pattern(const WCHAR *spec, STR *dir, STR *pattern)
{
    const WCHAR *last = nullptr;
    for (const WCHAR *p = spec; *p; p++)
        if (*p == L'\\' || *p == L'/' || (*p == L':' && p == spec + 1))
            last = p;
    if (last) {
        str_addn(dir, spec, (SIZE_T)(last - spec) + 1);
        str_adds(pattern, last + 1);
    } else {
        str_adds(pattern, spec);
    }
    str_get(dir);
    str_get(pattern);
}

void format_number_commas(ULONGLONG v, STR *out)
{
    WCHAR digits[32];
    int n = 0;
    do {
        digits[n++] = (WCHAR)(L'0' + (int)(v % 10));
        v /= 10;
    } while (v);
    for (int i = n - 1; i >= 0; i--) {
        str_addc(out, digits[i]);
        if (i && i % 3 == 0)
            str_addc(out, L',');
    }
}

void format_date_time(const FILETIME *ft, STR *out)
{
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, &st))
        return;
    WCHAR date[64], time[64];
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, NUM(date)))
        _snwprintf(date, NUM(date), L"%02u/%02u/%04u", st.wMonth, st.wDay, st.wYear);
    if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, NUM(time)))
        _snwprintf(time, NUM(time), L"%02u:%02u", st.wHour, st.wMinute);
    str_adds(out, date);
    str_addc(out, L' ');
    str_adds(out, time);
}

int compare_names(const WCHAR *a, const WCHAR *b)
{
    int r = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, -1, b, -1);
    return r - 2;
}

/* a plain insertion merge sort, stable. */
static void merge_sort(void **items, void **tmp, SIZE_T lo, SIZE_T hi, int (*cmp)(const void *, const void *))
{
    if (hi - lo < 2)
        return;
    SIZE_T mid = lo + (hi - lo) / 2;
    merge_sort(items, tmp, lo, mid, cmp);
    merge_sort(items, tmp, mid, hi, cmp);
    SIZE_T i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        tmp[k++] = cmp(items[j], items[i]) < 0 ? items[j++] : items[i++];
    while (i < mid)
        tmp[k++] = items[i++];
    while (j < hi)
        tmp[k++] = items[j++];
    memcpy(items + lo, tmp + lo, (hi - lo) * sizeof(void *));
}

void sort_pointers(void **items, SIZE_T count, int (*cmp)(const void *, const void *))
{
    if (count < 2)
        return;
    void **tmp = xalloc(count * sizeof(void *));
    merge_sort(items, tmp, 0, count, cmp);
    xfree(tmp);
}

/* ------------------------------------------------------------------ argument lists */

static BOOL arg_delim(WCHAR c, BOOL equals_delim)
{
    return is_space(c) || c == L',' || c == L';' || (equals_delim && c == L'=');
}

void args_split(const WCHAR *line, ARGS *a, BOOL equals_delim)
{
    a->v = nullptr;
    a->n = 0;
    int cap = 0;
    const WCHAR *p = line;
    for (;;) {
        while (*p && arg_delim(*p, equals_delim))
            p++;
        if (!*p)
            break;
        STR tok;
        str_init(&tok);
        BOOL quoted = FALSE;
        while (*p && (quoted || !arg_delim(*p, equals_delim))) {
            if (*p == L'"')
                quoted = !quoted;
            str_addc(&tok, *p++);
        }
        if (a->n == cap) {
            cap = cap ? cap * 2 : 8;
            a->v = xrealloc(a->v, (SIZE_T)cap * sizeof(WCHAR *));
        }
        a->v[a->n++] = str_take(&tok);
    }
}

void args_free(ARGS *a)
{
    for (int i = 0; i < a->n; i++)
        xfree(a->v[i]);
    xfree(a->v);
    a->v = nullptr;
    a->n = 0;
}

BOOL is_switch(const WCHAR *tok)
{
    return tok[0] == L'/' && tok[1];
}
