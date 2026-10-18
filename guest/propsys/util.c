/* the shared helpers of propsys: type rules, strings, guids and numbers. */
#include "propsys_private.h"

BOOL PsValidType(VARTYPE vt)
{
    VARTYPE base = vt & VT_TYPEMASK;
    switch (vt & ~VT_TYPEMASK) {
    case 0:
        switch (base) {
        case VT_EMPTY: case VT_NULL: case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE:
        case VT_BSTR: case VT_DISPATCH: case VT_ERROR: case VT_BOOL: case VT_UNKNOWN: case VT_DECIMAL: case VT_I1:
        case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT: case VT_LPSTR:
        case VT_LPWSTR: case VT_FILETIME: case VT_BLOB: case VT_STREAM: case VT_STORAGE: case VT_STREAMED_OBJECT:
        case VT_STORED_OBJECT: case VT_BLOBOBJECT: case VT_CF: case VT_CLSID: case VT_VERSIONED_STREAM:
            return TRUE;
        }
        return FALSE;
    case VT_VECTOR:
        switch (base) {
        case VT_I1: case VT_UI1: case VT_I2: case VT_UI2: case VT_I4: case VT_UI4: case VT_I8: case VT_UI8:
        case VT_R4: case VT_R8: case VT_BOOL: case VT_ERROR: case VT_CY: case VT_DATE: case VT_FILETIME:
        case VT_CLSID: case VT_CF: case VT_BSTR: case VT_LPSTR: case VT_LPWSTR: case VT_VARIANT:
            return TRUE;
        }
        return FALSE;
    case VT_ARRAY:
    case VT_BYREF:
        switch (base) {
        case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BSTR: case VT_DISPATCH:
        case VT_ERROR: case VT_BOOL: case VT_VARIANT: case VT_UNKNOWN: case VT_DECIMAL: case VT_I1: case VT_UI1:
        case VT_UI2: case VT_UI4: case VT_INT: case VT_UINT:
            return TRUE;
        }
        return (vt & VT_BYREF) && (base == VT_I8 || base == VT_UI8);
    case VT_BYREF | VT_ARRAY:
        return TRUE;
    }
    return FALSE;
}

BOOL PsValidValue(const PROPVARIANT *value)
{
    if (!PsValidType(value->vt))
        return FALSE;
    if ((value->vt & VT_VECTOR) && value->cac.cElems && !value->cac.pElems)
        return FALSE;
    return TRUE;
}

UINT PsElementSize(VARTYPE base)
{
    switch (base) {
    case VT_I1: case VT_UI1:
        return 1;
    case VT_I2: case VT_UI2: case VT_BOOL:
        return 2;
    case VT_I4: case VT_UI4: case VT_R4: case VT_ERROR: case VT_INT: case VT_UINT:
        return 4;
    case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: case VT_FILETIME:
        return 8;
    case VT_CLSID:
        return sizeof(CLSID);
    case VT_CF:
        return sizeof(CLIPDATA);
    case VT_BSTR: case VT_LPSTR: case VT_LPWSTR:
        return sizeof(void *);
    case VT_VARIANT:
        return sizeof(PROPVARIANT);
    }
    return 0;
}

static ULONG array_count(SAFEARRAY *array)
{
    LONG low = 0, high = 0;
    if (!array || SafeArrayGetDim(array) != 1 || FAILED(SafeArrayGetLBound(array, 1, &low)) ||
        FAILED(SafeArrayGetUBound(array, 1, &high)) || high < low)
        return 0;
    return (ULONG)(high - low + 1);
}

ULONG PsElementCount(const PROPVARIANT *value)
{
    VARTYPE vt = value->vt;
    if (vt & VT_BYREF)
        return 1;
    if (vt & VT_VECTOR)
        return value->cac.cElems;
    if (vt & VT_ARRAY)
        return array_count(value->parray);
    if (vt == VT_EMPTY || vt == VT_NULL || !PsValidType(vt))
        return 0;
    return 1;
}

