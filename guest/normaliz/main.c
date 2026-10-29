/*
 * normaliz.dll: internationalized domain names and Unicode normalization. names are converted label by label with
 * punycode, mapping upper case letters to lower case for the nameprep step. there are no normalization tables here, so
 * NormalizeString and IsNormalizedString work on text that is plain ASCII, which every form leaves as it is.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define MAX_LABEL 63
#define MAX_NAME 255

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

static WCHAR digit_of(unsigned d)
{
    return (WCHAR)(d < 26 ? L'a' + d : L'0' + (d - 26));
}

static int value_of(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0' + 26;
    if (c >= L'a' && c <= L'z')
        return c - L'a';
    if (c >= L'A' && c <= L'Z')
        return c - L'A';
    return -1;
}

/* the label as code points, joining surrogate pairs. */
static int to_code_points(const WCHAR *label, int length, unsigned *out)
{
    int count = 0;
    for (int i = 0; i < length; i++) {
        unsigned c = label[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < length && label[i + 1] >= 0xDC00 && label[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (label[i + 1] - 0xDC00);
            i++;
        }
        out[count++] = c;
    }
    return count;
}

/* the punycode of a label, or -1 when it does not fit. */
static int punycode_encode(const unsigned *input, int length, WCHAR *out, int room)
{
    int at = 0;
    unsigned basic = 0;
    for (int i = 0; i < length; i++)
        if (input[i] < 0x80) {
            if (at >= room)
                return -1;
            out[at++] = (WCHAR)input[i];
            basic++;
        }
    unsigned handled = basic;
    if (basic) {
        if (at >= room)
            return -1;
        out[at++] = L'-';
    }
    unsigned n = INITIAL_N, delta = 0, bias = INITIAL_BIAS;
    while (handled < (unsigned)length) {
        unsigned m = 0xFFFFFFFF;
        for (int i = 0; i < length; i++)
            if (input[i] >= n && input[i] < m)
                m = input[i];
        if ((m - n) > (0xFFFFFFFFu - delta) / (handled + 1))
            return -1;
        delta += (m - n) * (handled + 1);
        n = m;
        for (int i = 0; i < length; i++) {
            if (input[i] < n && ++delta == 0)
                return -1;
            if (input[i] != n)
                continue;
            unsigned q = delta;
            for (unsigned k = BASE;; k += BASE) {
                unsigned t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
                if (q < t)
                    break;
                if (at >= room)
                    return -1;
                out[at++] = digit_of(t + (q - t) % (BASE - t));
                q = (q - t) / (BASE - t);
            }
            if (at >= room)
                return -1;
            out[at++] = digit_of(q);
            bias = adapt(delta, handled + 1, handled == basic);
            delta = 0;
            handled++;
        }
        delta++;
        n++;
    }
    return at;
}

/* the code points of a punycode label, or -1 when it is not one. */
static int punycode_decode(const WCHAR *input, int length, unsigned *out, int room)
{
    int count = 0, start = 0;
    for (int i = length - 1; i > 0; i--)
        if (input[i] == L'-') {
            start = i;
            break;
        }
    for (int i = 0; i < start; i++) {
        if (input[i] >= 0x80 || count >= room)
            return -1;
        out[count++] = input[i];
    }
    unsigned n = INITIAL_N, i = 0, bias = INITIAL_BIAS;
    for (int at = start ? start + 1 : 0; at < length;) {
        unsigned old = i, w = 1;
        for (unsigned k = BASE;; k += BASE) {
            if (at >= length)
                return -1;
            int digit = value_of(input[at++]);
            if (digit < 0 || (unsigned)digit > (0xFFFFFFFFu - i) / w)
                return -1;
            i += digit * w;
            unsigned t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
            if ((unsigned)digit < t)
                break;
            if (w > 0xFFFFFFFFu / (BASE - t))
                return -1;
            w *= BASE - t;
        }
        bias = adapt(i - old, count + 1, old == 0);
        if (i / (count + 1) > 0xFFFFFFFFu - n)
            return -1;
        n += i / (count + 1);
        i %= count + 1;
        if (count >= room || n > 0x10FFFF)
            return -1;
        for (int j = count; j > (int)i; j--)
            out[j] = out[j - 1];
        out[i++] = n;
        count++;
    }
    return count;
}

static BOOL is_dot(WCHAR c)
{
    return c == L'.' || c == 0x3002 || c == 0xFF0E || c == 0xFF61;
}

static BOOL is_ascii(const WCHAR *s, int length)
{
    for (int i = 0; i < length; i++)
        if (s[i] >= 0x80)
            return FALSE;
    return TRUE;
}

/* the length of the text, which a count of -1 means up to the terminator, with the terminator counted if it was asked. */
static int text_length(const WCHAR *text, int count, BOOL *terminated)
{
    *terminated = count == -1;
    if (count == -1) {
        int n = 0;
        while (text[n])
            n++;
        return n;
    }
    return count;
}

/* what is left of the label after the mapping step. */
static void map_label(WCHAR *label, int length)
{
    for (int i = 0; i < length; i++)
        if (label[i] >= L'A' && label[i] <= L'Z')
            label[i] = (WCHAR)(label[i] + (L'a' - L'A'));
        else if (label[i] >= 0x80)
            LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, &label[i], 1, &label[i], 1);
}

