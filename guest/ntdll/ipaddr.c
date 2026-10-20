/*
 * internet addresses and their text: the four forms of the conversions of ntdll, for version 4 and 6, to and from
 * wide and narrow strings. An address of version 4 is four bytes in network order, an address of version 6 sixteen; ports
 * are in network order and the scope of version 6 in host order.
 *
 * A string with a terminator argument is parsed as far as it is an address, and the argument says where it ended. The
 * string of the extended forms must be an address entirely, with a scope (version 6) and a port as they come after it.
 * Version 4 in non strict form takes the forms of inet_addr: one to four numbers, in decimal, octal with a leading zero or
 * hexadecimal with 0x, the last one filling the bytes that remain. The strict form takes four decimal numbers without a
 * leading zero (a guess: windows may take leading zeros there as decimal).
 */
#include "ntdll_private.h"

#define IP_TEXT_MAX 128

static BOOLEAN is_digit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

static int hex_value(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* a narrow string as wide characters in the buffer of the caller, and where the wide parse ended in the narrow one. */
static BOOLEAN widen(PCSTR text, WCHAR *out)
{
    ULONG i = 0;
    for (; text[i]; i++) {
        if (i + 1 >= IP_TEXT_MAX)
            return FALSE;
        out[i] = (UCHAR)text[i];
    }
    out[i] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ version 4 */

/* the numbers of an address of version 4 from the front of the text, and the end of them. */
static const WCHAR *ipv4_parse(const WCHAR *s, BOOLEAN strict, ULONG *address)
{
    ULONGLONG values[4];
    ULONG count = 0;
    const WCHAR *at = s;
    for (;;) {
        ULONGLONG value = 0;
        ULONG base = 10;
        const WCHAR *start = at;
        if (!strict && at[0] == L'0' && (at[1] == L'x' || at[1] == L'X')) {
            base = 16;
            at += 2;
        } else if (!strict && at[0] == L'0' && is_digit(at[1])) {
            base = 8;
            at++;
        }
        const WCHAR *digits = at;
        for (;; at++) {
            int d = hex_value(*at);
            if (d < 0 || (ULONG)d >= base)
                break;
            value = value * base + (ULONG)d;
            if (value > 0xFFFFFFFFull)
                return nullptr;
        }
        if (at == digits && !(base == 8))
            return nullptr;
        if (strict && (at - start > 1 && start[0] == L'0'))
            return nullptr;
        values[count++] = value;
        if (*at != L'.' || count == 4)
            break;
        /* a dot after the numbers belongs to them only when another number follows. */
        if (!is_digit(at[1]))
            break;
        at++;
    }
    if (strict && count != 4)
        return nullptr;
    for (ULONG i = 0; i + 1 < count; i++)
        if (values[i] > 255)
            return nullptr;
    if (values[count - 1] >= (1ull << (8 * (5 - count))))
        return nullptr;
    ULONG result = 0;
    for (ULONG i = 0; i + 1 < count; i++)
        result |= (ULONG)values[i] << (24 - 8 * i);
    result |= (ULONG)values[count - 1];
    PUCHAR bytes = (PUCHAR)address;
    bytes[0] = (UCHAR)(result >> 24);
    bytes[1] = (UCHAR)(result >> 16);
    bytes[2] = (UCHAR)(result >> 8);
    bytes[3] = (UCHAR)result;
    return at;
}

/* a port after a colon, in network order, to the end of the text. */
static BOOLEAN parse_port(const WCHAR *s, USHORT *port)
{
    if (!is_digit(*s))
        return FALSE;
    ULONG value = 0;
    for (; is_digit(*s); s++) {
        value = value * 10 + (ULONG)(*s - L'0');
        if (value > 65535)
            return FALSE;
    }
    if (*s)
        return FALSE;
    *port = (USHORT)(((value & 0xFF) << 8) | (value >> 8));
    return TRUE;
}

static NTSTATUS ipv4_to_address(const WCHAR *s, BOOLEAN strict, const WCHAR **end, PVOID address)
{
    ULONG value;
    const WCHAR *stop = ipv4_parse(s, strict, &value);
    if (!stop)
        return STATUS_INVALID_PARAMETER;
    memcpy(address, &value, 4);
    *end = stop;
    return STATUS_SUCCESS;
}

static NTSTATUS ipv4_to_address_ex(const WCHAR *s, BOOLEAN strict, PVOID address, PUSHORT port)
{
    ULONG value;
    USHORT number = 0;
    const WCHAR *stop = ipv4_parse(s, strict, &value);
    if (!stop)
        return STATUS_INVALID_PARAMETER;
    if (*stop == L':') {
        if (!parse_port(stop + 1, &number))
            return STATUS_INVALID_PARAMETER;
    } else if (*stop) {
        return STATUS_INVALID_PARAMETER;
    }
    memcpy(address, &value, 4);
    *port = number;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlIpv4StringToAddressW(PCWSTR s, BOOLEAN strict, PCWSTR *terminator, PVOID address)
{
    if (!s || !terminator || !address)
        return STATUS_INVALID_PARAMETER;
    return ipv4_to_address(s, strict, terminator, address);
}

NTSTATUS NTAPI RtlIpv4StringToAddressA(PCSTR s, BOOLEAN strict, PCSTR *terminator, PVOID address)
{
    WCHAR text[IP_TEXT_MAX];
    const WCHAR *end;
    if (!s || !terminator || !address || !widen(s, text))
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = ipv4_to_address(text, strict, &end, address);
    if (NT_SUCCESS(status))
        *terminator = s + (end - text);
    return status;
}

NTSTATUS NTAPI RtlIpv4StringToAddressExW(PCWSTR s, BOOLEAN strict, PVOID address, PUSHORT port)
{
    if (!s || !address || !port)
        return STATUS_INVALID_PARAMETER;
    return ipv4_to_address_ex(s, strict, address, port);
}

NTSTATUS NTAPI RtlIpv4StringToAddressExA(PCSTR s, BOOLEAN strict, PVOID address, PUSHORT port)
{
    WCHAR text[IP_TEXT_MAX];
    if (!s || !address || !port || !widen(s, text))
        return STATUS_INVALID_PARAMETER;
    return ipv4_to_address_ex(text, strict, address, port);
}

/* the text of an address of version 4 and the port after a colon when there is one. */
static ULONG ipv4_format(const UCHAR *a, USHORT port, WCHAR *out)
{
    ULONG n = 0;
    for (ULONG i = 0; i < 4; i++) {
        UCHAR v = a[i];
        if (i)
            out[n++] = L'.';
        if (v >= 100)
            out[n++] = (WCHAR)(L'0' + v / 100);
        if (v >= 10)
            out[n++] = (WCHAR)(L'0' + v / 10 % 10);
        out[n++] = (WCHAR)(L'0' + v % 10);
    }
    if (port) {
        ULONG host = ((port & 0xFF) << 8) | (port >> 8);
        WCHAR digits[6];
        ULONG d = 0;
        out[n++] = L':';
        for (; host; host /= 10)
            digits[d++] = (WCHAR)(L'0' + host % 10);
        while (d)
            out[n++] = digits[--d];
    }
    out[n] = 0;
    return n;
}

PWSTR NTAPI RtlIpv4AddressToStringW(const VOID *address, PWSTR out)
{
    return out + ipv4_format(address, 0, out);
}

PSTR NTAPI RtlIpv4AddressToStringA(const VOID *address, PSTR out)
{
    WCHAR text[24];
    ULONG n = ipv4_format(address, 0, text);
    for (ULONG i = 0; i <= n; i++)
        out[i] = (CHAR)text[i];
    return out + n;
}

NTSTATUS NTAPI RtlIpv4AddressToStringExW(const VOID *address, USHORT port, PWSTR out, PULONG length)
{
    WCHAR text[24];
    if (!address || !out || !length)
        return STATUS_INVALID_PARAMETER;
    ULONG n = ipv4_format(address, port, text);
    if (*length < n + 1) {
        *length = n + 1;
        return STATUS_INVALID_PARAMETER;
    }
    memcpy(out, text, (n + 1) * sizeof(WCHAR));
    *length = n + 1;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlIpv4AddressToStringExA(const VOID *address, USHORT port, PSTR out, PULONG length)
{
    WCHAR text[24];
    if (!address || !out || !length)
        return STATUS_INVALID_PARAMETER;
    ULONG n = ipv4_format(address, port, text);
    if (*length < n + 1) {
        *length = n + 1;
        return STATUS_INVALID_PARAMETER;
    }
    for (ULONG i = 0; i <= n; i++)
        out[i] = (CHAR)text[i];
    *length = n + 1;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ version 6 */

/* the groups of an address of version 6 from the front of the text, with a dotted tail of version 4 where one ends it. */
static const WCHAR *ipv6_parse(const WCHAR *s, UCHAR out[16])
{
    USHORT groups[8] = {};
    ULONG count = 0;
    LONG gap = -1;
    const WCHAR *at = s;
    if (at[0] == L':') {
        if (at[1] != L':')
            return nullptr;
        gap = 0;
        at += 2;
    }
    while (*at) {
        const WCHAR *start = at;
        ULONG value = 0, digits = 0;
        for (; hex_value(*at) >= 0 && digits < 4; at++, digits++)
            value = value * 16 + (ULONG)hex_value(*at);
        if (!digits) {
            /* nothing but a colon pair that has just been taken may be here. */
            break;
        }
        if (*at == L'.') {
            /* the last two groups are an address of version 4. */
            ULONG v4;
            if (count > 6)
                return nullptr;
            const WCHAR *stop = ipv4_parse(start, TRUE, &v4);
            if (!stop)
                return nullptr;
            const UCHAR *b = (const UCHAR *)&v4;
            groups[count++] = (USHORT)(b[0] << 8 | b[1]);
            groups[count++] = (USHORT)(b[2] << 8 | b[3]);
            at = stop;
            break;
        }
        if (count == 8)
            return nullptr;
        groups[count++] = (USHORT)value;
        if (*at != L':')
            break;
        if (at[1] == L':') {
            if (gap >= 0)
                return nullptr;
            gap = (LONG)count;
            at += 2;
            if (!*at || hex_value(*at) < 0)
                break;
        } else {
            /* a lone colon has to be followed by a group. */
            if (hex_value(at[1]) < 0)
                break;
            at++;
        }
    }
    if (gap < 0 ? count != 8 : count > 7)
        return nullptr;
    USHORT full[8] = {};
    if (gap < 0) {
        memcpy(full, groups, sizeof(full));
    } else {
        ULONG tail = count - (ULONG)gap;
        memcpy(full, groups, (ULONG)gap * sizeof(USHORT));
        memcpy(full + 8 - tail, groups + gap, tail * sizeof(USHORT));
    }
    for (ULONG i = 0; i < 8; i++) {
        out[2 * i] = (UCHAR)(full[i] >> 8);
        out[2 * i + 1] = (UCHAR)full[i];
    }
    return at;
}

static NTSTATUS ipv6_to_address(const WCHAR *s, const WCHAR **end, PVOID address)
{
    UCHAR bytes[16];
    const WCHAR *stop = ipv6_parse(s, bytes);
    if (!stop)
        return STATUS_INVALID_PARAMETER;
    memcpy(address, bytes, 16);
    *end = stop;
    return STATUS_SUCCESS;
}

/* [address%scope]:port, or address%scope, with every part after the address optional. */
static NTSTATUS ipv6_to_address_ex(const WCHAR *s, PVOID address, PULONG scope, PUSHORT port)
{
    UCHAR bytes[16];
    BOOLEAN bracket = *s == L'[';
    ULONG scope_id = 0;
    USHORT number = 0;
    const WCHAR *stop = ipv6_parse(bracket ? s + 1 : s, bytes);
    if (!stop)
        return STATUS_INVALID_PARAMETER;
    if (*stop == L'%') {
        ULONG digits = 0;
        for (stop++; is_digit(*stop); stop++, digits++) {
            if (scope_id > (0xFFFFFFFFu - 9) / 10)
                return STATUS_INVALID_PARAMETER;
            scope_id = scope_id * 10 + (ULONG)(*stop - L'0');
        }
        if (!digits)
            return STATUS_INVALID_PARAMETER;
    }
    if (bracket) {
        if (*stop != L']')
            return STATUS_INVALID_PARAMETER;
        stop++;
        if (*stop == L':') {
            if (!parse_port(stop + 1, &number))
                return STATUS_INVALID_PARAMETER;
            stop = L"";
        }
    }
    if (*stop)
        return STATUS_INVALID_PARAMETER;
    memcpy(address, bytes, 16);
    *scope = scope_id;
    *port = number;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlIpv6StringToAddressW(PCWSTR s, PCWSTR *terminator, PVOID address)
{
    if (!s || !terminator || !address)
        return STATUS_INVALID_PARAMETER;
    return ipv6_to_address(s, terminator, address);
}

NTSTATUS NTAPI RtlIpv6StringToAddressA(PCSTR s, PCSTR *terminator, PVOID address)
{
    WCHAR text[IP_TEXT_MAX];
    const WCHAR *end;
    if (!s || !terminator || !address || !widen(s, text))
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = ipv6_to_address(text, &end, address);
    if (NT_SUCCESS(status))
        *terminator = s + (end - text);
    return status;
}

NTSTATUS NTAPI RtlIpv6StringToAddressExW(PCWSTR s, PVOID address, PULONG scope, PUSHORT port)
{
    if (!s || !address || !scope || !port)
        return STATUS_INVALID_PARAMETER;
    return ipv6_to_address_ex(s, address, scope, port);
}

NTSTATUS NTAPI RtlIpv6StringToAddressExA(PCSTR s, PVOID address, PULONG scope, PUSHORT port)
{
    WCHAR text[IP_TEXT_MAX];
    if (!s || !address || !scope || !port || !widen(s, text))
        return STATUS_INVALID_PARAMETER;
    return ipv6_to_address_ex(text, address, scope, port);
}

static ULONG put_hex(WCHAR *out, ULONG value)
{
    static const WCHAR digits[] = L"0123456789abcdef";
    ULONG n = 0;
    BOOLEAN started = FALSE;
    for (int shift = 12; shift >= 0; shift -= 4) {
        ULONG d = (value >> shift) & 15;
        if (d || started || shift == 0) {
            out[n++] = digits[d];
            started = TRUE;
        }
    }
    return n;
}

/* the text of an address of version 6 as windows writes it, the longest run of zero groups (of two or more) as a pair of
   colons and a tail of version 4 where the address is mapped, compatible or an isatap address. */
static ULONG ipv6_format(const UCHAR *a, ULONG scope, USHORT port, WCHAR *out)
{
    USHORT g[8];
    for (ULONG i = 0; i < 8; i++)
        g[i] = (USHORT)(a[2 * i] << 8 | a[2 * i + 1]);
    ULONG n = 0;
    BOOLEAN v4tail = FALSE;
    ULONG groups = 8;
    if (port)
        out[n++] = L'[';
    BOOLEAN zero5 = !g[0] && !g[1] && !g[2] && !g[3] && !g[4];
    if (zero5 && g[5] == 0xFFFF)
        v4tail = TRUE;
    else if (zero5 && !g[5] && g[6] > 1)
        v4tail = TRUE;
    else if (!g[0] && !g[1] && !g[2] && !g[3] && g[4] == 0xFFFF && !g[5])
        v4tail = TRUE;
    else if (!g[0] && !g[1] && !g[2] && !g[3] && (!g[4] || g[4] == 0x0200) && g[5] == 0x5EFE)
        v4tail = TRUE;
    if (v4tail)
        groups = 6;
    LONG best = -1, best_length = 0;
    for (ULONG i = 0; i < groups;) {
        if (g[i]) {
            i++;
            continue;
        }
        ULONG j = i;
        while (j < groups && !g[j])
            j++;
        if ((LONG)(j - i) > best_length && j - i >= 2) {
            best = (LONG)i;
            best_length = (LONG)(j - i);
        }
        i = j;
    }
    BOOLEAN need_colon = FALSE;
    for (ULONG i = 0; i < groups;) {
        if ((LONG)i == best) {
            out[n++] = L':';
            out[n++] = L':';
            need_colon = FALSE;
            i += (ULONG)best_length;
            continue;
        }
        if (need_colon)
            out[n++] = L':';
        n += put_hex(out + n, g[i]);
        need_colon = TRUE;
        i++;
    }
    if (v4tail) {
        if (need_colon)
            out[n++] = L':';
        n += ipv4_format(a + 12, 0, out + n);
    }
    if (scope) {
        WCHAR digits[10];
        ULONG d = 0, value = scope;
        out[n++] = L'%';
        for (; value; value /= 10)
            digits[d++] = (WCHAR)(L'0' + value % 10);
        while (d)
            out[n++] = digits[--d];
    }
    if (port) {
        ULONG host = ((port & 0xFF) << 8) | (port >> 8);
        WCHAR digits[6];
        ULONG d = 0;
        out[n++] = L']';
        out[n++] = L':';
        for (; host; host /= 10)
            digits[d++] = (WCHAR)(L'0' + host % 10);
        while (d)
            out[n++] = digits[--d];
    }
    out[n] = 0;
    return n;
}

PWSTR NTAPI RtlIpv6AddressToStringW(const VOID *address, PWSTR out)
{
    return out + ipv6_format(address, 0, 0, out);
}

PSTR NTAPI RtlIpv6AddressToStringA(const VOID *address, PSTR out)
{
    WCHAR text[64];
    ULONG n = ipv6_format(address, 0, 0, text);
    for (ULONG i = 0; i <= n; i++)
        out[i] = (CHAR)text[i];
    return out + n;
}

NTSTATUS NTAPI RtlIpv6AddressToStringExW(const VOID *address, ULONG scope, USHORT port, PWSTR out, PULONG length)
{
    WCHAR text[96];
    if (!address || !out || !length)
        return STATUS_INVALID_PARAMETER;
    ULONG n = ipv6_format(address, scope, port, text);
    if (*length < n + 1) {
        *length = n + 1;
        return STATUS_INVALID_PARAMETER;
    }
    memcpy(out, text, (n + 1) * sizeof(WCHAR));
    *length = n + 1;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlIpv6AddressToStringExA(const VOID *address, ULONG scope, USHORT port, PSTR out, PULONG length)
{
    WCHAR text[96];
    if (!address || !out || !length)
        return STATUS_INVALID_PARAMETER;
    ULONG n = ipv6_format(address, scope, port, text);
    if (*length < n + 1) {
        *length = n + 1;
        return STATUS_INVALID_PARAMETER;
    }
    for (ULONG i = 0; i <= n; i++)
        out[i] = (CHAR)text[i];
    *length = n + 1;
    return STATUS_SUCCESS;
}
