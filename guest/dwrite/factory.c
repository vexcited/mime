/*
 * IDWriteFactory: the object programs start from. Versions one and two are
 * answered by the same object, text layout is not offered.
 */
#include "dw_private.h"

static DwFactory *g_shared;

/* the interface asked for, to the debug output: a program that wants a newer factory says so by failing. */
static void log_missing(const char *what, REFIID riid, void *caller)
{
    /* once for each interface: a program that probes for newer ones does so at every font. */
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
    /* the guid as it is written: three fields of the machine's order, then eight bytes. */
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
    /* who asked, as the offset in its module. */
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
        IsEqualGUID(riid, &IID_IDWriteFactory3)) {
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
    return DwGlyphRunAnalysisCreate(run, 1.0f, transform, mode, measuring, antialias, origin_x, origin_y, analysis);
}

static HRESULT WINAPI factory3_missing(const char *name)
{
    static const char *seen[16];
    static LONG seen_count;
    LONG count = seen_count;
    for (LONG i = 0; i < count && i < 16; i++)
        if (seen[i] == name)
            return E_NOTIMPL;
    LONG slot = InterlockedIncrement(&seen_count) - 1;
    if (slot < 16)
        seen[slot] = name;
    OutputDebugStringA("dwrite: factory3 ");
    OutputDebugStringA(name);
    OutputDebugStringA("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI factory3_analysis(IDWriteFactory3 *This, const DWRITE_GLYPH_RUN *run, const DWRITE_MATRIX *transform,
                                        DWRITE_RENDERING_MODE1 mode, DWRITE_MEASURING_MODE measuring,
                                        DWRITE_GRID_FIT_MODE grid_fit, DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT x,
                                        FLOAT y, IDWriteGlyphRunAnalysis **analysis)
{
    /* the old rendering modes, which the new one extends: outline and the downsampled one are natural to the rasterizer. */
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
    return factory3_missing("CreateCustomRenderingParams3");
}

static HRESULT WINAPI factory3_face_reference(IDWriteFactory3 *This, IDWriteFontFile *file, UINT32 index,
                                              DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFaceReference **reference)
{
    return factory3_missing("CreateFontFaceReference");
}

static HRESULT WINAPI factory3_face_reference_path(IDWriteFactory3 *This, const WCHAR *path, const FILETIME *time,
                                                   UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                                                   IDWriteFontFaceReference **reference)
{
    return factory3_missing("CreateFontFaceReference(path)");
}

static HRESULT WINAPI factory3_system_set(IDWriteFactory3 *This, IDWriteFontSet **set)
{
    return factory3_missing("GetSystemFontSet");
}

static HRESULT WINAPI factory3_set_builder(IDWriteFactory3 *This, IDWriteFontSetBuilder **builder)
{
    return factory3_missing("CreateFontSetBuilder");
}

static HRESULT WINAPI factory3_collection_from_set(IDWriteFactory3 *This, IDWriteFontSet *set,
                                                   IDWriteFontCollection1 **collection)
{
    return factory3_missing("CreateFontCollectionFromFontSet");
}

static HRESULT WINAPI factory3_system_collection(IDWriteFactory3 *This, BOOL downloadable,
                                                 IDWriteFontCollection1 **collection, BOOL check)
{
    return factory3_missing("GetSystemFontCollection(1)");
}

static HRESULT WINAPI factory3_download_queue(IDWriteFactory3 *This, IDWriteFontDownloadQueue **queue)
{
    return factory3_missing("GetFontDownloadQueue");
}

/* the table of the second factory with the nine methods of the third after it: both are one object. */
static const struct {
    IDWriteFactory2Vtbl base;
    void *third[9];
} g_factory_vtbl = {{
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
}, {
    (void *)factory3_analysis,
    (void *)factory3_params,
    (void *)factory3_face_reference,
    (void *)factory3_face_reference_path,
    (void *)factory3_system_set,
    (void *)factory3_set_builder,
    (void *)factory3_collection_from_set,
    (void *)factory3_system_collection,
    (void *)factory3_download_queue,
}};

DwFactory *DwFactoryNew(BOOL shared)
{
    DwFactory *f = DwAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->lpVtbl = &g_factory_vtbl.base;
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
