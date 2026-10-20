/* memory, growing strings, interned strings, character classes and the numbers of xpath. */
#include <xmldom/xmldom.h>

int _fltused = 0x9875;

/* ------------------------------------------------------------------ memory */

void *xd_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *xd_alloc_raw(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *xd_realloc(void *p, SIZE_T size)
{
    if (!p)
        return xd_alloc_raw(size);
    return HeapReAlloc(GetProcessHeap(), 0, p, size ? size : 1);
}

void xd_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

UINT xd_wlen(const WCHAR *s)
{
    UINT n = 0;
    while (s[n])
        n++;
    return n;
}

WCHAR *xd_wcsdup(const WCHAR *s, UINT length)
{
    if (length == (UINT)-1)
        length = s ? xd_wlen(s) : 0;
    WCHAR *copy = xd_alloc_raw(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!copy)
        return nullptr;
    if (length)
        memcpy(copy, s, (SIZE_T)length * sizeof(WCHAR));
    copy[length] = 0;
    return copy;
}

BSTR xd_bstr(const WCHAR *s, UINT length)
{
    if (!s)
        return nullptr;
    return SysAllocStringLen(s, length == (UINT)-1 ? xd_wlen(s) : length);
}

/* ------------------------------------------------------------------ growing strings */

void xb_free(XBuf *b)
{
    xd_free(b->s);
    b->s = nullptr;
    b->length = b->cap = 0;
}

void xb_reserve(XBuf *b, UINT more)
{
    if (b->failed)
        return;
    UINT need = b->length + more + 1;
    if (need <= b->cap)
        return;
    UINT cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap += cap / 2 + 16;
    WCHAR *s = xd_realloc(b->s, (SIZE_T)cap * sizeof(WCHAR));
    if (!s) {
        b->failed = TRUE;
        return;
    }
    b->s = s;
    b->cap = cap;
}

void xb_put(XBuf *b, const WCHAR *s, UINT length)
{
    xb_reserve(b, length);
    if (b->failed)
        return;
    if (length)
        memcpy(b->s + b->length, s, (SIZE_T)length * sizeof(WCHAR));
    b->length += length;
    b->s[b->length] = 0;
}

void xb_putc(XBuf *b, WCHAR c)
{
    xb_reserve(b, 1);
    if (b->failed)
        return;
    b->s[b->length++] = c;
    b->s[b->length] = 0;
}

void xb_puta(XBuf *b, const char *ascii)
{
    UINT n = 0;
    while (ascii[n])
        n++;
    xb_reserve(b, n);
    if (b->failed)
        return;
    for (UINT i = 0; i < n; i++)
        b->s[b->length + i] = (UCHAR)ascii[i];
    b->length += n;
    b->s[b->length] = 0;
}

/* ------------------------------------------------------------------ interned strings */

struct XAtom {
    XAtom *next;
    UINT hash;
    UINT length;
    WCHAR s[1];
};

typedef struct XBlock {
    struct XBlock *next;
} XBlock;

#define ARENA_BLOCK 0x10000

void *xd_arena(XDoc *doc, SIZE_T size)
{
    size = (size + 7) & ~(SIZE_T)7;
    if (size > ARENA_BLOCK / 4) {
        XBlock *block = xd_alloc_raw(sizeof(XBlock) + size);
        if (!block)
            return nullptr;
        block->next = doc->arena;
        doc->arena = block;
        return (UINT8 *)(block + 1);
    }
    if ((SIZE_T)(doc->arena_end - doc->arena_cur) < size) {
        XBlock *block = xd_alloc_raw(sizeof(XBlock) + ARENA_BLOCK);
        if (!block)
            return nullptr;
        block->next = doc->arena;
        doc->arena = block;
        doc->arena_cur = (UINT8 *)(block + 1);
        doc->arena_end = doc->arena_cur + ARENA_BLOCK;
    }
    void *p = doc->arena_cur;
    doc->arena_cur += size;
    return p;
}

void xd_atoms_free(XDoc *doc)
{
    xd_free(doc->atoms);
    doc->atoms = nullptr;
    doc->atom_slots = doc->atom_count = 0;
    XBlock *block = doc->arena;
    while (block) {
        XBlock *next = block->next;
        xd_free(block);
        block = next;
    }
    doc->arena = nullptr;
    doc->arena_cur = doc->arena_end = nullptr;
}

static UINT hash_chars(const WCHAR *s, UINT length)
{
    UINT h = 2166136261u;
    for (UINT i = 0; i < length; i++)
        h = (h ^ s[i]) * 16777619u;
    return h;
}

static BOOL atoms_grow(XDoc *doc)
{
    UINT slots = doc->atom_slots ? doc->atom_slots * 2 : 256;
    XAtom **table = xd_alloc((SIZE_T)slots * sizeof(XAtom *));
    if (!table)
        return FALSE;
    for (UINT i = 0; i < doc->atom_slots; i++) {
        XAtom *a = doc->atoms[i];
        while (a) {
            XAtom *next = a->next;
            UINT slot = a->hash & (slots - 1);
            a->next = table[slot];
            table[slot] = a;
            a = next;
        }
    }
    xd_free(doc->atoms);
    doc->atoms = table;
    doc->atom_slots = slots;
    return TRUE;
}

const WCHAR *xd_intern(XDoc *doc, const WCHAR *s, UINT length)
{
    if (length == (UINT)-1)
        length = xd_wlen(s);
    UINT hash = hash_chars(s, length);
    if (doc->atom_slots) {
        for (XAtom *a = doc->atoms[hash & (doc->atom_slots - 1)]; a; a = a->next)
            if (a->hash == hash && a->length == length && !memcmp(a->s, s, (SIZE_T)length * sizeof(WCHAR)))
                return a->s;
    }
    if (doc->atom_count >= doc->atom_slots * 3 / 4 && !atoms_grow(doc))
        return nullptr;
    XAtom *a = xd_arena(doc, offsetof(XAtom, s) + ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!a)
        return nullptr;
    a->hash = hash;
    a->length = length;
    memcpy(a->s, s, (SIZE_T)length * sizeof(WCHAR));
    a->s[length] = 0;
    UINT slot = hash & (doc->atom_slots - 1);
    a->next = doc->atoms[slot];
    doc->atoms[slot] = a;
    doc->atom_count++;
    return a->s;
}

UINT xd_atom_len(const WCHAR *atom)
{
    return ((XAtom *)((char *)atom - offsetof(XAtom, s)))->length;
}

const WCHAR *xd_find_atom(XDoc *doc, const WCHAR *s, UINT length)
{
    if (!doc->atom_slots)
        return nullptr;
    UINT hash = hash_chars(s, length);
    for (XAtom *a = doc->atoms[hash & (doc->atom_slots - 1)]; a; a = a->next)
        if (a->hash == hash && a->length == length && !memcmp(a->s, s, (SIZE_T)length * sizeof(WCHAR)))
            return a->s;
    return nullptr;
}

/* ------------------------------------------------------------------ characters */

BOOL xd_is_space(WCHAR c)
{
    return c == 0x20 || c == 0x9 || c == 0xA || c == 0xD;
}

BOOL xd_is_xml_char(UINT c)
{
    return c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) ||
           (c >= 0x10000 && c <= 0x10FFFF);
}

