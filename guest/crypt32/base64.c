/*
 * CryptBinaryToString and CryptStringToBinary: base64, pem headers and the
 * hexadecimal dump formats, byte for byte as crypt32.dll of windows 11
 * produces and accepts them.
 */
#include "crypt32_private.h"

static constexpr char b64_std[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static constexpr char b64_uri[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static const struct {
    const char *begin;
    const char *end;
} pem_headers[] = {
    {"-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----"},
    {"-----BEGIN NEW CERTIFICATE REQUEST-----", "-----END NEW CERTIFICATE REQUEST-----"},
    {"-----BEGIN X509 CRL-----", "-----END X509 CRL-----"},
};

/* counts every character and stores those that fit. */
typedef struct {
    char *buffer;
    DWORD capacity;
    DWORD count;
} sink;

static void put_char(sink *s, char c)
{
    if (s->buffer && s->count < s->capacity)
        s->buffer[s->count] = c;
    s->count++;
}

static void put_str(sink *s, const char *text)
{
    while (*text)
        put_char(s, *text++);
}

static const char *newline_for(DWORD flags)
{
    return (flags & CRYPT_STRING_NOCRLF) ? "" : (flags & CRYPT_STRING_NOCR) ? "\n" : "\r\n";
}

/* ------------------------------------------------------------------ encoding */

static void base64_encode(sink *s, const BYTE *data, DWORD size, DWORD flags)
{
    const char *nl = newline_for(flags);
    BOOL uri = (flags & 0x37FFFFFF) == CRYPT_STRING_BASE64URI;
    BOOL percent = (flags & CRYPT_STRING_PERCENTESCAPE) != 0;
    const char *alphabet = uri ? b64_uri : b64_std;
    DWORD line_start = s->count;
    DWORD start = s->count;
    for (DWORD i = 0; i < size; i += 3) {
        DWORD left = size - i;
        BYTE b0 = data[i], b1 = left > 1 ? data[i + 1] : 0, b2 = left > 2 ? data[i + 2] : 0;
        char quad[4] = {alphabet[b0 >> 2], alphabet[((b0 << 4) & 0x3F) | (b1 >> 4)],
                        left > 1 ? alphabet[((b1 << 2) & 0x3F) | (b2 >> 6)] : '=', left > 2 ? alphabet[b2 & 0x3F] : '='};
        char expanded[12];
        DWORD n = 0;
        for (int k = 0; k < 4; k++) {
            if (uri && quad[k] == '=')
                break;
            const char *escape = nullptr;
            if (percent)
                escape = quad[k] == '+' ? "%2B" : quad[k] == '/' ? "%2F" : quad[k] == '=' ? "%3D" : nullptr;
            if (escape) {
                memcpy(expanded + n, escape, 3);
                n += 3;
            } else {
                expanded[n++] = quad[k];
            }
        }
        for (DWORD k = 0; k < n; k++) {
            if (s->count - line_start >= 64) {
                put_str(s, nl);
                line_start = s->count;
            }
            put_char(s, expanded[k]);
        }
    }
    if (s->count != start)
        put_str(s, nl);
}

static DWORD hex_encode(sink *s, const BYTE *data, DWORD size, DWORD flags)
{
    const char *nl = newline_for(flags);
    BOOL has_nl = nl[0] != 0;
    const char *sep = " ", *group = "  ", *line_break = nl, *prefix = "";
    BOOL address = FALSE, ascii = FALSE;
    switch (flags & 0x3FFFFFFF) {
    case CRYPT_STRING_HEXRAW:
        sep = group = line_break = "";
        break;
    case CRYPT_STRING_HEX:
        line_break = has_nl ? nl : " ";
        group = has_nl ? "  " : " ";
        break;
    case CRYPT_STRING_HEXASCII:
        ascii = TRUE;
        break;
    case CRYPT_STRING_HEXADDR:
        address = TRUE;
        prefix = "\t";
        break;
    case CRYPT_STRING_HEXASCIIADDR:
        address = TRUE;
        ascii = TRUE;
        prefix = "\t";
        break;
    default:
        return ERROR_INVALID_DATA;
    }
    static const char digits[] = "0123456789abcdef";
    for (DWORD offset = 0; offset < size; offset += 16) {
        DWORD count = size - offset < 16 ? size - offset : 16;
        for (DWORD i = 0; i < count; i++) {
            const char *before = "", *separator = sep;
            if (!(i & 7))
                separator = group;
            if (!i) {
                separator = prefix;
                if (offset)
                    before = line_break;
            }
            put_str(s, before);
            if (!i && address) {
                char text[16];
                _snprintf(text, sizeof(text), "%04x", offset);
                put_str(s, text);
            }
            put_str(s, separator);
            put_char(s, digits[data[offset + i] >> 4]);
            put_char(s, digits[data[offset + i] & 15]);
        }
        if (ascii) {
            DWORD pad = (count <= 8) + 51 - 3 * count;
            for (DWORD i = 0; i < pad; i++)
                put_char(s, ' ');
            for (DWORD i = 0; i < count; i++) {
                BYTE c = data[offset + i];
                put_char(s, c >= 0x20 && c <= 0x7E ? (char)c : '.');
            }
        }
    }
    if (size)
        put_str(s, nl);
    return 0;
}

BOOL WINAPI CryptBinaryToStringA(const BYTE *pbBinary, DWORD cbBinary, DWORD dwFlags, LPSTR pszString,
                                 DWORD *pcchString)
{
    if (!pbBinary || !cbBinary || !pcchString)
        return CrFail(ERROR_INVALID_PARAMETER);
    BYTE hash[20];
    if (dwFlags & CRYPT_STRING_HASHDATA) {
        CrHash(CALG_SHA1, pbBinary, cbBinary, hash);
        pbBinary = hash;
        cbBinary = sizeof(hash);
        dwFlags &= ~CRYPT_STRING_HASHDATA;
    }
    DWORD mode = dwFlags & 0x37FFFFFF;
    int header = -1;
    switch (mode) {
    case CRYPT_STRING_BASE64HEADER:
        header = 0;
        break;
    case CRYPT_STRING_BASE64REQUESTHEADER:
        header = 1;
        break;
    case CRYPT_STRING_BASE64X509CRLHEADER:
        header = 2;
        break;
    case CRYPT_STRING_BASE64_ANY:
    case CRYPT_STRING_ANY:
        dwFlags = (dwFlags & 0xC0000000) | CRYPT_STRING_BASE64;
        mode = CRYPT_STRING_BASE64;
        break;
    case CRYPT_STRING_HEX_ANY:
        dwFlags = (dwFlags & 0xC0000000) | CRYPT_STRING_HEX;
        mode = CRYPT_STRING_HEX;
        break;
    }
    if (mode == CRYPT_STRING_BINARY) {
        if (pszString) {
            if (*pcchString < cbBinary)
                return CrFail(ERROR_MORE_DATA);
            memcpy(pszString, pbBinary, cbBinary);
        }
        *pcchString = cbBinary;
        return TRUE;
    }
    BOOL base64 = mode <= CRYPT_STRING_BASE64REQUESTHEADER || mode == CRYPT_STRING_BASE64X509CRLHEADER ||
                  mode == CRYPT_STRING_BASE64URI;
    BOOL hex = mode == CRYPT_STRING_HEX || mode == CRYPT_STRING_HEXASCII || mode == CRYPT_STRING_HEXADDR ||
               mode == CRYPT_STRING_HEXASCIIADDR || mode == CRYPT_STRING_HEXRAW;
    if (!base64 && !hex)
        return CrFail(ERROR_INVALID_PARAMETER);
    const char *nl = newline_for(dwFlags);
    sink s = {pszString, pszString ? *pcchString : 0, 0};
    if (header >= 0) {
        put_str(&s, pem_headers[header].begin);
        put_str(&s, nl);
    }
    if (base64) {
        base64_encode(&s, pbBinary, cbBinary, dwFlags);
    } else {
        DWORD error = hex_encode(&s, pbBinary, cbBinary, dwFlags);
        if (error)
            return CrFail(error);
    }
    if (header >= 0) {
        put_str(&s, pem_headers[header].end);
        put_str(&s, nl);
    }
    if (!pszString) {
        *pcchString = s.count + 1;
        return TRUE;
    }
    if (s.count >= s.capacity) {
        /* base64 output that fills the buffer exactly reports the size it needs. */
        if (base64 && s.count == s.capacity)
            *pcchString = s.count + 1;
        return CrFail(ERROR_MORE_DATA);
    }
    pszString[s.count] = 0;
    *pcchString = s.count;
    return TRUE;
}

BOOL WINAPI CryptBinaryToStringW(const BYTE *pbBinary, DWORD cbBinary, DWORD dwFlags, LPWSTR pszString,
                                 DWORD *pcchString)
{
    if (!pszString)
        return CryptBinaryToStringA(pbBinary, cbBinary, dwFlags, nullptr, pcchString);
    if (!pcchString || !*pcchString)
        return CrFail(ERROR_INVALID_PARAMETER);
    if ((dwFlags & 0x3FFFFFFF) == CRYPT_STRING_BINARY)
        return CryptBinaryToStringA(pbBinary, cbBinary, dwFlags, (LPSTR)pszString, pcchString);
    DWORD capacity = *pcchString;
    char *narrow = LocalAlloc(LMEM_FIXED, capacity);
    if (!narrow)
        return FALSE;
    BOOL ok = CryptBinaryToStringA(pbBinary, cbBinary, dwFlags, narrow, pcchString);
    if (ok) {
        if (capacity <= *pcchString) {
            ok = CrFail(ERROR_ARITHMETIC_OVERFLOW);
        } else {
            for (DWORD i = 0; i <= *pcchString; i++)
                pszString[i] = (BYTE)narrow[i];
        }
    }
    LocalFree(narrow);
    return ok;
}

/* ------------------------------------------------------------------ decoding */

static BOOL b64_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int b64_value(char c, BOOL uri)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == (uri ? '-' : '+'))
        return 62;
    if (c == (uri ? '_' : '/'))
        return 63;
    return -1;
}

