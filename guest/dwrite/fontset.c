#include "dw_private.h"

static BOOL axes_equal(const DwFontKey *a, const DwFontKey *b)
{
    if (a->axis_count != b->axis_count)
        return FALSE;
    for (UINT32 i = 0; i < a->axis_count; i++) {
        if (a->axes[i].axisTag != b->axes[i].axisTag || a->axes[i].value != b->axes[i].value)
            return FALSE;
    }
    return TRUE;
}

static BOOL keys_equal(const DwFontKey *a, const DwFontKey *b)
{
    return a->index == b->index && a->simulations == b->simulations && axes_equal(a, b) && DwSameFile(a->file, b->file);
}

static void copy_axes(DwFontKey *key, const DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    key->axis_count = count < DW_MAX_AXES ? count : DW_MAX_AXES;
    if (key->axis_count)
        memcpy(key->axes, values, key->axis_count * sizeof(*values));
}

/* ------------------------------------------------------------------ axes and attributes */

static const FLOAT g_widths[9] = {50.0f, 62.5f, 75.0f, 87.5f, 100.0f, 112.5f, 125.0f, 150.0f, 200.0f};

UINT32 DwAttributeAxes(const DwAttributes *a, DWRITE_FONT_AXIS_VALUE *values)
{
    values[0].axisTag = DWRITE_FONT_AXIS_TAG_WEIGHT;
    values[0].value = (FLOAT)a->weight;
    values[1].axisTag = DWRITE_FONT_AXIS_TAG_WIDTH;
    values[1].value = a->stretch >= 1 && a->stretch <= 9 ? g_widths[a->stretch - 1] : 100.0f;
    values[2].axisTag = DWRITE_FONT_AXIS_TAG_ITALIC;
    values[2].value = a->style == DWRITE_FONT_STYLE_ITALIC ? 1.0f : 0.0f;
    values[3].axisTag = DWRITE_FONT_AXIS_TAG_SLANT;
    values[3].value = a->style == DWRITE_FONT_STYLE_NORMAL ? 0.0f : -20.0f;
    return 4;
}

void DwAxesToAttributes(const DWRITE_FONT_AXIS_VALUE *values, UINT32 count, DWRITE_FONT_WEIGHT *weight,
                        DWRITE_FONT_STRETCH *stretch, DWRITE_FONT_STYLE *style)
{
    BOOL italic = *style == DWRITE_FONT_STYLE_ITALIC, slanted = *style == DWRITE_FONT_STYLE_OBLIQUE;
    for (UINT32 i = 0; i < count; i++) {
        FLOAT v = values[i].value;
        switch (values[i].axisTag) {
        case DWRITE_FONT_AXIS_TAG_WEIGHT:
            *weight = (DWRITE_FONT_WEIGHT)(v < 1 ? 1 : v > 999 ? 999 : (int)(v + 0.5f));
            break;
        case DWRITE_FONT_AXIS_TAG_WIDTH: {
            int best = 4;
            for (int s = 0; s < 9; s++) {
                FLOAT d = g_widths[s] - v, e = g_widths[best] - v;
                if ((d < 0 ? -d : d) < (e < 0 ? -e : e))
                    best = s;
            }
            *stretch = (DWRITE_FONT_STRETCH)(best + 1);
            break;
        }
        case DWRITE_FONT_AXIS_TAG_ITALIC:
            italic = v >= 0.5f;
            break;
        case DWRITE_FONT_AXIS_TAG_SLANT:
            slanted = v != 0;
            break;
        default:
            break;
        }
    }
    *style = italic ? DWRITE_FONT_STYLE_ITALIC : slanted ? DWRITE_FONT_STYLE_OBLIQUE : DWRITE_FONT_STYLE_NORMAL;
}

/* ------------------------------------------------------------------ string lists */

typedef struct DwStringList {
    const IDWriteStringListVtbl *lpVtbl;
    LONG refs;
    UINT32 count;
    WCHAR **locales;
    WCHAR **texts;
} DwStringList;

static HRESULT WINAPI list_query(IDWriteStringList *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteStringList)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI list_addref(IDWriteStringList *This) { return InterlockedIncrement(&((DwStringList *)This)->refs); }

static ULONG WINAPI list_release(IDWriteStringList *This)
{
    DwStringList *l = (DwStringList *)This;
    ULONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        for (UINT32 i = 0; i < l->count; i++) {
            DwFree(l->locales[i]);
            DwFree(l->texts[i]);
        }
        DwFree(l->locales);
        DwFree(l->texts);
        DwFree(l);
    }
    return refs;
}

static UINT32 WINAPI list_count(IDWriteStringList *This) { return ((DwStringList *)This)->count; }

static HRESULT list_length(const WCHAR *const *strings, UINT32 count, UINT32 index, UINT32 *length)
{
    if (!length)
        return E_POINTER;
    if (index >= count) {
        *length = 0;
        return E_INVALIDARG;
    }
    *length = (UINT32)wcslen(strings[index]);
    return S_OK;
}

static HRESULT list_copy(const WCHAR *const *strings, UINT32 count, UINT32 index, WCHAR *buffer, UINT32 size)
{
    if (index >= count) {
        if (buffer && size)
            buffer[0] = 0;
        return E_INVALIDARG;
    }
    UINT32 length = (UINT32)wcslen(strings[index]);
    if (!buffer || size <= length) {
        if (buffer && size)
            buffer[0] = 0;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    memcpy(buffer, strings[index], (length + 1) * sizeof(WCHAR));
    return S_OK;
}

static HRESULT WINAPI list_locale_length(IDWriteStringList *This, UINT32 index, UINT32 *length)
{
    DwStringList *l = (DwStringList *)This;
    return list_length((const WCHAR *const *)l->locales, l->count, index, length);
}

static HRESULT WINAPI list_locale(IDWriteStringList *This, UINT32 index, WCHAR *name, UINT32 size)
{
    DwStringList *l = (DwStringList *)This;
    return list_copy((const WCHAR *const *)l->locales, l->count, index, name, size);
}

static HRESULT WINAPI list_string_length(IDWriteStringList *This, UINT32 index, UINT32 *length)
{
    DwStringList *l = (DwStringList *)This;
    return list_length((const WCHAR *const *)l->texts, l->count, index, length);
}

static HRESULT WINAPI list_string(IDWriteStringList *This, UINT32 index, WCHAR *string, UINT32 size)
{
    DwStringList *l = (DwStringList *)This;
    return list_copy((const WCHAR *const *)l->texts, l->count, index, string, size);
}

static const IDWriteStringListVtbl g_list_vtbl = {
    list_query, list_addref, list_release, list_count, list_locale_length, list_locale, list_string_length, list_string,
};

HRESULT DwStringListCreate(const WCHAR *const *locales, const WCHAR *const *texts, UINT32 count, IDWriteStringList **out)
{
    *out = nullptr;
    DwStringList *l = DwAllocZero(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->lpVtbl = &g_list_vtbl;
    l->refs = 1;
    l->locales = DwAllocZero((count + 1) * sizeof(WCHAR *));
    l->texts = DwAllocZero((count + 1) * sizeof(WCHAR *));
    if (!l->locales || !l->texts) {
        list_release((IDWriteStringList *)l);
        return E_OUTOFMEMORY;
    }
    for (UINT32 i = 0; i < count; i++) {
        l->locales[i] = DwStrDup(locales[i] ? locales[i] : L"");
        l->texts[i] = DwStrDup(texts[i]);
        if (!l->locales[i] || !l->texts[i]) {
            DwFree(l->locales[i]);
            DwFree(l->texts[i]);
            list_release((IDWriteStringList *)l);
            return E_OUTOFMEMORY;
        }
        l->count++;
    }
    *out = (IDWriteStringList *)l;
    return S_OK;
}

/* ------------------------------------------------------------------ font face references */

typedef struct DwReference {
    const IDWriteFontFaceReference1Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwFontKey key;
} DwReference;

static const IDWriteFontFaceReference1Vtbl g_reference_vtbl;

static HRESULT WINAPI reference_query(IDWriteFontFaceReference1 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontFaceReference) ||
        IsEqualGUID(riid, &IID_IDWriteFontFaceReference1)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI reference_addref(IDWriteFontFaceReference1 *This)
{
    return InterlockedIncrement(&((DwReference *)This)->refs);
}

static ULONG WINAPI reference_release(IDWriteFontFaceReference1 *This)
{
    DwReference *r = (DwReference *)This;
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        r->key.file->lpVtbl->Release(r->key.file);
        DwFree(r);
    }
    return refs;
}

static HRESULT reference_face(DwReference *r, DWRITE_FONT_SIMULATIONS simulations, void **face)
{
    if (!face)
        return E_POINTER;
    *face = nullptr;
    return DwFaceCreateWithAxes(r->factory, r->key.file, r->key.index, simulations, r->key.axes, r->key.axis_count,
                                (IDWriteFontFace **)face);
}

static HRESULT WINAPI reference_create_face(IDWriteFontFaceReference1 *This, IDWriteFontFace3 **face)
{
    DwReference *r = (DwReference *)This;
    return reference_face(r, r->key.simulations, (void **)face);
}

static HRESULT WINAPI reference_create_face_simulated(IDWriteFontFaceReference1 *This,
                                                      DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace3 **face)
{
    return reference_face((DwReference *)This, simulations, (void **)face);
}

static BOOL WINAPI reference_equals(IDWriteFontFaceReference1 *This, IDWriteFontFaceReference *other)
{
    DwFontKey key;
    return other && DwFaceReferenceKey(other, &key) && keys_equal(&((DwReference *)This)->key, &key);
}

static UINT32 WINAPI reference_index(IDWriteFontFaceReference1 *This) { return ((DwReference *)This)->key.index; }

static DWRITE_FONT_SIMULATIONS WINAPI reference_simulations(IDWriteFontFaceReference1 *This)
{
    return ((DwReference *)This)->key.simulations;
}

static HRESULT WINAPI reference_file(IDWriteFontFaceReference1 *This, IDWriteFontFile **file)
{
    if (!file)
        return E_POINTER;
    *file = ((DwReference *)This)->key.file;
    (*file)->lpVtbl->AddRef(*file);
    return S_OK;
}

static HRESULT file_stream_info(IDWriteFontFile *file, UINT64 *size, UINT64 *time)
{
    IDWriteFontFileStream *stream;
    HRESULT hr = DwFontFileOpen(file, &stream);
    if (FAILED(hr))
        return hr;
    if (size)
        hr = stream->lpVtbl->GetFileSize(stream, size);
    if (SUCCEEDED(hr) && time)
        hr = stream->lpVtbl->GetLastWriteTime(stream, time);
    stream->lpVtbl->Release(stream);
    return hr;
}

static UINT64 WINAPI reference_file_size(IDWriteFontFaceReference1 *This)
{
    UINT64 size = 0;
    if (FAILED(file_stream_info(((DwReference *)This)->key.file, &size, nullptr)))
        return 0;
    return size;
}

static HRESULT WINAPI reference_file_time(IDWriteFontFaceReference1 *This, FILETIME *time)
{
    if (!time)
        return E_POINTER;
    UINT64 t = 0;
    HRESULT hr = file_stream_info(((DwReference *)This)->key.file, nullptr, &t);
    time->dwLowDateTime = (DWORD)t;
    time->dwHighDateTime = (DWORD)(t >> 32);
    return hr;
}

static DWRITE_LOCALITY WINAPI reference_locality(IDWriteFontFaceReference1 *This) { return DWRITE_LOCALITY_LOCAL; }

/* every file is local, so there is never anything to download. */
static HRESULT WINAPI reference_enqueue(IDWriteFontFaceReference1 *This) { return S_OK; }

static HRESULT WINAPI reference_enqueue_characters(IDWriteFontFaceReference1 *This, const WCHAR *characters,
                                                   UINT32 count)
{
    return count && !characters ? E_INVALIDARG : S_OK;
}

static HRESULT WINAPI reference_enqueue_glyphs(IDWriteFontFaceReference1 *This, const UINT16 *glyphs, UINT32 count)
{
    return count && !glyphs ? E_INVALIDARG : S_OK;
}

static HRESULT WINAPI reference_enqueue_fragment(IDWriteFontFaceReference1 *This, UINT64 offset, UINT64 size)
{
    return S_OK;
}

static HRESULT WINAPI reference_create_face5(IDWriteFontFaceReference1 *This, IDWriteFontFace5 **face)
{
    DwReference *r = (DwReference *)This;
    return reference_face(r, r->key.simulations, (void **)face);
}

/* the values of every axis of the face, the defaults where the reference names none. */
static UINT32 instance_axes(const DwFontKey *key, DWRITE_FONT_AXIS_VALUE *values)
{
    DwFontData *data;
    if (FAILED(DwFontDataOpen(key->file, key->index, &data)))
        return 0;
    DwAxis axes[DW_MAX_AXES];
    UINT32 count = DwFontDataAxes(data, axes, DW_MAX_AXES);
    DwFontDataRelease(data);
    for (UINT32 i = 0; i < count; i++) {
        values[i].axisTag = (DWRITE_FONT_AXIS_TAG)axes[i].tag;
        values[i].value = axes[i].def;
        for (UINT32 k = 0; k < key->axis_count; k++) {
            if (key->axes[k].axisTag == values[i].axisTag) {
                FLOAT v = key->axes[k].value;
                values[i].value = v < axes[i].min ? axes[i].min : v > axes[i].max ? axes[i].max : v;
            }
        }
    }
    return count;
}

static UINT32 WINAPI reference_axis_count(IDWriteFontFaceReference1 *This)
{
    DWRITE_FONT_AXIS_VALUE values[DW_MAX_AXES];
    return instance_axes(&((DwReference *)This)->key, values);
}

static HRESULT WINAPI reference_axis_values(IDWriteFontFaceReference1 *This, DWRITE_FONT_AXIS_VALUE *values,
                                            UINT32 count)
{
    DWRITE_FONT_AXIS_VALUE all[DW_MAX_AXES];
    UINT32 n = instance_axes(&((DwReference *)This)->key, all);
    if (count < n || (n && !values))
        return E_NOT_SUFFICIENT_BUFFER;
    if (n)
        memcpy(values, all, n * sizeof(*values));
    return S_OK;
}

static const IDWriteFontFaceReference1Vtbl g_reference_vtbl = {
    reference_query,          reference_addref,          reference_release,
    reference_create_face,    reference_create_face_simulated, reference_equals,
    reference_index,          reference_simulations,     reference_file,
    reference_file_size,      reference_file_size,       reference_file_time,
    reference_locality,       reference_enqueue,         reference_enqueue_characters,
    reference_enqueue_glyphs, reference_enqueue_fragment, reference_create_face5,
    reference_axis_count,     reference_axis_values,
};

HRESULT DwFaceReferenceCreate(DwFactory *factory, const DwFontKey *key, IDWriteFontFaceReference1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!key->file || (key->simulations & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE)))
        return E_INVALIDARG;
    DwReference *r = DwAllocZero(sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->lpVtbl = &g_reference_vtbl;
    r->refs = 1;
    r->factory = factory;
    r->key = *key;
    r->key.file->lpVtbl->AddRef(r->key.file);
    *out = (IDWriteFontFaceReference1 *)r;
    return S_OK;
}

