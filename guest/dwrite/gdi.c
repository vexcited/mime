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
    /* a font without a name is the system's. */
    if (!name[0])
        wcscpy(name, L"Segoe UI");
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

/*
 * IDWriteBitmapRenderTarget: a memory dc with a 32-bit bitmap that glyph runs are drawn into. the bitmap starts black,
 * its first row is the top one. cleartype blends each color with its own coverage and leaves the fourth byte alone,
 * grayscale blends all four bytes. the black box is the one of the run, not cut to the bitmap.
 */
typedef struct DwTarget {
    const IDWriteBitmapRenderTarget2Vtbl *lpVtbl;
    LONG refs;
    HDC dc;
    HBITMAP bitmap, initial;
    UINT32 *bits;
    UINT32 width, height;
    FLOAT pixels_per_dip;
    DWRITE_MATRIX transform;
    DWRITE_TEXT_ANTIALIAS_MODE antialias;
} DwTarget;

static HRESULT WINAPI target_query(IDWriteBitmapRenderTarget2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteBitmapRenderTarget) ||
        IsEqualGUID(riid, &IID_IDWriteBitmapRenderTarget1) || IsEqualGUID(riid, &IID_IDWriteBitmapRenderTarget2)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI target_addref(IDWriteBitmapRenderTarget2 *This) { return InterlockedIncrement(&((DwTarget *)This)->refs); }

static ULONG WINAPI target_release(IDWriteBitmapRenderTarget2 *This)
{
    DwTarget *t = (DwTarget *)This;
    ULONG refs = InterlockedDecrement(&t->refs);
    if (!refs) {
        if (t->dc) {
            if (t->initial)
                SelectObject(t->dc, t->initial);
            DeleteDC(t->dc);
        }
        if (t->bitmap)
            DeleteObject(t->bitmap);
        DwFree(t);
    }
    return refs;
}

/* a bitmap of no size keeps one pixel behind it, as a dc must hold a bitmap. */
static HRESULT target_set_bitmap(DwTarget *t, UINT32 width, UINT32 height)
{
    if (width > 0x4000 || height > 0x4000)
        return E_INVALIDARG;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width ? (LONG)width : 1;
    info.bmiHeader.biHeight = -(height ? (LONG)height : 1);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(t->dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits)
        return E_OUTOFMEMORY;
    HGDIOBJ old = SelectObject(t->dc, bitmap);
    if (!t->initial)
        t->initial = old;
    if (t->bitmap)
        DeleteObject(t->bitmap);
    t->bitmap = bitmap;
    t->bits = bits;
    t->width = width;
    t->height = height;
    return S_OK;
}

