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

/*
 * what a locale says about numbers, the way oleaut32 reads it: the separators, the symbol of the currency, and the
 * pieces of a number format. Where the locale has nothing, the defaults are the ones windows has: a dot, no thousands
 * separator (a comma for money), a dollar sign. A space is a thousands separator as well when the locale's one is a
 * space of some kind.
 */
typedef struct {
    WCHAR Decimal, Thousand, ThousandAlt, NegativeSign, MonDecimal, MonThousand;
    WCHAR Currency[10];
    int CurrencyLen;
    /* a symbol of one character that has no other case or width is compared as it is. */
    BOOL CurrencySimple;
    BOOL LeadingZero;
    int Grouping, NegativeOrder, Digits, MonGrouping, CurrencyDigits;
} NUM_LOCALE;

static BOOL is_space(WCHAR c);

static int locale_int(LCID lcid, ULONG info_flags, LCTYPE type, int fallback)
{
    WCHAR text[8];
    if (!GetLocaleInfoW(lcid, type | info_flags, text, 8) || !text[0])
        return fallback;
    int v = 0;
    for (const WCHAR *p = text; *p >= L'0' && *p <= L'9'; p++)
        v = v * 10 + (*p - L'0');
    return v;
}

static WCHAR locale_char(LCID lcid, ULONG info_flags, LCTYPE type, WCHAR fallback)
{
    WCHAR text[8];
    return GetLocaleInfoW(lcid, type | info_flags, text, 8) > 1 ? text[0] : fallback;
}

static void number_locale(LCID lcid, ULONG flags, NUM_LOCALE *l)
{
    ULONG info_flags = flags & (LOCALE_NOUSEROVERRIDE | LOCALE_USE_NLS);
    if (lcid == 0 || lcid == LOCALE_USER_DEFAULT)
        lcid = GetUserDefaultLCID();
    else if (lcid == LOCALE_SYSTEM_DEFAULT)
        lcid = GetSystemDefaultLCID();
    WCHAR text[16];
    int len = GetLocaleInfoW(lcid, LOCALE_SCURRENCY | info_flags, text, 10) - 1;
    if (len <= 0) {
        l->Currency[0] = L'$';
        l->Currency[1] = 0;
        l->CurrencyLen = 1;
        l->CurrencySimple = TRUE;
    } else {
        memcpy(l->Currency, text, (len + 1) * sizeof(WCHAR));
        l->CurrencyLen = len;
        l->CurrencySimple = FALSE;
        if (len == 1) {
            WCHAR narrow, wide;
            LCMapStringW(lcid, LCMAP_HALFWIDTH | LCMAP_LOWERCASE, l->Currency, 1, &narrow, 1);
            LCMapStringW(lcid, LCMAP_FULLWIDTH | LCMAP_UPPERCASE, l->Currency, 1, &wide, 1);
            l->CurrencySimple = narrow == wide;
        }
    }
    l->Decimal = locale_char(lcid, info_flags, LOCALE_SDECIMAL, L'.');
    l->NegativeSign = locale_char(lcid, info_flags, LOCALE_SNEGATIVESIGN, L'-');
    l->MonDecimal = locale_char(lcid, info_flags, LOCALE_SMONDECIMALSEP, l->Decimal);
    l->MonThousand = locale_char(lcid, info_flags, LOCALE_SMONTHOUSANDSEP, L',');
    l->Thousand = locale_char(lcid, info_flags, LOCALE_STHOUSAND, 0);
    if (l->Thousand == l->Decimal)
        l->Thousand = 0;
    l->ThousandAlt = l->Thousand && is_space(l->Thousand) ? L' ' : l->Thousand;
    WCHAR zero[4] = {L'1', 0};
    GetLocaleInfoW(lcid, LOCALE_ILZERO | info_flags, zero, 4);
    l->LeadingZero = zero[0] != L'0';
    l->Grouping = locale_int(lcid, info_flags, LOCALE_SGROUPING, 3);
    l->NegativeOrder = locale_int(lcid, info_flags, LOCALE_INEGNUMBER, 0);
    l->Digits = locale_int(lcid, info_flags, LOCALE_IDIGITS, 0);
    l->MonGrouping = locale_int(lcid, info_flags, LOCALE_SMONGROUPING, 3);
    l->CurrencyDigits = locale_int(lcid, info_flags, LOCALE_ICURRDIGITS, 0);
}

