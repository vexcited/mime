/*
 * arithmetic on variants: the operands are checked for a type the operation takes, then both go to one type and the
 * result is the smallest type of a chain that holds it. the chains are 8 bit to 16 to 32 bit integers to a double, a
 * 64 bit integer or a single to a double, and a date to a double when the value is out of the range of dates.
 */
#include "oleaut32_private.h"

double __cdecl modf(double x, double *whole);

#define LCID_DEFAULT 0x0400
#define FLT_MAX_VALUE 3.4028234663852886e38

static BOOL not_finite(double d)
{
    return !(d - d == 0.0);
}

/* the types an operation takes. */
#define TYPES_ARITH 0x00124FFFul
#define TYPES_WIDE 0x00FF4FFFul

static BOOL supported(ULONG mask, VARTYPE vt)
{
    return vt < 32 && (mask >> vt & 1);
}

static BOOL is_array_or_record(VARTYPE vt)
{
    return (vt & VT_ARRAY) || vt == VT_RECORD;
}

/* a type beyond the table of the operation. */
static BOOL beyond(ULONG mask, VARTYPE vt)
{
    return mask == TYPES_ARITH ? vt > VT_UI1 && vt != VT_I8 : vt > VT_UINT;
}

/*
 * a type nothing takes is a mismatch for an array or a record and a bad type for the rest. the right operand is looked
 * at first, and only the types beyond the table of the operation count for it.
 */
static HRESULT screen(ULONG mask, const VARIANT *left, const VARIANT *right)
{
    VARTYPE r = right->vt, l = left->vt;
    if (beyond(mask, r))
        return is_array_or_record(r) ? DISP_E_TYPEMISMATCH : DISP_E_BADVARTYPE;
    if (is_array_or_record(l))
        return DISP_E_TYPEMISMATCH;
    if (!supported(mask, l) || !supported(mask, r))
        return DISP_E_BADVARTYPE;
    return S_OK;
}

/* a value of the given type from any operand, with the conversion rules of variants. */
static HRESULT convert_f(const VARIANT *src, VARTYPE vt, VARIANT *dst, USHORT flags)
{
    VARIANT in = *src;
    VariantInit(dst);
    if (in.vt == vt) {
        if (vt == VT_BSTR)
            return VariantCopy(dst, &in);
        *dst = in;
        return S_OK;
    }
    HRESULT hr = VariantChangeTypeEx(dst, &in, LCID_DEFAULT, flags, vt);
    if (hr == DISP_E_TYPEMISMATCH && in.vt == VT_BSTR && (flags & VARIANT_ALPHABOOL)) {
        VARIANT_BOOL b;
        if (SUCCEEDED(VarBoolFromStr(in.bstrVal, LCID_DEFAULT, VARIANT_ALPHABOOL, &b))) {
            VARIANT t;
            t.vt = VT_BOOL;
            t.llVal = 0;
            t.boolVal = b;
            return VariantChangeTypeEx(dst, &t, LCID_DEFAULT, 0, vt);
        }
    }
    return hr;
}

static HRESULT convert(const VARIANT *src, VARTYPE vt, VARIANT *dst)
{
    return convert_f(src, vt, dst, 0);
}

static BOOL date_fits(double d)
{
    return d > -657435.0 && d < 2958466.0;
}

/* ------------------------------------------------------------------ the kinds of operand */

typedef enum { K_UI1, K_I2, K_I4, K_I8, K_R4, K_R8, K_DATE, K_CY, K_DEC } KIND;

static KIND kind_of(VARTYPE vt)
{
    switch (vt) {
    case VT_UI1: return K_UI1;
    case VT_I2:
    case VT_BOOL:
    case VT_EMPTY: return K_I2;
    case VT_I4: return K_I4;
    case VT_I8: return K_I8;
    case VT_R4: return K_R4;
    case VT_DATE: return K_DATE;
    case VT_CY: return K_CY;
    case VT_DECIMAL: return K_DEC;
    default: return K_R8;
    }
}

static const VARTYPE vt_of_kind[] = {VT_UI1, VT_I2, VT_I4, VT_I8, VT_R4, VT_R8, VT_DATE, VT_CY, VT_DECIMAL};

typedef enum { OP_ADD, OP_SUB, OP_MUL } ARITH;
typedef enum { DIV_IDIV, DIV_MOD, DIV_POW, DIV_DIV } DIVISION;

static BOOL is_integer(KIND k)
{
    return k <= K_I8;
}

/* the type both sides of an operation are brought to. */
static KIND common_kind(ARITH op, KIND a, KIND b)
{
    if (a == K_DEC || b == K_DEC)
        return K_DEC;
    if (op == OP_MUL) {
        if (a == K_CY || b == K_CY) {
            if ((is_integer(a) || a == K_CY) && (is_integer(b) || b == K_CY))
                return K_CY;
            return K_R8;
        }
        if (a == K_DATE || b == K_DATE)
            return K_R8;
    } else {
        if (op == OP_SUB && a == K_DATE && b == K_DATE)
            return K_R8;
        if (a == K_DATE || b == K_DATE)
            return K_DATE;
        if (a == K_CY || b == K_CY)
            return K_CY;
    }
    if (a == K_R8 || b == K_R8)
        return K_R8;
    if (a == K_R4 || b == K_R4) {
        KIND o = a == K_R4 ? b : a;
        return o == K_R4 || o == K_UI1 || o == K_I2 ? K_R4 : K_R8;
    }
    if (a == K_I8 || b == K_I8)
        return K_I8;
    if (a == K_I4 || b == K_I4)
        return K_I4;
    if (a == K_I2 || b == K_I2)
        return K_I2;
    return K_UI1;
}

static HRESULT cy_op(ARITH op, CY a, CY b, CY *out)
{
    return op == OP_ADD ? VarCyAdd(a, b, out) : op == OP_SUB ? VarCySub(a, b, out) : VarCyMul(a, b, out);
}

