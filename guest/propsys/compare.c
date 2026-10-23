/* ordering property values: PropVariantCompareEx and VariantCompare. */
#include "propsys_private.h"

static BOOL empty_value(const PROPVARIANT *value)
{
    return value->vt == VT_EMPTY || value->vt == VT_NULL || !PsElementCount(value);
}

static int sign(LONGLONG v)
{
    return v < 0 ? -1 : v > 0;
}

/* compares down to the PVCU_* unit, finer fields are ignored. */
static int compare_filetime(const FILETIME *a, const FILETIME *b, PROPVAR_COMPARE_UNIT unit)
{
    if (unit == PVCU_DEFAULT)
        return CompareFileTime(a, b);
    SYSTEMTIME x, y;
    if (!FileTimeToSystemTime(a, &x) || !FileTimeToSystemTime(b, &y))
        return CompareFileTime(a, b);
    WORD xs[] = {x.wYear, x.wMonth, x.wDay, x.wHour, x.wMinute, x.wSecond};
    WORD ys[] = {y.wYear, y.wMonth, y.wDay, y.wHour, y.wMinute, y.wSecond};
    int fields = 7 - (int)unit;
    for (int i = 0; i < fields && i < 6; i++)
        if (xs[i] != ys[i])
            return xs[i] < ys[i] ? -1 : 1;
    return 0;
}

static int compare_strings(LPCWSTR a, LPCWSTR b, PROPVAR_COMPARE_FLAGS flags)
{
    if (!a)
        a = L"";
    if (!b)
        b = L"";
    if (flags & PVCF_USESTRCMP)
        return PsStrCmp(a, b, FALSE);
    if (flags & PVCF_USESTRCMPC)
        return sign(wcscmp(a, b));
    if (flags & PVCF_USESTRCMPI)
        return PsStrCmp(a, b, TRUE);
    if (flags & PVCF_USESTRCMPIC)
        return sign(_wcsicmp(a, b));
    if (flags & PVCF_DIGITSASNUMBERS_CASESENSITIVE) {
        int result = CompareStringEx(nullptr, 8, a, -1, b, -1, nullptr, nullptr, 0);
        return result ? result - CSTR_EQUAL : 0;
    }
    return PsStrCmpLogical(a, b);
}

INT WINAPI PropVariantCompareEx(const PROPVARIANT *a, const PROPVARIANT *b, PROPVAR_COMPARE_UNIT unit,
                                PROPVAR_COMPARE_FLAGS flags)
{
    PROPVARIANT nothing = {0};
    if (!PsValidValue(a))
        a = &nothing;
    if (!PsValidValue(b))
        b = &nothing;
    BOOL ea = empty_value(a), eb = empty_value(b);
    if (ea || eb) {
        if (ea && eb)
            return 0;
        int empty_first = (flags & PVCF_TREATEMPTYASGREATERTHAN) ? 1 : -1;
        return ea ? empty_first : -empty_first;
    }

    PROPVARIANT converted = {0};
    BOOL strings_a = a->vt == VT_BSTR || a->vt == VT_LPWSTR, strings_b = b->vt == VT_BSTR || b->vt == VT_LPWSTR;
    if (a->vt != b->vt && !(strings_a && strings_b)) {
        if ((a->vt == VT_FILETIME || a->vt == VT_DATE) && (b->vt == VT_FILETIME || b->vt == VT_DATE)) {
            const PROPVARIANT *date = a->vt == VT_DATE ? a : b;
            SYSTEMTIME st;
            converted.vt = VT_FILETIME;
            if (VariantTimeToSystemTime(date->date, &st) && SystemTimeToFileTime(&st, &converted.filetime)) {
                if (a->vt == VT_DATE)
                    a = &converted;
                else
                    b = &converted;
            }
        } else if (SUCCEEDED(PropVariantChangeType(&converted, b, PVCHF_DEFAULT, a->vt))) {
            b = &converted;
        }
    }
    int result;
    if (a->vt != b->vt && !((a->vt == VT_BSTR || a->vt == VT_LPWSTR) && (b->vt == VT_BSTR || b->vt == VT_LPWSTR))) {
        result = (int)a->vt - (int)b->vt;
        PropVariantClear(&converted);
        return result;
    }

    if (a->vt & (VT_VECTOR | VT_ARRAY)) {
        ULONG ca = PsElementCount(a), cb = PsElementCount(b);
        result = 0;
        for (ULONG i = 0; i < ca && i < cb && !result; i++) {
            PROPVARIANT x, y;
            if (SUCCEEDED(InitPropVariantFromPropVariantVectorElem(a, i, &x))) {
                if (SUCCEEDED(InitPropVariantFromPropVariantVectorElem(b, i, &y))) {
                    result = PropVariantCompareEx(&x, &y, unit, flags);
                    PropVariantClear(&y);
                }
                PropVariantClear(&x);
            }
        }
        if (!result && ca != cb)
            result = ca < cb ? -1 : 1;
        PropVariantClear(&converted);
        return result;
    }

    switch (a->vt) {
    case VT_BSTR: case VT_LPWSTR:
        result = compare_strings(a->pwszVal, b->pwszVal, flags);
        break;
    case VT_LPSTR: {
        LPWSTR x = nullptr, y = nullptr;
        result = 0;
        if (SUCCEEDED(PsAnsiToWideAlloc(a->pszVal, &x)) && SUCCEEDED(PsAnsiToWideAlloc(b->pszVal, &y)))
            result = compare_strings(x, y, flags);
        CoTaskMemFree(x);
        CoTaskMemFree(y);
        break;
    }
    case VT_FILETIME:
        result = compare_filetime(&a->filetime, &b->filetime, unit);
        break;
    case VT_BLOB: case VT_BLOBOBJECT: {
        ULONG n = a->blob.cbSize < b->blob.cbSize ? a->blob.cbSize : b->blob.cbSize;
        result = sign(memcmp(a->blob.pBlobData, b->blob.pBlobData, n));
        if (!result && a->blob.cbSize != b->blob.cbSize)
            result = a->blob.cbSize < b->blob.cbSize ? -1 : 1;
        break;
    }
    case VT_CLSID:
        result = sign(memcmp(a->puuid, b->puuid, sizeof(GUID)));
        break;
    case VT_BYREF | VT_VARIANT:
        result = PropVariantCompareEx(a->pvarVal, b->pvarVal, unit, flags);
        break;
    case VT_BYREF | VT_BSTR:
        result = compare_strings(*(BSTR *)a->byref, *(BSTR *)b->byref, flags);
        break;
    default:
        result = VariantCompare((const VARIANT *)a, (const VARIANT *)b);
        break;
    }
    PropVariantClear(&converted);
    return result;
}

