/* variant conversions and return value helpers for the dom methods. */
#include <xmldom/xmldom.h>

const VARIANT *xv_deref(const VARIANT *v)
{
    while (v && v->vt == (VT_VARIANT | VT_BYREF) && v->pvarVal)
        v = v->pvarVal;
    return v;
}

BOOL xv_is_missing(const VARIANT *v)
{
    v = xv_deref(v);
    if (!v)
        return TRUE;
    return v->vt == VT_EMPTY || v->vt == VT_NULL || (v->vt == VT_ERROR && v->scode == DISP_E_PARAMNOTFOUND);
}

static void put_integer(XBuf *out, INT64 value)
{
    WCHAR digits[24];
    UINT n = 0;
    UINT64 v = value < 0 ? (UINT64)(-value) : (UINT64)value;
    do {
        digits[n++] = (WCHAR)('0' + (UINT)(v % 10));
        v /= 10;
    } while (v);
    if (value < 0)
        xb_putc(out, '-');
    while (n)
        xb_putc(out, digits[--n]);
}

BOOL xv_to_string(const VARIANT *v, XBuf *out)
{
    v = xv_deref(v);
    if (!v)
        return TRUE;
    VARTYPE vt = v->vt;
    BOOL byref = (vt & VT_BYREF) != 0;
    switch (vt & ~VT_BYREF) {
    case VT_EMPTY:
    case VT_NULL:
        return TRUE;
    case VT_BSTR: {
        BSTR s = byref ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (s)
            xb_put(out, s, SysStringLen(s));
        return TRUE;
    }
    case VT_I1:
        put_integer(out, byref ? *(CHAR *)v->byref : v->cVal);
        return TRUE;
    case VT_UI1:
        put_integer(out, byref ? *(BYTE *)v->byref : v->bVal);
        return TRUE;
    case VT_I2:
        put_integer(out, byref ? *(SHORT *)v->byref : v->iVal);
        return TRUE;
    case VT_UI2:
        put_integer(out, byref ? *(USHORT *)v->byref : v->uiVal);
        return TRUE;
    case VT_I4:
    case VT_INT:
        put_integer(out, byref ? *(LONG *)v->byref : v->lVal);
        return TRUE;
    case VT_UI4:
    case VT_UINT:
        put_integer(out, byref ? *(ULONG *)v->byref : v->ulVal);
        return TRUE;
    case VT_I8:
        put_integer(out, byref ? *(LONGLONG *)v->byref : v->llVal);
        return TRUE;
    case VT_UI8:
        put_integer(out, (INT64)(byref ? *(ULONGLONG *)v->byref : v->ullVal));
        return TRUE;
    case VT_BOOL: {
        VARIANT_BOOL b = byref ? *(VARIANT_BOOL *)v->byref : v->boolVal;
        xb_puta(out, b ? "True" : "False");
        return TRUE;
    }
    case VT_R4:
        xd_format_number(byref ? *(FLOAT *)v->byref : v->fltVal, out);
        return TRUE;
    case VT_R8:
        xd_format_number(byref ? *(DOUBLE *)v->byref : v->dblVal, out);
        return TRUE;
    }
    return FALSE;
}

