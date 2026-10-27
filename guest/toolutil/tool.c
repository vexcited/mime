#include "tool.h"

int tu_argc;
WCHAR **tu_argv;
const WCHAR *tu_tail;

/* ------------------------------------------------------------------ memory */

void *tu_alloc(SIZE_T bytes)
{
    void *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
    if (!memory) {
        tu_eprint(L"ERROR: Not enough memory is available.\n");
        tu_exit(1);
    }
    return memory;
}

void *tu_realloc(void *memory, SIZE_T bytes)
{
    if (!memory)
        return tu_alloc(bytes);
    void *grown = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, memory, bytes ? bytes : 1);
    if (!grown) {
        tu_eprint(L"ERROR: Not enough memory is available.\n");
        tu_exit(1);
    }
    return grown;
}

void tu_free(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

WCHAR *tu_dup_n(const WCHAR *text, SIZE_T count)
{
    WCHAR *copy = tu_alloc((count + 1) * sizeof(WCHAR));
    memcpy(copy, text, count * sizeof(WCHAR));
    return copy;
}

WCHAR *tu_dup(const WCHAR *text)
{
    return tu_dup_n(text, wcslen(text));
}

/* ------------------------------------------------------------------ growing text */

void tu_str_reserve(TU_STR *s, SIZE_T extra)
{
    SIZE_T need = s->length + extra + 1;
    if (need <= s->capacity)
        return;
    SIZE_T capacity = s->capacity ? s->capacity : 64;
    while (capacity < need)
        capacity *= 2;
    s->data = tu_realloc(s->data, capacity * sizeof(WCHAR));
    s->capacity = capacity;
}

void tu_str_append(TU_STR *s, const WCHAR *text, SIZE_T count)
{
    tu_str_reserve(s, count);
    memcpy(s->data + s->length, text, count * sizeof(WCHAR));
    s->length += count;
    s->data[s->length] = 0;
}

void tu_str_puts(TU_STR *s, const WCHAR *text)
{
    tu_str_append(s, text, wcslen(text));
}

void tu_str_putc(TU_STR *s, WCHAR c)
{
    tu_str_reserve(s, 1);
    s->data[s->length++] = c;
    s->data[s->length] = 0;
}

const WCHAR *tu_str_text(TU_STR *s)
{
    tu_str_reserve(s, 0);
    s->data[s->length] = 0;
    return s->data;
}

void tu_str_free(TU_STR *s)
{
    tu_free(s->data);
    s->data = nullptr;
    s->length = s->capacity = 0;
}

static void pad(TU_STR *s, WCHAR c, int count)
{
    for (; count > 0; count--)
        tu_str_putc(s, c);
}

/* one number or string with its width and precision. */
static void emit(TU_STR *s, const WCHAR *prefix, SIZE_T prefix_length, const WCHAR *body, SIZE_T body_length, int width,
                 BOOL left, BOOL zeros)
{
    int total = (int)(prefix_length + body_length);
    if (!left && !zeros)
        pad(s, L' ', width - total);
    tu_str_append(s, prefix, prefix_length);
    if (!left && zeros)
        pad(s, L'0', width - total);
    tu_str_append(s, body, body_length);
    if (left)
        pad(s, L' ', width - total);
}

void tu_str_vformat(TU_STR *s, const WCHAR *f, va_list ap)
{
    for (; *f; f++) {
        if (*f != L'%') {
            tu_str_putc(s, *f);
            continue;
        }
        f++;
        BOOL left = FALSE, plus = FALSE, space = FALSE, zeros = FALSE;
        for (;; f++) {
            if (*f == L'-')
                left = TRUE;
            else if (*f == L'+')
                plus = TRUE;
            else if (*f == L' ')
                space = TRUE;
            else if (*f == L'0')
                zeros = TRUE;
            else
                break;
        }
        int width = 0, precision = -1;
        if (*f == L'*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = TRUE;
                width = -width;
            }
            f++;
        } else {
            while (*f >= L'0' && *f <= L'9')
                width = width * 10 + (*f++ - L'0');
        }
        if (*f == L'.') {
            f++;
            precision = 0;
            if (*f == L'*') {
                precision = va_arg(ap, int);
                f++;
            } else {
                while (*f >= L'0' && *f <= L'9')
                    precision = precision * 10 + (*f++ - L'0');
            }
        }
        int size = 0; /* 1 is 64 bits, 2 is a short. */
        if (*f == L'l' && f[1] == L'l') {
            size = 1;
            f += 2;
        } else if (*f == L'I' && f[1] == L'6' && f[2] == L'4') {
            size = 1;
            f += 3;
        } else if (*f == L'z' || *f == L'I' || *f == L'j' || *f == L't') {
            size = sizeof(void *) == 8 ? 1 : 0;
            f++;
        } else if (*f == L'l') {
            f++;
        } else if (*f == L'h') {
            size = 2;
            f++;
        }
        WCHAR digits[72];
        switch (*f) {
        case L'd':
        case L'i':
        case L'u':
        case L'x':
        case L'X':
        case L'p': {
            BOOL is_signed = *f == L'd' || *f == L'i';
            UINT64 value;
            BOOL negative = FALSE;
            if (*f == L'p')
                size = sizeof(void *) == 8 ? 1 : 0;
            if (is_signed) {
                INT64 v = size == 1 ? va_arg(ap, INT64) : va_arg(ap, int);
                negative = v < 0;
                value = negative ? 0 - (UINT64)v : (UINT64)v;
            } else if (size == 1) {
                value = va_arg(ap, UINT64);
            } else {
                value = va_arg(ap, unsigned int);
            }
            BOOL hex = *f == L'x' || *f == L'X' || *f == L'p';
            const char *alphabet = *f == L'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            int n = 0;
            WCHAR reversed[72];
            if (hex) {
                do {
                    reversed[n++] = alphabet[value & 15];
                    value >>= 4;
                } while (value);
            } else {
                do {
                    reversed[n++] = (WCHAR)(L'0' + (DWORD)(value % 10));
                    value /= 10;
                } while (value);
            }
            while (n < precision && n < 70)
                reversed[n++] = L'0';
            for (int i = 0; i < n; i++)
                digits[i] = reversed[n - 1 - i];
            WCHAR sign[2];
            SIZE_T sign_length = 0;
            if (negative)
                sign[sign_length++] = L'-';
            else if (is_signed && plus)
                sign[sign_length++] = L'+';
            else if (is_signed && space)
                sign[sign_length++] = L' ';
            emit(s, sign, sign_length, digits, n, width, left, zeros && !left && precision < 0);
            break;
        }
        case L'c': {
            WCHAR c = (WCHAR)va_arg(ap, int);
            emit(s, L"", 0, &c, 1, width, left, FALSE);
            break;
        }
        case L's':
        case L'S': {
            const void *argument = va_arg(ap, const void *);
            BOOL narrow = *f == L'S' || size == 2;
            if (!argument)
                argument = narrow ? (const void *)"(null)" : (const void *)L"(null)";
            if (narrow) {
                const char *text = argument;
                SIZE_T n = 0;
                while (text[n] && (precision < 0 || (int)n < precision))
                    n++;
                WCHAR *wide = tu_alloc((n + 1) * sizeof(WCHAR));
                int converted = n ? MultiByteToWideChar(CP_ACP, 0, text, (int)n, wide, (int)n) : 0;
                emit(s, L"", 0, wide, converted, width, left, FALSE);
                tu_free(wide);
            } else {
                const WCHAR *text = argument;
                SIZE_T n = 0;
                while (text[n] && (precision < 0 || (int)n < precision))
                    n++;
                emit(s, L"", 0, text, n, width, left, FALSE);
            }
            break;
        }
        case L'%':
            tu_str_putc(s, L'%');
            break;
        default:
            if (!*f)
                return;
            tu_str_putc(s, *f);
            break;
        }
    }
}