/*
 * reads the next significant character, decoding %xx escapes when asked;
 * returns -1 at the end, -2 for a broken escape.
 */
static int next_char(const char **p, const char *end, BOOL percent)
{
    while (*p < end) {
        char c = *(*p)++;
        if (b64_space(c))
            continue;
        if (!percent || c != '%')
            return (BYTE)c;
        int hi = -1, lo = -1;
        while (*p < end && b64_space(**p))
            (*p)++;
        if (*p < end)
            hi = hex_digit(*(*p)++);
        while (*p < end && b64_space(**p))
            (*p)++;
        if (*p < end)
            lo = hex_digit(*(*p)++);
        if (hi < 0 || lo < 0)
            return -2;
        c = (char)(hi << 4 | lo);
        if (b64_space(c))
            continue;
        return (BYTE)c;
    }
    return -1;
}

static DWORD base64_decode(const char *text, DWORD length, BOOL strict, BOOL uri, BOOL percent, BYTE *out,
                           DWORD *size)
{
    const char *end = text + length, *p = text, *stop = text;
    DWORD count = 0;
    /* counts the alphabet characters up to the first other one. */
    for (;;) {
        const char *before = p;
        int c = next_char(&p, end, percent);
        if (c == -2)
            return ERROR_INVALID_DATA;
        if (c < 0 || b64_value((char)c, uri) < 0) {
            stop = c < 0 ? end : before;
            break;
        }
        count++;
    }
    if (!count)
        return ERROR_INVALID_DATA;
    BOOL check = strict || uri;
    if (check && (count & 3) == 1)
        return ERROR_INVALID_DATA;
    const char *after = stop;
    if (!uri && (count & 3)) {
        DWORD pads = 4 - (count & 3);
        const char *q = stop;
        while (pads) {
            int c = next_char(&q, end, percent);
            if (c != '=')
                return ERROR_INVALID_DATA;
            pads--;
        }
        after = q;
    }
    if (check) {
        DWORD equals = 0;
        for (const char *q = after; q < end; q++) {
            if (b64_space(*q))
                continue;
            if (uri && *q == '=' && ++equals < 3)
                continue;
            return ERROR_INVALID_DATA;
        }
    }
    DWORD needed = 3 * ((count + 3) / 4);
    switch (count & 3) {
    case 1:
    case 2:
        needed -= 2;
        break;
    case 3:
        needed -= 1;
        break;
    }
    if (!out) {
        *size = needed;
        return 0;
    }
    if (needed > *size) {
        *size = needed;
        return ERROR_MORE_DATA;
    }
    DWORD written = 0, group = 0, have = 0;
    p = text;
    for (DWORD i = 0; i < count; i++) {
        int c = next_char(&p, stop, percent);
        group = (group << 6) | (DWORD)b64_value((char)c, uri);
        if (++have == 4) {
            out[written++] = (BYTE)(group >> 16);
            out[written++] = (BYTE)(group >> 8);
            out[written++] = (BYTE)group;
            group = have = 0;
        }
    }
    if (have == 1) {
        /* windows pads a lone character with the value 64 and keeps one byte. */
        out[written++] = (BYTE)((group << 2) | 4);
    } else if (have == 2) {
        out[written++] = (BYTE)(group >> 4);
    } else if (have == 3) {
        out[written++] = (BYTE)(group >> 10);
        out[written++] = (BYTE)(group >> 2);
    }
    *size = written;
    return 0;
}

