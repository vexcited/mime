/*
 * address text: inet_addr and inet_ntoa, inet_pton and inet_ntop, and the
 * WSAStringToAddress and WSAAddressToString conversions.
 */
#include "ws2_32_private.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

BOOL WsNarrow(PCWSTR s, char *buffer, SIZE_T size)
{
    SIZE_T i = 0;
    for (; s[i]; i++) {
        if (s[i] > 0x7F || i + 1 >= size)
            return FALSE;
        buffer[i] = (char)s[i];
    }
    buffer[i] = 0;
    return TRUE;
}

BOOL WsWiden(const char *s, PWSTR buffer, SIZE_T count)
{
    SIZE_T n = strlen(s);
    if (n >= count)
        return FALSE;
    for (SIZE_T i = 0; i <= n; i++)
        buffer[i] = (WCHAR)(UCHAR)s[i];
    return TRUE;
}

/* ------------------------------------------------------------------ parsing */

/* one part of the inet_aton forms: hexadecimal after 0x, octal after 0, decimal otherwise. */
static BOOL parse_part(const char **p, BOOL strict, ULONG *value)
{
    const char *s = *p;
    ULONGLONG v = 0;
    int base = 10, digits = 0;
    if (!strict && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (!strict && s[0] == '0') {
        base = 8;
    }
    for (;; s++) {
        int d = hex_value(*s);
        if (d < 0 || d >= base)
            break;
        v = v * base + d;
        if (v > 0xFFFFFFFFull)
            return FALSE;
        digits++;
    }
    if (!digits)
        return FALSE;
    *value = (ULONG)v;
    *p = s;
    return TRUE;
}

BOOL WsParseIpv4(const char *s, BOOL strict, IN_ADDR *addr, const char **end)
{
    ULONG parts[4];
    int count = 0;
    const char *p = s;
    for (;;) {
        if (!parse_part(&p, strict, &parts[count]))
            return FALSE;
        count++;
        if (*p != '.' || count == 4)
            break;
        p++;
    }
    if (strict && count != 4)
        return FALSE;
    ULONG value;
    switch (count) {
    case 1:
        value = parts[0];
        break;
    case 2:
        if (parts[0] > 0xFF || parts[1] > 0xFFFFFF)
            return FALSE;
        value = parts[0] << 24 | parts[1];
        break;
    case 3:
        if (parts[0] > 0xFF || parts[1] > 0xFF || parts[2] > 0xFFFF)
            return FALSE;
        value = parts[0] << 24 | parts[1] << 16 | parts[2];
        break;
    default:
        if (parts[0] > 0xFF || parts[1] > 0xFF || parts[2] > 0xFF || parts[3] > 0xFF)
            return FALSE;
        value = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
        break;
    }
    addr->S_un.S_addr = htonl(value);
    if (end)
        *end = p;
    return TRUE;
}

/* the address itself, stopping at the first character that cannot continue it. */
static BOOL parse_ipv6(const char **p, IN6_ADDR *addr)
{
    USHORT words[8];
    int count = 0, gap = -1;
    const char *s = *p;
    if (s[0] == ':') {
        if (s[1] != ':')
            return FALSE;
        gap = 0;
        s += 2;
    }
    while (count < 8) {
        int d = hex_value(*s);
        if (d < 0)
            break;
        /* an embedded ipv4 address ends the text. */
        const char *q = s;
        while (*q >= '0' && *q <= '9')
            q++;
        if (*q == '.' && count <= 6) {
            IN_ADDR v4;
            const char *e;
            if (!WsParseIpv4(s, TRUE, &v4, &e))
                return FALSE;
            words[count++] = (USHORT)(v4.S_un.S_un_b.s_b1 << 8 | v4.S_un.S_un_b.s_b2);
            words[count++] = (USHORT)(v4.S_un.S_un_b.s_b3 << 8 | v4.S_un.S_un_b.s_b4);
            s = e;
            break;
        }
        ULONG v = 0;
        int n = 0;
        while ((d = hex_value(*s)) >= 0) {
            if (++n > 4)
                return FALSE;
            v = v << 4 | (ULONG)d;
            s++;
        }
        words[count++] = (USHORT)v;
        if (*s != ':')
            break;
        if (s[1] == ':') {
            if (gap >= 0)
                return FALSE;
            gap = count;
            s += 2;
            continue;
        }
        if (hex_value(s[1]) < 0)
            return FALSE;
        s++;
    }
    if (gap < 0 ? count != 8 : count > 7)
        return FALSE;
    memset(addr, 0, sizeof(*addr));
    int tail = gap < 0 ? 0 : count - gap;
    for (int i = 0; i < count - tail; i++)
        addr->u.Word[i] = htons(words[i]);
    for (int i = 0; i < tail; i++)
        addr->u.Word[8 - tail + i] = htons(words[gap + i]);
    *p = s;
    return TRUE;
}

BOOL WsParseIpv6(const char *s, IN6_ADDR *addr)
{
    IN6_ADDR a;
    if (!parse_ipv6(&s, &a) || *s)
        return FALSE;
    *addr = a;
    return TRUE;
}

static BOOL parse_decimal(const char **p, ULONGLONG max, ULONG *value)
{
    const char *s = *p;
    ULONGLONG v = 0;
    if (*s < '0' || *s > '9')
        return FALSE;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (ULONG)(*s++ - '0');
        if (v > max)
            return FALSE;
    }
    *value = (ULONG)v;
    *p = s;
    return TRUE;
}