void tu_str_format(TU_STR *s, const WCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    tu_str_vformat(s, format, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ output */

static TU_STR g_out[2];
static HANDLE g_handle[2];
static int g_console[2] = {-1, -1};
static UINT g_code_page;

static HANDLE out_handle(int stream)
{
    if (!g_handle[stream])
        g_handle[stream] = GetStdHandle(stream == TU_STDOUT ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    return g_handle[stream];
}

BOOL tu_is_console(int stream)
{
    if (g_console[stream] < 0) {
        DWORD mode;
        HANDLE h = out_handle(stream);
        g_console[stream] = h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
    }
    return g_console[stream];
}

/* bytes already in the encoding of the output wait here, in order with the text. */
static BYTE *g_bytes;
static SIZE_T g_bytes_length;
static SIZE_T g_bytes_capacity;

static void flush_bytes(void)
{
    HANDLE h = out_handle(TU_STDOUT);
    SIZE_T done = 0;
    while (done < g_bytes_length && h && h != INVALID_HANDLE_VALUE) {
        DWORD written;
        if (!WriteFile(h, g_bytes + done, (DWORD)(g_bytes_length - done), &written, nullptr) || !written)
            break;
        done += written;
    }
    g_bytes_length = 0;
}

static void flush_stream(int stream)
{
    if (stream == TU_STDOUT && g_bytes_length)
        flush_bytes();
    TU_STR *out = &g_out[stream];
    if (!out->length)
        return;
    HANDLE h = out_handle(stream);
    if (h && h != INVALID_HANDLE_VALUE) {
        if (tu_is_console(stream)) {
            SIZE_T done = 0;
            while (done < out->length) {
                DWORD chunk = (DWORD)(out->length - done > 16384 ? 16384 : out->length - done), written;
                if (!WriteConsoleW(h, out->data + done, chunk, &written, nullptr) || !written)
                    break;
                done += written;
            }
        } else {
            if (!g_code_page)
                g_code_page = GetOEMCP();
            int bytes = WideCharToMultiByte(g_code_page, 0, out->data, (int)out->length, nullptr, 0, nullptr, nullptr);
            char *narrow = bytes > 0 ? tu_alloc(bytes) : nullptr;
            if (narrow) {
                WideCharToMultiByte(g_code_page, 0, out->data, (int)out->length, narrow, bytes, nullptr, nullptr);
                int done = 0;
                while (done < bytes) {
                    DWORD written;
                    if (!WriteFile(h, narrow + done, bytes - done, &written, nullptr) || !written)
                        break;
                    done += written;
                }
                tu_free(narrow);
            }
        }
    }
    out->length = 0;
}

void tu_write_bytes(const BYTE *data, SIZE_T count)
{
    if (tu_is_console(TU_STDOUT)) {
        int n = MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)data, (int)count, nullptr, 0);
        WCHAR *wide = tu_alloc((n + 1) * sizeof(WCHAR));
        n = n ? MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)data, (int)count, wide, n) : 0;
        tu_write(TU_STDOUT, wide, n);
        tu_free(wide);
        return;
    }
    if (g_out[TU_STDOUT].length)
        flush_stream(TU_STDOUT);
    if (g_bytes_length + count > g_bytes_capacity) {
        g_bytes_capacity = (g_bytes_length + count) * 2 + 4096;
        g_bytes = tu_realloc(g_bytes, g_bytes_capacity);
    }
    memcpy(g_bytes + g_bytes_length, data, count);
    g_bytes_length += count;
    if (g_bytes_length >= 65536)
        flush_bytes();
}

