/*
 * the values of the language: arithmetic with the promotions of vbscript, comparison, and conversions on the
 * variant functions of oleaut32.
 */
#include "vbs.h"

BSTR bstr_of_text(PCWSTR text, SIZE_T length)
{
    return SysAllocStringLen(text, (UINT)length);
}

/* copies what a variant stands for, following the references. */
void var_deref(VARIANT *dst, const VARIANT *src)
{
    VariantInit(dst);
    while ((src->vt & VT_BYREF) && (src->vt & VT_TYPEMASK) == VT_VARIANT && src->pvarVal)
        src = src->pvarVal;
    if ((src->vt & VT_BYREF) && (src->vt & VT_TYPEMASK) == VT_VARIANT)
        return;
    VariantCopyInd(dst, (VARIANT *)src);
}

HRESULT change_type(VARIANT *dst, VARIANT *src, VARTYPE vt, LCID lcid)
{
    return VariantChangeTypeEx(dst, src, lcid, 0, vt);
}

static BOOL is_int_type(VARTYPE vt)
{
    return vt == VT_I2 || vt == VT_I4 || vt == VT_UI1 || vt == VT_BOOL || vt == VT_EMPTY || vt == VT_I1 || vt == VT_UI2;
}

static BOOL is_numeric_type(VARTYPE vt)
{
    return is_int_type(vt) || vt == VT_R4 || vt == VT_R8 || vt == VT_CY || vt == VT_DATE;
}

HRESULT var_binary(int op, VARIANT *l, VARIANT *r, VARIANT *out, LCID lcid)
{
    HRESULT hr;
    VariantInit(out);
    switch (op) {
    case OP_ADD:
    case OP_SUB:
    case OP_MUL: {
        hr = op == OP_ADD ? VarAdd(l, r, out) : op == OP_SUB ? VarSub(l, r, out) : VarMul(l, r, out);
        if (hr == DISP_E_OVERFLOW && is_int_type(l->vt) && is_int_type(r->vt)) {
            VARIANT a, b;
            VariantInit(&a);
            VariantInit(&b);
            if (SUCCEEDED(VariantChangeTypeEx(&a, l, lcid, 0, VT_I4)) && SUCCEEDED(VariantChangeTypeEx(&b, r, lcid, 0, VT_I4))) {
                VariantClear(out);
                hr = op == OP_ADD ? VarAdd(&a, &b, out) : op == OP_SUB ? VarSub(&a, &b, out) : VarMul(&a, &b, out);
                if (hr == DISP_E_OVERFLOW) {
                    VariantClear(&a);
                    VariantClear(&b);
                    VariantChangeTypeEx(&a, l, lcid, 0, VT_R8);
                    VariantChangeTypeEx(&b, r, lcid, 0, VT_R8);
                    hr = op == OP_ADD ? VarAdd(&a, &b, out) : op == OP_SUB ? VarSub(&a, &b, out) : VarMul(&a, &b, out);
                }
            }
        } else if (hr == DISP_E_OVERFLOW && is_numeric_type(l->vt) && is_numeric_type(r->vt) && l->vt != VT_CY && r->vt != VT_CY &&
                   (l->vt == VT_I4 || r->vt == VT_I4)) {
            VARIANT a, b;
            VariantInit(&a);
            VariantInit(&b);
            VariantChangeTypeEx(&a, l, lcid, 0, VT_R8);
            VariantChangeTypeEx(&b, r, lcid, 0, VT_R8);
            hr = op == OP_ADD ? VarAdd(&a, &b, out) : op == OP_SUB ? VarSub(&a, &b, out) : VarMul(&a, &b, out);
        }
        return hr;
    }
    case OP_DIV: {
        HRESULT hr = VarDiv(l, r, out);
        if (hr != DISP_E_OVERFLOW || !is_numeric_type(r->vt) || r->vt == VT_DECIMAL)
            return hr;
        /* the 32 bit oleaut32 reports every division by zero as an overflow, the engine tells them apart. */
        VARIANT x, y;
        VariantInit(&x);
        VariantInit(&y);
        if (SUCCEEDED(VariantChangeTypeEx(&y, r, lcid, 0, VT_R8)) && y.dblVal == 0.0 &&
            SUCCEEDED(VariantChangeTypeEx(&x, l, lcid, 0, VT_R8)) && x.dblVal != 0.0)
            return DISP_E_DIVBYZERO;
        return hr;
    }
    case OP_IDIV: return VarIdiv(l, r, out);
    case OP_MOD: return VarMod(l, r, out);
    case OP_POW: return VarPow(l, r, out);
    case OP_CAT: return VarCat(l, r, out);
    case OP_AND: return VarAnd(l, r, out);
    case OP_OR: return VarOr(l, r, out);
    case OP_XOR: return VarXor(l, r, out);
    case OP_EQV: return VarEqv(l, r, out);
    case OP_IMP: return VarImp(l, r, out);
    }
    return E_INVALIDARG;
}

