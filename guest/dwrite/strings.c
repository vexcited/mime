/*
 * IDWriteLocalizedStrings: the names of a font in the languages its name table has.
 */
#include "dw_private.h"

struct DwStrings {
    const IDWriteLocalizedStringsVtbl *lpVtbl;
    LONG refs;
    UINT32 count;
    UINT32 capacity;
    DwStringEntry *entries;
};

static DwStrings *from_interface(IDWriteLocalizedStrings *strings) { return (DwStrings *)strings; }

static HRESULT WINAPI strings_query(IDWriteLocalizedStrings *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteLocalizedStrings)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI strings_addref(IDWriteLocalizedStrings *This)
{
    return InterlockedIncrement(&from_interface(This)->refs);
}

static ULONG WINAPI strings_release(IDWriteLocalizedStrings *This)
{
    DwStrings *s = from_interface(This);
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        for (UINT32 i = 0; i < s->count; i++) {
            DwFree(s->entries[i].locale);
            DwFree(s->entries[i].text);
        }
        DwFree(s->entries);
        DwFree(s);
    }
    return refs;
}

static UINT32 WINAPI strings_get_count(IDWriteLocalizedStrings *This) { return from_interface(This)->count; }

static HRESULT WINAPI strings_find(IDWriteLocalizedStrings *This, const WCHAR *locale, UINT32 *index, BOOL *exists)
{
    DwStrings *s = from_interface(This);
    if (!index || !exists)
        return E_POINTER;
    *index = (UINT32)-1;
    *exists = FALSE;
    if (!locale)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < s->count; i++) {
        if (!_wcsicmp(s->entries[i].locale, locale)) {
            *index = i;
            *exists = TRUE;
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI strings_locale_length(IDWriteLocalizedStrings *This, UINT32 index, UINT32 *length)
{
    DwStrings *s = from_interface(This);
    if (!length)
        return E_POINTER;
    if (index >= s->count) {
        *length = (UINT32)-1;
        return E_FAIL;
    }
    *length = (UINT32)wcslen(s->entries[index].locale);
    return S_OK;
}

static HRESULT copy_out(const WCHAR *text, WCHAR *buffer, UINT32 size)
{
    UINT32 length = (UINT32)wcslen(text);
    if (!buffer || size < length + 1)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, text, (length + 1) * sizeof(WCHAR));
    return S_OK;
}

static HRESULT WINAPI strings_locale_name(IDWriteLocalizedStrings *This, UINT32 index, WCHAR *name, UINT32 size)
{
    DwStrings *s = from_interface(This);
    if (index >= s->count)
        return E_FAIL;
    return copy_out(s->entries[index].locale, name, size);
}

static HRESULT WINAPI strings_string_length(IDWriteLocalizedStrings *This, UINT32 index, UINT32 *length)
{
    DwStrings *s = from_interface(This);
    if (!length)
        return E_POINTER;
    if (index >= s->count) {
        *length = (UINT32)-1;
        return E_FAIL;
    }
    *length = (UINT32)wcslen(s->entries[index].text);
    return S_OK;
}

static HRESULT WINAPI strings_string(IDWriteLocalizedStrings *This, UINT32 index, WCHAR *string, UINT32 size)
{
    DwStrings *s = from_interface(This);
    if (index >= s->count)
        return E_FAIL;
    return copy_out(s->entries[index].text, string, size);
}

static const IDWriteLocalizedStringsVtbl g_strings_vtbl = {
    strings_query,         strings_addref,        strings_release,       strings_get_count,
    strings_find,          strings_locale_length, strings_locale_name,   strings_string_length,
    strings_string,
};

DwStrings *DwStringsNew(void)
{
    DwStrings *s = DwAllocZero(sizeof(*s));
    if (s) {
        s->lpVtbl = &g_strings_vtbl;
        s->refs = 1;
    }
    return s;
}

BOOL DwStringsAdd(DwStrings *s, const WCHAR *locale, UINT32 locale_length, const WCHAR *text, UINT32 length)
{
    WCHAR *l = DwStrDupN(locale, locale_length);
    if (!l)
        return FALSE;
    DwLower(l);
    for (UINT32 i = 0; i < s->count; i++) {
        if (!wcscmp(s->entries[i].locale, l)) {
            DwFree(l);
            return TRUE;
        }
    }
    if (s->count == s->capacity) {
        UINT32 capacity = s->capacity ? s->capacity * 2 : 4;
        DwStringEntry *entries = DwAlloc(capacity * sizeof(*entries));
        if (!entries) {
            DwFree(l);
            return FALSE;
        }
        if (s->count)
            memcpy(entries, s->entries, s->count * sizeof(*entries));
        DwFree(s->entries);
        s->entries = entries;
        s->capacity = capacity;
    }
    WCHAR *t = DwStrDupN(text, length);
    if (!t) {
        DwFree(l);
        return FALSE;
    }
    s->entries[s->count].locale = l;
    s->entries[s->count].text = t;
    s->count++;
    return TRUE;
}

UINT32 DwStringsCount(const DwStrings *s) { return s ? s->count : 0; }

const DwStringEntry *DwStringsAt(const DwStrings *s, UINT32 index)
{
    return s && index < s->count ? &s->entries[index] : nullptr;
}

const WCHAR *DwStringsBest(const DwStrings *s, const WCHAR *locale)
{
    if (!s || !s->count)
        return nullptr;
    if (locale) {
        for (UINT32 i = 0; i < s->count; i++) {
            if (!_wcsicmp(s->entries[i].locale, locale))
                return s->entries[i].text;
        }
    }
    for (UINT32 i = 0; i < s->count; i++) {
        if (!wcscmp(s->entries[i].locale, L"en-us"))
            return s->entries[i].text;
    }
    return s->entries[0].text;
}

IDWriteLocalizedStrings *DwStringsInterface(DwStrings *s) { return (IDWriteLocalizedStrings *)s; }

DwStrings *DwStringsCopy(const DwStrings *s)
{
    DwStrings *copy = DwStringsNew();
    for (UINT32 i = 0; copy && s && i < s->count; i++)
        DwStringsAdd(copy, s->entries[i].locale, (UINT32)wcslen(s->entries[i].locale), s->entries[i].text,
                     (UINT32)wcslen(s->entries[i].text));
    return copy;
}

void DwStringsAddRef(DwStrings *s) { InterlockedIncrement(&s->refs); }

void DwStringsRelease(DwStrings *s)
{
    if (s)
        strings_release((IDWriteLocalizedStrings *)s);
}