BOOL DwFaceReferenceKey(IDWriteFontFaceReference *reference, DwFontKey *key)
{
    memset(key, 0, sizeof(*key));
    if (!reference)
        return FALSE;
    if (reference->lpVtbl == (const IDWriteFontFaceReferenceVtbl *)&g_reference_vtbl) {
        *key = ((DwReference *)reference)->key;
        return TRUE;
    }
    IDWriteFontFile *file;
    if (FAILED(reference->lpVtbl->GetFontFile(reference, &file)))
        return FALSE;
    key->file = file;
    /* the caller's reference keeps the file alive. */
    file->lpVtbl->Release(file);
    key->index = reference->lpVtbl->GetFontFaceIndex(reference);
    key->simulations = reference->lpVtbl->GetSimulations(reference);
    IDWriteFontFaceReference1 *one;
    if (SUCCEEDED(reference->lpVtbl->QueryInterface(reference, &IID_IDWriteFontFaceReference1, (void **)&one))) {
        UINT32 count = one->lpVtbl->GetFontAxisValueCount(one);
        if (count && count <= DW_MAX_AXES && SUCCEEDED(one->lpVtbl->GetFontAxisValues(one, key->axes, count)))
            key->axis_count = count;
        one->lpVtbl->Release(one);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ fonts of sets */

typedef struct DwCustomProperty {
    DWRITE_FONT_PROPERTY_ID id;
    WCHAR *value;
    WCHAR *locale;
} DwCustomProperty;

struct DwSetFont {
    LONG refs;
    DwFontKey key;
    DWRITE_FONT_SOURCE_TYPE source;
    UINT32 custom_count;
    DwCustomProperty *custom;
    UINT32 range_count;
    DWRITE_FONT_AXIS_RANGE ranges[DW_MAX_AXES];
    /* read from the file on first use. */
    SRWLOCK lock;
    BOOL ready;
    BOOL readable;
    DwAttributes attributes;
    DwStrings *properties[DWRITE_FONT_PROPERTY_ID_TOTAL_RS3];
    UINT32 axis_count;
    DwAxis axes[DW_MAX_AXES];
};

static void set_font_addref(DwSetFont *f) { InterlockedIncrement(&f->refs); }

static void set_font_release(DwSetFont *f)
{
    if (!f || InterlockedDecrement(&f->refs))
        return;
    f->key.file->lpVtbl->Release(f->key.file);
    for (UINT32 i = 0; i < f->custom_count; i++) {
        DwFree(f->custom[i].value);
        DwFree(f->custom[i].locale);
    }
    DwFree(f->custom);
    for (UINT32 i = 0; i < DWRITE_FONT_PROPERTY_ID_TOTAL_RS3; i++)
        DwStringsRelease(f->properties[i]);
    DwFree(f);
}

static DwSetFont *set_font_new(const DwFontKey *key, DWRITE_FONT_SOURCE_TYPE source)
{
    DwSetFont *f = DwAllocZero(sizeof(*f));
    if (!f)
        return nullptr;
    f->refs = 1;
    f->key = *key;
    f->key.file->lpVtbl->AddRef(f->key.file);
    f->source = source;
    InitializeSRWLock(&f->lock);
    return f;
}

static HRESULT set_font_add_custom(DwSetFont *f, const DWRITE_FONT_PROPERTY *properties, UINT32 count)
{
    if (!count)
        return S_OK;
    f->custom = DwAllocZero(count * sizeof(*f->custom));
    if (!f->custom)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < count; i++) {
        f->custom[i].id = properties[i].propertyId;
        f->custom[i].value = DwStrDup(properties[i].propertyValue ? properties[i].propertyValue : L"");
        f->custom[i].locale = DwStrDup(properties[i].localeName ? properties[i].localeName : L"");
        f->custom_count++;
        if (!f->custom[i].value || !f->custom[i].locale)
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

const DwFontKey *DwSetFontKey(const DwSetFont *font) { return &font->key; }

static DwStrings *number_strings(UINT32 value)
{
    WCHAR text[16];
    int n = 0;
    WCHAR digits[16];
    do {
        digits[n++] = (WCHAR)(L'0' + value % 10);
        value /= 10;
    } while (value);
    for (int i = 0; i < n; i++)
        text[i] = digits[n - 1 - i];
    text[n] = 0;
    DwStrings *s = DwStringsNew();
    if (s)
        DwStringsAdd(s, L"", 0, text, (UINT32)n);
    return s;
}

static DwStrings *simulated_face_names(DwStrings *base, DWRITE_FONT_SIMULATIONS simulations)
{
    if (!simulations || !base)
        return base;
    const WCHAR *regular = DwStringsBest(base, L"en-us");
    const WCHAR *suffix = simulations == (DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE) ? L"Bold Oblique"
                          : simulations == DWRITE_FONT_SIMULATIONS_BOLD                                     ? L"Bold"
                                                                                                            : L"Oblique";
    WCHAR text[160];
    if (!regular || !_wcsicmp(regular, L"Regular") || !_wcsicmp(regular, L"Normal") || wcslen(regular) > 100) {
        wcscpy(text, suffix);
    } else {
        wcscpy(text, regular);
        wcscat(text, L" ");
        wcscat(text, suffix);
    }
    DwStrings *names = DwStringsNew();
    if (names)
        DwStringsAdd(names, L"en-us", 5, text, (UINT32)wcslen(text));
    DwStringsRelease(base);
    return names;
}

/* reads the names, attributes and axes the first time they are needed. */
static BOOL set_font_ready(DwSetFont *f)
{
    AcquireSRWLockShared(&f->lock);
    BOOL ready = f->ready;
    ReleaseSRWLockShared(&f->lock);
    if (ready)
        return f->readable;
    AcquireSRWLockExclusive(&f->lock);
    if (!f->ready) {
        DwFontData *data;
        if (SUCCEEDED(DwFontDataOpen(f->key.file, f->key.index, &data))) {
            DwAttributes a;
            DwFontDataAttributes(data, &a);
            DwStrings *family, *face, *typographic, *typographic_face;
            DwAttributes wws = a;
            DwModelNames(data, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, &wws, &family, &face);
            DwModelNames(data, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &a, &typographic, &typographic_face);
            a = wws;
            f->axis_count = DwFontDataAxes(data, f->axes, DW_MAX_AXES);
            /* an instance's axis values say what it is. */
            DwAxesToAttributes(f->key.axes, f->key.axis_count, &a.weight, &a.stretch, &a.style);
            if ((f->key.simulations & DWRITE_FONT_SIMULATIONS_BOLD) && a.weight < DWRITE_FONT_WEIGHT_BOLD)
                a.weight = DWRITE_FONT_WEIGHT_BOLD;
            if ((f->key.simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) && a.style == DWRITE_FONT_STYLE_NORMAL)
                a.style = DWRITE_FONT_STYLE_OBLIQUE;
            f->attributes = a;
            f->properties[DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FAMILY_NAME] = family;
            f->properties[DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME] = typographic;
            f->properties[DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FACE_NAME] =
                simulated_face_names(face, f->key.simulations);
            f->properties[DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FACE_NAME] =
                simulated_face_names(typographic_face, f->key.simulations);
            f->properties[DWRITE_FONT_PROPERTY_ID_FULL_NAME] = DwFontDataNames(data, 4);
            f->properties[DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME] = DwFontDataNames(data, 1);
            f->properties[DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME] = DwFontDataNames(data, 6);
            f->properties[DWRITE_FONT_PROPERTY_ID_WEIGHT] = number_strings(a.weight);
            f->properties[DWRITE_FONT_PROPERTY_ID_STRETCH] = number_strings(a.stretch);
            f->properties[DWRITE_FONT_PROPERTY_ID_STYLE] = number_strings(a.style);
            DwFontDataRelease(data);
            f->readable = TRUE;
        }
        /* what the builder was given replaces what the file says. */
        for (UINT32 id = 1; id < DWRITE_FONT_PROPERTY_ID_TOTAL_RS3; id++) {
            DwStrings *custom = nullptr;
            for (UINT32 i = 0; i < f->custom_count; i++) {
                if ((UINT32)f->custom[i].id != id)
                    continue;
                if (!custom)
                    custom = DwStringsNew();
                if (custom)
                    DwStringsAdd(custom, f->custom[i].locale, (UINT32)wcslen(f->custom[i].locale), f->custom[i].value,
                                 (UINT32)wcslen(f->custom[i].value));
            }
            if (custom) {
                DwStringsRelease(f->properties[id]);
                f->properties[id] = custom;
            }
        }
        if (f->custom_count && !f->readable) {
            f->attributes.weight = DWRITE_FONT_WEIGHT_NORMAL;
            f->attributes.stretch = DWRITE_FONT_STRETCH_NORMAL;
            f->attributes.style = DWRITE_FONT_STYLE_NORMAL;
            f->readable = TRUE;
        }
        f->ready = TRUE;
    }
    BOOL readable = f->readable;
    ReleaseSRWLockExclusive(&f->lock);
    return readable;
}

static DwStrings *set_font_property(DwSetFont *f, DWRITE_FONT_PROPERTY_ID id)
{
    if ((unsigned)id >= DWRITE_FONT_PROPERTY_ID_TOTAL_RS3 || !set_font_ready(f))
        return nullptr;
    return f->properties[id];
}

DwStrings *DwSetFontCustom(DwSetFont *font, DWRITE_FONT_PROPERTY_ID id)
{
    for (UINT32 i = 0; i < font->custom_count; i++) {
        if (font->custom[i].id == id)
            return set_font_property(font, id);
    }
    return nullptr;
}

static BOOL set_font_matches(DwSetFont *f, const DWRITE_FONT_PROPERTY *property)
{
    DwStrings *strings = set_font_property(f, property->propertyId);
    if (!strings || !property->propertyValue)
        return FALSE;
    const WCHAR *locale = property->localeName;
    for (UINT32 i = 0; i < DwStringsCount(strings); i++) {
        const DwStringEntry *e = DwStringsAt(strings, i);
        if (locale && locale[0] && _wcsicmp(locale, e->locale))
            continue;
        if (!DwCompareNames(e->text, property->propertyValue))
            return TRUE;
    }
    return FALSE;
}

static BOOL set_font_in_family(DwSetFont *f, const WCHAR *family)
{
    static const DWRITE_FONT_PROPERTY_ID ids[] = {
        DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FAMILY_NAME,
        DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME,
        DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME,
    };
    for (UINT32 i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        DWRITE_FONT_PROPERTY p = {ids[i], family, nullptr};
        if (set_font_matches(f, &p))
            return TRUE;
    }
    return FALSE;
}

/* the font's ranges: points for its instance and its attributes, the fvar ranges where it varies. */
static UINT32 set_font_ranges(DwSetFont *f, DWRITE_FONT_AXIS_RANGE *ranges)
{
    if (!set_font_ready(f))
        return 0;
    if (f->range_count) {
        memcpy(ranges, f->ranges, f->range_count * sizeof(*ranges));
        return f->range_count;
    }
    UINT32 n = 0;
    for (UINT32 i = 0; i < f->axis_count; i++) {
        ranges[n].axisTag = (DWRITE_FONT_AXIS_TAG)f->axes[i].tag;
        ranges[n].minValue = f->axes[i].min;
        ranges[n].maxValue = f->axes[i].max;
        for (UINT32 k = 0; k < f->key.axis_count; k++) {
            if (f->key.axes[k].axisTag == ranges[n].axisTag)
                ranges[n].minValue = ranges[n].maxValue = f->key.axes[k].value;
        }
        n++;
    }
    DWRITE_FONT_AXIS_VALUE implied[4];
    UINT32 count = DwAttributeAxes(&f->attributes, implied);
    for (UINT32 i = 0; i < count && n < DW_MAX_AXES; i++) {
        BOOL present = FALSE;
        for (UINT32 k = 0; k < n; k++)
            present |= ranges[k].axisTag == implied[i].axisTag;
        if (!present) {
            ranges[n].axisTag = implied[i].axisTag;
            ranges[n].minValue = ranges[n].maxValue = implied[i].value;
            n++;
        }
    }
    return n;
}

static BOOL set_font_in_ranges(DwSetFont *f, const DWRITE_FONT_AXIS_RANGE *wanted, UINT32 count, BOOL any)
{
    DWRITE_FONT_AXIS_RANGE ranges[DW_MAX_AXES];
    UINT32 n = set_font_ranges(f, ranges);
    if (!count)
        return TRUE;
    for (UINT32 i = 0; i < count; i++) {
        BOOL hit = FALSE;
        for (UINT32 k = 0; k < n && !hit; k++) {
            hit = ranges[k].axisTag == wanted[i].axisTag && ranges[k].maxValue >= wanted[i].minValue &&
                  ranges[k].minValue <= wanted[i].maxValue;
        }
        if (hit && any)
            return TRUE;
        if (!hit && !any)
            return FALSE;
    }
    return !any;
}

/* ------------------------------------------------------------------ font sets */

typedef struct DwFontSet {
    const IDWriteFontSet4Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwSetFont **fonts;
    UINT32 count;
    HANDLE expiration;
} DwFontSet;

static const IDWriteFontSet4Vtbl g_set_vtbl;

static HRESULT set_create(DwFactory *factory, DwSetFont *const *fonts, UINT32 count, IDWriteFontSet4 **out);

static DwFontSet *set_of(void *set) { return (DwFontSet *)set; }

BOOL DwFontSetFonts(IDWriteFontSet *set, DwSetFont *const **fonts, UINT32 *count)
{
    if (!set || set->lpVtbl != (const IDWriteFontSetVtbl *)&g_set_vtbl)
        return FALSE;
    *fonts = set_of(set)->fonts;
    *count = set_of(set)->count;
    return TRUE;
}

static HRESULT WINAPI set_query(IDWriteFontSet4 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontSet) ||
        IsEqualGUID(riid, &IID_IDWriteFontSet1) || IsEqualGUID(riid, &IID_IDWriteFontSet2) ||
        IsEqualGUID(riid, &IID_IDWriteFontSet3) || IsEqualGUID(riid, &IID_IDWriteFontSet4)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI set_addref(IDWriteFontSet4 *This) { return InterlockedIncrement(&set_of(This)->refs); }

static ULONG WINAPI set_release(IDWriteFontSet4 *This)
{
    DwFontSet *s = set_of(This);
    ULONG refs = InterlockedDecrement(&s->refs);
    if (!refs) {
        for (UINT32 i = 0; i < s->count; i++)
            set_font_release(s->fonts[i]);
        DwFree(s->fonts);
        if (s->expiration)
            CloseHandle(s->expiration);
        DwFree(s);
    }
    return refs;
}

static UINT32 WINAPI set_count(IDWriteFontSet4 *This) { return set_of(This)->count; }

static HRESULT WINAPI set_reference1(IDWriteFontSet4 *This, UINT32 index, IDWriteFontFaceReference1 **reference)
{
    DwFontSet *s = set_of(This);
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    if (index >= s->count)
        return E_INVALIDARG;
    return DwFaceReferenceCreate(s->factory, &s->fonts[index]->key, reference);
}

static HRESULT WINAPI set_reference(IDWriteFontSet4 *This, UINT32 index, IDWriteFontFaceReference **reference)
{
    return set_reference1(This, index, (IDWriteFontFaceReference1 **)reference);
}

static HRESULT find_key(DwFontSet *s, const DwFontKey *key, UINT32 *index, BOOL *exists)
{
    for (UINT32 i = 0; i < s->count; i++) {
        if (keys_equal(&s->fonts[i]->key, key)) {
            *index = i;
            *exists = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI set_find_reference(IDWriteFontSet4 *This, IDWriteFontFaceReference *reference, UINT32 *index,
                                         BOOL *exists)
{
    if (!index || !exists)
        return E_POINTER;
    *index = (UINT32)-1;
    *exists = FALSE;
    DwFontKey key;
    if (!DwFaceReferenceKey(reference, &key))
        return E_INVALIDARG;
    return find_key(set_of(This), &key, index, exists);
}

static HRESULT WINAPI set_find_face(IDWriteFontSet4 *This, IDWriteFontFace *face, UINT32 *index, BOOL *exists)
{
    if (!index || !exists)
        return E_POINTER;
    *index = (UINT32)-1;
    *exists = FALSE;
    if (!face)
        return E_INVALIDARG;
    DwFontKey key = {};
    UINT32 files = 1;
    IDWriteFontFile *file;
    HRESULT hr = face->lpVtbl->GetFiles(face, &files, &file);
    if (FAILED(hr))
        return hr;
    key.file = file;
    key.index = face->lpVtbl->GetIndex(face);
    key.simulations = face->lpVtbl->GetSimulations(face);
    IDWriteFontFace5 *five;
    BOOL varies = FALSE;
    if (SUCCEEDED(face->lpVtbl->QueryInterface(face, &IID_IDWriteFontFace5, (void **)&five))) {
        varies = five->lpVtbl->HasVariations(five);
        UINT32 count = five->lpVtbl->GetFontAxisValueCount(five);
        if (varies && count <= DW_MAX_AXES && SUCCEEDED(five->lpVtbl->GetFontAxisValues(five, key.axes, count)))
            key.axis_count = count;
        five->lpVtbl->Release(five);
    }
    find_key(set_of(This), &key, index, exists);
    /* a face of the default instance is the font that names no axis values. */
    if (!*exists && varies) {
        DwFontSet *s = set_of(This);
        for (UINT32 i = 0; i < s->count && !*exists; i++) {
            DwFontKey *k = &s->fonts[i]->key;
            if (!k->axis_count && k->index == key.index && k->simulations == key.simulations &&
                DwSameFile(k->file, key.file)) {
                DWRITE_FONT_AXIS_VALUE defaults[DW_MAX_AXES];
                UINT32 n = instance_axes(k, defaults);
                if (n == key.axis_count && !memcmp(defaults, key.axes, n * sizeof(*defaults))) {
                    *index = i;
                    *exists = TRUE;
                }
            }
        }
    }
    file->lpVtbl->Release(file);
    return S_OK;
}

/* the string of a font for the first of the preferred locales it has, else english, else its first. */
static const DwStringEntry *preferred_entry(DwStrings *strings, const WCHAR *preferred)
{
    UINT32 count = DwStringsCount(strings);
    if (!count)
        return nullptr;
    const WCHAR *p = preferred;
    while (p && *p) {
        const WCHAR *end = p;
        while (*end && *end != ';')
            end++;
        size_t length = (size_t)(end - p);
        for (UINT32 i = 0; i < count && length; i++) {
            const DwStringEntry *e = DwStringsAt(strings, i);
            if (wcslen(e->locale) == length && !_wcsnicmp(e->locale, p, length))
                return e;
        }
        p = *end ? end + 1 : end;
    }
    for (UINT32 i = 0; i < count; i++) {
        const DwStringEntry *e = DwStringsAt(strings, i);
        if (!_wcsicmp(e->locale, L"en-us"))
            return e;
    }
    return DwStringsAt(strings, 0);
}

static HRESULT WINAPI set_values_locale(IDWriteFontSet4 *This, DWRITE_FONT_PROPERTY_ID id, const WCHAR *preferred,
                                        IDWriteStringList **values)
{
    DwFontSet *s = set_of(This);
    if (!values)
        return E_POINTER;
    *values = nullptr;
    if ((unsigned)id >= DWRITE_FONT_PROPERTY_ID_TOTAL_RS3)
        return E_INVALIDARG;
    const WCHAR **locales = DwAlloc((s->count + 1) * sizeof(WCHAR *));
    const WCHAR **texts = DwAlloc((s->count + 1) * sizeof(WCHAR *));
    if (!locales || !texts) {
        DwFree(locales);
        DwFree(texts);
        return E_OUTOFMEMORY;
    }
    UINT32 n = 0;
    for (UINT32 i = 0; i < s->count; i++) {
        DwStrings *strings = set_font_property(s->fonts[i], id);
        const DwStringEntry *e = strings ? preferred_entry(strings, preferred) : nullptr;
        if (!e)
            continue;
        BOOL seen = FALSE;
        for (UINT32 k = 0; k < n && !seen; k++)
            seen = !DwCompareNames(texts[k], e->text);
        if (seen)
            continue;
        locales[n] = e->locale;
        texts[n] = e->text;
        n++;
    }
    HRESULT hr = DwStringListCreate(locales, texts, n, values);
    DwFree(locales);
    DwFree(texts);
    return hr;
}

static HRESULT WINAPI set_values(IDWriteFontSet4 *This, DWRITE_FONT_PROPERTY_ID id, IDWriteStringList **values)
{
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    if (!GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH))
        wcscpy(locale, L"en-us");
    return set_values_locale(This, id, locale, values);
}

static HRESULT WINAPI set_font_values(IDWriteFontSet4 *This, UINT32 index, DWRITE_FONT_PROPERTY_ID id, BOOL *exists,
                                      IDWriteLocalizedStrings **values)
{
    DwFontSet *s = set_of(This);
    if (!exists || !values)
        return E_POINTER;
    *exists = FALSE;
    *values = nullptr;
    if (index >= s->count || (unsigned)id >= DWRITE_FONT_PROPERTY_ID_TOTAL_RS3)
        return E_INVALIDARG;
    DwStrings *strings = set_font_property(s->fonts[index], id);
    if (!strings)
        return S_OK;
    DwStringsAddRef(strings);
    *values = DwStringsInterface(strings);
    *exists = TRUE;
    return S_OK;
}

static HRESULT WINAPI set_occurrences(IDWriteFontSet4 *This, const DWRITE_FONT_PROPERTY *property, UINT32 *count)
{
    DwFontSet *s = set_of(This);
    if (!count)
        return E_POINTER;
    *count = 0;
    if (!property)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < s->count; i++)
        *count += set_font_matches(s->fonts[i], property);
    return S_OK;
}

/* a new set of the fonts kept, in the order of the indices. */
static HRESULT subset(DwFontSet *s, const UINT32 *indices, UINT32 count, void **out)
{
    DwSetFont **fonts = DwAlloc((count + 1) * sizeof(*fonts));
    if (!fonts)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < count; i++)
        fonts[i] = s->fonts[indices[i]];
    HRESULT hr = set_create(s->factory, fonts, count, (IDWriteFontSet4 **)out);
    DwFree(fonts);
    return hr;
}

/* the fonts of a family, the best fit for the request first. */
static UINT32 *family_order(DwFontSet *s, const WCHAR *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                            DWRITE_FONT_STYLE style, UINT32 *count)
{
    UINT32 *order = DwAlloc((s->count + 1) * sizeof(UINT32));
    *count = 0;
    if (!order)
        return nullptr;
    for (UINT32 i = 0; i < s->count; i++) {
        if (set_font_in_family(s->fonts[i], family))
            order[(*count)++] = i;
    }
    for (UINT32 i = 1; i < *count; i++) {
        UINT32 v = order[i], j = i;
        while (j > 0 && DwCompareFit(&s->fonts[v]->attributes, s->fonts[v]->key.simulations,
                                     &s->fonts[order[j - 1]]->attributes, s->fonts[order[j - 1]]->key.simulations,
                                     weight, stretch, style) < 0) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    return order;
}

static HRESULT WINAPI set_matching_name(IDWriteFontSet4 *This, const WCHAR *family, DWRITE_FONT_WEIGHT weight,
                                        DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFontSet **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!family)
        return E_INVALIDARG;
    UINT32 count;
    UINT32 *order = family_order(set_of(This), family, weight, stretch, style, &count);
    if (!order)
        return E_OUTOFMEMORY;
    HRESULT hr = subset(set_of(This), order, count, (void **)out);
    DwFree(order);
    return hr;
}

static HRESULT filter_properties(DwFontSet *s, const DWRITE_FONT_PROPERTY *properties, UINT32 count, BOOL any,
                                 UINT32 **indices, UINT32 *found)
{
    if (count && !properties)
        return E_INVALIDARG;
    *indices = DwAlloc((s->count + 1) * sizeof(UINT32));
    *found = 0;
    if (!*indices)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < s->count; i++) {
        BOOL keep = !any || !count;
        for (UINT32 p = 0; p < count; p++) {
            BOOL hit = set_font_matches(s->fonts[i], &properties[p]);
            if (any && hit) {
                keep = TRUE;
                break;
            }
            if (!any && !hit) {
                keep = FALSE;
                break;
            }
        }
        if (keep)
            (*indices)[(*found)++] = i;
    }
    return S_OK;
}

static HRESULT WINAPI set_matching(IDWriteFontSet4 *This, const DWRITE_FONT_PROPERTY *properties, UINT32 count,
                                   IDWriteFontSet **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    UINT32 *indices, found;
    HRESULT hr = filter_properties(set_of(This), properties, count, FALSE, &indices, &found);
    if (SUCCEEDED(hr))
        hr = subset(set_of(This), indices, found, (void **)out);
    DwFree(indices);
    return hr;
}

static void request_of_axes(const DWRITE_FONT_AXIS_VALUE *values, UINT32 count, DWRITE_FONT_WEIGHT *weight,
                            DWRITE_FONT_STRETCH *stretch, DWRITE_FONT_STYLE *style)
{
    *weight = DWRITE_FONT_WEIGHT_NORMAL;
    *stretch = DWRITE_FONT_STRETCH_NORMAL;
    *style = DWRITE_FONT_STYLE_NORMAL;
    DwAxesToAttributes(values, count, weight, stretch, style);
}

/* the fonts that have the property, ordered by the fit to the axis values. */
static HRESULT matching_axes(DwFontSet *s, const DWRITE_FONT_PROPERTY *property, const DWRITE_FONT_AXIS_VALUE *values,
                             UINT32 count, UINT32 **indices, UINT32 *found)
{
    if (!property || (count && !values))
        return E_INVALIDARG;
    HRESULT hr = filter_properties(s, property, 1, FALSE, indices, found);
    if (FAILED(hr))
        return hr;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_STYLE style;
    request_of_axes(values, count, &weight, &stretch, &style);
    UINT32 *order = *indices;
    for (UINT32 i = 1; i < *found; i++) {
        UINT32 v = order[i], j = i;
        while (j > 0 && DwCompareFit(&s->fonts[v]->attributes, s->fonts[v]->key.simulations,
                                     &s->fonts[order[j - 1]]->attributes, s->fonts[order[j - 1]]->key.simulations,
                                     weight, stretch, style) < 0) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    return S_OK;
}

static HRESULT WINAPI set_matching_axes(IDWriteFontSet4 *This, const DWRITE_FONT_PROPERTY *property,
                                        const DWRITE_FONT_AXIS_VALUE *values, UINT32 count, IDWriteFontSet1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    UINT32 *indices = nullptr, found;
    HRESULT hr = matching_axes(set_of(This), property, values, count, &indices, &found);
    if (SUCCEEDED(hr))
        hr = subset(set_of(This), indices, found, (void **)out);
    DwFree(indices);
    return hr;
}

static HRESULT WINAPI set_first_resources(IDWriteFontSet4 *This, IDWriteFontSet1 **out)
{
    DwFontSet *s = set_of(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    UINT32 *indices = DwAlloc((s->count + 1) * sizeof(UINT32));
    if (!indices)
        return E_OUTOFMEMORY;
    UINT32 n = 0;
    for (UINT32 i = 0; i < s->count; i++) {
        BOOL seen = FALSE;
        for (UINT32 k = 0; k < n && !seen; k++) {
            const DwFontKey *a = &s->fonts[indices[k]]->key, *b = &s->fonts[i]->key;
            seen = a->index == b->index && DwSameFile(a->file, b->file);
        }
        if (!seen)
            indices[n++] = i;
    }
    HRESULT hr = subset(s, indices, n, (void **)out);
    DwFree(indices);
    return hr;
}

static HRESULT WINAPI set_filtered_indices_set(IDWriteFontSet4 *This, const UINT32 *indices, UINT32 count,
                                               IDWriteFontSet1 **out)
{
    DwFontSet *s = set_of(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (count && !indices)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++) {
        if (indices[i] >= s->count)
            return E_INVALIDARG;
    }
    return subset(s, indices, count, (void **)out);
}

static HRESULT filter_ranges(DwFontSet *s, const DWRITE_FONT_AXIS_RANGE *ranges, UINT32 count, BOOL any,
                             UINT32 **indices, UINT32 *found)
{
    if (count && !ranges)
        return E_INVALIDARG;
    *indices = DwAlloc((s->count + 1) * sizeof(UINT32));
    *found = 0;
    if (!*indices)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < s->count; i++) {
        if (set_font_in_ranges(s->fonts[i], ranges, count, any))
            (*indices)[(*found)++] = i;
    }
    return S_OK;
}

static HRESULT WINAPI set_filtered_axes(IDWriteFontSet4 *This, const DWRITE_FONT_AXIS_RANGE *ranges, UINT32 count,
                                        BOOL any, IDWriteFontSet1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    UINT32 *indices = nullptr, found;
    HRESULT hr = filter_ranges(set_of(This), ranges, count, any, &indices, &found);
    if (SUCCEEDED(hr))
        hr = subset(set_of(This), indices, found, (void **)out);
    DwFree(indices);
    return hr;
}

static HRESULT WINAPI set_filtered(IDWriteFontSet4 *This, const DWRITE_FONT_PROPERTY *properties, UINT32 count,
                                   BOOL any, IDWriteFontSet1 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    UINT32 *indices = nullptr, found;
    HRESULT hr = filter_properties(set_of(This), properties, count, any, &indices, &found);
    if (SUCCEEDED(hr))
        hr = subset(set_of(This), indices, found, (void **)out);
    DwFree(indices);
    return hr;
}

static HRESULT copy_indices(const UINT32 *indices, UINT32 found, UINT32 *out, UINT32 max, UINT32 *actual)
{
    if (!actual)
        return E_POINTER;
    *actual = found;
    if (found > max || (found && !out))
        return E_NOT_SUFFICIENT_BUFFER;
    if (found)
        memcpy(out, indices, found * sizeof(UINT32));
    return S_OK;
}

static HRESULT WINAPI set_filtered_indices_axes(IDWriteFontSet4 *This, const DWRITE_FONT_AXIS_RANGE *ranges,
                                                UINT32 count, BOOL any, UINT32 *out, UINT32 max, UINT32 *actual)
{
    UINT32 *indices = nullptr, found;
    HRESULT hr = filter_ranges(set_of(This), ranges, count, any, &indices, &found);
    if (SUCCEEDED(hr))
        hr = copy_indices(indices, found, out, max, actual);
    DwFree(indices);
    return hr;
}

static HRESULT WINAPI set_filtered_indices(IDWriteFontSet4 *This, const DWRITE_FONT_PROPERTY *properties, UINT32 count,
                                           BOOL any, UINT32 *out, UINT32 max, UINT32 *actual)
{
    UINT32 *indices = nullptr, found;
    HRESULT hr = filter_properties(set_of(This), properties, count, any, &indices, &found);
    if (SUCCEEDED(hr))
        hr = copy_indices(indices, found, out, max, actual);
    DwFree(indices);
    return hr;
}

static HRESULT copy_ranges(const DWRITE_FONT_AXIS_RANGE *ranges, UINT32 n, DWRITE_FONT_AXIS_RANGE *out, UINT32 max,
                           UINT32 *actual)
{
    if (!actual)
        return E_POINTER;
    *actual = n;
    if (n > max || (n && !out))
        return E_NOT_SUFFICIENT_BUFFER;
    if (n)
        memcpy(out, ranges, n * sizeof(*ranges));
    return S_OK;
}

static HRESULT WINAPI set_font_axis_ranges(IDWriteFontSet4 *This, UINT32 index, DWRITE_FONT_AXIS_RANGE *out,
                                           UINT32 max, UINT32 *actual)
{
    DwFontSet *s = set_of(This);
    if (actual)
        *actual = 0;
    if (index >= s->count)
        return E_INVALIDARG;
    DWRITE_FONT_AXIS_RANGE ranges[DW_MAX_AXES];
    UINT32 n = set_font_ranges(s->fonts[index], ranges);
    return copy_ranges(ranges, n, out, max, actual);
}

static HRESULT WINAPI set_axis_ranges(IDWriteFontSet4 *This, DWRITE_FONT_AXIS_RANGE *out, UINT32 max, UINT32 *actual)
{
    DwFontSet *s = set_of(This);
    DWRITE_FONT_AXIS_RANGE all[DW_MAX_AXES * 2];
    UINT32 total = 0;
    for (UINT32 i = 0; i < s->count; i++) {
        DWRITE_FONT_AXIS_RANGE ranges[DW_MAX_AXES];
        UINT32 n = set_font_ranges(s->fonts[i], ranges);
        for (UINT32 r = 0; r < n; r++) {
            UINT32 k = 0;
            while (k < total && all[k].axisTag != ranges[r].axisTag)
                k++;
            if (k == total) {
                if (total == sizeof(all) / sizeof(all[0]))
                    continue;
                all[total++] = ranges[r];
            } else {
                if (ranges[r].minValue < all[k].minValue)
                    all[k].minValue = ranges[r].minValue;
                if (ranges[r].maxValue > all[k].maxValue)
                    all[k].maxValue = ranges[r].maxValue;
            }
        }
    }
    return copy_ranges(all, total, out, max, actual);
}

static HRESULT WINAPI set_resource(IDWriteFontSet4 *This, UINT32 index, IDWriteFontResource **resource)
{
    DwFontSet *s = set_of(This);
    if (!resource)
        return E_POINTER;
    *resource = nullptr;
    if (index >= s->count)
        return E_INVALIDARG;
    return DwFontResourceCreate(s->factory, s->fonts[index]->key.file, s->fonts[index]->key.index, resource);
}

static HRESULT WINAPI set_create_face(IDWriteFontSet4 *This, UINT32 index, IDWriteFontFace5 **face)
{
    DwFontSet *s = set_of(This);
    if (!face)
        return E_POINTER;
    *face = nullptr;
    if (index >= s->count)
        return E_INVALIDARG;
    const DwFontKey *k = &s->fonts[index]->key;
    return DwFaceCreateWithAxes(s->factory, k->file, k->index, k->simulations, k->axes, k->axis_count,
                                (IDWriteFontFace **)face);
}

static DWRITE_LOCALITY WINAPI set_locality(IDWriteFontSet4 *This, UINT32 index) { return DWRITE_LOCALITY_LOCAL; }

/* fonts never go away under a set, so its event is never set. */
static HANDLE WINAPI set_expiration(IDWriteFontSet4 *This)
{
    DwFontSet *s = set_of(This);
    if (!s->expiration) {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event && InterlockedCompareExchangePointer(&s->expiration, event, nullptr))
            CloseHandle(event);
    }
    return s->expiration;
}

static DWRITE_FONT_SOURCE_TYPE WINAPI set_source_type(IDWriteFontSet4 *This, UINT32 index)
{
    DwFontSet *s = set_of(This);
    return index < s->count ? s->fonts[index]->source : DWRITE_FONT_SOURCE_TYPE_UNKNOWN;
}

/* the path of a font on disk; fonts of other loaders have no source name. */
static const WCHAR *source_name(DwFontSet *s, UINT32 index)
{
    if (index >= s->count)
        return nullptr;
    const void *key;
    UINT32 size;
    IDWriteFontFile *file = s->fonts[index]->key.file;
    IDWriteFontFileLoader *loader;
    if (FAILED(file->lpVtbl->GetLoader(file, &loader)))
        return nullptr;
    loader->lpVtbl->Release(loader);
    if (loader != (IDWriteFontFileLoader *)DwLocalLoader() || FAILED(file->lpVtbl->GetReferenceKey(file, &key, &size)))
        return nullptr;
    return DwLocalKeyPath(key, size);
}

static UINT32 WINAPI set_source_length(IDWriteFontSet4 *This, UINT32 index)
{
    const WCHAR *name = source_name(set_of(This), index);
    return name ? (UINT32)wcslen(name) : 0;
}

static HRESULT WINAPI set_source(IDWriteFontSet4 *This, UINT32 index, WCHAR *buffer, UINT32 size)
{
    if (index >= set_of(This)->count)
        return E_INVALIDARG;
    const WCHAR *name = source_name(set_of(This), index);
    if (!name)
        name = L"";
    UINT32 length = (UINT32)wcslen(name);
    if (!buffer || size <= length) {
        if (buffer && size)
            buffer[0] = 0;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    memcpy(buffer, name, (length + 1) * sizeof(WCHAR));
    return S_OK;
}

/* weight, width, italic and slant from the attributes, and the optical size in points, unless the input names them. */
static UINT32 WINAPI set_convert(IDWriteFontSet4 *This, const DWRITE_FONT_AXIS_VALUE *input, UINT32 input_count,
                                 DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,
                                 FLOAT size, DWRITE_FONT_AXIS_VALUE *output)
{
    if (!output)
        return 0;
    DwAttributes a = {weight, stretch, style};
    DWRITE_FONT_AXIS_VALUE values[5];
    UINT32 n = DwAttributeAxes(&a, values);
    values[n].axisTag = DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE;
    values[n].value = size * 0.75f;
    n++;
    for (UINT32 i = 0; i < n; i++) {
        for (UINT32 k = 0; input && k < input_count; k++) {
            if (input[k].axisTag == values[i].axisTag)
                values[i].value = input[k].value;
        }
    }
    memcpy(output, values, n * sizeof(*values));
    return n;
}

static HRESULT WINAPI set_matching4(IDWriteFontSet4 *This, const WCHAR *family, const DWRITE_FONT_AXIS_VALUE *values,
                                    UINT32 count, DWRITE_FONT_SIMULATIONS allowed, IDWriteFontSet4 **out)
{
    DwFontSet *s = set_of(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!family || (count && !values))
        return E_INVALIDARG;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_STYLE style;
    request_of_axes(values, count, &weight, &stretch, &style);
    UINT32 found;
    UINT32 *order = family_order(s, family, weight, stretch, style, &found);
    if (!order)
        return E_OUTOFMEMORY;
    DwSetFont **fonts = DwAllocZero((found + 2) * sizeof(*fonts));
    if (!fonts) {
        DwFree(order);
        return E_OUTOFMEMORY;
    }
    UINT32 n = 0;
    /* the best font with the simulations it lacks for the request comes first. */
    DwSetFont *simulated = nullptr;
    if (found) {
        DwSetFont *best = s->fonts[order[0]];
        DWRITE_FONT_SIMULATIONS add = 0;
        if ((allowed & DWRITE_FONT_SIMULATIONS_BOLD) && weight >= DWRITE_FONT_WEIGHT_SEMI_BOLD &&
            best->attributes.weight < DWRITE_FONT_WEIGHT_SEMI_BOLD - 50)
            add |= DWRITE_FONT_SIMULATIONS_BOLD;
        if ((allowed & DWRITE_FONT_SIMULATIONS_OBLIQUE) && style != DWRITE_FONT_STYLE_NORMAL &&
            best->attributes.style == DWRITE_FONT_STYLE_NORMAL)
            add |= DWRITE_FONT_SIMULATIONS_OBLIQUE;
        if (add & ~best->key.simulations) {
            DwFontKey key = best->key;
            key.simulations |= add;
            simulated = set_font_new(&key, best->source);
            if (simulated)
                fonts[n++] = simulated;
        }
    }
    for (UINT32 i = 0; i < found; i++)
        fonts[n++] = s->fonts[order[i]];
    HRESULT hr = set_create(s->factory, fonts, n, out);
    set_font_release(simulated);
    DwFree(fonts);
    DwFree(order);
    return hr;
}

static const IDWriteFontSet4Vtbl g_set_vtbl = {
    set_query,
    set_addref,
    set_release,
    set_count,
    set_reference,
    set_find_reference,
    set_find_face,
    set_values,
    set_values_locale,
    set_font_values,
    set_occurrences,
    set_matching_name,
    set_matching,
    set_matching_axes,
    set_first_resources,
    set_filtered_indices_set,
    set_filtered_axes,
    set_filtered,
    set_filtered_indices_axes,
    set_filtered_indices,
    set_font_axis_ranges,
    set_axis_ranges,
    set_reference1,
    set_resource,
    set_create_face,
    set_locality,
    set_expiration,
    set_source_type,
    set_source_length,
    set_source,
    set_convert,
    set_matching4,
};

static HRESULT set_create(DwFactory *factory, DwSetFont *const *fonts, UINT32 count, IDWriteFontSet4 **out)
{
    *out = nullptr;
    DwFontSet *s = DwAllocZero(sizeof(*s));
    if (!s)
        return E_OUTOFMEMORY;
    s->fonts = DwAlloc((count + 1) * sizeof(*s->fonts));
    if (!s->fonts) {
        DwFree(s);
        return E_OUTOFMEMORY;
    }
    s->lpVtbl = &g_set_vtbl;
    s->refs = 1;
    s->factory = factory;
    for (UINT32 i = 0; i < count; i++) {
        s->fonts[i] = fonts[i];
        set_font_addref(fonts[i]);
    }
    s->count = count;
    *out = (IDWriteFontSet4 *)s;
    return S_OK;
}

static HRESULT set_from_keys(DwFactory *factory, const DwFontKey *keys, UINT32 count, DWRITE_FONT_SOURCE_TYPE source,
                             IDWriteFontSet4 **out)
{
    DwSetFont **fonts = DwAllocZero((count + 1) * sizeof(*fonts));
    if (!fonts)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (UINT32 i = 0; i < count && SUCCEEDED(hr); i++) {
        fonts[i] = set_font_new(&keys[i], source);
        if (!fonts[i])
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = set_create(factory, fonts, count, out);
    for (UINT32 i = 0; i < count; i++)
        set_font_release(fonts[i]);
    DwFree(fonts);
    return hr;
}

HRESULT DwFontSetFromKeys(DwFactory *factory, const DwFontKey *keys, UINT32 count, IDWriteFontSet4 **out)
{
    return set_from_keys(factory, keys, count, DWRITE_FONT_SOURCE_TYPE_UNKNOWN, out);
}

/* ------------------------------------------------------------------ the system font set */

static BOOL is_font_file(const WCHAR *name)
{
    size_t n = wcslen(name);
    return n > 4 && (!_wcsicmp(name + n - 4, L".ttf") || !_wcsicmp(name + n - 4, L".ttc") ||
                     !_wcsicmp(name + n - 4, L".otf") || !_wcsicmp(name + n - 4, L".otc"));
}

typedef struct KeyList {
    DwFontKey *keys;
    UINT32 count, capacity;
} KeyList;

static BOOL key_list_add(KeyList *l, const DwFontKey *key)
{
    if (l->count == l->capacity) {
        UINT32 capacity = l->capacity ? l->capacity * 2 : 128;
        DwFontKey *grown = DwAlloc(capacity * sizeof(*grown));
        if (!grown)
            return FALSE;
        if (l->count)
            memcpy(grown, l->keys, l->count * sizeof(*grown));
        DwFree(l->keys);
        l->keys = grown;
        l->capacity = capacity;
    }
    l->keys[l->count++] = *key;
    return TRUE;
}

/* every face of every font file of a directory. the keys hold references to the files. */
static void add_directory(KeyList *l, const WCHAR *directory)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    size_t length = wcslen(directory);
    if (length + 2 >= MAX_PATH)
        return;
    wcscpy(pattern, directory);
    wcscat(pattern, L"*");
    WIN32_FIND_DATAW find;
    HANDLE h = FindFirstFileW(pattern, &find);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !is_font_file(find.cFileName) ||
            length + wcslen(find.cFileName) >= MAX_PATH)
            continue;
        wcscpy(path, directory);
        wcscat(path, find.cFileName);
        IDWriteFontFile *file;
        if (FAILED(DwFontFileFromPath(path, &find.ftLastWriteTime, &file)))
            continue;
        BOOL supported;
        DWRITE_FONT_FILE_TYPE file_type;
        DWRITE_FONT_FACE_TYPE face_type;
        UINT32 faces = 0;
        if (SUCCEEDED(file->lpVtbl->Analyze(file, &supported, &file_type, &face_type, &faces)) && supported) {
            for (UINT32 i = 0; i < faces; i++) {
                DwFontKey key = {file, i};
                if (key_list_add(l, &key))
                    file->lpVtbl->AddRef(file);
            }
        }
        file->lpVtbl->Release(file);
    } while (FindNextFileW(h, &find));
    FindClose(h);
}

HRESULT DwSystemFontSet(DwFactory *factory, IDWriteFontSet4 **out)
{
    AcquireSRWLockExclusive(&factory->lock);
    HRESULT hr = S_OK;
    if (!factory->system_set) {
        KeyList machine = {}, user = {};
        WCHAR directory[MAX_PATH];
        UINT n = GetWindowsDirectoryW(directory, MAX_PATH);
        if (n && n < MAX_PATH - 8) {
            wcscat(directory, L"\\Fonts\\");
            add_directory(&machine, directory);
        }
        /* the fonts a user installs without being an administrator. */
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, MAX_PATH);
        if (n && n < MAX_PATH - 32) {
            wcscat(directory, L"\\Microsoft\\Windows\\Fonts\\");
            add_directory(&user, directory);
        }
        IDWriteFontSet4 *machine_set = nullptr, *user_set = nullptr;
        hr = set_from_keys(factory, machine.keys, machine.count, DWRITE_FONT_SOURCE_TYPE_PER_MACHINE, &machine_set);
        if (SUCCEEDED(hr))
            hr = set_from_keys(factory, user.keys, user.count, DWRITE_FONT_SOURCE_TYPE_PER_USER, &user_set);
        if (SUCCEEDED(hr)) {
            DwFontSet *a = set_of(machine_set), *b = set_of(user_set);
            DwSetFont **fonts = DwAlloc((a->count + b->count + 1) * sizeof(*fonts));
            if (fonts) {
                memcpy(fonts, a->fonts, a->count * sizeof(*fonts));
                if (b->count)
                    memcpy(fonts + a->count, b->fonts, b->count * sizeof(*fonts));
                hr = set_create(factory, fonts, a->count + b->count, &factory->system_set);
                DwFree(fonts);
            } else {
                hr = E_OUTOFMEMORY;
            }
        }
        if (machine_set)
            set_release(machine_set);
        if (user_set)
            set_release(user_set);
        for (UINT32 i = 0; i < machine.count; i++)
            machine.keys[i].file->lpVtbl->Release(machine.keys[i].file);
        for (UINT32 i = 0; i < user.count; i++)
            user.keys[i].file->lpVtbl->Release(user.keys[i].file);
        DwFree(machine.keys);
        DwFree(user.keys);
    }
    if (SUCCEEDED(hr)) {
        *out = factory->system_set;
        set_addref(*out);
    } else {
        *out = nullptr;
    }
    ReleaseSRWLockExclusive(&factory->lock);
    return hr;
}

