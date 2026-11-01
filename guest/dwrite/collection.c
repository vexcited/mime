/*
 * font collections, from the fonts directory or a loader's files, and the
 * family, font and font list objects they hand out.
 *
 * like DirectWrite, a typographic family holds every face that shares its
 * name. a face whose win32 family name differs is also listed under that
 * name, so "Segoe UI Light" is found the way GDI names it. families without
 * bold or italic faces get simulated ones.
 */
#include "dw_private.h"

/* ------------------------------------------------------------------ matching */

static int stretch_rank(int request, int stretch)
{
    int d = stretch - request;
    if (request <= DWRITE_FONT_STRETCH_NORMAL)
        return d <= 0 ? -d : 100 + d;
    return d >= 0 ? d : 100 - d;
}

static int style_rank(DWRITE_FONT_STYLE request, DWRITE_FONT_STYLE style)
{
    static const int ranks[3][3] = {{0, 1, 2}, {2, 0, 1}, {2, 1, 0}};
    if ((unsigned)request > 2 || (unsigned)style > 2)
        return 3;
    return ranks[request][style];
}

static int weight_rank(int request, int w)
{
    if (request == 400 || request == 500) {
        int other = request == 400 ? 500 : 400;
        if (w == request)
            return 0;
        if (w == other)
            return 1;
        return w < 400 ? 2 + (400 - w) : 1000 + (w - 500);
    }
    if (request < 400)
        return w <= request ? request - w : 1000 + (w - request);
    return w >= request ? w - request : 1000 + (request - w);
}

typedef struct Key {
    int stretch, style, weight, simulated;
} Key;

static Key key_of(const DwEntry *e, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style)
{
    Key k;
    k.stretch = stretch_rank(stretch, e->attributes.stretch);
    k.style = style_rank(style, e->attributes.style);
    k.weight = weight_rank(weight, e->attributes.weight);
    k.simulated = e->simulations != DWRITE_FONT_SIMULATIONS_NONE;
    return k;
}

static int key_compare(Key a, Key b)
{
    if (a.stretch != b.stretch)
        return a.stretch < b.stretch ? -1 : 1;
    if (a.style != b.style)
        return a.style < b.style ? -1 : 1;
    if (a.weight != b.weight)
        return a.weight < b.weight ? -1 : 1;
    if (a.simulated != b.simulated)
        return a.simulated < b.simulated ? -1 : 1;
    return 0;
}

UINT32 DwBestEntry(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style)
{
    UINT32 best = 0;
    Key best_key = {};
    for (UINT32 i = 0; i < family->count; i++) {
        Key k = key_of(&family->entries[i], weight, stretch, style);
        if (!i || key_compare(k, best_key) < 0) {
            best = i;
            best_key = k;
        }
    }
    return best;
}

void DwSortEntries(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style, UINT32 *order)
{
    Key *keys = DwAlloc(family->count * sizeof(*keys));
    for (UINT32 i = 0; i < family->count; i++) {
        order[i] = i;
        if (keys)
            keys[i] = key_of(&family->entries[i], weight, stretch, style);
    }
    if (keys) {
        for (UINT32 i = 1; i < family->count; i++) {
            UINT32 v = order[i];
            UINT32 j = i;
            while (j > 0 && key_compare(keys[v], keys[order[j - 1]]) < 0) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = v;
        }
    }
    DwFree(keys);
}

/* ------------------------------------------------------------------ building */

typedef struct Builder {
    DwFamily *families;
    UINT32 count, capacity;
} Builder;

static DwFamily *family_for(Builder *b, DwStrings *names)
{
    const WCHAR *key = DwStringsBest(names, L"en-us");
    if (!key)
        return nullptr;
    for (UINT32 i = 0; i < b->count; i++) {
        const WCHAR *other = DwStringsBest(b->families[i].names, L"en-us");
        if (other && !DwCompareNames(key, other)) {
            for (UINT32 l = 0; l < DwStringsCount(names); l++) {
                const DwStringEntry *e = DwStringsAt(names, l);
                DwStringsAdd(b->families[i].names, e->locale, (UINT32)wcslen(e->locale), e->text,
                             (UINT32)wcslen(e->text));
            }
            return &b->families[i];
        }
    }
    if (b->count == b->capacity) {
        UINT32 capacity = b->capacity ? b->capacity * 2 : 64;
        DwFamily *families = DwAllocZero(capacity * sizeof(*families));
        if (!families)
            return nullptr;
        if (b->count)
            memcpy(families, b->families, b->count * sizeof(*families));
        DwFree(b->families);
        b->families = families;
        b->capacity = capacity;
    }
    DwFamily *f = &b->families[b->count];
    memset(f, 0, sizeof(*f));
    f->names = DwStringsCopy(names);
    if (!f->names)
        return nullptr;
    b->count++;
    return f;
}

