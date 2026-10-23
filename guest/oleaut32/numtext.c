/*
 * DECIMAL checks and conversions, and the text VarBstrFrom* writes for integers, floats and decimals, with or without
 * the locale's number formatting.
 */
#include "oleaut32_private.h"
#include "dtoa.h"
#include <mime/win32/winnls.h>

/* ------------------------------------------------------------------ decimals */

#define DECIMAL_NEG_BIT 0x80
#define DECIMAL_MAX_SCALE 28

/* scale is at most 28 and sign holds nothing but the negative bit. */
BOOL AutDecimalValid(const DECIMAL *d)
{
    return d->scale <= DECIMAL_MAX_SCALE && (d->sign & ~DECIMAL_NEG_BIT) == 0;
}

/* the 96-bit mantissa as three words, low first. */
static void decimal_words(const DECIMAL *d, ULONG *w)
{
    w[0] = (ULONG)d->Lo64;
    w[1] = (ULONG)(d->Lo64 >> 32);
    w[2] = d->Hi32;
}

/* w / 10, returns the remainder. */
static ULONG words_div10(ULONG *w)
{
    ULONGLONG rem = 0;
    for (int i = 2; i >= 0; i--) {
        ULONGLONG t = (rem << 32) | w[i];
        w[i] = (ULONG)(t / 10);
        rem = t % 10;
    }
    return (ULONG)rem;
}

static BOOL words_zero(const ULONG *w)
{
    return !w[0] && !w[1] && !w[2];
}

/* most significant digit first, returns the count, 0 for zero. */
static int words_digits(const ULONG *words, char *out)
{
    ULONG w[3] = {words[0], words[1], words[2]};
    char reversed[32];
    int count = 0;
    while (!words_zero(w))
        reversed[count++] = (char)('0' + words_div10(w));
    for (int i = 0; i < count; i++)
        out[i] = reversed[count - 1 - i];
    return count;
}

/* each power rounded once to a double, like a table of constants. */
static const double g_ten_powers[DECIMAL_MAX_SCALE + 1] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14,
    1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28};

/* like windows: sum the three words as doubles, then divide by the power, which can round the last bit. */
double AutDecimalToDouble(const DECIMAL *d)
{
    double v = (double)(ULONG)d->Lo64 + (double)(ULONG)(d->Lo64 >> 32) * 4294967296.0 +
               (double)d->Hi32 * 4294967296.0 * 4294967296.0;
    v /= g_ten_powers[d->scale];
    return (d->sign & DECIMAL_NEG_BIT) ? -v : v;
}

/* rounds half to even, false when the result does not fit 64 bits. */
BOOL AutDecimalToInteger(const DECIMAL *d, BOOL *negative, ULONGLONG *magnitude)
{
    ULONG w[3];
    decimal_words(d, w);
    ULONG last = 0;
    BOOL below = FALSE;
    for (int i = 0; i < d->scale; i++) {
        if (last)
            below = TRUE;
        last = words_div10(w);
    }
    if (d->scale && (last > 5 || (last == 5 && (below || (w[0] & 1))))) {
        for (int i = 0; i < 3 && ++w[i] == 0; i++) {
        }
    }
    *negative = (d->sign & DECIMAL_NEG_BIT) != 0;
    if (w[2])
        return FALSE;
    *magnitude = ((ULONGLONG)w[1] << 32) | w[0];
    return TRUE;
}

/* ------------------------------------------------------------------ locale settings */

typedef struct {
    ULONG Override;
    WCHAR Decimal[8];
    WCHAR Thousand[8];
    UINT Digits;
    UINT LeadingZero;
    UINT NegativeOrder;
    UINT Grouping;
} NUMBER_LOCALE;

static ULONG locale_override(ULONG flags)
{
    return (flags | g_process_nls_flags) & LOCALE_NOUSEROVERRIDE;
}

static UINT locale_number(LCID lcid, LCTYPE type, ULONG override, UINT fallback)
{
    DWORD v;
    if (!GetLocaleInfoW(lcid, type | override | LOCALE_RETURN_NUMBER, (WCHAR *)&v, sizeof(v) / sizeof(WCHAR)))
        return fallback;
    return v;
}

static void load_number_locale(LCID lcid, ULONG flags, NUMBER_LOCALE *l)
{
    l->Override = locale_override(flags);
    if (!GetLocaleInfoW(lcid, LOCALE_SDECIMAL | l->Override, l->Decimal, RTL_NUMBER_OF(l->Decimal)))
        wcscpy(l->Decimal, L".");
    if (!GetLocaleInfoW(lcid, LOCALE_STHOUSAND | l->Override, l->Thousand, RTL_NUMBER_OF(l->Thousand)))
        wcscpy(l->Thousand, L",");
    l->Digits = locale_number(lcid, LOCALE_IDIGITS, l->Override, 2);
    l->LeadingZero = locale_number(lcid, LOCALE_ILZERO, l->Override, 1);
    l->NegativeOrder = locale_number(lcid, LOCALE_INEGNUMBER, l->Override, 1);
    /* only the first group is read, "3;2;0" gives 3. */
    WCHAR grouping[16];
    l->Grouping = 0;
    if (GetLocaleInfoW(lcid, LOCALE_SGROUPING | l->Override, grouping, RTL_NUMBER_OF(grouping)))
        for (const WCHAR *c = grouping; *c >= L'0' && *c <= L'9'; c++)
            l->Grouping = l->Grouping * 10 + (*c - L'0');
}