void tu_flush(void)
{
    flush_stream(TU_STDOUT);
    flush_stream(TU_STDERR);
}

void tu_write(int stream, const WCHAR *text, SIZE_T count)
{
    if (stream == TU_STDERR)
        flush_stream(TU_STDOUT);
    TU_STR *out = &g_out[stream];
    /* a line feed alone becomes a carriage return and a line feed, as the messages of windows' tools have it. */
    tu_str_reserve(out, count * 2);
    for (SIZE_T i = 0; i < count; i++) {
        if (text[i] == L'\n' && !(i ? text[i - 1] == L'\r' : out->length && out->data[out->length - 1] == L'\r'))
            out->data[out->length++] = L'\r';
        out->data[out->length++] = text[i];
    }
    out->data[out->length] = 0;
    if (stream == TU_STDERR || out->length >= 16384)
        flush_stream(stream);
}

void tu_puts(const WCHAR *text)
{
    tu_write(TU_STDOUT, text, wcslen(text));
}

static void print_to(int stream, const WCHAR *format, va_list ap)
{
    TU_STR text = {};
    tu_str_vformat(&text, format, ap);
    tu_write(stream, text.data ? text.data : L"", text.length);
    tu_str_free(&text);
}

void tu_print(const WCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    print_to(TU_STDOUT, format, ap);
    va_end(ap);
}