static void entry_retain(DwEntry *e)
{
    e->file->lpVtbl->AddRef(e->file);
    if (e->face_names)
        DwStringsAddRef(e->face_names);
    if (e->win32_family)
        DwStringsAddRef(e->win32_family);
    if (e->win32_subfamily)
        DwStringsAddRef(e->win32_subfamily);
}

static void entry_release(DwEntry *e)
{
    e->file->lpVtbl->Release(e->file);
    DwStringsRelease(e->face_names);
    DwStringsRelease(e->win32_family);
    DwStringsRelease(e->win32_subfamily);
}

static BOOL push_entry(DwFamily *f, const DwEntry *entry)
{
    if (f->count == f->capacity) {
        UINT32 capacity = f->capacity ? f->capacity * 2 : 4;
        DwEntry *entries = DwAlloc(capacity * sizeof(*entries));
        if (!entries)
            return FALSE;
        if (f->count)
            memcpy(entries, f->entries, f->count * sizeof(*entries));
        DwFree(f->entries);
        f->entries = entries;
        f->capacity = capacity;
    }
    f->entries[f->count] = *entry;
    entry_retain(&f->entries[f->count]);
    f->count++;
    return TRUE;
}

static DwStrings *simulated_name(const DwStrings *base, DWRITE_FONT_SIMULATIONS simulations)
{
    const WCHAR *regular = DwStringsBest(base, L"en-us");
    const WCHAR *suffix = simulations == (DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE)
                              ? L"Bold Oblique"
                          : simulations == DWRITE_FONT_SIMULATIONS_BOLD ? L"Bold"
                                                                        : L"Oblique";
    WCHAR text[160];
    if (!regular || !_wcsicmp(regular, L"Regular") || !_wcsicmp(regular, L"Normal") ||
        (!_wcsicmp(regular, L"Bold") && (simulations & DWRITE_FONT_SIMULATIONS_BOLD)) || wcslen(regular) > 100)
        wcscpy(text, suffix);
    else {
        wcscpy(text, regular);
        wcscat(text, L" ");
        wcscat(text, suffix);
    }
    DwStrings *names = DwStringsNew();
    if (names)
        DwStringsAdd(names, L"en-us", 5, text, (UINT32)wcslen(text));
    return names;
}

/* bold is simulated from the regular weights, oblique where no face is slanted. */
static void add_simulations(DwFamily *f)
{
    UINT32 real = f->count;
    for (int stretch = 1; stretch <= 9; stretch++) {
        for (int style = DWRITE_FONT_STYLE_NORMAL; style <= DWRITE_FONT_STYLE_ITALIC; style++) {
            BOOL has_bold = FALSE;
            UINT32 base = real;
            int base_distance = 0x7fffffff;
            for (UINT32 i = 0; i < real; i++) {
                const DwEntry *e = &f->entries[i];
                if (e->attributes.stretch != stretch || e->attributes.style != style)
                    continue;
                if (e->attributes.weight >= 600)
                    has_bold = TRUE;
                int d = e->attributes.weight - 400;
                d = d < 0 ? -d : d;
                if (e->attributes.weight <= 500 && d < base_distance) {
                    base = i;
                    base_distance = d;
                }
            }
            if (base < real && !has_bold) {
                DwEntry sim = f->entries[base];
                sim.attributes.weight = DWRITE_FONT_WEIGHT_BOLD;
                sim.simulations = DWRITE_FONT_SIMULATIONS_BOLD;
                sim.face_names = simulated_name(f->entries[base].face_names, sim.simulations);
                push_entry(f, &sim);
                DwStringsRelease(sim.face_names);
            }
        }
        BOOL has_slant = FALSE, any = FALSE;
        for (UINT32 i = 0; i < f->count; i++) {
            if (f->entries[i].attributes.stretch != stretch)
                continue;
            any = TRUE;
            if (f->entries[i].attributes.style != DWRITE_FONT_STYLE_NORMAL)
                has_slant = TRUE;
        }
        if (!any || has_slant)
            continue;
        UINT32 present = f->count;
        for (UINT32 i = 0; i < present; i++) {
            if (f->entries[i].attributes.stretch != stretch)
                continue;
            DwEntry sim = f->entries[i];
            sim.attributes.style = DWRITE_FONT_STYLE_OBLIQUE;
            sim.simulations |= DWRITE_FONT_SIMULATIONS_OBLIQUE;
            sim.face_names = simulated_name(f->entries[i].face_names, sim.simulations);
            push_entry(f, &sim);
            DwStringsRelease(sim.face_names);
        }
    }
}