static BOOL is_space(WCHAR c)
{
    return c == L' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
           c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

static BOOL currency_here(const WCHAR *p, const NUM_LOCALE *loc, LCID lcid)
{
    if (loc->CurrencySimple)
        return *p == loc->Currency[0];
    for (int i = 0; i < loc->CurrencyLen; i++)
        if (!p[i])
            return FALSE;
    return CompareStringW(lcid ? lcid : LOCALE_USER_DEFAULT, NORM_IGNORECASE | NORM_IGNOREWIDTH, p, loc->CurrencyLen, loc->Currency,
                          loc->CurrencyLen) == CSTR_EQUAL;
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
    BOOL sign = FALSE, currency = FALSE, expect_paren = FALSE, hex = FALSE, stopped = FALSE;
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
        if (!hex) {
            n->Shift = shift;
            if (out_flags)
                *out_flags = out;
            return DISP_E_TYPEMISMATCH;
        }
        n->Shift = shift;
        n->Count = count;
        n->Seen = TRUE;
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
        } else if (!currency && (in_flags & NUMPRS_CURRENCY) && currency_here(p, &loc, lcid) && c != loc.Decimal &&
                   c != loc.Thousand && c != loc.ThousandAlt) {
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
            } else if (loc.Thousand && (c == loc.Thousand || c == loc.ThousandAlt) && any) {
                /* a separator that is not asked for ends the number, and nothing after it is looked at. */
                if (!(in_flags & NUMPRS_THOUSANDS)) {
                    stopped = TRUE;
                    break;
                }
                out |= NUMPRS_THOUSANDS;
            } else if (c == loc.MonThousand && c != loc.Decimal && any && (in_flags & NUMPRS_CURRENCY) &&
                       (in_flags & NUMPRS_THOUSANDS)) {
                out |= NUMPRS_THOUSANDS | NUMPRS_CURRENCY;
            } else if (c == loc.MonDecimal && c != loc.Decimal && c != loc.Thousand && c != loc.ThousandAlt && !point &&
                       (in_flags & NUMPRS_CURRENCY) && (in_flags & NUMPRS_DECIMAL)) {
                point = TRUE;
                out |= NUMPRS_DECIMAL | NUMPRS_CURRENCY;
            } else {
                break;
            }
        }
        if (!any) {
            /* a point or a separator with no digit around it was not taken. */
            if (out_flags)
                *out_flags = out & ~(ULONG)(NUMPRS_DECIMAL | NUMPRS_THOUSANDS);
            return DISP_E_TYPEMISMATCH;
        }
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
        n->Exponent = exponent;
        n->Seen = TRUE;
    }