/* finds "-----BEGIN ...-----" or the end marker; returns its offset or -1 and the marker length. */
static int find_marker(const char *text, DWORD length, BOOL begin, DWORD *marker)
{
    const char *key = begin ? "-----BEGIN " : "-----END ";
    DWORD key_length = begin ? 11 : 9;
    const char *end = text + length, *p = text;
    for (;;) {
        while (p < end && *p != '-')
            p++;
        if (p + key_length > end)
            return -1;
        if (!strncmp(p, key, key_length)) {
            const char *q = p + key_length;
            while (q < end && *q != '-')
                q++;
            if (q + 5 > end)
                return -1;
            if (!strncmp(q, "-----", 5)) {
                int back = 0;
                while (p > text && back < 2) {
                    if (p[-1] != '-')
                        break;
                    p--;
                    back++;
                }
                *marker = (DWORD)(q - p) + 5;
                return (int)(p - text);
            }
        }
        p++;
    }
}

static DWORD hex_decode_simple(const char *text, DWORD length, BYTE *out, DWORD *size)
{
    DWORD capacity = out ? *size : 0, count = 0;
    BOOL overflow = FALSE;
    const char *end = text + length;
    for (const char *p = text; p < end;) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '-') {
            p++;
            continue;
        }
        int hi = hex_digit(c);
        if (hi < 0 || p + 1 >= end)
            return ERROR_INVALID_DATA;
        int lo = hex_digit(p[1]);
        if (lo < 0)
            return ERROR_INVALID_DATA;
        p += 2;
        if (out) {
            if (count < capacity)
                out[count] = (BYTE)(hi << 4 | lo);
            else
                overflow = TRUE;
        }
        count++;
    }
    *size = count;
    return overflow ? ERROR_MORE_DATA : 0;
}

