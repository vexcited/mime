/*
 * fonts: the font files, the families made of them, the collections that hold the families and the font objects
 * of the flat api. The installed collection is what gdi32 lists, read through its font data calls the first time
 * a face is needed. A private collection reads font files and memory itself.
 */
#include "text_private.h"

/* the tag of a font table as GetFontData takes it, the first letter in the lowest byte. */
#define TABLE_TAG(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)

/* ------------------------------------------------------------------ the lock of the shared state */

static volatile LONG g_lock;

static void text_lock(void)
{
    while (InterlockedCompareExchange(&g_lock, 1, 0) != 0)
        Sleep(0);
}

static void text_unlock(void) { InterlockedExchange(&g_lock, 0); }

/* ------------------------------------------------------------------ names */

static WCHAR raise_ascii(WCHAR c) { return c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c; }

/* names are equal when their first 31 characters are, with only a to z raised. */
static int name_compare(const WCHAR *a, const WCHAR *b)
{
    for (INT i = 0; i < LF_FACESIZE - 1; i++) {
        WCHAR x = raise_ascii(a[i]), y = raise_ascii(b[i]);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ files and faces */

static GFontBlob *blob_new(BYTE *bytes, SIZE_T size)
{
    GFontBlob *blob = gp_alloc_zero(sizeof(*blob));
    if (!blob)
        return nullptr;
    blob->refs = 1;
    blob->bytes = bytes;
    blob->size = size;
    return blob;
}

static void blob_release(GFontBlob *blob)
{
    if (blob && InterlockedDecrement(&blob->refs) == 0) {
        gp_free(blob->bytes);
        gp_free(blob);
    }
}

static void face_free(GFace *face)
{
    if (face) {
        blob_release(face->blob);
        gp_free(face);
    }
}

/* the design numbers of a face: the horizontal header's, or the typographic ones when the font asks for them. */
static void face_metrics(GFace *face)
{
    const MimeTtfMetrics *m = &face->metrics;
    face->em = m->units_per_em;
    face->ascent = m->ascender;
    face->descent = m->descender < 0 ? -m->descender : m->descender;
    face->gap = m->line_gap;
    if (m->use_typo_metrics && (m->typo_ascender || m->typo_descender)) {
        face->ascent = m->typo_ascender;
        face->descent = m->typo_descender < 0 ? -m->typo_descender : m->typo_descender;
        face->gap = m->typo_line_gap;
    }
    if (!face->ascent && !face->descent) {
        face->ascent = m->win_ascent;
        face->descent = m->win_descent;
        face->gap = 0;
    }
    face->underline_pos = m->underline_position ? m->underline_position : -face->em / 10;
    face->underline_size = m->underline_thickness > 0 ? m->underline_thickness : face->em / 20;
    face->strike_pos = m->strikeout_position ? m->strikeout_position : face->em / 4;
    face->strike_size = m->strikeout_size > 0 ? m->strikeout_size : face->em / 20;
}

/* parses face number index of the bytes; the face keeps a reference on them. */
static GFace *face_open(GFontBlob *blob, unsigned index)
{
    GFace *face = gp_alloc_zero(sizeof(*face));
    if (!face)
        return nullptr;
    if (!mime_ttf_open(&face->ttf, blob->bytes, blob->size, index) || !face->ttf.units_per_em) {
        gp_free(face);
        return nullptr;
    }
    mime_ttf_metrics(&face->ttf, &face->metrics);
    face_metrics(face);
    InterlockedIncrement(&blob->refs);
    face->blob = blob;
    return face;
}

/* ------------------------------------------------------------------ families and collections */

static int slot_of(BOOL bold, BOOL italic) { return (bold ? 1 : 0) + (italic ? 2 : 0); }

static GFamily *family_new(const WCHAR *name, BOOL installed)
{
    GFamily *family = gp_alloc_zero(sizeof(*family));
    if (!family)
        return nullptr;
    family->tag = GP_TAG_FAMILY;
    family->refs = 1;
    family->installed = installed;
    family->charset = DEFAULT_CHARSET;
    lstrcpynW(family->name, name, LF_FACESIZE);
    return family;
}

static void family_free(GFamily *family)
{
    for (INT i = 0; i < GP_SLOT_COUNT; i++)
        face_free(family->slot[i].face);
    family->tag = 0;
    gp_free(family);
}

static void family_release(GFamily *family)
{
    /* the installed families live as long as the process. */
    if (family && !family->installed && InterlockedDecrement(&family->refs) == 0)
        family_free(family);
}

static void family_retain(GFamily *family)
{
    if (!family->installed)
        InterlockedIncrement(&family->refs);
}

/* the index a name is or would be at in the sorted families of a collection. */
static INT collection_lower_bound(const GCollection *c, const WCHAR *name, BOOL *found)
{
    INT lo = 0, hi = c->count;
    *found = FALSE;
    while (lo < hi) {
        INT mid = (lo + hi) / 2;
        int cmp = name_compare(c->families[mid]->name, name);
        if (cmp == 0) {
            *found = TRUE;
            return mid;
        }
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static GFamily *collection_find(const GCollection *c, const WCHAR *name)
{
    BOOL found;
    INT at = collection_lower_bound(c, name, &found);
    return found ? c->families[at] : nullptr;
}

/* the family of that name, made when the collection does not have it yet. */
static GFamily *collection_add_family(GCollection *c, const WCHAR *name, BOOL installed)
{
    BOOL found;
    INT at = collection_lower_bound(c, name, &found);
    if (found)
        return c->families[at];
    if (c->count == c->capacity) {
        INT capacity = c->capacity ? c->capacity * 2 : 64;
        GFamily **grown = gp_realloc(c->families, sizeof(GFamily *) * (SIZE_T)capacity);
        if (!grown)
            return nullptr;
        c->families = grown;
        c->capacity = capacity;
    }
    GFamily *family = family_new(name, installed);
    if (!family)
        return nullptr;
    memmove(c->families + at + 1, c->families + at, sizeof(GFamily *) * (SIZE_T)(c->count - at));
    c->families[at] = family;
    c->count++;
    return family;
}

static GCollection g_installed = {GP_TAG_COLLECTION, TRUE};

/* ------------------------------------------------------------------ the installed collection */

static int CALLBACK list_family(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    GCollection *c = (GCollection *)param;
    if ((type & TRUETYPE_FONTTYPE) && lf->lfFaceName[0] && lf->lfFaceName[0] != '@')
        collection_add_family(c, lf->lfFaceName, TRUE);
    return 1;
}

static void installed_build(void)
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, list_family, (LPARAM)&g_installed, 0);
    DeleteDC(dc);
}

static GCollection *installed_collection(void)
{
    text_lock();
    if (!g_installed.ready) {
        installed_build();
        g_installed.ready = TRUE;
    }
    text_unlock();
    return &g_installed;
}

/* a weight is closer to the regular or the bold weight of its slot. */
static int weight_distance(int weight, int slot)
{
    int ideal = slot & 1 ? 700 : 400;
    return weight > ideal ? weight - ideal : ideal - weight;
}


typedef struct FaceEnum {
    GFamily *family;
    BOOL first;
} FaceEnum;

static int CALLBACK list_face(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    FaceEnum *e = (FaceEnum *)param;
    GFamily *family = e->family;
    if (!(type & TRUETYPE_FONTTYPE) || name_compare(lf->lfFaceName, family->name) != 0)
        return 1;
    if (e->first) {
        family->charset = lf->lfCharSet;
        e->first = FALSE;
    }
    int weight = lf->lfWeight ? lf->lfWeight : 400;
    int slot = slot_of(weight > 500, lf->lfItalic != 0);
    GFaceSlot *s = &family->slot[slot];
    if (s->present && weight_distance(s->lf.lfWeight, slot) <= weight_distance(weight, slot))
        return 1;
    s->present = TRUE;
    s->lf = *lf;
    s->lf.lfWeight = weight;
    s->lf.lfHeight = -2048;
    s->lf.lfWidth = 0;
    return 1;
}

/* the slots of an installed family come from the faces gdi32 lists under its name. Called with the lock held. */
static void family_load_slots(GFamily *family)
{
    if (family->slots_loaded)
        return;
    family->slots_loaded = TRUE;
    if (!family->installed)
        return;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, family->name, LF_FACESIZE);
    FaceEnum e = {family, TRUE};
    EnumFontFamiliesExW(dc, &lf, list_face, (LPARAM)&e, 0);
    DeleteDC(dc);
}

/* which face of a collection file the device context has selected: the one with the same header table. */
static unsigned collection_face_index(HDC dc, const GFontBlob *blob, const LOGFONTW *lf)
{
    unsigned count = mime_ttf_face_count(blob->bytes, blob->size);
    BYTE head[54];
    if (GetFontData(dc, TABLE_TAG('h', 'e', 'a', 'd'), 0, head, sizeof(head)) == sizeof(head)) {
        for (unsigned i = 0; i < count; i++) {
            MimeTtfFont probe;
            if (mime_ttf_open(&probe, blob->bytes, blob->size, i) && probe.head.size >= sizeof(head) &&
                !memcmp(probe.head.data, head, sizeof(head)))
                return i;
        }
    }
    for (unsigned i = 0; i < count; i++) {
        MimeTtfFont probe;
        WCHAR name[LF_FACESIZE];
        size_t length = 0;
        if (mime_ttf_open(&probe, blob->bytes, blob->size, i) &&
            mime_ttf_name(&probe, 1, (uint16_t *)name, LF_FACESIZE, &length) && length < LF_FACESIZE &&
            name_compare(name, lf->lfFaceName) == 0)
            return i;
    }
    return 0;
}

/* reads the face gdi32 realizes for a logical font from its font data. */
static GFace *load_installed_face(const LOGFONTW *lf)
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return nullptr;
    GFace *face = nullptr;
    HFONT font = CreateFontIndirectW(lf);
    if (font) {
        HGDIOBJ old = SelectObject(dc, font);
        DWORD tag = TABLE_TAG('t', 't', 'c', 'f');
        DWORD size = GetFontData(dc, tag, 0, nullptr, 0);
        BOOL collection = size != GDI_ERROR;
        if (!collection) {
            tag = 0;
            size = GetFontData(dc, 0, 0, nullptr, 0);
        }
        if (size != GDI_ERROR && size > 0) {
            BYTE *bytes = gp_alloc(size);
            GFontBlob *blob = nullptr;
            if (bytes && GetFontData(dc, tag, 0, bytes, size) == size)
                blob = blob_new(bytes, size);
            if (blob) {
                face = face_open(blob, collection ? collection_face_index(dc, blob, lf) : 0);
                blob_release(blob);
            } else {
                gp_free(bytes);
            }
        }
        SelectObject(dc, old);
        DeleteObject(font);
    }
    DeleteDC(dc);
    return face;
}

