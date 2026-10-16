/* counted string helpers (rtl). */
#include "rtl.h"

void NTAPI RtlInitUnicodeString(PUNICODE_STRING dst, PCWSTR src)
{
    if (src) {
        size_t len = wcslen(src) * sizeof(WCHAR);
        if (len > 0xFFFC)
            len = 0xFFFC;
        dst->Length = (USHORT)len;
        dst->MaximumLength = (USHORT)(len + sizeof(WCHAR));
    } else {
        dst->Length = dst->MaximumLength = 0;
    }
    dst->Buffer = (PWSTR)src;
}

NTSTATUS NTAPI RtlInitUnicodeStringEx(PUNICODE_STRING dst, PCWSTR src)
{
    if (src && wcslen(src) * sizeof(WCHAR) > 0xFFFC)
        return STATUS_NAME_TOO_LONG;
    RtlInitUnicodeString(dst, src);
    return STATUS_SUCCESS;
}

void NTAPI RtlInitAnsiString(PANSI_STRING dst, PCSTR src)
{
    if (src) {
        size_t len = strlen(src);
        if (len > 0xFFFE)
            len = 0xFFFE;
        dst->Length = (USHORT)len;
        dst->MaximumLength = (USHORT)(len + 1);
    } else {
        dst->Length = dst->MaximumLength = 0;
    }
    dst->Buffer = (PCHAR)src;
}

void NTAPI RtlInitString(PSTRING dst, PCSTR src)
{
    RtlInitAnsiString(dst, src);
}

WCHAR NTAPI RtlUpcaseUnicodeChar(WCHAR c)
{
    return towupper(c);
}

WCHAR NTAPI RtlDowncaseUnicodeChar(WCHAR c)
{
    return towlower(c);
}

LONG NTAPI RtlCompareUnicodeStrings(PCWCH a, SIZE_T alen, PCWCH b, SIZE_T blen, BOOLEAN case_insensitive)
{
    SIZE_T n = alen < blen ? alen : blen;
    for (SIZE_T i = 0; i < n; i++) {
        WCHAR x = a[i], y = b[i];
        if (case_insensitive) {
            x = towupper(x);
            y = towupper(y);
        }
        if (x != y)
            return (LONG)x - (LONG)y;
    }
    return (LONG)alen - (LONG)blen;
}

LONG NTAPI RtlCompareUnicodeString(PCUNICODE_STRING a, PCUNICODE_STRING b, BOOLEAN case_insensitive)
{
    return RtlCompareUnicodeStrings(a->Buffer, a->Length / sizeof(WCHAR), b->Buffer, b->Length / sizeof(WCHAR),
                                    case_insensitive);
}

BOOLEAN NTAPI RtlEqualUnicodeString(PCUNICODE_STRING a, PCUNICODE_STRING b, BOOLEAN case_insensitive)
{
    if (a->Length != b->Length)
        return FALSE;
    return RtlCompareUnicodeString(a, b, case_insensitive) == 0;
}

BOOLEAN NTAPI RtlPrefixUnicodeString(PCUNICODE_STRING prefix, PCUNICODE_STRING s, BOOLEAN case_insensitive)
{
    if (prefix->Length > s->Length)
        return FALSE;
    return RtlCompareUnicodeStrings(prefix->Buffer, prefix->Length / sizeof(WCHAR), s->Buffer,
                                    prefix->Length / sizeof(WCHAR), case_insensitive) == 0;
}

void NTAPI RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    if (!src || !src->Buffer) {
        dst->Length = 0;
        return;
    }
    USHORT n = src->Length < dst->MaximumLength ? src->Length : dst->MaximumLength;
    memcpy(dst->Buffer, src->Buffer, n);
    dst->Length = n;
    if (n + sizeof(WCHAR) <= dst->MaximumLength)
        dst->Buffer[n / sizeof(WCHAR)] = 0;
}