HRESULT PsElementPointer(const PROPVARIANT *value, ULONG index, const void **element)
{
    *element = nullptr;
    VARTYPE vt = value->vt;
    if (vt & VT_VECTOR) {
        if (index >= value->cac.cElems)
            return E_INVALIDARG;
        UINT size = PsElementSize(vt & VT_TYPEMASK);
        if (!size)
            return E_UNEXPECTED;
        *element = (const BYTE *)value->cac.pElems + (SIZE_T)index * size;
        return S_OK;
    }
    if (vt & VT_ARRAY) {
        LONG low = 0;
        if (index >= array_count(value->parray) || FAILED(SafeArrayGetLBound(value->parray, 1, &low)))
            return E_INVALIDARG;
        LONG position = low + (LONG)index;
        void *item;
        HRESULT hr = SafeArrayPtrOfIndex(value->parray, &position, &item);
        if (SUCCEEDED(hr))
            *element = item;
        return hr;
    }
    if (index)
        return E_INVALIDARG;
    if (vt & VT_BYREF) {
        if (!value->byref)
            return E_INVALIDARG;
        *element = value->byref;
        return S_OK;
    }
    if (vt == VT_CLSID || vt == VT_CF) {
        if (!value->puuid)
            return E_INVALIDARG;
        *element = value->puuid;
        return S_OK;
    }
    *element = &value->cVal;
    return S_OK;
}

void *PsAllocZero(SIZE_T size)
{
    void *memory = CoTaskMemAlloc(size ? size : 1);
    if (memory)
        memset(memory, 0, size ? size : 1);
    return memory;
}

HRESULT PsAllocStringLen(LPCWSTR source, SIZE_T length, LPWSTR *out)
{
    *out = CoTaskMemAlloc((length + 1) * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    if (length)
        memcpy(*out, source, length * sizeof(WCHAR));
    (*out)[length] = 0;
    return S_OK;
}

HRESULT PsAllocString(LPCWSTR source, LPWSTR *out)
{
    if (!source)
        source = L"";
    return PsAllocStringLen(source, wcslen(source), out);
}

HRESULT PsAnsiToWideAlloc(LPCSTR source, LPWSTR *out)
{
    *out = nullptr;
    if (!source)
        source = "";
    int length = MultiByteToWideChar(CP_ACP, 0, source, -1, nullptr, 0);
    if (!length)
        return HRESULT_FROM_WIN32(GetLastError());
    *out = CoTaskMemAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, source, -1, *out, length);
    return S_OK;
}

HRESULT PsWideToAnsiAlloc(LPCWSTR source, LPSTR *out)
{
    *out = nullptr;
    if (!source)
        source = L"";
    int length = WideCharToMultiByte(CP_ACP, 0x400, source, -1, nullptr, 0, nullptr, nullptr);
    if (!length)
        return HRESULT_FROM_WIN32(GetLastError());
    *out = CoTaskMemAlloc((SIZE_T)length);
    if (!*out)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_ACP, 0x400, source, -1, *out, length, nullptr, nullptr);
    return S_OK;
}

