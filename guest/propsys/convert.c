/*
 * PropVariantChangeType: every conversion goes through a primary type: signed and
 * unsigned integers, doubles, booleans, wide strings, file times and clsids.
 */
#include "propsys_private.h"

#define E_OVERFLOW_WIN32 ((HRESULT)0x80070216L)

static BOOL integer_type(VARTYPE vt)
{
    switch (vt) {
    case VT_I1: case VT_UI1: case VT_I2: case VT_UI2: case VT_I4: case VT_UI4: case VT_I8: case VT_UI8:
        return TRUE;
    }
    return FALSE;
}

static BOOL signed_type(VARTYPE vt)
{
    return vt == VT_I1 || vt == VT_I2 || vt == VT_I4 || vt == VT_I8 || vt == VT_BOOL;
}

static void store_integer(VARTYPE vt, void *out, ULONGLONG bits)
{
    switch (vt) {
    case VT_I1: case VT_UI1:
        *(BYTE *)out = (BYTE)bits;
        break;
    case VT_I2: case VT_UI2: case VT_BOOL:
        *(USHORT *)out = (USHORT)bits;
        break;
    case VT_I4: case VT_UI4:
        *(ULONG *)out = (ULONG)bits;
        break;
    default:
        *(ULONGLONG *)out = bits;
        break;
    }
}

static BOOL signed_fits(VARTYPE vt, LONGLONG v)
{
    switch (vt) {
    case VT_I1: return v >= -128 && v <= 127;
    case VT_UI1: return v >= 0 && v <= 0xFF;
    case VT_I2: return v >= -32768 && v <= 32767;
    case VT_UI2: return v >= 0 && v <= 0xFFFF;
    case VT_I4: return v >= -2147483647LL - 1 && v <= 2147483647LL;
    case VT_UI4: return v >= 0 && v <= 0xFFFFFFFFLL;
    case VT_I8: return TRUE;
    case VT_UI8: return v >= 0;
    }
    return FALSE;
}

static BOOL unsigned_fits(VARTYPE vt, ULONGLONG v)
{
    switch (vt) {
    case VT_I1: return v <= 127;
    case VT_UI1: return v <= 0xFF;
    case VT_I2: return v <= 32767;
    case VT_UI2: return v <= 0xFFFF;
    case VT_I4: return v <= 2147483647ULL;
    case VT_UI4: return v <= 0xFFFFFFFFULL;
    case VT_I8: return v <= 0x7FFFFFFFFFFFFFFFULL;
    case VT_UI8: return TRUE;
    }
    return FALSE;
}

/* a narrowing that does not fit stores all ones and reports the overflow. */
static HRESULT integer_to_integer(BOOL is_signed, ULONGLONG bits, VARTYPE to, void *out)
{
    BOOL fits = is_signed ? signed_fits(to, (LONGLONG)bits) : unsigned_fits(to, bits);
    store_integer(to, out, fits ? bits : ~0ULL);
    return fits ? S_OK : E_OVERFLOW_WIN32;
}