void tu_eprint(const WCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    print_to(TU_STDERR, format, ap);
    va_end(ap);
}

int tu_console_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (tu_is_console(TU_STDOUT) && GetConsoleScreenBufferInfo(out_handle(TU_STDOUT), &info))
        return info.dwSize.X;
    return 80;
}

void tu_error_text(DWORD error, TU_STR *out)
{
    WCHAR text[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text,
                             TU_ARRAY(text), nullptr);
    while (n && (text[n - 1] == L'\r' || text[n - 1] == L'\n' || text[n - 1] == L' '))
        n--;
    if (!n) {
        tu_str_format(out, L"Unknown error (0x%x)", error);
        return;
    }
    tu_str_append(out, text, n);
}

DECLSPEC_NORETURN void tu_exit(int code)
{
    tu_flush();
    ExitProcess((UINT)code);
    for (;;)
        ;
}

/* ------------------------------------------------------------------ input */

void tu_reader_init(TU_READER *r, HANDLE handle)
{
    memset(r, 0, sizeof(*r));
    r->handle = handle;
    r->first = TRUE;
}

void tu_reader_free(TU_READER *r)
{
    tu_free(r->data);
    r->data = nullptr;
}

BOOL tu_reader_line(TU_READER *r, const BYTE **line, SIZE_T *length)
{
    for (;;) {
        for (SIZE_T i = r->start; i < r->size; i++) {
            if (r->data[i] == '\n') {
                SIZE_T n = i - r->start;
                if (n && r->data[i - 1] == '\r')
                    n--;
                *line = r->data + r->start;
                *length = n;
                r->start = i + 1;
                return TRUE;
            }
        }
        if (r->end) {
            if (r->start >= r->size)
                return FALSE;
            SIZE_T n = r->size - r->start;
            if (n && r->data[r->size - 1] == '\r')
                n--;
            *line = r->data + r->start;
            *length = n;
            r->start = r->size;
            return TRUE;
        }
        /* drop what was handed out and read more behind the partial line. */
        if (r->start) {
            r->base += r->start;
            memmove(r->data, r->data + r->start, r->size - r->start);
            r->size -= r->start;
            r->start = 0;
        }
        if (r->size + 65536 > r->capacity) {
            r->capacity = r->capacity ? r->capacity * 2 : 131072;
            r->data = tu_realloc(r->data, r->capacity);
        }
        DWORD got = 0;
        if (!ReadFile(r->handle, r->data + r->size, (DWORD)(r->capacity - r->size), &got, nullptr) || !got) {
            r->end = TRUE;
            continue;
        }
        r->size += got;
    }
}

/* ------------------------------------------------------------------ strings */

WCHAR tu_upper(WCHAR c)
{
    return RtlUpcaseUnicodeChar(c);
}

WCHAR tu_lower(WCHAR c)
{
    return RtlDowncaseUnicodeChar(c);
}

BOOL tu_is_space(WCHAR c)
{
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f';
}

BOOL tu_is_digit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

