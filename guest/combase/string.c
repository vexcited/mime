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

static_assert(sizeof(STRING_HEADER) <= sizeof(HSTRING_HEADER), "string reference header size");

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
    *string = nullptr;
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
    *string = nullptr;
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
    *copy = nullptr;
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

/* ------------------------------------------------------------------ building new strings */

#define BUFFER_MAGIC 0xF8B1A8BEu
#define ERROR_ARITHMETIC_OVERFLOW_HR ((HRESULT)0x80070216L)

static LPCWSTR text_of(HSTRING string, UINT32 *length)
{
    return WindowsGetStringRawBuffer(string, length);
}

/* a new string from characters, which are empty for the null string. */
static HRESULT make(LPCWSTR text, UINT32 length, HSTRING *out)
{
    *out = nullptr;
    if (!length)
        return S_OK;
    SHARED_STRING *s;
    HRESULT hr = allocate(length, &s);
    if (FAILED(hr))
        return hr;
    memcpy(s->Text, text, length * sizeof(WCHAR));
    *out = (HSTRING)s;
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsConcatString(HSTRING a, HSTRING b, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    UINT32 la, lb;
    LPCWSTR sa = text_of(a, &la), sb = text_of(b, &lb);
    if (!lb)
        return WindowsDuplicateString(a, result);
    if (!la)
        return WindowsDuplicateString(b, result);
    if (la + lb < la)
        return ERROR_ARITHMETIC_OVERFLOW_HR;
    SHARED_STRING *s;
    HRESULT hr = allocate(la + lb, &s);
    if (FAILED(hr))
        return hr;
    memcpy(s->Text, sa, la * sizeof(WCHAR));
    memcpy(s->Text + la, sb, lb * sizeof(WCHAR));
    *result = (HSTRING)s;
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsStringHasEmbeddedNull(HSTRING string, BOOL *has_null)
{
    if (!has_null)
        return E_INVALIDARG;
    UINT32 length;
    LPCWSTR text = text_of(string, &length);
    *has_null = FALSE;
    for (UINT32 i = 0; i < length; i++) {
        if (!text[i]) {
            *has_null = TRUE;
            break;
        }
    }
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsSubstring(HSTRING string, UINT32 start, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    UINT32 length;
    LPCWSTR text = text_of(string, &length);
    if (start > length)
        return E_BOUNDS;
    return make(text + start, length - start, result);
}

ROAPI HRESULT WINAPI WindowsSubstringWithSpecifiedLength(HSTRING string, UINT32 start, UINT32 count, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    UINT32 length;
    LPCWSTR text = text_of(string, &length);
    if (start > length)
        return E_BOUNDS;
    if (start + count < start)
        return ERROR_ARITHMETIC_OVERFLOW_HR;
    if (start + count > length)
        return E_BOUNDS;
    return make(text + start, count, result);
}

static BOOL in_set(WCHAR c, LPCWSTR set, UINT32 count)
{
    for (UINT32 i = 0; i < count; i++) {
        if (set[i] == c)
            return TRUE;
    }
    return FALSE;
}

/* the characters of the second string are a set, not a sequence. */
ROAPI HRESULT WINAPI WindowsTrimStringStart(HSTRING string, HSTRING trim, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (WindowsIsStringEmpty(trim))
        return E_INVALIDARG;
    UINT32 length, set_count;
    LPCWSTR text = text_of(string, &length), set = text_of(trim, &set_count);
    UINT32 start = 0;
    while (start < length && in_set(text[start], set, set_count))
        start++;
    if (!start)
        return WindowsDuplicateString(string, result);
    return make(text + start, length - start, result);
}

ROAPI HRESULT WINAPI WindowsTrimStringEnd(HSTRING string, HSTRING trim, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (WindowsIsStringEmpty(trim))
        return E_INVALIDARG;
    UINT32 length, set_count;
    LPCWSTR text = text_of(string, &length), set = text_of(trim, &set_count);
    UINT32 end = length;
    while (end > 0 && in_set(text[end - 1], set, set_count))
        end--;
    if (end == length)
        return WindowsDuplicateString(string, result);
    return make(text, end, result);
}

/* every match from the left, none overlapping. */
ROAPI HRESULT WINAPI WindowsReplaceString(HSTRING string, HSTRING from, HSTRING to, HSTRING *result)
{
    if (!result)
        return E_INVALIDARG;
    *result = nullptr;
    if (WindowsIsStringEmpty(from))
        return E_INVALIDARG;
    UINT32 length, from_count, to_count;
    LPCWSTR text = text_of(string, &length), old = text_of(from, &from_count), repl = text_of(to, &to_count);
    if (!length)
        return S_OK;
    UINT64 total = 0;
    for (UINT32 i = 0; i < length;) {
        if (length - i >= from_count && !memcmp(text + i, old, from_count * sizeof(WCHAR))) {
            total += to_count;
            i += from_count;
        } else {
            total++;
            i++;
        }
    }
    if (total > 0xFFFFFFFFu)
        return ERROR_ARITHMETIC_OVERFLOW_HR;
    if (!total)
        return S_OK;
    SHARED_STRING *s;
    HRESULT hr = allocate((UINT32)total, &s);
    if (FAILED(hr))
        return hr;
    WCHAR *at = s->Text;
    for (UINT32 i = 0; i < length;) {
        if (length - i >= from_count && !memcmp(text + i, old, from_count * sizeof(WCHAR))) {
            memcpy(at, repl, to_count * sizeof(WCHAR));
            at += to_count;
            i += from_count;
        } else {
            *at++ = text[i++];
        }
    }
    *result = (HSTRING)s;
    return S_OK;
}

/*
 * a string whose characters are written by the caller. The handle is the string itself with a magic in its flags,
 * which promoting clears. An empty buffer promotes to the null string.
 */
ROAPI HRESULT WINAPI WindowsPreallocateStringBuffer(UINT32 length, WCHAR **buffer, HSTRING_BUFFER *handle)
{
    if (!buffer || !handle)
        return E_POINTER;
    *buffer = nullptr;
    *handle = nullptr;
    SHARED_STRING *s;
    HRESULT hr = allocate(length, &s);
    if (FAILED(hr))
        return length > 0x7FFFFFF0 / sizeof(WCHAR) ? ERROR_ARITHMETIC_OVERFLOW_HR : hr;
    memset(s->Text, 0, (length + 1) * sizeof(WCHAR));
    s->Header.Flags = BUFFER_MAGIC;
    *handle = (HSTRING_BUFFER)s;
    *buffer = s->Text;
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsPromoteStringBuffer(HSTRING_BUFFER handle, HSTRING *string)
{
    if (!string)
        return E_POINTER;
    *string = nullptr;
    SHARED_STRING *s = (SHARED_STRING *)handle;
    if (!s)
        return S_OK;
    if (s->Header.Flags != BUFFER_MAGIC || s->Text[s->Header.Length])
        return E_INVALIDARG;
    s->Header.Flags = 0;
    if (s->Header.Length)
        *string = (HSTRING)s;
    else
        WindowsDeleteString((HSTRING)s);
    return S_OK;
}

ROAPI HRESULT WINAPI WindowsDeleteStringBuffer(HSTRING_BUFFER handle)
{
    SHARED_STRING *s = (SHARED_STRING *)handle;
    if (!s)
        return S_OK;
    if (s->Header.Flags != BUFFER_MAGIC)
        RaiseException(STATUS_INVALID_PARAMETER, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    s->Header.Flags = 0;
    return WindowsDeleteString((HSTRING)s);
}