/* ------------------------------------------------------------------ font set builders */

typedef struct DwSetBuilder {
    const IDWriteFontSetBuilder2Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwSetFont **fonts;
    UINT32 count, capacity;
} DwSetBuilder;

static DwSetBuilder *builder_of(void *b) { return (DwSetBuilder *)b; }

static HRESULT WINAPI builder_query(IDWriteFontSetBuilder2 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontSetBuilder) ||
        IsEqualGUID(riid, &IID_IDWriteFontSetBuilder1) || IsEqualGUID(riid, &IID_IDWriteFontSetBuilder2)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI builder_addref(IDWriteFontSetBuilder2 *This) { return InterlockedIncrement(&builder_of(This)->refs); }

static ULONG WINAPI builder_release(IDWriteFontSetBuilder2 *This)
{
    DwSetBuilder *b = builder_of(This);
    ULONG refs = InterlockedDecrement(&b->refs);
    if (!refs) {
        for (UINT32 i = 0; i < b->count; i++)
            set_font_release(b->fonts[i]);
        DwFree(b->fonts);
        DwFree(b);
    }
    return refs;
}

/* takes the font's reference. */
static HRESULT builder_push(DwSetBuilder *b, DwSetFont *font)
{
    if (b->count == b->capacity) {
        UINT32 capacity = b->capacity ? b->capacity * 2 : 16;
        DwSetFont **grown = DwAlloc(capacity * sizeof(*grown));
        if (!grown) {
            set_font_release(font);
            return E_OUTOFMEMORY;
        }
        if (b->count)
            memcpy(grown, b->fonts, b->count * sizeof(*grown));
        DwFree(b->fonts);
        b->fonts = grown;
        b->capacity = capacity;
    }
    b->fonts[b->count++] = font;
    return S_OK;
}