static HRESULT bstr_from_text(const WCHAR *text, BSTR *out)
{
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* formats a plain number such as "-1234.5" with the locale's number format and the given places. */
static HRESULT bstr_from_nls(const WCHAR *plain, LCID lcid, ULONG flags, int places, BSTR *out)
{
    NUMBER_LOCALE l;
    load_number_locale(lcid, flags, &l);
    NUMBERFMTW format;
    format.NumDigits = places;
    format.LeadingZero = l.LeadingZero;
    format.Grouping = l.Grouping;
    format.lpDecimalSep = l.Decimal;
    format.lpThousandSep = l.Thousand;
    format.NegativeOrder = l.NegativeOrder;
    WCHAR text[400];
    if (!GetNumberFormatW(lcid, 0, plain, &format, text, RTL_NUMBER_OF(text)))
        return E_INVALIDARG;
    return bstr_from_text(text, out);
}

/* ------------------------------------------------------------------ integers */

int AutFormatInteger(LONGLONG v, BOOL is_unsigned, WCHAR *out)
{
    WCHAR reversed[24];
    int count = 0, n = 0;
    BOOL negative = !is_unsigned && v < 0;
    ULONGLONG m = negative ? 0 - (ULONGLONG)v : (ULONGLONG)v;
    do {
        reversed[count++] = (WCHAR)(L'0' + m % 10);
        m /= 10;
    } while (m);
    if (negative)
        out[n++] = L'-';
    while (count)
        out[n++] = reversed[--count];
    out[n] = 0;
    return n;
}

HRESULT AutBstrFromInteger(LONGLONG v, BOOL is_unsigned, LCID lcid, ULONG flags, BSTR *out)
{
    WCHAR text[32];
    if (!out)
        return E_INVALIDARG;
    AutFormatInteger(v, is_unsigned, text);
    if (flags & LOCALE_USE_NLS)
        return bstr_from_nls(text, lcid, flags, 0, out);
    return bstr_from_text(text, out);
}

/* ------------------------------------------------------------------ doubles */

#define MAX_TEXT 400

typedef struct {
    WCHAR Decimal[8];
    BOOL LeadingZero;
} PLAIN_LOCALE;

static void load_plain_locale(LCID lcid, ULONG flags, PLAIN_LOCALE *p)
{
    ULONG override = locale_override(flags);
    if (!GetLocaleInfoW(lcid, LOCALE_SDECIMAL | override, p->Decimal, RTL_NUMBER_OF(p->Decimal)))
        wcscpy(p->Decimal, L".");
    p->LeadingZero = locale_number(lcid, LOCALE_ILZERO, override, 1) != 0;
}

typedef struct {
    WCHAR *Out;
    int Length;
} TEXT_OUT;

static void put_char(TEXT_OUT *t, WCHAR c)
{
    if (t->Length < MAX_TEXT - 1)
        t->Out[t->Length++] = c;
}

static void put_string(TEXT_OUT *t, const WCHAR *s)
{
    while (*s)
        put_char(t, *s++);
}

/* ties round away from zero, no trailing zeros, x = 0.digits * 10^point. */
static int rounded_digits(double x, int n, char *digits, int *point)
{
    char exact[48];
    int k = AutDoubleDigits(x, 40, exact, point);
    if (k <= n) {
        memcpy(digits, exact, k);
        digits[k] = 0;
        return k;
    }
    BOOL up = exact[n] >= '5';
    memcpy(digits, exact, n);
    int i = n - 1;
    if (up) {
        while (i >= 0 && digits[i] == '9')
            digits[i--] = '0';
        if (i >= 0) {
            digits[i]++;
        } else {
            digits[0] = '1';
            memset(digits + 1, '0', n - 1);
            (*point)++;
        }
    }
    while (n > 0 && digits[n - 1] == '0')
        n--;
    digits[n] = 0;
    return n;
}

/* writes x like the C runtime does with the type's digits, using the locale's decimal separator. */
static void double_text(double x, int type_digits, const PLAIN_LOCALE *loc, TEXT_OUT *t)
{
    ULONGLONG bits;
    memcpy(&bits, &x, 8);
    BOOL negative = (bits >> 63) != 0;
    ULONGLONG fraction = bits & 0xFFFFFFFFFFFFFull;
    int exponent = (int)((bits >> 52) & 0x7FF);
    if (negative)
        put_char(t, L'-');
    if (exponent == 0x7FF) {
        put_char(t, L'1');
        put_string(t, loc->Decimal);
        put_char(t, L'#');
        if (!fraction)
            put_string(t, L"INF");
        else if (fraction & 0x8000000000000ull)
            put_string(t, fraction == 0x8000000000000ull && negative ? L"IND" : L"QNAN");
        else
            put_string(t, L"SNAN");
        return;
    }
    if (!exponent && !fraction) {
        put_char(t, L'0');
        return;
    }
    char digits[48];
    int point;
    int k = rounded_digits(negative ? -x : x, type_digits, digits, &point);
    BOOL fixed = point > 0 ? point <= type_digits : k - point <= type_digits;
    if (fixed) {
        if (point <= 0) {
            if (loc->LeadingZero)
                put_char(t, L'0');
            put_string(t, loc->Decimal);
            for (int i = 0; i < -point; i++)
                put_char(t, L'0');
            for (int i = 0; i < k; i++)
                put_char(t, (WCHAR)digits[i]);
        } else {
            for (int i = 0; i < point; i++)
                put_char(t, i < k ? (WCHAR)digits[i] : L'0');
            if (k > point) {
                put_string(t, loc->Decimal);
                for (int i = point; i < k; i++)
                    put_char(t, (WCHAR)digits[i]);
            }
        }
        return;
    }
    put_char(t, (WCHAR)digits[0]);
    if (k > 1) {
        put_string(t, loc->Decimal);
        for (int i = 1; i < k; i++)
            put_char(t, (WCHAR)digits[i]);
    }
    int e = point - 1;
    put_char(t, L'E');
    put_char(t, e < 0 ? L'-' : L'+');
    if (e < 0)
        e = -e;
    if (e >= 100)
        put_char(t, (WCHAR)(L'0' + e / 100));
    put_char(t, (WCHAR)(L'0' + e / 10 % 10));
    put_char(t, (WCHAR)(L'0' + e % 10));
}

/*
 * 17 significant digits without an exponent, for GetNumberFormatW. Places past one beyond what the locale keeps cannot
 * change the result, so they are not written.
 */
static BOOL double_plain(double x, int places, WCHAR *out)
{
    ULONGLONG bits;
    memcpy(&bits, &x, 8);
    BOOL negative = (bits >> 63) != 0;
    int exponent = (int)((bits >> 52) & 0x7FF);
    if (exponent == 0x7FF)
        return FALSE;
    char digits[48];
    int point;
    int k = AutDoubleDigits(negative ? -x : x, 17, digits, &point);
    int n = 0;
    if (negative)
        out[n++] = L'-';
    if (!k) {
        out[n++] = L'0';
        out[n] = 0;
        return TRUE;
    }
    int whole = point > 0 ? point : 0;
    for (int i = 0; i < whole; i++)
        out[n++] = i < k ? (WCHAR)digits[i] : L'0';
    if (!whole)
        out[n++] = L'0';
    out[n++] = L'.';
    int written = 0;
    for (int i = point; written <= places; i++, written++) {
        if (i < 0)
            out[n++] = L'0';
        else if (i < k)
            out[n++] = (WCHAR)digits[i];
        else
            break;
    }
    out[n] = 0;
    return TRUE;
}

HRESULT AutBstrFromDouble(double v, int ndigits, LCID lcid, ULONG flags, BSTR *out)
{
    WCHAR text[MAX_TEXT];
    if (!out)
        return E_INVALIDARG;
    if (flags & LOCALE_USE_NLS) {
        NUMBER_LOCALE l;
        load_number_locale(lcid, flags, &l);
        WCHAR plain[MAX_TEXT];
        if (!double_plain(v, (int)l.Digits, plain))
            return E_INVALIDARG;
        return bstr_from_nls(plain, lcid, flags, (int)l.Digits, out);
    }
    PLAIN_LOCALE loc;
    load_plain_locale(lcid, flags, &loc);
    TEXT_OUT t = {text, 0};
    double_text(v, ndigits, &loc, &t);
    text[t.Length] = 0;
    return bstr_from_text(text, out);
}

/* ------------------------------------------------------------------ decimal text */

HRESULT AutFormatDecimal(BOOL negative, ULONG high, ULONGLONG low, int scale, LCID lcid, ULONG flags, BSTR *out)
{
    WCHAR text[MAX_TEXT];
    char digits[40];
    ULONG w[3] = {(ULONG)low, (ULONG)(low >> 32), high};
    if (!out)
        return E_INVALIDARG;
    int k = words_digits(w, digits);
    /* zero has no sign and no places, and trailing zeros in the places are dropped. */
    if (!k)
        negative = FALSE, scale = 0;
    while (scale && k && digits[k - 1] == '0') {
        k--;
        scale--;
    }
    PLAIN_LOCALE loc;
    load_plain_locale(lcid, flags, &loc);
    TEXT_OUT t = {text, 0};
    if (negative)
        put_char(&t, L'-');
    if (k > scale) {
        for (int i = 0; i < k - scale; i++)
            put_char(&t, (WCHAR)digits[i]);
    } else if (loc.LeadingZero || !scale) {
        put_char(&t, L'0');
    }
    if (scale) {
        put_string(&t, loc.Decimal);
        for (int i = k; i < scale; i++)
            put_char(&t, L'0');
        for (int i = k > scale ? k - scale : 0; i < k; i++)
            put_char(&t, (WCHAR)digits[i]);
    }
    text[t.Length] = 0;
    return bstr_from_text(text, out);
}