static int entry_order(const DwEntry *a, const DwEntry *b)
{
    if (a->attributes.stretch != b->attributes.stretch)
        return a->attributes.stretch < b->attributes.stretch ? -1 : 1;
    if (a->attributes.weight != b->attributes.weight)
        return a->attributes.weight < b->attributes.weight ? -1 : 1;
    if (a->attributes.style != b->attributes.style)
        return a->attributes.style < b->attributes.style ? -1 : 1;
    if (a->simulations != b->simulations)
        return a->simulations < b->simulations ? -1 : 1;
    return 0;
}

static void sort_entries(DwFamily *f)
{
    for (UINT32 i = 1; i < f->count; i++) {
        DwEntry v = f->entries[i];
        UINT32 j = i;
        while (j > 0 && entry_order(&v, &f->entries[j - 1]) < 0) {
            f->entries[j] = f->entries[j - 1];
            j--;
        }
        f->entries[j] = v;
    }
}

static void add_file(Builder *b, IDWriteFontFile *file)
{
    BOOL supported;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    UINT32 faces;
    if (FAILED(file->lpVtbl->Analyze(file, &supported, &file_type, &face_type, &faces)) || !supported)
        return;
    for (UINT32 index = 0; index < faces; index++) {
        DwFontData *data;
        if (FAILED(DwFontDataOpen(file, index, &data)))
            continue;
        DwEntry entry = {};
        entry.file = file;
        entry.face_index = index;
        entry.face_type = face_type;
        DwFontDataAttributes(data, &entry.attributes);
        DwFontDataMetrics(data, &entry.metrics);
        DwStrings *wws = DwFontDataNames(data, 21);
        DwStrings *typographic = DwFontDataNames(data, 16);
        DwStrings *win32 = DwFontDataNames(data, 1);
        DwStrings *wws_sub = DwFontDataNames(data, 22);
        DwStrings *typographic_sub = DwFontDataNames(data, 17);
        DwStrings *win32_sub = DwFontDataNames(data, 2);
        entry.win32_family = win32;
        entry.win32_subfamily = win32_sub;
        entry.face_names = wws_sub ? wws_sub : typographic_sub ? typographic_sub : win32_sub;
        DwStrings *family_names = wws ? wws : typographic ? typographic : win32;
        if (family_names) {
            DwFamily *f = family_for(b, family_names);
            if (f)
                push_entry(f, &entry);
            /* a face whose win32 name differs from its bigger family also gets a family of its own. */
            if (win32 && family_names != win32 && DwCompareNames(DwStringsBest(win32, L"en-us"),
                                                                  DwStringsBest(family_names, L"en-us"))) {
                DwFamily *g = family_for(b, win32);
                if (g) {
                    DwEntry gdi = entry;
                    gdi.gdi_family = TRUE;
                    gdi.face_names = win32_sub ? win32_sub : entry.face_names;
                    push_entry(g, &gdi);
                }
            }
        }
        /* the entries took references of their own. */
        DwStringsRelease(wws);
        DwStringsRelease(typographic);
        DwStringsRelease(win32);
        DwStringsRelease(wws_sub);
        DwStringsRelease(typographic_sub);
        DwStringsRelease(win32_sub);
        DwFontDataRelease(data);
    }
}

static void sort_families(Builder *b)
{
    for (UINT32 i = 1; i < b->count; i++) {
        DwFamily v = b->families[i];
        UINT32 j = i;
        while (j > 0 &&
               DwCompareNames(DwStringsBest(v.names, L"en-us"), DwStringsBest(b->families[j - 1].names, L"en-us")) < 0) {
            b->families[j] = b->families[j - 1];
            j--;
        }
        b->families[j] = v;
    }
}

/* ------------------------------------------------------------------ collection object */

static HRESULT WINAPI collection_query(IDWriteFontCollection *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontCollection)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI collection_addref(IDWriteFontCollection *This)
{
    return InterlockedIncrement(&((DwCollection *)This)->refs);
}

static ULONG WINAPI collection_release(IDWriteFontCollection *This)
{
    DwCollection *c = (DwCollection *)This;
    ULONG refs = InterlockedDecrement(&c->refs);
    if (!refs) {
        for (UINT32 i = 0; i < c->count; i++) {
            for (UINT32 e = 0; e < c->families[i].count; e++)
                entry_release(&c->families[i].entries[e]);
            DwFree(c->families[i].entries);
            DwStringsRelease(c->families[i].names);
        }
        DwFree(c->families);
        DwFree(c);
    }
    return refs;
}

