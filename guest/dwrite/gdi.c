/*
 * IDWriteGdiInterop: converts between DirectWrite fonts and GDI LOGFONTs.
 */
#include "dw_private.h"

typedef struct DwInterop {
    const IDWriteGdiInterop1Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
} DwInterop;

static HRESULT WINAPI interop_query(IDWriteGdiInterop *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteGdiInterop) ||
        IsEqualGUID(riid, &IID_IDWriteGdiInterop1)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI interop_addref(IDWriteGdiInterop *This) { return InterlockedIncrement(&((DwInterop *)This)->refs); }

static ULONG WINAPI interop_release(IDWriteGdiInterop *This)
{
    DwInterop *i = (DwInterop *)This;
    ULONG refs = InterlockedDecrement(&i->refs);
    if (!refs)
        DwFree(i);
    return refs;
}

static HRESULT font_in_collection(DwFactory *factory, DwCollection *system, const LOGFONTW *lf, IDWriteFont **font)
{
    if (!system)
        return E_FAIL;
    WCHAR name[LF_FACESIZE + 1];
    memcpy(name, lf->lfFaceName, LF_FACESIZE * sizeof(WCHAR));
    name[LF_FACESIZE] = 0;
    /* a font without a name is the system's, and if Segoe UI is not installed the open font of the ROM is. */
    if (!name[0])
        wcscpy(name, DwFindFamily(system, L"Segoe UI", &(UINT32){0}) ? L"Segoe UI" : MIME_FALLBACK_FONT);
    DWRITE_FONT_WEIGHT weight = lf->lfWeight ? (DWRITE_FONT_WEIGHT)lf->lfWeight : DWRITE_FONT_WEIGHT_NORMAL;
    if (lf->lfWeight < 0 || lf->lfWeight > 999)
        weight = DWRITE_FONT_WEIGHT_NORMAL;
    return DwCreateFontByName(system, name, weight, DWRITE_FONT_STRETCH_NORMAL,
                              lf->lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, font);
}

static HRESULT font_for_logfont(DwFactory *factory, const LOGFONTW *lf, IDWriteFont **font)
{
    return font_in_collection(factory, DwCollectionOf(factory, nullptr), lf, font);
}

static HRESULT WINAPI interop_from_logfont(IDWriteGdiInterop *This, const LOGFONTW *lf, IDWriteFont **font)
{
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (!lf)
        return E_INVALIDARG;
    return font_for_logfont(((DwInterop *)This)->factory, lf, font);
}

static HRESULT WINAPI interop_to_logfont(IDWriteGdiInterop *This, IDWriteFont *font, LOGFONTW *lf, BOOL *is_system)
{
    DwInterop *interop = (DwInterop *)This;
    if (!lf || !is_system)
        return E_POINTER;
    memset(lf, 0, sizeof(*lf));
    *is_system = FALSE;
    DwCollection *c;
    const DwEntry *e;
    if (!font || !DwFontEntryOf(font, &c, &e))
        return E_INVALIDARG;
    const WCHAR *name = DwStringsBest(e->win32_family, L"en-us");
    if (!name) {
        UINT32 family = 0;
        for (UINT32 i = 0; i < c->count; i++) {
            if (e >= c->families[i].entries && e < c->families[i].entries + c->families[i].count)
                family = i;
        }
        name = DwStringsBest(c->families[family].names, L"en-us");
    }
    if (!name)
        return DWRITE_E_NOFONT;
    lf->lfWeight = (LONG)e->attributes.weight;
    lf->lfItalic = e->attributes.style != DWRITE_FONT_STYLE_NORMAL;
    lf->lfCharSet = DEFAULT_CHARSET;
    size_t n = wcslen(name);
    if (n >= LF_FACESIZE)
        n = LF_FACESIZE - 1;
    memcpy(lf->lfFaceName, name, n * sizeof(WCHAR));
    *is_system = c == DwCollectionOf(interop->factory, nullptr);
    return S_OK;
}

static HRESULT WINAPI interop_face_to_logfont(IDWriteGdiInterop *This, IDWriteFontFace *face, LOGFONTW *lf)
{
    DwInterop *interop = (DwInterop *)This;
    if (!lf)
        return E_POINTER;
    memset(lf, 0, sizeof(*lf));
    if (!face)
        return E_INVALIDARG;
    DwCollection *system = DwCollectionOf(interop->factory, nullptr);
    UINT32 family, entry;
    if (!system || !DwFindEntry(system, face, &family, &entry))
        return DWRITE_E_NOFONT;
    IDWriteFont *font;
    HRESULT hr = DwFontCreate(system, family, entry, &font);
    if (FAILED(hr))
        return hr;
    BOOL is_system;
    hr = interop_to_logfont(This, font, lf, &is_system);
    font->lpVtbl->Release(font);
    return hr;
}

static HRESULT WINAPI interop_face_from_hdc(IDWriteGdiInterop *This, HDC dc, IDWriteFontFace **face)
{
    DwInterop *interop = (DwInterop *)This;
    if (!face)
        return E_POINTER;
    *face = nullptr;
    if (!dc)
        return E_INVALIDARG;
    HGDIOBJ object = GetCurrentObject(dc, OBJ_FONT);
    LOGFONTW lf;
    if (!object || !GetObjectW(object, sizeof(lf), &lf))
        return E_FAIL;
    IDWriteFont *font;
    HRESULT hr = font_for_logfont(interop->factory, &lf, &font);
    if (FAILED(hr))
        return hr;
    hr = font->lpVtbl->CreateFontFace(font, face);
    font->lpVtbl->Release(font);
    return hr;
}

