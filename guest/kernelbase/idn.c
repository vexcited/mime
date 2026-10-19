/*
 * internationalized domain names: IdnToAscii turns a name with non-ASCII labels into the ASCII form with the xn-- prefix
 * and punycode of RFC 3492, IdnToUnicode turns it back and IdnToNameprepUnicode only maps and normalizes the name. The
 * mapping of nameprep is NFKC and lower case here, which is Windows' for the characters of the usual scripts; the
 * prohibited characters, the bidirectional rules and the unassigned code points are not looked at.
 *
 * Flags: IDN_ALLOW_UNASSIGNED 1 and IDN_USE_STD3_ASCII_RULES 2 for the three, IDN_EMAIL_ADDRESS 4 for the first two and
 * IDN_RAW_PUNYCODE 8 for IdnToUnicode. The errors are the ones the native functions give: ERROR_INVALID_FLAGS,
 * ERROR_INVALID_PARAMETER, ERROR_INVALID_NAME for a name that cannot be, and ERROR_INSUFFICIENT_BUFFER.
 */
#include "kernelbase_private.h"

#define IDN_MAX_LABEL 63
#define IDN_MAX_NAME 255
#define IDN_WORK 1024

/* ------------------------------------------------------------------ punycode */

enum { BASE = 36, TMIN = 1, TMAX = 26, SKEW = 38, DAMP = 700, INITIAL_BIAS = 72, INITIAL_N = 128 };

static unsigned adapt(unsigned delta, unsigned points, BOOL first)
{
    delta = first ? delta / DAMP : delta / 2;
    delta += delta / points;
    unsigned k = 0;
    while (delta > ((BASE - TMIN) * TMAX) / 2) {
        delta /= BASE - TMIN;
        k += BASE;
    }
    return k + (BASE - TMIN + 1) * delta / (delta + SKEW);
}

static WCHAR digit_char(unsigned d)
{
    return (WCHAR)(d < 26 ? 'a' + d : '0' + d - 26);
}

static int digit_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0' + 26;
    if (c >= 'a' && c <= 'z')
        return c - 'a';
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    return -1;
}

