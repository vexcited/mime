/*
 * the conversions between the types of variants that have exports of their own: VarI2FromR8, VarBstrFromCy and the
 * rest of the family. Every number becomes one of five kinds first and is stored from there, so each target has its
 * rule for overflow and rounding once: floats round half to even to integers, a currency keeps four decimals, a
 * decimal is exact.
 */
#include "oleaut32_private.h"
#include "dtoa.h"
#include <mime/win32/winnls.h>


static double round_even(double x)
{
    const double magic = 4503599627370496.0;
    double a = x < 0 ? -x : x;
    if (a >= magic)
        return x;
    double r = (a + magic) - magic;
    return x < 0 ? -r : r;
}

BOOL AutRoundHalfEven(double x, double *rounded)
{
    if (!(x > -9.3e18 && x < 9.3e18))
        return FALSE;
    *rounded = round_even(x);
    return TRUE;
}

static ULONGLONG dbl_to_u64(double r)
{
    if (r >= 9223372036854775808.0)
        return (ULONGLONG)(LONGLONG)(r - 9223372036854775808.0) + 0x8000000000000000ull;
    return (ULONGLONG)(LONGLONG)r;
}

static double u64_to_dbl(ULONGLONG u)
{
    if ((LONGLONG)u >= 0)
        return (double)(LONGLONG)u;
    return (double)(LONGLONG)((u >> 1) | (u & 1)) * 2.0;
}

/* ------------------------------------------------------------------ numbers */

static AUT_NUM num_int(LONGLONG v)
{
    AUT_NUM n;
    n.Kind = NK_INT;
    n.Precision = 0;
    n.I = v;
    return n;
}

static AUT_NUM num_u64(ULONGLONG v)
{
    AUT_NUM n;
    n.Precision = 0;
    if ((LONGLONG)v >= 0) {
        n.Kind = NK_INT;
        n.I = (LONGLONG)v;
    } else {
        n.Kind = NK_UINT;
        n.U = v;
    }
    return n;
}

static AUT_NUM num_real(double v, int precision)
{
    AUT_NUM n;
    n.Kind = NK_REAL;
    n.Precision = precision;
    n.R = v;
    return n;
}

static AUT_NUM num_cy(CY v)
{
    AUT_NUM n;
    n.Kind = NK_CY;
    n.Precision = 0;
    n.I = v.int64;
    return n;
}

static AUT_NUM num_dec(const DECIMAL *v)
{
    AUT_NUM n;
    n.Kind = NK_DEC;
    n.Precision = 0;
    n.D = *v;
    return n;
}

#define MK_INT(x) num_int((LONGLONG)(x))
#define MK_U64(x) num_u64(x)
#define MK_R4(x) num_real((double)(x), 7)
#define MK_R8(x) num_real((x), 15)
#define MK_CY(x) num_cy(x)
#define MK_DEC(x) num_dec(x)