static HRESULT WINAPI interop_render_target(IDWriteGdiInterop *This, HDC dc, UINT32 width, UINT32 height,
                                            IDWriteBitmapRenderTarget **target)
{
    if (target)
        *target = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI interop_from_logfont1(IDWriteGdiInterop *This, const LOGFONTW *lf,
                                            IDWriteFontCollection *collection, IDWriteFont **font)
{
    DwInterop *interop = (DwInterop *)This;
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (!lf)
        return E_INVALIDARG;
    DwCollection *c = DwCollectionOf(interop->factory, collection);
    if (!c)
        return E_INVALIDARG;
    return font_in_collection(interop->factory, c, lf, font);
}

/* the unicode and code page ranges of the OS/2 table. */
static HRESULT signature_of(DwFontData *data, FONTSIGNATURE *signature)
{
    memset(signature, 0, sizeof(*signature));
    UINT32 size = 0;
    const BYTE *os2 = DwFontDataTable(data, DW_TAG('O', 'S', '/', '2'), &size);
    if (!os2 || size < 58)
        return S_OK;
    for (int i = 0; i < 4; i++)
        signature->fsUsb[i] = DwBe32(os2 + 42 + 4 * i);
    if (DwBe16(os2) >= 1 && size >= 86) {
        signature->fsCsb[0] = DwBe32(os2 + 78);
        signature->fsCsb[1] = DwBe32(os2 + 82);
    }
    return S_OK;
}

static HRESULT WINAPI interop_face_signature(IDWriteGdiInterop *This, IDWriteFontFace *face, FONTSIGNATURE *signature)
{
    if (!signature)
        return E_POINTER;
    memset(signature, 0, sizeof(*signature));
    DwFace *f = DwFaceFrom(face);
    if (!f)
        return E_INVALIDARG;
    return signature_of(f->data, signature);
}

static HRESULT WINAPI interop_font_signature(IDWriteGdiInterop *This, IDWriteFont *font, FONTSIGNATURE *signature)
{
    if (!signature)
        return E_POINTER;
    memset(signature, 0, sizeof(*signature));
    DwCollection *c;
    const DwEntry *e;
    if (!font || !DwFontEntryOf(font, &c, &e))
        return E_INVALIDARG;
    DwFontData *data;
    HRESULT hr = DwFontDataOpen(e->file, e->face_index, &data);
    if (FAILED(hr))
        return hr;
    hr = signature_of(data, signature);
    DwFontDataRelease(data);
    return hr;
}

/* the fonts of the set in the LOGFONT's win32 family, the closest to its weight and italic first. */
static HRESULT WINAPI interop_matching_logfont(IDWriteGdiInterop *This, const LOGFONTW *lf, IDWriteFontSet *set,
                                               IDWriteFontSet **subset)
{
    if (!subset)
        return E_POINTER;
    *subset = nullptr;
    if (!lf || !set)
        return E_INVALIDARG;
    WCHAR name[LF_FACESIZE + 1];
    memcpy(name, lf->lfFaceName, LF_FACESIZE * sizeof(WCHAR));
    name[LF_FACESIZE] = 0;
    DWRITE_FONT_PROPERTY property = {DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME, name, nullptr};
    IDWriteFontSet *family;
    HRESULT hr = set->lpVtbl->GetMatchingFonts(set, &property, 1, &family);
    if (FAILED(hr))
        return hr;
    UINT32 count = family->lpVtbl->GetFontCount(family);
    if (!count) {
        *subset = family;
        return S_OK;
    }
    /* the order of a family request, by the names the set gives the family. */
    IDWriteLocalizedStrings *names;
    BOOL exists = FALSE;
    hr = family->lpVtbl->GetFontPropertyValues(family, 0, DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FAMILY_NAME,
                                               &exists, &names);
    if (FAILED(hr) || !exists) {
        *subset = family;
        return S_OK;
    }
    WCHAR wws[256] = {};
    names->lpVtbl->GetString(names, 0, wws, 256);
    names->lpVtbl->Release(names);
    DWRITE_FONT_WEIGHT weight = lf->lfWeight > 0 && lf->lfWeight <= 999 ? (DWRITE_FONT_WEIGHT)lf->lfWeight
                                                                        : DWRITE_FONT_WEIGHT_NORMAL;
    hr = family->lpVtbl->GetMatchingFontsByName(family, wws, weight, DWRITE_FONT_STRETCH_NORMAL,
                                                lf->lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                                subset);
    family->lpVtbl->Release(family);
    return hr;
}

static const IDWriteGdiInterop1Vtbl g_interop_vtbl = {
    (void *)interop_query,          (void *)interop_addref,        (void *)interop_release,
    (void *)interop_from_logfont,   (void *)interop_to_logfont,    (void *)interop_face_to_logfont,
    (void *)interop_face_from_hdc,  (void *)interop_render_target, (void *)interop_from_logfont1,
    (void *)interop_face_signature, (void *)interop_font_signature, (void *)interop_matching_logfont,
};

HRESULT DwInteropCreate(DwFactory *factory, IDWriteGdiInterop **out)
{
    DwInterop *i = DwAllocZero(sizeof(*i));
    if (!i)
        return E_OUTOFMEMORY;
    i->lpVtbl = &g_interop_vtbl;
    i->refs = 1;
    i->factory = factory;
    *out = (IDWriteGdiInterop *)i;
    return S_OK;
}