/* the face of a slot, read the first time it is asked for. Called with the lock held. */
static const GFace *slot_face(GFamily *family, int index)
{
    GFaceSlot *s = &family->slot[index];
    if (!s->present)
        return nullptr;
    if (!s->face && !s->tried) {
        s->tried = TRUE;
        if (family->installed)
            s->face = load_installed_face(&s->lf);
    }
    return s->face;
}

const GFace *gp_family_face(GFamily *family, INT style, INT *slot)
{
    /* the slots tried for each of the four styles, in order, -1 ends the list. */
    static const signed char order[4][4] = {{0, -1, -1, -1}, {1, 0, -1, -1}, {2, 0, -1, -1}, {3, 1, 2, 0}};
    const GFace *face = nullptr;
    text_lock();
    family_load_slots(family);
    for (int k = 0; k < 4 && !face; k++) {
        int index = order[style & 3][k];
        if (index < 0)
            break;
        face = slot_face(family, index);
        if (face) {
            if (slot)
                *slot = index;
            break;
        }
        if (index == 0) {
            /* a family with no regular face uses its italic, else its bold, else its bold italic one in its place. */
            static const signed char stand_in[3] = {2, 1, 3};
            for (int j = 0; j < 3 && !face; j++) {
                face = slot_face(family, stand_in[j]);
                if (face && slot)
                    *slot = stand_in[j];
            }
        }
    }
    text_unlock();
    return face;
}