typedef enum { NUMBER_NONE, NUMBER_SIGNED, NUMBER_UNSIGNED, NUMBER_REAL } NUMBER_KIND;

static NUMBER_KIND number(const VARIANT *v, LONGLONG *s, ULONGLONG *u, double *r)
{
    switch (v->vt) {
    case VT_I1: *s = v->cVal; return NUMBER_SIGNED;
    case VT_I2: case VT_BOOL: *s = v->iVal; return NUMBER_SIGNED;
    case VT_I4: case VT_INT: case VT_ERROR: *s = v->lVal; return NUMBER_SIGNED;
    case VT_I8: *s = v->llVal; return NUMBER_SIGNED;
    case VT_UI1: *u = v->bVal; return NUMBER_UNSIGNED;
    case VT_UI2: *u = v->uiVal; return NUMBER_UNSIGNED;
    case VT_UI4: case VT_UINT: *u = v->ulVal; return NUMBER_UNSIGNED;
    case VT_UI8: *u = v->ullVal; return NUMBER_UNSIGNED;
    case VT_R4: *r = v->fltVal; return NUMBER_REAL;
    case VT_R8: case VT_DATE: *r = v->dblVal; return NUMBER_REAL;
    case VT_CY: *r = (double)v->cyVal.int64 / 10000.0; return NUMBER_REAL;
    }
    return NUMBER_NONE;
}

static double as_real(NUMBER_KIND kind, LONGLONG s, ULONGLONG u, double r)
{
    if (kind == NUMBER_SIGNED)
        return (double)s;
    if (kind == NUMBER_UNSIGNED)
        return u >> 63 ? (double)(LONGLONG)(u >> 1) * 2.0 : (double)(LONGLONG)u;
    return r;
}

INT WINAPI VariantCompare(const VARIANT *a, const VARIANT *b)
{
    BOOL ea = a->vt < VT_I2, eb = b->vt < VT_I2;
    if (ea || eb)
        return ea == eb ? 0 : ea ? -1 : 1;
    if ((a->vt & VT_ARRAY) && (b->vt & VT_ARRAY)) {
        ULONG ca = PsElementCount((const PROPVARIANT *)a), cb = PsElementCount((const PROPVARIANT *)b);
        int result = 0;
        for (ULONG i = 0; i < ca && i < cb && !result; i++) {
            VARIANT x, y;
            if (SUCCEEDED(InitVariantFromVariantArrayElem(a, i, &x))) {
                if (SUCCEEDED(InitVariantFromVariantArrayElem(b, i, &y))) {
                    result = VariantCompare(&x, &y);
                    VariantClear(&y);
                }
                VariantClear(&x);
            }
        }
        if (!result && ca != cb)
            result = ca < cb ? -1 : 1;
        return result;
    }
    if (a->vt == VT_BSTR && b->vt == VT_BSTR)
        return PsStrCmpLogical(a->bstrVal ? a->bstrVal : L"", b->bstrVal ? b->bstrVal : L"");
    LONGLONG sa = 0, sb = 0;
    ULONGLONG ua = 0, ub = 0;
    double ra = 0, rb = 0;
    NUMBER_KIND ka = number(a, &sa, &ua, &ra), kb = number(b, &sb, &ub, &rb);
    /* what VarCmp cannot order counts as equal. */
    if (ka == NUMBER_NONE || kb == NUMBER_NONE)
        return 0;
    if (ka == NUMBER_REAL || kb == NUMBER_REAL) {
        double x = as_real(ka, sa, ua, ra), y = as_real(kb, sb, ub, rb);
        return x < y ? -1 : x > y;
    }
    if (ka == NUMBER_SIGNED && kb == NUMBER_SIGNED)
        return sa < sb ? -1 : sa > sb;
    if (ka == NUMBER_UNSIGNED && kb == NUMBER_UNSIGNED)
        return ua < ub ? -1 : ua > ub;
    if (ka == NUMBER_SIGNED)
        return sa < 0 || (ULONGLONG)sa < ub ? -1 : (ULONGLONG)sa > ub;
    return sb < 0 || ua > (ULONGLONG)sb ? 1 : ua < (ULONGLONG)sb ? -1 : 0;
}
