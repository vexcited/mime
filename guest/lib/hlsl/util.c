#include "internal.h"

struct HlChunk {
    HlChunk *next;
    size_t size;
};

#define CHUNK_SIZE (256 << 10)

[[noreturn]] void hl_fatal(Hlsl *h)
{
    h->fatal(h->fatal_ctx);
    __builtin_trap();
}

static void *raw_alloc(Hlsl *h, size_t size)
{
    h->allocated += size;
    if (h->allocated > HL_MAX_MEMORY)
        hl_abort(h, "out of memory: the shader is too large to compile");
    void *p = mime_alloc(&h->alloc, size);
    if (!p)
        hl_fatal(h);
    return p;
}

void *hl_alloc(Hlsl *h, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (size > (size_t)(h->end - h->at)) {
        size_t header = (sizeof(HlChunk) + 15) & ~(size_t)15;
        size_t chunk = size + header > CHUNK_SIZE ? size + header : CHUNK_SIZE;
        if (size > ((size_t)1 << 30))
            hl_fatal(h);
        HlChunk *c = raw_alloc(h, chunk);
        c->size = chunk;
        c->next = h->chunks;
        h->chunks = c;
        /* a big block gets a chunk of its own and leaves the current one. */
        if (chunk > CHUNK_SIZE) {
            void *p = (uint8_t *)c + header;
            __builtin_memset(p, 0, size);
            return p;
        }
        h->at = (uint8_t *)c + header;
        h->end = (uint8_t *)c + chunk;
    }
    void *p = h->at;
    h->at += size;
    __builtin_memset(p, 0, size);
    return p;
}

void *hl_realloc(Hlsl *h, void *p, size_t old_size, size_t size)
{
    if (size <= old_size)
        return p;
    void *n = hl_alloc(h, size);
    if (p && old_size)
        __builtin_memcpy(n, p, old_size);
    return n;
}

char *hl_strndup(Hlsl *h, const char *s, size_t n)
{
    char *p = hl_alloc(h, n + 1);
    __builtin_memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static uint32_t hash(const char *s, size_t n)
{
    uint32_t v = 2166136261u;
    for (size_t i = 0; i < n; i++)
        v = (v ^ (uint8_t)s[i]) * 16777619u;
    return v;
}

const char *hl_intern(Hlsl *h, const char *s, size_t n)
{
    if (h->intern_count * 2 >= h->intern_capacity) {
        uint32_t capacity = h->intern_capacity ? h->intern_capacity * 2 : 1024;
        const char **table = hl_alloc(h, capacity * sizeof(*table));
        for (uint32_t i = 0; i < h->intern_capacity; i++) {
            const char *e = h->interned[i];
            if (!e)
                continue;
            uint32_t at = hash(e, __builtin_strlen(e)) & (capacity - 1);
            while (table[at])
                at = (at + 1) & (capacity - 1);
            table[at] = e;
        }
        h->interned = table;
        h->intern_capacity = capacity;
    }
    uint32_t at = hash(s, n) & (h->intern_capacity - 1);
    for (const char *e; (e = h->interned[at]); at = (at + 1) & (h->intern_capacity - 1))
        if (!__builtin_strncmp(e, s, n) && !e[n])
            return e;
    const char *e = hl_strndup(h, s, n);
    h->interned[at] = e;
    h->intern_count++;
    return e;
}

int hl_strnicmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int x = (uint8_t)a[i], y = (uint8_t)b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y || !x)
            return x - y;
    }
    return 0;
}

int hl_stricmp(const char *a, const char *b) { return hl_strnicmp(a, b, (size_t)-1); }

/* ------------------------------------------------------------------ text */

void hl_buf_append(Hlsl *h, HlBuf *b, const char *s, size_t n)
{
    if (b->length + n + 1 > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256;
        while (b->length + n + 1 > capacity)
            capacity *= 2;
        b->data = hl_realloc(h, b->data, b->capacity, capacity);
        b->capacity = capacity;
    }
    __builtin_memcpy(b->data + b->length, s, n);
    b->length += n;
    b->data[b->length] = 0;
}

void hl_buf_puts(Hlsl *h, HlBuf *b, const char *s) { hl_buf_append(h, b, s, __builtin_strlen(s)); }

void hl_buf_putc(Hlsl *h, HlBuf *b, char c) { hl_buf_append(h, b, &c, 1); }

static void pad(Hlsl *h, HlBuf *b, const char *s, size_t n, int width, bool left, char fill)
{
    if (!left)
        for (int i = (int)n; i < width; i++)
            hl_buf_putc(h, b, fill);
    hl_buf_append(h, b, s, n);
    if (left)
        for (int i = (int)n; i < width; i++)
            hl_buf_putc(h, b, ' ');
}