static HRESULT check_properties(const DWRITE_FONT_PROPERTY *properties, UINT32 count)
{
    if (count && !properties)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < count; i++) {
        if (properties[i].propertyId == DWRITE_FONT_PROPERTY_ID_NONE ||
            (unsigned)properties[i].propertyId >= DWRITE_FONT_PROPERTY_ID_TOTAL_RS3)
            return E_INVALIDARG;
    }
    return S_OK;
}

static HRESULT builder_add_key(DwSetBuilder *b, const DwFontKey *key, const DWRITE_FONT_PROPERTY *properties,
                               UINT32 property_count, const DWRITE_FONT_AXIS_RANGE *ranges, UINT32 range_count)
{
    HRESULT hr = check_properties(properties, property_count);
    if (FAILED(hr))
        return hr;
    DwSetFont *f = set_font_new(key, DWRITE_FONT_SOURCE_TYPE_UNKNOWN);
    if (!f)
        return E_OUTOFMEMORY;
    hr = set_font_add_custom(f, properties, property_count);
    if (FAILED(hr)) {
        set_font_release(f);
        return hr;
    }
    f->range_count = range_count < DW_MAX_AXES ? range_count : DW_MAX_AXES;
    if (f->range_count)
        memcpy(f->ranges, ranges, f->range_count * sizeof(*ranges));
    return builder_push(b, f);
}