NTSTATUS NTAPI RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR src)
{
    if (!src)
        return STATUS_SUCCESS;
    size_t n = wcslen(src) * sizeof(WCHAR);
    if (dst->Length + n > dst->MaximumLength)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy((char *)dst->Buffer + dst->Length, src, n);
    dst->Length = (USHORT)(dst->Length + n);
    if (dst->Length + sizeof(WCHAR) <= dst->MaximumLength)
        dst->Buffer[dst->Length / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src)
{
    if (dst->Length + src->Length > dst->MaximumLength)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy((char *)dst->Buffer + dst->Length, src->Buffer, src->Length);
    dst->Length = (USHORT)(dst->Length + src->Length);
    if (dst->Length + sizeof(WCHAR) <= dst->MaximumLength)
        dst->Buffer[dst->Length / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUpcaseUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    if (allocate) {
        dst->Buffer = MimeRtlAllocate(src->Length + sizeof(WCHAR));
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)(src->Length + sizeof(WCHAR));
    } else if (src->Length > dst->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    for (USHORT i = 0; i < src->Length / sizeof(WCHAR); i++)
        dst->Buffer[i] = towupper(src->Buffer[i]);
    dst->Length = src->Length;
    return STATUS_SUCCESS;
}

/*
 * the ansi code page is windows-1252. characters outside latin-1 map to
 * '?' when narrowing; widening maps 0x80..0x9F through the cp1252 table.
 */
static constexpr WCHAR g_cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static WCHAR ansi_to_wide(UCHAR c)
{
    if (c >= 0x80 && c < 0xA0)
        return g_cp1252_high[c - 0x80];
    return c;
}

static CHAR wide_to_ansi(WCHAR c)
{
    if (c < 0x80 || (c >= 0xA0 && c < 0x100))
        return (CHAR)c;
    for (int i = 0; i < 32; i++)
        if (g_cp1252_high[i] == c)
            return (CHAR)(0x80 + i);
    return '?';
}

NTSTATUS NTAPI RtlMultiByteToUnicodeN(PWCH dst, ULONG dst_bytes, PULONG written, const CHAR *src, ULONG src_bytes)
{
    ULONG n = src_bytes < dst_bytes / sizeof(WCHAR) ? src_bytes : dst_bytes / sizeof(WCHAR);
    for (ULONG i = 0; i < n; i++)
        dst[i] = ansi_to_wide((UCHAR)src[i]);
    if (written)
        *written = n * sizeof(WCHAR);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnicodeToMultiByteN(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src, ULONG src_bytes)
{
    ULONG n = src_bytes / sizeof(WCHAR) < dst_bytes ? src_bytes / sizeof(WCHAR) : dst_bytes;
    for (ULONG i = 0; i < n; i++)
        dst[i] = wide_to_ansi(src[i]);
    if (written)
        *written = n;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUTF8ToUnicodeN(PWSTR dst, ULONG dst_bytes, PULONG written, PCCH src, ULONG src_bytes)
{
    const UCHAR *s = (const UCHAR *)src, *end = s + src_bytes;
    ULONG o = 0, cap = dst ? dst_bytes / sizeof(WCHAR) : 0;
    while (s < end) {
        ULONG c = *s++;
        if (c >= 0xF0 && end - s >= 3) {
            c = ((c & 7) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else if (c >= 0xE0 && end - s >= 2) {
            c = ((c & 0xF) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if (c >= 0xC0 && end - s >= 1) {
            c = ((c & 0x1F) << 6) | (s[0] & 0x3F);
            s += 1;
        } else if (c >= 0x80) {
            c = 0xFFFD;
        }
        if (c >= 0x10000) {
            if (dst && o + 2 <= cap) {
                dst[o] = (WCHAR)(0xD800 + ((c - 0x10000) >> 10));
                dst[o + 1] = (WCHAR)(0xDC00 + ((c - 0x10000) & 0x3FF));
            }
            o += 2;
        } else {
            if (dst && o < cap)
                dst[o] = (WCHAR)c;
            o++;
        }
    }
    if (written)
        *written = (dst ? (o < cap ? o : cap) : o) * sizeof(WCHAR);
    return dst && o > cap ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnicodeToUTF8N(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src, ULONG src_bytes)
{
    ULONG n = src_bytes / sizeof(WCHAR), o = 0;
    for (ULONG i = 0; i < n; i++) {
        ULONG c = src[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        }
        UCHAR tmp[4];
        ULONG len;
        if (c < 0x80) {
            tmp[0] = (UCHAR)c;
            len = 1;
        } else if (c < 0x800) {
            tmp[0] = (UCHAR)(0xC0 | (c >> 6));
            tmp[1] = (UCHAR)(0x80 | (c & 0x3F));
            len = 2;
        } else if (c < 0x10000) {
            tmp[0] = (UCHAR)(0xE0 | (c >> 12));
            tmp[1] = (UCHAR)(0x80 | ((c >> 6) & 0x3F));
            tmp[2] = (UCHAR)(0x80 | (c & 0x3F));
            len = 3;
        } else {
            tmp[0] = (UCHAR)(0xF0 | (c >> 18));
            tmp[1] = (UCHAR)(0x80 | ((c >> 12) & 0x3F));
            tmp[2] = (UCHAR)(0x80 | ((c >> 6) & 0x3F));
            tmp[3] = (UCHAR)(0x80 | (c & 0x3F));
            len = 4;
        }
        if (dst) {
            if (o + len > dst_bytes)
                break;
            memcpy(dst + o, tmp, len);
        }
        o += len;
    }
    if (written)
        *written = o;
    return STATUS_SUCCESS;
}

ULONG NTAPI RtlUnicodeStringToAnsiSize(PCUNICODE_STRING s)
{
    return s->Length / sizeof(WCHAR) + 1;
}

NTSTATUS NTAPI RtlUnicodeStringToAnsiString(PANSI_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate)
{
    ULONG len = src->Length / sizeof(WCHAR);
    if (allocate) {
        dst->Buffer = MimeRtlAllocate(len + 1);
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)(len + 1);
    } else if (len >= dst->MaximumLength) {
        if (!dst->MaximumLength)
            return STATUS_BUFFER_OVERFLOW;
        len = dst->MaximumLength - 1u;
    }
    RtlUnicodeToMultiByteN(dst->Buffer, len, nullptr, src->Buffer, len * sizeof(WCHAR));
    dst->Buffer[len] = 0;
    dst->Length = (USHORT)len;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlAnsiStringToUnicodeString(PUNICODE_STRING dst, PCANSI_STRING src, BOOLEAN allocate)
{
    ULONG bytes = src->Length * sizeof(WCHAR);
    if (bytes > 0xFFFC)
        return STATUS_INVALID_PARAMETER_2;
    if (allocate) {
        dst->Buffer = MimeRtlAllocate(bytes + sizeof(WCHAR));
        if (!dst->Buffer)
            return STATUS_NO_MEMORY;
        dst->MaximumLength = (USHORT)(bytes + sizeof(WCHAR));
    } else if (bytes > dst->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    RtlMultiByteToUnicodeN(dst->Buffer, bytes, nullptr, src->Buffer, src->Length);
    dst->Length = (USHORT)bytes;
    if (bytes + sizeof(WCHAR) <= dst->MaximumLength)
        dst->Buffer[bytes / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI RtlCreateUnicodeString(PUNICODE_STRING s, PCWSTR source)
{
    SIZE_T bytes = (wcslen(source) + 1) * sizeof(WCHAR);
    s->Buffer = nullptr;
    s->Length = s->MaximumLength = 0;
    if (bytes > 0xFFFE)
        return FALSE;
    s->Buffer = MimeRtlAllocate(bytes);
    if (!s->Buffer)
        return FALSE;
    memcpy(s->Buffer, source, bytes);
    s->Length = (USHORT)(bytes - sizeof(WCHAR));
    s->MaximumLength = (USHORT)bytes;
    return TRUE;
}

void NTAPI RtlFreeUnicodeString(PUNICODE_STRING s)
{
    if (s->Buffer)
        MimeRtlFree(s->Buffer);
    s->Buffer = nullptr;
    s->Length = s->MaximumLength = 0;
}

void NTAPI RtlFreeAnsiString(PANSI_STRING s)
{
    if (s->Buffer)
        MimeRtlFree(s->Buffer);
    s->Buffer = nullptr;
    s->Length = s->MaximumLength = 0;
}

NTSTATUS NTAPI RtlIntegerToUnicodeString(ULONG value, ULONG base, PUNICODE_STRING s)
{
    WCHAR tmp[34];
    int n = 0;
    if (!base)
        base = 10;
    if (base != 2 && base != 8 && base != 10 && base != 16)
        return STATUS_INVALID_PARAMETER;
    do {
        ULONG d = value % base;
        tmp[n++] = (WCHAR)(d < 10 ? '0' + d : 'A' + d - 10);
        value /= base;
    } while (value);
    if ((ULONG)n * sizeof(WCHAR) > s->MaximumLength)
        return STATUS_BUFFER_OVERFLOW;
    for (int i = 0; i < n; i++)
        s->Buffer[i] = tmp[n - 1 - i];
    s->Length = (USHORT)(n * sizeof(WCHAR));
    if (s->Length + sizeof(WCHAR) <= s->MaximumLength)
        s->Buffer[n] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnicodeStringToInteger(PCUNICODE_STRING s, ULONG base, PULONG value)
{
    const WCHAR *p = s->Buffer, *end = s->Buffer + s->Length / sizeof(WCHAR);
    BOOLEAN neg = FALSE;
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    if (p < end && (*p == '-' || *p == '+'))
        neg = *p++ == '-';
    if (!base) {
        base = 10;
        if (end - p >= 2 && p[0] == '0') {
            if (p[1] == 'x' || p[1] == 'X')
                base = 16, p += 2;
            else if (p[1] == 'o' || p[1] == 'O')
                base = 8, p += 2;
            else if (p[1] == 'b' || p[1] == 'B')
                base = 2, p += 2;
        }
    }
    ULONG v = 0;
    for (; p < end; p++) {
        ULONG d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            d = *p - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
    }
    *value = neg ? (ULONG)-(LONG)v : v;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlHashUnicodeString(PCUNICODE_STRING s, BOOLEAN case_insensitive, ULONG algorithm, PULONG hash)
{
    /* the x65599 hash, as used by HASH_STRING_ALGORITHM_X65599 and the loader. */
    ULONG h = 0;
    for (USHORT i = 0; i < s->Length / sizeof(WCHAR); i++) {
        WCHAR c = case_insensitive ? towupper(s->Buffer[i]) : s->Buffer[i];
        h = h * 65599 + c;
    }
    *hash = h;
    return STATUS_SUCCESS;
}