/* StringCchCopyW: a too small buffer keeps what fits and reports it. */
HRESULT PsCopyString(LPWSTR buffer, UINT length, LPCWSTR source)
{
    if (!length || length > 0x7FFFFFFF)
        return E_INVALIDARG;
    if (!source)
        source = L"";
    UINT i = 0;
    while (i + 1 < length && source[i]) {
        buffer[i] = source[i];
        i++;
    }
    buffer[i] = 0;
    return source[i] ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL hex_field(LPCWSTR *p, UINT digits, ULONGLONG *value)
{
    *value = 0;
    for (UINT i = 0; i < digits; i++) {
        int d = hex_digit((*p)[i]);
        if (d < 0)
            return FALSE;
        *value = *value << 4 | (ULONGLONG)d;
    }
    *p += digits;
    return TRUE;
}

/* the braced form only, as GUIDFromStringW takes it. */
BOOL PsGuidFromString(LPCWSTR s, GUID *guid)
{
    static const UINT groups[] = {8, 4, 4, 4, 12};
    ULONGLONG fields[5];
    if (!s || *s++ != '{')
        return FALSE;
    for (UINT i = 0; i < 5; i++) {
        if (!hex_field(&s, groups[i], &fields[i]))
            return FALSE;
        if (*s++ != (i == 4 ? '}' : '-'))
            return FALSE;
    }
    guid->Data1 = (DWORD)fields[0];
    guid->Data2 = (WORD)fields[1];
    guid->Data3 = (WORD)fields[2];
    guid->Data4[0] = (BYTE)(fields[3] >> 8);
    guid->Data4[1] = (BYTE)fields[3];
    for (int i = 0; i < 6; i++)
        guid->Data4[2 + i] = (BYTE)(fields[4] >> (40 - 8 * i));
    return TRUE;
}

int PsStrCmp(LPCWSTR a, LPCWSTR b, BOOL ignore_case)
{
    int result = CompareStringW(LOCALE_USER_DEFAULT, ignore_case ? NORM_IGNORECASE : 0, a, -1, b, -1);
    return result ? result - CSTR_EQUAL : 0;
}

static BOOL is_digit(WCHAR c)
{
    return c >= '0' && c <= '9';
}

/* StrCmpLogicalW: runs of digits compare by value, the rest without case. */
int PsStrCmpLogical(LPCWSTR a, LPCWSTR b)
{
    while (*a && *b) {
        if (is_digit(*a) && is_digit(*b)) {
            while (*a == '0' && is_digit(a[1]))
                a++;
            while (*b == '0' && is_digit(b[1]))
                b++;
            SIZE_T la = 0, lb = 0;
            while (is_digit(a[la]))
                la++;
            while (is_digit(b[lb]))
                lb++;
            if (la != lb)
                return la < lb ? -1 : 1;
            for (SIZE_T i = 0; i < la; i++)
                if (a[i] != b[i])
                    return a[i] < b[i] ? -1 : 1;
            a += la;
            b += lb;
            continue;
        }
        if (is_digit(*a) != is_digit(*b))
            return is_digit(*a) ? -1 : 1;
        int result = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, a, 1, b, 1);
        if (result && result != CSTR_EQUAL)
            return result - CSTR_EQUAL;
        a++;
        b++;
    }
    if (*a)
        return 1;
    return *b ? -1 : 0;
}

/* doubles go through msvcrt, the one formatter of the system that knows them. */
typedef int(__cdecl *SNWPRINTF)(WCHAR *, SIZE_T, const WCHAR *, ...);
typedef double(__cdecl *WCSTOD)(const WCHAR *, WCHAR **);

static FARPROC msvcrt_function(const char *name)
{
    HMODULE module = GetModuleHandleW(L"msvcrt.dll");
    if (!module)
        module = LoadLibraryW(L"msvcrt.dll");
    return module ? GetProcAddress(module, name) : nullptr;
}

BOOL PsParseDouble(LPCWSTR string, double *value)
{
    WCSTOD parse = (WCSTOD)msvcrt_function("wcstod");
    *value = 0;
    if (!parse || !string || !*string)
        return FALSE;
    WCHAR *end = nullptr;
    double result = parse(string, &end);
    if (!end || *end)
        return FALSE;
    *value = result;
    return TRUE;
}

HRESULT PsDoubleToString(double value, LPWSTR *out)
{
    SNWPRINTF print = (SNWPRINTF)msvcrt_function("_snwprintf");
    WCHAR text[64];
    if (!print)
        return E_UNEXPECTED;
    print(text, 63, L"%.15G", value);
    text[63] = 0;
    double back;
    if (!PsParseDouble(text, &back) || back != value) {
        print(text, 63, L"%.18g", value);
        text[63] = 0;
    }
    return PsAllocString(text, out);
}
