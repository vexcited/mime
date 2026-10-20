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
DEFINE_INT_STORE(I8, LONGLONG, T_I8)
DEFINE_INT_STORE(UI8, ULONGLONG, T_UI8)

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
    if (d < -FLT_LIMIT || d > FLT_LIMIT)
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
    if (!(d < 2958466.0 && d > -657435.0))
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
    case NK_REAL: {
        char digits[48];
        int point;
        double d = n->R;
        int count = AutDoubleDigits(d, n->Precision, digits, &point);
        ULONGLONG bits;
        memcpy(&bits, &d, 8);
        if (((bits >> 52) & 0x7FF) == 0x7FF || (bits & 0x7FFFFFFFFFFFFFFFull) >= 0x44F0000000000000ull)
            return DISP_E_OVERFLOW;
        if (!count || point < -28) {
            out->wReserved = 0;
            out->scale = 0;
            out->sign = 0;
            out->Hi32 = 0;
            out->Lo64 = 0;
            return S_OK;
        }
        AUT_NUMBER num;
        memset(&num, 0, sizeof(num));
        num.Count = count;
        for (int i = 0; i < count; i++)
            num.Digits[i] = (BYTE)(digits[i] - '0');
        num.Exponent = point - count;
        num.Negative = d < 0;
        return AutDecimalFromNumber(&num, out);
    }
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
CONV(UI1, BYTE, Bool, VARIANT_BOOL, MK_INT)
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
CONV(UI2, USHORT, Bool, VARIANT_BOOL, MK_INT)
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
CONV(UI4, ULONG, Bool, VARIANT_BOOL, MK_INT)
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
CONV(UI8, ULONGLONG, Bool, VARIANT_BOOL, MK_INT)
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
CONV(I1, CHAR, Bool, VARIANT_BOOL, MK_INT)
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
CONV(Dec, DECIMAL, Bool, VARIANT_BOOL, MK_INT)

/* ------------------------------------------------------------------ the exports from strings */

static HRESULT str_number(LPCOLESTR text, LCID lcid, ULONG flags, AUT_NUMBER *n)
{
    return AutNumberFromString(text, lcid, flags, NUMPRS_STD, n, nullptr, nullptr);
}

static HRESULT str_to_int(LPCOLESTR text, LCID lcid, ULONG flags, const ITYPE *t, LONGLONG *out)
{
    AUT_NUMBER n;
    HRESULT hr = str_number(text, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    ULONG high;
    ULONGLONG low;
    if (!AutNumberToU96(&n, 0, &high, &low) || high)
        return DISP_E_OVERFLOW;
    BOOL neg = n.Negative && low;
    if (neg ? low > 0 - (ULONGLONG)t->Min : low > t->Max)
        return DISP_E_OVERFLOW;
    *out = neg ? (LONGLONG)(0 - low) : (LONGLONG)low;
    return S_OK;
}

#define STR_INT(Name, CT, TYPE)                                                                                        \
    WINOLEAUTAPI HRESULT WINAPI Var##Name##FromStr(LPCOLESTR in, LCID lcid, ULONG flags, CT *out)                      \
    {                                                                                                                  \
        LONGLONG v;                                                                                                    \
        if (!out)                                                                                                      \
            return E_INVALIDARG;                                                                                       \
        HRESULT hr = str_to_int(in, lcid, flags, &TYPE, &v);                                                           \
        if (SUCCEEDED(hr))                                                                                             \
            *out = (CT)v;                                                                                              \
        return hr;                                                                                                     \
    }
STR_INT(I1, CHAR, T_I1)
STR_INT(UI1, BYTE, T_UI1)
STR_INT(I2, SHORT, T_I2)
STR_INT(UI2, USHORT, T_UI2)
STR_INT(I4, LONG, T_I4)
STR_INT(UI4, ULONG, T_UI4)
STR_INT(I8, LONGLONG, T_I8)
STR_INT(UI8, ULONGLONG, T_UI8)

static HRESULT str_to_r8(LPCOLESTR in, LCID lcid, ULONG flags, double *out)
{
    AUT_NUMBER n;
    HRESULT hr = str_number(in, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    double d = AutNumberToDouble(&n);
    if (!(d >= -1.7976931348623157e308 && d <= 1.7976931348623157e308))
        return DISP_E_OVERFLOW;
    *out = d;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarR8FromStr(LPCOLESTR in, LCID lcid, ULONG flags, DOUBLE *out)
{
    return out ? str_to_r8(in, lcid, flags, out) : E_INVALIDARG;
}

WINOLEAUTAPI HRESULT WINAPI VarR4FromStr(LPCOLESTR in, LCID lcid, ULONG flags, FLOAT *out)
{
    double d;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_to_r8(in, lcid, flags, &d);
    if (FAILED(hr))
        return hr;
    if (d < -FLT_LIMIT || d > FLT_LIMIT)
        return DISP_E_OVERFLOW;
    *out = (float)d;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyFromStr(LPCOLESTR in, LCID lcid, ULONG flags, CY *out)
{
    AUT_NUMBER n;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_number(in, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    ULONG high;
    ULONGLONG low;
    if (!AutNumberToU96(&n, 4, &high, &low) || high || low > (n.Negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull))
        return DISP_E_OVERFLOW;
    out->int64 = n.Negative ? (LONGLONG)(0 - low) : (LONGLONG)low;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecFromStr(LPCOLESTR in, LCID lcid, ULONG flags, DECIMAL *out)
{
    AUT_NUMBER n;
    if (!out)
        return E_INVALIDARG;
    HRESULT hr = str_number(in, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    return AutDecimalFromNumber(&n, out);
}

/* the names of a boolean: windows keeps a list by language, English is first and the rest are a guess left out. */
static const WCHAR g_false_name[] = L"False", g_true_name[] = L"True";

WINOLEAUTAPI HRESULT WINAPI VarBoolFromStr(LPCOLESTR in, LCID lcid, ULONG flags, VARIANT_BOOL *out)
{
    if (!out || !in)
        return E_INVALIDARG;
    if (!wcscmp(in, L"#FALSE#") || !_wcsicmp(in, g_false_name)) {
        *out = VARIANT_FALSE;
        return S_OK;
    }
    if (!wcscmp(in, L"#TRUE#") || !_wcsicmp(in, g_true_name)) {
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
    *out = SysAllocString(in ? g_true_name : g_false_name);
    return *out ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromCy(CY value, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    BOOL negative = value.int64 < 0;
    ULONGLONG magnitude = negative ? 0 - (ULONGLONG)value.int64 : (ULONGLONG)value.int64;
    if (flags & LOCALE_USE_NLS) {
        /* the currency format of the locale, from the plain digits. */
        WCHAR digits[32];
        int n = AutFormatInteger((LONGLONG)(magnitude / 10000), TRUE, digits + (negative ? 1 : 0));
        if (negative)
            digits[0] = L'-';
        n += negative;
        digits[n++] = L'.';
        ULONG frac = (ULONG)(magnitude % 10000);
        digits[n++] = (WCHAR)(L'0' + frac / 1000);
        digits[n++] = (WCHAR)(L'0' + frac / 100 % 10);
        digits[n++] = (WCHAR)(L'0' + frac / 10 % 10);
        digits[n++] = (WCHAR)(L'0' + frac % 10);
        digits[n] = 0;
        WCHAR text[200];
        int written = GetCurrencyFormatW(lcid ? lcid : LOCALE_USER_DEFAULT, flags & LOCALE_NOUSEROVERRIDE, digits,
                                         nullptr, text, 200);
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
