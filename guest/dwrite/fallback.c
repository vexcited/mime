/*
 * IDWriteFontFallback: picks a font for characters the requested family
 * lacks. the requested family goes first, then the ones windows uses for the
 * character's script, then any font that has it.
 */
#include "dw_private.h"

typedef struct DwFallback {
    const IDWriteFontFallbackVtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
} DwFallback;

static HRESULT WINAPI fallback_query(IDWriteFontFallback *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFallback)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI fallback_addref(IDWriteFontFallback *This) { return InterlockedIncrement(&((DwFallback *)This)->refs); }

static ULONG WINAPI fallback_release(IDWriteFontFallback *This)
{
    DwFallback *f = (DwFallback *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs)
        DwFree(f);
    return refs;
}

static const WCHAR *const g_emoji[] = {L"Segoe UI Emoji", L"Segoe UI Symbol", nullptr};
static const WCHAR *const g_cjk[] = {L"Microsoft YaHei", L"Yu Gothic UI", L"MS Gothic", L"SimSun",
                                     L"Microsoft JhengHei", L"Malgun Gothic", nullptr};
static const WCHAR *const g_thai[] = {L"Leelawadee UI", L"Tahoma", L"Microsoft Sans Serif", nullptr};
static const WCHAR *const g_hangul[] = {L"Malgun Gothic", L"Microsoft YaHei", L"Gulim", nullptr};
static const WCHAR *const g_general[] = {
    L"Segoe UI", L"Segoe UI Symbol", L"Arial", L"Times New Roman", L"Cambria Math", L"Segoe UI Historic",
    L"Nirmala UI", L"Ebrima", L"Gadugi", L"Leelawadee UI", L"Myanmar Text", L"Microsoft Himalaya",
    L"Mongolian Baiti", L"Microsoft New Tai Lue", L"Microsoft Tai Le", L"Microsoft PhagsPa", L"Javanese Text",
    L"Sylfaen", L"Segoe UI Emoji", L"Microsoft YaHei", L"Yu Gothic UI", L"Malgun Gothic", L"MS Gothic",
    L"Microsoft JhengHei", L"SimSun", nullptr};

static BOOL is_emoji(UINT32 c)
{
    return (c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0x2600 && c <= 0x27BF) || c == 0xFE0F || c == 0x200D;
}

static BOOL is_ignorable(UINT32 c)
{
    return c == 0x200C || c == 0x200D || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x34F;
}

static BOOL is_thai(UINT32 c) { return c >= 0x0E00 && c <= 0x0E7F; }

static BOOL is_hangul(UINT32 c) { return (c >= 0x1100 && c <= 0x11FF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x3130 && c <= 0x318F); }

static BOOL is_cjk(UINT32 c)
{
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) ||
           (c >= 0x20000 && c <= 0x2FFFF);
}

static UINT32 next_code_point(const WCHAR *text, UINT32 length, UINT32 *position)
{
    UINT32 c = text[(*position)++];
    if (c >= 0xD800 && c < 0xDC00 && *position < length && text[*position] >= 0xDC00 && text[*position] < 0xE000)
        c = 0x10000 + ((c - 0xD800) << 10) + (text[(*position)++] - 0xDC00);
    return c;
}

/* opens the family's best entry for the request if it has the character. */
static BOOL try_family(DwCollection *c, UINT32 family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                       DWRITE_FONT_STYLE style, UINT32 cp, UINT32 *entry, DwFontData **data)
{
    const DwFamily *f = &c->families[family];
    if (!f->count)
        return FALSE;
    UINT32 e = DwBestEntry(f, weight, stretch, style);
    DwFontData *d;
    if (FAILED(DwFontDataOpen(f->entries[e].file, f->entries[e].face_index, &d)))
        return FALSE;
    if (!DwFontDataHasCharacter(d, cp)) {
        DwFontDataRelease(d);
        return FALSE;
    }
    *entry = e;
    *data = d;
    return TRUE;
}

