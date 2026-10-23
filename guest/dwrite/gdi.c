/*
 * IDWriteGdiInterop: converts between DirectWrite fonts and GDI LOGFONTs.
 */
#include "dw_private.h"

typedef struct DwInterop {
    const IDWriteGdiInteropVtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
} DwInterop;

static HRESULT WINAPI interop_query(IDWriteGdiInterop *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteGdiInterop)) {
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

static HRESULT font_for_logfont(DwFactory *factory, const LOGFONTW *lf, IDWriteFont **font)
{
    DwCollection *system = DwCollectionOf(factory, nullptr);
    if (!system)
        return E_FAIL;
    WCHAR name[LF_FACESIZE + 1];
    memcpy(name, lf->lfFaceName, LF_FACESIZE * sizeof(WCHAR));
    name[LF_FACESIZE] = 0;
    if (!name[0])
        wcscpy(name, L"Segoe UI");
    DWRITE_FONT_WEIGHT weight = lf->lfWeight ? (DWRITE_FONT_WEIGHT)lf->lfWeight : DWRITE_FONT_WEIGHT_NORMAL;
    if (lf->lfWeight < 0 || lf->lfWeight > 999)
        weight = DWRITE_FONT_WEIGHT_NORMAL;
    return DwCreateFontByName(system, name, weight, DWRITE_FONT_STRETCH_NORMAL,
                              lf->lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, font);
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

static const IDWriteGdiInteropVtbl g_interop_vtbl = {
    interop_query,        interop_addref,          interop_release,         interop_from_logfont,
    interop_to_logfont,   interop_face_to_logfont, interop_face_from_hdc,   interop_render_target,
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