static size_t format_unsigned(char *out, unsigned long long v, unsigned base, bool upper)
{
    char tmp[24];
    size_t n = 0;
    do {
        unsigned d = (unsigned)(v % base);
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= base;
    } while (v);
    for (size_t i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    return n;
}

/* %f with the given decimals, of doubles fxc literals fit in. */
static size_t format_fixed(char *out, double v, int decimals)
{
    size_t n = 0;
    if (v != v)
        return __builtin_memcpy(out, "nan", 3), 3;
    if (v < 0) {
        out[n++] = '-';
        v = -v;
    }
    if (v > 1e19)
        return __builtin_memcpy(out + n, "inf", 3), n + 3;
    double scale = 1;
    for (int i = 0; i < decimals; i++)
        scale *= 10;
    double r = v * scale + 0.5;
    unsigned long long whole = (unsigned long long)v;
    unsigned long long frac = 0;
    if (r < 1.8e19) {
        unsigned long long all = (unsigned long long)r;
        unsigned long long s = (unsigned long long)scale;
        whole = all / s;
        frac = all % s;
    }
    n += format_unsigned(out + n, whole, 10, false);
    if (decimals > 0) {
        out[n++] = '.';
        char digits[24];
        size_t d = format_unsigned(digits, frac, 10, false);
        for (int i = (int)d; i < decimals; i++)
            out[n++] = '0';
        __builtin_memcpy(out + n, digits, d);
        n += d;
    }
    return n;
}

void hl_format_float(char *out, size_t size, float value)
{
    char tmp[64];
    size_t n = 0;
    double v = value;
    if (v != v) {
        __builtin_memcpy(out, "nan", 4);
        return;
    }
    if (v == 0) {
        __builtin_memcpy(out, (__builtin_signbit(value) ? "-0.0" : "0.0"), 5);
        return;
    }
    double a = v < 0 ? -v : v;
    if (a > 3.5e38) {
        __builtin_memcpy(out, v < 0 ? "-inf" : "inf", 5);
        return;
    }
    /* the fewest significant digits that read back the same. */
    for (int digits = 1; digits <= 9; digits++) {
        int exp10 = 0;
        double m = a;
        while (m >= 10)
            m /= 10, exp10++;
        while (m < 1)
            m *= 10, exp10--;
        double scale = 1;
        for (int i = 1; i < digits; i++)
            scale *= 10;
        double rounded = (double)(unsigned long long)(m * scale + 0.5) / scale;
        double back = rounded;
        for (int i = 0; i < exp10; i++)
            back *= 10;
        for (int i = 0; i > exp10; i--)
            back /= 10;
        if ((float)back == (float)a || digits == 9) {
            n = 0;
            if (v < 0)
                tmp[n++] = '-';
            if (exp10 >= -5 && exp10 < 16) {
                int decimals = digits - 1 - exp10;
                n += format_fixed(tmp + n, back, decimals < 1 ? 1 : decimals);
            } else {
                n += format_fixed(tmp + n, rounded, digits > 1 ? digits - 1 : 1);
                tmp[n++] = 'e';
                if (exp10 < 0)
                    tmp[n++] = '-', exp10 = -exp10;
                n += format_unsigned(tmp + n, (unsigned)exp10, 10, false);
            }
            break;
        }
    }
    if (n >= size)
        n = size - 1;
    __builtin_memcpy(out, tmp, n);
    out[n] = 0;
}

void hl_buf_vprintf(Hlsl *h, HlBuf *b, const char *f, va_list ap)
{
    while (*f) {
        if (*f != '%') {
            const char *start = f;
            while (*f && *f != '%')
                f++;
            hl_buf_append(h, b, start, (size_t)(f - start));
            continue;
        }
        f++;
        bool left = false;
        char fill = ' ';
        int width = 0, precision = -1, longs = 0;
        for (;; f++) {
            if (*f == '-')
                left = true;
            else if (*f == '0')
                fill = '0';
            else
                break;
        }
        if (*f == '*')
            width = va_arg(ap, int), f++;
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++;
            precision = 0;
            if (*f == '*')
                precision = va_arg(ap, int), f++;
            while (*f >= '0' && *f <= '9')
                precision = precision * 10 + (*f++ - '0');
        }
        while (*f == 'l')
            longs++, f++;
        if (*f == 'z')
            longs = 2, f++;
        char tmp[64];
        size_t n = 0;
        switch (*f) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            size_t len = __builtin_strlen(s);
            if (precision >= 0 && (size_t)precision < len)
                len = (size_t)precision;
            pad(h, b, s, len, width, left, ' ');
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            pad(h, b, tmp, 1, width, left, ' ');
            break;
        case 'd': {
            long long v = longs >= 2 ? va_arg(ap, long long) : longs ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = (unsigned long long)v;
            if (v < 0)
                tmp[n++] = '-', u = 0 - u;
            n += format_unsigned(tmp + n, u, 10, false);
            pad(h, b, tmp, n, width, left, fill);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = longs >= 2 ? va_arg(ap, unsigned long long)
                                   : longs   ? va_arg(ap, unsigned long)
                                             : va_arg(ap, unsigned);
            n = format_unsigned(tmp, v, *f == 'u' ? 10 : 16, *f == 'X');
            pad(h, b, tmp, n, width, left, fill);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            n = format_unsigned(tmp, v, 16, true);
            int digits = sizeof(void *) * 2;
            for (int i = (int)n; i < digits; i++)
                hl_buf_putc(h, b, '0');
            hl_buf_append(h, b, tmp, n);
            break;
        }
        case 'f':
            n = format_fixed(tmp, va_arg(ap, double), precision < 0 ? 6 : precision);
            pad(h, b, tmp, n, width, left, fill);
            break;
        case 'g':
            hl_format_float(tmp, sizeof(tmp), (float)va_arg(ap, double));
            pad(h, b, tmp, __builtin_strlen(tmp), width, left, ' ');
            break;
        case '%':
            hl_buf_putc(h, b, '%');
            break;
        default:
            return;
        }
        f++;
    }
}

