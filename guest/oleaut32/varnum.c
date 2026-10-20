/*
 * numbers and text: the parser behind VarParseNumFromStr and the conversions from strings, and the formatting of
 * numbers into strings. Digits are kept as they are typed, and every rounding is done on them, so a string never goes
 * through a double on its way to an integer, a currency or a decimal.
 */
#include "oleaut32_private.h"
#include "dtoa.h"
#include <mime/win32/winnls.h>

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

/* ------------------------------------------------------------------ locale */

typedef struct {
    WCHAR Decimal, Thousand, Currency[8], CurrencyLen;
} NUM_LOCALE;

static void number_locale(LCID lcid, ULONG flags, NUM_LOCALE *l)
{
    WCHAR text[16];
    ULONG info_flags = flags & LOCALE_NOUSEROVERRIDE;
    if (!lcid)
        lcid = LOCALE_USER_DEFAULT;
    l->Decimal = GetLocaleInfoW(lcid, LOCALE_SDECIMAL | info_flags, text, 16) && text[0] ? text[0] : L'.';
    l->Thousand = GetLocaleInfoW(lcid, LOCALE_STHOUSAND | info_flags, text, 16) && text[0] ? text[0] : L',';
    l->CurrencyLen = 0;
    if (GetLocaleInfoW(lcid, LOCALE_SCURRENCY | info_flags, l->Currency, 8) > 1)
        l->CurrencyLen = (WCHAR)wcslen(l->Currency);
}

static BOOL is_space(WCHAR c)
{
    return c == L' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x3000;
}

static int digit_value(WCHAR c, int shift)
{
    int v;
    if (c >= L'0' && c <= L'9')
        v = c - L'0';
    else if (c >= L'a' && c <= L'f')
        v = c - L'a' + 10;
    else if (c >= L'A' && c <= L'F')
        v = c - L'A' + 10;
    else
        return -1;
    int base = shift ? 1 << shift : 10;
    return v < base ? v : -1;
}

/* ------------------------------------------------------------------ parsing */