static HRESULT WINAPI builder_add_reference_properties(IDWriteFontSetBuilder2 *This,
                                                       IDWriteFontFaceReference *reference,
                                                       const DWRITE_FONT_PROPERTY *properties, UINT32 count)
{
    DwFontKey key;
    if (!DwFaceReferenceKey(reference, &key))
        return E_INVALIDARG;
    return builder_add_key(builder_of(This), &key, properties, count, nullptr, 0);
}

static HRESULT WINAPI builder_add_reference(IDWriteFontSetBuilder2 *This, IDWriteFontFaceReference *reference)
{
    return builder_add_reference_properties(This, reference, nullptr, 0);
}

static HRESULT WINAPI builder_add_set(IDWriteFontSetBuilder2 *This, IDWriteFontSet *set)
{
    DwSetBuilder *b = builder_of(This);
    if (!set)
        return E_INVALIDARG;
    DwSetFont *const *fonts;
    UINT32 count;
    if (DwFontSetFonts(set, &fonts, &count)) {
        for (UINT32 i = 0; i < count; i++) {
            set_font_addref(fonts[i]);
            HRESULT hr = builder_push(b, fonts[i]);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    }
    count = set->lpVtbl->GetFontCount(set);
    for (UINT32 i = 0; i < count; i++) {
        IDWriteFontFaceReference *reference;
        HRESULT hr = set->lpVtbl->GetFontFaceReference(set, i, &reference);
        if (FAILED(hr))
            return hr;
        hr = builder_add_reference(This, reference);
        reference->lpVtbl->Release(reference);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT WINAPI builder_create(IDWriteFontSetBuilder2 *This, IDWriteFontSet **set)
{
    DwSetBuilder *b = builder_of(This);
    if (!set)
        return E_POINTER;
    return set_create(b->factory, b->fonts, b->count, (IDWriteFontSet4 **)set);
}

static HRESULT WINAPI builder_add_file(IDWriteFontSetBuilder2 *This, IDWriteFontFile *file)
{
    if (!file)
        return E_INVALIDARG;
    BOOL supported;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    UINT32 faces;
    HRESULT hr = file->lpVtbl->Analyze(file, &supported, &file_type, &face_type, &faces);
    if (FAILED(hr))
        return hr;
    if (!supported)
        return DWRITE_E_FILEFORMAT;
    for (UINT32 i = 0; i < faces && SUCCEEDED(hr); i++) {
        DwFontKey key = {file, i};
        hr = builder_add_key(builder_of(This), &key, nullptr, 0, nullptr, 0);
    }
    return hr;
}

static HRESULT WINAPI builder_add_font(IDWriteFontSetBuilder2 *This, IDWriteFontFile *file, UINT32 index,
                                       DWRITE_FONT_SIMULATIONS simulations, const DWRITE_FONT_AXIS_VALUE *values,
                                       UINT32 value_count, const DWRITE_FONT_AXIS_RANGE *ranges, UINT32 range_count,
                                       const DWRITE_FONT_PROPERTY *properties, UINT32 property_count)
{
    if (!file || (value_count && !values) || (range_count && !ranges) ||
        (simulations & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE)))
        return E_INVALIDARG;
    for (UINT32 i = 0; i < range_count; i++) {
        if (ranges[i].minValue > ranges[i].maxValue)
            return E_INVALIDARG;
    }
    DwFontKey key = {file, index, simulations};
    copy_axes(&key, values, value_count);
    return builder_add_key(builder_of(This), &key, properties, property_count, ranges, range_count);
}

static HRESULT WINAPI builder_add_path(IDWriteFontSetBuilder2 *This, const WCHAR *path)
{
    if (!path)
        return E_INVALIDARG;
    IDWriteFontFile *file;
    HRESULT hr = DwFontFileFromPath(path, nullptr, &file);
    if (FAILED(hr))
        return hr;
    hr = builder_add_file(This, file);
    file->lpVtbl->Release(file);
    return hr;
}

static const IDWriteFontSetBuilder2Vtbl g_builder_vtbl = {
    builder_query,          builder_addref,   builder_release,  builder_add_reference_properties,
    builder_add_reference,  builder_add_set,  builder_create,   builder_add_file,
    builder_add_font,       builder_add_path,
};

HRESULT DwFontSetBuilderCreate(DwFactory *factory, IDWriteFontSetBuilder2 **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DwSetBuilder *b = DwAllocZero(sizeof(*b));
    if (!b)
        return E_OUTOFMEMORY;
    b->lpVtbl = &g_builder_vtbl;
    b->refs = 1;
    b->factory = factory;
    *out = (IDWriteFontSetBuilder2 *)b;
    return S_OK;
}

/* ------------------------------------------------------------------ font resources */

typedef struct DwStatValue {
    UINT32 axis;
    FLOAT min, max;
    UINT32 name_id;
} DwStatValue;

typedef struct DwResource {
    const IDWriteFontResourceVtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    IDWriteFontFile *file;
    UINT32 index;
    DwFontData *data;
    UINT32 axis_count;
    DwAxis axes[DW_MAX_AXES];
    UINT32 value_count;
    DwStatValue *values;
} DwResource;

static DwResource *resource_of(void *r) { return (DwResource *)r; }

static HRESULT WINAPI resource_query(IDWriteFontResource *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontResource)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI resource_addref(IDWriteFontResource *This) { return InterlockedIncrement(&resource_of(This)->refs); }

static ULONG WINAPI resource_release(IDWriteFontResource *This)
{
    DwResource *r = resource_of(This);
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        r->file->lpVtbl->Release(r->file);
        DwFontDataRelease(r->data);
        DwFree(r->values);
        DwFree(r);
    }
    return refs;
}

static HRESULT WINAPI resource_file(IDWriteFontResource *This, IDWriteFontFile **file)
{
    if (!file)
        return E_POINTER;
    *file = resource_of(This)->file;
    (*file)->lpVtbl->AddRef(*file);
    return S_OK;
}

static UINT32 WINAPI resource_index(IDWriteFontResource *This) { return resource_of(This)->index; }
static UINT32 WINAPI resource_axis_count(IDWriteFontResource *This) { return resource_of(This)->axis_count; }

static HRESULT WINAPI resource_defaults(IDWriteFontResource *This, DWRITE_FONT_AXIS_VALUE *values, UINT32 count)
{
    DwResource *r = resource_of(This);
    if (count < r->axis_count || (r->axis_count && !values))
        return E_NOT_SUFFICIENT_BUFFER;
    for (UINT32 i = 0; i < r->axis_count; i++) {
        values[i].axisTag = (DWRITE_FONT_AXIS_TAG)r->axes[i].tag;
        values[i].value = r->axes[i].def;
    }
    return S_OK;
}

static HRESULT WINAPI resource_ranges(IDWriteFontResource *This, DWRITE_FONT_AXIS_RANGE *ranges, UINT32 count)
{
    DwResource *r = resource_of(This);
    if (count < r->axis_count || (r->axis_count && !ranges))
        return E_NOT_SUFFICIENT_BUFFER;
    for (UINT32 i = 0; i < r->axis_count; i++) {
        ranges[i].axisTag = (DWRITE_FONT_AXIS_TAG)r->axes[i].tag;
        ranges[i].minValue = r->axes[i].min;
        ranges[i].maxValue = r->axes[i].max;
    }
    return S_OK;
}

static DWRITE_FONT_AXIS_ATTRIBUTES WINAPI resource_axis_attributes(IDWriteFontResource *This, UINT32 axis)
{
    DwResource *r = resource_of(This);
    if (axis >= r->axis_count)
        return DWRITE_FONT_AXIS_ATTRIBUTES_NONE;
    UINT32 a = r->axes[axis].min < r->axes[axis].max ? DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE : 0;
    if (r->axes[axis].hidden)
        a |= DWRITE_FONT_AXIS_ATTRIBUTES_HIDDEN;
    return (DWRITE_FONT_AXIS_ATTRIBUTES)a;
}

static HRESULT name_strings(DwResource *r, UINT32 name_id, IDWriteLocalizedStrings **names)
{
    DwStrings *strings = name_id ? DwFontDataNames(r->data, name_id) : nullptr;
    if (!strings)
        strings = DwStringsNew();
    if (!strings)
        return E_OUTOFMEMORY;
    *names = DwStringsInterface(strings);
    return S_OK;
}

static HRESULT WINAPI resource_axis_names(IDWriteFontResource *This, UINT32 axis, IDWriteLocalizedStrings **names)
{
    DwResource *r = resource_of(This);
    if (!names)
        return E_POINTER;
    *names = nullptr;
    if (axis >= r->axis_count)
        return E_INVALIDARG;
    return name_strings(r, r->axes[axis].name_id, names);
}

static UINT32 WINAPI resource_value_count(IDWriteFontResource *This, UINT32 axis)
{
    DwResource *r = resource_of(This);
    UINT32 n = 0;
    for (UINT32 i = 0; i < r->value_count; i++)
        n += r->values[i].axis == axis;
    return n;
}

static HRESULT WINAPI resource_value_names(IDWriteFontResource *This, UINT32 axis, UINT32 value,
                                           DWRITE_FONT_AXIS_RANGE *range, IDWriteLocalizedStrings **names)
{
    DwResource *r = resource_of(This);
    if (!names)
        return E_POINTER;
    *names = nullptr;
    if (range)
        memset(range, 0, sizeof(*range));
    if (axis >= r->axis_count)
        return E_INVALIDARG;
    for (UINT32 i = 0; i < r->value_count; i++) {
        if (r->values[i].axis != axis || value--)
            continue;
        if (range) {
            range->axisTag = (DWRITE_FONT_AXIS_TAG)r->axes[axis].tag;
            range->minValue = r->values[i].min;
            range->maxValue = r->values[i].max;
        }
        return name_strings(r, r->values[i].name_id, names);
    }
    return E_INVALIDARG;
}

static BOOL WINAPI resource_has_variations(IDWriteFontResource *This)
{
    DwResource *r = resource_of(This);
    for (UINT32 i = 0; i < r->axis_count; i++) {
        if (r->axes[i].min < r->axes[i].max)
            return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI resource_create_face(IDWriteFontResource *This, DWRITE_FONT_SIMULATIONS simulations,
                                           const DWRITE_FONT_AXIS_VALUE *values, UINT32 count, IDWriteFontFace5 **face)
{
    DwResource *r = resource_of(This);
    if (!face)
        return E_POINTER;
    *face = nullptr;
    if (count && !values)
        return E_INVALIDARG;
    return DwFaceCreateWithAxes(r->factory, r->file, r->index, simulations, values, count, (IDWriteFontFace **)face);
}

static HRESULT WINAPI resource_create_reference(IDWriteFontResource *This, DWRITE_FONT_SIMULATIONS simulations,
                                                const DWRITE_FONT_AXIS_VALUE *values, UINT32 count,
                                                IDWriteFontFaceReference1 **reference)
{
    DwResource *r = resource_of(This);
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    if (count && !values)
        return E_INVALIDARG;
    DwFontKey key = {r->file, r->index, simulations};
    copy_axes(&key, values, count);
    return DwFaceReferenceCreate(r->factory, &key, reference);
}

static const IDWriteFontResourceVtbl g_resource_vtbl = {
    resource_query,       resource_addref,         resource_release,    resource_file,
    resource_index,       resource_axis_count,     resource_defaults,   resource_ranges,
    resource_axis_attributes, resource_axis_names, resource_value_count, resource_value_names,
    resource_has_variations, resource_create_face, resource_create_reference,
};

static FLOAT fixed_value(const BYTE *p) { return (FLOAT)(INT32)DwBe32(p) / 65536.0f; }

/* the axis values that STAT names, those of formats one to three. */
static void read_stat(DwResource *r)
{
    UINT32 size;
    const BYTE *stat = DwFontDataTable(r->data, DW_TAG('S', 'T', 'A', 'T'), &size);
    if (!stat || size < 18)
        return;
    UINT32 record_size = DwBe16(stat + 4), design_count = DwBe16(stat + 6), design_offset = DwBe32(stat + 8);
    UINT32 value_count = DwBe16(stat + 12), values_offset = DwBe32(stat + 14);
    if (record_size < 8 || design_offset > size || (UINT64)design_count * record_size > size - design_offset ||
        values_offset > size || 2ull * value_count > size - values_offset)
        return;
    r->values = DwAllocZero((value_count + 1) * sizeof(*r->values));
    if (!r->values)
        return;
    for (UINT32 i = 0; i < value_count; i++) {
        UINT32 offset = values_offset + DwBe16(stat + values_offset + 2 * i);
        if (offset > size - 12)
            continue;
        const BYTE *v = stat + offset;
        UINT32 format = DwBe16(v), design = DwBe16(v + 2);
        if (design >= design_count)
            continue;
        UINT32 tag = DwBe32(stat + design_offset + design * record_size);
        UINT32 le = ((tag & 0xff) << 24) | ((tag & 0xff00) << 8) | ((tag >> 8) & 0xff00) | (tag >> 24);
        UINT32 axis = r->axis_count;
        for (UINT32 a = 0; a < r->axis_count; a++) {
            if (r->axes[a].tag == le)
                axis = a;
        }
        if (axis == r->axis_count)
            continue;
        DwStatValue *out = &r->values[r->value_count];
        out->axis = axis;
        out->name_id = DwBe16(v + 6);
        if (format == 1 || format == 3) {
            out->min = out->max = fixed_value(v + 8);
        } else if (format == 2 && offset <= size - 20) {
            out->min = fixed_value(v + 12);
            out->max = fixed_value(v + 16);
        } else {
            continue;
        }
        r->value_count++;
    }
}

HRESULT DwFontResourceCreate(DwFactory *factory, IDWriteFontFile *file, UINT32 index, IDWriteFontResource **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!file)
        return E_INVALIDARG;
    DwFontData *data;
    HRESULT hr = DwFontDataOpen(file, index, &data);
    if (FAILED(hr))
        return hr;
    DwResource *r = DwAllocZero(sizeof(*r));
    if (!r) {
        DwFontDataRelease(data);
        return E_OUTOFMEMORY;
    }
    r->lpVtbl = &g_resource_vtbl;
    r->refs = 1;
    r->factory = factory;
    r->file = file;
    file->lpVtbl->AddRef(file);
    r->index = index;
    r->data = data;
    r->axis_count = DwFontDataAxes(data, r->axes, DW_MAX_AXES);
    read_stat(r);
    *out = (IDWriteFontResource *)r;
    return S_OK;
}

/* ------------------------------------------------------------------ the download queue */

typedef struct DwDownloadQueue {
    const IDWriteFontDownloadQueueVtbl *lpVtbl;
    LONG refs;
    SRWLOCK lock;
    IDWriteFontDownloadListener *listeners[32];
} DwDownloadQueue;

static DwDownloadQueue *queue_of(void *q) { return (DwDownloadQueue *)q; }

static HRESULT WINAPI queue_query(IDWriteFontDownloadQueue *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontDownloadQueue)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI queue_addref(IDWriteFontDownloadQueue *This) { return InterlockedIncrement(&queue_of(This)->refs); }

static ULONG WINAPI queue_release(IDWriteFontDownloadQueue *This)
{
    DwDownloadQueue *q = queue_of(This);
    ULONG refs = InterlockedDecrement(&q->refs);
    if (!refs) {
        for (UINT32 i = 0; i < 32; i++) {
            if (q->listeners[i])
                q->listeners[i]->lpVtbl->Release(q->listeners[i]);
        }
        DwFree(q);
    }
    return refs;
}

static HRESULT WINAPI queue_add_listener(IDWriteFontDownloadQueue *This, IDWriteFontDownloadListener *listener,
                                         UINT32 *token)
{
    DwDownloadQueue *q = queue_of(This);
    if (!listener || !token)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&q->lock);
    HRESULT hr = E_OUTOFMEMORY;
    for (UINT32 i = 0; i < 32; i++) {
        if (!q->listeners[i]) {
            q->listeners[i] = listener;
            listener->lpVtbl->AddRef(listener);
            *token = i;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&q->lock);
    return hr;
}

static HRESULT WINAPI queue_remove_listener(IDWriteFontDownloadQueue *This, UINT32 token)
{
    DwDownloadQueue *q = queue_of(This);
    if (token >= 32)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&q->lock);
    IDWriteFontDownloadListener *listener = q->listeners[token];
    q->listeners[token] = nullptr;
    ReleaseSRWLockExclusive(&q->lock);
    if (!listener)
        return E_INVALIDARG;
    listener->lpVtbl->Release(listener);
    return S_OK;
}

static BOOL WINAPI queue_empty(IDWriteFontDownloadQueue *This) { return TRUE; }

/* nothing is ever queued: every font is local. */
static HRESULT WINAPI queue_begin(IDWriteFontDownloadQueue *This, IUnknown *context) { return S_FALSE; }
static HRESULT WINAPI queue_cancel(IDWriteFontDownloadQueue *This) { return S_OK; }
static UINT64 WINAPI queue_generation(IDWriteFontDownloadQueue *This) { return 0; }

static const IDWriteFontDownloadQueueVtbl g_queue_vtbl = {
    queue_query,  queue_addref, queue_release,    queue_add_listener, queue_remove_listener,
    queue_empty,  queue_begin,  queue_cancel,     queue_generation,
};

HRESULT DwFontDownloadQueueCreate(IDWriteFontDownloadQueue **out)
{
    DwDownloadQueue *q = DwAllocZero(sizeof(*q));
    if (!q)
        return E_OUTOFMEMORY;
    q->lpVtbl = &g_queue_vtbl;
    q->refs = 1;
    InitializeSRWLock(&q->lock);
    *out = (IDWriteFontDownloadQueue *)q;
    return S_OK;
}
