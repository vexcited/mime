/*
 * DER reading and writing shared by the encoders, decoders and certificate code.
 */
#include "crypt32_private.h"

/* ------------------------------------------------------------------ reading */

DWORD DerRead(der_reader *r, der_item *item)
{
    if (r->n < 2)
        return CRYPT_E_ASN1_EOD;
    const BYTE *p = r->p;
    if ((p[0] & 0x1F) == 0x1F)
        return CRYPT_E_ASN1_BADTAG;
    DWORD header = 2;
    DWORD length = p[1];
    if (length & 0x80) {
        DWORD count = length & 0x7F;
        if (!count)
            return CRYPT_E_ASN1_CORRUPT;
        if (count > 4)
            return CRYPT_E_ASN1_LARGE;
        if (r->n < 2 + count)
            return CRYPT_E_ASN1_EOD;
        length = 0;
        for (DWORD i = 0; i < count; i++)
            length = (length << 8) | p[2 + i];
        header += count;
    }
    if (length > r->n - header)
        return CRYPT_E_ASN1_EOD;
    item->tag = p[0];
    item->start = p;
    item->content = p + header;
    item->length = length;
    item->total = header + length;
    r->p += item->total;
    r->n -= item->total;
    return 0;
}

DWORD DerExpect(der_reader *r, BYTE tag, der_item *item)
{
    der_reader copy = *r;
    DWORD error = DerRead(&copy, item);
    if (error)
        return error;
    if (item->tag != tag)
        return CRYPT_E_ASN1_BADTAG;
    *r = copy;
    return 0;
}

BOOL DerPeek(const der_reader *r, BYTE tag)
{
    return r->n && r->p[0] == tag;
}

DWORD DerCount(const der_item *item)
{
    der_reader r;
    DerOpen(&r, item);
    DWORD count = 0;
    der_item child;
    while (r.n && !DerRead(&r, &child))
        count++;
    return count;
}

static void append_number(char *out, DWORD size, DWORD *pos, ULONG64 value)
{
    char digits[24];
    int n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n--) {
        if (*pos + 1 < size)
            out[*pos] = digits[n];
        (*pos)++;
    }
}

/* writes the dotted form; returns the length without the terminator or an error. */
DWORD DerParseOid(const der_item *item, char *out, DWORD size)
{
    if (item->tag != ASN_OID)
        return CRYPT_E_ASN1_BADTAG;
    if (!item->length)
        return CRYPT_E_ASN1_CORRUPT;
    DWORD pos = 0;
    ULONG64 value = 0;
    BOOL first = TRUE;
    for (DWORD i = 0; i < item->length; i++) {
        BYTE b = item->content[i];
        if (value >> 57)
            return CRYPT_E_ASN1_LARGE;
        value = (value << 7) | (b & 0x7F);
        if (b & 0x80) {
            if (i + 1 == item->length)
                return CRYPT_E_ASN1_CORRUPT;
            continue;
        }
        if (first) {
            ULONG64 arc = value < 40 ? 0 : value < 80 ? 1 : 2;
            append_number(out, size, &pos, arc);
            if (pos + 1 < size)
                out[pos] = '.';
            pos++;
            append_number(out, size, &pos, value - arc * 40);
            first = FALSE;
        } else {
            if (pos + 1 < size)
                out[pos] = '.';
            pos++;
            append_number(out, size, &pos, value);
        }
        value = 0;
    }
    if (size)
        out[pos < size ? pos : size - 1] = 0;
    return 0;
}

