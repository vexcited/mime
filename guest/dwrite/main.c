/*
 * the entry point of dwrite.dll, and the memory and text helpers its parts share.
 */
#include "dw_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void *DwAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }

void *DwAllocZero(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1); }

void DwFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

WCHAR *DwStrDupN(const WCHAR *s, UINT32 length)
{
    WCHAR *copy = DwAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!copy)
        return nullptr;
    if (length)
        memcpy(copy, s, length * sizeof(WCHAR));
    copy[length] = 0;
    return copy;
}

WCHAR *DwStrDup(const WCHAR *s) { return DwStrDupN(s, (UINT32)wcslen(s)); }

void DwLower(WCHAR *s)
{
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z')
            *s = (WCHAR)(*s + 32);
    }
}

static WCHAR fold(WCHAR c)
{
    if (c >= 'A' && c <= 'Z')
        return (WCHAR)(c + 32);
    if (c < 0x80)
        return c;
    /* latin-1 and the greek and cyrillic capitals, which names of fonts use. */
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9) || (c >= 0x410 && c <= 0x42F))
        return (WCHAR)(c + 32);
    if (c >= 0x400 && c <= 0x40F)
        return (WCHAR)(c + 80);
    return c;
}

int DwCompareNames(const WCHAR *a, const WCHAR *b)
{
    for (;; a++, b++) {
        WCHAR x = fold(*a), y = fold(*b);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

DWRITEAPI HRESULT WINAPI DWriteCreateFactory(DWRITE_FACTORY_TYPE type, REFIID iid, IUnknown **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (type != DWRITE_FACTORY_TYPE_SHARED && type != DWRITE_FACTORY_TYPE_ISOLATED)
        return E_INVALIDARG;
    if (!iid)
        return E_INVALIDARG;
    DwFactory *factory = type == DWRITE_FACTORY_TYPE_SHARED ? DwFactoryShared() : nullptr;
    if (type == DWRITE_FACTORY_TYPE_ISOLATED) {
        extern DwFactory *DwFactoryNew(BOOL shared);
        factory = DwFactoryNew(FALSE);
    }
    if (!factory)
        return E_OUTOFMEMORY;
    IUnknown *unknown = (IUnknown *)factory;
    HRESULT hr = unknown->lpVtbl->QueryInterface(unknown, iid, (void **)out);
    if (type == DWRITE_FACTORY_TYPE_ISOLATED)
        unknown->lpVtbl->Release(unknown);
    return hr;
}