/* ------------------------------------------------------------------ lookups */

static const struct {
    const WCHAR *from;
    const WCHAR *to;
} g_substitutes[] = {
    {L"MS Shell Dlg", L"Microsoft Sans Serif"}, {L"MS Shell Dlg 2", L"Tahoma"},
    {L"MS Sans Serif", L"Microsoft Sans Serif"}, {L"Helv", L"Microsoft Sans Serif"},
    {L"Helvetica", L"Arial"},                     {L"Tms Rmn", L"Times New Roman"},
    {L"Times", L"Times New Roman"},               {L"MS Serif", L"Times New Roman"},
    {L"Courier", L"Courier New"},
};

/* the western script variants of three families are those families. */
static const WCHAR *const g_script_bases[] = {L"Arial", L"Courier New", L"Times New Roman"};
static const WCHAR *const g_script_suffixes[] = {L" Baltic", L" CE", L" CYR", L" Greek", L" TUR"};

static const WCHAR *substitute_for(const WCHAR *name)
{
    for (size_t i = 0; i < sizeof(g_substitutes) / sizeof(g_substitutes[0]); i++) {
        if (name_compare(name, g_substitutes[i].from) == 0)
            return g_substitutes[i].to;
    }
    for (size_t b = 0; b < sizeof(g_script_bases) / sizeof(g_script_bases[0]); b++) {
        for (size_t s = 0; s < sizeof(g_script_suffixes) / sizeof(g_script_suffixes[0]); s++) {
            WCHAR full[48];
            lstrcpynW(full, g_script_bases[b], 32);
            lstrcatW(full, g_script_suffixes[s]);
            if (name_compare(name, full) == 0)
                return g_script_bases[b];
        }
    }
    return nullptr;
}

static GFamily *installed_find(const WCHAR *name)
{
    GCollection *c = installed_collection();
    text_lock();
    GFamily *family = collection_find(c, name);
    text_unlock();
    return family;
}

GFamily *gp_family_by_name(const WCHAR *name)
{
    GFamily *family = installed_find(name);
    if (!family) {
        const WCHAR *other = substitute_for(name);
        if (other)
            family = installed_find(other);
    }
    return family;
}

static GFamily *first_installed_family(void)
{
    GCollection *c = installed_collection();
    return c->count ? c->families[0] : nullptr;
}

static GFamily *generic_family(const void *kind)
{
    static const WCHAR *const sans[] = {L"Microsoft Sans Serif", L"Arial", L"Tahoma", nullptr};
    static const WCHAR *const serif[] = {L"Times New Roman", nullptr};
    static const WCHAR *const mono[] = {L"Courier New", L"Lucida Console", nullptr};
    const WCHAR *const *names = kind == GP_FAMILY_SANS_SERIF ? sans : (kind == GP_FAMILY_SERIF ? serif : mono);
    for (; *names; names++) {
        GFamily *family = installed_find(*names);
        if (family)
            return family;
    }
    if (kind != GP_FAMILY_SANS_SERIF)
        return generic_family(GP_FAMILY_SANS_SERIF);
    return first_installed_family();
}

static BOOL is_generic(const void *p)
{
    return p == GP_FAMILY_SANS_SERIF || p == GP_FAMILY_SERIF || p == GP_FAMILY_MONOSPACE;
}

GFamily *gp_family_resolve(const void *family)
{
    if (!family)
        return nullptr;
    if (is_generic(family))
        return generic_family(family);
    return GP_CAST(GFamily, GP_TAG_FAMILY, family);
}

/* a family argument: InvalidParameter for what is none, FontFamilyNotFound for a generic one nothing stands for. */
static GpStatus family_arg(const void *p, GFamily **out)
{
    if (!p)
        return InvalidParameter;
    *out = gp_family_resolve(p);
    if (*out)
        return Ok;
    return is_generic(p) ? FontFamilyNotFound : InvalidParameter;
}

/* ------------------------------------------------------------------ private collections */

static GCollection *collection_cast(const void *p) { return GP_CAST(GCollection, GP_TAG_COLLECTION, p); }