BOOL WsParseIpv6Ex(const char *s, IN6_ADDR *addr, ULONG *scope, USHORT *port)
{
    BOOL bracket = *s == '[';
    ULONG scope_value = 0, port_value = 0;
    if (bracket)
        s++;
    IN6_ADDR a;
    if (!parse_ipv6(&s, &a))
        return FALSE;
    if (*s == '%') {
        s++;
        if (!parse_decimal(&s, 0xFFFFFFFF, &scope_value))
            return FALSE;
    }
    if (bracket) {
        if (*s++ != ']')
            return FALSE;
        if (*s == ':') {
            s++;
            if (!parse_decimal(&s, 0xFFFF, &port_value))
                return FALSE;
        }
    }
    if (*s)
        return FALSE;
    *addr = a;
    *scope = scope_value;
    *port = htons((USHORT)port_value);
    return TRUE;
}

/* ------------------------------------------------------------------ formatting */

int WsFormatIpv4(const IN_ADDR *addr, char *buffer)
{
    const UCHAR *b = &addr->S_un.S_un_b.s_b1;
    return _snprintf(buffer, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

int WsFormatIpv6(const IN6_ADDR *addr, char *buffer)
{
    USHORT w[8];
    for (int i = 0; i < 8; i++)
        w[i] = ntohs(addr->u.Word[i]);
    char *p = buffer;
    const UCHAR *b = addr->u.Byte;
    BOOL zero5 = !w[0] && !w[1] && !w[2] && !w[3] && !w[4];
    /* the forms ending with an ipv4 address: mapped, translated, compatible and isatap. */
    if (zero5 && w[5] == 0xFFFF)
        return _snprintf(buffer, INET6_ADDRSTRLEN, "::ffff:%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    if (!w[0] && !w[1] && !w[2] && !w[3] && w[4] == 0xFFFF && !w[5])
        return _snprintf(buffer, INET6_ADDRSTRLEN, "::ffff:0:%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    if (zero5 && !w[5] && w[6])
        return _snprintf(buffer, INET6_ADDRSTRLEN, "::%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    int isatap = (w[4] == 0 || w[4] == 0x200) && w[5] == 0x5EFE;
    int words = isatap ? 6 : 8;
    /* the first longest run of at least two zero words becomes ::. */
    int best = -1, best_len = 0;
    for (int i = 0; i < words;) {
        if (w[i]) {
            i++;
            continue;
        }
        int j = i;
        while (j < words && !w[j])
            j++;
        if (j - i > best_len) {
            best = i;
            best_len = j - i;
        }
        i = j;
    }
    if (best_len < 2)
        best = -1;
    for (int i = 0; i < words; i++) {
        if (i == best) {
            *p++ = ':';
            if (i == 0)
                *p++ = ':';
            i += best_len - 1;
            continue;
        }
        p += _snprintf(p, 6, "%x", w[i]);
        if (i < words - 1 || isatap)
            *p++ = ':';
    }
    if (isatap)
        p += _snprintf(p, INET_ADDRSTRLEN, "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    *p = 0;
    return (int)(p - buffer);
}

int WsFormatSockaddr(const SOCKADDR *addr, DWORD length, char *buffer, DWORD size)
{
    char text[INET6_ADDRSTRLEN];
    if (addr->sa_family == AF_INET) {
        if (length < sizeof(SOCKADDR_IN))
            return -1;
        const SOCKADDR_IN *in = (const SOCKADDR_IN *)addr;
        WsFormatIpv4(&in->sin_addr, text);
        if (in->sin_port)
            return _snprintf(buffer, size, "%s:%u", text, ntohs(in->sin_port));
        return _snprintf(buffer, size, "%s", text);
    }
    if (addr->sa_family == AF_INET6) {
        if (length < sizeof(SOCKADDR_IN6))
            return -1;
        const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)addr;
        WsFormatIpv6(&in6->sin6_addr, text);
        char scope[16] = "";
        if (in6->sin6_scope_id)
            _snprintf(scope, sizeof(scope), "%%%u", in6->sin6_scope_id);
        if (in6->sin6_port)
            return _snprintf(buffer, size, "[%s%s]:%u", text, scope, ntohs(in6->sin6_port));
        return _snprintf(buffer, size, "%s%s", text, scope);
    }
    return -2;
}

/* ------------------------------------------------------------------ inet_addr and inet_ntoa */

unsigned long WSAAPI inet_addr(const char *cp)
{
    if (!cp)
        return INADDR_NONE;
    if (cp[0] == ' ' && !cp[1])
        return 0;
    IN_ADDR addr;
    const char *end;
    if (!WsParseIpv4(cp, FALSE, &addr, &end))
        return INADDR_NONE;
    if (*end && !(*end == ' ' || (*end >= '\t' && *end <= '\r')))
        return INADDR_NONE;
    return addr.S_un.S_addr;
}

char *WSAAPI inet_ntoa(struct in_addr in)
{
    WS_THREAD *t = WsThread();
    if (!t) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    WsFormatIpv4(&in, t->ntoa);
    return t->ntoa;
}

/* ------------------------------------------------------------------ inet_pton and inet_ntop */

static INT pton(INT family, const char *s, PVOID addr)
{
    if (family == AF_INET) {
        IN_ADDR a;
        const char *end;
        if (!WsParseIpv4(s, TRUE, &a, &end) || *end)
            return 0;
        memcpy(addr, &a, sizeof(a));
        return 1;
    }
    IN6_ADDR a;
    if (!WsParseIpv6(s, &a))
        return 0;
    memcpy(addr, &a, sizeof(a));
    return 1;
}

INT WSAAPI inet_pton(INT family, PCSTR string, PVOID addr)
{
    if (family != AF_INET && family != AF_INET6) {
        SetLastError(WSAEAFNOSUPPORT);
        return -1;
    }
    if (!string || !addr) {
        SetLastError(WSAEFAULT);
        return -1;
    }
    return pton(family, string, addr);
}

INT WSAAPI InetPtonW(INT family, PCWSTR string, PVOID addr)
{
    if (family != AF_INET && family != AF_INET6) {
        SetLastError(WSAEAFNOSUPPORT);
        return -1;
    }
    if (!string || !addr) {
        SetLastError(WSAEFAULT);
        return -1;
    }
    char narrow[INET6_ADDRSTRLEN + 16];
    if (!WsNarrow(string, narrow, sizeof(narrow)))
        return 0;
    return pton(family, narrow, addr);
}

static int ntop(INT family, const VOID *addr, char *text)
{
    if (family == AF_INET)
        return WsFormatIpv4(addr, text);
    return WsFormatIpv6(addr, text);
}

PCSTR WSAAPI inet_ntop(INT family, const VOID *addr, PSTR string, size_t size)
{
    if (family != AF_INET && family != AF_INET6) {
        SetLastError(WSAEAFNOSUPPORT);
        return nullptr;
    }
    char text[INET6_ADDRSTRLEN];
    if (!addr || !string || (size_t)ntop(family, addr, text) >= size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    memcpy(string, text, strlen(text) + 1);
    return string;
}

PCWSTR WSAAPI InetNtopW(INT family, const VOID *addr, PWSTR string, size_t size)
{
    if (family != AF_INET && family != AF_INET6) {
        SetLastError(WSAEAFNOSUPPORT);
        return nullptr;
    }
    char text[INET6_ADDRSTRLEN];
    if (!addr || !string || (size_t)ntop(family, addr, text) >= size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    WsWiden(text, string, size);
    return string;
}

/* ------------------------------------------------------------------ WSAStringToAddress */

static INT string_to_address(const char *s, INT family, LPSOCKADDR address, LPINT length)
{
    if (family == AF_INET) {
        IN_ADDR a;
        const char *end;
        ULONG port = 0;
        if (!WsParseIpv4(s, FALSE, &a, &end))
            return WsFail(WSAEINVAL);
        if (*end == ':') {
            end++;
            if (!parse_decimal(&end, 0xFFFF, &port))
                return WsFail(WSAEINVAL);
        }
        if (*end)
            return WsFail(WSAEINVAL);
        if (*length < (INT)sizeof(SOCKADDR_IN)) {
            *length = sizeof(SOCKADDR_IN);
            return WsFail(WSAEFAULT);
        }
        SOCKADDR_IN *in = (SOCKADDR_IN *)address;
        memset(in, 0, sizeof(*in));
        in->sin_family = AF_INET;
        in->sin_port = htons((USHORT)port);
        in->sin_addr = a;
        *length = sizeof(SOCKADDR_IN);
        return 0;
    }
    if (family == AF_INET6) {
        IN6_ADDR a;
        ULONG scope;
        USHORT port;
        if (!WsParseIpv6Ex(s, &a, &scope, &port))
            return WsFail(WSAEINVAL);
        if (*length < (INT)sizeof(SOCKADDR_IN6)) {
            *length = sizeof(SOCKADDR_IN6);
            return WsFail(WSAEFAULT);
        }
        SOCKADDR_IN6 *in6 = (SOCKADDR_IN6 *)address;
        memset(in6, 0, sizeof(*in6));
        in6->sin6_family = AF_INET6;
        in6->sin6_port = port;
        in6->sin6_addr = a;
        in6->sin6_scope_id = scope;
        *length = sizeof(SOCKADDR_IN6);
        return 0;
    }
    return WsFail(WSAEINVAL);
}

INT WSAAPI WSAStringToAddressA(LPSTR string, INT family, LPWSAPROTOCOL_INFOA info, LPSOCKADDR address,
                               LPINT length)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!string || !address || !length)
        return WsFail(WSAEFAULT);
    if (info && info->iAddressFamily != family)
        return WsFail(WSAEINVAL);
    return string_to_address(string, family, address, length);
}

INT WSAAPI WSAStringToAddressW(LPWSTR string, INT family, LPWSAPROTOCOL_INFOW info, LPSOCKADDR address,
                               LPINT length)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!string || !address || !length)
        return WsFail(WSAEFAULT);
    if (info && info->iAddressFamily != family)
        return WsFail(WSAEINVAL);
    char narrow[128];
    if (!WsNarrow(string, narrow, sizeof(narrow)))
        return WsFail(WSAEINVAL);
    return string_to_address(narrow, family, address, length);
}

/* ------------------------------------------------------------------ WSAAddressToString */

static int address_to_text(LPSOCKADDR address, DWORD length, char *text, DWORD size)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!address)
        return WsFail(WSAEFAULT);
    int n = WsFormatSockaddr(address, length, text, size);
    if (n == -1)
        return WsFail(WSAEFAULT);
    if (n < 0)
        return WsFail(WSAEINVAL);
    return n;
}

INT WSAAPI WSAAddressToStringA(LPSOCKADDR address, DWORD length, LPWSAPROTOCOL_INFOA info, LPSTR string,
                               LPDWORD string_length)
{
    char text[96];
    int n = address_to_text(address, length, text, sizeof(text));
    if (n < 0)
        return SOCKET_ERROR;
    if (!string_length)
        return WsFail(WSAEFAULT);
    if (!string || *string_length < (DWORD)n + 1) {
        *string_length = (DWORD)n + 1;
        return WsFail(WSAEFAULT);
    }
    memcpy(string, text, (SIZE_T)n + 1);
    *string_length = (DWORD)n + 1;
    return 0;
}

INT WSAAPI WSAAddressToStringW(LPSOCKADDR address, DWORD length, LPWSAPROTOCOL_INFOW info, LPWSTR string,
                               LPDWORD string_length)
{
    char text[96];
    int n = address_to_text(address, length, text, sizeof(text));
    if (n < 0)
        return SOCKET_ERROR;
    if (!string_length)
        return WsFail(WSAEFAULT);
    if (!string || *string_length < (DWORD)n + 1) {
        *string_length = (DWORD)n + 1;
        return WsFail(WSAEFAULT);
    }
    WsWiden(text, string, (SIZE_T)n + 1);
    *string_length = (DWORD)n + 1;
    return 0;
}