static HRESULT integer_string(BOOL is_signed, ULONGLONG bits, LPWSTR *out)
{
    WCHAR text[24], *p = text + 23;
    BOOL negative = is_signed && (LONGLONG)bits < 0;
    ULONGLONG magnitude = negative ? 0 - bits : bits;
    *p = 0;
    do {
        *--p = (WCHAR)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (negative)
        *--p = '-';
    return PsAllocString(p, out);
}

/* banker's rounding, like the VarXxFromR8 conversions. */
static double round_even(double x)
{
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    double t = (double)(LONGLONG)x, fraction = x - t;
    if (fraction > 0.5 || (fraction == 0.5 && ((LONGLONG)t & 1)))
        t += 1;
    else if (fraction < -0.5 || (fraction == -0.5 && ((LONGLONG)t & 1)))
        t -= 1;
    return t;
}

static HRESULT double_to_integer(double value, VARTYPE to, void *out)
{
    if (value != value)
        return DISP_E_OVERFLOW;
    double r = round_even(value);
    if (to == VT_UI8) {
        if (r < 0 || r >= 18446744073709551616.0)
            return DISP_E_OVERFLOW;
        ULONGLONG u = r >= 9223372036854775808.0 ? (ULONGLONG)(LONGLONG)(r - 9223372036854775808.0) + (1ULL << 63)
                                                 : (ULONGLONG)(LONGLONG)r;
        store_integer(to, out, u);
        return S_OK;
    }
    if (r < -9223372036854775808.0 || r >= 9223372036854775808.0 || !signed_fits(to, (LONGLONG)r))
        return DISP_E_OVERFLOW;
    store_integer(to, out, (ULONGLONG)(LONGLONG)r);
    return S_OK;
}

static BOOL is_space(WCHAR c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

/* the whole string must parse, in base 0 unless hex is refused. garbage or overflow fail. */
static HRESULT parse_integer(LPCWSTR s, BOOL is_signed, PROPVAR_CHANGE_FLAGS flags, ULONGLONG *out)
{
    *out = 0;
    if (!s)
        return DISP_E_TYPEMISMATCH;
    while (is_space(*s))
        s++;
    if (!*s)
        return DISP_E_TYPEMISMATCH;
    BOOL negative = FALSE;
    if (*s == '-' || *s == '+')
        negative = *s++ == '-';
    UINT base = 10;
    if (!(flags & PVCHF_NOHEXSTRING) && *s == '0') {
        if ((s[1] == 'x' || s[1] == 'X') && s[2]) {
            base = 16;
            s += 2;
        } else {
            base = 8;
        }
    }
    ULONGLONG value = 0;
    BOOL any = FALSE, overflow = FALSE;
    for (; *s; s++) {
        UINT digit;
        if (*s >= '0' && *s <= '9')
            digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            digit = *s - 'A' + 10;
        else
            break;
        if (digit >= base)
            break;
        if (value > (~0ULL - digit) / base)
            overflow = TRUE;
        value = value * base + digit;
        any = TRUE;
    }
    if (!any || *s || overflow)
        return DISP_E_TYPEMISMATCH;
    if (is_signed) {
        if (negative ? value > (1ULL << 63) : value > 0x7FFFFFFFFFFFFFFFULL)
            return DISP_E_TYPEMISMATCH;
        *out = negative ? 0 - value : value;
    } else {
        *out = negative ? 0 - value : value;
    }
    return S_OK;
}

static HRESULT bool_from_string(LPCWSTR s, VARIANT_BOOL *out)
{
    while (is_space(*s))
        s++;
    SIZE_T length = wcslen(s);
    while (length && is_space(s[length - 1]))
        length--;
    if (length == 4 && !_wcsnicmp(s, L"true", 4)) {
        *out = VARIANT_TRUE;
        return S_OK;
    }
    if (length == 5 && !_wcsnicmp(s, L"false", 5)) {
        *out = VARIANT_FALSE;
        return S_OK;
    }
    WCHAR text[64];
    double value;
    if (length && length < 64) {
        memcpy(text, s, length * sizeof(WCHAR));
        text[length] = 0;
        if (PsParseDouble(text, &value)) {
            *out = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
    }
    return DISP_E_TYPEMISMATCH;
}

static BOOL read_number(LPCWSTR *s, UINT digits_min, UINT digits_max, USHORT *value)
{
    UINT n = 0;
    *value = 0;
    while (n < digits_max && **s >= '0' && **s <= '9') {
        *value = (USHORT)(*value * 10 + (**s - '0'));
        (*s)++;
        n++;
    }
    return n >= digits_min;
}

/* ISO 8601: a date, a time after T, fractions and a zone. */
static HRESULT iso8601_filetime(LPCWSTR s, FILETIME *out)
{
    SYSTEMTIME time = {0};
    if (!read_number(&s, 4, 4, &time.wYear) || *s++ != '-' || !read_number(&s, 2, 2, &time.wMonth) || *s++ != '-' ||
        !read_number(&s, 2, 2, &time.wDay))
        return E_INVALIDARG;
    LONGLONG bias = 0;
    if (*s == 'T' || *s == 't') {
        s++;
        if (!read_number(&s, 2, 2, &time.wHour) || *s++ != ':' || !read_number(&s, 2, 2, &time.wMinute))
            return E_INVALIDARG;
        if (*s == ':') {
            s++;
            if (!read_number(&s, 2, 2, &time.wSecond))
                return E_INVALIDARG;
            if (*s == '.') {
                s++;
                USHORT fraction;
                LPCWSTR start = s;
                if (!read_number(&s, 1, 3, &fraction))
                    return E_INVALIDARG;
                for (SIZE_T n = (SIZE_T)(s - start); n < 3; n++)
                    fraction *= 10;
                time.wMilliseconds = fraction;
                while (*s >= '0' && *s <= '9')
                    s++;
            }
        }
        if (*s == 'Z' || *s == 'z') {
            s++;
        } else if (*s == '+' || *s == '-') {
            BOOL ahead = *s++ == '+';
            USHORT hours, minutes = 0;
            if (!read_number(&s, 2, 2, &hours))
                return E_INVALIDARG;
            if (*s == ':')
                s++;
            if (*s)
                read_number(&s, 2, 2, &minutes);
            bias = (LONGLONG)(hours * 60 + minutes) * 600000000LL * (ahead ? -1 : 1);
        }
    }
    if (*s)
        return E_INVALIDARG;
    if (!SystemTimeToFileTime(&time, out))
        return HRESULT_FROM_WIN32(GetLastError());
    ULARGE_INTEGER value = {.LowPart = out->dwLowDateTime, .HighPart = out->dwHighDateTime};
    value.QuadPart += (ULONGLONG)bias;
    out->dwLowDateTime = value.LowPart;
    out->dwHighDateTime = value.HighPart;
    return S_OK;
}

static HRESULT filetime_from_string(LPCWSTR s, FILETIME *out)
{
    SYSTEMTIME time = {0};
    LPCWSTR p = s;
    USHORT *fields[] = {&time.wYear, &time.wMonth, &time.wDay, &time.wHour, &time.wMinute, &time.wSecond,
                        &time.wMilliseconds};
    static const char separators[] = "//:::.";
    BOOL matched = TRUE;
    for (int i = 0; i < 7 && matched; i++) {
        if (!read_number(&p, 1, 5, fields[i]))
            matched = FALSE;
        else if (i < 6 && *p++ != separators[i])
            matched = FALSE;
    }
    if (matched && !*p) {
        if (!SystemTimeToFileTime(&time, out))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }
    return iso8601_filetime(s, out);
}

static HRESULT filetime_string(const FILETIME *time, LPWSTR *out)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(time, &st))
        return HRESULT_FROM_WIN32(GetLastError());
    WCHAR text[40];
    wsprintfW(text, L"%04u/%02u/%02u:%02u:%02u:%02u.%03u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
              st.wSecond, st.wMilliseconds);
    return PsAllocString(text, out);
}

static HRESULT guid_string(const GUID *guid, LPWSTR *out)
{
    WCHAR text[40];
    if (!StringFromGUID2(guid, text, 40))
        return E_FAIL;
    return PsAllocString(text, out);
}

/* the caller zeroes the target. */
static HRESULT coerce_primary(VARTYPE from, const void *source, VARTYPE to, void *out, PROPVAR_CHANGE_FLAGS flags)
{
    if (from == VT_LPWSTR) {
        LPCWSTR s = *(LPCWSTR const *)source;
        if (!s)
            s = L"";
        switch (to) {
        case VT_LPWSTR:
            return PsAllocString(s, out);
        case VT_I1: case VT_I2: case VT_I4: case VT_I8: case VT_UI1: case VT_UI2: case VT_UI4: case VT_UI8: {
            BOOL is_signed = signed_type(to);
            ULONGLONG bits;
            HRESULT hr = parse_integer(s, is_signed, flags, &bits);
            if (FAILED(hr))
                return hr;
            return integer_to_integer(is_signed, bits, to, out);
        }
        case VT_R8: {
            double value;
            if (!PsParseDouble(s, &value))
                return TYPE_E_TYPEMISMATCH;
            *(double *)out = value;
            return S_OK;
        }
        case VT_BOOL:
            return bool_from_string(s, out);
        case VT_FILETIME:
            return filetime_from_string(s, out);
        case VT_CLSID:
            return PsGuidFromString(s, out) ? S_OK : E_INVALIDARG;
        }
        return DISP_E_TYPEMISMATCH;
    }
    if (from == VT_EMPTY) {
        switch (to) {
        case VT_BOOL: case VT_I2: case VT_UI2: case VT_I4: case VT_UI4: case VT_R8: case VT_I8: case VT_UI8:
        case VT_I1: case VT_UI1:
            return S_OK;
        case VT_LPWSTR:
            return PsAllocString(L"", out);
        case VT_CLSID:
            memset(out, 0, sizeof(GUID));
            return S_OK;
        }
        return DISP_E_TYPEMISMATCH;
    }
    if (integer_type(from) || from == VT_BOOL) {
        ULONGLONG bits;
        BOOL is_signed = signed_type(from);
        switch (from) {
        case VT_I1: bits = (ULONGLONG)(LONGLONG) * (const CHAR *)source; break;
        case VT_UI1: bits = *(const BYTE *)source; break;
        case VT_I2: case VT_BOOL: bits = (ULONGLONG)(LONGLONG) * (const SHORT *)source; break;
        case VT_UI2: bits = *(const USHORT *)source; break;
        case VT_I4: bits = (ULONGLONG)(LONGLONG) * (const LONG *)source; break;
        case VT_UI4: bits = *(const ULONG *)source; break;
        default: bits = *(const ULONGLONG *)source; break;
        }
        if (to == VT_BOOL) {
            *(VARIANT_BOOL *)out = bits ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (to == VT_R8) {
            *(double *)out = is_signed ? (double)(LONGLONG)bits
                                       : (bits >> 63 ? (double)(LONGLONG)(bits >> 1) * 2.0 + (double)(bits & 1)
                                                     : (double)(LONGLONG)bits);
            return S_OK;
        }
        if (to == VT_LPWSTR) {
            if (from == VT_BOOL && (flags & PVCHF_ALPHABOOL))
                return PsAllocString(bits ? L"True" : L"False", out);
            return integer_string(is_signed, bits, out);
        }
        if (!integer_type(to))
            return DISP_E_TYPEMISMATCH;
        /* true becomes all ones in any integer, without reporting an overflow. */
        if (from == VT_BOOL) {
            store_integer(to, out, bits);
            return S_OK;
        }
        return integer_to_integer(is_signed, bits, to, out);
    }
    if (from == VT_R8) {
        double value = *(const double *)source;
        if (to == VT_R8) {
            *(double *)out = value;
            return S_OK;
        }
        if (to == VT_BOOL) {
            *(VARIANT_BOOL *)out = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (to == VT_LPWSTR)
            return PsDoubleToString(value, out);
        if (integer_type(to))
            return double_to_integer(value, to, out);
        return DISP_E_TYPEMISMATCH;
    }
    if (from == VT_FILETIME) {
        if (to == VT_FILETIME) {
            *(FILETIME *)out = *(const FILETIME *)source;
            return S_OK;
        }
        if (to == VT_LPWSTR)
            return filetime_string(source, out);
        return DISP_E_TYPEMISMATCH;
    }
    if (from == VT_CLSID) {
        if (to == VT_CLSID) {
            *(GUID *)out = *(const GUID *)source;
            return S_OK;
        }
        if (to == VT_LPWSTR)
            return guid_string(source, out);
        return DISP_E_TYPEMISMATCH;
    }
    return DISP_E_TYPEMISMATCH;
}

static HRESULT date_to_filetime(DATE date, FILETIME *out)
{
    SYSTEMTIME time;
    if (!VariantTimeToSystemTime(date, &time) || !SystemTimeToFileTime(&time, out))
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT filetime_to_date(const FILETIME *time, DATE *out)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(time, &st) || !SystemTimeToVariantTime(&st, out))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT PsCoerceElement(VARTYPE from, const void *source, VARTYPE to, void *out, PROPVAR_CHANGE_FLAGS flags)
{
    union {
        double r8;
        FILETIME time;
        LPWSTR string;
        LPCWSTR borrowed;
    } normal;
    LPWSTR owned = nullptr;
    HRESULT hr = S_OK;

    if (from == VT_VARIANT) {
        PROPVARIANT converted;
        hr = PropVariantChangeType(&converted, source, flags, to);
        if (FAILED(hr))
            return hr;
        if (to == VT_CLSID) {
            *(GUID *)out = *converted.puuid;
            CoTaskMemFree(converted.puuid);
        } else {
            memcpy(out, &converted.cVal, PsElementSize(to));
        }
        return hr;
    }
    switch (from) {
    case VT_NULL:
        from = VT_EMPTY;
        break;
    case VT_R4:
        normal.r8 = *(const float *)source;
        source = &normal;
        from = VT_R8;
        break;
    case VT_DATE:
        hr = date_to_filetime(*(const DATE *)source, &normal.time);
        source = &normal;
        from = VT_FILETIME;
        break;
    case VT_INT:
        from = VT_I4;
        break;
    case VT_UINT:
        from = VT_UI4;
        break;
    case VT_LPSTR:
        hr = PsAnsiToWideAlloc(*(LPCSTR const *)source, &owned);
        normal.borrowed = owned;
        source = &normal;
        from = VT_LPWSTR;
        break;
    case VT_BSTR:
        normal.borrowed = *(BSTR const *)source ? *(BSTR const *)source : L"";
        source = &normal;
        from = VT_LPWSTR;
        break;
    case VT_EMPTY: case VT_I2: case VT_I4: case VT_R8: case VT_BOOL: case VT_I1: case VT_UI1: case VT_UI2:
    case VT_UI4: case VT_I8: case VT_UI8: case VT_LPWSTR: case VT_FILETIME: case VT_CLSID:
        break;
    default:
        return DISP_E_TYPEMISMATCH;
    }
    if (FAILED(hr)) {
        CoTaskMemFree(owned);
        return hr;
    }

    union {
        double r8;
        FILETIME time;
        LPWSTR string;
    } result = {0};
    void *target = out;
    VARTYPE primary = to;
    switch (to) {
    case VT_R4:
        target = &result;
        primary = VT_R8;
        break;
    case VT_DATE:
        target = &result;
        primary = VT_FILETIME;
        break;
    case VT_BSTR: case VT_LPSTR:
        target = &result;
        primary = VT_LPWSTR;
        break;
    case VT_INT:
        primary = VT_I4;
        break;
    case VT_UINT:
        primary = VT_UI4;
        break;
    case VT_EMPTY: case VT_I2: case VT_I4: case VT_R8: case VT_ERROR: case VT_BOOL: case VT_I1: case VT_UI1:
    case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_LPWSTR: case VT_FILETIME: case VT_CLSID:
        break;
    default:
        CoTaskMemFree(owned);
        return DISP_E_TYPEMISMATCH;
    }
    hr = coerce_primary(from, source, primary, target, flags);
    CoTaskMemFree(owned);
    if (FAILED(hr) || target == out)
        return hr;
    switch (to) {
    case VT_R4:
        *(float *)out = (float)result.r8;
        break;
    case VT_DATE:
        hr = filetime_to_date(&result.time, out);
        break;
    case VT_BSTR:
        *(BSTR *)out = SysAllocString(result.string);
        if (!*(BSTR *)out)
            hr = E_OUTOFMEMORY;
        CoTaskMemFree(result.string);
        break;
    case VT_LPSTR:
        hr = PsWideToAnsiAlloc(result.string, out);
        CoTaskMemFree(result.string);
        break;
    }
    return hr;
}

/* like PropVariantToVariant, a vector becomes an array of the same base type. */
static HRESULT coerce_to_vector(PROPVARIANT *dest, const PROPVARIANT *source, PROPVAR_CHANGE_FLAGS flags,
                                VARTYPE type)
{
    VARTYPE base = type & VT_TYPEMASK;
    UINT size = PsElementSize(base);
    if (!size)
        return E_INVALIDARG;
    ULONG count = (source->vt & (VT_VECTOR | VT_ARRAY | VT_BYREF)) || PsValidType(source->vt) ? PsElementCount(source)
                                                                                              : 0;
    if (source->vt == VT_EMPTY || source->vt == VT_NULL)
        count = 0;
    if ((ULONGLONG)count * size > 0xFFFFFFFFULL)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    BYTE *items = PsAllocZero((SIZE_T)count * size);
    if (!items)
        return E_OUTOFMEMORY;
    dest->vt = type;
    dest->cac.cElems = 0;
    dest->cac.pElems = (CHAR *)items;
    for (ULONG i = 0; i < count; i++) {
        const void *element;
        HRESULT hr = PsElementPointer(source, i, &element);
        if (SUCCEEDED(hr))
            hr = PsCoerceElement(source->vt & VT_TYPEMASK, element, base, items + (SIZE_T)i * size, flags);
        if (FAILED(hr)) {
            PropVariantClear(dest);
            return hr;
        }
        dest->cac.cElems++;
    }
    return S_OK;
}

static BOOL pointer_type(VARTYPE base)
{
    switch (base) {
    case VT_UNKNOWN: case VT_DISPATCH: case VT_LPSTR: case VT_LPWSTR: case VT_STREAM: case VT_STORAGE:
    case VT_STREAMED_OBJECT: case VT_STORED_OBJECT: case VT_VERSIONED_STREAM:
        return TRUE;
    }
    return FALSE;
}

HRESULT WINAPI PropVariantChangeType(PROPVARIANT *dest, const PROPVARIANT *source, PROPVAR_CHANGE_FLAGS flags,
                                     VARTYPE type)
{
    PROPVARIANT empty_string;
    memset(dest, 0, sizeof(*dest));
    if (source->vt == VT_LPWSTR && !source->pwszVal) {
        memset(&empty_string, 0, sizeof(empty_string));
        empty_string.vt = VT_LPWSTR;
        empty_string.pwszVal = (LPWSTR)L"";
        source = &empty_string;
    }
    if ((type & VT_BYREF) || (type & VT_RESERVED) || !PsValidType(type) || !PsValidValue(source))
        return E_INVALIDARG;
    if (type == VT_EMPTY || type == VT_NULL) {
        dest->vt = type;
        return S_OK;
    }
    PROPVARIANT nothing = {0};
    if ((source->vt & (VT_VECTOR | VT_ARRAY)) && !PsElementCount(source))
        source = &nothing;
    if (source->vt == type)
        return PropVariantCopy(dest, source);

    if (type & VT_VECTOR)
        return coerce_to_vector(dest, source, flags, type);
    if (type & VT_ARRAY) {
        PROPVARIANT vector = {0};
        HRESULT hr = coerce_to_vector(&vector, source, flags, (type & VT_TYPEMASK) | VT_VECTOR);
        if (SUCCEEDED(hr)) {
            hr = PropVariantToVariant(&vector, (VARIANT *)dest);
            PropVariantClear(&vector);
        }
        return hr;
    }

    VARTYPE from = source->vt & VT_TYPEMASK;
    ULONG count = PsElementCount(source);
    if ((source->vt == (VT_VECTOR | VT_UI1) || source->vt == (VT_ARRAY | VT_UI1)) && count == 16) {
        from = VT_CLSID;
        count = 1;
    }
    const void *element = &source->cVal;
    HRESULT hr = S_OK;
    if (count == 0 && source->vt == VT_EMPTY)
        from = VT_EMPTY;
    else if (count != 1)
        return DISP_E_TYPEMISMATCH;
    else
        hr = PsElementPointer(source, 0, &element);
    if (FAILED(hr))
        return hr;
    if ((source->vt & VT_VECTOR) || (source->vt & VT_ARRAY) || (source->vt & VT_BYREF)) {
        if (pointer_type(from) && !*(void *const *)element)
            return E_INVALIDARG;
    } else if (pointer_type(from) && from != VT_EMPTY && !source->byref) {
        return E_INVALIDARG;
    }
    if (from == VT_VARIANT)
        return PropVariantChangeType(dest, element, flags, type);
    void *out = &dest->cVal;
    GUID *guid = nullptr;
    if (type == VT_CLSID) {
        guid = PsAllocZero(sizeof(GUID));
        if (!guid)
            return E_OUTOFMEMORY;
        dest->puuid = guid;
        out = guid;
    }
    hr = PsCoerceElement(from, element, type, out, flags);
    if (FAILED(hr)) {
        CoTaskMemFree(guid);
        memset(dest, 0, sizeof(*dest));
        return hr;
    }
    if (hr == S_OK)
        dest->vt = type;
    return hr;
}
