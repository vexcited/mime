/*
 * IDWriteFactory: every version, one to eight, is the same object.
 */
#include "text_private.h"

static DwFactory *g_shared;

/* logs unsupported interfaces: a program that wants a newer factory only says so by failing. */
static void log_missing(const char *what, REFIID riid, void *caller)
{
    /* once per interface, since programs probe for newer ones at every font. */
    static GUID seen[16];
    static LONG seen_count;
    LONG count = seen_count;
    for (LONG i = 0; i < count && i < 16; i++)
        if (IsEqualGUID(&seen[i], riid))
            return;
    LONG slot = InterlockedIncrement(&seen_count) - 1;
    if (slot < 16)
        seen[slot] = *riid;

    static const char hex[] = "0123456789abcdef";
    char text[96] = "dwrite: no interface {";
    int n = (int)strlen(text);
    const unsigned char *b = (const unsigned char *)riid;
    /* the first three fields are in machine order, then eight plain bytes. */
    static const char order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text[n++] = '-';
        text[n++] = hex[b[(int)order[i]] >> 4];
        text[n++] = hex[b[(int)order[i]] & 15];
    }
    text[n++] = '}';
    for (const char *w = what; *w && n < (int)sizeof(text) - 3; w++)
        text[n++] = *w;
    /* the caller, as an offset in its module. */
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, caller,
                           &module) && module && n < (int)sizeof(text) - 20) {
        unsigned long long off = (unsigned long long)((char *)caller - (char *)module);
        text[n++] = ' ';
        text[n++] = '+';
        for (int shift = 36; shift >= 0; shift -= 4)
            text[n++] = hex[(off >> shift) & 15];
    }
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

static HRESULT WINAPI factory_query(IDWriteFactory2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFactory) ||
        IsEqualGUID(riid, &IID_IDWriteFactory1) || IsEqualGUID(riid, &IID_IDWriteFactory2) ||
        IsEqualGUID(riid, &IID_IDWriteFactory3) || IsEqualGUID(riid, &IID_IDWriteFactory4) ||
        IsEqualGUID(riid, &IID_IDWriteFactory5) || IsEqualGUID(riid, &IID_IDWriteFactory6) ||
        IsEqualGUID(riid, &IID_IDWriteFactory7) || IsEqualGUID(riid, &IID_IDWriteFactory8)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    log_missing(" of the factory", riid, __builtin_return_address(0));
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_addref(IDWriteFactory2 *This) { return InterlockedIncrement(&((DwFactory *)This)->refs); }

static ULONG WINAPI factory_release(IDWriteFactory2 *This)
{
    DwFactory *f = (DwFactory *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs && !f->shared) {
        if (f->system)
            f->system->lpVtbl->Release(f->system);
        if (f->system_typographic)
            f->system_typographic->lpVtbl->Release(f->system_typographic);
        if (f->system_set)
            f->system_set->lpVtbl->Release(f->system_set);
        if (f->fallback)
            f->fallback->lpVtbl->Release(f->fallback);
        if (f->interop)
            f->interop->lpVtbl->Release(f->interop);
        DwFree(f->file_loaders);
        DwFree(f->collection_loaders);
        DwFree(f);
    }
    return refs;
}

static HRESULT WINAPI factory_system_collection(IDWriteFactory2 *This, IDWriteFontCollection **collection,
                                                BOOL check_for_updates)
{
    if (!collection)
        return E_POINTER;
    return DwSystemCollection((DwFactory *)This, collection);
}

/* ------------------------------------------------------------------ registered loaders */

static BOOL list_has(void *const *list, UINT32 count, const void *item)
{
    for (UINT32 i = 0; i < count; i++) {
        if (list[i] == item)
            return TRUE;
    }
    return FALSE;
}

static HRESULT list_add(void ***list, UINT32 *count, void *item)
{
    void **grown = DwAlloc((*count + 1) * sizeof(void *));
    if (!grown)
        return E_OUTOFMEMORY;
    if (*count)
        memcpy(grown, *list, *count * sizeof(void *));
    grown[(*count)++] = item;
    DwFree(*list);
    *list = grown;
    return S_OK;
}