HRESULT AutNumberFromString(LPCOLESTR text, LCID lcid, ULONG locale_flags, ULONG in_flags, AUT_NUMBER *n,
                            ULONG *out_flags, int *used)
{
    NUM_LOCALE loc;
    const WCHAR *p = text;
    ULONG out = 0;
    memset(n, 0, sizeof(*n));
    if (used)
        *used = 0;
    if (out_flags)
        *out_flags = 0;
    if (!text)
        return DISP_E_TYPEMISMATCH;
    number_locale(lcid, locale_flags, &loc);

    /* a prefix of white space, signs, parentheses and the currency symbol, in any order but with one sign. */
    BOOL sign = FALSE, currency = FALSE, expect_paren = FALSE, hex = FALSE;
    if (is_space(*p) && (in_flags & NUMPRS_LEADING_WHITE)) {
        out |= NUMPRS_LEADING_WHITE;
        while (is_space(*p))
            p++;
    }
    if (*p == L'&' && (in_flags & NUMPRS_HEX_OCT)) {
        int shift = 3;
        const WCHAR *q = p + 1;
        if (*q == L'h' || *q == L'H') {
            shift = 4;
            q++;
        } else if (*q == L'o' || *q == L'O') {
            q++;
        }
        int count = 0;
        while (digit_value(*q, shift) >= 0) {
            int v = digit_value(*q, shift);
            if (count || v) {
                if (count < (int)sizeof(n->Digits))
                    n->Digits[count++] = (BYTE)v;
                else
                    n->Inexact = TRUE;
            }
            q++;
            hex = TRUE;
        }
        if (!hex)
            return DISP_E_TYPEMISMATCH;
        n->Shift = shift;
        n->Count = count;
        out |= NUMPRS_HEX_OCT;
        p = q;
        goto trailing;
    }
    for (;;) {
        WCHAR c = *p;
        if (is_space(c) && (in_flags & NUMPRS_LEADING_WHITE)) {
            out |= NUMPRS_LEADING_WHITE;
            p++;
        } else if (c == L'+' && !sign && (in_flags & NUMPRS_LEADING_PLUS)) {
            sign = TRUE;
            out |= NUMPRS_LEADING_PLUS;
            p++;
        } else if (c == L'-' && !sign && (in_flags & NUMPRS_LEADING_MINUS)) {
            sign = TRUE;
            out |= NUMPRS_LEADING_MINUS | NUMPRS_NEG;
            n->Negative = TRUE;
            p++;
        } else if (c == L'(' && !sign && (in_flags & NUMPRS_PARENS)) {
            sign = TRUE;
            expect_paren = TRUE;
            out |= NUMPRS_PARENS | NUMPRS_NEG;
            n->Negative = TRUE;
            p++;
        } else if (!currency && (in_flags & NUMPRS_CURRENCY) && loc.CurrencyLen &&
                   !wcsncmp(p, loc.Currency, loc.CurrencyLen) && c != loc.Decimal && c != loc.Thousand) {
            currency = TRUE;
            out |= NUMPRS_CURRENCY;
            p += loc.CurrencyLen;
        } else {
            break;
        }
    }

    /* the digits: integer part, thousands separators, a decimal point, a fraction. */
    {
        BOOL any = FALSE, point = FALSE;
        int count = 0, exponent = 0;
        for (;; p++) {
            WCHAR c = *p;
            if (c >= L'0' && c <= L'9') {
                any = TRUE;
                int v = c - L'0';
                if (!count && !v) {
                    if (point)
                        exponent--;
                    continue;
                }
                if (count < (int)sizeof(n->Digits)) {
                    n->Digits[count++] = (BYTE)v;
                    if (point)
                        exponent--;
                } else {
                    if (v)
                        n->Inexact = TRUE;
                    if (!point)
                        exponent++;
                }
            } else if (c == loc.Decimal && !point && (in_flags & NUMPRS_DECIMAL)) {
                point = TRUE;
                out |= NUMPRS_DECIMAL;
            } else if (c == loc.Thousand && !point && any && (in_flags & NUMPRS_THOUSANDS)) {
                out |= NUMPRS_THOUSANDS;
            } else {
                break;
            }
        }
        if (!any)
            return DISP_E_TYPEMISMATCH;
        if ((*p == L'e' || *p == L'E' || *p == L'd' || *p == L'D') && (in_flags & NUMPRS_EXPONENT)) {
            const WCHAR *q = p + 1;
            BOOL negative = FALSE;
            if (*q == L'-' || *q == L'+')
                negative = *q++ == L'-';
            if (*q >= L'0' && *q <= L'9') {
                int e = 0;
                for (; *q >= L'0' && *q <= L'9'; q++)
                    if (e < 100000)
                        e = e * 10 + (*q - L'0');
                exponent += negative ? -e : e;
                out |= NUMPRS_EXPONENT;
                p = q;
            }
        }
        /* zeros at the end of the digits move into the exponent. */
        while (count && !n->Digits[count - 1]) {
            count--;
            exponent++;
        }
        n->Count = count;
        n->Exponent = count ? exponent : 0;
    }

trailing:
    for (;;) {
        WCHAR c = *p;
        if (is_space(c) && (in_flags & NUMPRS_TRAILING_WHITE)) {
            out |= NUMPRS_TRAILING_WHITE;
            p++;
        } else if (c == L'+' && !sign && (in_flags & NUMPRS_TRAILING_PLUS)) {
            sign = TRUE;
            out |= NUMPRS_TRAILING_PLUS;
            p++;
        } else if (c == L'-' && !sign && (in_flags & NUMPRS_TRAILING_MINUS)) {
            sign = TRUE;
            out |= NUMPRS_TRAILING_MINUS | NUMPRS_NEG;
            n->Negative = TRUE;
            p++;
        } else if (c == L')' && expect_paren) {
            expect_paren = FALSE;
            p++;
        } else if (!currency && (in_flags & NUMPRS_CURRENCY) && loc.CurrencyLen &&
                   !wcsncmp(p, loc.Currency, loc.CurrencyLen)) {
            currency = TRUE;
            out |= NUMPRS_CURRENCY;
            p += loc.CurrencyLen;
        } else {
            break;
        }
    }
    if (n->Inexact)
        out |= NUMPRS_INEXACT;
    if (out_flags)
        *out_flags = out;
    if (used)
        *used = (int)(p - text);
    if (expect_paren || ((in_flags & NUMPRS_USE_ALL) && *p))
        return DISP_E_TYPEMISMATCH;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarParseNumFromStr(LPCOLESTR text, LCID lcid, ULONG flags, NUMPARSE *parse, BYTE *digits)
{
    if (!parse || !digits)
        return E_INVALIDARG;
    AUT_NUMBER n;
    ULONG out;
    int used;
    HRESULT hr = AutNumberFromString(text, lcid, flags, parse->dwInFlags, &n, &out, &used);
    parse->dwOutFlags = out;
    parse->cchUsed = used;
    parse->nBaseShift = 0;
    parse->nPwr10 = 0;
    if (FAILED(hr)) {
        if (text && used)
            parse->cchUsed = used;
        return hr;
    }
    int capacity = parse->cDig, count = n.Count;
    parse->nBaseShift = n.Shift;
    parse->nPwr10 = n.Shift ? 0 : n.Exponent;
    if (count > capacity) {
        for (int i = capacity; i < count; i++)
            if (n.Digits[i])
                parse->dwOutFlags |= NUMPRS_INEXACT;
        if (!n.Shift)
            parse->nPwr10 += count - capacity;
        count = capacity;
        while (count && !n.Digits[count - 1] && !n.Shift) {
            count--;
            parse->nPwr10++;
        }
    }
    if (count > 0)
        memcpy(digits, n.Digits, count);
    parse->cDig = count;
    return S_OK;
}

/* ------------------------------------------------------------------ exact values of digits */

/* a 96 bit integer is three words, least significant first. */
static BOOL u96_mul_small(ULONG v[3], ULONG m, ULONG add)
{
    ULONGLONG carry = add;
    for (int i = 0; i < 3; i++) {
        ULONGLONG t = (ULONGLONG)v[i] * m + carry;
        v[i] = (ULONG)t;
        carry = t >> 32;
    }
    return carry == 0;
}

static ULONG u96_div_small(ULONG v[3], ULONG d)
{
    ULONGLONG rem = 0;
    for (int i = 2; i >= 0; i--) {
        ULONGLONG t = (rem << 32) | v[i];
        v[i] = (ULONG)(t / d);
        rem = t % d;
    }
    return (ULONG)rem;
}

/*
 * the digits times ten to the exponent plus extra, rounded half to even to a 96 bit integer. FALSE when it does not
 * fit. A digit string with a base other than ten is an integer already.
 */
BOOL AutNumberToU96(const AUT_NUMBER *n, int extra, ULONG *high, ULONGLONG *low)
{
    ULONG v[3] = {0, 0, 0};
    if (n->Shift) {
        for (int i = 0; i < n->Count; i++)
            if (!u96_mul_small(v, 1u << n->Shift, n->Digits[i]))
                return FALSE;
        for (int i = 0; i < extra; i++)
            if (!u96_mul_small(v, 10, 0))
                return FALSE;
        *low = ((ULONGLONG)v[1] << 32) | v[0];
        *high = v[2];
        return TRUE;
    }
    int exponent = n->Exponent + extra;
    int keep = n->Count + (exponent < 0 ? exponent : 0);
    BOOL round_up = FALSE;
    if (!n->Count) {
        keep = 0;
    } else if (keep < 0) {
        keep = 0;
        /* everything is below one half of a unit. */
    } else if (exponent < 0) {
        /* the first digit that is dropped decides, ties go to the even neighbour. */
        BYTE first = n->Digits[keep];
        BOOL rest = n->Inexact;
        for (int i = keep + 1; i < n->Count && !rest; i++)
            rest = n->Digits[i] != 0;
        BOOL odd = keep ? (n->Digits[keep - 1] & 1) : FALSE;
        round_up = first > 5 || (first == 5 && (rest || odd));
    }
    for (int i = 0; i < keep; i++)
        if (!u96_mul_small(v, 10, n->Digits[i]))
            return FALSE;
    if (exponent > 0 && n->Count) {
        if (exponent > 30)
            return FALSE;
        for (int i = 0; i < exponent; i++)
            if (!u96_mul_small(v, 10, 0))
                return FALSE;
    }
    if (round_up && !u96_mul_small(v, 1, 1))
        return FALSE;
    *low = ((ULONGLONG)v[1] << 32) | v[0];
    *high = v[2];
    return TRUE;
}

double AutNumberToDouble(const AUT_NUMBER *n)
{
    double d;
    if (n->Shift) {
        ULONG hi;
        ULONGLONG lo;
        if (!AutNumberToU96(n, 0, &hi, &lo))
            return n->Negative ? -1e308 * 10.0 : 1e308 * 10.0;
        d = (double)(LONGLONG)(lo >> 1) * 2.0 + (double)(LONGLONG)(lo & 1);
        d += (double)hi * 18446744073709551616.0;
    } else {
        d = AutDigitsToDouble(n->Digits, n->Count, n->Count + n->Exponent);
    }
    return n->Negative ? -d : d;
}

/* ------------------------------------------------------------------ VarNumFromParseNum */

WINOLEAUTAPI HRESULT WINAPI VarNumFromParseNum(NUMPARSE *parse, BYTE *digits, ULONG vtbits, VARIANT *out)
{
    if (!parse || !digits || !out)
        return E_INVALIDARG;
    AUT_NUMBER n;
    memset(&n, 0, sizeof(n));
    int count = parse->cDig;
    if (count > (int)sizeof(n.Digits) || count < 0)
        return DISP_E_OVERFLOW;
    memcpy(n.Digits, digits, count);
    n.Count = count;
    n.Shift = parse->nBaseShift;
    n.Exponent = parse->nPwr10;
    n.Negative = (parse->dwOutFlags & NUMPRS_NEG) != 0;
    n.Inexact = (parse->dwOutFlags & NUMPRS_INEXACT) != 0;
    while (n.Count && !n.Digits[n.Count - 1] && !n.Shift) {
        n.Count--;
        n.Exponent++;
    }
    /* integers first, the smallest type that holds the value. */
    ULONG hi;
    ULONGLONG lo;
    BOOL integer = FALSE;
    /* a fraction only rounds into an integer when no type that holds it was asked for. */
    BOOL fractional_types = (vtbits & (VTBIT_R4 | VTBIT_R8 | VTBIT_CY | VTBIT_DECIMAL)) != 0;
    if (n.Shift || n.Exponent >= 0 || !n.Count || !fractional_types) {
        integer = AutNumberToU96(&n, 0, &hi, &lo) && !hi;
    }
    if (integer) {
        BOOL neg = n.Negative && lo;
        struct {
            VARTYPE vt;
            ULONG bit;
            LONGLONG min;
            ULONGLONG max;
        } types[] = {
            {VT_I1, VTBIT_I1, -128, 127},         {VT_UI1, VTBIT_UI1, 0, 255},
            {VT_I2, VTBIT_I2, -32768, 32767},     {VT_UI2, VTBIT_UI2, 0, 65535},
            {VT_I4, VTBIT_I4, -2147483648ll, 2147483647ull},
            {VT_UI4, VTBIT_UI4, 0, 4294967295ull},
            {VT_I8, 0, 0, 0},                     {VT_UI8, 0, 0, 0},
        };
        for (int i = 0; i < 6; i++) {
            if (!(vtbits & types[i].bit))
                continue;
            if (neg ? (lo > (ULONGLONG)(-types[i].min)) : (lo > types[i].max))
                continue;
            if (neg && types[i].min == 0)
                continue;
            VariantClear(out);
            out->vt = types[i].vt;
            out->llVal = 0;
            LONGLONG v = neg ? -(LONGLONG)lo : (LONGLONG)lo;
            switch (types[i].vt) {
            case VT_I1:
            case VT_UI1: out->bVal = (BYTE)v; break;
            case VT_I2:
            case VT_UI2: out->iVal = (SHORT)v; break;
            default: out->lVal = (LONG)v; break;
            }
            return S_OK;
        }
        if ((vtbits & VTBIT_I8) && (neg ? lo <= 0x8000000000000000ull : lo <= 0x7FFFFFFFFFFFFFFFull)) {
            VariantClear(out);
            out->vt = VT_I8;
            out->llVal = neg ? (LONGLONG)(0 - lo) : (LONGLONG)lo;
            return S_OK;
        }
        if ((vtbits & VTBIT_UI8) && !neg) {
            VariantClear(out);
            out->vt = VT_UI8;
            out->ullVal = lo;
            return S_OK;
        }
    }
    if (vtbits & (VTBIT_R4 | VTBIT_R8)) {
        double d = AutNumberToDouble(&n);
        if ((vtbits & VTBIT_R8) || n.Count <= 7) {
            if (!(d >= -1.7976931348623157e308 && d <= 1.7976931348623157e308))
                return DISP_E_OVERFLOW;
            VariantClear(out);
            if (vtbits & VTBIT_R8) {
                out->vt = VT_R8;
                out->dblVal = d;
            } else {
                if (d < -3.402823567797336e38 || d > 3.402823567797336e38)
                    return DISP_E_OVERFLOW;
                out->vt = VT_R4;
                out->fltVal = (float)d;
            }
            return S_OK;
        }
    }
    if (vtbits & VTBIT_CY) {
        if (AutNumberToU96(&n, 4, &hi, &lo) && !hi && lo <= (n.Negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull)) {
            VariantClear(out);
            out->vt = VT_CY;
            out->cyVal.int64 = n.Negative ? (LONGLONG)(0 - lo) : (LONGLONG)lo;
            return S_OK;
        }
    }
    if (vtbits & VTBIT_DECIMAL) {
        DECIMAL dec;
        HRESULT hr = AutDecimalFromNumber(&n, &dec);
        if (FAILED(hr))
            return hr;
        VariantClear(out);
        out->decVal = dec;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    return DISP_E_OVERFLOW;
}

/* ------------------------------------------------------------------ decimals */

/* the largest scale up to 28 whose digits still fit in 96 bits. */
HRESULT AutDecimalFromNumber(const AUT_NUMBER *n, DECIMAL *out)
{
    int scale = n->Shift || n->Exponent >= 0 ? 0 : -n->Exponent;
    if (scale > 28)
        scale = 28;
    ULONG high;
    ULONGLONG low;
    while (!AutNumberToU96(n, scale, &high, &low)) {
        if (!scale)
            return DISP_E_OVERFLOW;
        scale--;
    }
    /* digits that scaled up from nothing leave trailing zeros nobody typed. */
    out->wReserved = 0;
    out->scale = (BYTE)scale;
    out->sign = n->Negative && (high || low) ? 0x80 : 0;
    out->Hi32 = high;
    out->Lo64 = low;
    return S_OK;
}

BOOL AutDecimalValid(const DECIMAL *d)
{
    return d->scale <= 28 && !(d->sign & 0x7F);
}

/* a decimal as a double. */
double AutDecimalToDouble(const DECIMAL *d)
{
    static const double pow10[29] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
                                     1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
                                     1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28};
    ULONGLONG lo = d->Lo64;
    double v = (double)(LONGLONG)(lo >> 1) * 2.0 + (double)(LONGLONG)(lo & 1);
    v += (double)d->Hi32 * 18446744073709551616.0;
    v /= pow10[d->scale > 28 ? 28 : d->scale];
    return d->sign ? -v : v;
}

/*
 * the value of a decimal rounded to an integer, half to even: its magnitude, which is 64 bits at most unless
 * overflow comes back TRUE, and its sign.
 */
BOOL AutDecimalToInteger(const DECIMAL *d, BOOL *negative, ULONGLONG *magnitude)
{
    ULONG v[3] = {(ULONG)d->Lo64, (ULONG)(d->Lo64 >> 32), d->Hi32};
    ULONG last = 0;
    BOOL sticky = FALSE;
    for (int i = 0; i < d->scale; i++) {
        sticky = sticky || last != 0;
        last = u96_div_small(v, 10);
    }
    if (d->scale) {
        BOOL odd = v[0] & 1;
        if (last > 5 || (last == 5 && (sticky || odd))) {
            u96_mul_small(v, 1, 1);
        }
    }
    *negative = d->sign != 0;
    if (v[2])
        return FALSE;
    *magnitude = ((ULONGLONG)v[1] << 32) | v[0];
    return TRUE;
}

/* ------------------------------------------------------------------ formatting */

static BSTR make_bstr(const WCHAR *text, HRESULT *hr)
{
    BSTR b = SysAllocString(text);
    *hr = b ? S_OK : E_OUTOFMEMORY;
    return b;
}

/* the digits of v with a minus sign, most significant first. */
int AutFormatInteger(LONGLONG v, BOOL is_unsigned, WCHAR *out)
{
    WCHAR rev[24];
    int count = 0, n = 0;
    ULONGLONG u;
    if (!is_unsigned && v < 0) {
        out[n++] = L'-';
        u = 0 - (ULONGLONG)v;
    } else {
        u = (ULONGLONG)v;
    }
    ULONG hi = (ULONG)(u >> 32), lo = (ULONG)u;
    do {
        /* divide the 64 bit value by ten with 32 bit steps. */
        ULONG q_hi = hi / 10, r = hi % 10;
        ULONGLONG t = ((ULONGLONG)r << 32) | lo;
        ULONG q_lo = (ULONG)(t / 10);
        r = (ULONG)(t % 10);
        rev[count++] = (WCHAR)(L'0' + r);
        hi = q_hi;
        lo = q_lo;
    } while (hi || lo);
    while (count)
        out[n++] = rev[--count];
    out[n] = 0;
    return n;
}

/* the text goes through the locale's number format, without fraction digits for an integer. */
static HRESULT format_with_locale(LCID lcid, ULONG flags, const WCHAR *text, BOOL integer, BSTR *out)
{
    WCHAR buffer[400];
    ULONG info_flags = flags & LOCALE_NOUSEROVERRIDE;
    int n = GetNumberFormatW(lcid ? lcid : LOCALE_USER_DEFAULT, info_flags, text, nullptr, buffer, 400);
    if (!n)
        return E_INVALIDARG;
    if (integer) {
        WCHAR point[8];
        if (GetLocaleInfoW(lcid ? lcid : LOCALE_USER_DEFAULT, LOCALE_SDECIMAL | info_flags, point, 8) && point[0]) {
            WCHAR *cut = wcsstr(buffer, point);
            if (cut)
                *cut = 0;
        }
    }
    HRESULT hr;
    *out = make_bstr(buffer, &hr);
    return hr;
}

HRESULT AutBstrFromInteger(LONGLONG v, BOOL is_unsigned, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    WCHAR text[24];
    AutFormatInteger(v, is_unsigned, text);
    if (flags & LOCALE_USE_NLS)
        return format_with_locale(lcid, flags, text, TRUE, out);
    HRESULT hr;
    *out = make_bstr(text, &hr);
    return hr;
}

/* a double in the shortest form of ndigits digits at most: 1E+15, 0.0001, 123.456. */
HRESULT AutBstrFromDouble(double v, int ndigits, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    char digits[48];
    int point;
    int count = AutDoubleDigits(v, ndigits, digits, &point);
    WCHAR text[96];
    int n = 0;
    ULONGLONG bits;
    memcpy(&bits, &v, 8);
    if (((bits >> 52) & 0x7FF) == 0x7FF)
        return DISP_E_OVERFLOW;
    if (flags & LOCALE_USE_NLS) {
        /* fixed notation, then the locale's grouping and fraction digits. */
        WCHAR digits_text[96];
        int k = 0;
        if (v < 0 && count)
            digits_text[k++] = L'-';
        if (!count) {
            digits_text[k++] = L'0';
        } else if (point <= 0) {
            digits_text[k++] = L'0';
            digits_text[k++] = L'.';
            for (int i = 0; i < -point && k < 90; i++)
                digits_text[k++] = L'0';
            for (int i = 0; i < count; i++)
                digits_text[k++] = (WCHAR)digits[i];
        } else {
            for (int i = 0; i < point && k < 90; i++)
                digits_text[k++] = i < count ? (WCHAR)digits[i] : L'0';
            if (count > point) {
                digits_text[k++] = L'.';
                for (int i = point; i < count; i++)
                    digits_text[k++] = (WCHAR)digits[i];
            }
        }
        digits_text[k] = 0;
        return format_with_locale(lcid, flags, digits_text, FALSE, out);
    }
    NUM_LOCALE loc;
    number_locale(lcid, flags, &loc);
    if (!count) {
        text[n++] = L'0';
    } else {
        if (v < 0)
            text[n++] = L'-';
        int exponent = point - 1;
        if (exponent < -4 || exponent >= ndigits) {
            text[n++] = (WCHAR)digits[0];
            if (count > 1) {
                text[n++] = loc.Decimal;
                for (int i = 1; i < count; i++)
                    text[n++] = (WCHAR)digits[i];
            }
            text[n++] = L'E';
            text[n++] = exponent < 0 ? L'-' : L'+';
            int e = exponent < 0 ? -exponent : exponent;
            if (e >= 100)
                text[n++] = (WCHAR)(L'0' + e / 100);
            text[n++] = (WCHAR)(L'0' + e / 10 % 10);
            text[n++] = (WCHAR)(L'0' + e % 10);
        } else if (point <= 0) {
            text[n++] = L'0';
            text[n++] = loc.Decimal;
            for (int i = 0; i < -point; i++)
                text[n++] = L'0';
            for (int i = 0; i < count; i++)
                text[n++] = (WCHAR)digits[i];
        } else {
            for (int i = 0; i < point; i++)
                text[n++] = i < count ? (WCHAR)digits[i] : L'0';
            if (count > point) {
                text[n++] = loc.Decimal;
                for (int i = point; i < count; i++)
                    text[n++] = (WCHAR)digits[i];
            }
        }
    }
    text[n] = 0;
    HRESULT hr;
    *out = make_bstr(text, &hr);
    return hr;
}

/* writes the digits of a 96-bit value and a decimal point before the last scale digits, without trailing zeros. */
HRESULT AutFormatDecimal(BOOL negative, ULONG high, ULONGLONG low, int scale, LCID lcid, ULONG flags, BSTR *out)
{
    WCHAR digits[40];
    int count = 0;
    ULONG parts[3] = {(ULONG)low, (ULONG)(low >> 32), high};
    do {
        digits[count++] = (WCHAR)(L'0' + u96_div_small(parts, 10));
    } while (parts[0] || parts[1] || parts[2]);
    while (count <= scale)
        digits[count++] = L'0';
    int drop = 0;
    while (drop < scale && digits[drop] == L'0')
        drop++;
    NUM_LOCALE loc;
    number_locale(lcid, flags, &loc);
    WCHAR text[48];
    int n = 0;
    BOOL zero = count - drop == 1 && digits[count - 1] == L'0' && drop == scale;
    if (negative && !zero)
        text[n++] = L'-';
    for (int i = count - 1; i >= scale; i--)
        text[n++] = digits[i];
    if (drop < scale) {
        text[n++] = loc.Decimal;
        for (int i = scale - 1; i >= drop; i--)
            text[n++] = digits[i];
    }
    text[n] = 0;
    if (flags & LOCALE_USE_NLS) {
        /* the format of the locale wants a plain dot. */
        for (int i = 0; i < n; i++)
            if (text[i] == loc.Decimal)
                text[i] = L'.';
        return format_with_locale(lcid, flags, text, FALSE, out);
    }
    HRESULT hr;
    *out = make_bstr(text, &hr);
    return hr;
}