/* a collection with its list of families ready. */
static GCollection *collection_use(const void *p)
{
    GCollection *c = collection_cast(p);
    if (c && c->installed)
        installed_collection();
    return c;
}

static GCollection *private_cast(const void *p)
{
    GCollection *c = collection_cast(p);
    return c && !c->installed ? c : nullptr;
}

static GpStatus collection_add_blob(GCollection *c, GFontBlob *blob)
{
    unsigned count = mime_ttf_face_count(blob->bytes, blob->size);
    BOOL added = FALSE;
    GpStatus status = Ok;
    text_lock();
    for (unsigned i = 0; i < count; i++) {
        GFace *face = face_open(blob, i);
        if (!face)
            continue;
        WCHAR name[LF_FACESIZE];
        size_t length = 0;
        if (!mime_ttf_name(&face->ttf, 1, (uint16_t *)name, LF_FACESIZE, &length) || !name[0] || length >= LF_FACESIZE) {
            face_free(face);
            continue;
        }
        GFamily *family = collection_add_family(c, name, FALSE);
        if (!family) {
            face_free(face);
            status = OutOfMemory;
            break;
        }
        int weight = face->metrics.weight;
        int slot = slot_of(weight > 500, face->metrics.italic);
        GFaceSlot *s = &family->slot[slot];
        added = TRUE;
        family->slots_loaded = TRUE;
        if (s->present && weight_distance(weight, slot) >= weight_distance(s->lf.lfWeight, slot)) {
            face_free(face);
            continue;
        }
        face_free(s->face);
        s->present = TRUE;
        s->tried = TRUE;
        s->face = face;
        s->lf.lfWeight = weight;
    }
    text_unlock();
    if (status != Ok)
        return status;
    return added ? Ok : GenericError;
}