static BOOL pick(DwCollection *c, DwCollection *system, const WCHAR *base, DWRITE_FONT_WEIGHT weight,
                 DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, UINT32 cp, DwCollection **found_in,
                 UINT32 *family, UINT32 *entry, DwFontData **data)
{
    UINT32 index;
    if (base && *base && DwFindFamilyExact(c, base, &index) &&
        try_family(c, index, weight, stretch, style, cp, entry, data)) {
        *found_in = c;
        *family = index;
        return TRUE;
    }
    const WCHAR *const *lists[2] = {is_emoji(cp)    ? g_emoji
                                    : is_thai(cp)   ? g_thai
                                    : is_hangul(cp) ? g_hangul
                                    : is_cjk(cp)    ? g_cjk
                                                    : nullptr,
                                    g_general};
    for (int l = 0; l < 2; l++) {
        for (const WCHAR *const *name = lists[l]; name && *name; name++) {
            if (DwFindFamily(system, *name, &index) && try_family(system, index, weight, stretch, style, cp, entry, data)) {
                *found_in = system;
                *family = index;
                return TRUE;
            }
        }
    }
    /* last, any font that has it. */
    for (index = 0; index < system->count; index++) {
        if (try_family(system, index, weight, stretch, style, cp, entry, data)) {
            *found_in = system;
            *family = index;
            return TRUE;
        }
    }
    return FALSE;
}

static HRESULT WINAPI fallback_map(IDWriteFontFallback *This, IDWriteTextAnalysisSource *source, UINT32 position,
                                   UINT32 length, IDWriteFontCollection *base_collection, const WCHAR *base_family,
                                   DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
                                   UINT32 *mapped_length, IDWriteFont **mapped_font, FLOAT *scale)
{
    DwFallback *fb = (DwFallback *)This;
    if (!mapped_length || !mapped_font || !scale)
        return E_INVALIDARG;
    *mapped_length = 0;
    *mapped_font = nullptr;
    *scale = 1.0f;
    if (!source || !length || length > 0x100000)
        return E_INVALIDARG;
    WCHAR *text = DwAlloc((SIZE_T)length * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    UINT32 have = 0;
    while (have < length) {
        const WCHAR *chunk;
        UINT32 n = 0;
        HRESULT hr = source->lpVtbl->GetTextAtPosition(source, position + have, &chunk, &n);
        if (FAILED(hr) || !n || !chunk)
            break;
        if (n > length - have)
            n = length - have;
        memcpy(text + have, chunk, n * sizeof(WCHAR));
        have += n;
    }
    if (!have) {
        DwFree(text);
        return E_INVALIDARG;
    }
    DwCollection *system = DwCollectionOf(fb->factory, nullptr);
    DwCollection *c = base_collection ? DwCollectionOf(fb->factory, base_collection) : system;
    if (!c)
        c = system;
    UINT32 cursor = 0, first = next_code_point(text, have, &cursor);
    DwCollection *found_in;
    UINT32 family, entry;
    DwFontData *data;
    HRESULT hr = S_OK;
    UINT32 base_index = 0xFFFFFFFF;
    BOOL has_base = base_family && *base_family && DwFindFamilyExact(c, base_family, &base_index);
    if (!has_base)
        has_base = DwFindFamilyExact(c, L"Segoe UI", &base_index);
    if (system && pick(c, system, base_family, weight, stretch, style, first, &found_in, &family, &entry, &data)) {
        /* the run goes on while the font has the characters, and a font that is not the requested one gives way to it. */
        BOOL is_base = has_base && found_in == c && family == base_index;
        UINT32 end = cursor;
        while (end < have) {
            UINT32 at = end;
            UINT32 cp = next_code_point(text, have, &at);
            if (is_ignorable(cp)) {
                end = at;
                continue;
            }
            if (!DwFontDataHasCharacter(data, cp))
                break;
            if (!is_base && has_base) {
                UINT32 base_entry;
                DwFontData *base_data;
                if (try_family(c, base_index, weight, stretch, style, cp, &base_entry, &base_data)) {
                    DwFontDataRelease(base_data);
                    break;
                }
            }
            end = at;
        }
        DwFontDataRelease(data);
        *mapped_length = end;
        hr = DwFontCreate(found_in, family, entry, mapped_font);
    } else {
        /* no font has it: the characters after it that no font has either go with it. */
        UINT32 end = cursor;
        while (end < have) {
            UINT32 at = end;
            UINT32 cp = next_code_point(text, have, &at);
            if (system && pick(c, system, base_family, weight, stretch, style, cp, &found_in, &family, &entry, &data)) {
                DwFontDataRelease(data);
                break;
            }
            end = at;
        }
        *mapped_length = end;
    }
    DwFree(text);
    return hr;
}

static const IDWriteFontFallbackVtbl g_fallback_vtbl = {
    fallback_query, fallback_addref, fallback_release, fallback_map,
};

HRESULT DwFallbackCreate(DwFactory *factory, IDWriteFontFallback **out)
{
    DwFallback *f = DwAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &g_fallback_vtbl;
    f->refs = 1;
    f->factory = factory;
    *out = (IDWriteFontFallback *)f;
    return S_OK;
}
