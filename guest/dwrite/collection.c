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

int DwCompareFit(const DwAttributes *a, DWRITE_FONT_SIMULATIONS a_simulations, const DwAttributes *b,
                 DWRITE_FONT_SIMULATIONS b_simulations, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                 DWRITE_FONT_STYLE style)
{
    DwEntry x = {}, y = {};
    x.attributes = *a;
    x.simulations = a_simulations;
    y.attributes = *b;
    y.simulations = b_simulations;
    return key_compare(key_of(&x, weight, stretch, style), key_of(&y, weight, stretch, style));
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
    UINT32 public_count;
    DWRITE_FONT_FAMILY_MODEL model;
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
        /* every upright face has an oblique twin, even when the family has italics of its own. */
        UINT32 present = f->count;
        for (UINT32 i = 0; i < present; i++) {
            if (f->entries[i].attributes.stretch != stretch || f->entries[i].attributes.style != DWRITE_FONT_STYLE_NORMAL)
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
    /* the faces made by simulation come after the real ones. */
    if ((a->simulations != 0) != (b->simulations != 0))
        return a->simulations ? 1 : -1;
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

/*
 * a font named after its weight, like Franklin Gothic Medium, belongs to the family without the word. returns the
 * family names with the word removed and the face name it leaves, or null when the name has no such word.
 */
static DwStrings *family_without_weight(const DwStrings *win32, UINT32 weight, BOOL italic, DwStrings **face)
{
    static const struct { const WCHAR *word; UINT32 weight; } words[] = {
        {L"Medium", 500}, {L"Semilight", 350}, {L"Light", 300}, {L"Semibold", 600}, {L"Demibold", 600},
        {L"Black", 900}, {L"Heavy", 900},    {L"Thin", 100},  {L"Extralight", 200}, {L"Extrabold", 800},
    };
    const WCHAR *name = DwStringsBest(win32, L"en-us");
    size_t length = name ? wcslen(name) : 0;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        size_t word = wcslen(words[i].word);
        if ((words[i].weight != weight && weight != 400) || length <= word + 1 || name[length - word - 1] != ' ' ||
            _wcsicmp(name + length - word, words[i].word))
            continue;
        DwStrings *family = DwStringsNew();
        DwStrings *sub = DwStringsNew();
        WCHAR text[LF_FACESIZE + 16];
        if (!family || !sub || length - word - 1 >= sizeof(text) / sizeof(text[0]) - 1) {
            DwStringsRelease(family);
            DwStringsRelease(sub);
            return nullptr;
        }
        DwStringsAdd(family, L"en-us", 5, name, (UINT32)(length - word - 1));
        wcscpy(text, words[i].word);
        if (italic)
            wcscat(text, L" Italic");
        DwStringsAdd(sub, L"en-us", 5, text, (UINT32)wcslen(text));
        *face = sub;
        return family;
    }
    return nullptr;
}

void DwModelNames(const DwFontData *data, DWRITE_FONT_FAMILY_MODEL model, DwAttributes *attributes,
                  DwStrings **family, DwStrings **face)
{
    DwStrings *wws = DwFontDataNames(data, 21), *typographic = DwFontDataNames(data, 16);
    DwStrings *win32 = DwFontDataNames(data, 1), *wws_sub = DwFontDataNames(data, 22);
    DwStrings *typographic_sub = DwFontDataNames(data, 17), *win32_sub = DwFontDataNames(data, 2);
    DwStrings *family_names, *face_names;
    /* the typographic model groups by the designer's family, the other by the family of weight, stretch and style. */
    if (model == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC) {
        family_names = typographic ? typographic : wws ? wws : win32;
        face_names = typographic_sub ? typographic_sub : wws_sub ? wws_sub : win32_sub;
    } else {
        family_names = wws ? wws : typographic ? typographic : win32;
        face_names = wws_sub ? wws_sub : typographic_sub ? typographic_sub : win32_sub;
    }
    DwStrings *derived = nullptr, *derived_face = nullptr;
    if (!wws && !typographic && win32) {
        derived = family_without_weight(win32, attributes->weight, attributes->style != DWRITE_FONT_STYLE_NORMAL,
                                        &derived_face);
        if (derived) {
            family_names = derived;
            face_names = derived_face;
            /* a font left at the regular weight by its file has the weight its name gives. */
            if (attributes->weight == 400)
                attributes->weight = DwWeightOfName(DwStringsBest(derived_face, L"en-us"));
        }
    }
    if (family_names)
        DwStringsAddRef(family_names);
    if (face_names)
        DwStringsAddRef(face_names);
    *family = family_names;
    *face = face_names;
    DwStringsRelease(derived);
    DwStringsRelease(derived_face);
    DwStringsRelease(wws);
    DwStringsRelease(typographic);
    DwStringsRelease(win32);
    DwStringsRelease(wws_sub);
    DwStringsRelease(typographic_sub);
    DwStringsRelease(win32_sub);
}

static DWRITE_FONT_FACE_TYPE face_type_of(const DwFontData *data)
{
    return data->collection ? DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION
           : data->cff      ? DWRITE_FONT_FACE_TYPE_CFF
                            : DWRITE_FONT_FACE_TYPE_TRUETYPE;
}

/* a number a font set's builder was given as a property. */
static BOOL custom_number(DwSetFont *font, DWRITE_FONT_PROPERTY_ID id, UINT32 *value)
{
    DwStrings *strings = font ? DwSetFontCustom(font, id) : nullptr;
    const WCHAR *text = strings ? DwStringsBest(strings, L"en-us") : nullptr;
    if (!text || !*text)
        return FALSE;
    UINT32 v = 0;
    for (; *text; text++) {
        if (*text < L'0' || *text > L'9' || v > 100000)
            return FALSE;
        v = v * 10 + (UINT32)(*text - L'0');
    }
    *value = v;
    return TRUE;
}

/* one face of a file, under the names of the model, or those a font set gives it. */
static void add_face(Builder *b, IDWriteFontFile *file, UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                     DwSetFont *font)
{
    DwFontData *data;
    if (FAILED(DwFontDataOpen(file, index, &data)))
        return;
    DwEntry entry = {};
    entry.file = file;
    entry.face_index = index;
    entry.face_type = face_type_of(data);
    entry.simulations = simulations;
    DwFontDataAttributes(data, &entry.attributes);
    DwFontDataMetrics(data, &entry.metrics);
    DwStrings *win32 = DwFontDataNames(data, 1);
    DwStrings *win32_sub = DwFontDataNames(data, 2);
    entry.win32_family = win32;
    entry.win32_subfamily = win32_sub;
    DwStrings *family_names, *face_names;
    DwModelNames(data, b->model, &entry.attributes, &family_names, &face_names);
    if (font) {
        BOOL typographic = b->model == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC;
        DwStrings *custom_family = DwSetFontCustom(font, typographic ? DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME
                                                                      : DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FAMILY_NAME);
        DwStrings *custom_face = DwSetFontCustom(font, typographic ? DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FACE_NAME
                                                                    : DWRITE_FONT_PROPERTY_ID_WEIGHT_STRETCH_STYLE_FACE_NAME);
        if (custom_family) {
            DwStringsRelease(family_names);
            DwStringsAddRef(custom_family);
            family_names = custom_family;
        }
        if (custom_face) {
            DwStringsRelease(face_names);
            DwStringsAddRef(custom_face);
            face_names = custom_face;
        }
        const DwFontKey *key = DwSetFontKey(font);
        DwAxesToAttributes(key->axes, key->axis_count, &entry.attributes.weight, &entry.attributes.stretch,
                           &entry.attributes.style);
        UINT32 v;
        if (custom_number(font, DWRITE_FONT_PROPERTY_ID_WEIGHT, &v) && v >= 1 && v <= 999)
            entry.attributes.weight = (DWRITE_FONT_WEIGHT)v;
        if (custom_number(font, DWRITE_FONT_PROPERTY_ID_STRETCH, &v) && v >= 1 && v <= 9)
            entry.attributes.stretch = (DWRITE_FONT_STRETCH)v;
        if (custom_number(font, DWRITE_FONT_PROPERTY_ID_STYLE, &v) && v <= 2)
            entry.attributes.style = (DWRITE_FONT_STYLE)v;
    }
    if (simulations & DWRITE_FONT_SIMULATIONS_BOLD)
        entry.attributes.weight = DWRITE_FONT_WEIGHT_BOLD;
    if ((simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) && entry.attributes.style == DWRITE_FONT_STYLE_NORMAL)
        entry.attributes.style = DWRITE_FONT_STYLE_OBLIQUE;
    entry.face_names = face_names;
    if (family_names) {
        DwFamily *f = family_for(b, family_names);
        if (f)
            push_entry(f, &entry);
        /* a face whose win32 name differs from its bigger family also gets a family of its own. */
        if (win32 && !simulations && DwCompareNames(DwStringsBest(win32, L"en-us"), DwStringsBest(family_names, L"en-us"))) {
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
    DwStringsRelease(family_names);
    DwStringsRelease(face_names);
    DwStringsRelease(win32);
    DwStringsRelease(win32_sub);
    DwFontDataRelease(data);
}

static void add_file(Builder *b, IDWriteFontFile *file)
{
    BOOL supported;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    UINT32 faces;
    if (FAILED(file->lpVtbl->Analyze(file, &supported, &file_type, &face_type, &faces)) || !supported)
        return;
    for (UINT32 index = 0; index < faces; index++)
        add_face(b, file, index, DWRITE_FONT_SIMULATIONS_NONE, nullptr);
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

/* the families named after win32 names only serve fonts looked up by name, they come after the families listed. */
static void partition_families(Builder *b)
{
    DwFamily *ordered = DwAlloc((b->count ? b->count : 1) * sizeof(DwFamily));
    if (!ordered) {
        b->public_count = b->count;
        return;
    }
    UINT32 n = 0;
    for (UINT32 pass = 0; pass < 2; pass++) {
        for (UINT32 i = 0; i < b->count; i++) {
            BOOL gdi = b->families[i].count && b->families[i].entries[0].gdi_family;
            if (gdi == (pass == 1))
                ordered[n++] = b->families[i];
        }
        if (pass == 0)
            b->public_count = n;
    }
    memcpy(b->families, ordered, n * sizeof(DwFamily));
    DwFree(ordered);
}

/* ------------------------------------------------------------------ collection object */

static HRESULT WINAPI collection_query(IDWriteFontCollection *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontCollection) ||
        IsEqualGUID(riid, &IID_IDWriteFontCollection1) || IsEqualGUID(riid, &IID_IDWriteFontCollection2) ||
        IsEqualGUID(riid, &IID_IDWriteFontCollection3)) {
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
        if (c->set)
            c->set->lpVtbl->Release(c->set);
        DwFree(c);
    }
    return refs;
}

static UINT32 WINAPI collection_count(IDWriteFontCollection *This) { return ((DwCollection *)This)->public_count; }

static HRESULT WINAPI collection_family(IDWriteFontCollection *This, UINT32 index, IDWriteFontFamily **family)
{
    if (!family)
        return E_POINTER;
    *family = nullptr;
    DwCollection *c = (DwCollection *)This;
    if (index >= c->public_count)
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
    if (DwFindFamilyExact((DwCollection *)This, name, &found)) {
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

HRESULT DwCollectionSet(DwCollection *c, UINT32 family, const UINT32 *order, UINT32 count, IDWriteFontSet4 **set)
{
    *set = nullptr;
    /* the whole collection is the set it was made of, or the real faces of every family it lists. */
    if (family == (UINT32)-1) {
        AcquireSRWLockExclusive(&c->lock);
        HRESULT hr = S_OK;
        if (!c->set) {
            UINT32 total = 0;
            for (UINT32 i = 0; i < c->public_count; i++)
                total += c->families[i].count;
            DwFontKey *keys = DwAlloc((total + 1) * sizeof(*keys));
            if (keys) {
                UINT32 n = 0;
                for (UINT32 i = 0; i < c->public_count; i++) {
                    for (UINT32 e = 0; e < c->families[i].count; e++) {
                        const DwEntry *entry = &c->families[i].entries[e];
                        if (entry->simulations)
                            continue;
                        memset(&keys[n], 0, sizeof(keys[n]));
                        keys[n].file = entry->file;
                        keys[n].index = entry->face_index;
                        n++;
                    }
                }
                hr = DwFontSetFromKeys(c->factory, keys, n, &c->set);
                DwFree(keys);
            } else {
                hr = E_OUTOFMEMORY;
            }
        }
        if (SUCCEEDED(hr)) {
            *set = c->set;
            (*set)->lpVtbl->AddRef(*set);
        }
        ReleaseSRWLockExclusive(&c->lock);
        return hr;
    }
    const DwFamily *f = &c->families[family];
    if (!order)
        count = f->count;
    DwFontKey *keys = DwAlloc((count + 1) * sizeof(*keys));
    if (!keys)
        return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < count; i++) {
        const DwEntry *entry = &f->entries[order ? order[i] : i];
        memset(&keys[i], 0, sizeof(keys[i]));
        keys[i].file = entry->file;
        keys[i].index = entry->face_index;
        keys[i].simulations = entry->simulations;
    }
    HRESULT hr = DwFontSetFromKeys(c->factory, keys, count, set);
    DwFree(keys);
    return hr;
}

static HRESULT WINAPI collection_font_set1(IDWriteFontCollection2 *This, IDWriteFontSet1 **set)
{
    if (!set)
        return E_POINTER;
    return DwCollectionSet((DwCollection *)This, (UINT32)-1, nullptr, 0, (IDWriteFontSet4 **)set);
}

static HRESULT WINAPI collection_font_set(IDWriteFontCollection2 *This, IDWriteFontSet **set)
{
    return collection_font_set1(This, (IDWriteFontSet1 **)set);
}

static HRESULT WINAPI collection_family1(IDWriteFontCollection2 *This, UINT32 index, IDWriteFontFamily1 **family)
{
    return collection_family((IDWriteFontCollection *)This, index, (IDWriteFontFamily **)family);
}

static HRESULT WINAPI collection_family2(IDWriteFontCollection2 *This, UINT32 index, IDWriteFontFamily2 **family)
{
    return collection_family((IDWriteFontCollection *)This, index, (IDWriteFontFamily **)family);
}

static HRESULT list_create(DwCollection *c, UINT32 family, UINT32 *order, UINT32 count, IDWriteFontList **out);

static HRESULT WINAPI collection_matching(IDWriteFontCollection2 *This, const WCHAR *name,
                                          const DWRITE_FONT_AXIS_VALUE *axis_values, UINT32 count,
                                          IDWriteFontList2 **fonts)
{
    DwCollection *c = (DwCollection *)This;
    if (!fonts)
        return E_POINTER;
    *fonts = nullptr;
    if (!name || (count && !axis_values))
        return E_INVALIDARG;
    UINT32 index;
    /* a family the collection does not have gives an empty list. */
    if (!DwFindFamilyExact(c, name, &index))
        return list_create(c, (UINT32)-1, nullptr, 0, (IDWriteFontList **)fonts);
    const DwFamily *family = &c->families[index];
    UINT32 *order = DwAlloc((family->count + 1) * sizeof(UINT32));
    if (!order)
        return E_OUTOFMEMORY;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DwAxesToAttributes(axis_values, count, &weight, &stretch, &style);
    DwSortEntries(family, weight, stretch, style, order);
    return list_create(c, index, order, family->count, (IDWriteFontList **)fonts);
}

static DWRITE_FONT_FAMILY_MODEL WINAPI collection_model(IDWriteFontCollection2 *This)
{
    return ((DwCollection *)This)->model;
}

/* the fonts of a collection never go away, so the event is never set. */
static HANDLE WINAPI collection_expiration(IDWriteFontCollection2 *This)
{
    static HANDLE event;
    if (!event) {
        HANDLE created = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (created && InterlockedCompareExchangePointer(&event, created, nullptr))
            CloseHandle(created);
    }
    return event;
}

static const IDWriteFontCollection3Vtbl g_collection_vtbl = {
    (void *)collection_query,   (void *)collection_addref,     (void *)collection_release,
    (void *)collection_count,   (void *)collection_family,     (void *)collection_find,
    (void *)collection_from_face, (void *)collection_font_set, (void *)collection_family1,
    (void *)collection_family2, (void *)collection_matching,   (void *)collection_model,
    (void *)collection_font_set1, (void *)collection_expiration,
};

static void builder_free(Builder *b)
{
    for (UINT32 i = 0; i < b->count; i++) {
        for (UINT32 e = 0; e < b->families[i].count; e++)
            entry_release(&b->families[i].entries[e]);
        DwFree(b->families[i].entries);
        DwStringsRelease(b->families[i].names);
    }
    DwFree(b->families);
}

static HRESULT builder_finish(Builder *b, DwFactory *factory, DwCollection **out)
{
    for (UINT32 i = 0; i < b->count; i++) {
        add_simulations(&b->families[i]);
        sort_entries(&b->families[i]);
    }
    sort_families(b);
    partition_families(b);
    DwCollection *c = DwAllocZero(sizeof(*c));
    if (!c) {
        builder_free(b);
        return E_OUTOFMEMORY;
    }
    c->lpVtbl = (const IDWriteFontCollectionVtbl *)&g_collection_vtbl;
    c->refs = 1;
    c->factory = factory;
    c->families = b->families;
    c->count = b->count;
    c->public_count = b->public_count;
    c->model = b->model;
    InitializeSRWLock(&c->lock);
    *out = c;
    return S_OK;
}

HRESULT DwCollectionFromSet(DwFactory *factory, IDWriteFontSet *set, DWRITE_FONT_FAMILY_MODEL model, DwCollection **out)
{
    *out = nullptr;
    if (!set)
        return E_INVALIDARG;
    Builder b = {};
    b.model = model;
    DwSetFont *const *fonts;
    UINT32 count;
    IDWriteFontSet4 *kept = nullptr;
    if (DwFontSetFonts(set, &fonts, &count)) {
        for (UINT32 i = 0; i < count; i++) {
            const DwFontKey *key = DwSetFontKey(fonts[i]);
            add_face(&b, key->file, key->index, key->simulations, fonts[i]);
        }
        set->lpVtbl->QueryInterface(set, &IID_IDWriteFontSet4, (void **)&kept);
    } else {
        /* a set of another implementation gives its fonts by reference. */
        count = set->lpVtbl->GetFontCount(set);
        for (UINT32 i = 0; i < count; i++) {
            IDWriteFontFaceReference *reference;
            if (FAILED(set->lpVtbl->GetFontFaceReference(set, i, &reference)))
                continue;
            DwFontKey key;
            if (DwFaceReferenceKey(reference, &key))
                add_face(&b, key.file, key.index, key.simulations, nullptr);
            reference->lpVtbl->Release(reference);
        }
    }
    HRESULT hr = builder_finish(&b, factory, out);
    if (SUCCEEDED(hr))
        (*out)->set = kept;
    else if (kept)
        kept->lpVtbl->Release(kept);
    return hr;
}

HRESULT DwCollectionFromFiles(DwFactory *factory, IDWriteFontFile *const *files, UINT32 count,
                              IDWriteFontFileEnumerator *enumerator, DwCollection **out)
{
    Builder b = {};
    b.model = DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE;
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
        builder_free(&b);
        return hr;
    }
    return builder_finish(&b, factory, out);
}

/* ------------------------------------------------------------------ system collection */

HRESULT DwSystemCollectionModel(DwFactory *factory, DWRITE_FONT_FAMILY_MODEL model, IDWriteFontCollection **out)
{
    *out = nullptr;
    IDWriteFontSet4 *set;
    HRESULT hr = DwSystemFontSet(factory, &set);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&factory->lock);
    IDWriteFontCollection **slot =
        model == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC ? &factory->system_typographic : &factory->system;
    if (!*slot) {
        DwCollection *c;
        hr = DwCollectionFromSet(factory, (IDWriteFontSet *)set, model, &c);
        if (SUCCEEDED(hr))
            *slot = (IDWriteFontCollection *)c;
    }
    if (SUCCEEDED(hr)) {
        *out = *slot;
        (*out)->lpVtbl->AddRef(*out);
    }
    ReleaseSRWLockExclusive(&factory->lock);
    set->lpVtbl->Release(set);
    return hr;
}

HRESULT DwSystemCollection(DwFactory *factory, IDWriteFontCollection **out)
{
    return DwSystemCollectionModel(factory, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, out);
}

DwCollection *DwCollectionOf(DwFactory *factory, IDWriteFontCollection *collection)
{
    if (collection) {
        /* collections of another implementation are not ours to read. */
        const IDWriteFontCollectionVtbl *vtbl = collection->lpVtbl;
        return vtbl == (const IDWriteFontCollectionVtbl *)&g_collection_vtbl ? (DwCollection *)collection : nullptr;
    }
    IDWriteFontCollection *system;
    if (FAILED(DwSystemCollection(factory, &system)))
        return nullptr;
    system->lpVtbl->Release(system);
    return (DwCollection *)system;
}

/* ------------------------------------------------------------------ fonts */

typedef struct DwFont {
    const IDWriteFont3Vtbl *lpVtbl;
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
        IsEqualGUID(riid, &IID_IDWriteFont1) || IsEqualGUID(riid, &IID_IDWriteFont2) ||
        IsEqualGUID(riid, &IID_IDWriteFont3)) {
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

DwStrings *DwInformationalStrings(const DwFontData *data, DWRITE_INFORMATIONAL_STRING_ID id)
{
    UINT32 name_id = name_id_of(id);
    if (name_id == 0xffff || id == DWRITE_INFORMATIONAL_STRING_NONE)
        return nullptr;
    return DwFontDataNames(data, name_id);
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
        names = DwInformationalStrings(data, id);
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

/* Mime draws no color glyphs, so no font is a color font. */
static BOOL WINAPI font_color(IDWriteFont1 *This) { return FALSE; }

static BOOL entries_equal(const DwEntry *a, const DwEntry *b)
{
    return a->face_index == b->face_index && a->simulations == b->simulations && DwSameFile(a->file, b->file);
}

BOOL DwFontEntryOf(IDWriteFont *font, DwCollection **collection, const DwEntry **entry);

static BOOL WINAPI font_equals(IDWriteFont1 *This, IDWriteFont *other)
{
    DwCollection *c;
    const DwEntry *e;
    if (!other || !DwFontEntryOf(other, &c, &e))
        return FALSE;
    return entries_equal(font_entry((DwFont *)This), e);
}

static HRESULT WINAPI font_reference(IDWriteFont1 *This, IDWriteFontFaceReference **reference)
{
    DwFont *f = (DwFont *)This;
    if (!reference)
        return E_POINTER;
    const DwEntry *e = font_entry(f);
    DwFontKey key = {e->file, e->face_index, e->simulations};
    return DwFaceReferenceCreate(f->collection->factory, &key, (IDWriteFontFaceReference1 **)reference);
}

static BOOL WINAPI font_has_character3(IDWriteFont1 *This, UINT32 code_point)
{
    BOOL exists = FALSE;
    font_has_character(This, code_point, &exists);
    return exists;
}

static DWRITE_LOCALITY WINAPI font_locality(IDWriteFont1 *This) { return DWRITE_LOCALITY_LOCAL; }

static const IDWriteFont3Vtbl g_font_vtbl = {
    (void *)font_query,         (void *)font_addref,      (void *)font_release,     (void *)font_family,
    (void *)font_weight,        (void *)font_stretch,     (void *)font_style,       (void *)font_symbol,
    (void *)font_face_names,    (void *)font_informational, (void *)font_simulations, (void *)font_metrics,
    (void *)font_has_character, (void *)font_create_face, (void *)font_metrics1,    (void *)font_panose,
    (void *)font_ranges,        (void *)font_monospaced,  (void *)font_color,       (void *)font_create_face,
    (void *)font_equals,        (void *)font_reference,   (void *)font_has_character3, (void *)font_locality,
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
    const IDWriteFontFamily2Vtbl *lpVtbl;
    LONG refs;
    DwCollection *collection;
    UINT32 index;
} DwFamilyObject;

/* fonts of one family in an order, or none for a family the collection lacks. */
typedef struct DwFontList {
    const IDWriteFontList2Vtbl *lpVtbl;
    LONG refs;
    DwCollection *collection;
    UINT32 family;
    UINT32 *order;
    UINT32 count;
} DwFontList;

static HRESULT WINAPI list_query(IDWriteFontList *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontList) ||
        IsEqualGUID(riid, &IID_IDWriteFontList1) || IsEqualGUID(riid, &IID_IDWriteFontList2)) {
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

static UINT32 WINAPI list_count(IDWriteFontList *This) { return ((DwFontList *)This)->count; }

static HRESULT WINAPI list_font(IDWriteFontList *This, UINT32 index, IDWriteFont **font)
{
    DwFontList *l = (DwFontList *)This;
    if (!font)
        return E_POINTER;
    *font = nullptr;
    if (index >= l->count)
        return E_INVALIDARG;
    return DwFontCreate(l->collection, l->family, l->order[index], font);
}

static DWRITE_LOCALITY WINAPI list_locality(IDWriteFontList *This, UINT32 index) { return DWRITE_LOCALITY_LOCAL; }

static HRESULT entry_reference(DwCollection *c, UINT32 family, UINT32 entry, IDWriteFontFaceReference **reference)
{
    if (!reference)
        return E_POINTER;
    *reference = nullptr;
    const DwEntry *e = &c->families[family].entries[entry];
    DwFontKey key = {e->file, e->face_index, e->simulations};
    return DwFaceReferenceCreate(c->factory, &key, (IDWriteFontFaceReference1 **)reference);
}

static HRESULT WINAPI list_reference(IDWriteFontList *This, UINT32 index, IDWriteFontFaceReference **reference)
{
    DwFontList *l = (DwFontList *)This;
    if (reference)
        *reference = nullptr;
    if (index >= l->count)
        return E_INVALIDARG;
    return entry_reference(l->collection, l->family, l->order[index], reference);
}

static HRESULT WINAPI list_font_set(IDWriteFontList *This, IDWriteFontSet1 **set)
{
    DwFontList *l = (DwFontList *)This;
    if (!set)
        return E_POINTER;
    if (l->family == (UINT32)-1)
        return DwFontSetFromKeys(l->collection->factory, nullptr, 0, (IDWriteFontSet4 **)set);
    return DwCollectionSet(l->collection, l->family, l->order, l->count, (IDWriteFontSet4 **)set);
}

static const IDWriteFontList2Vtbl g_list_vtbl = {
    (void *)list_query,  (void *)list_addref,   (void *)list_release,  (void *)list_collection,
    (void *)list_count,  (void *)list_font,     (void *)list_locality, (void *)list_font,
    (void *)list_reference, (void *)list_font_set,
};

/* takes the order. */
static HRESULT list_create(DwCollection *c, UINT32 family, UINT32 *order, UINT32 count, IDWriteFontList **out)
{
    DwFontList *l = DwAllocZero(sizeof(*l));
    if (!l) {
        DwFree(order);
        return E_OUTOFMEMORY;
    }
    l->lpVtbl = &g_list_vtbl;
    l->refs = 1;
    l->collection = c;
    collection_addref((IDWriteFontCollection *)c);
    l->family = family;
    l->order = order;
    l->count = count;
    *out = (IDWriteFontList *)l;
    return S_OK;
}

static HRESULT WINAPI family_query(IDWriteFontFamily *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDWriteFontList) ||
        IsEqualGUID(riid, &IID_IDWriteFontFamily) || IsEqualGUID(riid, &IID_IDWriteFontFamily1) ||
        IsEqualGUID(riid, &IID_IDWriteFontFamily2)) {
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

static HRESULT family_sorted(DwFamilyObject *f, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                             DWRITE_FONT_STYLE style, IDWriteFontList **fonts)
{
    if (!fonts)
        return E_POINTER;
    *fonts = nullptr;
    const DwFamily *family = &f->collection->families[f->index];
    UINT32 *order = DwAlloc((family->count + 1) * sizeof(UINT32));
    if (!order)
        return E_OUTOFMEMORY;
    DwSortEntries(family, weight, stretch, style, order);
    return list_create(f->collection, f->index, order, family->count, fonts);
}

static HRESULT WINAPI family_matching(IDWriteFontFamily *This, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                                      DWRITE_FONT_STYLE style, IDWriteFontList **fonts)
{
    return family_sorted((DwFamilyObject *)This, weight, stretch, style, fonts);
}

static DWRITE_LOCALITY WINAPI family_locality(IDWriteFontFamily2 *This, UINT32 index)
{
    return DWRITE_LOCALITY_LOCAL;
}

static HRESULT WINAPI family_font3(IDWriteFontFamily2 *This, UINT32 index, IDWriteFont3 **font)
{
    return family_font((IDWriteFontFamily *)This, index, (IDWriteFont **)font);
}

static HRESULT WINAPI family_reference(IDWriteFontFamily2 *This, UINT32 index, IDWriteFontFaceReference **reference)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (reference)
        *reference = nullptr;
    if (index >= f->collection->families[f->index].count)
        return E_INVALIDARG;
    return entry_reference(f->collection, f->index, index, reference);
}

static HRESULT WINAPI family_matching_axes(IDWriteFontFamily2 *This, const DWRITE_FONT_AXIS_VALUE *axis_values,
                                           UINT32 count, IDWriteFontList2 **fonts)
{
    if (fonts)
        *fonts = nullptr;
    if (count && !axis_values)
        return E_INVALIDARG;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DwAxesToAttributes(axis_values, count, &weight, &stretch, &style);
    return family_sorted((DwFamilyObject *)This, weight, stretch, style, (IDWriteFontList **)fonts);
}

static HRESULT WINAPI family_font_set(IDWriteFontFamily2 *This, IDWriteFontSet1 **set)
{
    DwFamilyObject *f = (DwFamilyObject *)This;
    if (!set)
        return E_POINTER;
    return DwCollectionSet(f->collection, f->index, nullptr, 0, (IDWriteFontSet4 **)set);
}

static const IDWriteFontFamily2Vtbl g_family_vtbl = {
    (void *)family_query, (void *)family_addref, (void *)family_release, (void *)family_collection,
    (void *)family_count, (void *)family_font, (void *)family_names, (void *)family_first,
    (void *)family_matching, family_locality, family_font3, family_reference, family_matching_axes,
    family_font_set,
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
