/*
 * the conversions between the types of variants that have exports of their own: VarI2FromR8, VarBstrFromCy and the
 * rest of the family. Every number becomes one of five kinds first and is stored from there, so each target has its
 * rule for overflow and rounding once: floats round half to even to integers, a currency keeps four decimals, a
 * decimal is exact.
 */
#include "oleaut32_private.h"
#include "dtoa.h"
#include <mime/win32/winnls.h>

#define FLT_LIMIT 3.402823567797336e38

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
/* windows rejects floats beyond 2^62 for the 64 bit types, though the range would hold more. */
static const ITYPE T_I8 = {-9223372036854775807ll - 1, 9223372036854775807ull, -4.611686018427388e18,
                           4.611686018427388e18};
static const ITYPE T_UI8 = {0, 0xFFFFFFFFFFFFFFFFull, -0.5, 1.844674407370955e19};

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
 * a currency as a 64 bit integer: windows rounds with the low half of the value only, so a negative one that fits in 32
 * bits is always taken one further from zero and a large one is nudged by whatever the low half leaves. Kept as it is.
 */
static HRESULT cy_to_i8(LONGLONG cy, LONGLONG *out)
{
    LONGLONG q = cy / 10000;
    LONGLONG multiple = 10000 * q;
    LONGLONG low = (LONGLONG)(ULONG)cy - multiple;
    LONGLONG distance = low >= 0 ? low : multiple - (LONGLONG)(ULONG)cy;
    if ((q & 1) + distance > 5000)
        q += cy < 0 ? -1 : 1;
    *out = q;
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
 * an unsigned 64 bit integer from a decimal: without a scale the words are taken as they are, and only a negative one
 * with both low words set is refused, so minus one is one. With a scale the value goes through a double.
 */
static HRESULT store_UI8(const AUT_NUM *n, ULONGLONG *out)
{
    if (n->Kind == NK_DEC && AutDecimalValid(&n->D) && !n->D.scale) {
        const DECIMAL *d = &n->D;
        if (d->Hi32 || (d->sign && (ULONG)d->Lo64 && (ULONG)(d->Lo64 >> 32)))
            return DISP_E_OVERFLOW;
        *out = d->Lo64;
        return S_OK;
    }
    LONGLONG v;
    HRESULT hr = num_to_int(n, &T_UI8, &v);
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

static HRESULT store_R4(const AUT_NUM *n, FLOAT *out)
{
    if (n->Kind == NK_INT) {
        *out = (float)n->I;
        return S_OK;
    }
    double d;
    HRESULT hr = num_to_r8(n, &d);
    if (FAILED(hr))
        return hr;
    /* the lower limit is one step above minus the upper one, and a not a number is outside both. */
    static const ULONGLONG low_bits = 0xC7EFFFFFEFFFFFFFull;
    double low;
    memcpy(&low, &low_bits, 8);
    if (!(d >= low && d <= FLT_LIMIT))
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
        /* a not a number passes the limits and comes out as the smallest value, which is what windows' arithmetic does. */
        if (d != d) {
            out->int64 = (LONGLONG)0x8000000000000000ull;
            return S_OK;
        }
        if (!(d < 9.223372036854776e14 && d > -9.223372036854776e14))
            return DISP_E_OVERFLOW;
        double r = round_even(d * 10000.0);
        if (!(r < 9223372036854775808.0 && r >= -9223372036854775808.0))
            return DISP_E_OVERFLOW;
        out->int64 = (LONGLONG)r;
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
 * a float or a double as a decimal, the way windows does it and not by the shortest digits: the value is scaled by a
 * power of ten, taken from an estimate of its size, to seven or fifteen digits, rounded half to even, and then trailing
 * zeros are taken off but never more than the digits the type has, so a carry that makes eight digits of a float keeps
 * a zero. The estimate of the power is the exponent times log10(2) as a fraction of 65536.
 */
static HRESULT dec_from_real(double value, BOOL is_float, DECIMAL *out)
{
    ULONGLONG bits;
    memcpy(&bits, &value, 8);
    int exponent;
    int digits;
    double upper, normal;
    if (is_float) {
        float f = (float)value;
        ULONG fb;
        memcpy(&fb, &f, 4);
        exponent = (int)((fb >> 23) & 0xFF);
        if (exponent < 0x20)
            goto zero;
        if (exponent > 0xDE)
            return DISP_E_OVERFLOW;
        digits = 6;
        upper = 1e7;
        normal = 1e6;
        exponent -= 126;
    } else {
        exponent = (int)((bits >> 52) & 0x7FF);
        if (exponent < 0x3A0)
            goto zero;
        if (exponent > 0x45E)
            return DISP_E_OVERFLOW;
        digits = 14;
        upper = 1e15;
        normal = 1e14;
        exponent -= 1022;
    }
    {
        int estimate = (19728 * exponent) >> 16;
        int scale = digits - estimate;
        double v = value < 0 ? -value : value;
        if (scale < 0) {
            if (scale == -1 && v < upper)
                scale = 0;
            else
                v = v / pow10_double(estimate - digits);
        } else {
            if (scale > 28)
                scale = 28;
            v = v * pow10_double(scale);
        }
        if (v < normal && scale < 28) {
            v *= 10.0;
            scale++;
        }
        ULONGLONG m = (ULONGLONG)(LONGLONG)v;
        double fraction = v - (double)(LONGLONG)m;
        if (fraction > 0.5 || (fraction == 0.5 && (m & 1)))
            m++;
        if (!m)
            goto zero;
        out->wReserved = 0;
        out->sign = (bits >> 63) ? 0x80 : 0;
        if (scale >= 0) {
            int room = scale < digits ? scale : digits;
            for (int chunk = is_float ? 4 : 8; chunk; chunk >>= 1) {
                if (chunk <= room) {
                    ULONGLONG p = 1;
                    for (int i = 0; i < chunk; i++)
                        p *= 10;
                    if (m % p == 0) {
                        scale -= chunk;
                        m /= p;
                        room -= chunk;
                    }
                }
            }
            out->Lo64 = m;
            out->Hi32 = 0;
            out->scale = (BYTE)scale;
            return S_OK;
        }
        if (!mul_pow10_96(m, -scale, out))
            return DISP_E_OVERFLOW;
        out->scale = 0;
        return S_OK;
    }
zero:
    out->wReserved = 0;
    out->scale = 0;
    out->sign = 0;
    out->Hi32 = 0;
    out->Lo64 = 0;
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
 * the conversions from text parse the digits and ask for the value as the types of the function: the widths of the buffer
 * of digits and the types asked for are the ones windows uses, which is what decides how far a hexadecimal number reaches
 * and whether a fraction rounds away. The 32 and 64 bit signed integers add up the digits themselves.
 */
static HRESULT str_parse(LPCOLESTR in, LCID lcid, ULONG flags, int digit_count, NUMPARSE *np, BYTE *digits)
{
    memset(np, 0, sizeof(*np));
    np->dwInFlags = NUMPRS_STD;
    np->cDig = digit_count;
    return VarParseNumFromStr(in, lcid, flags, np, digits);
}

static HRESULT str_variant(LPCOLESTR in, LCID lcid, ULONG flags, int digit_count, ULONG type_bits, VARIANT *v)
{
    NUMPARSE np;
    BYTE digits[40];
    HRESULT hr = str_parse(in, lcid, flags, digit_count, &np, digits);
    if (FAILED(hr))
        return hr;
    memset(v, 0, sizeof(*v));
    return VarNumFromParseNum(&np, digits, type_bits, v);
}

/* the digits rounded to a whole number, half to even, once the whole part has been read from them. */
static BOOL str_rounds_up(const NUMPARSE *np, const BYTE *rest, int left, ULONGLONG whole)
{
    if (left <= 0)
        return FALSE;
    if (rest[0] > 5)
        return TRUE;
    if (rest[0] < 5)
        return FALSE;
    if (np->dwOutFlags & NUMPRS_INEXACT)
        return TRUE;
    for (int i = 1; i < left; i++)
        if (rest[i])
            return TRUE;
    return (whole & 1) != 0;
}

static ULONGLONG pow10_u64(int n)
{
    ULONGLONG r = 1;
    while (n-- > 0)
        r *= 10;
    return r;
}

/* a signed integer of 32 or 64 bits, bits being the width and digit_count what the buffer of digits holds. */
static HRESULT str_signed(LPCOLESTR in, LCID lcid, ULONG flags, int digit_count, int bits, LONGLONG *out)
{
    NUMPARSE np;
    BYTE rg[40];
    HRESULT hr = str_parse(in, lcid, flags, digit_count, &np, rg);
    if (FAILED(hr))
        return hr;
    ULONGLONG v = 0;
    const BYTE *p = rg;
    if (np.nBaseShift) {
        int left = np.cDig;
        if (bits == 32 ? !(left * np.nBaseShift <= 33 && (left * np.nBaseShift != 33 || rg[0] <= 3)) : left * np.nBaseShift > 65)
            return DISP_E_OVERFLOW;
        while (left-- > 0)
            v = *p++ + (v << np.nBaseShift);
        *out = bits == 32 ? (LONGLONG)(LONG)(ULONG)v : (LONGLONG)v;
        return S_OK;
    }
    int left = np.cDig, whole_digits = np.cDig + np.nPwr10;
    if (bits == 32 ? whole_digits >= 10 && (whole_digits != 10 || rg[0] > 2) : whole_digits > 19)
        return DISP_E_OVERFLOW;
    BOOL scale = FALSE;
    if (whole_digits > 0) {
        do {
            if (left <= 0)
                break;
            v = *p++ + 10 * v;
            whole_digits--;
            left--;
        } while (whole_digits > 0);
        if (whole_digits > 0) {
            v *= pow10_u64(whole_digits);
            scale = TRUE;
        }
    }
    if (!scale && left > 0 && whole_digits == 0 && str_rounds_up(&np, p, left, v))
        v++;
    LONGLONG signed_v = bits == 32 ? (LONGLONG)(LONG)(ULONG)v : (LONGLONG)v;
    if (np.dwOutFlags & NUMPRS_NEG) {
        signed_v = bits == 32 ? (LONGLONG)(LONG)(0 - (ULONG)v) : (LONGLONG)(0 - v);
        if (signed_v > 0)
            return DISP_E_OVERFLOW;
    } else if (signed_v < 0) {
        return DISP_E_OVERFLOW;
    }
    *out = signed_v;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarI4FromStr(LPCOLESTR in, LCID lcid, ULONG flags, LONG *out)
{
    LONGLONG v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_signed(in, lcid, flags, 11, 32, &v);
    if (SUCCEEDED(hr))
        *out = (LONG)v;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarI8FromStr(LPCOLESTR in, LCID lcid, ULONG flags, LONGLONG *out)
{
    if (!out)
        return E_INVALIDARG;
    return str_signed(in, lcid, flags, 21, 64, out);
}

WINOLEAUTAPI HRESULT WINAPI VarI2FromStr(LPCOLESTR in, LCID lcid, ULONG flags, SHORT *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 12, VTBIT_I2, &v);
    if (SUCCEEDED(hr))
        *out = v.iVal;
    return hr;
}

/* the one byte types read a short first, as windows does. */
WINOLEAUTAPI HRESULT WINAPI VarI1FromStr(LPCOLESTR in, LCID lcid, ULONG flags, CHAR *out)
{
    SHORT v = 0;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = VarI2FromStr(in, lcid, flags, &v);
    return SUCCEEDED(hr) ? VarI1FromI2(v, out) : hr;
}

WINOLEAUTAPI HRESULT WINAPI VarUI1FromStr(LPCOLESTR in, LCID lcid, ULONG flags, BYTE *out)
{
    SHORT v = 0;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = VarI2FromStr(in, lcid, flags, &v);
    return SUCCEEDED(hr) ? VarUI1FromUI2((USHORT)v, out) : hr;
}

WINOLEAUTAPI HRESULT WINAPI VarUI2FromStr(LPCOLESTR in, LCID lcid, ULONG flags, USHORT *out)
{
    LONG v = 0;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = VarI4FromStr(in, lcid, flags, &v);
    if (FAILED(hr))
        return hr;
    if ((ULONG)v > 0xFFFF)
        return DISP_E_OVERFLOW;
    *out = (USHORT)v;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarUI4FromStr(LPCOLESTR in, LCID lcid, ULONG flags, ULONG *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 11, VTBIT_UI4, &v);
    if (SUCCEEDED(hr))
        *out = v.ulVal;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarUI8FromStr(LPCOLESTR in, LCID lcid, ULONG flags, ULONGLONG *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 21, VTBIT_UI8, &v);
    if (SUCCEEDED(hr))
        *out = v.ullVal;
    return hr;
}

static HRESULT str_to_r8(LPCOLESTR in, LCID lcid, ULONG flags, double *out)
{
    VARIANT v;
    HRESULT hr = str_variant(in, lcid, flags, 20, VTBIT_R8, &v);
    if (SUCCEEDED(hr))
        *out = v.dblVal;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarR8FromStr(LPCOLESTR in, LCID lcid, ULONG flags, DOUBLE *out)
{
    return out ? str_to_r8(in, lcid, flags, out) : E_INVALIDARG;
}

WINOLEAUTAPI HRESULT WINAPI VarR4FromStr(LPCOLESTR in, LCID lcid, ULONG flags, FLOAT *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 12, VTBIT_R4, &v);
    if (SUCCEEDED(hr))
        *out = v.fltVal;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarCyFromStr(LPCOLESTR in, LCID lcid, ULONG flags, CY *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 21, VTBIT_CY, &v);
    if (SUCCEEDED(hr))
        *out = v.cyVal;
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarDecFromStr(LPCOLESTR in, LCID lcid, ULONG flags, DECIMAL *out)
{
    VARIANT v;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_variant(in, lcid, flags, 30, VTBIT_DECIMAL, &v);
    if (SUCCEEDED(hr))
        *out = v.decVal;
    return hr;
}

/*
 * the names of a boolean: windows keeps a list of 25 languages, the first being English, which is the one used unless the
 * flag for the local names is set. A language is found by its primary language or, where that is zero, by the locale.
 */
typedef struct {
    WORD Lcid, Language;
    const WCHAR *False, *True;
} BOOL_NAMES;

static const BOOL_NAMES g_bool_names[25] = {
    {0x409, 9, L"False", L"True"},
    {0x401, 1, L"False", L"True"},
    {0x804, 0, L"False", L"True"},
    {0x404, 4, L"False", L"True"},
    {0x405, 5, L"Nepravda", L"Pravda"},
    {0x406, 6, L"Falsk", L"Sand"},
    {0x407, 7, L"Falsch", L"Wahr"},
    {0x408, 8, L"\u03a8\u03b5\u03c5\u03b4\u03ad\u03c2", L"\u0391\u03bb\u03b7\u03b8\u03ad\u03c2"},
    {0x40a, 0xa, L"Falso", L"Verdadero"},
    {0x40b, 0xb, L"Ep\u00e4tosi", L"Tosi"},
    {0x40c, 0xc, L"Faux", L"Vrai"},
    {0x40d, 0xd, L"False", L"True"},
    {0x40e, 0xe, L"Hamis", L"Igaz"},
    {0x410, 0x10, L"Falso", L"Vero"},
    {0x411, 0x11, L"False", L"True"},
    {0x412, 0x12, L"False", L"True"},
    {0x413, 0x13, L"Onwaar", L"Waar"},
    {0x414, 0x14, L"Usann", L"Sann"},
    {0x415, 0x15, L"Fa\u0142sz", L"Prawda"},
    {0x816, 0, L"Falso", L"Verdadeiro"},
    {0x416, 0x16, L"Falso", L"Verdadeiro"},
    {0x419, 0x19, L"\u041b\u043e\u0436\u044c", L"\u0418\u0441\u0442\u0438\u043d\u0430"},
    {0x41d, 0x1d, L"Falskt", L"Sant"},
    {0x41f, 0x1f, L"Yanl\u0131\u015f", L"Do\u011fru"},
    {0x424, 0x24, L"Neresni\u010dno", L"Resni\u010dno"},
};

static const BOOL_NAMES *bool_names(LCID lcid, ULONG flags)
{
    if (!(flags & VARIANT_LOCALBOOL))
        return &g_bool_names[0];
    if (lcid == LOCALE_USER_DEFAULT)
        lcid = GetUserDefaultLCID();
    else if (lcid == LOCALE_SYSTEM_DEFAULT)
        lcid = GetSystemDefaultLCID();
    for (int i = 0; i < 25; i++)
        if ((g_bool_names[i].Language && g_bool_names[i].Language == (lcid & 0x3FF)) || lcid == g_bool_names[i].Lcid)
            return &g_bool_names[i];
    return &g_bool_names[0];
}

WINOLEAUTAPI HRESULT WINAPI VarBoolFromStr(LPCOLESTR in, LCID lcid, ULONG flags, VARIANT_BOOL *out)
{
    if (!out || !in)
        return E_INVALIDARG;
    const BOOL_NAMES *names = bool_names(lcid, flags);
    if (!wcscmp(in, L"#FALSE#") || !_wcsicmp(in, names->False)) {
        *out = VARIANT_FALSE;
        return S_OK;
    }
    if (!wcscmp(in, L"#TRUE#") || !_wcsicmp(in, names->True)) {
        *out = VARIANT_TRUE;
        return S_OK;
    }
    double d;
    HRESULT hr = str_to_r8(in, lcid, flags & ~(ULONG)VARIANT_LOCALBOOL, &d);
    if (FAILED(hr))
        return hr;
    *out = d == 0.0 ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
}

/* ------------------------------------------------------------------ the exports from dispatch objects */

HRESULT AutExtractValueProperty(IDispatch *object, LCID lcid, VARIANT *result)
{
    DISPPARAMS none = {nullptr, nullptr, 0, 0};
    result->vt = VT_EMPTY;
    if (!object)
        return DISP_E_TYPEMISMATCH;
    HRESULT hr = object->lpVtbl->Invoke(object, DISPID_VALUE, &GUID_NULL, lcid, DISPATCH_PROPERTYGET, &none, result,
                                        nullptr, nullptr);
    if (SUCCEEDED(hr) && result->vt == VT_DISPATCH) {
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

WINOLEAUTAPI HRESULT WINAPI VarBstrFromBool(VARIANT_BOOL in, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    const BOOL_NAMES *names = bool_names(lcid, flags);
    *out = SysAllocString(in ? names->True : names->False);
    return *out ? S_OK : E_OUTOFMEMORY;
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