static BOOL hex_white(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static BOOL hex_skip(char c)
{
    return hex_white(c) || c == '\n' || c == ',' || c == '-';
}

static DWORD hex_decode_complex(const char *text, DWORD length, DWORD mode, BYTE *out, DWORD *size)
{
    static const int steps_ascii[] = {1, 2, 3, -1}, steps_addr[] = {0, 1, 3, -1}, steps_both[] = {0, 1, 2, 3, -1};
    const int *steps = mode == CRYPT_STRING_HEXASCII  ? steps_ascii
                       : mode == CRYPT_STRING_HEXADDR ? steps_addr
                                                      : steps_both;
    const char *p = text, *end = text + length;
    DWORD capacity = out ? *size : 0, total = 0, line = 0, previous = 0;
    BOOL overflow = FALSE, broken = FALSE;
    const int *step = steps;
    while (p < end) {
        int kind = *step++;
        if (kind < 0) {
            step = steps;
            continue;
        }
        if (kind == 0) {
            while (p < end && hex_white(*p))
                p++;
            if (p >= end)
                break;
            DWORD value = 0, digits = 0;
            while (digits <= 8 && p < end && hex_digit(*p) >= 0) {
                value = (value << 4) | (DWORD)hex_digit(*p++);
                digits++;
            }
            if (digits < 4 || digits > 8)
                return ERROR_INVALID_DATA;
            if (!broken && previous && previous + 16 != value)
                return ERROR_INVALID_DATA;
            previous = value;
        } else if (kind == 1) {
            for (DWORD i = 0; i < 16; i++) {
                while (p < end && hex_skip(*p))
                    p++;
                if (p >= end)
                    goto done;
                const char *q = p;
                DWORD digits = 0, value = 0;
                while (digits <= 2 && q < end && hex_digit(*q) >= 0) {
                    value = (value << 4) | (DWORD)hex_digit(*q++);
                    digits++;
                }
                if (digits != 2) {
                    broken = TRUE;
                    break;
                }
                p = q;
                if (out) {
                    if (total < capacity)
                        out[total] = (BYTE)value;
                    else
                        overflow = TRUE;
                }
                total++;
                line++;
            }
        } else if (kind == 2) {
            while (p < end && hex_white(*p))
                p++;
            if (line) {
                if (p >= end)
                    break;
                for (DWORD i = 0; i < line && p < end && *p >= 0x20 && *p <= 0x7E; i++)
                    p++;
            }
        } else {
            while (p < end && hex_white(*p))
                p++;
            if (p >= end)
                break;
            if (*p != '\n')
                return ERROR_INVALID_DATA;
            p++;
            line = 0;
            step = steps;
        }
    }
done:
    if (!total)
        return ERROR_INVALID_DATA;
    *size = total;
    return overflow ? ERROR_MORE_DATA : 0;
}

/* decodes one format; out may be NULL to measure. */
static DWORD decode_format(const char *text, DWORD length, DWORD format, DWORD extra, BYTE *out, DWORD *size,
                           DWORD *skip)
{
    BOOL strict = (extra & CRYPT_STRING_STRICT) != 0, percent = (extra & CRYPT_STRING_PERCENTESCAPE) != 0;
    switch (format) {
    case CRYPT_STRING_BASE64:
    case CRYPT_STRING_BASE64URI:
        return base64_decode(text, length, strict, format == CRYPT_STRING_BASE64URI, percent, out, size);
    case CRYPT_STRING_BASE64HEADER:
    case CRYPT_STRING_BASE64REQUESTHEADER:
    case CRYPT_STRING_BASE64X509CRLHEADER: {
        DWORD marker;
        int begin = find_marker(text, length, TRUE, &marker);
        if (begin < 0)
            return ERROR_INVALID_DATA;
        const char *body = text + begin + marker;
        DWORD body_length = length - (DWORD)begin - marker, end_marker;
        int finish = find_marker(body, body_length, FALSE, &end_marker);
        if (finish < 0)
            return ERROR_INVALID_DATA;
        if (skip)
            *skip = (DWORD)begin;
        return base64_decode(body, (DWORD)finish, strict, FALSE, percent, out, size);
    }
    case CRYPT_STRING_BINARY:
        if (out) {
            if (*size < length)
                return ERROR_MORE_DATA;
            memcpy(out, text, length);
        }
        *size = length;
        return 0;
    case CRYPT_STRING_HEX:
    case CRYPT_STRING_HEXRAW:
        return hex_decode_simple(text, length, out, size);
    case CRYPT_STRING_HEXASCII:
    case CRYPT_STRING_HEXADDR:
    case CRYPT_STRING_HEXASCIIADDR:
        return hex_decode_complex(text, length, format, out, size);
    }
    return ERROR_INVALID_DATA;
}

/* base64 without the strict flag guesses the alphabet from the first special character. */
static BOOL looks_like_uri(const char *text, DWORD length, DWORD format, BOOL percent)
{
    const char *p = text, *end = text + length;
    while (p < end && *p) {
        char c = *p;
        if (c == '%' && percent) {
            const char *q = p;
            int decoded = next_char(&q, end, TRUE);
            if (decoded >= 0) {
                c = (char)decoded;
                p = q - 1;
            }
        }
        if (c == '+' || c == '/' || c == '=')
            return FALSE;
        if (c == '-' || c == '_')
            return TRUE;
        p++;
    }
    return format == CRYPT_STRING_BASE64URI;
}

BOOL WINAPI CryptStringToBinaryA(LPCSTR pszString, DWORD cchString, DWORD dwFlags, BYTE *pbBinary,
                                 DWORD *pcbBinary, DWORD *pdwSkip, DWORD *pdwFlags)
{
    if (!pszString || !pcbBinary)
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!cchString)
        cchString = (DWORD)strlen(pszString);
    DWORD extra = dwFlags & (CRYPT_STRING_STRICT | CRYPT_STRING_PERCENTESCAPE);
    DWORD format = dwFlags & ~(CRYPT_STRING_STRICT | CRYPT_STRING_PERCENTESCAPE);
    if (pdwSkip)
        *pdwSkip = 0;
    if (pdwFlags)
        *pdwFlags = 0;
    if (!pbBinary)
        *pcbBinary = 0;
    static const DWORD any_base64[] = {CRYPT_STRING_BASE64HEADER, CRYPT_STRING_BASE64, CRYPT_STRING_BASE64URI};
    static const DWORD any[] = {CRYPT_STRING_BASE64HEADER, CRYPT_STRING_BASE64, CRYPT_STRING_BASE64URI,
                                CRYPT_STRING_BINARY};
    static const DWORD any_hex[] = {CRYPT_STRING_HEXADDR, CRYPT_STRING_HEXASCIIADDR, CRYPT_STRING_HEX,
                                    CRYPT_STRING_HEXRAW, CRYPT_STRING_HEXASCII};
    static const DWORD base64_first[] = {CRYPT_STRING_BASE64, CRYPT_STRING_BASE64URI};
    static const DWORD uri_first[] = {CRYPT_STRING_BASE64URI, CRYPT_STRING_BASE64};
    const DWORD *formats = &format;
    DWORD count = 1;
    if (format == CRYPT_STRING_BASE64_ANY) {
        formats = any_base64;
        count = RTL_NUMBER_OF(any_base64);
    } else if (format == CRYPT_STRING_ANY) {
        formats = any;
        count = RTL_NUMBER_OF(any);
    } else if (format == CRYPT_STRING_HEX_ANY) {
        formats = any_hex;
        count = RTL_NUMBER_OF(any_hex);
    } else if (!(extra & CRYPT_STRING_STRICT) && (format == CRYPT_STRING_BASE64 || format == CRYPT_STRING_BASE64URI)) {
        BOOL uri = looks_like_uri(pszString, cchString, format, (extra & CRYPT_STRING_PERCENTESCAPE) != 0);
        formats = uri ? uri_first : base64_first;
        count = 2;
    }
    DWORD error = ERROR_INVALID_DATA;
    for (DWORD i = 0; i < count; i++) {
        DWORD size = pbBinary ? *pcbBinary : 0, skip = 0;
        error = decode_format(pszString, cchString, formats[i], extra, pbBinary, &size, &skip);
        if (pdwSkip)
            *pdwSkip = skip;
        if (!error) {
            *pcbBinary = size;
            if (pdwFlags)
                *pdwFlags = formats[i];
            return TRUE;
        }
    }
    return CrFail(error);
}