HRESULT var_unary(int op, VARIANT *v, VARIANT *out, LCID lcid)
{
    VariantInit(out);
    switch (op) {
    case OP_NEG: {
        HRESULT hr = VarNeg(v, out);
        if (hr == DISP_E_OVERFLOW && (v->vt == VT_I2 || v->vt == VT_I4)) {
            VARIANT a;
            VariantInit(&a);
            VariantChangeTypeEx(&a, v, lcid, 0, v->vt == VT_I2 ? VT_I4 : VT_R8);
            hr = VarNeg(&a, out);
        }
        return hr;
    }
    case OP_NOT: return VarNot(v, out);
    case OP_PLUS: return VariantCopy(out, v);
    }
    return E_INVALIDARG;
}

/* compares two values: result is -1, 0 or 1, or 2 when one is null. */
HRESULT var_compare(VARIANT *l, VARIANT *r, LCID lcid, int *result)
{
    if (l->vt == VT_NULL || r->vt == VT_NULL) {
        *result = 2;
        return S_OK;
    }
    BOOL ls = l->vt == VT_BSTR, rs = r->vt == VT_BSTR;
    if (ls && rs) {
        const WCHAR *a = l->bstrVal ? l->bstrVal : L"", *b = r->bstrVal ? r->bstrVal : L"";
        UINT la = SysStringLen(l->bstrVal), lb = SysStringLen(r->bstrVal);
        UINT n = la < lb ? la : lb;
        for (UINT i = 0; i < n; i++)
            if (a[i] != b[i]) {
                *result = a[i] < b[i] ? -1 : 1;
                return S_OK;
            }
        *result = la < lb ? -1 : la > lb ? 1 : 0;
        return S_OK;
    }
    VARIANT a, b;
    VariantInit(&a);
    VariantInit(&b);
    HRESULT hr;
    if (ls && r->vt == VT_EMPTY) {
        *result = SysStringLen(l->bstrVal) ? 1 : 0;
        return S_OK;
    }
    if (rs && l->vt == VT_EMPTY) {
        *result = SysStringLen(r->bstrVal) ? -1 : 0;
        return S_OK;
    }
    VARIANT *pa = l, *pb = r;
    if (ls || rs) {
        VARIANT *s = ls ? l : r;
        VARIANT *conv = ls ? &a : &b;
        hr = VariantChangeTypeEx(conv, s, lcid, 0, VT_R8);
        if (FAILED(hr))
            return DISP_E_TYPEMISMATCH;
        if (ls)
            pa = &a;
        else
            pb = &b;
    }
    hr = VarCmp(pa, pb, lcid, 0);
    VariantClear(&a);
    VariantClear(&b);
    if (hr == VARCMP_LT)
        *result = -1;
    else if (hr == VARCMP_EQ)
        *result = 0;
    else if (hr == VARCMP_GT)
        *result = 1;
    else if (hr == VARCMP_NULL)
        *result = 2;
    else
        return hr;
    return S_OK;
}
