/*
 * CoGetObject for names that stand for a class: "new:" makes an instance, "clsid:" gets the class factory, and the
 * COM elevation moniker prefix is stripped. other names need file, item and composite monikers and a display name
 * parser, which are missing here, so they fail as a syntax error.
 */
#include "ole32_private.h"

#define MK_E_SYNTAX_ ((HRESULT)0x800401E4L)

/* case insensitive prefix match, the remainder goes in *rest. */
static BOOL strip(PCWSTR text, PCWSTR prefix, PCWSTR *rest)
{
    SIZE_T length = wcslen(prefix);
    if (_wcsnicmp(text, prefix, length))
        return FALSE;
    *rest = text + length;
    return TRUE;
}

/* a braced guid or a ProgID, with an optional trailing colon. */
static HRESULT parse_class(PCWSTR text, CLSID *clsid)
{
    WCHAR copy[256];
    SIZE_T length = wcslen(text);
    if (!length || length >= RTL_NUMBER_OF(copy))
        return MK_E_SYNTAX_;
    wcscpy(copy, text);
    if (copy[length - 1] == L':')
        copy[length - 1] = 0;
    HRESULT hr = copy[0] == L'{' ? CLSIDFromString(copy, clsid) : CLSIDFromProgID(copy, clsid);
    return FAILED(hr) ? MK_E_SYNTAX_ : S_OK;
}

/* dwClassContext of BIND_OPTS2, when given. */
static DWORD context_of(const BIND_OPTS *options)
{
    if (options && options->cbStruct >= sizeof(BIND_OPTS2)) {
        DWORD context = ((const BIND_OPTS2 *)options)->dwClassContext;
        if (context)
            return context;
    }
    return CLSCTX_ALL;
}

WINOLEAPI HRESULT WINAPI CoGetObject(LPCWSTR name, BIND_OPTS *options, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!name || !riid)
        return E_INVALIDARG;
    if (options && options->cbStruct < sizeof(BIND_OPTS))
        return E_INVALIDARG;
    /* the elevation moniker asks for an administrator, which the session already is. */
    PCWSTR rest = name;
    if (strip(name, L"Elevation:Administrator!", &rest) || strip(name, L"Elevation:Highest!", &rest))
        name = rest;
    CLSID clsid;
    HRESULT hr;
    if (strip(name, L"new:", &rest)) {
        hr = parse_class(rest, &clsid);
        if (FAILED(hr))
            return hr;
        return CoCreateInstance(&clsid, nullptr, context_of(options), riid, object);
    }
    if (strip(name, L"clsid:", &rest)) {
        hr = parse_class(rest, &clsid);
        if (FAILED(hr))
            return hr;
        return CoGetClassObject(&clsid, context_of(options), nullptr, riid, object);
    }
    return MK_E_SYNTAX_;
}