BOOL xv_to_long(const VARIANT *v, LONG *out)
{
    v = xv_deref(v);
    if (!v)
        return FALSE;
    BOOL byref = (v->vt & VT_BYREF) != 0;
    switch (v->vt & ~VT_BYREF) {
    case VT_I1:
        *out = byref ? *(CHAR *)v->byref : v->cVal;
        return TRUE;
    case VT_UI1:
        *out = byref ? *(BYTE *)v->byref : v->bVal;
        return TRUE;
    case VT_I2:
    case VT_BOOL:
        *out = byref ? *(SHORT *)v->byref : v->iVal;
        return TRUE;
    case VT_UI2:
        *out = byref ? *(USHORT *)v->byref : v->uiVal;
        return TRUE;
    case VT_I4:
    case VT_INT:
    case VT_UI4:
    case VT_UINT:
        *out = byref ? *(LONG *)v->byref : v->lVal;
        return TRUE;
    case VT_R4:
        *out = (LONG)(byref ? *(FLOAT *)v->byref : v->fltVal);
        return TRUE;
    case VT_R8:
        *out = (LONG)(byref ? *(DOUBLE *)v->byref : v->dblVal);
        return TRUE;
    case VT_BSTR: {
        BSTR s = byref ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (!s)
            return FALSE;
        double d = xd_parse_number(s, SysStringLen(s));
        if (xd_isnan(d))
            return FALSE;
        *out = (LONG)d;
        return TRUE;
    }
    }
    return FALSE;
}

BOOL xv_to_bool(const VARIANT *v, BOOL *out)
{
    v = xv_deref(v);
    if (!v)
        return FALSE;
    if ((v->vt & ~VT_BYREF) == VT_BSTR) {
        BSTR s = (v->vt & VT_BYREF) ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (!s)
            return FALSE;
        if (!xd_wcsicmp(s, L"true")) {
            *out = TRUE;
            return TRUE;
        }
        if (!xd_wcsicmp(s, L"false")) {
            *out = FALSE;
            return TRUE;
        }
    }
    LONG n;
    if (!xv_to_long(v, &n))
        return FALSE;
    *out = n != 0;
    return TRUE;
}

BOOL xd_node_is_ours(const void *p)
{
    if (!p)
        return FALSE;
    const void *vtbl = ((const XNode *)p)->vtbl;
    return vtbl == &xd_vtbl_element || vtbl == &xd_vtbl_attribute || vtbl == &xd_vtbl_text || vtbl == &xd_vtbl_cdata ||
           vtbl == &xd_vtbl_comment || vtbl == &xd_vtbl_pi || vtbl == &xd_vtbl_doctype ||
           vtbl == &xd_vtbl_document || vtbl == &xd_vtbl_fragment || vtbl == &xd_vtbl_entityref ||
           vtbl == &xd_vtbl_node;
}

XNode *xv_to_node(const VARIANT *v)
{
    v = xv_deref(v);
    if (!v)
        return nullptr;
    IUnknown *unk = nullptr;
    switch (v->vt) {
    case VT_DISPATCH:
        unk = (IUnknown *)v->pdispVal;
        break;
    case VT_UNKNOWN:
        unk = v->punkVal;
        break;
    case VT_DISPATCH | VT_BYREF:
        unk = v->ppunkVal ? *v->ppunkVal : nullptr;
        break;
    case VT_UNKNOWN | VT_BYREF:
        unk = v->ppunkVal ? *v->ppunkVal : nullptr;
        break;
    }
    if (!unk)
        return nullptr;
    if (xd_node_is_ours(unk)) {
        xn_addref((XNode *)unk);
        return (XNode *)unk;
    }
    void *found = nullptr;
    if (SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &IID_IXMLDOMNode, &found)) && found) {
        if (xd_node_is_ours(found))
            return (XNode *)found;
        ((IUnknown *)found)->lpVtbl->Release((IUnknown *)found);
    }
    return nullptr;
}

HRESULT xd_ret_string(BSTR *out, const WCHAR *s, UINT length)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!s)
        return S_FALSE;
    *out = SysAllocStringLen(s, length);
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT xd_ret_buf(BSTR *out, XBuf *b)
{
    HRESULT hr;
    if (b->failed)
        hr = E_OUTOFMEMORY;
    else if (out)
        hr = xd_ret_string(out, b->s ? b->s : L"", b->length);
    else
        hr = E_INVALIDARG;
    xb_free(b);
    return hr;
}

HRESULT xd_ret_node(XNode *n, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!n)
        return S_FALSE;
    xn_addref(n);
    *out = n;
    return S_OK;
}