static HRESULT WINAPI target_draw(IDWriteBitmapRenderTarget2 *This, FLOAT origin_x, FLOAT origin_y,
                                  DWRITE_MEASURING_MODE measuring, const DWRITE_GLYPH_RUN *run,
                                  IDWriteRenderingParams *params, COLORREF color, RECT *black_box)
{
    DwTarget *t = (DwTarget *)This;
    if (black_box)
        memset(black_box, 0, sizeof(*black_box));
    if (!run || !run->fontFace || !params)
        return E_INVALIDARG;
    DWRITE_RENDERING_MODE mode = params->lpVtbl->GetRenderingMode(params);
    if (mode == DWRITE_RENDERING_MODE_DEFAULT) {
        HRESULT hr = run->fontFace->lpVtbl->GetRecommendedRenderingMode(run->fontFace, run->fontEmSize, t->pixels_per_dip,
                                                                       measuring, params, &mode);
        if (FAILED(hr))
            return hr;
    }
    IDWriteGlyphRunAnalysis *analysis;
    HRESULT hr = DwGlyphRunAnalysisCreate(run, t->pixels_per_dip, &t->transform, mode, measuring, t->antialias, origin_x,
                                          origin_y, &analysis);
    if (FAILED(hr))
        return hr;
    BOOL cleartype = t->antialias == DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE && mode != DWRITE_RENDERING_MODE_ALIASED;
    DWRITE_TEXTURE_TYPE type = cleartype ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1;
    RECT bounds;
    hr = analysis->lpVtbl->GetAlphaTextureBounds(analysis, type, &bounds);
    LONG w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    if (SUCCEEDED(hr) && w > 0 && h > 0) {
        if (black_box)
            *black_box = bounds;
        UINT32 step = cleartype ? 3 : 1;
        BYTE *texture = DwAlloc((SIZE_T)w * h * step);
        if (!texture)
            hr = E_OUTOFMEMORY;
        else
            hr = analysis->lpVtbl->CreateAlphaTexture(analysis, type, &bounds, texture, (UINT32)(w * h * step));
        UINT32 src[3] = {GetRValue(color), GetGValue(color), GetBValue(color)};
        LONG x0 = bounds.left < 0 ? -bounds.left : 0, y0 = bounds.top < 0 ? -bounds.top : 0;
        LONG x1 = bounds.right > (LONG)t->width ? (LONG)t->width - bounds.left : w;
        LONG y1 = bounds.bottom > (LONG)t->height ? (LONG)t->height - bounds.top : h;
        for (LONG y = y0; SUCCEEDED(hr) && y < y1; y++) {
            const BYTE *c = texture + ((SIZE_T)y * w + x0) * step;
            UINT32 *dst = t->bits + (SIZE_T)(bounds.top + y) * t->width + bounds.left + x0;
            for (LONG x = x0; x < x1; x++, c += step, dst++) {
                UINT32 d = *dst, r = (d >> 16) & 255, g = (d >> 8) & 255, b = d & 255, a = d >> 24;
                UINT32 cr = c[0], cg = cleartype ? c[1] : c[0], cb = cleartype ? c[2] : c[0];
                if (!(cr | cg | cb))
                    continue;
                r = (r * (255 - cr) + src[0] * cr + 127) / 255;
                g = (g * (255 - cg) + src[1] * cg + 127) / 255;
                b = (b * (255 - cb) + src[2] * cb + 127) / 255;
                if (!cleartype)
                    a = (a * (255 - cr) + 255 * cr + 127) / 255;
                *dst = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        DwFree(texture);
    }
    analysis->lpVtbl->Release(analysis);
    return hr;
}

static HDC WINAPI target_dc(IDWriteBitmapRenderTarget2 *This) { return ((DwTarget *)This)->dc; }

static FLOAT WINAPI target_dip(IDWriteBitmapRenderTarget2 *This) { return ((DwTarget *)This)->pixels_per_dip; }

static HRESULT WINAPI target_set_dip(IDWriteBitmapRenderTarget2 *This, FLOAT pixels_per_dip)
{
    if (!(pixels_per_dip > 0))
        return E_INVALIDARG;
    ((DwTarget *)This)->pixels_per_dip = pixels_per_dip;
    return S_OK;
}

static HRESULT WINAPI target_transform(IDWriteBitmapRenderTarget2 *This, DWRITE_MATRIX *transform)
{
    if (!transform)
        return E_INVALIDARG;
    *transform = ((DwTarget *)This)->transform;
    return S_OK;
}

static HRESULT WINAPI target_set_transform(IDWriteBitmapRenderTarget2 *This, const DWRITE_MATRIX *transform)
{
    static const DWRITE_MATRIX identity = {1, 0, 0, 1, 0, 0};
    ((DwTarget *)This)->transform = transform ? *transform : identity;
    return S_OK;
}

static HRESULT WINAPI target_size(IDWriteBitmapRenderTarget2 *This, SIZE *size)
{
    DwTarget *t = (DwTarget *)This;
    if (!size)
        return E_INVALIDARG;
    size->cx = (LONG)t->width;
    size->cy = (LONG)t->height;
    return S_OK;
}

static HRESULT WINAPI target_resize(IDWriteBitmapRenderTarget2 *This, UINT32 width, UINT32 height)
{
    DwTarget *t = (DwTarget *)This;
    if (width == t->width && height == t->height)
        return S_OK;
    return target_set_bitmap(t, width, height);
}

static DWRITE_TEXT_ANTIALIAS_MODE WINAPI target_antialias(IDWriteBitmapRenderTarget2 *This)
{
    return ((DwTarget *)This)->antialias;
}

static HRESULT WINAPI target_set_antialias(IDWriteBitmapRenderTarget2 *This, DWRITE_TEXT_ANTIALIAS_MODE mode)
{
    if (mode != DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE && mode != DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        return E_INVALIDARG;
    ((DwTarget *)This)->antialias = mode;
    return S_OK;
}

static HRESULT WINAPI target_data(IDWriteBitmapRenderTarget2 *This, DWRITE_BITMAP_DATA_BGRA32 *data)
{
    DwTarget *t = (DwTarget *)This;
    if (!data)
        return E_INVALIDARG;
    data->width = t->width;
    data->height = t->height;
    data->pixels = t->bits;
    return S_OK;
}

static const IDWriteBitmapRenderTarget2Vtbl g_target_vtbl = {
    target_query,   target_addref,        target_release, target_draw,   target_dc,        target_dip,
    target_set_dip, target_transform,     target_set_transform, target_size, target_resize, target_antialias,
    target_set_antialias, target_data,
};

static HRESULT WINAPI interop_render_target(IDWriteGdiInterop *This, HDC dc, UINT32 width, UINT32 height,
                                            IDWriteBitmapRenderTarget **target)
{
    if (!target)
        return E_INVALIDARG;
    *target = nullptr;
    DwTarget *t = DwAllocZero(sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->lpVtbl = &g_target_vtbl;
    t->refs = 1;
    t->pixels_per_dip = 1.0f;
    t->transform.m11 = t->transform.m22 = 1.0f;
    t->antialias = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
    t->dc = CreateCompatibleDC(dc);
    HRESULT hr = t->dc ? target_set_bitmap(t, width, height) : E_FAIL;
    if (FAILED(hr)) {
        target_release((IDWriteBitmapRenderTarget2 *)t);
        return hr;
    }
    *target = (IDWriteBitmapRenderTarget *)t;
    return S_OK;
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