void hl_buf_printf(Hlsl *h, HlBuf *b, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, b, format, ap);
    va_end(ap);
}

char *hl_sprintf(Hlsl *h, const char *format, ...)
{
    HlBuf b = {};
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, &b, format, ap);
    va_end(ap);
    return b.data ? b.data : hl_strndup(h, "", 0);
}

/* ------------------------------------------------------------------ messages */

const char *hl_file_name(Hlsl *h, uint32_t file)
{
    if (file < h->file_count && h->files[file].name)
        return h->files[file].name;
    return h->source_name;
}

static void location(Hlsl *h, HlLoc loc)
{
    hl_buf_printf(h, &h->messages, "%s(%u,%u): ", hl_file_name(h, loc.file), loc.line ? loc.line : 1,
                  loc.col ? loc.col : 1);
}

void hl_error(Hlsl *h, HlLoc loc, uint32_t code, const char *format, ...)
{
    /* fxc stops at some point; so does a runaway source. */
    if (h->errors >= HL_MAX_ERRORS)
        return;
    h->errors++;
    if (h->errors == HL_MAX_ERRORS) {
        location(h, loc);
        hl_buf_puts(h, &h->messages, "error X3000: too many errors, compilation aborted\n");
        return;
    }
    location(h, loc);
    hl_buf_printf(h, &h->messages, "error X%u: ", code);
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, &h->messages, format, ap);
    va_end(ap);
    hl_buf_putc(h, &h->messages, '\n');
}

void hl_set_warning(Hlsl *h, uint32_t code, uint8_t mode)
{
    for (uint32_t i = 0; i < h->warning_count; i++)
        if (h->warnings[i].code == code) {
            h->warnings[i].mode = mode;
            return;
        }
    HL_PUSH(h, h->warnings, h->warning_count, h->warning_capacity, ((HlWarningState){code, mode}));
}

void hl_warning(Hlsl *h, HlLoc loc, uint32_t code, const char *format, ...)
{
    bool as_error = h->warnings_are_errors;
    for (uint32_t i = 0; i < h->warning_count; i++) {
        if (h->warnings[i].code != code)
            continue;
        if (h->warnings[i].mode == 1)
            return;
        if (h->warnings[i].mode == 2)
            as_error = true;
    }
    if (as_error)
        h->errors++;
    location(h, loc);
    hl_buf_printf(h, &h->messages, "%s X%u: ", as_error ? "error" : "warning", code);
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, &h->messages, format, ap);
    va_end(ap);
    hl_buf_putc(h, &h->messages, '\n');
}

void hl_note(Hlsl *h, HlLoc loc, const char *format, ...)
{
    location(h, loc);
    va_list ap;
    va_start(ap, format);
    hl_buf_vprintf(h, &h->messages, format, ap);
    va_end(ap);
    hl_buf_putc(h, &h->messages, '\n');
}

/* ------------------------------------------------------------------ the context */

[[noreturn]] void hl_abort(Hlsl *h, const char *what)
{
    /* the messages may not grow past the cap either. */
    h->allocated = 0;
    h->errors++;
    hl_buf_printf(h, &h->messages, "%s(1,1): error X3000: %s\n", h->source_name ? h->source_name : "", what);
    hl_fatal(h);
}

Hlsl *hlsl_new(const MimeAllocator *alloc, void (*fatal)(void *ctx), void *fatal_ctx)
{
    Hlsl *h = mime_alloc(alloc, sizeof(Hlsl));
    if (!h)
        return nullptr;
    __builtin_memset(h, 0, sizeof(*h));
    h->alloc = *alloc;
    h->fatal = fatal;
    h->fatal_ctx = fatal_ctx;
    return h;
}

void hlsl_delete(Hlsl *h)
{
    if (!h)
        return;
    for (HlChunk *c = h->chunks, *next; c; c = next) {
        next = c->next;
        mime_free(&h->alloc, c);
    }
    mime_free(&h->alloc, h);
}

const void *hlsl_output(Hlsl *h, size_t *size)
{
    *size = h->output.length;
    return h->output.data;
}

const char *hlsl_messages(Hlsl *h, size_t *size)
{
    *size = h->messages.length;
    return h->messages.length ? h->messages.data : nullptr;
}