BOOL tu_eq(const WCHAR *a, const WCHAR *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

BOOL tu_eq_n(const WCHAR *a, const WCHAR *b, SIZE_T count)
{
    return CompareStringOrdinal(a, (int)count, b, (int)count, TRUE) == CSTR_EQUAL;
}

BOOL tu_prefix(const WCHAR *a, const WCHAR *b, const WCHAR **rest)
{
    SIZE_T n = wcslen(b);
    if (wcsnlen(a, n) < n || !tu_eq_n(a, b, n))
        return FALSE;
    if (rest)
        *rest = a + n;
    return TRUE;
}

BOOL tu_parse_u32(const WCHAR *text, DWORD *value)
{
    if (!*text)
        return FALSE;
    UINT64 v = 0;
    for (; *text; text++) {
        if (!tu_is_digit(*text))
            return FALSE;
        v = v * 10 + (*text - L'0');
        if (v > 0xFFFFFFFFull)
            return FALSE;
    }
    *value = (DWORD)v;
    return TRUE;
}

BOOL tu_parse_int(const WCHAR *text, LONG *value)
{
    BOOL negative = *text == L'-';
    DWORD v;
    if (*text == L'-' || *text == L'+')
        text++;
    if (!tu_parse_u32(text, &v))
        return FALSE;
    if (v > (negative ? 0x80000000u : 0x7FFFFFFFu))
        return FALSE;
    *value = negative ? (LONG)(0 - v) : (LONG)v;
    return TRUE;
}

BOOL tu_parse_number(const WCHAR *text, DWORD *value)
{
    if (text[0] == L'0' && (text[1] | 0x20) == L'x' && text[2]) {
        UINT64 v = 0;
        for (text += 2; *text; text++) {
            int d;
            if (tu_is_digit(*text))
                d = *text - L'0';
            else if ((*text | 0x20) >= L'a' && (*text | 0x20) <= L'f')
                d = (*text | 0x20) - L'a' + 10;
            else
                return FALSE;
            v = v * 16 + d;
            if (v > 0xFFFFFFFFull)
                return FALSE;
        }
        *value = (DWORD)v;
        return TRUE;
    }
    return tu_parse_u32(text, value);
}

BOOL tu_wild(const WCHAR *pattern, const WCHAR *text)
{
    const WCHAR *star = nullptr, *retry = nullptr;
    while (*text) {
        if (*pattern == L'*') {
            star = pattern++;
            retry = text;
        } else if (*pattern == L'?' || (*pattern && tu_upper(*pattern) == tu_upper(*text))) {
            pattern++;
            text++;
        } else if (star) {
            pattern = star + 1;
            text = ++retry;
        } else {
            return FALSE;
        }
    }
    while (*pattern == L'*')
        pattern++;
    return !*pattern;
}

WCHAR *tu_getenv(const WCHAR *name)
{
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size)
        return nullptr;
    WCHAR *value = tu_alloc(size * sizeof(WCHAR));
    DWORD n = GetEnvironmentVariableW(name, value, size);
    if (!n || n >= size) {
        tu_free(value);
        return nullptr;
    }
    return value;
}

/* ------------------------------------------------------------------ the command line */

/* the words the way the runtime cuts them: backslashes count only before a quote. */
static void split(const WCHAR *line)
{
    SIZE_T length = wcslen(line);
    WCHAR *copy = tu_alloc((length + 1) * sizeof(WCHAR));
    WCHAR **words = tu_alloc((length / 2 + 2) * sizeof(WCHAR *));
    int count = 0;
    WCHAR *out = copy;
    const WCHAR *p = line;
    if (*p == L'"') {
        words[count++] = out;
        for (p++; *p && *p != L'"'; p++)
            *out++ = *p;
        if (*p)
            p++;
    } else {
        words[count++] = out;
        for (; *p && *p != L' ' && *p != L'\t'; p++)
            *out++ = *p;
    }
    *out++ = 0;
    while (*p == L' ' || *p == L'\t')
        p++;
    tu_tail = line + (p - line);
    for (;;) {
        while (*p == L' ' || *p == L'\t')
            p++;
        if (!*p)
            break;
        words[count++] = out;
        BOOL quoted = FALSE;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            int slashes = 0;
            while (*p == L'\\') {
                slashes++;
                p++;
            }
            if (*p == L'"') {
                for (int i = 0; i < slashes / 2; i++)
                    *out++ = L'\\';
                if (slashes & 1) {
                    *out++ = L'"';
                } else {
                    quoted = !quoted;
                }
                p++;
            } else {
                for (int i = 0; i < slashes; i++)
                    *out++ = L'\\';
                if (*p)
                    *out++ = *p++;
            }
        }
        *out++ = 0;
    }
    words[count] = nullptr;
    tu_argc = count;
    tu_argv = words;
}

void entry(void)
{
    split(GetCommandLineW());
    tu_exit(tool_main());
}