static int digits_at(const BYTE *p, DWORD count)
{
    int v = 0;
    for (DWORD i = 0; i < count; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

static LONG64 days_from_civil(int year, int month, int day)
{
    /* days since 1601-01-01. */
    static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    LONG64 y = year - 1601;
    LONG64 days = y * 365 + y / 4 - y / 100 + y / 400;
    days += before[month - 1] + day - 1;
    BOOL leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (leap && month > 2)
        days++;
    return days;
}

DWORD DerParseTime(const der_item *item, FILETIME *ft)
{
    const BYTE *p = item->content;
    DWORD n = item->length;
    int year;
    if (item->tag == ASN_UTCTIME) {
        if (n < 11)
            return CRYPT_E_ASN1_CORRUPT;
        year = digits_at(p, 2);
        if (year < 0)
            return CRYPT_E_ASN1_CORRUPT;
        year += year < 50 ? 2000 : 1900;
        p += 2;
        n -= 2;
    } else if (item->tag == ASN_GENERALTIME) {
        if (n < 13)
            return CRYPT_E_ASN1_CORRUPT;
        year = digits_at(p, 4);
        if (year < 1601)
            return CRYPT_E_ASN1_CORRUPT;
        p += 4;
        n -= 4;
    } else {
        return CRYPT_E_ASN1_BADTAG;
    }
    int month = digits_at(p, 2), day = digits_at(p + 2, 2), hour = digits_at(p + 4, 2), minute = digits_at(p + 6, 2);
    p += 8;
    n -= 8;
    int second = 0, millis = 0;
    if (n >= 2 && p[0] >= '0' && p[0] <= '9') {
        second = digits_at(p, 2);
        p += 2;
        n -= 2;
    }
    if (n && (p[0] == '.' || p[0] == ',')) {
        p++;
        n--;
        int scale = 100;
        while (n && p[0] >= '0' && p[0] <= '9') {
            millis += (p[0] - '0') * scale;
            scale /= 10;
            p++;
            n--;
        }
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60)
        return CRYPT_E_ASN1_CORRUPT;
    LONG64 offset = 0;
    if (n && p[0] == 'Z') {
        p++;
        n--;
    } else if (n >= 5 && (p[0] == '+' || p[0] == '-')) {
        int oh = digits_at(p + 1, 2), om = digits_at(p + 3, 2);
        if (oh < 0 || om < 0)
            return CRYPT_E_ASN1_CORRUPT;
        offset = (oh * 60 + om) * 60;
        if (p[0] == '-')
            offset = -offset;
        n -= 5;
    } else if (item->tag == ASN_UTCTIME) {
        return CRYPT_E_ASN1_CORRUPT;
    }
    LONG64 seconds = days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second - offset;
    ULONG64 ticks = (ULONG64)seconds * 10000000 + (ULONG64)millis * 10000;
    ft->dwLowDateTime = (DWORD)ticks;
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
    return 0;
}

DWORD DerParseInt(const der_item *item, int *value)
{
    if (!item->length)
        return CRYPT_E_ASN1_CORRUPT;
    if (item->length > 4)
        return CRYPT_E_ASN1_LARGE;
    int v = (signed char)item->content[0];
    for (DWORD i = 1; i < item->length; i++)
        v = (int)((unsigned)v << 8) | item->content[i];
    *value = v;
    return 0;
}

/* ------------------------------------------------------------------ writing */

void DerWriteInit(der_writer *w)
{
    w->data = nullptr;
    w->size = 0;
    w->capacity = 0;
    w->error = 0;
}

void DerWriteFree(der_writer *w)
{
    CrFree(w->data);
    DerWriteInit(w);
}

static BOOL reserve(der_writer *w, DWORD extra)
{
    if (w->error)
        return FALSE;
    if (w->size + extra <= w->capacity)
        return TRUE;
    DWORD capacity = w->capacity ? w->capacity * 2 : 256;
    while (capacity < w->size + extra)
        capacity *= 2;
    BYTE *data = CrAlloc(capacity);
    if (!data) {
        w->error = ERROR_OUTOFMEMORY;
        return FALSE;
    }
    if (w->size)
        memcpy(data, w->data, w->size);
    CrFree(w->data);
    w->data = data;
    w->capacity = capacity;
    return TRUE;
}

void DerWriteBytes(der_writer *w, const void *data, DWORD size)
{
    if (!size || !reserve(w, size))
        return;
    memcpy(w->data + w->size, data, size);
    w->size += size;
}

DWORD DerBegin(der_writer *w, BYTE tag)
{
    BYTE header[2] = {tag, 0};
    DerWriteBytes(w, header, 2);
    return w->size - 1;
}

void DerEnd(der_writer *w, DWORD mark)
{
    if (w->error)
        return;
    DWORD length = w->size - mark - 1;
    if (length < 0x80) {
        w->data[mark] = (BYTE)length;
        return;
    }
    DWORD count = length > 0xFFFFFF ? 4 : length > 0xFFFF ? 3 : length > 0xFF ? 2 : 1;
    if (!reserve(w, count))
        return;
    memmove(w->data + mark + 1 + count, w->data + mark + 1, length);
    w->data[mark] = (BYTE)(0x80 | count);
    for (DWORD i = 0; i < count; i++)
        w->data[mark + 1 + i] = (BYTE)(length >> (8 * (count - 1 - i)));
    w->size += count;
}

void DerWriteTlv(der_writer *w, BYTE tag, const void *data, DWORD size)
{
    DWORD mark = DerBegin(w, tag);
    DerWriteBytes(w, data, size);
    DerEnd(w, mark);
}

static void write_base128(der_writer *w, ULONG64 value)
{
    BYTE buffer[10];
    int n = 0;
    do {
        buffer[9 - n] = (BYTE)(value & 0x7F) | (n ? 0x80 : 0);
        value >>= 7;
        n++;
    } while (value);
    DerWriteBytes(w, buffer + 10 - n, n);
}

void DerWriteOid(der_writer *w, LPCSTR oid)
{
    ULONG64 arcs[64];
    DWORD count = 0;
    const char *p = oid;
    if (!p || !*p) {
        w->error = E_INVALIDARG;
        return;
    }
    while (*p) {
        if (*p < '0' || *p > '9' || count == RTL_NUMBER_OF(arcs)) {
            w->error = CRYPT_E_ASN1_ERROR;
            return;
        }
        ULONG64 v = 0;
        while (*p >= '0' && *p <= '9')
            v = v * 10 + (ULONG64)(*p++ - '0');
        arcs[count++] = v;
        if (*p == '.')
            p++;
        else if (*p) {
            w->error = CRYPT_E_ASN1_ERROR;
            return;
        }
    }
    if (count < 2 || arcs[0] > 2 || (arcs[0] < 2 && arcs[1] > 39)) {
        w->error = CRYPT_E_ASN1_ERROR;
        return;
    }
    DWORD mark = DerBegin(w, ASN_OID);
    write_base128(w, arcs[0] * 40 + arcs[1]);
    for (DWORD i = 2; i < count; i++)
        write_base128(w, arcs[i]);
    DerEnd(w, mark);
}

void DerWriteIntLe(der_writer *w, const BYTE *le, DWORD size, BOOL unsigned_value)
{
    DWORD mark = DerBegin(w, ASN_INTEGER);
    if (unsigned_value) {
        while (size > 1 && !le[size - 1])
            size--;
        if (!size || (le[size - 1] & 0x80)) {
            BYTE zero = 0;
            DerWriteBytes(w, &zero, 1);
        }
    } else {
        while (size > 1 && ((le[size - 1] == 0 && !(le[size - 2] & 0x80)) ||
                            (le[size - 1] == 0xFF && (le[size - 2] & 0x80))))
            size--;
        if (!size) {
            BYTE zero = 0;
            DerWriteBytes(w, &zero, 1);
        }
    }
    for (DWORD i = size; i; i--)
        DerWriteBytes(w, &le[i - 1], 1);
    DerEnd(w, mark);
}

void DerWriteInt(der_writer *w, int value)
{
    BYTE le[4] = {(BYTE)value, (BYTE)(value >> 8), (BYTE)(value >> 16), (BYTE)(value >> 24)};
    DerWriteIntLe(w, le, 4, FALSE);
}

void DerWriteTime(der_writer *w, const FILETIME *ft)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(ft, &st)) {
        w->error = CRYPT_E_BAD_ENCODE;
        return;
    }
    char text[20];
    BOOL utc = st.wYear >= 1950 && st.wYear < 2050;
    if (utc)
        _snprintf(text, sizeof(text), "%02u%02u%02u%02u%02u%02uZ", st.wYear % 100, st.wMonth, st.wDay, st.wHour,
                  st.wMinute, st.wSecond);
    else
        _snprintf(text, sizeof(text), "%04u%02u%02u%02u%02u%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
                  st.wMinute, st.wSecond);
    DerWriteTlv(w, utc ? ASN_UTCTIME : ASN_GENERALTIME, text, (DWORD)strlen(text));
}
