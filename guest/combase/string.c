/*
 * windows runtime strings. the layout is the one windows uses, which
 * C++/WinRT also builds itself: a header with flags, length and the
 * character pointer, then for allocated strings a reference count and the
 * characters. references live in a caller's HSTRING_HEADER and are never
 * freed; a NULL HSTRING is the empty string.
 */
#include "combase_private.h"

#define HSTRING_REFERENCE 0x1

typedef struct STRING_HEADER {
    UINT32 Flags;
    UINT32 Length;
    UINT32 Padding1;
    UINT32 Padding2;
    LPCWSTR Buffer;
} STRING_HEADER;

typedef struct SHARED_STRING {
    STRING_HEADER Header;
    volatile LONG References;
    WCHAR Text[1];
} SHARED_STRING;

_Static_assert(sizeof(STRING_HEADER) <= sizeof(HSTRING_HEADER), "string reference header size");

static HRESULT allocate(UINT32 length, SHARED_STRING **out)
{
    if (length > 0x7FFFFFF0 / sizeof(WCHAR))
        return E_OUTOFMEMORY;
    SHARED_STRING *s = CbAlloc(sizeof(SHARED_STRING) + length * sizeof(WCHAR));
    if (!s)
        return E_OUTOFMEMORY;
    s->Header.Flags = 0;
    s->Header.Length = length;
    s->Header.Padding1 = s->Header.Padding2 = 0;
    s->Header.Buffer = s->Text;
    s->References = 1;
    s->Text[length] = 0;
    *out = s;
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsCreateString(LPCWSTR source, UINT32 length, HSTRING *string)
{
    if (!string)
        return E_INVALIDARG;
    *string = NULL;
    if (!length)
        return S_OK;
    if (!source)
        return E_POINTER;
    SHARED_STRING *s;
    HRESULT hr = allocate(length, &s);
    if (FAILED(hr))
        return hr;
    memcpy(s->Text, source, length * sizeof(WCHAR));
    *string = (HSTRING)s;
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsCreateStringReference(LPCWSTR source, UINT32 length, HSTRING_HEADER *header,
                                                  HSTRING *string)
{
    if (!string || !header)
        return E_INVALIDARG;
    *string = NULL;
    if (!length)
        return S_OK;
    /* a reference must point at a terminated string. */
    if (!source || source[length])
        return E_INVALIDARG;
    STRING_HEADER *h = (STRING_HEADER *)header;
    h->Flags = HSTRING_REFERENCE;
    h->Length = length;
    h->Padding1 = h->Padding2 = 0;
    h->Buffer = source;
    *string = (HSTRING)h;
    return S_OK;
}

HSTRING CbStaticString(HSTRING_HEADER *header, LPCWSTR text)
{
    HSTRING string;
    WindowsCreateStringReference(text, (UINT32)wcslen(text), header, &string);
    return string;
}

ROAPI HRESULT WINAPI WindowsDeleteString(HSTRING string)
{
    SHARED_STRING *s = (SHARED_STRING *)string;
    if (s && !(s->Header.Flags & HSTRING_REFERENCE) && !InterlockedDecrement(&s->References))
        CbFree(s);
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsDuplicateString(HSTRING string, HSTRING *copy)
{
    if (!copy)
        return E_INVALIDARG;
    *copy = NULL;
    SHARED_STRING *s = (SHARED_STRING *)string;
    if (!s)
        return S_OK;
    /* a reference may not outlive its header, so duplicating it copies the text. */
    if (s->Header.Flags & HSTRING_REFERENCE)
        return WindowsCreateString(s->Header.Buffer, s->Header.Length, copy);
    InterlockedIncrement(&s->References);
    *copy = string;
    return S_OK;
}

ROAPI LPCWSTR WINAPI WindowsGetStringRawBuffer(HSTRING string, UINT32 *length)
{
    const STRING_HEADER *h = (const STRING_HEADER *)string;
    if (length)
        *length = h ? h->Length : 0;
    return h ? h->Buffer : L"";
}

ROAPI UINT32 WINAPI WindowsGetStringLen(HSTRING string)
{
    return string ? ((const STRING_HEADER *)string)->Length : 0;
}

ROAPI BOOL WINAPI WindowsIsStringEmpty(HSTRING string)
{
    return !WindowsGetStringLen(string);
}

ROAPI HRESULT WINAPI WindowsCompareStringOrdinal(HSTRING a, HSTRING b, INT32 *result)
{
    if (!result)
        return E_INVALIDARG;
    UINT32 la, lb;
    LPCWSTR sa = WindowsGetStringRawBuffer(a, &la), sb = WindowsGetStringRawBuffer(b, &lb);
    UINT32 n = la < lb ? la : lb;
    INT32 order = 0;
    for (UINT32 i = 0; i < n && !order; i++)
        order = sa[i] < sb[i] ? -1 : sa[i] > sb[i];
    *result = order ? order : la < lb ? -1 : la > lb;
    return S_OK;
}