BOOL xd_is_name_start(UINT c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
    return (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) ||
           (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) || (c >= 0x200C && c <= 0x200D) ||
           (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
           (c >= 0xD800 && c <= 0xDFFF) || (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) ||
           (c >= 0x10000 && c <= 0xEFFFF);
}

BOOL xd_is_name_char(UINT c)
{
    if (xd_is_name_start(c))
        return TRUE;
    return c == '-' || c == '.' || (c >= '0' && c <= '9') || c == 0xB7 || (c >= 0x300 && c <= 0x36F) ||
           (c >= 0x203F && c <= 0x2040);
}

int xd_check_name(const WCHAR *s, UINT length, BOOL allow_colon)
{
    if (!length)
        return 0;
    UINT colons = 0;
    for (UINT i = 0; i < length; i++) {
        UINT c = s[i];
        if (c == ':') {
            if (!allow_colon || i == 0 || i + 1 == length || ++colons > 1)
                return (int)i;
            continue;
        }
        if (i == 0 ? !xd_is_name_start(c) : !xd_is_name_char(c))
            return (int)i;
    }
    return -1;
}

int xd_wcsicmp(const WCHAR *a, const WCHAR *b)
{
    for (;; a++, b++) {
        WCHAR x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

/* ------------------------------------------------------------------ numbers */

double xd_nan(void)
{
    union {
        UINT64 bits;
        double value;
    } u = {0x7FF8000000000000ull};
    return u.value;
}

BOOL xd_isnan(double value)
{
    return value != value;
}

static double pow10_int(int e)
{
    double r = 1, base = 10;
    UINT n = e < 0 ? (UINT)-e : (UINT)e;
    while (n) {
        if (n & 1)
            r *= base;
        base *= base;
        n >>= 1;
    }
    return e < 0 ? 1 / r : r;
}

void xd_format_number(double value, XBuf *out)
{
    if (xd_isnan(value)) {
        xb_puta(out, "NaN");
        return;
    }
    if (value == 0) {
        xb_putc(out, '0');
        return;
    }
    if (value > 1.7976931348623157e308) {
        xb_puta(out, "Infinity");
        return;
    }
    if (value < -1.7976931348623157e308) {
        xb_puta(out, "-Infinity");
        return;
    }
    if (value < 0) {
        xb_putc(out, '-');
        value = -value;
    }
    /* integers below 2^63 are exact. */
    if (value < 9.2e18 && value == (double)(INT64)value) {
        UINT64 v = (UINT64)(INT64)value;
        WCHAR digits[24];
        UINT n = 0;
        while (v) {
            digits[n++] = (WCHAR)('0' + (UINT)(v % 10));
            v /= 10;
        }
        while (n)
            xb_putc(out, digits[--n]);
        return;
    }
    /* otherwise fifteen significant digits, without an exponent. */
    int exp10 = 0;
    double scaled = value;
    if (scaled >= 1e15) {
        while (scaled >= 1e15) {
            scaled /= 10;
            exp10++;
        }
    } else {
        while (scaled < 1e14) {
            scaled *= 10;
            exp10--;
        }
    }
    UINT64 digits = (UINT64)(scaled + 0.5);
    if (digits >= 1000000000000000ull) {
        digits /= 10;
        exp10++;
    }
    WCHAR d[24];
    UINT n = 0;
    while (digits) {
        d[n++] = (WCHAR)('0' + (UINT)(digits % 10));
        digits /= 10;
    }
    /* d holds the digits backwards, fifteen of them; exp10 is the power of the last. */
    WCHAR text[16];
    for (UINT i = 0; i < n; i++)
        text[i] = d[n - 1 - i];
    UINT used = n;
    while (used > 1 && text[used - 1] == '0') {
        used--;
        exp10++;
    }
    int point = (int)used + exp10; /* digits before the decimal point. */
    if (point <= 0) {
        xb_puta(out, "0.");
        for (int i = 0; i < -point; i++)
            xb_putc(out, '0');
        xb_put(out, text, used);
    } else if ((UINT)point >= used) {
        xb_put(out, text, used);
        for (int i = 0; i < point - (int)used; i++)
            xb_putc(out, '0');
    } else {
        xb_put(out, text, (UINT)point);
        xb_putc(out, '.');
        xb_put(out, text + point, used - (UINT)point);
    }
}

double xd_parse_number(const WCHAR *s, UINT length)
{
    UINT i = 0;
    while (i < length && xd_is_space(s[i]))
        i++;
    while (length > i && xd_is_space(s[length - 1]))
        length--;
    BOOL negative = FALSE;
    if (i < length && s[i] == '-') {
        negative = TRUE;
        i++;
    }
    if (i >= length)
        return xd_nan();
    double value = 0;
    UINT digits = 0;
    for (; i < length && s[i] >= '0' && s[i] <= '9'; i++, digits++)
        value = value * 10 + (s[i] - '0');
    if (i < length && s[i] == '.') {
        i++;
        UINT64 fraction = 0;
        int places = 0;
        for (; i < length && s[i] >= '0' && s[i] <= '9'; i++, digits++) {
            if (fraction < 100000000000000000ull) {
                fraction = fraction * 10 + (UINT)(s[i] - '0');
                places++;
            }
        }
        value += (double)fraction / pow10_int(places);
    }
    if (i != length || !digits)
        return xd_nan();
    return negative ? -value : value;
}