/* the code points of the label as utf-16 are encoded; returns the length written or -1 when it does not fit. */
static int punycode_encode(const WCHAR *label, int length, WCHAR *out, int room)
{
    unsigned cp[IDN_WORK];
    int count = 0;
    for (int i = 0; i < length && count < IDN_WORK; i++) {
        unsigned c = label[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && label[i + 1] >= 0xDC00 && label[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (label[++i] - 0xDC00);
        cp[count++] = c;
    }
    int at = 0;
    for (int i = 0; i < count; i++) {
        if (cp[i] < 0x80) {
            if (at >= room)
                return -1;
            out[at++] = (WCHAR)cp[i];
        }
    }
    unsigned basic = (unsigned)at, handled = basic;
    if (basic) {
        if (at >= room)
            return -1;
        out[at++] = '-';
    }
    unsigned n = INITIAL_N, delta = 0, bias = INITIAL_BIAS;
    while (handled < (unsigned)count) {
        unsigned m = 0xFFFFFFFFu;
        for (int i = 0; i < count; i++)
            if (cp[i] >= n && cp[i] < m)
                m = cp[i];
        if (m - n > (0xFFFFFFFFu - delta) / (handled + 1))
            return -1;
        delta += (m - n) * (handled + 1);
        n = m;
        for (int i = 0; i < count; i++) {
            if (cp[i] < n && ++delta == 0)
                return -1;
            if (cp[i] == n) {
                unsigned q = delta;
                for (unsigned k = BASE;; k += BASE) {
                    unsigned t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
                    if (q < t)
                        break;
                    if (at >= room)
                        return -1;
                    out[at++] = digit_char(t + (q - t) % (BASE - t));
                    q = (q - t) / (BASE - t);
                }
                if (at >= room)
                    return -1;
                out[at++] = digit_char(q);
                bias = adapt(delta, handled + 1, handled == basic);
                delta = 0;
                handled++;
            }
        }
        delta++;
        n++;
    }
    return at;
}

/* the label after the xn-- prefix as utf-16; -1 for a malformed one or one that does not fit. */
static int punycode_decode(const WCHAR *in, int length, WCHAR *out, int room)
{
    unsigned cp[IDN_WORK];
    int count = 0;
    int basic = 0;
    for (int i = 0; i < length; i++)
        if (in[i] == '-')
            basic = i;
    for (int i = 0; i < basic; i++) {
        if (in[i] >= 0x80 || count >= IDN_WORK)
            return -1;
        cp[count++] = in[i];
    }
    unsigned n = INITIAL_N, i = 0, bias = INITIAL_BIAS;
    for (int at = basic ? basic + 1 : 0; at < length; count++) {
        unsigned old = i, w = 1;
        for (unsigned k = BASE;; k += BASE) {
            if (at >= length)
                return -1;
            int d = digit_value(in[at++]);
            if (d < 0 || (unsigned)d > (0xFFFFFFFFu - i) / w)
                return -1;
            i += (unsigned)d * w;
            unsigned t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
            if ((unsigned)d < t)
                break;
            if (w > 0xFFFFFFFFu / (BASE - t))
                return -1;
            w *= BASE - t;
        }
        bias = adapt(i - old, (unsigned)count + 1, old == 0);
        if (i / ((unsigned)count + 1) > 0xFFFFFFFFu - n)
            return -1;
        n += i / ((unsigned)count + 1);
        i %= (unsigned)count + 1;
        if (n < 0x80 || n > 0x10FFFF || (n >= 0xD800 && n < 0xE000) || count >= IDN_WORK)
            return -1;
        memmove(cp + i + 1, cp + i, (size_t)(count - i) * sizeof(unsigned));
        cp[i++] = n;
    }
    int written = 0;
    for (int j = 0; j < count; j++) {
        unsigned c = cp[j];
        int units = c >= 0x10000 ? 2 : 1;
        if (written + units > room)
            return -1;
        if (units == 2) {
            c -= 0x10000;
            out[written++] = (WCHAR)(0xD800 + (c >> 10));
            out[written++] = (WCHAR)(0xDC00 + (c & 0x3FF));
        } else {
            out[written++] = (WCHAR)c;
        }
    }
    return written;
}

/* ------------------------------------------------------------------ names */

static BOOL is_dot(WCHAR c)
{
    return c == '.' || c == 0x3002 || c == 0xFF0E || c == 0xFF61;
}

static WCHAR lower(WCHAR c)
{
    WCHAR out = c;
    if (c < 0x80)
        return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c;
    if (LCMapStringEx(nullptr, LCMAP_LOWERCASE, &c, 1, &out, 1, nullptr, nullptr, 0) != 1)
        return c;
    return out;
}

/* the rules for the ascii in a label when std3 is asked: letters, digits and hyphens, none at an end. */
static BOOL std3_label(const WCHAR *label, int length, BOOL email_local)
{
    for (int i = 0; i < length; i++) {
        WCHAR c = label[i];
        BOOL ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c >= 0x80;
        if (!ok && !(email_local && c > 0x20 && c < 0x7F))
            return FALSE;
    }
    return !length || email_local || (label[0] != '-' && label[length - 1] != '-');
}

static BOOL all_ascii(const WCHAR *s, int length)
{
    for (int i = 0; i < length; i++)
        if (s[i] >= 0x80)
            return FALSE;
    return TRUE;
}

/* the mapping of nameprep: compatibility composed form, lower case, and every dot written the same. */
static int nameprep(const WCHAR *src, int length, WCHAR *dst, int room)
{
    if (!length)
        return 0;
    int n = NormalizeString(5, src, length, dst, room);
    if (n <= 0)
        return -1;
    for (int i = 0; i < n; i++) {
        dst[i] = lower(dst[i]);
        if (is_dot(dst[i]))
            dst[i] = '.';
    }
    return n;
}

/* the last at sign of an address and what follows is the domain; before it the text is kept as it is. */
static int email_split(const WCHAR *s, int length)
{
    for (int i = length - 1; i >= 0; i--)
        if (s[i] == '@')
            return i + 1;
    return 0;
}

typedef BOOL(*LABEL_FUNCTION)(const WCHAR *label, int length, WCHAR *out, int *at, int room, DWORD flags);

static BOOL put_text(WCHAR *out, int *at, int room, const WCHAR *text, int length)
{
    if (*at + length > room)
        return FALSE;
    memcpy(out + *at, text, (size_t)length * sizeof(WCHAR));
    *at += length;
    return TRUE;
}

/* the name label by label: the dots are kept and an empty label is only allowed before the final dot. */
static int convert_name(const WCHAR *name, int length, WCHAR *out, int room, DWORD flags, LABEL_FUNCTION convert)
{
    int at = 0, start = 0;
    BOOL ok = TRUE;
    for (int i = 0; i <= length && ok; i++) {
        if (i < length && !is_dot(name[i]))
            continue;
        int label = i - start;
        if (!label && !(i == length && start > 0) && !(i == length && !length))
            return -1;
        if (label && !convert(name + start, label, out, &at, room, flags))
            return at < 0 ? -2 : -1;
        if (i < length && !put_text(out, &at, room, L".", 1))
            return -2;
        start = i + 1;
    }
    return at;
}

static BOOL label_to_ascii(const WCHAR *label, int length, WCHAR *out, int *at, int room, DWORD flags)
{
    BOOL std3 = (flags & IDN_USE_STD3_ASCII_RULES) != 0;
    if (all_ascii(label, length)) {
        if (length > IDN_MAX_LABEL || (std3 && !std3_label(label, length, FALSE)))
            return FALSE;
        if (!put_text(out, at, room, label, length)) {
            *at = -1;
            return FALSE;
        }
        return TRUE;
    }
    WCHAR mapped[IDN_WORK], encoded[IDN_WORK];
    int n = nameprep(label, length, mapped, IDN_WORK);
    if (n <= 0 || (std3 && !std3_label(mapped, n, FALSE)))
        return FALSE;
    int units;
    if (all_ascii(mapped, n)) {
        units = n;
        memcpy(encoded, mapped, (size_t)n * sizeof(WCHAR));
    } else {
        memcpy(encoded, L"xn--", 4 * sizeof(WCHAR));
        int p = punycode_encode(mapped, n, encoded + 4, IDN_WORK - 4);
        if (p < 0)
            return FALSE;
        units = p + 4;
    }
    if (units > IDN_MAX_LABEL)
        return FALSE;
    if (!put_text(out, at, room, encoded, units)) {
        *at = -1;
        return FALSE;
    }
    return TRUE;
}

static BOOL label_to_unicode(const WCHAR *label, int length, WCHAR *out, int *at, int room, DWORD flags)
{
    if (length > 4 && !_wcsnicmp(label, L"xn--", 4)) {
        WCHAR decoded[IDN_WORK];
        int n = punycode_decode(label + 4, length - 4, decoded, IDN_WORK);
        if (n > 0) {
            /* a label that does not encode back to what was given was not made by an encoder. */
            WCHAR again[IDN_WORK];
            int p = punycode_encode(decoded, n, again, IDN_WORK);
            if (p == length - 4 && !_wcsnicmp(again, label + 4, (size_t)p) && !all_ascii(decoded, n)) {
                if (!put_text(out, at, room, decoded, n)) {
                    *at = -1;
                    return FALSE;
                }
                return TRUE;
            }
        }
        if (flags & IDN_RAW_PUNYCODE)
            return FALSE;
    }
    if (!put_text(out, at, room, label, length)) {
        *at = -1;
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the functions */

/* the arguments checked as the native workers check them; the length is that of the name without a terminator. */
static BOOL idn_arguments(DWORD flags, DWORD allowed, LPCWSTR src, int *length, BOOL *terminated, LPWSTR dst, int dst_len)
{
    if (flags & ~allowed) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    if (!src || *length < -1 || dst_len < 0 || (dst_len > 0 && !dst)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (*length == -1)
        *length = (int)wcsnlen(src, 0x7FFFFFFF) + 1;
    *terminated = FALSE;
    if (*length > 0 && src[*length - 1] == 0) {
        *terminated = TRUE;
        --*length;
    }
    if (!*length) {
        KbSetError(ERROR_INVALID_NAME);
        return FALSE;
    }
    return TRUE;
}

/* the result put in the caller's buffer, or its size when there is none. */
static int idn_finish(const WCHAR *result, int length, BOOL terminated, LPWSTR dst, int dst_len)
{
    int total = length + (terminated ? 1 : 0);
    if (!dst || !dst_len)
        return total;
    if (total > dst_len) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(dst, result, (size_t)length * sizeof(WCHAR));
    if (terminated)
        dst[length] = 0;
    return total;
}

static int idn_run(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len, DWORD allowed, LABEL_FUNCTION convert,
                   BOOL map_only)
{
    BOOL terminated;
    if (!idn_arguments(flags, allowed, src, &src_len, &terminated, dst, dst_len))
        return 0;
    WCHAR *work = KbAlloc(2 * IDN_WORK * sizeof(WCHAR));
    if (!work) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    WCHAR *result = work + IDN_WORK;
    int length;
    int prefix = 0;
    /* an address keeps the part before its last at sign as it is, and only the domain is converted. */
    if (flags & IDN_EMAIL_ADDRESS) {
        prefix = email_split(src, src_len);
        if (prefix > IDN_WORK / 2) {
            KbFree(work);
            KbSetError(ERROR_INVALID_NAME);
            return 0;
        }
        memcpy(result, src, (size_t)prefix * sizeof(WCHAR));
    }
    if (src_len - prefix > IDN_MAX_NAME + 1) {
        KbFree(work);
        KbSetError(ERROR_INVALID_NAME);
        return 0;
    }
    if (map_only) {
        length = nameprep(src + prefix, src_len - prefix, result + prefix, IDN_WORK - prefix);
        if (length < 0 || ((flags & IDN_USE_STD3_ASCII_RULES) && !std3_label(result + prefix, length, TRUE)))
            length = -1;
    } else {
        length = convert_name(src + prefix, src_len - prefix, result + prefix, IDN_WORK - prefix, flags, convert);
    }
    int r = 0;
    if (length < 0) {
        KbSetError(length == -2 ? ERROR_INSUFFICIENT_BUFFER : ERROR_INVALID_NAME);
    } else if (prefix + length > IDN_MAX_NAME + prefix) {
        KbSetError(ERROR_INVALID_NAME);
    } else {
        r = idn_finish(result, prefix + length, terminated, dst, dst_len);
    }
    KbFree(work);
    return r;
}

int WINAPI IdnToAscii(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    return idn_run(flags, src, src_len, dst, dst_len, 7, label_to_ascii, FALSE);
}

int WINAPI IdnToUnicode(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    return idn_run(flags, src, src_len, dst, dst_len, 0xF, label_to_unicode, FALSE);
}

int WINAPI IdnToNameprepUnicode(DWORD flags, LPCWSTR src, int src_len, LPWSTR dst, int dst_len)
{
    return idn_run(flags, src, src_len, dst, dst_len, 7, nullptr, TRUE);
}