/* converts a name label by label, with to_ascii telling the direction. */
static int convert(DWORD flags, const WCHAR *name, int count, WCHAR *out, int room, BOOL to_ascii, BOOL map_only)
{
    if (flags & ~(DWORD)(IDN_ALLOW_UNASSIGNED | IDN_USE_STD3_ASCII_RULES) || !name || count < -1 || (room && !out) ||
        room < 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL terminated;
    int length = text_length(name, count, &terminated);
    if (!length) {
        SetLastError(ERROR_INVALID_NAME);
        return 0;
    }
    WCHAR *result = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length * 8 + 16) * sizeof(WCHAR));
    unsigned *points = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(unsigned));
    if (!result || !points) {
        HeapFree(GetProcessHeap(), 0, result);
        HeapFree(GetProcessHeap(), 0, points);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    int written = 0, error = 0;
    for (int start = 0; start <= length && !error;) {
        int end = start;
        while (end < length && !is_dot(name[end]))
            end++;
        int label_length = end - start;
        /* an empty label is allowed only at the end, as the root. */
        if (!label_length && !(end == length && start > 0)) {
            error = ERROR_INVALID_NAME;
            break;
        }
        WCHAR *label = result + written;
        int made = 0;
        if (map_only) {
            memcpy(label, name + start, label_length * sizeof(WCHAR));
            map_label(label, label_length);
            made = label_length;
        } else if (to_ascii) {
            memcpy(label, name + start, label_length * sizeof(WCHAR));
            map_label(label, label_length);
            if (is_ascii(label, label_length)) {
                made = label_length;
            } else {
                int count_points = to_code_points(label, label_length, points);
                WCHAR encoded[MAX_LABEL * 4];
                int n = punycode_encode(points, count_points, encoded, RTL_NUMBER_OF(encoded));
                if (n < 0) {
                    error = ERROR_INVALID_NAME;
                    break;
                }
                memcpy(label, L"xn--", 4 * sizeof(WCHAR));
                memcpy(label + 4, encoded, n * sizeof(WCHAR));
                made = 4 + n;
            }
            if (made > MAX_LABEL) {
                error = ERROR_INVALID_NAME;
                break;
            }
        } else {
            if (label_length > 4 && (name[start] | 0x20) == L'x' && (name[start + 1] | 0x20) == L'n' &&
                name[start + 2] == L'-' && name[start + 3] == L'-') {
                int n = punycode_decode(name + start + 4, label_length - 4, points, length);
                if (n < 0) {
                    error = ERROR_INVALID_NAME;
                    break;
                }
                for (int i = 0; i < n; i++) {
                    if (points[i] >= 0x10000) {
                        label[made++] = (WCHAR)(0xD800 + ((points[i] - 0x10000) >> 10));
                        label[made++] = (WCHAR)(0xDC00 + ((points[i] - 0x10000) & 0x3FF));
                    } else {
                        label[made++] = (WCHAR)points[i];
                    }
                }
            } else {
                memcpy(label, name + start, label_length * sizeof(WCHAR));
                made = label_length;
            }
        }
        written += made;
        if (end < length)
            result[written++] = L'.';
        start = end + 1;
    }
    if (!error && to_ascii && !map_only && written > MAX_NAME + 1)
        error = ERROR_INVALID_NAME;
    int total = written + (terminated ? 1 : 0);
    if (!error && room && total > room)
        error = ERROR_INSUFFICIENT_BUFFER;
    if (error) {
        SetLastError(error);
        total = 0;
    } else if (room) {
        memcpy(out, result, written * sizeof(WCHAR));
        if (terminated)
            out[written] = 0;
    }
    HeapFree(GetProcessHeap(), 0, result);
    HeapFree(GetProcessHeap(), 0, points);
    return total;
}

int WINAPI NormIdnToAscii(DWORD flags, LPCWSTR name, int count, LPWSTR out, int room)
{
    return convert(flags, name, count, out, room, TRUE, FALSE);
}

int WINAPI NormIdnToUnicode(DWORD flags, LPCWSTR name, int count, LPWSTR out, int room)
{
    return convert(flags, name, count, out, room, FALSE, FALSE);
}

int WINAPI NormIdnToNameprepUnicode(DWORD flags, LPCWSTR name, int count, LPWSTR out, int room)
{
    return convert(flags, name, count, out, room, TRUE, TRUE);
}

/* ------------------------------------------------------------------ normalization */

#define NORMALIZATION_C 0x1
#define NORMALIZATION_D 0x2
#define NORMALIZATION_KC 0x5
#define NORMALIZATION_KD 0x6

BOOL WINAPI NormIsNormalizedString(int form, LPCWSTR text, int count)
{
    if (form != NORMALIZATION_C && form != NORMALIZATION_D && form != NORMALIZATION_KC && form != NORMALIZATION_KD) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!text || count == 0 || count < -1) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL terminated;
    int length = text_length(text, count, &terminated);
    if (!is_ascii(text, length)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

int WINAPI NormNormalizeString(int form, LPCWSTR text, int count, LPWSTR out, int room)
{
    if (form != NORMALIZATION_C && form != NORMALIZATION_D && form != NORMALIZATION_KC && form != NORMALIZATION_KD) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!text || count == 0 || count < -1 || room < 0 || (room && !out)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL terminated;
    int length = text_length(text, count, &terminated);
    if (!is_ascii(text, length)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    int total = length + (terminated ? 1 : 0);
    if (!room)
        return total;
    if (room < total) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(out, text, total * sizeof(WCHAR));
    SetLastError(ERROR_SUCCESS);
    return total;
}