BOOL WINAPI CryptStringToBinaryW(LPCWSTR pszString, DWORD cchString, DWORD dwFlags, BYTE *pbBinary,
                                 DWORD *pcbBinary, DWORD *pdwSkip, DWORD *pdwFlags)
{
    if (!pszString || !pcbBinary)
        return CrFail(ERROR_INVALID_PARAMETER);
    DWORD format = dwFlags == CRYPT_STRING_ANY ? CRYPT_STRING_BASE64_ANY : dwFlags;
    if ((format & ~(CRYPT_STRING_STRICT | CRYPT_STRING_PERCENTESCAPE)) == CRYPT_STRING_BINARY)
        return CryptStringToBinaryA((LPCSTR)pszString, cchString, format, pbBinary, pcbBinary, pdwSkip, pdwFlags);
    if (!cchString)
        cchString = (DWORD)wcslen(pszString);
    if (cchString && pszString[0] == 0xFEFF) {
        pszString++;
        cchString--;
    }
    char *narrow = LocalAlloc(LMEM_FIXED, cchString + 1);
    if (!narrow)
        return CrFail(ERROR_OUTOFMEMORY);
    if (cchString &&
        !WideCharToMultiByte(CP_ACP, 0, pszString, (int)cchString, narrow, (int)cchString, nullptr, nullptr)) {
        LocalFree(narrow);
        return FALSE;
    }
    narrow[cchString] = 0;
    BOOL ok = cchString ? CryptStringToBinaryA(narrow, cchString, format, pbBinary, pcbBinary, pdwSkip, pdwFlags)
                        : CrFail(ERROR_INVALID_DATA);
    if (!ok && dwFlags == CRYPT_STRING_ANY)
        SetLastError(ERROR_INVALID_USER_BUFFER);
    LocalFree(narrow);
    return ok;
}