trailing:
    for (; !stopped;) {
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
        } else if (!currency && (in_flags & NUMPRS_CURRENCY) && currency_here(p, &loc, lcid)) {
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
    /* what was read stays in the answer when the text is refused as well. */
    parse->dwOutFlags = out;
    parse->cchUsed = used;
    int capacity = parse->cDig, count = n.Count;
    /* the digits of a hexadecimal or octal number cannot be cut short. */
    if (SUCCEEDED(hr) && n.Shift && count > capacity)
        hr = DISP_E_TYPEMISMATCH;
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
    /* a number with no digit but zeros is one zero digit. */
    if (!count && n.Seen && capacity > 0) {
        digits[0] = 0;
        count = 1;
    }
    parse->cDig = count;
    return hr;
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

/*
 * What a parsed number becomes. The types asked for are bits, and the answer is the first that holds the number in this
 * order: the integers from the smallest, then a float when the number is whole and small, a currency, a decimal, and at
 * last a float of any size. The way windows chooses is kept, not what would be tidy: a fraction is only rounded off when
 * no type that can hold it was asked for, a float is taken for any number when no currency or decimal was asked for, and
 * a hexadecimal number that has the top bit of its width set is a negative one.
 */

#define NB_I1 0x10000u
#define NB_UI1 0x20000u
#define NB_UI2 0x40000u
#define NB_UI4 0x80000u
#define NB_I8 0x100000u
#define NB_UI8 0x200000u
#define NB_FRACTIONAL 0x4070u
#define NB_INTEGERS 0x3F000Cu

static const ULONGLONG g_pow10_u64[20] = {1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull,
                                          1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull,
                                          100000000000000ull, 1000000000000000ull, 10000000000000000ull, 100000000000000000ull,
                                          1000000000000000000ull, 10000000000000000000ull};

static HRESULT set_integer_result(VARIANT *out, VARTYPE vt, LONGLONG value)
{
    VariantClear(out);
    out->vt = vt;
    out->llVal = 0;
    switch (vt) {
    case VT_I1:
    case VT_UI1: out->bVal = (BYTE)value; break;
    case VT_I2:
    case VT_UI2: out->iVal = (SHORT)value; break;
    case VT_I4:
    case VT_UI4: out->lVal = (LONG)value; break;
    default: out->llVal = value; break;
    }
    return S_OK;
}

/* the integer types in order, for a magnitude and the value it has with its sign. */
static BOOL pick_integer(ULONG bits, ULONGLONG magnitude, LONGLONG signed_value, VARTYPE *vt)
{
    if ((bits & (NB_I1 | NB_UI1 | VTBIT_I2)) && magnitude <= 0x8000) {
        if ((bits & (NB_I1 | NB_UI1)) && magnitude <= 0xFF) {
            if ((bits & NB_I1) && magnitude <= 0x80 && signed_value < 128) {
                *vt = VT_I1;
                return TRUE;
            }
            if (bits & NB_UI1) {
                *vt = VT_UI1;
                return TRUE;
            }
        }
        if ((bits & VTBIT_I2) && signed_value <= 0x7FFF) {
            *vt = VT_I2;
            return TRUE;
        }
    }
    if ((bits & (NB_UI2 | NB_UI4 | VTBIT_I4)) && magnitude <= 0xFFFFFFFFull) {
        if ((bits & NB_UI2) && magnitude <= 0xFFFF) {
            *vt = VT_UI2;
            return TRUE;
        }
        if ((bits & VTBIT_I4) && signed_value <= 0x7FFFFFFF && magnitude <= 0x80000000ull) {
            *vt = VT_I4;
            return TRUE;
        }
        if (bits & NB_UI4) {
            *vt = VT_UI4;
            return TRUE;
        }
    }
    return FALSE;
}

/* the digits and the power of ten as a number that can be written in the types of float, currency and decimal. */
static void raw_number(const NUMPARSE *np, const BYTE *digits, AUT_NUMBER *n)
{
    memset(n, 0, sizeof(*n));
    int count = np->cDig;
    if (count > (int)sizeof(n->Digits))
        count = sizeof(n->Digits);
    memcpy(n->Digits, digits, count);
    n->Count = count;
    n->Exponent = np->nPwr10;
    n->Negative = (np->dwOutFlags & NUMPRS_NEG) != 0;
    n->Inexact = (np->dwOutFlags & NUMPRS_INEXACT) != 0;
}

WINOLEAUTAPI HRESULT WINAPI VarNumFromParseNum(NUMPARSE *np, BYTE *rgbDig, ULONG dwVtBits, VARIANT *pvar)
{
    if (!np || !rgbDig || !pvar)
        return E_INVALIDARG;
    ULONG bits = dwVtBits;
    int cDig = np->cDig, pw = np->nPwr10, base = np->nBaseShift;
    ULONG flags = np->dwOutFlags;
    int mag = pw + cDig;
    int frac = 0;
    ULONGLONG acc = 0;
    BOOL slow = FALSE;
    /* the digits in the form the float, currency and decimal builders take, a hexadecimal number being a whole one. */
    AUT_NUMBER n;
    BYTE hex_digits[24];

    if (base) {
        if (base * cDig > 66 || (base * cDig == 66 && rgbDig[0] > 1))
            return DISP_E_OVERFLOW;
        for (int i = 0; i < cDig; i++)
            acc = rgbDig[i] + (acc << base);
        if (bits & NB_INTEGERS) {
            if (bits & (NB_I1 | NB_UI1)) {
                if ((bits & NB_I1) && (acc - 256 > 0xFFFFFE7Full))
                    return set_integer_result(pvar, VT_I1, (LONGLONG)acc);
                if ((bits & NB_UI1) && acc <= 0xFF)
                    return set_integer_result(pvar, VT_UI1, (LONGLONG)acc);
            }
            if (bits & (NB_UI2 | VTBIT_I2)) {
                if ((bits & VTBIT_I2) && (acc - 0x10000 > 0xFFFE7FFFull))
                    return set_integer_result(pvar, VT_I2, (LONGLONG)acc);
                if ((bits & NB_UI2) && acc <= 0xFFFF)
                    return set_integer_result(pvar, VT_UI2, (LONGLONG)acc);
            }
            if (bits & (NB_UI4 | VTBIT_I4)) {
                if (bits & VTBIT_I4)
                    return set_integer_result(pvar, VT_I4, (LONGLONG)acc);
                if ((bits & NB_UI4) && acc <= 0xFFFFFFFFull)
                    return set_integer_result(pvar, VT_UI4, (LONGLONG)acc);
            }
            if (bits & NB_I8)
                return set_integer_result(pvar, VT_I8, (LONGLONG)acc);
            if (bits & NB_UI8)
                return set_integer_result(pvar, VT_UI8, (LONGLONG)acc);
        }
        /* the sign of a number of 32 or 64 bits that has its top bit set is part of the number. */
        BOOL resolved = FALSE;
        if (acc >> 63) {
            flags |= NUMPRS_NEG;
            acc = 0 - acc;
            np->dwOutFlags = flags;
        } else if (acc >= 0x80000000ull && acc < 0x100000000ull) {
            flags |= NUMPRS_NEG;
            np->dwOutFlags = flags;
            if (acc != 0x80000000ull)
                acc = 0x100000000ull - acc;
            else
                resolved = TRUE;
        }
        if (!resolved && acc < 10000000ull) {
            /* a small number goes on with every type asked for. */
        } else if (bits & 0x4060) {
            bits &= ~0x10u;
        }
        /* the number as digits, whole. */
        int len = 0;
        BYTE reversed[24];
        for (ULONGLONG t = acc; t; t /= 10)
            reversed[len++] = (BYTE)(t % 10);
        for (int i = 0; i < len; i++)
            hex_digits[i] = reversed[len - 1 - i];
        memset(&n, 0, sizeof(n));
        memcpy(n.Digits, hex_digits, len);
        n.Count = len;
        n.Exponent = 0;
        n.Negative = (flags & NUMPRS_NEG) != 0;
        n.Inexact = FALSE;
        pw = 0;
        cDig = len;
        frac = 0;
        /* the number of digits the choices below look at is that of the text, not of the value. */
    } else {
        BOOL fractional = (bits & NB_FRACTIONAL) != 0;
        int whole_digits = cDig;
        int power = pw;
        frac = pw < 0 ? -pw : 0;
        if (!fractional) {
            whole_digits = cDig - frac;
            power = pw + frac;
        }
        BOOL integer_phase = FALSE, rounds = TRUE;
        int left = cDig;
        if ((whole_digits < 20 && !fractional) || (whole_digits <= 15 && whole_digits >= 0 && fractional) ||
            (whole_digits < 0 && fractional && whole_digits <= 15)) {
            /* the digits above the point, and a fraction that rounds them if nothing asked for can hold it. */
            int take = whole_digits;
            if (take == 10 && rgbDig[0] == 4) {
                rounds = FALSE;
                left -= 10;
            } else {
                left -= take;
            }
            const BYTE *p = rgbDig;
            for (int i = 0; i < take; i++)
                acc = *p++ + 10 * acc;
            if (left > 0 && take >= 0 && rounds) {
                BYTE d = *p;
                BOOL up = FALSE;
                if (d > 5) {
                    up = TRUE;
                } else if (d == 5) {
                    if (flags & NUMPRS_INEXACT) {
                        up = TRUE;
                    } else {
                        for (int i = 1; i < left && !up; i++)
                            up = p[i] != 0;
                        if (!up && (acc & 1))
                            up = TRUE;
                    }
                }
                if (up)
                    acc++;
            }
            integer_phase = TRUE;
        } else if (whole_digits > 15 && whole_digits == 20 && !fractional && rgbDig[0] == 1) {
            const BYTE *p = rgbDig;
            for (int i = 0; i < 20; i++)
                acc = *p++ + 10 * acc;
            if (acc >= 10000000000000000000ull) {
                left = cDig - 20;
                integer_phase = TRUE;
            } else {
                bits &= NB_FRACTIONAL;
                slow = TRUE;
            }
        } else {
            bits &= NB_FRACTIONAL;
            slow = TRUE;
        }
        if (integer_phase && (bits & NB_INTEGERS) && mag <= 20 && !(frac && fractional)) {
            BOOL ok = TRUE;
            if (power > 0) {
                if (power >= 20)
                    ok = FALSE;
                else if (mag == 20 && (rgbDig[0] > 1 || (rgbDig[0] != 0 && g_pow10_u64[power] * acc < 10000000000000000000ull)))
                    ok = FALSE;
                else
                    acc *= g_pow10_u64[power];
            }
            if (ok) {
                LONGLONG v21 = (LONGLONG)acc;
                if ((flags & NUMPRS_NEG) && acc) {
                    bits &= 0xFFD1FFFFu;
                    v21 = -(LONGLONG)acc;
                }
                VARTYPE vt;
                if (pick_integer(bits, acc, v21, &vt))
                    return set_integer_result(pvar, vt, v21);
                if (bits & NB_I8) {
                    if (!(v21 < 0 && !(flags & NUMPRS_NEG)))
                        return set_integer_result(pvar, VT_I8, v21);
                }
                if (bits & NB_UI8)
                    return set_integer_result(pvar, VT_UI8, (LONGLONG)acc);
            }
        }
        raw_number(np, rgbDig, &n);
        if (slow)
            bits &= NB_FRACTIONAL;
    }

    /* the types for a number that is not whole or does not fit an integer. */
    BOOL negative = (flags & NUMPRS_NEG) != 0;
    n.Negative = FALSE;
    double value = AutNumberToDouble(&n);
    n.Negative = negative;

    if (!slow) {
        BOOL go_float = (bits & 0x30) &&
                        (!(bits & 0x4040) || (frac == 0 && (mag <= 7 || (mag <= 15 && (bits & 0x20)))));
        /* the walk of windows does not end when the same choice is made again and again, which this one does. */
        for (int round = 0;; round++) {
            if (round == 4)
                return DISP_E_OVERFLOW;
            BOOL try_float = go_float;
            go_float = FALSE;
            if (!try_float) {
                BOOL cy_applies = (bits & 0x40) && mag <= 15 && !((bits & 0x4030) && frac > 4 && ((bits & 0x4020) || mag <= 3));
                if (cy_applies)
                    break;
                if ((bits & 0x4000) && mag <= 29) {
                    if ((bits & 0x30) && frac > 28 && ((bits & 0x20) || np->cDig - frac + 28 <= 7)) {
                        try_float = TRUE;
                    } else {
                        DECIMAL dec;
                        if (SUCCEEDED(AutDecimalFromNumber(&n, &dec))) {
                            VariantClear(pvar);
                            pvar->decVal = dec;
                            pvar->vt = VT_DECIMAL;
                            return S_OK;
                        }
                    }
                }
                if (!try_float && !(bits & 0x30))
                    return DISP_E_OVERFLOW;
            }
            if (value > 3.402823567797336e38)
                bits &= ~0x10u;
            if (!(value <= 1.7976931348623157e308))
                return DISP_E_OVERFLOW;
            double signed_value = negative ? -value : value;
            if ((bits & 0x10) && (!(bits & 0x4060) || (mag <= 7 && frac == 0))) {
                VariantClear(pvar);
                pvar->vt = VT_R4;
                pvar->fltVal = (float)signed_value;
                return S_OK;
            }
            if (bits & 0x20) {
                VariantClear(pvar);
                pvar->vt = VT_R8;
                pvar->dblVal = signed_value;
                return S_OK;
            }
        }
        /* a currency; when it does not fit, the rest is looked at again without it. */
        ULONG hi;
        ULONGLONG lo;
        if (AutNumberToU96(&n, 4, &hi, &lo) && !hi && lo <= (negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull)) {
            VariantClear(pvar);
            pvar->vt = VT_CY;
            pvar->cyVal.int64 = negative ? (LONGLONG)(0 - lo) : (LONGLONG)lo;
            return S_OK;
        }
        bits &= ~0x40u;
    }

    for (;;) {
        if (!bits)
            return DISP_E_OVERFLOW;
        if (!(bits & 0x4040) || ((bits & 0x20) && frac == 0 && np->cDig <= 15))
            break;
        if ((bits & 0x40) && mag <= 15 &&
            (!(bits & 0x4030) || frac <= 4 || (!(bits & 0x4000) && mag > 11) || (!(bits & 0x20) && mag > 3))) {
            ULONG hi;
            ULONGLONG lo;
            if (AutNumberToU96(&n, 4, &hi, &lo) && !hi && lo <= (negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull)) {
                VariantClear(pvar);
                pvar->vt = VT_CY;
                pvar->cyVal.int64 = negative ? (LONGLONG)(0 - lo) : (LONGLONG)lo;
                return S_OK;
            }
            bits &= ~0x40u;
            continue;
        }
        if ((bits & 0x4000) && mag <= 29) {
            BOOL build = TRUE;
            if ((bits & 0x30) && frac > 28) {
                int left_digits = np->cDig - frac + 28;
                build = left_digits > 15 || (!(bits & 0x20) && left_digits > 7);
            }
            if (build) {
                DECIMAL dec;
                if (SUCCEEDED(AutDecimalFromNumber(&n, &dec))) {
                    VariantClear(pvar);
                    pvar->decVal = dec;
                    pvar->vt = VT_DECIMAL;
                    return S_OK;
                }
                bits &= ~0x4000u;
                continue;
            }
        }
        bits &= ~0x4040u;
    }
    if (value > 3.402823567797336e38)
        bits &= ~0x10u;
    if (!(value <= 1.7976931348623157e308))
        return DISP_E_OVERFLOW;
    double signed_value = negative ? -value : value;
    if (bits & 0x20) {
        VariantClear(pvar);
        pvar->vt = VT_R8;
        pvar->dblVal = signed_value;
        return S_OK;
    }
    if (bits & 0x10) {
        VariantClear(pvar);
        pvar->vt = VT_R4;
        pvar->fltVal = (float)signed_value;
        return S_OK;
    }
    return DISP_E_OVERFLOW;
}

/* ------------------------------------------------------------------ decimals */

/* the largest scale up to 28 whose digits still fit in 96 bits. */
HRESULT AutDecimalFromNumber(const AUT_NUMBER *n, DECIMAL *out)
{
    out->wReserved = 0;
    out->sign = n->Negative ? 0x80 : 0;
    /* digits all below the last place a decimal has are nothing, whatever the scale they were typed with. */
    if (!n->Shift && n->Count + n->Exponent < -28) {
        out->scale = 0;
        out->Hi32 = 0;
        out->Lo64 = 0;
        return S_OK;
    }
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
    out->scale = (BYTE)scale;
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

/*
 * the text goes through the number format of the locale, built from the pieces that oleaut32 reads of it rather than
 * the locale's own: the first group of the grouping only, the digits of fractions given, none for an integer.
 */
static HRESULT format_with_locale(LCID lcid, ULONG flags, const WCHAR *text, BOOL integer, BSTR *out)
{
    NUM_LOCALE loc;
    number_locale(lcid, flags, &loc);
    WCHAR decimal[2] = {loc.Decimal, 0}, thousand[2] = {loc.Thousand, 0};
    NUMBERFMTW format;
    memset(&format, 0, sizeof(format));
    format.NumDigits = integer ? 0 : loc.Digits;
    format.LeadingZero = loc.LeadingZero;
    format.Grouping = loc.Grouping;
    format.lpDecimalSep = decimal;
    format.lpThousandSep = thousand;
    format.NegativeOrder = loc.NegativeOrder;
    WCHAR buffer[400];
    if (!GetNumberFormatW(lcid ? lcid : LOCALE_USER_DEFAULT, 0, text, &format, buffer, 400))
        return E_INVALIDARG;
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
    BOOL sign = (bits >> 63) != 0;
    if (((bits >> 52) & 0x7FF) == 0x7FF) {
        /* what the runtime of C writes: 1.#INF, 1.#QNAN for a quiet one, 1.#SNAN for a signaling one and 1.#IND for the
         * indefinite one, which is a quiet not a number with a sign and nothing else set. */
        ULONGLONG fraction = bits & 0xFFFFFFFFFFFFFull;
        const WCHAR *name = !fraction ? L"INF"
                            : !(fraction & 0x8000000000000ull) ? L"SNAN"
                            : sign && fraction == 0x8000000000000ull ? L"IND"
                                                                     : L"QNAN";
        if (flags & LOCALE_USE_NLS)
            return E_INVALIDARG;
        NUM_LOCALE loc;
        number_locale(lcid, flags, &loc);
        if (sign)
            text[n++] = L'-';
        text[n++] = L'1';
        text[n++] = loc.Decimal;
        text[n++] = L'#';
        while (*name)
            text[n++] = *name++;
        text[n] = 0;
        HRESULT hr;
        *out = make_bstr(text, &hr);
        return hr;
    }
    if (flags & LOCALE_USE_NLS) {
        /* fixed notation with as many fraction digits as the locale has, from the seventeen digits of the value, which
         * is what the runtime of C writes: the digits after those are zeros. Then the locale's grouping. */
        NUM_LOCALE nl;
        number_locale(lcid, flags, &nl);
        char all[48];
        int all_point;
        int all_count = AutDoubleDigits(v, 17, all, &all_point);
        WCHAR fixed[400];
        int k = 0;
        if (sign)
            fixed[k++] = L'-';
        /* the digits to keep: the whole part and the fraction the locale writes, rounded half away from zero. */
        int keep = all_point + nl.Digits;
        char d[48];
        int d_point = all_point;
        int d_count = 0;
        if (all_count) {
            if (keep < 0) {
                d_count = 0;
            } else if (keep >= all_count) {
                memcpy(d, all, all_count);
                d_count = all_count;
            } else {
                memcpy(d, all, keep);
                d_count = keep;
                if (all[keep] >= '5') {
                    int i = keep - 1;
                    for (; i >= 0; i--) {
                        if (d[i] == '9') {
                            d[i] = '0';
                        } else {
                            d[i]++;
                            break;
                        }
                    }
                    if (i < 0) {
                        memmove(d + 1, d, d_count);
                        d[0] = '1';
                        d_count++;
                        d_point++;
                    }
                }
            }
        }
        if (d_count == 0 || d_point <= 0) {
            fixed[k++] = L'0';
        } else {
            for (int i = 0; i < d_point && k < 340; i++)
                fixed[k++] = i < d_count ? (WCHAR)d[i] : L'0';
        }
        if (nl.Digits > 0) {
            fixed[k++] = L'.';
            for (int i = 0; i < nl.Digits && k < 380; i++) {
                int idx = d_point + i;
                fixed[k++] = idx >= 0 && idx < d_count ? (WCHAR)d[idx] : L'0';
            }
        }
        fixed[k] = 0;
        return format_with_locale(lcid, flags, fixed, FALSE, out);
    }
    NUM_LOCALE loc;
    number_locale(lcid, flags, &loc);
    if (!count) {
        if (sign)
            text[n++] = L'-';
        text[n++] = L'0';
    } else {
        if (v < 0)
            text[n++] = L'-';
        int exponent = point - 1;
        /* fixed notation while the digits that it writes, the zeros after the point and the digits of the number, do not
         * outnumber what the type holds; a float has seven and a double fifteen. */
        if (point <= 0 ? count - point > ndigits : point > ndigits) {
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
    int raw = count;
    while (count < scale)
        digits[count++] = L'0';
    int whole = raw > scale ? raw - scale : 0;
    int drop = 0;
    while (drop < scale && digits[drop] == L'0')
        drop++;
    NUM_LOCALE loc;
    number_locale(lcid, flags, &loc);
    WCHAR text[64];
    int n = 0;
    /* a decimal is not formatted by the locale's number format, whatever is asked: only its separator is used. A zero
     * keeps its sign, and a value that is only a fraction has a zero before the point if the locale wants one. */
    if (negative)
        text[n++] = L'-';
    if (!whole && (loc.LeadingZero || drop >= scale))
        text[n++] = L'0';
    for (int i = raw - 1; i >= scale; i--)
        text[n++] = digits[i];
    if (drop < scale) {
        text[n++] = loc.Decimal;
        for (int i = scale - 1; i >= drop; i--)
            text[n++] = digits[i];
    }
    text[n] = 0;
    HRESULT hr;
    *out = make_bstr(text, &hr);
    return hr;
}