WINGDIPAPI GpStatus WINAPI GdipNewPrivateFontCollection(GpFontCollection **collection)
{
    GP_REQUIRE_INIT();
    if (!collection)
        return InvalidParameter;
    GCollection *c = gp_alloc_zero(sizeof(*c));
    if (!c)
        return OutOfMemory;
    c->tag = GP_TAG_COLLECTION;
    *collection = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeletePrivateFontCollection(GpFontCollection **collection)
{
    GP_REQUIRE_INIT();
    if (!collection)
        return InvalidParameter;
    GCollection *c = private_cast(*collection);
    if (!c)
        return InvalidParameter;
    for (INT i = 0; i < c->count; i++)
        family_release(c->families[i]);
    for (INT i = 0; i < c->file_count; i++)
        gp_free(c->files[i]);
    gp_free(c->families);
    gp_free(c->files);
    c->tag = 0;
    gp_free(c);
    *collection = nullptr;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipNewInstalledFontCollection(GpFontCollection **collection)
{
    if (!collection)
        return InvalidParameter;
    *collection = &g_installed;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyCount(GpFontCollection *collection, INT *count)
{
    GCollection *c = collection_use(collection);
    if (!c || !count)
        return InvalidParameter;
    *count = c->count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyList(GpFontCollection *collection, INT sought,
                                                           GpFontFamily **families, INT *found)
{
    GCollection *c = collection_use(collection);
    if (!c || !families || !found)
        return InvalidParameter;
    INT n = sought < c->count ? sought : c->count;
    if (n < 0)
        n = 0;
    text_lock();
    for (INT i = 0; i < n; i++) {
        families[i] = c->families[i];
        family_retain(c->families[i]);
    }
    text_unlock();
    *found = n;
    return Ok;
}

/* the path a font file name stands for: whole paths as they are, other names in the Fonts directory, then on the path. */
static BOOL font_file_path(const WCHAR *name, WCHAR *path)
{
    BOOL absolute = name[0] == '\\' || ((name[0] | 0x20) >= 'a' && (name[0] | 0x20) <= 'z' && name[1] == ':' && name[2] == '\\');
    if (absolute) {
        if (lstrlenW(name) >= MAX_PATH)
            return FALSE;
        lstrcpyW(path, name);
        return TRUE;
    }
    WCHAR fonts[MAX_PATH];
    UINT n = GetWindowsDirectoryW(fonts, MAX_PATH);
    if (n && n + 8 + (UINT)lstrlenW(name) < MAX_PATH) {
        lstrcatW(fonts, L"\\Fonts\\");
        lstrcatW(fonts, name);
        if (GetFileAttributesW(fonts) != INVALID_FILE_ATTRIBUTES) {
            lstrcpyW(path, fonts);
            return TRUE;
        }
    }
    DWORD found = SearchPathW(nullptr, name, nullptr, MAX_PATH, path, nullptr);
    return found && found < MAX_PATH;
}

WINGDIPAPI GpStatus WINAPI GdipPrivateAddFontFile(GpFontCollection *collection, const WCHAR *filename)
{
    GCollection *c = private_cast(collection);
    if (!c || !filename)
        return InvalidParameter;
    WCHAR path[MAX_PATH];
    if (!font_file_path(filename, path))
        return FileNotFound;
    CharUpperBuffW(path, (DWORD)lstrlenW(path));
    for (INT i = 0; i < c->file_count; i++) {
        if (!lstrcmpW(c->files[i], path))
            return Ok;
    }
    BYTE *bytes = nullptr;
    SIZE_T size = 0;
    GpStatus status = gp_read_file(path, &bytes, &size);
    if (status != Ok)
        return status;
    GFontBlob *blob = blob_new(bytes, size);
    if (!blob) {
        gp_free(bytes);
        return OutOfMemory;
    }
    status = collection_add_blob(c, blob);
    blob_release(blob);
    if (status != Ok)
        return status;
    WCHAR **grown = gp_realloc(c->files, sizeof(WCHAR *) * (SIZE_T)(c->file_count + 1));
    WCHAR *copy = gp_dup(path, sizeof(WCHAR) * (SIZE_T)(lstrlenW(path) + 1));
    if (!grown || !copy) {
        if (grown)
            c->files = grown;
        gp_free(copy);
        return OutOfMemory;
    }
    c->files = grown;
    c->files[c->file_count++] = copy;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipPrivateAddMemoryFont(GpFontCollection *collection, const void *memory, INT length)
{
    GCollection *c = private_cast(collection);
    if (!c || !memory || length <= 0)
        return InvalidParameter;
    BYTE *bytes = gp_dup(memory, (SIZE_T)length);
    if (!bytes)
        return OutOfMemory;
    GFontBlob *blob = blob_new(bytes, (SIZE_T)length);
    if (!blob) {
        gp_free(bytes);
        return OutOfMemory;
    }
    GpStatus status = collection_add_blob(c, blob);
    blob_release(blob);
    return status;
}

/* ------------------------------------------------------------------ families */

WINGDIPAPI GpStatus WINAPI GdipCreateFontFamilyFromName(const WCHAR *name, GpFontCollection *collection,
                                                        GpFontFamily **family)
{
    GP_REQUIRE_INIT();
    if (!name || !family)
        return InvalidParameter;
    GCollection *c = collection ? collection_use(collection) : installed_collection();
    if (!c)
        return InvalidParameter;
    GFamily *found;
    if (c->installed) {
        found = gp_family_by_name(name);
    } else {
        text_lock();
        found = collection_find(c, name);
        text_unlock();
    }
    if (!found)
        return FontFamilyNotFound;
    family_retain(found);
    *family = found;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteFontFamily(GpFontFamily *family)
{
    if (is_generic(family))
        return Ok;
    GFamily *f = family ? GP_CAST(GFamily, GP_TAG_FAMILY, family) : nullptr;
    if (!f)
        return InvalidParameter;
    family_release(f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneFontFamily(GpFontFamily *family, GpFontFamily **clone)
{
    if (!family || !clone)
        return InvalidParameter;
    if (is_generic(family)) {
        *clone = family;
        return Ok;
    }
    GFamily *f = GP_CAST(GFamily, GP_TAG_FAMILY, family);
    if (!f)
        return InvalidParameter;
    family_retain(f);
    *clone = f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    *family = (GpFontFamily *)GP_FAMILY_SANS_SERIF;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySerif(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    *family = (GpFontFamily *)GP_FAMILY_SERIF;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilyMonospace(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    *family = (GpFontFamily *)GP_FAMILY_MONOSPACE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFamilyName(const GpFontFamily *family, WCHAR name[LF_FACESIZE], LANGID language)
{
    GFamily *f;
    GpStatus status = family_arg(family, &f);
    if (status != Ok)
        return status;
    if (!name)
        return InvalidParameter;
    lstrcpynW(name, f->name, LF_FACESIZE);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsStyleAvailable(const GpFontFamily *family, INT style, BOOL *available)
{
    GFamily *f;
    GpStatus status = family_arg(family, &f);
    if (status != Ok)
        return status;
    if (!available)
        return InvalidParameter;
    *available = gp_family_face(f, style, nullptr) != nullptr;
    return Ok;
}

/* the design number of a family for a style, 0 when the style has no face. */
static GpStatus family_metric(const GpFontFamily *family, INT style, UINT16 *out, INT which)
{
    GFamily *f;
    GpStatus status = family_arg(family, &f);
    if (status != Ok)
        return status;
    if (!out)
        return InvalidParameter;
    const GFace *face = gp_family_face(f, style, nullptr);
    INT value = 0;
    if (face) {
        switch (which) {
        case 0:
            value = face->em;
            break;
        case 1:
            value = face->ascent;
            break;
        case 2:
            value = face->descent;
            break;
        default:
            value = face->ascent + face->descent + face->gap;
            break;
        }
    }
    *out = (UINT16)value;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetEmHeight(const GpFontFamily *family, INT style, UINT16 *em)
{
    return family_metric(family, style, em, 0);
}

WINGDIPAPI GpStatus WINAPI GdipGetCellAscent(const GpFontFamily *family, INT style, UINT16 *ascent)
{
    return family_metric(family, style, ascent, 1);
}

WINGDIPAPI GpStatus WINAPI GdipGetCellDescent(const GpFontFamily *family, INT style, UINT16 *descent)
{
    return family_metric(family, style, descent, 2);
}

WINGDIPAPI GpStatus WINAPI GdipGetLineSpacing(const GpFontFamily *family, INT style, UINT16 *spacing)
{
    return family_metric(family, style, spacing, 3);
}

/* ------------------------------------------------------------------ glyphs */

UINT16 gp_face_glyph(const GFace *face, UINT32 cp) { return mime_ttf_glyph_index(&face->ttf, cp); }

INT gp_face_advance(const GFace *face, UINT16 glyph)
{
    int advance = 0;
    mime_ttf_glyph_hmetrics(&face->ttf, glyph, &advance, nullptr);
    return advance;
}

/* the points of the contour being walked. */
typedef struct OutlineBuilder {
    MimeVecPath *path;
    float ox, oy, ux, uy, vx, vy;
    MimeVecPoint *points;
    bool *on_curve;
    uint32_t count, capacity;
    bool failed;
} OutlineBuilder;

static void outline_point(void *ctx, float x, float y, bool on_curve)
{
    OutlineBuilder *b = ctx;
    if (b->failed)
        return;
    if (b->count == b->capacity) {
        uint32_t capacity = b->capacity ? b->capacity * 2 : 64;
        MimeVecPoint *points = gp_realloc(b->points, sizeof(MimeVecPoint) * capacity);
        if (points)
            b->points = points;
        bool *flags = points ? gp_realloc(b->on_curve, sizeof(bool) * capacity) : nullptr;
        if (flags)
            b->on_curve = flags;
        if (!points || !flags) {
            b->failed = true;
            return;
        }
        b->capacity = capacity;
    }
    b->points[b->count].x = b->ox + x * b->ux + y * b->vx;
    b->points[b->count].y = b->oy + x * b->uy + y * b->vy;
    b->on_curve[b->count] = on_curve;
    b->count++;
}

static MimeVecPoint midpoint(MimeVecPoint a, MimeVecPoint b)
{
    MimeVecPoint m = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    return m;
}

/* a quadratic curve from a through control to b is the cubic with controls two thirds of the way. */
static int outline_quad(MimeVecPath *path, MimeVecPoint a, MimeVecPoint control, MimeVecPoint b)
{
    const float k = 2.0f / 3.0f;
    return mime_vec_path_bezier_to(path, a.x + k * (control.x - a.x), a.y + k * (control.y - a.y),
                                   b.x + k * (control.x - b.x), b.y + k * (control.y - b.y), b.x, b.y);
}

/* turns the contour of on and off curve points into a closed figure. */
static void outline_end(void *ctx)
{
    OutlineBuilder *b = ctx;
    uint32_t n = b->count;
    b->count = 0;
    if (b->failed || n < 2)
        return;
    MimeVecPoint start, current;
    uint32_t first, last;
    if (b->on_curve[0]) {
        start = b->points[0];
        first = 1;
        last = n;
    } else if (b->on_curve[n - 1]) {
        start = b->points[n - 1];
        first = 0;
        last = n - 1;
    } else {
        start = midpoint(b->points[0], b->points[n - 1]);
        first = 0;
        last = n;
    }
    int e = mime_vec_path_move_to(b->path, start.x, start.y);
    current = start;
    bool pending = false;
    MimeVecPoint control = {0, 0};
    for (uint32_t i = first; i < last && e == MIME_VEC_OK; i++) {
        MimeVecPoint p = b->points[i];
        if (b->on_curve[i]) {
            e = pending ? outline_quad(b->path, current, control, p) : mime_vec_path_line_to(b->path, p.x, p.y);
            pending = false;
            current = p;
        } else if (pending) {
            MimeVecPoint m = midpoint(control, p);
            e = outline_quad(b->path, current, control, m);
            current = m;
            control = p;
        } else {
            control = p;
            pending = true;
        }
    }
    if (e == MIME_VEC_OK && pending)
        e = outline_quad(b->path, current, control, start);
    if (e == MIME_VEC_OK)
        e = mime_vec_path_close_figure(b->path);
    if (e != MIME_VEC_OK)
        b->failed = true;
}

GpStatus gp_face_outline(const GFace *face, UINT16 glyph, MimeVecPath *path, float ox, float oy, float ux, float uy,
                         float vx, float vy)
{
    OutlineBuilder b;
    memset(&b, 0, sizeof(b));
    b.path = path;
    b.ox = ox;
    b.oy = oy;
    b.ux = ux;
    b.uy = uy;
    b.vx = vx;
    b.vy = vy;
    MimeTtfSink sink = {outline_point, outline_end, &b};
    mime_ttf_walk_outline(&face->ttf, glyph, &sink);
    gp_free(b.points);
    gp_free(b.on_curve);
    return b.failed ? OutOfMemory : Ok;
}

/* ------------------------------------------------------------------ fallback */

/* the families a character the font lacks is looked for in, in the order of Windows' font linking. */
static const WCHAR *const g_fallback_names[] = {
    L"Segoe UI",        L"Microsoft YaHei", L"Microsoft JhengHei", L"Yu Gothic",   L"Malgun Gothic", L"MS Gothic",
    L"Segoe UI Symbol", L"Segoe UI Emoji",  L"Nirmala UI",         L"Leelawadee UI", L"Ebrima",       L"Gadugi",
    L"Sylfaen",         L"Cambria Math",    L"Arial Unicode MS",   L"Arial",
};
enum { FALLBACK_COUNT = sizeof(g_fallback_names) / sizeof(g_fallback_names[0]) };

/* 0 not looked up yet, 1 found, 2 not installed. */
static volatile LONG g_fallback_state[FALLBACK_COUNT];
static GFamily *g_fallback_family[FALLBACK_COUNT];

const GFace *gp_fallback_face(UINT32 cp, INT style, UINT16 *glyph)
{
    for (INT i = 0; i < FALLBACK_COUNT; i++) {
        if (g_fallback_state[i] == 0) {
            g_fallback_family[i] = installed_find(g_fallback_names[i]);
            InterlockedExchange(&g_fallback_state[i], g_fallback_family[i] ? 1 : 2);
        }
        if (g_fallback_state[i] != 1)
            continue;
        const GFace *face = gp_family_face(g_fallback_family[i], style, nullptr);
        if (!face)
            continue;
        UINT16 g = gp_face_glyph(face, cp);
        if (g) {
            *glyph = g;
            return face;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ fonts */

GFont *gp_font_cast(const void *p) { return GP_CAST(GFont, GP_TAG_FONT, p); }

static GpStatus font_make(GFamily *family, const void *as_given, float size, INT style, GpUnit unit, GpFont **out)
{
    if (!gp_family_face(family, style, nullptr))
        return FontStyleNotFound;
    GFont *font = gp_alloc_zero(sizeof(*font));
    if (!font)
        return OutOfMemory;
    font->tag = GP_TAG_FONT;
    font->family = as_given;
    font->size = size;
    font->style = style;
    font->unit = unit;
    if (!is_generic(as_given))
        family_retain(family);
    *out = font;
    return Ok;
}

float gp_font_world_size(float size, GpUnit unit, const GGraphics *g)
{
    if (unit == UnitWorld || unit == UnitPixel)
        return size;
    float dpi = g ? g->dpi_y : 96.0f;
    float factor = gp_unit_to_pixels(unit, dpi);
    if (g) {
        float sx, sy;
        gp_graphics_page_scale(g, &sx, &sy);
        if (sy > 0.0f)
            factor /= sy;
    }
    return size * factor;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFont(const GpFontFamily *family, REAL size, INT style, Unit unit, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!family || !font)
        return InvalidParameter;
    if (!(size > 0.0f))
        return InvalidParameter;
    if (unit != UnitWorld && (unit < UnitPixel || unit > UnitMillimeter))
        return InvalidParameter;
    GFamily *f;
    GpStatus status = family_arg(family, &f);
    if (status != Ok)
        return status;
    return font_make(f, family, size, style, unit, font);
}

WINGDIPAPI GpStatus WINAPI GdipCloneFont(GpFont *font, GpFont **clone)
{
    GFont *f = gp_font_cast(font);
    if (!f || !clone)
        return InvalidParameter;
    GFamily *family = gp_family_resolve(f->family);
    if (!family)
        return FontFamilyNotFound;
    return font_make(family, f->family, f->size, f->style, f->unit, clone);
}

WINGDIPAPI GpStatus WINAPI GdipDeleteFont(GpFont *font)
{
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    if (!is_generic(f->family))
        family_release((GFamily *)f->family);
    f->tag = 0;
    gp_free(f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFamily(GpFont *font, GpFontFamily **family)
{
    GFont *f = gp_font_cast(font);
    if (!f || !family)
        return InvalidParameter;
    if (!is_generic(f->family))
        family_retain((GFamily *)f->family);
    *family = (GpFontFamily *)f->family;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontStyle(GpFont *font, INT *style)
{
    GFont *f = gp_font_cast(font);
    if (!f || !style)
        return InvalidParameter;
    *style = f->style;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontSize(GpFont *font, REAL *size)
{
    GFont *f = gp_font_cast(font);
    if (!f || !size)
        return InvalidParameter;
    *size = f->size;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontUnit(GpFont *font, Unit *unit)
{
    GFont *f = gp_font_cast(font);
    if (!f || !unit)
        return InvalidParameter;
    *unit = f->unit;
    return Ok;
}

/* the line spacing of the font's face in design units, and the em it is measured in. */
static GpStatus font_line_spacing(const GFont *f, INT *spacing, INT *em)
{
    GFamily *family = gp_family_resolve(f->family);
    if (!family)
        return FontFamilyNotFound;
    const GFace *face = gp_family_face(family, f->style, nullptr);
    *spacing = face ? face->ascent + face->descent + face->gap : 0;
    *em = face ? face->em : 1;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontHeight(const GpFont *font, const GpGraphics *graphics, REAL *height)
{
    const GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    const GFont *f = gp_font_cast(font);
    if (!f || !height)
        return InvalidParameter;
    INT spacing, em;
    GpStatus status = font_line_spacing(f, &spacing, &em);
    if (status != Ok)
        return status;
    *height = (float)spacing * gp_font_world_size(f->size, f->unit, g) / (float)em;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontHeightGivenDPI(const GpFont *font, REAL dpi, REAL *height)
{
    const GFont *f = gp_font_cast(font);
    if (!f || !height)
        return InvalidParameter;
    INT spacing, em;
    GpStatus status = font_line_spacing(f, &spacing, &em);
    if (status != Ok)
        return status;
    float pixels = f->unit == UnitWorld || f->unit == UnitPixel ? f->size : f->size * gp_unit_to_pixels(f->unit, dpi);
    *height = (float)spacing * pixels / (float)em;
    return Ok;
}

/* ------------------------------------------------------------------ logical fonts */

/* the font a device context has realized, in world units. */
static GpStatus font_from_dc(HDC dc, GpFont **out)
{
    TEXTMETRICW tm;
    WCHAR face[LF_FACESIZE];
    if (!GetTextMetricsW(dc, &tm) || !GetTextFaceW(dc, LF_FACESIZE, face))
        return Win32Error;
    if (!(tm.tmPitchAndFamily & TMPF_TRUETYPE))
        return NotTrueTypeFont;
    GFamily *family = gp_family_by_name(face);
    if (!family)
        return FontFamilyNotFound;
    INT style = (tm.tmWeight > 400 ? FontStyleBold : 0) | (tm.tmItalic ? FontStyleItalic : 0) |
                (tm.tmUnderlined ? FontStyleUnderline : 0) | (tm.tmStruckOut ? FontStyleStrikeout : 0);
    float size = (float)(tm.tmHeight - tm.tmInternalLeading);
    if (!(size > 0.0f))
        size = 1.0f;
    return font_make(family, family, size, style, UnitWorld, out);
}

/* the font of a logical font is realized in a context of its own, which is not changed by it. */
static GpStatus font_from_logfont(HDC hdc, const LOGFONTW *lf, GpFont **out)
{
    HDC dc = CreateCompatibleDC(hdc);
    if (!dc)
        return OutOfMemory;
    /* the height of the logical font is in the units of the context it came with */
    SIZE window, viewport;
    if (GetWindowExtEx(hdc, &window) && GetViewportExtEx(hdc, &viewport)) {
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, window.cx, window.cy, nullptr);
        SetViewportExtEx(dc, viewport.cx, viewport.cy, nullptr);
    }
    HFONT hf = CreateFontIndirectW(lf);
    if (!hf) {
        DeleteDC(dc);
        return OutOfMemory;
    }
    HGDIOBJ old = SelectObject(dc, hf);
    GpStatus status = font_from_dc(dc, out);
    SelectObject(dc, old);
    DeleteObject(hf);
    DeleteDC(dc);
    return status;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromDC(HDC hdc, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!hdc || !font)
        return InvalidParameter;
    return font_from_dc(hdc, font);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW *lf, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!hdc || !lf || !font)
        return InvalidParameter;
    return font_from_logfont(hdc, lf, font);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA *lf, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!hdc || !lf || !font)
        return InvalidParameter;
    LOGFONTW wide;
    memset(&wide, 0, sizeof(wide));
    wide.lfHeight = lf->lfHeight;
    wide.lfWidth = lf->lfWidth;
    wide.lfEscapement = lf->lfEscapement;
    wide.lfOrientation = lf->lfOrientation;
    wide.lfWeight = lf->lfWeight;
    wide.lfItalic = lf->lfItalic;
    wide.lfUnderline = lf->lfUnderline;
    wide.lfStrikeOut = lf->lfStrikeOut;
    wide.lfCharSet = lf->lfCharSet;
    wide.lfOutPrecision = lf->lfOutPrecision;
    wide.lfClipPrecision = lf->lfClipPrecision;
    wide.lfQuality = lf->lfQuality;
    wide.lfPitchAndFamily = lf->lfPitchAndFamily;
    MultiByteToWideChar(CP_ACP, 0, lf->lfFaceName, -1, wide.lfFaceName, LF_FACESIZE);
    wide.lfFaceName[LF_FACESIZE - 1] = 0;
    return font_from_logfont(hdc, &wide, font);
}

static GpStatus logfont_of(GpFont *font, GpGraphics *graphics, LOGFONTW *lf)
{
    GGraphics *g = gp_graphics_cast(graphics);
    GFont *f = gp_font_cast(font);
    if (!f || !g || !lf)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    GFamily *family = gp_family_resolve(f->family);
    if (!family)
        return FontFamilyNotFound;
    gp_family_face(family, f->style, nullptr);
    const MimeVecMatrix *m = gp_graphics_w2d(g);
    float row = mime_vec_sqrtf(m->m21 * m->m21 + m->m22 * m->m22);
    float device = gp_font_world_size(f->size, f->unit, g) * row;
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = -(LONG)mime_vec_floorf(device + 0.5f);
    /* the turn of the world transform in tenths of a degree, counted the way gdi counts them. */
    LONG tenths = (LONG)(mime_vec_atan2f(m->m12, m->m11) * (1800.0f / 3.14159265f));
    if (tenths) {
        tenths = 3600 - tenths;
        if (tenths >= 3600)
            tenths -= 3600;
    }
    lf->lfEscapement = tenths;
    lf->lfOrientation = tenths;
    lf->lfWeight = f->style & FontStyleBold ? 700 : 400;
    lf->lfItalic = (f->style & FontStyleItalic) != 0;
    lf->lfUnderline = (f->style & FontStyleUnderline) != 0;
    lf->lfStrikeOut = (f->style & FontStyleStrikeout) != 0;
    lf->lfCharSet = family->charset;
    lstrcpynW(lf->lfFaceName, family->name, LF_FACESIZE);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLogFontW(GpFont *font, GpGraphics *graphics, LOGFONTW *lf)
{
    return logfont_of(font, graphics, lf);
}

WINGDIPAPI GpStatus WINAPI GdipGetLogFontA(GpFont *font, GpGraphics *graphics, LOGFONTA *lf)
{
    LOGFONTW wide;
    if (!lf)
        return InvalidParameter;
    GpStatus status = logfont_of(font, graphics, &wide);
    if (status != Ok)
        return status;
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = wide.lfHeight;
    lf->lfWidth = wide.lfWidth;
    lf->lfEscapement = wide.lfEscapement;
    lf->lfOrientation = wide.lfOrientation;
    lf->lfWeight = wide.lfWeight;
    lf->lfItalic = wide.lfItalic;
    lf->lfUnderline = wide.lfUnderline;
    lf->lfStrikeOut = wide.lfStrikeOut;
    lf->lfCharSet = wide.lfCharSet;
    lf->lfOutPrecision = wide.lfOutPrecision;
    lf->lfClipPrecision = wide.lfClipPrecision;
    lf->lfQuality = wide.lfQuality;
    lf->lfPitchAndFamily = wide.lfPitchAndFamily;
    WideCharToMultiByte(CP_ACP, 0, wide.lfFaceName, -1, lf->lfFaceName, LF_FACESIZE, nullptr, nullptr);
    lf->lfFaceName[LF_FACESIZE - 1] = 0;
    return Ok;
}