static HRESULT list_remove(void **list, UINT32 *count, const void *item)
{
    for (UINT32 i = 0; i < *count; i++) {
        if (list[i] == item) {
            memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof(void *));
            (*count)--;
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI factory_register_collection_loader(IDWriteFactory2 *This, IDWriteFontCollectionLoader *loader)
{
    DwFactory *f = (DwFactory *)This;
    if (!loader)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr;
    if (list_has((void *const *)f->collection_loaders, f->collection_loader_count, loader))
        hr = DWRITE_E_ALREADYREGISTERED;
    else
        hr = list_add((void ***)&f->collection_loaders, &f->collection_loader_count, loader);
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

static HRESULT WINAPI factory_unregister_collection_loader(IDWriteFactory2 *This, IDWriteFontCollectionLoader *loader)
{
    DwFactory *f = (DwFactory *)This;
    if (!loader)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr = list_remove((void **)f->collection_loaders, &f->collection_loader_count, loader);
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

static HRESULT WINAPI factory_register_file_loader(IDWriteFactory2 *This, IDWriteFontFileLoader *loader)
{
    DwFactory *f = (DwFactory *)This;
    if (!loader)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr;
    if (loader == (IDWriteFontFileLoader *)DwLocalLoader() ||
        list_has((void *const *)f->file_loaders, f->file_loader_count, loader))
        hr = DWRITE_E_ALREADYREGISTERED;
    else
        hr = list_add((void ***)&f->file_loaders, &f->file_loader_count, loader);
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

static HRESULT WINAPI factory_unregister_file_loader(IDWriteFactory2 *This, IDWriteFontFileLoader *loader)
{
    DwFactory *f = (DwFactory *)This;
    if (!loader)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr = list_remove((void **)f->file_loaders, &f->file_loader_count, loader);
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

BOOL DwLoaderRegistered(DwFactory *f, IDWriteFontFileLoader *loader)
{
    if (loader == (IDWriteFontFileLoader *)DwLocalLoader())
        return TRUE;
    AcquireSRWLockShared(&f->lock);
    BOOL found = list_has((void *const *)f->file_loaders, f->file_loader_count, loader);
    ReleaseSRWLockShared(&f->lock);
    return found;
}

/* ------------------------------------------------------------------ collections, files and faces */

static HRESULT WINAPI factory_custom_collection(IDWriteFactory2 *This, IDWriteFontCollectionLoader *loader,
                                                const void *key, UINT32 size, IDWriteFontCollection **collection)
{
    DwFactory *f = (DwFactory *)This;
    if (!collection)
        return E_POINTER;
    *collection = nullptr;
    if (!loader || (size && !key))
        return E_INVALIDARG;
    AcquireSRWLockShared(&f->lock);
    BOOL registered = list_has((void *const *)f->collection_loaders, f->collection_loader_count, loader);
    ReleaseSRWLockShared(&f->lock);
    if (!registered)
        return E_INVALIDARG;
    IDWriteFontFileEnumerator *enumerator;
    HRESULT hr = loader->lpVtbl->CreateEnumeratorFromKey(loader, (IDWriteFactory *)This, key, size, &enumerator);
    if (FAILED(hr))
        return hr;
    DwCollection *c;
    hr = DwCollectionFromFiles(f, nullptr, 0, enumerator, &c);
    enumerator->lpVtbl->Release(enumerator);
    if (SUCCEEDED(hr))
        *collection = (IDWriteFontCollection *)c;
    return hr;
}

static HRESULT WINAPI factory_file_reference(IDWriteFactory2 *This, const WCHAR *path, const FILETIME *write_time,
                                             IDWriteFontFile **file)
{
    if (!file)
        return E_POINTER;
    *file = nullptr;
    if (!path)
        return E_INVALIDARG;
    return DwFontFileFromPath(path, write_time, file);
}

static HRESULT WINAPI factory_custom_file_reference(IDWriteFactory2 *This, const void *key, UINT32 size,
                                                    IDWriteFontFileLoader *loader, IDWriteFontFile **file)
{
    DwFactory *f = (DwFactory *)This;
    if (!file)
        return E_POINTER;
    *file = nullptr;
    if (!loader || (size && !key))
        return E_INVALIDARG;
    if (!DwLoaderRegistered(f, loader))
        return E_INVALIDARG;
    return DwFontFileCreate(loader, key, size, file);
}

static HRESULT WINAPI factory_create_face(IDWriteFactory2 *This, DWRITE_FONT_FACE_TYPE type, UINT32 count,
                                          IDWriteFontFile *const *files, UINT32 index,
                                          DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace **face)
{
    if (!face)
        return E_POINTER;
    *face = nullptr;
    if (!files || count != 1 || !files[0] || (unsigned)type > DWRITE_FONT_FACE_TYPE_RAW_CFF)
        return E_INVALIDARG;
    return DwFaceCreate((DwFactory *)This, files[0], type, index, simulations, face);
}

/* ------------------------------------------------------------------ rendering parameters and analysis */

static HRESULT make_params(FLOAT gamma, FLOAT contrast, FLOAT grayscale, FLOAT cleartype,
                           DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE1 mode, DWRITE_GRID_FIT_MODE grid_fit,
                           void **out)
{
    DwRenderingParams *p;
    HRESULT hr = DwRenderingParamsCreate(gamma, contrast, grayscale, cleartype, geometry, mode, grid_fit, &p);
    *out = SUCCEEDED(hr) ? p : nullptr;
    return hr;
}

static HRESULT WINAPI factory_rendering_params(IDWriteFactory2 *This, IDWriteRenderingParams **params)
{
    if (!params)
        return E_POINTER;
    /* what Windows answers on a display nobody tuned ClearType for. */
    return make_params(1.8f, 0.5f, 1.0f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE1_DEFAULT,
                       DWRITE_GRID_FIT_MODE_DEFAULT, (void **)params);
}

static HRESULT WINAPI factory_monitor_params(IDWriteFactory2 *This, HANDLE monitor, IDWriteRenderingParams **params)
{
    return factory_rendering_params(This, params);
}

static HRESULT WINAPI factory_custom_params(IDWriteFactory2 *This, FLOAT gamma, FLOAT contrast, FLOAT cleartype,
                                            DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE mode,
                                            IDWriteRenderingParams **params)
{
    if (!params)
        return E_POINTER;
    if ((unsigned)mode > DWRITE_RENDERING_MODE_OUTLINE)
        return E_INVALIDARG;
    return make_params(gamma, contrast, contrast, cleartype, geometry, (DWRITE_RENDERING_MODE1)mode,
                       DWRITE_GRID_FIT_MODE_DEFAULT, (void **)params);
}

static HRESULT WINAPI factory_analysis(IDWriteFactory2 *This, const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip,
                                       const DWRITE_MATRIX *transform, DWRITE_RENDERING_MODE mode,
                                       DWRITE_MEASURING_MODE measuring, FLOAT origin_x, FLOAT origin_y,
                                       IDWriteGlyphRunAnalysis **analysis)
{
    return DwGlyphRunAnalysisCreate(run, pixels_per_dip, transform, mode, measuring,
                                    DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE, origin_x, origin_y, analysis);
}

static HRESULT WINAPI factory_interop(IDWriteFactory2 *This, IDWriteGdiInterop **interop)
{
    DwFactory *f = (DwFactory *)This;
    if (!interop)
        return E_POINTER;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr = S_OK;
    if (!f->interop)
        hr = DwInteropCreate(f, &f->interop);
    if (SUCCEEDED(hr)) {
        *interop = f->interop;
        (*interop)->lpVtbl->AddRef(*interop);
    } else {
        *interop = nullptr;
    }
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

/* ------------------------------------------------------------------ text layout, not offered */

static HRESULT WINAPI factory_text_format(IDWriteFactory2 *This, const WCHAR *family, IDWriteFontCollection *collection,
                                          DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                          DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale,
                                          IDWriteTextFormat **format)
{
    return DwTextFormatCreate((DwFactory *)This, family, collection, weight, style, stretch, size, locale, format);
}

static HRESULT WINAPI factory_typography(IDWriteFactory2 *This, IDWriteTypography **typography)
{
    if (!typography)
        return E_POINTER;
    return DwTypographyCreate(typography);
}

static HRESULT WINAPI factory_text_layout(IDWriteFactory2 *This, const WCHAR *text, UINT32 length,
                                          IDWriteTextFormat *format, FLOAT max_width, FLOAT max_height,
                                          IDWriteTextLayout **layout)
{
    return DwTextLayoutCreate((DwFactory *)This, text, length, format, max_width, max_height, 1.0f, nullptr, TRUE, FALSE,
                              layout);
}

static HRESULT WINAPI factory_gdi_text_layout(IDWriteFactory2 *This, const WCHAR *text, UINT32 length,
                                              IDWriteTextFormat *format, FLOAT max_width, FLOAT max_height,
                                              FLOAT pixels_per_dip, const DWRITE_MATRIX *transform, BOOL natural,
                                              IDWriteTextLayout **layout)
{
    return DwTextLayoutCreate((DwFactory *)This, text, length, format, max_width, max_height, pixels_per_dip, transform,
                              natural, TRUE, layout);
}

static HRESULT WINAPI factory_ellipsis(IDWriteFactory2 *This, IDWriteTextFormat *format, IDWriteInlineObject **sign)
{
    return DwEllipsisSignCreate((DwFactory *)This, format, sign);
}

static HRESULT WINAPI factory_analyzer(IDWriteFactory2 *This, IDWriteTextAnalyzer **analyzer)
{
    if (!analyzer)
        return E_POINTER;
    return DwAnalyzerCreate((DwFactory *)This, analyzer);
}

static HRESULT WINAPI factory_number_substitution(IDWriteFactory2 *This, UINT32 method, const WCHAR *locale,
                                                  BOOL ignore_user_override, IDWriteNumberSubstitution **substitution)
{
    if (!substitution)
        return E_POINTER;
    *substitution = nullptr;
    if (method > DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL || (method != DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE && !locale))
        return E_INVALIDARG;
    return DwNumberSubstitutionCreate((DWRITE_NUMBER_SUBSTITUTION_METHOD)method, locale, ignore_user_override, substitution);
}

/* ------------------------------------------------------------------ versions one and two */

static HRESULT WINAPI factory_eudc(IDWriteFactory2 *This, IDWriteFontCollection **collection, BOOL check)
{
    DwFactory *f = (DwFactory *)This;
    if (!collection)
        return E_POINTER;
    *collection = nullptr;
    DwCollection *c;
    HRESULT hr = DwCollectionFromFiles(f, nullptr, 0, nullptr, &c);
    if (SUCCEEDED(hr))
        *collection = (IDWriteFontCollection *)c;
    return hr;
}

static HRESULT WINAPI factory_custom_params1(IDWriteFactory2 *This, FLOAT gamma, FLOAT contrast, FLOAT grayscale,
                                             FLOAT cleartype, DWRITE_PIXEL_GEOMETRY geometry,
                                             DWRITE_RENDERING_MODE mode, IDWriteRenderingParams1 **params)
{
    if (!params)
        return E_POINTER;
    if ((unsigned)mode > DWRITE_RENDERING_MODE_OUTLINE)
        return E_INVALIDARG;
    return make_params(gamma, contrast, grayscale, cleartype, geometry, (DWRITE_RENDERING_MODE1)mode,
                       DWRITE_GRID_FIT_MODE_DEFAULT, (void **)params);
}

static HRESULT WINAPI factory_fallback(IDWriteFactory2 *This, IDWriteFontFallback **fallback)
{
    DwFactory *f = (DwFactory *)This;
    if (!fallback)
        return E_POINTER;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr = S_OK;
    if (!f->fallback)
        hr = DwFallbackCreate(f, &f->fallback);
    if (SUCCEEDED(hr)) {
        *fallback = f->fallback;
        (*fallback)->lpVtbl->AddRef(*fallback);
    } else {
        *fallback = nullptr;
    }
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

static HRESULT WINAPI factory_fallback_builder(IDWriteFactory2 *This, IDWriteFontFallbackBuilder **builder)
{
    if (builder)
        *builder = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_translate_color(IDWriteFactory2 *This, FLOAT origin_x, FLOAT origin_y,
                                              const DWRITE_GLYPH_RUN *run,
                                              const DWRITE_GLYPH_RUN_DESCRIPTION *description,
                                              DWRITE_MEASURING_MODE measuring, const DWRITE_MATRIX *transform,
                                              UINT32 palette, IDWriteColorGlyphRunEnumerator **layers)
{
    if (layers)
        *layers = nullptr;
    return DWRITE_E_NOCOLOR;
}

static HRESULT WINAPI factory_custom_params2(IDWriteFactory2 *This, FLOAT gamma, FLOAT contrast, FLOAT grayscale,
                                             FLOAT cleartype, DWRITE_PIXEL_GEOMETRY geometry,
                                             DWRITE_RENDERING_MODE mode, DWRITE_GRID_FIT_MODE grid_fit,
                                             IDWriteRenderingParams2 **params)
{
    if (!params)
        return E_POINTER;
    if ((unsigned)mode > DWRITE_RENDERING_MODE_OUTLINE)
        return E_INVALIDARG;
    return make_params(gamma, contrast, grayscale, cleartype, geometry, (DWRITE_RENDERING_MODE1)mode, grid_fit,
                       (void **)params);
}

static HRESULT WINAPI factory_analysis2(IDWriteFactory2 *This, const DWRITE_GLYPH_RUN *run,
                                        const DWRITE_MATRIX *transform, DWRITE_RENDERING_MODE mode,
                                        DWRITE_MEASURING_MODE measuring, DWRITE_GRID_FIT_MODE grid_fit,
                                        DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x, FLOAT origin_y,
                                        IDWriteGlyphRunAnalysis **analysis)
{
    return DwGlyphRunAnalysisCreate(run, 1.0f, transform, mode, measuring, antialias, origin_x, origin_y, analysis);
}

static HRESULT WINAPI factory3_analysis(IDWriteFactory3 *This, const DWRITE_GLYPH_RUN *run, const DWRITE_MATRIX *transform,
                                        DWRITE_RENDERING_MODE1 mode, DWRITE_MEASURING_MODE measuring,
                                        DWRITE_GRID_FIT_MODE grid_fit, DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT x,
                                        FLOAT y, IDWriteGlyphRunAnalysis **analysis)
{
    /* map to the old modes: outline and the downsampled one are natural to the rasterizer anyway. */
    DWRITE_RENDERING_MODE old = mode == DWRITE_RENDERING_MODE1_OUTLINE ? DWRITE_RENDERING_MODE_OUTLINE
                                : mode == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED
                                    ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC
                                    : (DWRITE_RENDERING_MODE)mode;
    return factory_analysis2((IDWriteFactory2 *)This, run, transform, old, measuring, grid_fit, antialias, x, y, analysis);
}

static HRESULT WINAPI factory3_params(IDWriteFactory3 *This, FLOAT gamma, FLOAT contrast, FLOAT grayscale,
                                      FLOAT cleartype, DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE1 mode,
                                      DWRITE_GRID_FIT_MODE grid_fit, IDWriteRenderingParams3 **params)
{
    if (!params)
        return E_POINTER;
    return make_params(gamma, contrast, grayscale, cleartype, geometry, mode, grid_fit, (void **)params);
}

static HRESULT face_reference(DwFactory *f, IDWriteFontFile *file, UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                              const DWRITE_FONT_AXIS_VALUE *axes, UINT32 axis_count, IDWriteFontFaceReference1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!file || (axis_count && !axes))
        return E_INVALIDARG;
    DwFontKey key = {file, index, simulations};
    key.axis_count = axis_count < DW_MAX_AXES ? axis_count : DW_MAX_AXES;
    if (key.axis_count)
        memcpy(key.axes, axes, key.axis_count * sizeof(*axes));
    return DwFaceReferenceCreate(f, &key, out);
}

static HRESULT WINAPI factory3_face_reference(IDWriteFactory3 *This, IDWriteFontFile *file, UINT32 index,
                                              DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFaceReference **reference)
{
    return face_reference((DwFactory *)This, file, index, simulations, nullptr, 0,
                          (IDWriteFontFaceReference1 **)reference);
}

static HRESULT WINAPI factory3_face_reference_path(IDWriteFactory3 *This, const WCHAR *path, const FILETIME *time,
                                                   UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                                                   IDWriteFontFaceReference **reference)
{
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    if (!path)
        return E_INVALIDARG;
    IDWriteFontFile *file;
    HRESULT hr = DwFontFileFromPath(path, time, &file);
    if (FAILED(hr))
        return hr;
    hr = face_reference((DwFactory *)This, file, index, simulations, nullptr, 0, (IDWriteFontFaceReference1 **)reference);
    file->lpVtbl->Release(file);
    return hr;
}

static HRESULT WINAPI factory3_system_set(IDWriteFactory3 *This, IDWriteFontSet **set)
{
    if (!set)
        return E_POINTER;
    return DwSystemFontSet((DwFactory *)This, (IDWriteFontSet4 **)set);
}

static HRESULT WINAPI factory3_set_builder(IDWriteFactory3 *This, IDWriteFontSetBuilder **builder)
{
    return DwFontSetBuilderCreate((DwFactory *)This, (IDWriteFontSetBuilder2 **)builder);
}

static HRESULT collection_from_set(DwFactory *f, IDWriteFontSet *set, DWRITE_FONT_FAMILY_MODEL model, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!set || (model != DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC && model != DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE))
        return E_INVALIDARG;
    DwCollection *c;
    HRESULT hr = DwCollectionFromSet(f, set, model, &c);
    if (SUCCEEDED(hr))
        *out = c;
    return hr;
}

static HRESULT WINAPI factory3_collection_from_set(IDWriteFactory3 *This, IDWriteFontSet *set,
                                                   IDWriteFontCollection1 **collection)
{
    return collection_from_set((DwFactory *)This, set, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE,
                               (void **)collection);
}

static HRESULT WINAPI factory3_system_collection(IDWriteFactory3 *This, BOOL downloadable,
                                                 IDWriteFontCollection1 **collection, BOOL check)
{
    if (!collection)
        return E_POINTER;
    return DwSystemCollection((DwFactory *)This, (IDWriteFontCollection **)collection);
}

static HRESULT WINAPI factory3_download_queue(IDWriteFactory3 *This, IDWriteFontDownloadQueue **queue)
{
    if (!queue)
        return E_POINTER;
    return DwFontDownloadQueueCreate(queue);
}

/* Mime's fonts have no color layers to translate. */
static HRESULT WINAPI factory4_translate(IDWriteFactory3 *This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION *description,
                                         DWRITE_GLYPH_IMAGE_FORMATS formats, DWRITE_MEASURING_MODE measuring,
                                         const DWRITE_MATRIX *transform, UINT32 palette, IUnknown **layers)
{
    if (!layers)
        return E_POINTER;
    *layers = nullptr;
    if (!run)
        return E_INVALIDARG;
    return DWRITE_E_NOCOLOR;
}

/* each glyph's origin: the pen moves by the advances, against the line in a right to left run. */
static HRESULT glyph_origins(const DWRITE_GLYPH_RUN *run, D2D1_POINT_2F origin, D2D1_POINT_2F *origins)
{
    if (!run || (run->glyphCount && !origins))
        return E_INVALIDARG;
    FLOAT direction = (run->bidiLevel & 1) ? -1.0f : 1.0f;
    FLOAT pen = 0;
    DWRITE_FONT_METRICS metrics = {};
    if (run->isSideways && run->fontFace)
        run->fontFace->lpVtbl->GetMetrics(run->fontFace, &metrics);
    for (UINT32 i = 0; i < run->glyphCount; i++) {
        FLOAT advance = 0;
        if (run->glyphAdvances) {
            advance = run->glyphAdvances[i];
        } else if (run->fontFace && run->glyphIndices) {
            DWRITE_GLYPH_METRICS gm;
            if (SUCCEEDED(run->fontFace->lpVtbl->GetDesignGlyphMetrics(run->fontFace, &run->glyphIndices[i], 1, &gm,
                                                                       run->isSideways))) {
                UINT32 design = run->isSideways ? gm.advanceHeight : gm.advanceWidth;
                DWRITE_FONT_METRICS fm;
                run->fontFace->lpVtbl->GetMetrics(run->fontFace, &fm);
                advance = fm.designUnitsPerEm ? (FLOAT)design * run->fontEmSize / fm.designUnitsPerEm : 0;
            }
        }
        FLOAT along = 0, across = 0;
        if (run->glyphOffsets) {
            along = run->glyphOffsets[i].advanceOffset;
            across = run->glyphOffsets[i].ascenderOffset;
        }
        /* a right to left glyph is drawn from the left end of its advance. */
        FLOAT start = direction < 0 ? pen + advance : pen;
        if (run->isSideways) {
            origins[i].x = origin.x + across;
            origins[i].y = origin.y + direction * (start + along);
        } else {
            origins[i].x = origin.x + direction * (start + along);
            origins[i].y = origin.y - across;
        }
        pen += advance;
    }
    return S_OK;
}

static HRESULT WINAPI factory4_origins(IDWriteFactory3 *This, const DWRITE_GLYPH_RUN *run, D2D1_POINT_2F origin,
                                       D2D1_POINT_2F *origins)
{
    return glyph_origins(run, origin, origins);
}

static HRESULT WINAPI factory4_origins_transformed(IDWriteFactory3 *This, const DWRITE_GLYPH_RUN *run,
                                                   DWRITE_MEASURING_MODE measuring, D2D1_POINT_2F origin,
                                                   const DWRITE_MATRIX *transform, D2D1_POINT_2F *origins)
{
    return glyph_origins(run, origin, origins);
}

static HRESULT WINAPI factory5_set_builder(IDWriteFactory3 *This, IDWriteFontSetBuilder1 **builder)
{
    return DwFontSetBuilderCreate((DwFactory *)This, (IDWriteFontSetBuilder2 **)builder);
}

static HRESULT WINAPI factory5_memory_loader(IDWriteFactory3 *This, IDWriteInMemoryFontFileLoader **loader)
{
    return DwInMemoryLoaderCreate(loader);
}

/* Mime fetches no fonts from the web: remote fonts are refused as the system refuses them offline. */
static HRESULT WINAPI factory5_http_loader(IDWriteFactory3 *This, const WCHAR *referrer, const WCHAR *headers,
                                           IDWriteRemoteFontFileLoader **loader)
{
    if (!loader)
        return E_POINTER;
    *loader = nullptr;
    return E_NOTIMPL;
}

static DWRITE_CONTAINER_TYPE WINAPI factory5_container(IDWriteFactory3 *This, const void *data, UINT32 size)
{
    if (!data || size < 4)
        return DWRITE_CONTAINER_TYPE_UNKNOWN;
    UINT32 tag = DwBe32(data);
    if (tag == DW_TAG('w', 'O', 'F', 'F'))
        return DWRITE_CONTAINER_TYPE_WOFF;
    if (tag == DW_TAG('w', 'O', 'F', '2'))
        return DWRITE_CONTAINER_TYPE_WOFF2;
    return DWRITE_CONTAINER_TYPE_UNKNOWN;
}

static HRESULT WINAPI factory5_unpack(IDWriteFactory3 *This, DWRITE_CONTAINER_TYPE container, const void *data,
                                      UINT32 size, IDWriteFontFileStream **stream)
{
    if (!stream)
        return E_POINTER;
    *stream = nullptr;
    if (!data && size)
        return E_INVALIDARG;
    if (container != factory5_container(This, data, size))
        return DWRITE_E_FILEFORMAT;
    if (container == DWRITE_CONTAINER_TYPE_WOFF)
        return DwUnpackWoff(data, size, stream);
    /* woff2's brotli streams have no decoder in Mime yet. */
    return DWRITE_E_FILEFORMAT;
}

static HRESULT WINAPI factory6_face_reference(IDWriteFactory3 *This, IDWriteFontFile *file, UINT32 index,
                                              DWRITE_FONT_SIMULATIONS simulations, const DWRITE_FONT_AXIS_VALUE *axes,
                                              UINT32 count, IDWriteFontFaceReference1 **reference)
{
    return face_reference((DwFactory *)This, file, index, simulations, axes, count, reference);
}

static HRESULT WINAPI factory6_resource(IDWriteFactory3 *This, IDWriteFontFile *file, UINT32 index,
                                        IDWriteFontResource **resource)
{
    return DwFontResourceCreate((DwFactory *)This, file, index, resource);
}

static HRESULT WINAPI factory6_system_set(IDWriteFactory3 *This, BOOL downloadable, IDWriteFontSet1 **set)
{
    if (!set)
        return E_POINTER;
    return DwSystemFontSet((DwFactory *)This, (IDWriteFontSet4 **)set);
}

static HRESULT WINAPI factory6_system_collection(IDWriteFactory3 *This, BOOL downloadable,
                                                 DWRITE_FONT_FAMILY_MODEL model, IDWriteFontCollection2 **collection)
{
    if (!collection)
        return E_POINTER;
    *collection = nullptr;
    if (model != DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC && model != DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE)
        return E_INVALIDARG;
    return DwSystemCollectionModel((DwFactory *)This, model, (IDWriteFontCollection **)collection);
}

static HRESULT WINAPI factory6_collection_from_set(IDWriteFactory3 *This, IDWriteFontSet *set,
                                                   DWRITE_FONT_FAMILY_MODEL model, IDWriteFontCollection2 **collection)
{
    return collection_from_set((DwFactory *)This, set, model, (void **)collection);
}

static HRESULT WINAPI factory6_set_builder(IDWriteFactory3 *This, IDWriteFontSetBuilder2 **builder)
{
    return DwFontSetBuilderCreate((DwFactory *)This, builder);
}

/* the weight, stretch and style the axis values stand for choose the font; the format keeps the values. */
static HRESULT WINAPI factory6_text_format(IDWriteFactory3 *This, const WCHAR *family, IDWriteFontCollection *collection,
                                           const DWRITE_FONT_AXIS_VALUE *axes, UINT32 count, FLOAT size,
                                           const WCHAR *locale, IDWriteTextFormat3 **format)
{
    if (!format)
        return E_POINTER;
    *format = nullptr;
    if (count && !axes)
        return E_INVALIDARG;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DwAxesToAttributes(axes, count, &weight, &stretch, &style);
    IDWriteTextFormat *made;
    HRESULT hr = DwTextFormatCreate((DwFactory *)This, family, collection, weight, style, stretch, size, locale, &made);
    if (FAILED(hr))
        return hr;
    IDWriteTextFormat3 *three;
    hr = made->lpVtbl->QueryInterface(made, &IID_IDWriteTextFormat3, (void **)&three);
    made->lpVtbl->Release(made);
    if (FAILED(hr))
        return hr;
    if (count) {
        hr = three->lpVtbl->SetFontAxisValues(three, axes, count);
        if (FAILED(hr)) {
            three->lpVtbl->Release(three);
            return hr;
        }
    }
    *format = three;
    return S_OK;
}

static HRESULT WINAPI factory7_system_set(IDWriteFactory3 *This, BOOL downloadable, IDWriteFontSet2 **set)
{
    return factory6_system_set(This, downloadable, (IDWriteFontSet1 **)set);
}

static HRESULT WINAPI factory7_system_collection(IDWriteFactory3 *This, BOOL downloadable,
                                                 DWRITE_FONT_FAMILY_MODEL model, IDWriteFontCollection3 **collection)
{
    return factory6_system_collection(This, downloadable, model, (IDWriteFontCollection2 **)collection);
}

static HRESULT WINAPI factory8_translate(IDWriteFactory3 *This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION *description,
                                         DWRITE_GLYPH_IMAGE_FORMATS formats, DWRITE_PAINT_FEATURE_LEVEL level,
                                         DWRITE_MEASURING_MODE measuring, const DWRITE_MATRIX *transform,
                                         UINT32 palette, IUnknown **layers)
{
    return factory4_translate(This, origin, run, description, formats, measuring, transform, palette, layers);
}

static const IDWriteFactory8Vtbl g_factory_vtbl = {
    VTBL_FN(factory_query),
    VTBL_FN(factory_addref),
    VTBL_FN(factory_release),
    VTBL_FN(factory_system_collection),
    VTBL_FN(factory_custom_collection),
    VTBL_FN(factory_register_collection_loader),
    VTBL_FN(factory_unregister_collection_loader),
    VTBL_FN(factory_file_reference),
    VTBL_FN(factory_custom_file_reference),
    VTBL_FN(factory_create_face),
    VTBL_FN(factory_rendering_params),
    VTBL_FN(factory_monitor_params),
    VTBL_FN(factory_custom_params),
    VTBL_FN(factory_register_file_loader),
    VTBL_FN(factory_unregister_file_loader),
    VTBL_FN(factory_text_format),
    VTBL_FN(factory_typography),
    VTBL_FN(factory_interop),
    VTBL_FN(factory_text_layout),
    VTBL_FN(factory_gdi_text_layout),
    VTBL_FN(factory_ellipsis),
    VTBL_FN(factory_analyzer),
    VTBL_FN(factory_number_substitution),
    VTBL_FN(factory_analysis),
    VTBL_FN(factory_eudc),
    VTBL_FN(factory_custom_params1),
    VTBL_FN(factory_fallback),
    VTBL_FN(factory_fallback_builder),
    VTBL_FN(factory_translate_color),
    VTBL_FN(factory_custom_params2),
    VTBL_FN(factory_analysis2),
    VTBL_FN(factory3_analysis),
    VTBL_FN(factory3_params),
    VTBL_FN(factory3_face_reference),
    VTBL_FN(factory3_face_reference_path),
    VTBL_FN(factory3_system_set),
    VTBL_FN(factory3_set_builder),
    VTBL_FN(factory3_collection_from_set),
    VTBL_FN(factory3_system_collection),
    VTBL_FN(factory3_download_queue),
    VTBL_FN(factory4_translate),
    VTBL_FN(factory4_origins),
    VTBL_FN(factory4_origins_transformed),
    VTBL_FN(factory5_set_builder),
    VTBL_FN(factory5_memory_loader),
    VTBL_FN(factory5_http_loader),
    VTBL_FN(factory5_container),
    VTBL_FN(factory5_unpack),
    VTBL_FN(factory6_face_reference),
    VTBL_FN(factory6_resource),
    VTBL_FN(factory6_system_set),
    VTBL_FN(factory6_system_collection),
    VTBL_FN(factory6_collection_from_set),
    VTBL_FN(factory6_set_builder),
    VTBL_FN(factory6_text_format),
    VTBL_FN(factory7_system_set),
    VTBL_FN(factory7_system_collection),
    VTBL_FN(factory8_translate),
};

DwFactory *DwFactoryNew(BOOL shared)
{
    DwFactory *f = DwAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->lpVtbl = (const IDWriteFactory2Vtbl *)&g_factory_vtbl;
    f->refs = 1;
    f->shared = shared;
    InitializeSRWLock(&f->lock);
    return f;
}

DwFactory *DwFactoryShared(void)
{
    if (g_shared)
        return g_shared;
    DwFactory *f = DwFactoryNew(TRUE);
    if (!f)
        return nullptr;
    /* the first thread here wins, and the process keeps its reference for good. */
    DwFactory *previous = __sync_val_compare_and_swap(&g_shared, nullptr, f);
    if (previous) {
        DwFree(f);
        return previous;
    }
    return f;
}
