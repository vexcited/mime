/*
 * IDWriteFactory: the object programs start from. Versions one and two are
 * answered by the same object, text layout is not offered.
 */
#include "dw_private.h"

static DwFactory *g_shared;

static HRESULT WINAPI factory_query(IDWriteFactory2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFactory) ||
        IsEqualGUID(riid, &IID_IDWriteFactory1) || IsEqualGUID(riid, &IID_IDWriteFactory2)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
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

static BOOL loader_registered(DwFactory *f, IDWriteFontFileLoader *loader)
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
    if (!loader_registered(f, loader))
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
                           DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE mode, DWRITE_GRID_FIT_MODE grid_fit,
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
    return make_params(2.2f, 1.0f, 1.0f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_DEFAULT,
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
    return make_params(gamma, contrast, contrast, cleartype, geometry, mode, DWRITE_GRID_FIT_MODE_DEFAULT,
                       (void **)params);
}

static HRESULT WINAPI factory_analysis(IDWriteFactory2 *This, const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip,
                                       const DWRITE_MATRIX *transform, DWRITE_RENDERING_MODE mode,
                                       DWRITE_MEASURING_MODE measuring, FLOAT origin_x, FLOAT origin_y,
                                       IDWriteGlyphRunAnalysis **analysis)
{
    return DwGlyphRunAnalysisCreate(run, pixels_per_dip, transform, mode, measuring, origin_x, origin_y, analysis);
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

/* ------------------------------------------------------------------ text layout, which is not offered */

static HRESULT WINAPI factory_text_format(IDWriteFactory2 *This, const WCHAR *family, IDWriteFontCollection *collection,
                                          DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                          DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale,
                                          IDWriteTextFormat **format)
{
    if (format)
        *format = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_typography(IDWriteFactory2 *This, IDWriteTypography **typography)
{
    if (typography)
        *typography = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_text_layout(IDWriteFactory2 *This, const WCHAR *text, UINT32 length,
                                          IDWriteTextFormat *format, FLOAT max_width, FLOAT max_height,
                                          IDWriteTextLayout **layout)
{
    if (layout)
        *layout = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_gdi_text_layout(IDWriteFactory2 *This, const WCHAR *text, UINT32 length,
                                              IDWriteTextFormat *format, FLOAT max_width, FLOAT max_height,
                                              FLOAT pixels_per_dip, const DWRITE_MATRIX *transform, BOOL natural,
                                              IDWriteTextLayout **layout)
{
    if (layout)
        *layout = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ellipsis(IDWriteFactory2 *This, IDWriteTextFormat *format, IDWriteInlineObject **sign)
{
    if (sign)
        *sign = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_analyzer(IDWriteFactory2 *This, IDWriteTextAnalyzer **analyzer)
{
    if (analyzer)
        *analyzer = nullptr;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_number_substitution(IDWriteFactory2 *This, UINT32 method, const WCHAR *locale,
                                                  BOOL ignore_user_override, IDWriteNumberSubstitution **substitution)
{
    if (substitution)
        *substitution = nullptr;
    return E_NOTIMPL;
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
    return make_params(gamma, contrast, grayscale, cleartype, geometry, mode, DWRITE_GRID_FIT_MODE_DEFAULT,
                       (void **)params);
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
    return make_params(gamma, contrast, grayscale, cleartype, geometry, mode, grid_fit, (void **)params);
}

static HRESULT WINAPI factory_analysis2(IDWriteFactory2 *This, const DWRITE_GLYPH_RUN *run,
                                        const DWRITE_MATRIX *transform, DWRITE_RENDERING_MODE mode,
                                        DWRITE_MEASURING_MODE measuring, DWRITE_GRID_FIT_MODE grid_fit,
                                        DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x, FLOAT origin_y,
                                        IDWriteGlyphRunAnalysis **analysis)
{
    return DwGlyphRunAnalysisCreate(run, 1.0f, transform, mode, measuring, origin_x, origin_y, analysis);
}

static const IDWriteFactory2Vtbl g_factory_vtbl = {
    factory_query,
    factory_addref,
    factory_release,
    factory_system_collection,
    factory_custom_collection,
    factory_register_collection_loader,
    factory_unregister_collection_loader,
    factory_file_reference,
    factory_custom_file_reference,
    factory_create_face,
    factory_rendering_params,
    factory_monitor_params,
    factory_custom_params,
    factory_register_file_loader,
    factory_unregister_file_loader,
    factory_text_format,
    factory_typography,
    factory_interop,
    factory_text_layout,
    factory_gdi_text_layout,
    factory_ellipsis,
    factory_analyzer,
    factory_number_substitution,
    factory_analysis,
    factory_eudc,
    factory_custom_params1,
    factory_fallback,
    factory_fallback_builder,
    factory_translate_color,
    factory_custom_params2,
    factory_analysis2,
};

DwFactory *DwFactoryNew(BOOL shared)
{
    DwFactory *f = DwAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->lpVtbl = &g_factory_vtbl;
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
    /* the first to get here keeps it, and the process holds a reference for good. */
    DwFactory *previous = __sync_val_compare_and_swap(&g_shared, nullptr, f);
    if (previous) {
        DwFree(f);
        return previous;
    }
    return f;
}