static UINT32 WINAPI collection_count(IDWriteFontCollection *This) { return ((DwCollection *)This)->count; }

static HRESULT WINAPI collection_family(IDWriteFontCollection *This, UINT32 index, IDWriteFontFamily **family)
{
    if (!family)
        return E_POINTER;
    *family = nullptr;
    DwCollection *c = (DwCollection *)This;
    if (index >= c->count)
        return E_INVALIDARG;
    return DwFamilyCreate(c, index, family);
}

BOOL DwFindFamily(const DwCollection *c, const WCHAR *name, UINT32 *index)
{
    for (UINT32 i = 0; i < c->count; i++) {
        const DwStrings *names = c->families[i].names;
        for (UINT32 l = 0; l < DwStringsCount(names); l++) {
            if (!DwCompareNames(DwStringsAt(names, l)->text, name)) {
                *index = i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL DwFindFamilyExact(const DwCollection *c, const WCHAR *name, UINT32 *index)
{
    for (UINT32 i = 0; i < c->count; i++) {
        if (c->families[i].count && c->families[i].entries[0].gdi_family)
            continue;
        const DwStrings *names = c->families[i].names;
        for (UINT32 l = 0; l < DwStringsCount(names); l++) {
            if (!DwCompareNames(DwStringsAt(names, l)->text, name)) {
                *index = i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static HRESULT WINAPI collection_find(IDWriteFontCollection *This, const WCHAR *name, UINT32 *index, BOOL *exists)
{
    if (!index || !exists)
        return E_POINTER;
    *index = (UINT32)-1;
    *exists = FALSE;
    if (!name)
        return E_INVALIDARG;
    UINT32 found;
    if (DwFindFamily((DwCollection *)This, name, &found)) {
        *index = found;
        *exists = TRUE;
    }
    return S_OK;
}

BOOL DwFindEntry(const DwCollection *c, IDWriteFontFace *face, UINT32 *family, UINT32 *entry)
{
    DwFace *f = DwFaceFrom(face);
    if (!f)
        return FALSE;
    const void *key;
    UINT32 key_size;
    if (FAILED(f->file->lpVtbl->GetReferenceKey(f->file, &key, &key_size)))
        return FALSE;
    BOOL found = FALSE;
    for (int pass = 0; pass < 2 && !found; pass++) {
        for (UINT32 i = 0; i < c->count && !found; i++) {
            for (UINT32 e = 0; e < c->families[i].count && !found; e++) {
                const DwEntry *entry_i = &c->families[i].entries[e];
                if (entry_i->face_index != f->index || entry_i->gdi_family != (pass == 1))
                    continue;
                if (entry_i->simulations != f->simulations)
                    continue;
                const void *other;
                UINT32 other_size;
                if (SUCCEEDED(entry_i->file->lpVtbl->GetReferenceKey(entry_i->file, &other, &other_size)) &&
                    other_size == key_size && !memcmp(other, key, key_size)) {
                    *family = i;
                    *entry = e;
                    found = TRUE;
                }
            }
        }
    }
    return found;
}

static HRESULT WINAPI collection_from_face(IDWriteFontCollection *This, IDWriteFontFace *face, IDWriteFont **font)
{
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (!face)
        return E_INVALIDARG;
    UINT32 family, entry;
    if (!DwFindEntry((DwCollection *)This, face, &family, &entry))
        return DWRITE_E_NOFONT;
    return DwFontCreate((DwCollection *)This, family, entry, font);
}

static const IDWriteFontCollectionVtbl g_collection_vtbl = {
    collection_query, collection_addref, collection_release, collection_count, collection_family, collection_find,
    collection_from_face,
};

HRESULT DwCollectionFromFiles(DwFactory *factory, IDWriteFontFile *const *files, UINT32 count,
                              IDWriteFontFileEnumerator *enumerator, DwCollection **out)
{
    Builder b = {};
    for (UINT32 i = 0; i < count; i++)
        add_file(&b, files[i]);
    HRESULT hr = S_OK;
    while (enumerator) {
        BOOL more = FALSE;
        hr = enumerator->lpVtbl->MoveNext(enumerator, &more);
        if (FAILED(hr) || !more)
            break;
        IDWriteFontFile *file;
        hr = enumerator->lpVtbl->GetCurrentFontFile(enumerator, &file);
        if (FAILED(hr))
            break;
        add_file(&b, file);
        file->lpVtbl->Release(file);
    }
    if (FAILED(hr)) {
        for (UINT32 i = 0; i < b.count; i++) {
            for (UINT32 e = 0; e < b.families[i].count; e++)
                entry_release(&b.families[i].entries[e]);
            DwFree(b.families[i].entries);
            DwStringsRelease(b.families[i].names);
        }
        DwFree(b.families);
        return hr;
    }
    for (UINT32 i = 0; i < b.count; i++) {
        add_simulations(&b.families[i]);
        sort_entries(&b.families[i]);
    }
    sort_families(&b);
    DwCollection *c = DwAllocZero(sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &g_collection_vtbl;
    c->refs = 1;
    c->factory = factory;
    c->families = b.families;
    c->count = b.count;
    InitializeSRWLock(&c->lock);
    *out = c;
    return S_OK;
}

/* ------------------------------------------------------------------ system collection */

static BOOL is_font_file(const WCHAR *name)
{
    size_t n = wcslen(name);
    return (n > 4 && (!_wcsicmp(name + n - 4, L".ttf") || !_wcsicmp(name + n - 4, L".ttc") ||
                      !_wcsicmp(name + n - 4, L".otf") || !_wcsicmp(name + n - 4, L".otc")));
}

HRESULT DwSystemCollection(DwFactory *factory, IDWriteFontCollection **out)
{
    AcquireSRWLockExclusive(&factory->lock);
    if (!factory->system) {
        WCHAR directory[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
        UINT n = GetWindowsDirectoryW(directory, MAX_PATH);
        IDWriteFontFile **files = nullptr;
        UINT32 count = 0, capacity = 0;
        if (n && n < MAX_PATH - 8) {
            wcscat(directory, L"\\Fonts\\");
            wcscpy(pattern, directory);
            wcscat(pattern, L"*");
            WIN32_FIND_DATAW find;
            HANDLE h = FindFirstFileW(pattern, &find);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !is_font_file(find.cFileName) ||
                        wcslen(directory) + wcslen(find.cFileName) >= MAX_PATH)
                        continue;
                    wcscpy(path, directory);
                    wcscat(path, find.cFileName);
                    IDWriteFontFile *file;
                    if (FAILED(DwFontFileFromPath(path, &find.ftLastWriteTime, &file)))
                        continue;
                    if (count == capacity) {
                        capacity = capacity ? capacity * 2 : 128;
                        IDWriteFontFile **grown = DwAlloc(capacity * sizeof(*grown));
                        if (!grown) {
                            file->lpVtbl->Release(file);
                            break;
                        }
                        if (count)
                            memcpy(grown, files, count * sizeof(*grown));
                        DwFree(files);
                        files = grown;
                    }
                    files[count++] = file;
                } while (FindNextFileW(h, &find));
                FindClose(h);
            }
        }
        DwCollection *c;
        HRESULT hr = DwCollectionFromFiles(factory, files, count, nullptr, &c);
        for (UINT32 i = 0; i < count; i++)
            files[i]->lpVtbl->Release(files[i]);
        DwFree(files);
        if (FAILED(hr)) {
            ReleaseSRWLockExclusive(&factory->lock);
            *out = nullptr;
            return hr;
        }
        factory->system = (IDWriteFontCollection *)c;
    }
    *out = factory->system;
    (*out)->lpVtbl->AddRef(*out);
    ReleaseSRWLockExclusive(&factory->lock);
    return S_OK;
}

DwCollection *DwCollectionOf(DwFactory *factory, IDWriteFontCollection *collection)
{
    if (collection) {
        /* collections of another implementation are not ours to read. */
        const IDWriteFontCollectionVtbl *vtbl = collection->lpVtbl;
        return vtbl == &g_collection_vtbl ? (DwCollection *)collection : nullptr;
    }
    IDWriteFontCollection *system;
    if (FAILED(DwSystemCollection(factory, &system)))
        return nullptr;
    system->lpVtbl->Release(system);
    return (DwCollection *)system;
}

/* ------------------------------------------------------------------ fonts */

typedef struct DwFont {
    const IDWriteFont1Vtbl *lpVtbl;
    LONG refs;
    DwCollection *collection;
    UINT32 family, entry;
} DwFont;

static const DwEntry *font_entry(const DwFont *f) { return &f->collection->families[f->family].entries[f->entry]; }

static HRESULT WINAPI font_query(IDWriteFont1 *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFont) ||
        IsEqualGUID(riid, &IID_IDWriteFont1)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI font_addref(IDWriteFont1 *This) { return InterlockedIncrement(&((DwFont *)This)->refs); }

static ULONG WINAPI font_release(IDWriteFont1 *This)
{
    DwFont *f = (DwFont *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        collection_release((IDWriteFontCollection *)f->collection);
        DwFree(f);
    }
    return refs;
}

static HRESULT WINAPI font_family(IDWriteFont1 *This, IDWriteFontFamily **family)
{
    DwFont *f = (DwFont *)This;
    if (!family)
        return E_POINTER;
    return DwFamilyCreate(f->collection, f->family, family);
}

static DWRITE_FONT_WEIGHT WINAPI font_weight(IDWriteFont1 *This) { return font_entry((DwFont *)This)->attributes.weight; }
static DWRITE_FONT_STRETCH WINAPI font_stretch(IDWriteFont1 *This)
{
    return font_entry((DwFont *)This)->attributes.stretch;
}
static DWRITE_FONT_STYLE WINAPI font_style(IDWriteFont1 *This) { return font_entry((DwFont *)This)->attributes.style; }
static BOOL WINAPI font_symbol(IDWriteFont1 *This) { return font_entry((DwFont *)This)->attributes.symbol; }

static HRESULT strings_out(DwStrings *strings, IDWriteLocalizedStrings **out)
{
    DwStringsAddRef(strings);
    *out = DwStringsInterface(strings);
    return S_OK;
}

static HRESULT WINAPI font_face_names(IDWriteFont1 *This, IDWriteLocalizedStrings **names)
{
    if (!names)
        return E_POINTER;
    *names = nullptr;
    const DwEntry *e = font_entry((DwFont *)This);
    if (!e->face_names)
        return DWRITE_E_NOFONT;
    return strings_out(e->face_names, names);
}

/* the name table id behind each informational string, 0 when there is none. */
static UINT32 name_id_of(DWRITE_INFORMATIONAL_STRING_ID id)
{
    switch (id) {
    case DWRITE_INFORMATIONAL_STRING_COPYRIGHT_NOTICE: return 0;
    case DWRITE_INFORMATIONAL_STRING_VERSION_STRINGS: return 5;
    case DWRITE_INFORMATIONAL_STRING_TRADEMARK: return 7;
    case DWRITE_INFORMATIONAL_STRING_MANUFACTURER: return 8;
    case DWRITE_INFORMATIONAL_STRING_DESIGNER: return 9;
    case DWRITE_INFORMATIONAL_STRING_DESIGNER_URL: return 12;
    case DWRITE_INFORMATIONAL_STRING_DESCRIPTION: return 10;
    case DWRITE_INFORMATIONAL_STRING_FONT_VENDOR_URL: return 11;
    case DWRITE_INFORMATIONAL_STRING_LICENSE_DESCRIPTION: return 13;
    case DWRITE_INFORMATIONAL_STRING_LICENSE_INFO_URL: return 14;
    case DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES: return 1;
    case DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES: return 2;
    case DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES: return 16;
    case DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_SUBFAMILY_NAMES: return 17;
    case DWRITE_INFORMATIONAL_STRING_SAMPLE_TEXT: return 19;
    case DWRITE_INFORMATIONAL_STRING_FULL_NAME: return 4;
    case DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME: return 6;
    case DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_CID_NAME: return 20;
    case DWRITE_INFORMATIONAL_STRING_WEIGHT_STRETCH_STYLE_FAMILY_NAME: return 21;
    default: return 0xffff;
    }
}

static HRESULT WINAPI font_informational(IDWriteFont1 *This, DWRITE_INFORMATIONAL_STRING_ID id,
                                         IDWriteLocalizedStrings **strings, BOOL *exists)
{
    if (!strings || !exists)
        return E_POINTER;
    *strings = nullptr;
    *exists = FALSE;
    UINT32 name_id = name_id_of(id);
    if (name_id == 0xffff || id == DWRITE_INFORMATIONAL_STRING_NONE)
        return S_OK;
    const DwEntry *e = font_entry((DwFont *)This);
    DwStrings *names;
    if (name_id == 1 && e->win32_family) {
        names = e->win32_family;
        DwStringsAddRef(names);
    } else if (name_id == 2 && e->win32_subfamily) {
        names = e->win32_subfamily;
        DwStringsAddRef(names);
    } else {
        DwFontData *data;
        if (FAILED(DwFontDataOpen(e->file, e->face_index, &data)))
            return S_OK;
        names = DwFontDataNames(data, name_id);
        DwFontDataRelease(data);
    }
    if (!names)
        return S_OK;
    *strings = DwStringsInterface(names);
    *exists = TRUE;
    return S_OK;
}

static DWRITE_FONT_SIMULATIONS WINAPI font_simulations(IDWriteFont1 *This)
{
    return font_entry((DwFont *)This)->simulations;
}

static void WINAPI font_metrics(IDWriteFont1 *This, DWRITE_FONT_METRICS *metrics)
{
    DWRITE_FONT_METRICS1 m = font_entry((DwFont *)This)->metrics;
    DwSimulateMetrics(&m, font_entry((DwFont *)This)->simulations);
    memcpy(metrics, &m, sizeof(*metrics));
}

static HRESULT WINAPI font_has_character(IDWriteFont1 *This, UINT32 code_point, BOOL *exists)
{
    if (!exists)
        return E_POINTER;
    *exists = FALSE;
    const DwEntry *e = font_entry((DwFont *)This);
    DwFontData *data;
    HRESULT hr = DwFontDataOpen(e->file, e->face_index, &data);
    if (FAILED(hr))
        return hr;
    *exists = DwFontDataHasCharacter(data, code_point);
    DwFontDataRelease(data);
    return S_OK;
}

static HRESULT WINAPI font_create_face(IDWriteFont1 *This, IDWriteFontFace **face)
{
    DwFont *f = (DwFont *)This;
    if (!face)
        return E_POINTER;
    const DwEntry *e = font_entry(f);
    return DwFaceCreate(f->collection->factory, e->file, e->face_type, e->face_index, e->simulations, face);
}

static void WINAPI font_metrics1(IDWriteFont1 *This, DWRITE_FONT_METRICS1 *metrics)
{
    *metrics = font_entry((DwFont *)This)->metrics;
    DwSimulateMetrics(metrics, font_entry((DwFont *)This)->simulations);
}

static void WINAPI font_panose(IDWriteFont1 *This, DWRITE_PANOSE *panose)
{
    memset(panose, 0, sizeof(*panose));
    const DwEntry *e = font_entry((DwFont *)This);
    DwFontData *data;
    if (FAILED(DwFontDataOpen(e->file, e->face_index, &data)))
        return;
    if (data->ttf.os2.data && data->ttf.os2.size >= 42)
        memcpy(panose->values, data->ttf.os2.data + 32, 10);
    DwFontDataRelease(data);
}

static HRESULT WINAPI font_ranges(IDWriteFont1 *This, UINT32 max, DWRITE_UNICODE_RANGE *ranges, UINT32 *actual)
{
    if (!actual)
        return E_POINTER;
    const DwEntry *e = font_entry((DwFont *)This);
    DwFontData *data;
    HRESULT hr = DwFontDataOpen(e->file, e->face_index, &data);
    if (FAILED(hr)) {
        *actual = 0;
        return hr;
    }
    *actual = DwFontDataRanges(data, ranges, max);
    DwFontDataRelease(data);
    return *actual > max ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

static BOOL WINAPI font_monospaced(IDWriteFont1 *This) { return font_entry((DwFont *)This)->attributes.monospaced; }

static const IDWriteFont1Vtbl g_font_vtbl = {
    font_query,   font_addref,   font_release,        font_family,   font_weight,      font_stretch,
    font_style,   font_symbol,   font_face_names,     font_informational, font_simulations, font_metrics,
    font_has_character, font_create_face, font_metrics1, font_panose, font_ranges,    font_monospaced,
};

BOOL DwFontEntryOf(IDWriteFont *font, DwCollection **collection, const DwEntry **entry)
{
    if (!font || font->lpVtbl != (const IDWriteFontVtbl *)&g_font_vtbl)
        return FALSE;
    DwFont *f = (DwFont *)font;
    *collection = f->collection;
    *entry = font_entry(f);
    return TRUE;
}

HRESULT DwFontCreate(DwCollection *c, UINT32 family, UINT32 entry, IDWriteFont **out)
{
    DwFont *f = DwAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &g_font_vtbl;
    f->refs = 1;
    f->collection = c;
    collection_addref((IDWriteFontCollection *)c);
    f->family = family;
    f->entry = entry;
    *out = (IDWriteFont *)f;
    return S_OK;
}

/* ------------------------------------------------------------------ families and font lists */

typedef struct DwFamilyObject {
    const IDWriteFontFamilyVtbl *lpVtbl;
    LONG refs;
    DwCollection *collection;
    UINT32 index;
} DwFamilyObject;

typedef struct DwFontList {
    const IDWriteFontListVtbl *lpVtbl;
    LONG refs;
    DwCollection *collection;
    UINT32 family;
    UINT32 *order;
} DwFontList;

static HRESULT WINAPI list_query(IDWriteFontList *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontList)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI list_addref(IDWriteFontList *This) { return InterlockedIncrement(&((DwFontList *)This)->refs); }

static ULONG WINAPI list_release(IDWriteFontList *This)
{
    DwFontList *l = (DwFontList *)This;
    ULONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        collection_release((IDWriteFontCollection *)l->collection);
        DwFree(l->order);
        DwFree(l);
    }
    return refs;
}

static HRESULT WINAPI list_collection(IDWriteFontList *This, IDWriteFontCollection **collection)
{
    DwFontList *l = (DwFontList *)This;
    if (!collection)
        return E_POINTER;
    *collection = (IDWriteFontCollection *)l->collection;
    collection_addref(*collection);
    return S_OK;
}

static UINT32 WINAPI list_count(IDWriteFontList *This)
{
    DwFontList *l = (DwFontList *)This;
    return l->collection->families[l->family].count;
}

static HRESULT WINAPI list_font(IDWriteFontList *This, UINT32 index, IDWriteFont **font)
{
    DwFontList *l = (DwFontList *)This;
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (index >= l->collection->families[l->family].count)
        return E_INVALIDARG;
    return DwFontCreate(l->collection, l->family, l->order[index], font);
}

static const IDWriteFontListVtbl g_list_vtbl = {
    list_query, list_addref, list_release, list_collection, list_count, list_font,
};

static HRESULT WINAPI family_query(IDWriteFontFamily *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontList) ||
        IsEqualGUID(riid, &IID_IDWriteFontFamily)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI family_addref(IDWriteFontFamily *This)
{
    return InterlockedIncrement(&((DwFamilyObject *)This)->refs);
}

static ULONG WINAPI family_release(IDWriteFontFamily *This)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    ULONG refs = InterlockedDecrement(&f->refs);
    if (!refs) {
        collection_release((IDWriteFontCollection *)f->collection);
        DwFree(f);
    }
    return refs;
}

static HRESULT WINAPI family_collection(IDWriteFontFamily *This, IDWriteFontCollection **collection)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!collection)
        return E_POINTER;
    *collection = (IDWriteFontCollection *)f->collection;
    collection_addref(*collection);
    return S_OK;
}

static UINT32 WINAPI family_count(IDWriteFontFamily *This)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    return f->collection->families[f->index].count;
}

static HRESULT WINAPI family_font(IDWriteFontFamily *This, UINT32 index, IDWriteFont **font)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (index >= f->collection->families[f->index].count)
        return E_INVALIDARG;
    return DwFontCreate(f->collection, f->index, index, font);
}

static HRESULT WINAPI family_names(IDWriteFontFamily *This, IDWriteLocalizedStrings **names)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!names)
        return E_POINTER;
    return strings_out(f->collection->families[f->index].names, names);
}