static HRESULT int_op(ARITH op, LONGLONG a, LONGLONG b, LONGLONG *out)
{
    if (op == OP_MUL && ((a == (-9223372036854775807ll - 1) && b == -1) || (b == (-9223372036854775807ll - 1) && a == -1))) {
        *out = -9223372036854775807ll - 1;
        return S_OK;
    }
    BOOL over = op == OP_ADD ? __builtin_add_overflow(a, b, out) : op == OP_SUB ? __builtin_sub_overflow(a, b, out)
                                                                                 : __builtin_mul_overflow(a, b, out);
    return over ? DISP_E_OVERFLOW : S_OK;
}

static double real_op(ARITH op, double a, double b)
{
    return op == OP_ADD ? a + b : op == OP_SUB ? a - b : a * b;
}

static BOOL float_fits(double d)
{
    return d >= -FLT_MAX_VALUE && d <= FLT_MAX_VALUE;
}

/* stores an integer result in the smallest type of the chain from kind that holds it. */
static HRESULT store_integer(KIND kind, LONGLONG value, VARIANT *out)
{
    switch (kind) {
    case K_UI1:
        if (value >= 0 && value <= 255) {
            out->vt = VT_UI1;
            out->bVal = (BYTE)value;
            return S_OK;
        }
        [[fallthrough]];
    case K_I2:
        if (value >= -32768 && value <= 32767) {
            out->vt = VT_I2;
            out->iVal = (SHORT)value;
            return S_OK;
        }
        [[fallthrough]];
    case K_I4:
        if (value >= -2147483647ll - 1 && value <= 2147483647) {
            out->vt = VT_I4;
            out->lVal = (LONG)value;
            return S_OK;
        }
        out->vt = VT_R8;
        out->dblVal = (double)value;
        return S_OK;
    default:
        out->vt = VT_I8;
        out->llVal = value;
        return S_OK;
    }
}

