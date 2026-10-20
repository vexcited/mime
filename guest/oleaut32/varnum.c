/*
 * VarParseNumFromStr and VarNumFromParseNum: reading a number out of text, and choosing the variant type that holds it.
 * Written from the documented behaviour and from what the oleaut32 of windows answers, see docs/oleaut32.md.
 */
#include "oleaut32_private.h"
#include "dtoa.h"

int _fltused = 0x9875;

/* the sign byte of a decimal that is negative. */
#define DECIMAL_NEG 0x80

/* ------------------------------------------------------------------ the symbols of a locale */

#define MAX_CURRENCY 24

typedef struct {
    /* zero where the locale has no such separator. */
    WCHAR Decimal;
    WCHAR Thousand;
    WCHAR MoneyDecimal;
    WCHAR MoneyThousand;
    /* a thousands separator that is a space also accepts a plain space. */
    BOOL ThousandIsSpace;
    int CurrencyLength;
    WCHAR Currency[MAX_CURRENCY];
} SYMBOLS;

static BOOL is_white(WCHAR c)
{
    if (c == L' ' || (c >= 9 && c <= 13) || c == 0xA0)
        return TRUE;
    return c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

/* the first character of a piece of locale information, or the fallback when the locale does not answer. */
static WCHAR locale_char(LCID lcid, LCTYPE type, WCHAR fallback)
{
    WCHAR text[8];
    if (!GetLocaleInfoW(lcid, type, text, RTL_NUMBER_OF(text)))
        return fallback;
    return text[0];
}

static void load_symbols(LCID lcid, ULONG locale_flags, SYMBOLS *s)
{
    ULONG nls = (locale_flags | g_process_nls_flags) & LOCALE_NOUSEROVERRIDE;
    s->Decimal = locale_char(lcid, LOCALE_SDECIMAL | nls, L'.');
    s->Thousand = locale_char(lcid, LOCALE_STHOUSAND | nls, 0);
    s->MoneyDecimal = locale_char(lcid, LOCALE_SMONDECIMALSEP | nls, 0);
    s->MoneyThousand = locale_char(lcid, LOCALE_SMONTHOUSANDSEP | nls, L',');
    s->ThousandIsSpace = s->Thousand && is_white(s->Thousand);
    int length = GetLocaleInfoW(lcid, LOCALE_SCURRENCY | nls, s->Currency, MAX_CURRENCY);
    if (length > 1) {
        s->CurrencyLength = length - 1;
    } else {
        s->Currency[0] = L'$';
        s->CurrencyLength = 1;
    }
}

/* ------------------------------------------------------------------ the parser */

/* the largest exponent written after the e that is taken; a bigger one is an overflow. */
#define MAX_EXPONENT 1073741819

typedef struct {
    const WCHAR *Text;
    const WCHAR *At;
    ULONG In;
    ULONG Out;
    SYMBOLS Symbols;
    BYTE *Digits;
    int Capacity;
    int Count;
    int Power;
    int Shift;
    /* a sign or an opening parenthesis was read, which a second one cannot follow. */
    BOOL Signed;
    BOOL ParenOpen;
    /* a separator that was not asked for ended the number. */
    BOOL Closed;
} PARSE;

static void skip_white(PARSE *p, ULONG flag)
{
    if (!(p->In & flag))
        return;
    while (is_white(*p->At)) {
        p->At++;
        p->Out |= flag;
    }
}

static BOOL take_sign(PARSE *p)
{
    WCHAR c = *p->At;
    if (c == L'+' && (p->In & NUMPRS_LEADING_PLUS)) {
        p->Out |= NUMPRS_LEADING_PLUS;
    } else if (c == L'-' && (p->In & NUMPRS_LEADING_MINUS)) {
        p->Out |= NUMPRS_LEADING_MINUS | NUMPRS_NEG;
    } else if (c == L'(' && (p->In & NUMPRS_PARENS)) {
        p->Out |= NUMPRS_PARENS | NUMPRS_NEG;
        p->ParenOpen = TRUE;
    } else {
        return FALSE;
    }
    p->At++;
    p->Signed = TRUE;
    return TRUE;
}

/* the symbol of the currency, in any case. */
static BOOL take_currency(PARSE *p)
{
    const SYMBOLS *s = &p->Symbols;
    if (!(p->In & NUMPRS_CURRENCY))
        return FALSE;
    for (int i = 0; i < s->CurrencyLength; i++) {
        WCHAR a = p->At[i], b = s->Currency[i];
        if (!a)
            return FALSE;
        if (a != b && RtlUpcaseUnicodeChar(a) != RtlUpcaseUnicodeChar(b))
            return FALSE;
    }
    p->At += s->CurrencyLength;
    p->Out |= NUMPRS_CURRENCY;
    return TRUE;
}

static BOOL is_digit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

/* the separator of thousands of the locale, which also is a plain space when it is some kind of space. */
static BOOL is_thousand(const SYMBOLS *s, WCHAR c)
{
    return c && ((s->Thousand && c == s->Thousand) || (s->ThousandIsSpace && c == L' '));
}

/*
 * The digits, thousands separators and decimal point. Digits that do not fit are dropped and counted in the power of
 * ten, and trailing zeros move into it as well. Returns false when there is not a digit to be read.
 */
static BOOL take_digits(PARSE *p)
{
    const SYMBOLS *s = &p->Symbols;
    ULONG flags = 0;
    BOOL seen = FALSE, started = FALSE, fraction = FALSE, inexact = FALSE, ended = FALSE, cut_fraction = FALSE;
    int count = 0, power = 0;
    const WCHAR *at = p->At;

    for (;; at++) {
        WCHAR c = *at;
        if (is_digit(c)) {
            seen = TRUE;
            BYTE d = (BYTE)(c - L'0');
            if (d == 0 && !count) {
                /* a zero with no digit kept before it is a place after the point, and a power of ten before it. */
                if (fraction && !cut_fraction)
                    power--;
                else if (!fraction && started)
                    power++;
            } else if (count < p->Capacity) {
                started = TRUE;
                p->Digits[count++] = d;
                if (fraction)
                    power--;
            } else {
                started = TRUE;
                if (!fraction)
                    power++;
                else if (d)
                    cut_fraction = TRUE;
                if (d)
                    inexact = TRUE;
            }
        } else if (seen && is_thousand(s, c) && (p->In & NUMPRS_THOUSANDS)) {
            flags |= NUMPRS_THOUSANDS;
        } else if (seen && c && c == s->MoneyThousand && (p->In & NUMPRS_THOUSANDS) && (p->In & NUMPRS_CURRENCY)) {
            flags |= NUMPRS_THOUSANDS | NUMPRS_CURRENCY;
        } else if (seen && is_thousand(s, c) && is_white(c)) {
            /* a separator that is a space and was not asked for ends the number, and nothing after it is looked at. */
            ended = TRUE;
            break;
        } else if (!fraction && c && c == s->Decimal) {
            if (!(p->In & NUMPRS_DECIMAL) || (!seen && !is_digit(at[1])))
                break;
            fraction = TRUE;
            flags |= NUMPRS_DECIMAL;
        } else if (!fraction && c && c == s->MoneyDecimal && (p->In & (NUMPRS_DECIMAL | NUMPRS_CURRENCY))) {
            if (!seen && !is_digit(at[1]))
                break;
            fraction = TRUE;
            flags |= NUMPRS_DECIMAL | NUMPRS_CURRENCY;
        } else {
            break;
        }
    }
    if (!seen)
        return FALSE;
    if (count == 0) {
        p->Digits[0] = 0;
        count = 1;
    }
    while (count > 1 && p->Digits[count - 1] == 0) {
        count--;
        power++;
    }
    p->At = at;
    p->Out |= flags | (inexact ? NUMPRS_INEXACT : 0);
    p->Closed = ended;
    p->Count = count;
    p->Power = power;
    return TRUE;
}

/*
 * An exponent after the digits: e or d, a sign if there is one, and at least one digit. Without them nothing is taken.
 * Returns false for one too large to keep.
 */
static BOOL take_exponent(PARSE *p)
{
    if (!(p->In & NUMPRS_EXPONENT))
        return TRUE;
    WCHAR c = p->At[0] | 0x20;
    if (c != L'e' && c != L'd')
        return TRUE;
    const WCHAR *at = p->At + 1;
    BOOL negative = FALSE;
    if (*at == L'+' || *at == L'-') {
        negative = *at == L'-';
        at++;
    }
    if (!is_digit(*at))
        return TRUE;
    LONGLONG value = 0;
    for (; is_digit(*at); at++) {
        if (value <= MAX_EXPONENT)
            value = value * 10 + (*at - L'0');
    }
    if (value > MAX_EXPONENT)
        return FALSE;
    p->At = at;
    p->Out |= NUMPRS_EXPONENT;
    p->Power += negative ? -(int)value : (int)value;
    return TRUE;
}

/* whatever may follow the digits: white space, a sign, the closing parenthesis and the symbol of the currency. */
static void take_trailing(PARSE *p)
{
    for (;;) {
        const WCHAR *before = p->At;
        skip_white(p, NUMPRS_TRAILING_WHITE);
        if (p->At != before)
            continue;
        WCHAR c = *p->At;
        if (take_currency(p)) {
            continue;
        } else if (!p->Signed && c == L'+' && (p->In & NUMPRS_TRAILING_PLUS)) {
            p->Out |= NUMPRS_TRAILING_PLUS;
        } else if (!p->Signed && c == L'-' && (p->In & NUMPRS_TRAILING_MINUS)) {
            p->Out |= NUMPRS_TRAILING_MINUS | NUMPRS_NEG;
        } else if (p->ParenOpen && c == L')') {
            p->ParenOpen = FALSE;
        } else {
            return;
        }
        p->Signed = TRUE;
        p->At++;
    }
}

static int digit_value(WCHAR c, int shift)
{
    int v;
    if (c >= L'0' && c <= L'9')
        v = c - L'0';
    else if ((c | 0x20) >= L'a' && (c | 0x20) <= L'f')
        v = (c | 0x20) - L'a' + 10;
    else
        return -1;
    return v < (1 << shift) ? v : -1;
}

/*
 * &H and &O numbers: the digits are kept as they are, in the base of the shift, without leading zeros. Too many of
 * them for the buffer is an overflow, reported at the last digit that was kept.
 */
static HRESULT parse_based(PARSE *p)
{
    p->At++;
    p->Shift = 3;
    if ((*p->At | 0x20) == L'h') {
        p->Shift = 4;
        p->At++;
    } else if ((*p->At | 0x20) == L'o') {
        p->At++;
    }
    const WCHAR *last = nullptr;
    BOOL overflow = FALSE;
    int count = 0;
    for (const WCHAR *at = p->At;; at++) {
        int v = digit_value(*at, p->Shift);
        if (v < 0)
            break;
        if (count == 0 && v == 0) {
            last = at + 1;
            continue;
        }
        if (count == p->Capacity) {
            overflow = TRUE;
            break;
        }
        p->Digits[count++] = (BYTE)v;
        last = at + 1;
    }
    if (!last)
        return DISP_E_TYPEMISMATCH;
    if (count == 0) {
        p->Digits[0] = 0;
        count = 1;
    }
    p->Count = count;
    p->Out |= NUMPRS_HEX_OCT;
    p->At = last;
    if (overflow)
        return DISP_E_OVERFLOW;
    skip_white(p, NUMPRS_TRAILING_WHITE);
    return S_OK;
}

static HRESULT parse_decimal(PARSE *p)
{
    skip_white(p, NUMPRS_LEADING_WHITE);
    if (take_sign(p))
        skip_white(p, NUMPRS_LEADING_WHITE);
    if (take_currency(p)) {
        skip_white(p, NUMPRS_LEADING_WHITE);
        if (!p->Signed && take_sign(p))
            skip_white(p, NUMPRS_LEADING_WHITE);
    }
    ULONG prefix = p->Out;
    if (!take_digits(p)) {
        p->Out = prefix;
        return DISP_E_TYPEMISMATCH;
    }
    if (p->Closed)
        return S_OK;
    const WCHAR *before_exponent = p->At;
    if (!take_exponent(p)) {
        p->At = before_exponent;
        return DISP_E_OVERFLOW;
    }
    take_trailing(p);
    return p->ParenOpen ? DISP_E_TYPEMISMATCH : S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarParseNumFromStr(LPCOLESTR text, LCID lcid, ULONG locale_flags, NUMPARSE *np, BYTE *digits)
{
    if (!np || !digits)
        return E_INVALIDARG;
    PARSE p;
    memset(&p, 0, sizeof(p));
    p.Text = p.At = text;
    p.In = np->dwInFlags;
    p.Digits = digits;
    p.Capacity = np->cDig > 0 ? np->cDig : 0;
    np->cDig = np->cchUsed = np->nBaseShift = np->nPwr10 = 0;
    np->dwOutFlags = 0;
    if (!text)
        return DISP_E_TYPEMISMATCH;
    load_symbols(lcid, locale_flags, &p.Symbols);

    skip_white(&p, NUMPRS_LEADING_WHITE);
    HRESULT hr;
    if ((p.In & NUMPRS_HEX_OCT) && *p.At == L'&')
        hr = parse_based(&p);
    else
        hr = parse_decimal(&p);

    /* an overflow stops where it was found, and with the whole text asked for that is a mismatch. */
    if (hr == S_OK || hr == DISP_E_OVERFLOW) {
        if ((p.In & NUMPRS_USE_ALL) && *p.At)
            hr = DISP_E_TYPEMISMATCH;
    }
    np->cDig = p.Count;
    np->dwOutFlags = p.Out;
    np->nBaseShift = p.Shift;
    np->nPwr10 = p.Power;
    np->cchUsed = FAILED(hr) && !p.Count ? 0 : (int)(p.At - p.Text);
    return hr;
}

/* ------------------------------------------------------------------ wide integers */

/* an unsigned integer of 160 bits, which holds any decimal that has a chance to fit a currency or a decimal. */
#define WIDE_LIMBS 5

typedef struct {
    ULONG L[WIDE_LIMBS];
    BOOL Over;
} WIDE;

static void wide_mul_add(WIDE *w, ULONG m, ULONG a)
{
    ULONGLONG carry = a;
    for (int i = 0; i < WIDE_LIMBS; i++) {
        ULONGLONG t = (ULONGLONG)w->L[i] * m + carry;
        w->L[i] = (ULONG)t;
        carry = t >> 32;
    }
    if (carry)
        w->Over = TRUE;
}

static void wide_add_one(WIDE *w)
{
    for (int i = 0; i < WIDE_LIMBS; i++)
        if (++w->L[i])
            return;
    w->Over = TRUE;
}

/* the value as 64 bits, false when it does not fit. */
static BOOL wide_u64(const WIDE *w, ULONGLONG *v)
{
    if (w->Over)
        return FALSE;
    for (int i = 2; i < WIDE_LIMBS; i++)
        if (w->L[i])
            return FALSE;
    *v = ((ULONGLONG)w->L[1] << 32) | w->L[0];
    return TRUE;
}

/* the value fits in 96 bits. */
static BOOL wide_fits96(const WIDE *w)
{
    return !w->Over && !w->L[3] && !w->L[4];
}

/* ------------------------------------------------------------------ the number as the digits left it */

typedef struct {
    BOOL Negative;
    BOOL Inexact;
    BOOL Zero;
    /* the digits, as they are. */
    const BYTE *Digits;
    int Count;
    /* ten to this multiplies the digits as an integer; far larger than the digits can make it. */
    LONGLONG Power;
    /* the number was a bit pattern, whose value a double takes by arithmetic and not from the digits. */
    BOOL FromBits;
    ULONGLONG Bits;
} NUM;

/* the number as the caller wrote it: windows does not take zeros off either end of the digits. */
static void num_from_parse(const NUMPARSE *np, const BYTE *digits, NUM *n)
{
    n->Negative = (np->dwOutFlags & NUMPRS_NEG) != 0;
    n->Inexact = (np->dwOutFlags & NUMPRS_INEXACT) != 0;
    n->Digits = digits;
    n->Count = np->cDig > 0 ? np->cDig : 0;
    n->Power = np->nPwr10;
    n->FromBits = FALSE;
    n->Zero = TRUE;
    for (int i = 0; i < n->Count; i++)
        if (digits[i]) {
            n->Zero = FALSE;
            break;
        }
}

/* the digits and the power for an unsigned integer, with zeros in front of them up to the length asked for. */
static void num_from_integer(ULONGLONG v, BOOL negative, int length, LONGLONG power, BYTE *buffer, NUM *n)
{
    ULONGLONG value = v;
    int count = 0;
    BYTE reversed[20];
    while (v) {
        reversed[count++] = (BYTE)(v % 10);
        v /= 10;
    }
    n->Negative = negative;
    n->Inexact = FALSE;
    n->Zero = count == 0;
    n->Digits = buffer;
    n->Count = count > length ? count : length;
    n->Power = power;
    n->FromBits = TRUE;
    n->Bits = value;
    int padding = n->Count - count;
    for (int i = 0; i < padding; i++)
        buffer[i] = 0;
    for (int i = 0; i < count; i++)
        buffer[padding + i] = reversed[count - 1 - i];
}

/* the digits times ten to the power, as long as that is below 2^160. */
static void num_wide(const NUM *n, LONGLONG power, WIDE *w)
{
    memset(w, 0, sizeof(*w));
    for (int i = 0; i < n->Count; i++)
        wide_mul_add(w, 10, n->Digits[i]);
    for (LONGLONG i = 0; i < power && !w->Over; i++)
        wide_mul_add(w, 10, 0);
}

/*
 * The digits as an integer of the scale asked for: ten to the power of the number divided by ten to minus scale, so
 * the number is rounded to a multiple of ten to minus scale when it has more places. Ties go to the even, or up when the
 * number was cut short of digits already.
 */
static void num_scaled(const NUM *n, int scale, WIDE *w)
{
    LONGLONG shift = n->Power + scale;
    if (shift >= 0 || n->Zero) {
        memset(w, 0, sizeof(*w));
        if (n->Zero)
            return;
        /* a power beyond sixty is out of reach of 160 bits for any digit that is not zero. */
        if (shift > 60)
            w->Over = TRUE;
        else
            num_wide(n, shift, w);
        return;
    }
    LONGLONG drop = -shift;
    int keep = drop >= n->Count ? 0 : n->Count - (int)drop;
    memset(w, 0, sizeof(*w));
    for (int i = 0; i < keep; i++)
        wide_mul_add(w, 10, n->Digits[i]);
    /* the first digit that goes decides, the ones after it say whether it is exactly half. */
    int cut = 0;
    BOOL rest = FALSE;
    if (drop > n->Count) {
        cut = 0;
    } else {
        cut = n->Digits[keep];
        for (int i = keep + 1; i < n->Count; i++)
            if (n->Digits[i])
                rest = TRUE;
    }
    BOOL up;
    if (cut > 5 || (cut == 5 && rest))
        up = TRUE;
    else if (cut < 5)
        up = FALSE;
    else
        up = n->Inexact || (w->L[0] & 1);
    if (up)
        wide_add_one(w);
}

/* ------------------------------------------------------------------ the types */

typedef struct {
    VARTYPE Vt;
    int Bits;
    BOOL Signed;
} INT_TYPE;

/* the order in which a whole number looks for a home. */
static const INT_TYPE g_int_types[] = {{VT_I1, 8, TRUE},   {VT_UI1, 8, FALSE},  {VT_I2, 16, TRUE},  {VT_UI2, 16, FALSE},
                                       {VT_I4, 32, TRUE},  {VT_UI4, 32, FALSE}, {VT_I8, 64, TRUE},   {VT_UI8, 64, FALSE}};

static void put_integer(VARIANT *out, VARTYPE vt, ULONGLONG bits)
{
    out->vt = vt;
    switch (vt) {
    case VT_I1:
    case VT_UI1: out->bVal = (BYTE)bits; break;
    case VT_I2:
    case VT_UI2: out->uiVal = (USHORT)bits; break;
    case VT_I4:
    case VT_UI4: out->ulVal = (ULONG)bits; break;
    default: out->ullVal = bits; break;
    }
}

/* a magnitude and a sign into the first of the integer types asked for that holds it. */
static BOOL store_integer(ULONG vtbits, BOOL negative, ULONGLONG magnitude, VARIANT *out)
{
    if (!magnitude)
        negative = FALSE;
    for (size_t i = 0; i < RTL_NUMBER_OF(g_int_types); i++) {
        const INT_TYPE *t = &g_int_types[i];
        if (!(vtbits & (1u << t->Vt)))
            continue;
        ULONGLONG limit;
        if (t->Signed)
            /* windows lets any negative number through to the 64 bit type, wrapped, as long as it has 64 bits. */
            limit = t->Bits == 64 && negative ? 0xFFFFFFFFFFFFFFFFull
                                              : (1ull << (t->Bits - 1)) - 1 + (negative ? 1 : 0);
        else if (negative)
            continue;
        else
            limit = t->Bits == 64 ? 0xFFFFFFFFFFFFFFFFull : (1ull << t->Bits) - 1;
        if (magnitude > limit)
            continue;
        put_integer(out, t->Vt, negative ? 0 - magnitude : magnitude);
        return TRUE;
    }
    return FALSE;
}

/* the whole part of the number as a magnitude, rounded when asked to: false when it is beyond 64 bits. */
static BOOL num_round_u64(const NUM *n, ULONGLONG *v)
{
    WIDE w;
    /* a number of more than twenty digits is out of reach, though it be zero. */
    if (n->Count + n->Power > 20)
        return FALSE;
    num_scaled(n, 0, &w);
    return wide_u64(&w, v);
}

static void put_real(VARIANT *out, double v, BOOL r4)
{
    if (r4) {
        out->vt = VT_R4;
        out->fltVal = (float)v;
    } else {
        out->vt = VT_R8;
        out->dblVal = v;
    }
}

static double infinity(void)
{
    static const ULONGLONG bits = 0x7FF0000000000000ull;
    double v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static double u64_to_double(ULONGLONG v)
{
    if ((LONGLONG)v >= 0)
        return (double)(LONGLONG)v;
    return (double)(LONGLONG)((v >> 1) | (v & 1)) * 2.0;
}

/* the value of the digits, signed, in a double. */
static double num_double(const NUM *n)
{
    if (n->Zero)
        return n->Negative ? -0.0 : 0.0;
    double v;
    if (n->FromBits && n->Power) {
        /* windows scales a double of the integer by a double of the power, and its last bit shows it. */
        LONGLONG power = n->Power < 0 ? -n->Power : n->Power;
        BYTE one = 1;
        if (power > 400)
            v = n->Power > 0 ? infinity() : 0.0;
        else if (n->Power > 0)
            v = u64_to_double(n->Bits) * AutDigitsToDouble(&one, 1, (int)power + 1);
        else
            v = u64_to_double(n->Bits) / AutDigitsToDouble(&one, 1, (int)power + 1);
        return n->Negative ? -v : v;
    }
    LONGLONG point = n->Count + n->Power;
    if (point > 400)
        v = infinity();
    else if (point < -400)
        v = 0.0;
    else
        v = AutDigitsToDouble(n->Digits, n->Count, (int)point);
    return n->Negative ? -v : v;
}

static BOOL is_infinite(double v)
{
    return v - v != 0;
}

/* a currency of the number: exact when it has no more than four places, otherwise rounded to four. */
static BOOL currency_fits(const NUM *n, BOOL exact, WIDE *w)
{
    if (exact && n->Power < -4)
        return FALSE;
    /* more than fifteen digits before the point are out of reach, though they be zeros. */
    if (n->Count + n->Power > 15)
        return FALSE;
    ULONGLONG v;
    num_scaled(n, 4, w);
    if (!wide_u64(w, &v))
        return FALSE;
    return v <= 0x7FFFFFFFFFFFFFFFull + (n->Negative ? 1 : 0);
}

static void put_currency(VARIANT *out, const WIDE *w, BOOL negative)
{
    ULONGLONG v = ((ULONGLONG)w->L[1] << 32) | w->L[0];
    out->vt = VT_CY;
    out->cyVal.int64 = negative ? (LONGLONG)(0 - v) : (LONGLONG)v;
}

static void put_decimal(VARIANT *out, const WIDE *w, int scale, BOOL negative)
{
    out->decVal.wReserved = VT_DECIMAL;
    out->decVal.scale = (BYTE)scale;
    out->decVal.sign = negative ? DECIMAL_NEG : 0;
    out->decVal.Hi32 = w->L[2];
    out->decVal.Lo64 = ((ULONGLONG)w->L[1] << 32) | w->L[0];
}

/*
 * The decimal of the number: the digits as they are when they fit 96 bits and a scale of 28. Otherwise the places are cut
 * off one at a time, rounded, until they do; the number of digits that are left is returned in *kept, and a number whose
 * every digit is lost gives zero. False when nothing can be done, as for a whole number of 30 digits.
 */
static BOOL decimal_fits(const NUM *n, WIDE *w, int *scale, int *kept)
{
    *kept = n->Count;
    /* more than 29 digits before the point are out of reach, though they be zeros. */
    if (n->Power >= 0 && n->Count + n->Power > 29)
        return FALSE;
    if (n->Power >= 0) {
        num_wide(n, n->Power, w);
        *scale = 0;
        return wide_fits96(w);
    }
    LONGLONG places = -n->Power;
    if (n->Count - places < -28) {
        /* the first digit is already beyond the last place that a decimal has. */
        memset(w, 0, sizeof(*w));
        *scale = 0;
        *kept = 0;
        return TRUE;
    }
    for (int s = places > 28 ? 28 : (int)places; s >= 0; s--) {
        /* the digits left count as the digits there are, whatever their value, and are at most 29. */
        if (n->Count - (places - s) > 29)
            continue;
        num_scaled(n, s, w);
        if (wide_fits96(w)) {
            *scale = s;
            *kept = n->Count - (int)(places - s);
            return TRUE;
        }
    }
    return FALSE;
}

/* the first of the real, currency and decimal types asked for that holds the number, after the integers. */
static HRESULT store_other(const NUM *n, ULONG vtbits, BOOL whole, LONGLONG places4, LONGLONG places8, VARIANT *out)
{
    BOOL r4 = (vtbits & VTBIT_R4) != 0, r8 = (vtbits & VTBIT_R8) != 0;
    BOOL cy = (vtbits & VTBIT_CY) != 0, dec = (vtbits & VTBIT_DECIMAL) != 0;

    /* a whole number that a float keeps whole has no use for the other types. */
    if (whole) {
        if (r4 && places4 <= 7 && places8 <= 15) {
            put_real(out, num_double(n), TRUE);
            return S_OK;
        }
        if (r8 && places8 <= 15) {
            put_real(out, num_double(n), FALSE);
            return S_OK;
        }
    }
    /* what the currency would keep of the number, in digits. */
    LONGLONG currency_digits = n->Count + n->Power + 4;
    WIDE cw, dw;
    int dscale = 0, dkept = 0;
    BOOL decimal_ok = dec && decimal_fits(n, &dw, &dscale, &dkept);
    BOOL decimal_exact = decimal_ok && dkept == n->Count && n->Power >= -28;

    if (cy && currency_fits(n, TRUE, &cw)) {
        put_currency(out, &cw, n->Negative);
        return S_OK;
    }
    /* with many digits, a decimal and a currency are chosen by what the currency would keep, not by what is exact. */
    if (cy && dec && !r8 && !r4 && n->Count >= 16 && currency_digits >= 8 && currency_fits(n, FALSE, &cw)) {
        put_currency(out, &cw, n->Negative);
        return S_OK;
    }
    if (decimal_exact) {
        put_decimal(out, &dw, dscale, n->Negative);
        return S_OK;
    }
    if (r8 || r4) {
        /* a float is kept for the numbers that the types asked for cannot hold as many digits of as it does. */
        if (decimal_ok && dkept >= 16) {
            put_decimal(out, &dw, dscale, n->Negative);
            return S_OK;
        }
        if (cy && currency_digits >= 16 && currency_fits(n, FALSE, &cw)) {
            put_currency(out, &cw, n->Negative);
            return S_OK;
        }
        double v = num_double(n);
        if (is_infinite(v))
            return DISP_E_OVERFLOW;
        put_real(out, v, !r8);
        if (!r8 && is_infinite(out->fltVal))
            return DISP_E_OVERFLOW;
        return S_OK;
    }
    if (decimal_ok) {
        put_decimal(out, &dw, dscale, n->Negative);
        return S_OK;
    }
    if (cy && currency_fits(n, FALSE, &cw)) {
        put_currency(out, &cw, n->Negative);
        return S_OK;
    }
    return DISP_E_OVERFLOW;
}

static HRESULT choose_decimal(const NUM *n, ULONG vtbits, VARIANT *out)
{
    BOOL whole = n->Power >= 0;
    BOOL has_fraction_type = (vtbits & (VTBIT_R4 | VTBIT_R8 | VTBIT_CY | VTBIT_DECIMAL)) != 0;
    ULONGLONG magnitude;
    /* with more than fifteen digits, an integer is the choice only when nothing else could hold the number. */
    if ((whole || !has_fraction_type) && (n->Count <= 15 || !has_fraction_type)) {
        if (num_round_u64(n, &magnitude) && store_integer(vtbits, n->Negative, magnitude, out))
            return S_OK;
        if (!has_fraction_type)
            return DISP_E_OVERFLOW;
    }
    return store_other(n, vtbits, whole, n->Count + n->Power, n->Count + n->Power, out);
}

/* ------------------------------------------------------------------ hexadecimal and octal */

/* the integer the digits make, whose length in bits is returned; false when it takes more than 64. */
static BOOL based_value(const NUMPARSE *np, const BYTE *digits, ULONGLONG *v, int *bits)
{
    ULONGLONG value = 0;
    int length = 0;
    for (int i = 0; i < np->cDig; i++) {
        BYTE d = digits[i] & 15;
        if (!length) {
            int b = 0;
            for (int t = d; t; t >>= 1)
                b++;
            if (!b)
                continue;
            length = b;
        } else {
            length += np->nBaseShift;
        }
        if (length > 64)
            return FALSE;
        value = (value << np->nBaseShift) | d;
    }
    *v = value;
    *bits = length;
    return TRUE;
}

/*
 * The signed types take what fits in them as the bits of the number say, a number of 32 bits with its top bit set being
 * negative; one of more bits than that is cut to the width of the type. The unsigned ones need the value to fit.
 */
static BOOL based_fits(const INT_TYPE *t, ULONGLONG v, int bits)
{
    if (!t->Signed)
        return t->Bits == 64 || v < (1ull << t->Bits);
    if (t->Bits >= 32 || bits > 32)
        return TRUE;
    if (bits < 32)
        return v < (1ull << t->Bits);
    return v < (1ull << t->Bits) || v >= 0x100000000ull - (1ull << (t->Bits - 1));
}

static HRESULT choose_based(const NUMPARSE *np, const BYTE *digits, ULONG vtbits, VARIANT *out)
{
    ULONGLONG v;
    int bits;
    if (np->cDig > (np->nBaseShift == 3 ? 22 : 16) || !based_value(np, digits, &v, &bits))
        return DISP_E_OVERFLOW;
    for (size_t i = 0; i < RTL_NUMBER_OF(g_int_types); i++) {
        const INT_TYPE *t = &g_int_types[i];
        if ((vtbits & (1u << t->Vt)) && based_fits(t, v, bits)) {
            put_integer(out, t->Vt, v);
            return S_OK;
        }
    }
    /* the others are negative when the number says so or its top bit does, and are weighed by the unsigned digits. */
    BOOL top = (bits == 32 || bits == 64) && ((v >> (bits - 1)) & 1);
    ULONGLONG magnitude = v;
    if (top)
        magnitude = bits == 32 ? (0 - (ULONGLONG)(ULONG)v) & 0xFFFFFFFFull : 0 - v;
    BYTE buffer[24];
    NUM n;
    num_from_integer(magnitude, top || (np->dwOutFlags & NUMPRS_NEG) != 0, np->cDig, np->nPwr10, buffer, &n);
    if (bits == 64)
        vtbits &= ~(ULONG)VTBIT_CY;
    /*
     * A hexadecimal number of 13 to 15 digits that a currency refuses, asked for with a decimal and no real, comes out as
     * a decimal made of its digits read as decimal ones, 0xFFFFFFFFFFFFF being 16666666666665. That is windows' answer.
     */
    WIDE cw;
    if ((vtbits & VTBIT_CY) && (vtbits & VTBIT_DECIMAL) && !(vtbits & (VTBIT_R4 | VTBIT_R8)) && np->cDig >= 13 &&
        np->cDig <= 15 && !currency_fits(&n, TRUE, &cw)) {
        WIDE w;
        memset(&w, 0, sizeof(w));
        for (int i = 0; i < np->cDig; i++)
            wide_mul_add(&w, 10, digits[i] & 15);
        put_decimal(out, &w, 0, n.Negative);
        return S_OK;
    }
    /*
     * A double keeps whole as many digits as were written. A float keeps as many as the value has and, when the power of
     * ten is not below zero, as were written.
     */
    LONGLONG places = 0;
    for (ULONGLONG t = magnitude; t; t /= 10)
        places++;
    LONGLONG written = np->cDig;
    if (np->nPwr10 >= 0 && written > places)
        places = written;
    if (np->nPwr10 > 0) {
        places += np->nPwr10;
        written += np->nPwr10;
    }
    return store_other(&n, vtbits, TRUE, places, written, out);
}

WINOLEAUTAPI HRESULT WINAPI VarNumFromParseNum(NUMPARSE *np, BYTE *digits, ULONG vtbits, VARIANT *out)
{
    if (!np || !digits || !out)
        return E_INVALIDARG;
    if (np->nBaseShift)
        return choose_based(np, digits, vtbits, out);
    NUM n;
    num_from_parse(np, digits, &n);
    return choose_decimal(&n, vtbits, out);
}