BOOL AutNumFromVariant(const VARIANT *v, AUT_NUM *n)
{
    switch (v->vt) {
    case VT_I1: *n = num_int(v->cVal); return TRUE;
    case VT_UI1: *n = num_int(v->bVal); return TRUE;
    case VT_I2:
    case VT_BOOL: *n = num_int(v->iVal); return TRUE;
    case VT_UI2: *n = num_int(v->uiVal); return TRUE;
    case VT_I4:
    case VT_INT: *n = num_int(v->lVal); return TRUE;
    case VT_UI4:
    case VT_UINT: *n = num_int(v->ulVal); return TRUE;
    case VT_I8: *n = num_int(v->llVal); return TRUE;
    case VT_UI8: *n = num_u64(v->ullVal); return TRUE;
    case VT_R4: *n = num_real(v->fltVal, 7); return TRUE;
    case VT_R8:
    case VT_DATE: *n = num_real(v->dblVal, 15); return TRUE;
    case VT_CY: *n = num_cy(v->cyVal); return TRUE;
    case VT_DECIMAL: *n = num_dec(&v->decVal); return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ integers */

typedef struct {
    LONGLONG Min;
    ULONGLONG Max;
    double Lo, Hi;
} ITYPE;

/* a double is in range when Lo <= it < Hi, which is where it rounds into the type. */
static const ITYPE T_I1 = {-128, 127, -128.5, 127.5};
static const ITYPE T_UI1 = {0, 255, -0.5, 255.5};
static const ITYPE T_I2 = {-32768, 32767, -32768.5, 32767.5};
static const ITYPE T_UI2 = {0, 65535, -0.5, 65535.5};
static const ITYPE T_I4 = {-2147483647ll - 1, 2147483647, -2147483648.5, 2147483647.5};
static const ITYPE T_UI4 = {0, 4294967295ull, -0.5, 4294967295.5};
/* windows rejects floats beyond 2^62 for the signed 64 bit type, though it would hold more, and takes the unsigned one up to 2^64. */
static const ITYPE T_I8 = {-9223372036854775807ll - 1, 9223372036854775807ull, -4611686018427387904.0,
                           4611686018427387904.0};
static const ITYPE T_UI8 = {0, 0xFFFFFFFFFFFFFFFFull, -0.5, 18446744073709551616.0};

static HRESULT num_magnitude(const AUT_NUM *n, const ITYPE *t, BOOL *neg, ULONGLONG *mag)
{
    switch (n->Kind) {
    case NK_INT:
        *neg = n->I < 0;
        *mag = *neg ? 0 - (ULONGLONG)n->I : (ULONGLONG)n->I;
        break;
    case NK_UINT:
        *neg = FALSE;
        *mag = n->U;
        break;
    case NK_REAL: {
        double d = n->R;
        if (!(d >= t->Lo && d < t->Hi))
            return DISP_E_OVERFLOW;
        double r = round_even(d);
        *neg = r < 0;
        *mag = dbl_to_u64(*neg ? -r : r);
        break;
    }
    case NK_CY: {
        LONGLONG v = n->I;
        *neg = v < 0;
        ULONGLONG m = *neg ? 0 - (ULONGLONG)v : (ULONGLONG)v;
        ULONGLONG q = m / 10000, r = m % 10000;
        if (r > 5000 || (r == 5000 && (q & 1)))
            q++;
        *mag = q;
        break;
    }
    default:
        if (!AutDecimalValid(&n->D))
            return E_INVALIDARG;
        if (!AutDecimalToInteger(&n->D, neg, mag))
            return DISP_E_OVERFLOW;
        break;
    }
    if (!*mag)
        *neg = FALSE;
    return S_OK;
}

static HRESULT num_to_int(const AUT_NUM *n, const ITYPE *t, LONGLONG *out)
{
    BOOL neg;
    ULONGLONG mag;
    HRESULT hr = num_magnitude(n, t, &neg, &mag);
    if (FAILED(hr))
        return hr;
    if (neg ? mag > 0 - (ULONGLONG)t->Min : mag > t->Max)
        return DISP_E_OVERFLOW;
    *out = neg ? (LONGLONG)(0 - mag) : (LONGLONG)mag;
    return S_OK;
}

/*
 * A currency as a 64 bit integer. Windows rounds with the low 32 bits of the stored value only, so a negative currency
 * that fits in 32 bits is always taken one further from zero, and a large one is nudged by whatever the low half leaves.
 */
static HRESULT cy_to_i8(LONGLONG cy, LONGLONG *out)
{
    LONGLONG whole = cy / 10000;
    LONGLONG whole_cy = whole * 10000;
    LONGLONG low = (LONGLONG)(ULONG)cy;
    LONGLONG distance = low - whole_cy;
    if (distance < 0)
        distance = whole_cy - low;
    if ((whole & 1) + distance > 5000)
        whole += cy < 0 ? -1 : 1;
    *out = whole;
    return S_OK;
}

#define DEFINE_INT_STORE(Name, CT, TYPE)                                                                               \
    static HRESULT store_##Name(const AUT_NUM *n, CT *out)                                                             \
    {                                                                                                                  \
        LONGLONG v;                                                                                                    \
        HRESULT hr = num_to_int(n, &TYPE, &v);                                                                         \
        if (SUCCEEDED(hr))                                                                                             \
            *out = (CT)v;                                                                                              \
        return hr;                                                                                                     \
    }
DEFINE_INT_STORE(I1, CHAR, T_I1)
DEFINE_INT_STORE(UI1, BYTE, T_UI1)
DEFINE_INT_STORE(I2, SHORT, T_I2)
DEFINE_INT_STORE(UI2, USHORT, T_UI2)
DEFINE_INT_STORE(I4, LONG, T_I4)
DEFINE_INT_STORE(UI4, ULONG, T_UI4)
static HRESULT store_I8(const AUT_NUM *n, LONGLONG *out)
{
    if (n->Kind == NK_CY)
        return cy_to_i8(n->I, out);
    return num_to_int(n, &T_I8, out);
}

/*
 * An unsigned 64 bit integer from a decimal: without a scale the words are taken as they are, and only a negative one
 * with both low words set is refused, so minus one is one. With a scale the value goes through a double.
 */
static HRESULT store_UI8(const AUT_NUM *n, ULONGLONG *out)
{
    AUT_NUM real;
    const AUT_NUM *source = n;
    if (n->Kind == NK_DEC) {
        const DECIMAL *d = &n->D;
        if (!AutDecimalValid(d))
            return E_INVALIDARG;
        if (!d->scale) {
            if (d->Hi32 || (d->sign && (ULONG)d->Lo64 && d->Lo64 >> 32))
                return DISP_E_OVERFLOW;
            *out = d->Lo64;
            return S_OK;
        }
        real = num_real(AutDecimalToDouble(d), 15);
        source = &real;
    }
    LONGLONG v;
    HRESULT hr = num_to_int(source, &T_UI8, &v);
    if (SUCCEEDED(hr))
        *out = (ULONGLONG)v;
    return hr;
}

/* ------------------------------------------------------------------ floats */

static HRESULT num_to_r8(const AUT_NUM *n, double *out)
{
    switch (n->Kind) {
    case NK_INT: *out = (double)n->I; break;
    case NK_UINT: *out = u64_to_dbl(n->U); break;
    case NK_REAL: *out = n->R; break;
    case NK_CY: *out = (double)n->I / 10000.0; break;
    default:
        if (!AutDecimalValid(&n->D))
            return E_INVALIDARG;
        *out = AutDecimalToDouble(&n->D);
        break;
    }
    return S_OK;
}

static HRESULT store_R8(const AUT_NUM *n, DOUBLE *out)
{
    return num_to_r8(n, out);
}

static double double_from_bits(ULONGLONG bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static HRESULT store_R4(const AUT_NUM *n, FLOAT *out)
{
    double d;
    HRESULT hr = num_to_r8(n, &d);
    if (FAILED(hr))
        return hr;
    /* from one step above minus the limit to the middle between the largest float and 2^128, which is a tie that goes to infinity. */
    if (!(d >= double_from_bits(0xC7EFFFFFEFFFFFFFull) && d <= double_from_bits(0x47EFFFFFF0000000ull)))
        return DISP_E_OVERFLOW;
    *out = (float)d;
    return S_OK;
}

static HRESULT store_Date(const AUT_NUM *n, DATE *out)
{
    double d;
    HRESULT hr = num_to_r8(n, &d);
    if (FAILED(hr))
        return hr;
    if (d >= 2958466.0 || d <= -657435.0)
        return DISP_E_OVERFLOW;
    *out = d;
    return S_OK;
}

/* ------------------------------------------------------------------ currency and decimal */

static HRESULT cy_from_dec(const DECIMAL *decimal, CY *result)
{
    if (!decimal || !result || !AutDecimalValid(decimal))
        return E_INVALIDARG;
    ULONG words[3] = {(ULONG)decimal->Lo64, (ULONG)(decimal->Lo64 >> 32), decimal->Hi32};
    if (decimal->scale < 4) {
        for (UINT i = decimal->scale; i < 4; i++) {
            ULONGLONG carry = 0;
            for (UINT j = 0; j < 3; j++) {
                ULONGLONG value = (ULONGLONG)words[j] * 10 + carry;
                words[j] = (ULONG)value;
                carry = value >> 32;
            }
            if (carry)
                return DISP_E_OVERFLOW;
        }
    } else if (decimal->scale > 4) {
        ULONG remainder = 0;
        BOOL sticky = FALSE;
        for (UINT i = 4; i < decimal->scale; i++) {
            sticky |= remainder != 0;
            ULONGLONG carry = 0;
            for (int j = 2; j >= 0; j--) {
                ULONGLONG value = (carry << 32) | words[j];
                words[j] = (ULONG)(value / 10);
                carry = value % 10;
            }
            remainder = (ULONG)carry;
        }
        if (remainder > 5 || (remainder == 5 && (sticky || (words[0] & 1)))) {
            if (++words[0] == 0 && ++words[1] == 0)
                ++words[2];
        }
    }
    ULONGLONG magnitude = ((ULONGLONG)words[1] << 32) | words[0];
    if (words[2] || magnitude > (decimal->sign ? 0x8000000000000000ull : 0x7fffffffffffffffull))
        return DISP_E_OVERFLOW;
    result->int64 = (LONGLONG)(decimal->sign ? 0ull - magnitude : magnitude);
    return S_OK;
}

/* a currency from a number: integers stop at a whole count that leaves the room windows keeps, floats at the range of the type. */
static HRESULT store_Cy(const AUT_NUM *n, CY *out)
{
    switch (n->Kind) {
    case NK_INT:
        if (n->I > 922337203685476ll || n->I < -922337203685476ll)
            return DISP_E_OVERFLOW;
        out->int64 = n->I * 10000;
        return S_OK;
    case NK_UINT: return DISP_E_OVERFLOW;
    case NK_REAL: {
        double d = n->R;
        if (d >= 9.223372036854776e14 || d <= -9.223372036854776e14)
            return DISP_E_OVERFLOW;
        if (d != d) {
            out->int64 = (LONGLONG)0x8000000000000000ull;
            return S_OK;
        }
        double scaled = round_even(d * 10000.0);
        if (!(scaled >= -9223372036854775808.0 && scaled < 9223372036854775808.0))
            return DISP_E_OVERFLOW;
        out->int64 = (LONGLONG)scaled;
        return S_OK;
    }
    case NK_CY: out->int64 = n->I; return S_OK;
    default: return cy_from_dec(&n->D, out);
    }
}


/* ten to a small power, as the double that windows has in a table. */
static double pow10_double(int n)
{
    static const double table[30] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14,
                                     1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29};
    double r = 1.0;
    while (n >= 30) {
        r *= 1e29;
        n -= 29;
    }
    return r * table[n];
}

static ULONGLONG power_of_ten(int n)
{
    ULONGLONG r = 1;
    while (n-- > 0)
        r *= 10;
    return r;
}

/* the digits times ten to a power, as the three words of a decimal; FALSE when they do not fit. */
static BOOL mul_pow10_96(ULONGLONG mantissa, int power, DECIMAL *out)
{
    ULONG w[3] = {(ULONG)mantissa, (ULONG)(mantissa >> 32), 0};
    for (int i = 0; i < power; i++) {
        ULONGLONG carry = 0;
        for (int j = 0; j < 3; j++) {
            ULONGLONG t = (ULONGLONG)w[j] * 10 + carry;
            w[j] = (ULONG)t;
            carry = t >> 32;
        }
        if (carry)
            return FALSE;
    }
    out->Lo64 = ((ULONGLONG)w[1] << 32) | w[0];
    out->Hi32 = w[2];
    return TRUE;
}

/*
 * A float or a double as a decimal, the way windows does it and not by the shortest digits: the value is scaled by a power
 * of ten from an estimate of its size so that a fixed count of digits is kept, rounded half to even, and then trailing
 * zeros are taken off, but never more than the digits kept, so a carry that makes a power of ten keeps a few zeros. The
 * estimate of the power is the exponent times log10(2) as a fraction of 65536.
 */
static HRESULT dec_from_real(double value, BOOL single, DECIMAL *out)
{
    ULONG field, field_min, field_max;
    int bias, kept;
    double small_limit, round_limit, magnitude;
    BOOL negative;
    if (single) {
        float f = (float)value;
        ULONG bits;
        memcpy(&bits, &f, sizeof(bits));
        negative = bits >> 31;
        field = bits >> 23 & 0xFF;
        field_min = 0x20, field_max = 0xDE, bias = 126, kept = 6;
        small_limit = 1e7, round_limit = 1e6;
        magnitude = negative ? -(double)f : (double)f;
    } else {
        ULONGLONG bits;
        memcpy(&bits, &value, sizeof(bits));
        negative = bits >> 63;
        field = (ULONG)(bits >> 52 & 0x7FF);
        field_min = 0x3A0, field_max = 0x45E, bias = 1022, kept = 14;
        small_limit = 1e15, round_limit = 1e14;
        magnitude = negative ? -value : value;
    }
    memset(out, 0, sizeof(*out));
    if (field < field_min)
        return S_OK;
    if (field > field_max)
        return DISP_E_OVERFLOW;

    int estimate = (19728 * ((int)field - bias)) >> 16;
    int scale = kept - estimate;
    double v = magnitude;
    if (scale < 0) {
        if (scale == -1 && v < small_limit)
            scale = 0;
        else
            v /= pow10_double(estimate - kept);
    } else {
        if (scale > 28)
            scale = 28;
        v *= pow10_double(scale);
    }
    if (v < round_limit && scale < 28) {
        v *= 10.0;
        scale++;
    }
    ULONGLONG digits = (ULONGLONG)v;
    double fraction = v - (double)digits;
    if (fraction > 0.5 || (fraction == 0.5 && (digits & 1)))
        digits++;
    if (!digits)
        return S_OK;

    if (scale < 0) {
        if (!mul_pow10_96(digits, -scale, out))
            return DISP_E_OVERFLOW;
    } else {
        static const int single_steps[] = {4, 2, 1}, double_steps[] = {8, 4, 2, 1};
        const int *steps = single ? single_steps : double_steps;
        int count = single ? 3 : 4;
        int room = scale < kept ? scale : kept;
        for (int i = 0; i < count; i++) {
            int step = steps[i];
            ULONGLONG divisor = power_of_ten(step);
            if (step <= room && digits % divisor == 0) {
                digits /= divisor;
                scale -= step;
                room -= step;
            }
        }
        out->Lo64 = digits;
        out->scale = (BYTE)scale;
    }
    if (negative)
        out->sign = 0x80;
    return S_OK;
}

static HRESULT store_Dec(const AUT_NUM *n, DECIMAL *out)
{
    switch (n->Kind) {
    case NK_INT:
        out->wReserved = 0;
        out->scale = 0;
        out->sign = n->I < 0 ? 0x80 : 0;
        out->Hi32 = 0;
        out->Lo64 = n->I < 0 ? 0 - (ULONGLONG)n->I : (ULONGLONG)n->I;
        return S_OK;
    case NK_UINT:
        out->wReserved = 0;
        out->scale = 0;
        out->sign = 0;
        out->Hi32 = 0;
        out->Lo64 = n->U;
        return S_OK;
    case NK_REAL: return dec_from_real(n->R, n->Precision == 7, out);
    case NK_CY:
        out->wReserved = 0;
        out->scale = 4;
        out->sign = n->I < 0 ? 0x80 : 0;
        out->Hi32 = 0;
        out->Lo64 = n->I < 0 ? 0 - (ULONGLONG)n->I : (ULONGLONG)n->I;
        return S_OK;
    default:
        if (!AutDecimalValid(&n->D))
            return E_INVALIDARG;
        *out = n->D;
        return S_OK;
    }
}

static HRESULT store_Bool(const AUT_NUM *n, VARIANT_BOOL *out)
{
    BOOL nonzero;
    switch (n->Kind) {
    case NK_INT:
    case NK_CY: nonzero = n->I != 0; break;
    case NK_UINT: nonzero = TRUE; break;
    case NK_REAL: nonzero = n->R != 0.0; break;
    default:
        if (!AutDecimalValid(&n->D))
            return E_INVALIDARG;
        nonzero = n->D.Lo64 || n->D.Hi32;
        break;
    }
    *out = nonzero ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

HRESULT AutNumStore(const AUT_NUM *n, VARTYPE vt, VARIANT *dest, LCID lcid, ULONG flags)
{
    HRESULT hr;
    VARIANT v;
    memset(&v, 0, sizeof(v));
    switch (vt) {
    case VT_I1: hr = store_I1(n, &v.cVal); break;
    case VT_UI1: hr = store_UI1(n, &v.bVal); break;
    case VT_I2: hr = store_I2(n, &v.iVal); break;
    case VT_UI2: hr = store_UI2(n, &v.uiVal); break;
    case VT_I4:
    case VT_INT: hr = store_I4(n, &v.lVal); break;
    case VT_UI4:
    case VT_UINT: hr = store_UI4(n, &v.ulVal); break;
    case VT_I8: hr = store_I8(n, &v.llVal); break;
    case VT_UI8: hr = store_UI8(n, &v.ullVal); break;
    case VT_R4: hr = store_R4(n, &v.fltVal); break;
    case VT_R8: hr = store_R8(n, &v.dblVal); break;
    case VT_DATE: hr = store_Date(n, &v.date); break;
    case VT_CY: hr = store_Cy(n, &v.cyVal); break;
    case VT_BOOL: hr = store_Bool(n, &v.boolVal); break;
    case VT_DECIMAL: {
        DECIMAL d;
        hr = store_Dec(n, &d);
        if (SUCCEEDED(hr))
            v.decVal = d;
        break;
    }
    default: return DISP_E_TYPEMISMATCH;
    }
    if (FAILED(hr))
        return hr;
    v.vt = vt;
    *dest = v;
    return S_OK;
}

/* ------------------------------------------------------------------ the exports from numbers */

/* a boolean is a short that the small types take as it is, the 32 and 64 bit ones sign extended: -1 is all ones. */
#define FROM_BOOL(To, ToT, value)                                                                                      \
    WINOLEAUTAPI HRESULT WINAPI Var##To##FromBool(VARIANT_BOOL in, ToT *out)                                           \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        *out = value;                                                                                                  \
        return S_OK;                                                                                                   \
    }

#define CONV(To, ToT, Src, SrcT, MK)                                                                                   \
    WINOLEAUTAPI HRESULT WINAPI Var##To##From##Src(SrcT in, ToT *out)                                                  \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        AUT_NUM n = MK(in);                                                                                            \
        return store_##To(&n, out);                                                                                    \
    }

CONV(UI1, BYTE, I2, SHORT, MK_INT)
CONV(UI1, BYTE, I4, LONG, MK_INT)
CONV(UI1, BYTE, I8, LONGLONG, MK_INT)
CONV(UI1, BYTE, UI2, USHORT, MK_INT)
CONV(UI1, BYTE, UI4, ULONG, MK_INT)
CONV(UI1, BYTE, UI8, ULONGLONG, MK_U64)
CONV(UI1, BYTE, I1, CHAR, MK_INT)
CONV(UI1, BYTE, R4, FLOAT, MK_R4)
CONV(UI1, BYTE, R8, DOUBLE, MK_R8)
CONV(UI1, BYTE, Date, DATE, MK_R8)
CONV(UI1, BYTE, Cy, CY, MK_CY)
FROM_BOOL(UI1, BYTE, (BYTE)in)
CONV(UI1, BYTE, Dec, const DECIMAL *, MK_DEC)
CONV(I2, SHORT, UI1, BYTE, MK_INT)
CONV(I2, SHORT, I4, LONG, MK_INT)
CONV(I2, SHORT, I8, LONGLONG, MK_INT)
CONV(I2, SHORT, UI2, USHORT, MK_INT)
CONV(I2, SHORT, UI4, ULONG, MK_INT)
CONV(I2, SHORT, UI8, ULONGLONG, MK_U64)
CONV(I2, SHORT, I1, CHAR, MK_INT)
CONV(I2, SHORT, R4, FLOAT, MK_R4)
CONV(I2, SHORT, R8, DOUBLE, MK_R8)
CONV(I2, SHORT, Date, DATE, MK_R8)
CONV(I2, SHORT, Cy, CY, MK_CY)
CONV(I2, SHORT, Bool, VARIANT_BOOL, MK_INT)
CONV(I2, SHORT, Dec, const DECIMAL *, MK_DEC)
CONV(I4, LONG, UI1, BYTE, MK_INT)
CONV(I4, LONG, I2, SHORT, MK_INT)
CONV(I4, LONG, I8, LONGLONG, MK_INT)
CONV(I4, LONG, UI2, USHORT, MK_INT)
CONV(I4, LONG, UI4, ULONG, MK_INT)
CONV(I4, LONG, UI8, ULONGLONG, MK_U64)
CONV(I4, LONG, I1, CHAR, MK_INT)
CONV(I4, LONG, R4, FLOAT, MK_R4)
CONV(I4, LONG, R8, DOUBLE, MK_R8)
CONV(I4, LONG, Date, DATE, MK_R8)
CONV(I4, LONG, Cy, CY, MK_CY)
CONV(I4, LONG, Bool, VARIANT_BOOL, MK_INT)
CONV(I4, LONG, Dec, const DECIMAL *, MK_DEC)
CONV(I8, LONGLONG, UI1, BYTE, MK_INT)
CONV(I8, LONGLONG, I2, SHORT, MK_INT)
CONV(I8, LONGLONG, UI2, USHORT, MK_INT)
CONV(I8, LONGLONG, UI4, ULONG, MK_INT)
CONV(I8, LONGLONG, UI8, ULONGLONG, MK_U64)
CONV(I8, LONGLONG, I1, CHAR, MK_INT)
CONV(I8, LONGLONG, R4, FLOAT, MK_R4)
CONV(I8, LONGLONG, R8, DOUBLE, MK_R8)
CONV(I8, LONGLONG, Date, DATE, MK_R8)
CONV(I8, LONGLONG, Cy, CY, MK_CY)
CONV(I8, LONGLONG, Bool, VARIANT_BOOL, MK_INT)
CONV(I8, LONGLONG, Dec, const DECIMAL *, MK_DEC)
CONV(UI2, USHORT, UI1, BYTE, MK_INT)
CONV(UI2, USHORT, I2, SHORT, MK_INT)
CONV(UI2, USHORT, I4, LONG, MK_INT)
CONV(UI2, USHORT, I8, LONGLONG, MK_INT)
CONV(UI2, USHORT, UI4, ULONG, MK_INT)
CONV(UI2, USHORT, UI8, ULONGLONG, MK_U64)
CONV(UI2, USHORT, I1, CHAR, MK_INT)
CONV(UI2, USHORT, R4, FLOAT, MK_R4)
CONV(UI2, USHORT, R8, DOUBLE, MK_R8)
CONV(UI2, USHORT, Date, DATE, MK_R8)
CONV(UI2, USHORT, Cy, CY, MK_CY)
FROM_BOOL(UI2, USHORT, (USHORT)in)
CONV(UI2, USHORT, Dec, const DECIMAL *, MK_DEC)
CONV(UI4, ULONG, UI1, BYTE, MK_INT)
CONV(UI4, ULONG, I2, SHORT, MK_INT)
CONV(UI4, ULONG, I4, LONG, MK_INT)
CONV(UI4, ULONG, I8, LONGLONG, MK_INT)
CONV(UI4, ULONG, UI2, USHORT, MK_INT)
CONV(UI4, ULONG, UI8, ULONGLONG, MK_U64)
CONV(UI4, ULONG, I1, CHAR, MK_INT)
CONV(UI4, ULONG, R4, FLOAT, MK_R4)
CONV(UI4, ULONG, R8, DOUBLE, MK_R8)
CONV(UI4, ULONG, Date, DATE, MK_R8)
CONV(UI4, ULONG, Cy, CY, MK_CY)
FROM_BOOL(UI4, ULONG, (ULONG)(LONG)in)
CONV(UI4, ULONG, Dec, const DECIMAL *, MK_DEC)
CONV(UI8, ULONGLONG, UI1, BYTE, MK_INT)
CONV(UI8, ULONGLONG, I2, SHORT, MK_INT)
CONV(UI8, ULONGLONG, I8, LONGLONG, MK_INT)
CONV(UI8, ULONGLONG, UI2, USHORT, MK_INT)
CONV(UI8, ULONGLONG, UI4, ULONG, MK_INT)
CONV(UI8, ULONGLONG, I1, CHAR, MK_INT)
CONV(UI8, ULONGLONG, R4, FLOAT, MK_R4)
CONV(UI8, ULONGLONG, R8, DOUBLE, MK_R8)
CONV(UI8, ULONGLONG, Date, DATE, MK_R8)
CONV(UI8, ULONGLONG, Cy, CY, MK_CY)
FROM_BOOL(UI8, ULONGLONG, (ULONGLONG)(LONGLONG)in)
CONV(UI8, ULONGLONG, Dec, const DECIMAL *, MK_DEC)
CONV(I1, CHAR, UI1, BYTE, MK_INT)
CONV(I1, CHAR, I2, SHORT, MK_INT)
CONV(I1, CHAR, I4, LONG, MK_INT)
CONV(I1, CHAR, I8, LONGLONG, MK_INT)
CONV(I1, CHAR, UI2, USHORT, MK_INT)
CONV(I1, CHAR, UI4, ULONG, MK_INT)
CONV(I1, CHAR, UI8, ULONGLONG, MK_U64)
CONV(I1, CHAR, R4, FLOAT, MK_R4)
CONV(I1, CHAR, R8, DOUBLE, MK_R8)
CONV(I1, CHAR, Date, DATE, MK_R8)
CONV(I1, CHAR, Cy, CY, MK_CY)
FROM_BOOL(I1, CHAR, (CHAR)in)
CONV(I1, CHAR, Dec, const DECIMAL *, MK_DEC)
CONV(R4, FLOAT, UI1, BYTE, MK_INT)
CONV(R4, FLOAT, I2, SHORT, MK_INT)
CONV(R4, FLOAT, I4, LONG, MK_INT)
CONV(R4, FLOAT, I8, LONGLONG, MK_INT)
CONV(R4, FLOAT, UI2, USHORT, MK_INT)
CONV(R4, FLOAT, UI4, ULONG, MK_INT)
CONV(R4, FLOAT, UI8, ULONGLONG, MK_U64)
CONV(R4, FLOAT, I1, CHAR, MK_INT)
CONV(R4, FLOAT, R8, DOUBLE, MK_R8)
CONV(R4, FLOAT, Date, DATE, MK_R8)
CONV(R4, FLOAT, Cy, CY, MK_CY)
CONV(R4, FLOAT, Bool, VARIANT_BOOL, MK_INT)
CONV(R4, FLOAT, Dec, const DECIMAL *, MK_DEC)
CONV(R8, DOUBLE, UI1, BYTE, MK_INT)
CONV(R8, DOUBLE, I2, SHORT, MK_INT)
CONV(R8, DOUBLE, I4, LONG, MK_INT)
CONV(R8, DOUBLE, I8, LONGLONG, MK_INT)
CONV(R8, DOUBLE, UI2, USHORT, MK_INT)
CONV(R8, DOUBLE, UI4, ULONG, MK_INT)
CONV(R8, DOUBLE, UI8, ULONGLONG, MK_U64)
CONV(R8, DOUBLE, I1, CHAR, MK_INT)
CONV(R8, DOUBLE, R4, FLOAT, MK_R4)
CONV(R8, DOUBLE, Date, DATE, MK_R8)
CONV(R8, DOUBLE, Cy, CY, MK_CY)
CONV(R8, DOUBLE, Bool, VARIANT_BOOL, MK_INT)
CONV(R8, DOUBLE, Dec, const DECIMAL *, MK_DEC)
CONV(Date, DATE, UI1, BYTE, MK_INT)
CONV(Date, DATE, I2, SHORT, MK_INT)
CONV(Date, DATE, I4, LONG, MK_INT)
CONV(Date, DATE, I8, LONGLONG, MK_INT)
CONV(Date, DATE, UI2, USHORT, MK_INT)
CONV(Date, DATE, UI4, ULONG, MK_INT)
CONV(Date, DATE, UI8, ULONGLONG, MK_U64)
CONV(Date, DATE, I1, CHAR, MK_INT)
CONV(Date, DATE, R4, FLOAT, MK_R4)
CONV(Date, DATE, R8, DOUBLE, MK_R8)
CONV(Date, DATE, Cy, CY, MK_CY)
CONV(Date, DATE, Bool, VARIANT_BOOL, MK_INT)
CONV(Date, DATE, Dec, const DECIMAL *, MK_DEC)
CONV(Cy, CY, UI1, BYTE, MK_INT)
CONV(Cy, CY, I2, SHORT, MK_INT)
CONV(Cy, CY, I4, LONG, MK_INT)
CONV(Cy, CY, I8, LONGLONG, MK_INT)
CONV(Cy, CY, UI2, USHORT, MK_INT)
CONV(Cy, CY, UI4, ULONG, MK_INT)
CONV(Cy, CY, UI8, ULONGLONG, MK_U64)
CONV(Cy, CY, I1, CHAR, MK_INT)
CONV(Cy, CY, R4, FLOAT, MK_R4)
CONV(Cy, CY, R8, DOUBLE, MK_R8)
CONV(Cy, CY, Date, DATE, MK_R8)
CONV(Cy, CY, Bool, VARIANT_BOOL, MK_INT)
CONV(Cy, CY, Dec, const DECIMAL *, MK_DEC)
CONV(Bool, VARIANT_BOOL, UI1, BYTE, MK_INT)
CONV(Bool, VARIANT_BOOL, I2, SHORT, MK_INT)
CONV(Bool, VARIANT_BOOL, I4, LONG, MK_INT)
CONV(Bool, VARIANT_BOOL, I8, LONGLONG, MK_INT)
CONV(Bool, VARIANT_BOOL, UI2, USHORT, MK_INT)
CONV(Bool, VARIANT_BOOL, UI4, ULONG, MK_INT)
CONV(Bool, VARIANT_BOOL, UI8, ULONGLONG, MK_U64)
CONV(Bool, VARIANT_BOOL, I1, CHAR, MK_INT)
CONV(Bool, VARIANT_BOOL, R4, FLOAT, MK_R4)
CONV(Bool, VARIANT_BOOL, R8, DOUBLE, MK_R8)
CONV(Bool, VARIANT_BOOL, Date, DATE, MK_R8)
CONV(Bool, VARIANT_BOOL, Cy, CY, MK_CY)
CONV(Bool, VARIANT_BOOL, Dec, const DECIMAL *, MK_DEC)
CONV(Dec, DECIMAL, UI1, BYTE, MK_INT)
CONV(Dec, DECIMAL, I2, SHORT, MK_INT)
CONV(Dec, DECIMAL, I4, LONG, MK_INT)
CONV(Dec, DECIMAL, I8, LONGLONG, MK_INT)
CONV(Dec, DECIMAL, UI2, USHORT, MK_INT)
CONV(Dec, DECIMAL, UI4, ULONG, MK_INT)
CONV(Dec, DECIMAL, UI8, ULONGLONG, MK_U64)
CONV(Dec, DECIMAL, I1, CHAR, MK_INT)
CONV(Dec, DECIMAL, R4, FLOAT, MK_R4)
CONV(Dec, DECIMAL, R8, DOUBLE, MK_R8)
CONV(Dec, DECIMAL, Date, DATE, MK_R8)
CONV(Dec, DECIMAL, Cy, CY, MK_CY)
WINOLEAUTAPI HRESULT WINAPI VarDecFromBool(VARIANT_BOOL in, DECIMAL *out)
{
    if (!out)
        return E_INVALIDARG;
    out->wReserved = 0;
    out->scale = 0;
    out->sign = in ? 0x80 : 0;
    out->Hi32 = 0;
    out->Lo64 = in ? 1 : 0;
    return S_OK;
}

/* ------------------------------------------------------------------ the exports from strings */

/*
 * The conversions from text read the digits with all the parse flags and ask for one type. The size of the buffer of
 * digits is what decides how far a hexadecimal number reaches and whether a fraction rounds away.
 */
static HRESULT read_digits(LPCOLESTR text, LCID lcid, ULONG flags, int capacity, NUMPARSE *np, BYTE *digits)
{
    memset(np, 0, sizeof(*np));
    np->cDig = capacity;
    np->dwInFlags = NUMPRS_STD;
    return VarParseNumFromStr(text, lcid, flags, np, digits);
}

static HRESULT read_typed(LPCOLESTR text, LCID lcid, ULONG flags, int capacity, ULONG type_bit, VARIANT *value)
{
    NUMPARSE np;
    BYTE digits[32];
    HRESULT hr = read_digits(text, lcid, flags, capacity, &np, digits);
    if (FAILED(hr))
        return hr;
    memset(value, 0, sizeof(*value));
    return VarNumFromParseNum(&np, digits, type_bit, value);
}

/* 32 and 64 bit integers add the digits up themselves, so a based number is a bit pattern and a fraction rounds to even. */
static HRESULT read_integer(LPCOLESTR text, LCID lcid, ULONG flags, int capacity, int bits, ULONGLONG *out)
{
    NUMPARSE np;
    BYTE digits[32];
    HRESULT hr = read_digits(text, lcid, flags, capacity, &np, digits);
    if (FAILED(hr))
        return hr;
    int count = np.cDig;
    ULONGLONG value = 0;
    if (np.nBaseShift) {
        int width = count * np.nBaseShift;
        if (bits == 32 ? width > 33 || (width == 33 && digits[0] > 3) : width > 65)
            return DISP_E_OVERFLOW;
        for (int i = 0; i < count; i++)
            value = value << np.nBaseShift | digits[i];
        *out = bits == 32 ? (ULONGLONG)(ULONG)value : value;
        return S_OK;
    }

    int whole = count + np.nPwr10;
    if (bits == 32 ? whole >= 10 && (whole > 10 || digits[0] > 2) : whole > 19)
        return DISP_E_OVERFLOW;
    if (whole > 0) {
        int taken = whole < count ? whole : count;
        for (int i = 0; i < taken; i++)
            value = value * 10 + digits[i];
        if (taken < whole)
            value *= power_of_ten(whole - taken);
        else if (taken < count) {
            int first = digits[taken];
            BOOL later = FALSE;
            for (int i = taken + 1; i < count; i++)
                later |= digits[i] != 0;
            if (first > 5 || (first == 5 && ((np.dwOutFlags & NUMPRS_INEXACT) || later || (value & 1))))
                value++;
        }
    } else if (whole == 0 && count > 0) {
        int first = digits[0];
        BOOL later = FALSE;
        for (int i = 1; i < count; i++)
            later |= digits[i] != 0;
        if (first > 5 || (first == 5 && ((np.dwOutFlags & NUMPRS_INEXACT) || later)))
            value++;
    }

    ULONGLONG mask = bits == 32 ? 0xFFFFFFFFull : ~0ull;
    ULONGLONG sign_bit = bits == 32 ? 0x80000000ull : 0x8000000000000000ull;
    value &= mask;
    if (np.dwOutFlags & NUMPRS_NEG) {
        value = (0 - value) & mask;
        if (value && !(value & sign_bit))
            return DISP_E_OVERFLOW;
    } else if (value & sign_bit) {
        return DISP_E_OVERFLOW;
    }
    *out = value;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarI4FromStr(LPCOLESTR in, LCID lcid, ULONG flags, LONG *out)
{
    if (!out)
        return E_INVALIDARG;
    ULONGLONG value;
    HRESULT hr = read_integer(in, lcid, flags, 11, 32, &value);
    if (SUCCEEDED(hr))
        *out = (LONG)value;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarI8FromStr(LPCOLESTR in, LCID lcid, ULONG flags, LONGLONG *out)
{
    if (!out)
        return E_INVALIDARG;
    ULONGLONG value;
    HRESULT hr = read_integer(in, lcid, flags, 21, 64, &value);
    if (SUCCEEDED(hr))
        *out = (LONGLONG)value;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarI2FromStr(LPCOLESTR in, LCID lcid, ULONG flags, SHORT *out)
{
    if (!out)
        return E_INVALIDARG;
    VARIANT value;
    HRESULT hr = read_typed(in, lcid, flags, 12, VTBIT_I2, &value);
    if (SUCCEEDED(hr))
        *out = value.iVal;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarI1FromStr(LPCOLESTR in, LCID lcid, ULONG flags, CHAR *out)
{
    if (!out)
        return E_INVALIDARG;
    SHORT value;
    HRESULT hr = VarI2FromStr(in, lcid, flags, &value);
    if (FAILED(hr))
        return hr;
    if (value < -128 || value > 127)
        return DISP_E_OVERFLOW;
    *out = (CHAR)value;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarUI1FromStr(LPCOLESTR in, LCID lcid, ULONG flags, BYTE *out)
{
    if (!out)
        return E_INVALIDARG;
    SHORT value;
    HRESULT hr = VarI2FromStr(in, lcid, flags, &value);
    if (FAILED(hr))
        return hr;
    if ((USHORT)value > 255)
        return DISP_E_OVERFLOW;
    *out = (BYTE)value;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarUI2FromStr(LPCOLESTR in, LCID lcid, ULONG flags, USHORT *out)
{
    if (!out)
        return E_INVALIDARG;
    LONG value;
    HRESULT hr = VarI4FromStr(in, lcid, flags, &value);
    if (FAILED(hr))
        return hr;
    if ((ULONG)value > 0xFFFF)
        return DISP_E_OVERFLOW;
    *out = (USHORT)value;
    return S_OK;
}

#define STR_EXPORT(Name, CT, capacity, bit, field)                                                                     \
    WINOLEAUTAPI HRESULT WINAPI Var##Name##FromStr(LPCOLESTR in, LCID lcid, ULONG flags, CT *out)                     \
    {                                                                                                                  \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        VARIANT value;                                                                                                 \
        HRESULT hr = read_typed(in, lcid, flags, capacity, bit, &value);                                               \
        if (SUCCEEDED(hr))                                                                                             \
            *out = value.field;                                                                                        \
        return hr;                                                                                                     \
    }

STR_EXPORT(UI4, ULONG, 11, VTBIT_UI4, ulVal)
STR_EXPORT(UI8, ULONGLONG, 21, VTBIT_UI8, ullVal)
STR_EXPORT(R4, FLOAT, 12, VTBIT_R4, fltVal)
STR_EXPORT(R8, DOUBLE, 20, VTBIT_R8, dblVal)
STR_EXPORT(Cy, CY, 21, VTBIT_CY, cyVal)

WINOLEAUTAPI HRESULT WINAPI VarDecFromStr(LPCOLESTR in, LCID lcid, ULONG flags, DECIMAL *out)
{
    if (!out)
        return E_INVALIDARG;
    VARIANT value;
    HRESULT hr = read_typed(in, lcid, flags, 30, VTBIT_DECIMAL, &value);
    /* the first two bytes of the decimal stay what the parse left, the type of the variant it was made in. */
    if (SUCCEEDED(hr))
        *out = value.decVal;
    return hr;
}

/* ------------------------------------------------------------------ the names of booleans */

typedef struct {
    LCID Lcid;
    /* the primary language, or zero where only the exact locale matches. */
    USHORT Language;
    const WCHAR *False, *True;
} BOOL_NAMES;

static const BOOL_NAMES g_bool_names[] = {
    {0x409, 9, L"False", L"True"},
    {0x401, 1, L"False", L"True"},
    {0x804, 0, L"False", L"True"},
    {0x404, 4, L"False", L"True"},
    {0x405, 5, L"Nepravda", L"Pravda"},
    {0x406, 6, L"Falsk", L"Sand"},
    {0x407, 7, L"Falsch", L"Wahr"},
    {0x408, 8, L"Ψευδές", L"Αληθές"},
    {0x40a, 0xa, L"Falso", L"Verdadero"},
    {0x40b, 0xb, L"Epätosi", L"Tosi"},
    {0x40c, 0xc, L"Faux", L"Vrai"},
    {0x40d, 0xd, L"False", L"True"},
    {0x40e, 0xe, L"Hamis", L"Igaz"},
    {0x410, 0x10, L"Falso", L"Vero"},
    {0x411, 0x11, L"False", L"True"},
    {0x412, 0x12, L"False", L"True"},
    {0x413, 0x13, L"Onwaar", L"Waar"},
    {0x414, 0x14, L"Usann", L"Sann"},
    {0x415, 0x15, L"Fałsz", L"Prawda"},
    {0x816, 0, L"Falso", L"Verdadeiro"},
    {0x416, 0x16, L"Falso", L"Verdadeiro"},
    {0x419, 0x19, L"Ложь", L"Истина"},
    {0x41d, 0x1d, L"Falskt", L"Sant"},
    {0x41f, 0x1f, L"Yanlış", L"Doğru"},
    {0x424, 0x24, L"Neresnično", L"Resnično"},
};

/* English unless the local names were asked for and the locale has some. */
static const BOOL_NAMES *bool_names_for(LCID lcid, ULONG flags)
{
    if (!(flags & VARIANT_LOCALBOOL))
        return &g_bool_names[0];
    if (lcid == LOCALE_USER_DEFAULT)
        lcid = GetUserDefaultLCID();
    else if (lcid == LOCALE_SYSTEM_DEFAULT)
        lcid = GetSystemDefaultLCID();
    for (size_t i = 0; i < RTL_NUMBER_OF(g_bool_names); i++) {
        const BOOL_NAMES *names = &g_bool_names[i];
        if ((names->Language && names->Language == (lcid & 0x3FF)) || names->Lcid == lcid)
            return names;
    }
    return &g_bool_names[0];
}

/* only the letters of the alphabet of ASCII fold, whatever the language is. */
static BOOL same_name_ignoring_case(const WCHAR *text, const WCHAR *name)
{
    for (; *text && *name; text++, name++) {
        WCHAR a = *text, b = *name;
        if (a >= L'A' && a <= L'Z')
            a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z')
            b += L'a' - L'A';
        if (a != b)
            return FALSE;
    }
    return !*text && !*name;
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromBool(VARIANT_BOOL in, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    const BOOL_NAMES *names = bool_names_for(lcid, flags);
    *out = SysAllocString(in ? names->True : names->False);
    return *out ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI HRESULT WINAPI VarBoolFromStr(LPCOLESTR in, LCID lcid, ULONG flags, VARIANT_BOOL *out)
{
    if (!out || !in)
        return E_INVALIDARG;
    const BOOL_NAMES *names = bool_names_for(lcid, flags);
    BOOL value;
    if (!wcscmp(in, L"#FALSE#") || same_name_ignoring_case(in, names->False)) {
        value = FALSE;
    } else if (!wcscmp(in, L"#TRUE#") || same_name_ignoring_case(in, names->True)) {
        value = TRUE;
    } else {
        double number;
        HRESULT hr = VarR8FromStr(in, lcid, flags & ~(ULONG)VARIANT_LOCALBOOL, &number);
        if (FAILED(hr))
            return hr;
        value = number != 0.0;
    }
    *out = value ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

/* ------------------------------------------------------------------ the value of an object */

/* the default property of an object, which may not be another object. */
HRESULT AutExtractValueProperty(IDispatch *object, LCID lcid, VARIANT *result)
{
    VariantInit(result);
    if (!object)
        return DISP_E_TYPEMISMATCH;
    DISPPARAMS no_arguments = {nullptr, nullptr, 0, 0};
    HRESULT hr = object->lpVtbl->Invoke(object, DISPID_VALUE, &IID_NULL, lcid, DISPATCH_PROPERTYGET, &no_arguments,
                                        result, nullptr, nullptr);
    if (FAILED(hr))
        return hr;
    if (result->vt == VT_DISPATCH) {
        VariantClear(result);
        return DISP_E_TYPEMISMATCH;
    }
    return hr;
}

static HRESULT disp_value(IDispatch *object, LCID lcid, VARTYPE vt, VARIANT *out)
{
    VARIANT value;
    HRESULT hr = AutExtractValueProperty(object, lcid, &value);
    if (FAILED(hr))
        return hr;
    VariantInit(out);
    hr = VariantChangeTypeEx(out, &value, lcid, 0, vt);
    VariantClear(&value);
    return hr;
}

#define FROM_DISP(Name, CT, VT, FIELD)                                                                                 \
    WINOLEAUTAPI HRESULT WINAPI Var##Name##FromDisp(IDispatch *in, LCID lcid, CT *out)                                 \
    {                                                                                                                  \
        VARIANT v;                                                                                                     \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        HRESULT hr = disp_value(in, lcid, VT, &v);                                                                     \
        if (SUCCEEDED(hr))                                                                                             \
            *out = v.FIELD;                                                                                            \
        return hr;                                                                                                     \
    }
FROM_DISP(UI1, BYTE, VT_UI1, bVal)
FROM_DISP(I2, SHORT, VT_I2, iVal)
FROM_DISP(I4, LONG, VT_I4, lVal)
FROM_DISP(I8, LONGLONG, VT_I8, llVal)
FROM_DISP(UI2, USHORT, VT_UI2, uiVal)
FROM_DISP(UI4, ULONG, VT_UI4, ulVal)
FROM_DISP(UI8, ULONGLONG, VT_UI8, ullVal)
FROM_DISP(I1, CHAR, VT_I1, cVal)
FROM_DISP(R4, FLOAT, VT_R4, fltVal)
FROM_DISP(R8, DOUBLE, VT_R8, dblVal)
FROM_DISP(Date, DATE, VT_DATE, date)
FROM_DISP(Cy, CY, VT_CY, cyVal)
FROM_DISP(Bool, VARIANT_BOOL, VT_BOOL, boolVal)
FROM_DISP(Dec, DECIMAL, VT_DECIMAL, decVal)

WINOLEAUTAPI HRESULT WINAPI VarBstrFromDisp(IDispatch *in, LCID lcid, ULONG flags, BSTR *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = disp_value(in, lcid, VT_BSTR, &v);
    if (SUCCEEDED(hr))
        *out = v.bstrVal;
    return hr;
}

/* ------------------------------------------------------------------ the exports to strings */

#define BSTR_INTEGER(Src, SrcT, UNSIGNED)                                                                              \
    WINOLEAUTAPI HRESULT WINAPI VarBstrFrom##Src(SrcT in, LCID lcid, ULONG flags, BSTR *out)                           \
    {                                                                                                                  \
        return AutBstrFromInteger((LONGLONG)in, UNSIGNED, lcid, flags, out);                                           \
    }
BSTR_INTEGER(UI1, BYTE, TRUE)
BSTR_INTEGER(I2, SHORT, FALSE)
BSTR_INTEGER(I4, LONG, FALSE)
BSTR_INTEGER(I8, LONGLONG, FALSE)
BSTR_INTEGER(UI2, USHORT, TRUE)
BSTR_INTEGER(UI4, ULONG, TRUE)
BSTR_INTEGER(UI8, ULONGLONG, TRUE)
BSTR_INTEGER(I1, CHAR, FALSE)

WINOLEAUTAPI HRESULT WINAPI VarBstrFromR4(FLOAT in, LCID lcid, ULONG flags, BSTR *out)
{
    return AutBstrFromDouble((double)in, 7, lcid, flags, out);
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromR8(DOUBLE in, LCID lcid, ULONG flags, BSTR *out)
{
    return AutBstrFromDouble(in, 15, lcid, flags, out);
}


WINOLEAUTAPI HRESULT WINAPI VarBstrFromCy(CY value, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    BOOL negative = value.int64 < 0;
    ULONGLONG magnitude = negative ? 0 - (ULONGLONG)value.int64 : (ULONGLONG)value.int64;
    if (flags & LOCALE_USE_NLS) {
        /* the digits with a point put in four from the end. */
        WCHAR whole[32];
        int length = AutFormatInteger(value.int64, FALSE, whole);
        /* with fewer than four digits windows works in front of its buffer, and what comes out depends on what was there. */
        if (length < 4)
            return E_INVALIDARG;
        WCHAR digits[40];
        int n = 0;
        int at = length - 4;
        for (int i = 0; i < at; i++)
            digits[n++] = whole[i];
        digits[n++] = L'.';
        for (int i = at; i < length; i++)
            digits[n++] = whole[i];
        digits[n] = 0;
        WCHAR text[200];
        int written = GetCurrencyFormatW(lcid, flags & ~(ULONG)LOCALE_USE_NLS, digits, nullptr, text, 200);
        if (!written)
            return E_INVALIDARG;
        *out = SysAllocString(text);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    return AutFormatDecimal(negative, 0, magnitude, 4, lcid, flags, out);
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromDec(const DECIMAL *value, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out || !value)
        return E_INVALIDARG;
    if (!AutDecimalValid(value))
        return E_INVALIDARG;
    return AutFormatDecimal(value->sign != 0, value->Hi32, value->Lo64, value->scale, lcid, flags, out);
}