static HRESULT arith_core(ARITH op, const VARIANT *l, const VARIANT *r, VARIANT *out)
{
    KIND a = kind_of(l->vt), b = kind_of(r->vt);
    if (l->vt == VT_EMPTY && r->vt == VT_EMPTY)
        a = b = K_I2;
    else if (l->vt == VT_EMPTY)
        a = K_UI1;
    else if (r->vt == VT_EMPTY)
        b = K_UI1;
    KIND k = common_kind(op, a, b);
    if (k == K_CY && op == OP_MUL && (a == K_CY) != (b == K_CY)) {
        VARIANT c, i;
        HRESULT hr = convert(a == K_CY ? l : r, VT_CY, &c);
        if (SUCCEEDED(hr))
            hr = convert(a == K_CY ? r : l, VT_I8, &i);
        if (FAILED(hr))
            return hr;
        LONGLONG least = -9223372036854775807ll - 1, units = c.cyVal.int64, n = i.llVal, product = 0;
        if (units == least && n == 1 && a != K_I8 && b != K_I8) {
            out->vt = VT_CY;
            out->cyVal.int64 = units;
            return S_OK;
        }
        if (units && n) {
            if (units == least)
                return DISP_E_OVERFLOW;
            if (n == least && (a == K_I8 || b == K_I8))
                return DISP_E_OVERFLOW;
            if (__builtin_mul_overflow(units, n, &product))
                return DISP_E_OVERFLOW;
        }
        out->vt = VT_CY;
        out->cyVal.int64 = product;
        return S_OK;
    }
    VARIANT x, y;
    VARTYPE work = k == K_UI1 || k == K_I2 || k == K_I4 ? VT_I8 : k == K_DATE ? VT_R8 : vt_of_kind[k];
    HRESULT hr = convert(l, work, &x);
    if (SUCCEEDED(hr))
        hr = convert(r, work, &y);
    if (FAILED(hr))
        return hr;
    switch (k) {
    case K_UI1:
    case K_I2:
    case K_I4:
    case K_I8: {
        LONGLONG v;
        hr = int_op(op, x.llVal, y.llVal, &v);
        if (hr == DISP_E_OVERFLOW) {
            out->vt = VT_R8;
            out->dblVal = real_op(op, (double)x.llVal, (double)y.llVal);
            return S_OK;
        }
        return store_integer(k, v, out);
    }
    case K_R4: {
        double v = real_op(op, x.fltVal, y.fltVal);
        float f = (float)v;
        if (!float_fits(v) || not_finite(f)) {
            out->vt = VT_R8;
            out->dblVal = v;
        } else {
            out->vt = VT_R4;
            out->fltVal = f;
        }
        return S_OK;
    }
    case K_R8: {
        double v = real_op(op, x.dblVal, y.dblVal);
        if (not_finite(v))
            return DISP_E_OVERFLOW;
        out->vt = VT_R8;
        out->dblVal = v;
        return S_OK;
    }
    case K_DATE: {
        double v = real_op(op, x.dblVal, y.dblVal);
        if (not_finite(v))
            return DISP_E_OVERFLOW;
        out->vt = date_fits(v) ? VT_DATE : VT_R8;
        out->dblVal = v;
        return S_OK;
    }
    case K_CY: {
        CY c;
        hr = cy_op(op, x.cyVal, y.cyVal, &c);
        if (FAILED(hr))
            return hr;
        out->vt = VT_CY;
        out->cyVal = c;
        return S_OK;
    }
    default: {
        DECIMAL d;
        hr = op == OP_ADD ? VarDecAdd(&x.decVal, &y.decVal, &d)
             : op == OP_SUB ? VarDecSub(&x.decVal, &y.decVal, &d)
                            : VarDecMul(&x.decVal, &y.decVal, &d);
        if (FAILED(hr))
            return hr;
        out->decVal = d;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    }
}

/* an empty operand makes the product a plain zero. */
static HRESULT arith_values(ARITH op, const VARIANT *l, const VARIANT *r, VARIANT *out)
{
    HRESULT hr = arith_core(op, l, r, out);
    if (SUCCEEDED(hr) && op == OP_MUL && (l->vt == VT_EMPTY || r->vt == VT_EMPTY)) {
        if (out->vt == VT_R8 && out->dblVal == 0.0)
            out->dblVal = 0.0;
        else if (out->vt == VT_R4 && out->fltVal == 0.0f)
            out->fltVal = 0.0f;
        else if (out->vt == VT_DECIMAL && !out->decVal.Lo64 && !out->decVal.Hi32)
            out->decVal.sign = out->decVal.scale = 0;
    }
#ifdef __i386__
    /* an empty right operand leaves the zero of a decimal unsigned in the 32 bit library. */
    if (SUCCEEDED(hr) && op == OP_SUB && r->vt == VT_EMPTY && out->vt == VT_DECIMAL && !out->decVal.Lo64 && !out->decVal.Hi32)
        out->decVal.sign = 0;
#endif
    if (SUCCEEDED(hr) && op == OP_SUB && l->vt == VT_EMPTY && out->vt == VT_DECIMAL && !out->decVal.Lo64 && !out->decVal.Hi32 &&
        (r->vt != VT_DECIMAL || !r->decVal.sign))
        out->decVal.sign = 0x80;
    if (SUCCEEDED(hr) && op == OP_SUB && l->vt == VT_EMPTY && (out->vt == VT_R8 || out->vt == VT_DATE) && out->dblVal == 0.0)
        out->ullVal = 0x8000000000000000ull;
    else if (SUCCEEDED(hr) && op == OP_SUB && l->vt == VT_EMPTY && out->vt == VT_R4 && out->fltVal == 0.0f)
        out->ulVal = 0x80000000ul;
    return hr;
}

/* ------------------------------------------------------------------ operands that are not numbers */

typedef struct {
    ULONG mask;
    BOOL string_with_null_is_free;
    BOOL integer_division;
    USHORT flags;
} OPSPEC;

/*
 * an error, an object or a null decide the result before the numbers do. returns S_FALSE when the numbers are to be
 * worked on, with strings already turned to doubles in x and y.
 */
static HRESULT special_operands(const OPSPEC *spec, const VARIANT *l, const VARIANT *r, VARIANT *out, VARIANT *x, VARIANT *y)
{
    *x = *l;
    *y = *r;
    if (!spec->string_with_null_is_free) {
        BOOL bad = l->vt == VT_ERROR || r->vt == VT_ERROR || l->vt == VT_DISPATCH || r->vt == VT_DISPATCH;
        if (l->vt == VT_DISPATCH && r->vt != VT_NULL)
            return DISP_E_TYPEMISMATCH;
        for (int i = 0; i < 2 && bad; i++) {
            const VARIANT *side = i ? r : l;
            VARIANT t;
            if (side->vt == VT_ERROR || side->vt == VT_DISPATCH || side->vt == VT_NULL)
                continue;
            HRESULT hr = convert_f(side, spec->integer_division ? (l->vt == VT_I8 || r->vt == VT_I8 ? VT_I8 : VT_I4) : VT_R8,
                                   &t, spec->flags);
            if (FAILED(hr))
                return hr;
        }
    }
    if (l->vt == VT_ERROR || r->vt == VT_ERROR)
        return DISP_E_TYPEMISMATCH;
    BOOL null = l->vt == VT_NULL || r->vt == VT_NULL;
    if (l->vt == VT_DISPATCH || r->vt == VT_DISPATCH) {
        if (!null || spec->mask == TYPES_WIDE)
            return DISP_E_TYPEMISMATCH;
    }
    if (null) {
        const VARIANT *other = l->vt == VT_NULL ? r : l;
        if (other->vt == VT_BSTR && !spec->string_with_null_is_free) {
            VARIANT t;
            HRESULT hr = convert_f(other, spec->integer_division ? (other->vt == VT_I8 ? VT_I8 : VT_I4) : VT_R8, &t, spec->flags);
            if (FAILED(hr))
                return hr;
        } else if (spec->integer_division && other->vt != VT_NULL && other->vt != VT_DISPATCH && other->vt != VT_BSTR) {
            VARIANT t;
            HRESULT hr = convert_f(other, other->vt == VT_I8 ? VT_I8 : VT_I4, &t, spec->flags);
            if (FAILED(hr))
                return hr;
        }
        VariantInit(out);
        out->vt = VT_NULL;
        return S_OK;
    }
    return S_FALSE;
}

static HRESULT finish(HRESULT hr, VARIANT *scratch, VARIANT *result)
{
    if (SUCCEEDED(hr)) {
        VariantClear(result);
        *result = *scratch;
    } else {
        VariantClear(scratch);
    }
    return hr;
}

static HRESULT binary_arith_inner(ARITH op, LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    if (!left || !right || !result)
        return E_INVALIDARG;
    static const OPSPEC add_spec = {TYPES_ARITH, TRUE, FALSE, 0};
    static const OPSPEC other_spec = {TYPES_ARITH, FALSE, FALSE, 0};
    const OPSPEC *spec = op == OP_ADD ? &add_spec : &other_spec;
    HRESULT hr = screen(spec->mask, left, right);
    if (FAILED(hr))
        return hr;
    VARIANT out, x, y;
    VariantInit(&out);
    hr = special_operands(spec, left, right, &out, &x, &y);
    if (hr == S_OK)
        return finish(S_OK, &out, result);
    if (FAILED(hr))
        return hr;
    if (op == OP_ADD && left->vt == VT_BSTR && right->vt == VT_BSTR) {
        BSTR joined;
        hr = VarBstrCat(left->bstrVal, right->bstrVal, &joined);
        if (FAILED(hr))
            return hr;
        out.vt = VT_BSTR;
        out.bstrVal = joined;
        return finish(S_OK, &out, result);
    }
    if (op == OP_ADD && (left->vt == VT_BSTR || right->vt == VT_BSTR) && (left->vt == VT_EMPTY || right->vt == VT_EMPTY)) {
        const VARIANT *text = left->vt == VT_BSTR ? left : right;
        out.bstrVal = SysAllocStringLen(text->bstrVal, SysStringLen(text->bstrVal));
        out.vt = VT_BSTR;
        return finish(out.bstrVal ? S_OK : E_OUTOFMEMORY, &out, result);
    }
    VARIANT sx, sy;
    VariantInit(&sx);
    VariantInit(&sy);
    if (x.vt == VT_BSTR) {
        hr = convert(&x, VT_R8, &sx);
        if (FAILED(hr))
            return hr;
        x = sx;
    }
    if (y.vt == VT_BSTR) {
        hr = convert(&y, VT_R8, &sy);
        if (FAILED(hr))
            return hr;
        y = sy;
    }
    hr = arith_values(op, &x, &y, &out);
    return finish(hr, &out, result);
}

/* a result that failed is left empty. */
static HRESULT settle(HRESULT hr, const VARIANT *left, const VARIANT *right, VARIANT *result)
{
    if (FAILED(hr) && result) {
        if (result != left && result != right && !(result->vt & VT_BYREF))
            VariantClear(result);
        result->vt = VT_EMPTY;
    }
    return hr;
}

static HRESULT binary_arith(ARITH op, LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return settle(binary_arith_inner(op, left, right, result), left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarAdd(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_arith(OP_ADD, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarSub(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_arith(OP_SUB, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarMul(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_arith(OP_MUL, left, right, result);
}

/* ------------------------------------------------------------------ division */

static HRESULT divide_values(const VARIANT *l, const VARIANT *r, VARIANT *out)
{
    KIND a = kind_of(l->vt), b = kind_of(r->vt);
    BOOL decimal = a == K_DEC || b == K_DEC;
    BOOL single = !decimal && (a == K_R4 || b == K_R4) && (a == K_R4 || a == K_I2 || a == K_UI1) &&
                  (b == K_R4 || b == K_I2 || b == K_UI1);
    VARIANT x, y;
    HRESULT hr = convert(l, decimal ? VT_DECIMAL : VT_R8, &x);
    if (SUCCEEDED(hr))
        hr = convert(r, decimal ? VT_DECIMAL : VT_R8, &y);
    if (FAILED(hr))
        return hr;
    if (decimal) {
        DECIMAL d;
        hr = VarDecDiv(&x.decVal, &y.decVal, &d);
        if (FAILED(hr))
            return hr;
        out->decVal = d;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
#ifdef __i386__
    /* the 32 bit library divides in the floating point unit, where any division by zero is an overflow. */
    if (y.dblVal == 0.0)
        return DISP_E_OVERFLOW;
#endif
    if (y.dblVal == 0.0)
        return x.dblVal == 0.0 && (r->vt != VT_EMPTY || l->vt == VT_EMPTY || is_integer(kind_of(l->vt)) || l->vt == VT_CY)
                   ? DISP_E_OVERFLOW
                   : DISP_E_DIVBYZERO;
    double v = x.dblVal / y.dblVal;
    if (not_finite(v))
        return DISP_E_OVERFLOW;
    if (single && float_fits(v)) {
        out->vt = VT_R4;
        out->fltVal = (float)v;
    } else {
        out->vt = VT_R8;
        out->dblVal = v;
    }
    return S_OK;
}


static BOOL string_is_truth(const VARIANT *v)
{
    VARIANT_BOOL b;
    double d;
    return v->vt == VT_BSTR && FAILED(VarR8FromStr(v->bstrVal, LCID_DEFAULT, 0, &d)) &&
           SUCCEEDED(VarBoolFromStr(v->bstrVal, LCID_DEFAULT, VARIANT_ALPHABOOL, &b));
}

/* the integer type both sides of a division by integers are brought to. */
static VARTYPE integer_type(const VARIANT *l, const VARIANT *r)
{
    if (l->vt == VT_I8 || r->vt == VT_I8)
        return VT_I8;
    BOOL small_l = l->vt == VT_I2 || l->vt == VT_BOOL || l->vt == VT_UI1 || l->vt == VT_EMPTY || string_is_truth(l);
    BOOL small_r = r->vt == VT_I2 || r->vt == VT_BOOL || r->vt == VT_UI1 || r->vt == VT_EMPTY || string_is_truth(r);
    if (small_l && small_r)
        return l->vt == VT_UI1 && r->vt == VT_UI1 ? VT_UI1 : VT_I2;
    return VT_I4;
}

static HRESULT integer_divide(DIVISION op, const VARIANT *l, const VARIANT *r, VARIANT *out, USHORT flags)
{
    VARTYPE vt = integer_type(l, r);
    VARIANT x, y;
    HRESULT hr = convert_f(l, vt == VT_I8 ? VT_I8 : VT_I4, &x, flags);
    if (SUCCEEDED(hr))
        hr = convert_f(r, vt == VT_I8 ? VT_I8 : VT_I4, &y, flags);
    if (FAILED(hr))
        return hr;
    LONGLONG a = vt == VT_I8 ? x.llVal : x.lVal, b = vt == VT_I8 ? y.llVal : y.lVal;
    if (b == 0)
        return DISP_E_DIVBYZERO;
    LONGLONG least = vt == VT_I8 ? (-9223372036854775807ll - 1) : -2147483648ll;
    LONGLONG v;
    if (b == -1) {
        if (op == DIV_IDIV && a == least)
            return DISP_E_OVERFLOW;
        v = op == DIV_IDIV ? -a : 0;
    } else {
        v = op == DIV_IDIV ? a / b : a % b;
    }
    switch (vt) {
    case VT_UI1:
        return store_integer(K_UI1, v, out);
    case VT_I2:
        return store_integer(K_I2, v, out);
    case VT_I4:
        out->vt = VT_I4;
        out->lVal = (LONG)v;
        return S_OK;
    default:
        out->vt = VT_I8;
        out->llVal = v;
        return S_OK;
    }
}

static HRESULT power_values(const VARIANT *l, const VARIANT *r, VARIANT *out, USHORT flags)
{
    VARIANT x, y;
    HRESULT hr = convert_f(l, VT_R8, &x, flags);
    if (SUCCEEDED(hr))
        hr = convert_f(r, VT_R8, &y, flags);
    if (FAILED(hr))
        return hr;
    double v;
    hr = VarR8Pow(x.dblVal, y.dblVal, &v);
    if (FAILED(hr))
        return hr;
    out->vt = VT_R8;
    out->dblVal = v;
    return S_OK;
}


/* the value of an operand of the wide operations: a reference is read through. */
static HRESULT read_operand(const VARIANT *v, VARIANT *out, int depth)
{
    VARTYPE vt = v->vt;
    if (vt & VT_ARRAY)
        return DISP_E_TYPEMISMATCH;
    if (!(vt & VT_BYREF)) {
        if (vt == VT_RECORD || vt == VT_VARIANT || vt == VT_UNKNOWN)
            return DISP_E_TYPEMISMATCH;
        if (vt > VT_UINT)
            return DISP_E_BADVARTYPE;
        if (vt == 15)
            return DISP_E_BADVARTYPE;
        *out = *v;
        return S_OK;
    }
    VARTYPE base = vt & ~VT_BYREF;
    const void *p = v->byref;
    if (base == VT_RECORD || base == VT_UNKNOWN)
        return DISP_E_TYPEMISMATCH;
    if (base > VT_UINT || base == 15 || base == VT_EMPTY || base == VT_NULL)
        return DISP_E_BADVARTYPE;
    if (!p)
        return E_INVALIDARG;
    if (base == VT_VARIANT) {
        const VARIANT *inner = v->pvarVal;
        if ((inner->vt & VT_BYREF) && depth > 0)
            return DISP_E_TYPEMISMATCH;
        return read_operand(inner, out, depth + 1);
    }
    out->vt = base;
    switch (base) {
    case VT_I1:
    case VT_UI1: out->llVal = 0; out->bVal = *(const BYTE *)p; break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL: out->llVal = 0; out->uiVal = *(const USHORT *)p; break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
    case VT_R4: out->llVal = 0; out->ulVal = *(const ULONG *)p; break;
    case VT_DECIMAL: out->decVal = *(const DECIMAL *)p; out->vt = VT_DECIMAL; break;
    case VT_DISPATCH: out->pdispVal = *(IDispatch *const *)p; break;
    case VT_BSTR: out->bstrVal = *(const BSTR *)p; break;
    default: out->llVal = *(const LONGLONG *)p; break;
    }
    if (base == VT_I1)
        out->cVal = *(const CHAR *)p;
    return S_OK;
}

/* a reference to a small number reads as a long, as the operations convert it. */
static HRESULT read_wide(const VARIANT *v, VARIANT *out)
{
    HRESULT hr = read_operand(v, out, 0);
    if (SUCCEEDED(hr) && (v->vt & VT_BYREF) && (out->vt == VT_I2 || out->vt == VT_UI1 || out->vt == VT_BOOL)) {
        LONG value = out->vt == VT_I2 || out->vt == VT_BOOL ? out->iVal : out->bVal;
        out->llVal = 0;
        out->lVal = value;
        out->vt = VT_I4;
    }
    return hr;
}

/* whether an operand converts to the type the operation works in. */
static HRESULT check_conversion(const VARIANT *v, VARTYPE to, USHORT flags)
{
    VARIANT t;
    if (v->vt == VT_NULL || v->vt == VT_EMPTY || v->vt == VT_ERROR || v->vt == VT_DISPATCH)
        return S_OK;
    return convert_f(v, to, &t, flags);
}

static HRESULT read_operands(const VARIANT *left, const VARIANT *right, VARIANT *l, VARIANT *r, USHORT flags, VARTYPE to)
{
    HRESULT hr = read_wide(left, l);
    if (FAILED(hr))
        return hr;
    if (l->vt == VT_ERROR || l->vt == VT_DISPATCH)
        return DISP_E_TYPEMISMATCH;
    hr = check_conversion(l, to, flags);
    if (FAILED(hr))
        return hr;
    hr = read_wide(right, r);
    if (FAILED(hr))
        return hr;
    return check_conversion(r, to, flags);
}

static HRESULT binary_division_inner(DIVISION op, LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    if (!left || !right || !result)
        return E_INVALIDARG;
    static const OPSPEC div_spec = {TYPES_ARITH, FALSE, FALSE, 0};
    static const OPSPEC int_spec = {TYPES_WIDE, FALSE, TRUE, 2};
    static const OPSPEC pow_spec = {TYPES_WIDE, FALSE, FALSE, 0};
    const OPSPEC *spec = op == DIV_DIV ? &div_spec : op == DIV_POW ? &pow_spec : &int_spec;
    VARIANT lv, rv;
    HRESULT hr;
    if (op == DIV_DIV) {
        hr = screen(spec->mask, left, right);
        lv = *left;
        rv = *right;
    } else {
        hr = read_operands(left, right, &lv, &rv, spec->flags,
                           op == DIV_POW ? VT_R8 : (left->vt == VT_I8 || right->vt == VT_I8) ? VT_I8 : VT_I4);
    }
    if (FAILED(hr))
        return hr;
    VARIANT out, x, y;
    VariantInit(&out);
    hr = special_operands(spec, &lv, &rv, &out, &x, &y);
    if (hr == S_OK)
        return finish(S_OK, &out, result);
    if (FAILED(hr))
        return hr;
    if (op == DIV_DIV)
        hr = divide_values(&x, &y, &out);
    else if (op == DIV_POW)
        hr = power_values(&x, &y, &out, spec->flags);
    else
        hr = integer_divide(op, &x, &y, &out, spec->flags);
    return finish(hr, &out, result);
}

static HRESULT binary_division(DIVISION op, LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return settle(binary_division_inner(op, left, right, result), left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarDiv(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_division(DIV_DIV, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarIdiv(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_division(DIV_IDIV, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarMod(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_division(DIV_MOD, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarPow(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_division(DIV_POW, left, right, result);
}

/* ------------------------------------------------------------------ logic */

typedef enum { L_NULL, L_BOOL, L_SMALL, L_I4, L_I8 } LOGIC_KIND;

typedef struct {
    LOGIC_KIND kind;
    VARTYPE vt;
    LONGLONG value;
} LOGIC_OPERAND;

typedef enum { LG_AND, LG_OR, LG_XOR, LG_EQV, LG_IMP } LOGIC;

/* what an operand is for a logic operation, with strings read as numbers or as true and false. */
static HRESULT classify_logic(const VARIANT *v, LOGIC_OPERAND *out, BOOL wide)
{
    VARIANT t;
    HRESULT hr;
    out->vt = v->vt;
    out->value = 0;
    switch (v->vt) {
    case VT_NULL: out->kind = L_NULL; return S_OK;
    case VT_EMPTY: out->kind = L_SMALL; out->vt = VT_I2; return S_OK;
    case VT_BOOL: out->kind = L_BOOL; out->value = v->boolVal; return S_OK;
    case VT_I2: out->kind = L_SMALL; out->value = v->iVal; return S_OK;
    case VT_UI1: out->kind = L_SMALL; out->value = v->bVal; return S_OK;
    case VT_I8: out->kind = L_I8; out->value = v->llVal; return S_OK;
    case VT_BSTR:
        hr = convert_f(v, VT_R8, &t, VARIANT_ALPHABOOL);
        if (SUCCEEDED(hr)) {
            VARIANT_BOOL b;
            if (FAILED(VarR8FromStr(v->bstrVal, LCID_DEFAULT, 0, &t.dblVal)) &&
                SUCCEEDED(VarBoolFromStr(v->bstrVal, LCID_DEFAULT, VARIANT_ALPHABOOL, &b))) {
                out->kind = L_BOOL;
                out->vt = VT_BOOL;
                out->value = b;
                return S_OK;
            }
        }
        if (FAILED(hr))
            return hr;
        out->kind = wide ? L_I8 : L_I4;
        out->vt = VT_R8;
        out->value = 0;
        return S_OK;
    case VT_DISPATCH:
    case VT_ERROR: return DISP_E_TYPEMISMATCH;
    default: out->kind = wide ? L_I8 : L_I4; return S_OK;
    }
}

static HRESULT logic_value(const VARIANT *v, LOGIC_OPERAND *o)
{
    if (o->kind != L_I4 && o->kind != L_I8)
        return S_OK;
    VARIANT t;
    HRESULT hr;
    if (v->vt == VT_BSTR) {
        VARIANT d;
        hr = convert_f(v, VT_R8, &d, VARIANT_ALPHABOOL);
        if (SUCCEEDED(hr))
            hr = convert(&d, o->kind == L_I8 ? VT_I8 : VT_I4, &t);
    } else {
        hr = convert(v, o->kind == L_I8 ? VT_I8 : VT_I4, &t);
    }
    if (FAILED(hr))
        return hr;
    o->value = o->kind == L_I8 ? t.llVal : t.lVal;
    return S_OK;
}

static LONGLONG logic_bits(LOGIC op, LONGLONG a, LONGLONG b)
{
    switch (op) {
    case LG_AND: return a & b;
    case LG_OR: return a | b;
    case LG_XOR: return a ^ b;
    case LG_EQV: return ~(a ^ b);
    default: return ~a | b;
    }
}

static HRESULT store_logic(VARTYPE vt, LONGLONG value, VARIANT *out)
{
    out->llVal = 0;
    out->vt = vt;
    switch (vt) {
    case VT_BOOL: out->iVal = (SHORT)value; break;
    case VT_UI1: out->bVal = (BYTE)value; break;
    case VT_I2: out->iVal = (SHORT)value; break;
    case VT_I4: out->lVal = (LONG)value; break;
    default: out->llVal = value; break;
    }
    return S_OK;
}

static HRESULT logic_values(LOGIC op, const VARIANT *raw_left, const VARIANT *raw_right, VARIANT *out)
{
    LOGIC_OPERAND a, b;
    VARIANT lv, rv;
    const VARIANT *left = &lv, *right = &rv;
    BOOL wide = raw_left->vt == VT_I8 || raw_right->vt == VT_I8;
    HRESULT hr = read_wide(raw_left, &lv);
    if (SUCCEEDED(hr))
        hr = classify_logic(left, &a, wide);
    if (SUCCEEDED(hr))
        hr = logic_value(left, &a);
    if (SUCCEEDED(hr))
        hr = read_wide(raw_right, &rv);
    if (SUCCEEDED(hr))
        hr = classify_logic(right, &b, wide);
    if (SUCCEEDED(hr))
        hr = logic_value(right, &b);
    if (FAILED(hr))
        return hr;
    if (op == LG_IMP && (a.kind == L_I8 || b.kind == L_I8)) {
        if (a.kind == L_I4)
            a.kind = L_I8;
        if (b.kind == L_I4)
            b.kind = L_I8;
    }
    if (a.kind == L_NULL || b.kind == L_NULL) {
        if (a.kind == L_NULL && b.kind == L_NULL) {
            out->vt = VT_NULL;
            return S_OK;
        }
        const LOGIC_OPERAND *x = a.kind == L_NULL ? &b : &a;
        BOOL null_left = a.kind == L_NULL;
        VARTYPE vt = x->kind == L_BOOL ? VT_BOOL : x->kind == L_I4 ? VT_I4 : x->kind == L_I8 ? VT_I8 : x->vt == VT_UI1 ? VT_UI1 : VT_I2;
        LONGLONG v = x->value;
        BOOL decided = FALSE;
        switch (op) {
        case LG_AND: decided = v == 0; break;
        case LG_OR: decided = v != 0; break;
        case LG_IMP:
            if (null_left) {
                decided = v != 0;
            } else {
                v = ~v;
                decided = v != 0;
                if (x->kind == L_BOOL)
                    v = v ? -1 : 0;
                else if (vt == VT_UI1)
                    v &= 0xFF;
            }
            break;
        default: break;
        }
        if (!decided) {
            out->vt = VT_NULL;
            return S_OK;
        }
        return store_logic(vt, v, out);
    }
    VARTYPE vt;
    if (a.kind == L_I8 || b.kind == L_I8)
        vt = VT_I8;
    else if (a.kind == L_I4 || b.kind == L_I4)
        vt = VT_I4;
    else if (a.kind == L_BOOL && b.kind == L_BOOL)
        vt = VT_BOOL;
    else if (a.vt == VT_UI1 && b.vt == VT_UI1)
        vt = VT_UI1;
    else
        vt = VT_I2;
    return store_logic(vt, logic_bits(op, a.value, b.value), out);
}

static HRESULT binary_logic(LOGIC op, LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    if (!left || !right || !result)
        return E_INVALIDARG;
    VARIANT out;
    VariantInit(&out);
    HRESULT hr = logic_values(op, left, right, &out);
    hr = finish(hr, &out, result);
    return settle(hr, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarAnd(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_logic(LG_AND, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarOr(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_logic(LG_OR, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarXor(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_logic(LG_XOR, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarEqv(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    return binary_logic(LG_EQV, left, right, result);
}

WINOLEAUTAPI HRESULT WINAPI VarImp(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    if (!left || !right || !result)
        return E_INVALIDARG;
    VARIANT negated, wider;
    VariantInit(&negated);
    const VARIANT *source = left;
    if (left->vt == VT_UI1 && right->vt != VT_UI1 && right->vt != VT_NULL) {
        wider.llVal = 0;
        wider.iVal = left->bVal;
        wider.vt = VT_I2;
        source = &wider;
    }
    HRESULT hr = VarNot((LPVARIANT)source, &negated);
    if (SUCCEEDED(hr)) {
        hr = VarOr(&negated, right, result);
        VariantClear(&negated);
    } else {
        hr = settle(hr, left, right, result);
    }
    return hr;
}

/* ------------------------------------------------------------------ one operand */

typedef enum { U_NEG, U_ABS, U_FIX, U_INT } UNARY_OP;

static double truncate_real(double d, BOOL floor_it)
{
    double whole;
    double fraction = modf(d, &whole);
    return floor_it && fraction < 0.0 ? whole - 1.0 : whole;
}

static HRESULT unary_values(UNARY_OP op, const VARIANT *in, VARIANT *out)
{
    VARIANT v = *in, t;
    HRESULT hr;
    VariantInit(&t);
    if (v.vt == VT_BSTR) {
        hr = convert(&v, VT_R8, &t);
        if (FAILED(hr))
            return hr;
        v = t;
    }
    if (v.vt == VT_EMPTY || v.vt == VT_BOOL) {
        v.llVal = 0;
        v.iVal = in->vt == VT_BOOL ? in->boolVal : 0;
        v.vt = VT_I2;
    }
    switch (v.vt) {
    case VT_UI1:
        if (op == U_NEG && v.bVal)
            return store_integer(K_I2, -(LONGLONG)v.bVal, out);
        out->vt = VT_UI1;
        out->bVal = v.bVal;
        return S_OK;
    case VT_I2:
    case VT_I4:
    case VT_I8: {
        LONGLONG x = v.vt == VT_I2 ? v.iVal : v.vt == VT_I4 ? v.lVal : v.llVal;
        KIND k = v.vt == VT_I2 ? K_I2 : v.vt == VT_I4 ? K_I4 : K_I8;
        LONGLONG least = -9223372036854775807ll - 1;
        if (op == U_FIX || op == U_INT || (op == U_ABS && x >= 0))
            return store_integer(k, x, out);
        if (x == least) {
            out->vt = VT_R8;
            out->dblVal = -(double)x;
            return S_OK;
        }
        return store_integer(k, op == U_NEG ? -x : (x < 0 ? -x : x), out);
    }
    case VT_R4:
    case VT_R8:
    case VT_DATE: {
        double d = v.vt == VT_R4 ? v.fltVal : v.dblVal;
        d = op == U_NEG ? -d : op == U_ABS ? (d < 0.0 ? -d : d) : truncate_real(d, op == U_INT);
        if (v.vt == VT_R4) {
            out->vt = VT_R4;
            out->fltVal = (float)d;
        } else {
            out->vt = v.vt;
            out->dblVal = d;
        }
        return S_OK;
    }
    case VT_CY: {
        CY c;
        hr = op == U_NEG ? VarCyNeg(v.cyVal, &c) : op == U_ABS ? VarCyAbs(v.cyVal, &c) : op == U_FIX ? VarCyFix(v.cyVal, &c)
                                                                                                       : VarCyInt(v.cyVal, &c);
        if (FAILED(hr))
            return hr;
        out->vt = VT_CY;
        out->cyVal = c;
        return S_OK;
    }
    case VT_DECIMAL: {
        DECIMAL d;
        hr = op == U_NEG ? VarDecNeg(&v.decVal, &d) : op == U_ABS ? VarDecAbs(&v.decVal, &d) : op == U_FIX ? VarDecFix(&v.decVal, &d)
                                                                                                          : VarDecInt(&v.decVal, &d);
        if (FAILED(hr))
            return hr;
        out->decVal = d;
        out->vt = VT_DECIMAL;
        return S_OK;
    }
    default: return DISP_E_TYPEMISMATCH;
    }
}

static HRESULT unary_inner(UNARY_OP op, LPVARIANT in, LPVARIANT result)
{
    VARTYPE vt = in->vt;
    if (is_array_or_record(vt) || vt == VT_DISPATCH || vt == VT_ERROR || vt == VT_UNKNOWN)
        return DISP_E_TYPEMISMATCH;
    if (!supported(TYPES_ARITH, vt) || vt > VT_UINT)
        return DISP_E_BADVARTYPE;
    VARIANT out;
    VariantInit(&out);
    HRESULT hr;
    if (vt == VT_NULL) {
        out.vt = VT_NULL;
        hr = S_OK;
    } else {
        hr = unary_values(op, in, &out);
    }
    return finish(hr, &out, result);
}

static HRESULT unary(UNARY_OP op, LPVARIANT in, LPVARIANT result)
{
    if (!in || !result)
        return E_INVALIDARG;
    if (in->vt == VT_ERROR || in->vt == VT_UNKNOWN)
        return DISP_E_TYPEMISMATCH;
    return settle(unary_inner(op, in, result), in, in, result);
}

WINOLEAUTAPI HRESULT WINAPI VarNeg(LPVARIANT in, LPVARIANT result)
{
    return unary(U_NEG, in, result);
}

WINOLEAUTAPI HRESULT WINAPI VarAbs(LPVARIANT in, LPVARIANT result)
{
    return unary(U_ABS, in, result);
}

WINOLEAUTAPI HRESULT WINAPI VarFix(LPVARIANT in, LPVARIANT result)
{
    return unary(U_FIX, in, result);
}

WINOLEAUTAPI HRESULT WINAPI VarInt(LPVARIANT in, LPVARIANT result)
{
    return unary(U_INT, in, result);
}

WINOLEAUTAPI HRESULT WINAPI VarNot(LPVARIANT in, LPVARIANT result)
{
    if (!in || !result)
        return E_INVALIDARG;
    VARIANT v, out;
    VariantInit(&out);
    HRESULT hr = read_wide(in, &v);
    LOGIC_OPERAND o;
    if (SUCCEEDED(hr))
        hr = classify_logic(&v, &o, FALSE);
    if (SUCCEEDED(hr))
        hr = logic_value(&v, &o);
    if (SUCCEEDED(hr)) {
        if (o.kind == L_NULL) {
            out.vt = VT_NULL;
        } else {
            VARTYPE vt = o.kind == L_BOOL ? VT_BOOL : o.kind == L_I4 ? VT_I4 : o.kind == L_I8 ? VT_I8 : o.vt == VT_UI1 ? VT_UI1 : VT_I2;
            store_logic(vt, ~o.value, &out);
        }
    }
    return settle(finish(hr, &out, result), in, in, result);
}

/* ------------------------------------------------------------------ rounding */

WINOLEAUTAPI HRESULT WINAPI VarRound(LPVARIANT in, int decimals, LPVARIANT result)
{
    if (!in || !result)
        return E_INVALIDARG;
    VARIANT out;
    VariantInit(&out);
    HRESULT hr = S_OK;
    VARTYPE vt = in->vt;
    if (decimals < 0) {
        hr = E_INVALIDARG;
    } else if (vt == VT_ERROR || vt == VT_UNKNOWN) {
        return DISP_E_TYPEMISMATCH;
    } else if (is_array_or_record(vt) || vt == VT_DISPATCH) {
        hr = DISP_E_TYPEMISMATCH;
    } else if (!supported(TYPES_ARITH, vt) && vt != VT_UI8) {
        hr = DISP_E_BADVARTYPE;
    } else {
        VARIANT v = *in, t;
        if (vt == VT_BSTR) {
            hr = convert(&v, VT_R8, &t);
            v = t;
        } else if (vt == VT_EMPTY || vt == VT_BOOL) {
            v.llVal = 0;
            v.iVal = vt == VT_BOOL ? in->boolVal : 0;
            v.vt = VT_I2;
        }
        if (SUCCEEDED(hr)) {
            switch (v.vt) {
            case VT_NULL:
            case VT_UI1:
            case VT_I2:
            case VT_I4:
            case VT_I8:
            case VT_UI8: out = v; break;
            case VT_R4:
            case VT_R8:
            case VT_DATE: {
                double d;
                hr = VarR8Round(v.vt == VT_R4 ? v.fltVal : v.dblVal, decimals, &d);
                if (SUCCEEDED(hr)) {
                    out.vt = v.vt;
                    if (v.vt == VT_R4)
                        out.fltVal = (float)d;
                    else
                        out.dblVal = d;
                }
                break;
            }
            case VT_CY: {
                CY c;
                hr = VarCyRound(v.cyVal, decimals, &c);
                if (SUCCEEDED(hr)) {
                    out.vt = VT_CY;
                    out.cyVal = c;
                }
                break;
            }
            default: {
                DECIMAL d;
                hr = VarDecRound(&v.decVal, decimals, &d);
                if (SUCCEEDED(hr)) {
                    out.decVal = d;
                    out.vt = VT_DECIMAL;
                }
                break;
            }
            }
        }
    }
    return settle(finish(hr, &out, result), in, in, result);
}

/* ------------------------------------------------------------------ strings */

static HRESULT text_of(const VARIANT *v, BSTR *text)
{
    if (v->vt == VT_NULL || v->vt == VT_EMPTY) {
        *text = SysAllocStringLen(nullptr, 0);
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    VARIANT t;
    VariantInit(&t);
    VARIANT copy = *v;
    HRESULT hr = VariantChangeTypeEx(&t, &copy, LCID_DEFAULT, VARIANT_ALPHABOOL, VT_BSTR);
    if (FAILED(hr))
        return hr;
    *text = t.bstrVal;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCat(LPVARIANT left, LPVARIANT right, LPVARIANT result)
{
    if (!left || !right || !result)
        return E_INVALIDARG;
    VARIANT lv, rv, out;
    VariantInit(&out);
    HRESULT hr = read_wide(left, &lv);
    if (SUCCEEDED(hr) && (lv.vt == VT_DISPATCH || lv.vt == VT_ERROR))
        hr = DISP_E_TYPEMISMATCH;
    if (SUCCEEDED(hr))
        hr = read_wide(right, &rv);
    if (SUCCEEDED(hr) && (rv.vt == VT_DISPATCH || rv.vt == VT_ERROR))
        hr = DISP_E_TYPEMISMATCH;
    if (SUCCEEDED(hr)) {
        if (lv.vt == VT_NULL && rv.vt == VT_NULL) {
            out.vt = VT_NULL;
        } else {
            BSTR a = nullptr, b = nullptr, joined = nullptr;
            hr = text_of(&lv, &a);
            if (SUCCEEDED(hr))
                hr = text_of(&rv, &b);
            if (SUCCEEDED(hr))
                hr = VarBstrCat(a, b, &joined);
            SysFreeString(a);
            SysFreeString(b);
            if (SUCCEEDED(hr)) {
                out.vt = VT_BSTR;
                out.bstrVal = joined;
            }
        }
    }
    return settle(finish(hr, &out, result), left, right, result);
}