static HRESULT WINAPI family_first(IDWriteFontFamily *This, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                                   DWRITE_FONT_STYLE style, IDWriteFont **font)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!font)
        return E_POINTER;
    *font = nullptr;
    const DwFamily *family = &f->collection->families[f->index];
    if (!family->count)
        return DWRITE_E_NOFONT;
    return DwFontCreate(f->collection, f->index, DwBestEntry(family, weight, stretch, style), font);
}

static HRESULT WINAPI family_matching(IDWriteFontFamily *This, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                                      DWRITE_FONT_STYLE style, IDWriteFontList **fonts)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!fonts)
        return E_POINTER;
    *fonts = nullptr;
    const DwFamily *family = &f->collection->families[f->index];
    DwFontList *l = DwAllocZero(sizeof(*l));
    if (!l)
        return E_OUTOFMEMORY;
    l->order = DwAlloc(family->count * sizeof(UINT32));
    if (!l->order) {
        DwFree(l);
        return E_OUTOFMEMORY;
    }
    DwSortEntries(family, weight, stretch, style, l->order);
    l->lpVtbl = &g_list_vtbl;
    l->refs = 1;
    l->collection = f->collection;
    collection_addref((IDWriteFontCollection *)f->collection);
    l->family = f->index;
    *fonts = (IDWriteFontList *)l;
    return S_OK;
}

static const IDWriteFontFamilyVtbl g_family_vtbl = {
    family_query, family_addref, family_release, family_collection, family_count, family_font, family_names,
    family_first, family_matching,
};

HRESULT DwFamilyCreate(DwCollection *c, UINT32 index, IDWriteFontFamily **out)
{
    DwFamilyObject *f = DwAllocZero(sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->lpVtbl = &g_family_vtbl;
    f->refs = 1;
    f->collection = c;
    collection_addref((IDWriteFontCollection *)c);
    f->index = index;
    *out = (IDWriteFontFamily *)f;
    return S_OK;
}

HRESULT DwCreateFontByName(DwCollection *c, const WCHAR *name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                           DWRITE_FONT_STYLE style, IDWriteFont **font)
{
    UINT32 index;
    if (!DwFindFamily(c, name, &index))
        return DWRITE_E_NOFONT;
    const DwFamily *family = &c->families[index];
    if (!family->count)
        return DWRITE_E_NOFONT;
    return DwFontCreate(c, index, DwBestEntry(family, weight, stretch, style), font);
}
