/*
 * fonts: the families of the installed collection and of private collections, their faces, and the font objects of
 * the flat api. the installed families come from the font enumeration of gdi and their faces are read through
 * GetFontData when a font is first used; a private collection holds the files and memory images it was given.
 */
#include "text_private.h"
#include <ttf/ttf_internal.h>

/* ------------------------------------------------------------------ the lock */

/* the families, faces and outline caches are shared by the threads of a program. the lock is recursive. */
static volatile LONG g_owner;
static LONG g_depth;

static void text_lock(void)
{
    LONG me = (LONG)GetCurrentThreadId();
    if (g_owner == me) {
        g_depth++;
        return;
    }
    while (InterlockedCompareExchange(&g_owner, me, 0) != 0)
        Sleep(0);
    g_depth = 1;
}

static void text_unlock(void)
{
    if (--g_depth == 0)
        InterlockedExchange(&g_owner, 0);
}

/* ------------------------------------------------------------------ strings */

/* gdi+ compares names after raising a to z only, in at most 31 characters. */
static void name_copy(WCHAR *dst, const WCHAR *src)
{
    INT i = 0;
    for (; i < LF_FACESIZE - 1 && src[i]; i++)
        dst[i] = src[i];
    for (; i < LF_FACESIZE; i++)
        dst[i] = 0;
}

static void name_upper(WCHAR *dst, const WCHAR *src)
{
    INT i = 0;
    for (; i < LF_FACESIZE - 1 && src[i]; i++)
        dst[i] = src[i] >= 'a' && src[i] <= 'z' ? (WCHAR)(src[i] - 32) : src[i];
    for (; i < LF_FACESIZE; i++)
        dst[i] = 0;
}

static INT name_compare(const WCHAR *a, const WCHAR *b)
{
    for (INT i = 0; i < LF_FACESIZE; i++) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
        if (!a[i])
            break;
    }
    return 0;
}

/* ------------------------------------------------------------------ faces */

static void face_metrics(GFace *f)
{
    MimeTtfMetrics m;
    mime_ttf_metrics(&f->ttf, &m);
    f->upem = (UINT16)(m.units_per_em > 0 ? m.units_per_em : 2048);
    /* the design metrics come from hhea, or from the typographic ones the font asks for. */
    INT ascent = m.ascender, descent = -m.descender, gap = m.line_gap;
    if (m.use_typo_metrics && (m.typo_ascender || m.typo_descender)) {
        ascent = m.typo_ascender;
        descent = -m.typo_descender;
        gap = m.typo_line_gap;
    }
    if (ascent <= 0 && descent <= 0) {
        ascent = m.win_ascent;
        descent = m.win_descent;
        gap = 0;
    }
    f->ascent = (UINT16)(ascent < 0 ? 0 : ascent);
    f->descent = (UINT16)(descent < 0 ? 0 : descent);
    f->gap = (UINT16)(gap < 0 ? 0 : gap);
    f->underline_pos = m.underline_position;
    f->underline_thickness = m.underline_thickness > 0 ? m.underline_thickness : f->upem / 20;
    f->strike_pos = m.strikeout_position;
    f->strike_thickness = m.strikeout_size > 0 ? m.strikeout_size : f->underline_thickness;
    f->bold = m.weight > 500;
    f->italic = m.italic;
    f->symbol = f->ttf.cmap_symbol;
    f->space_glyph = mime_ttf_glyph_index(&f->ttf, 0x20);
}

/* a face over the bytes of a font file, which stay valid as long as the face. NULL when they are no font. */
static GFace *face_open(BYTE *data, SIZE_T size, BOOL owns, UINT index)
{
    GFace *f = gp_alloc_zero(sizeof(*f));
    if (!f)
        return nullptr;
    f->data = data;
    f->size = size;
    f->owns_data = owns;
    if (!mime_ttf_open(&f->ttf, data, size, index)) {
        gp_free(f);
        return nullptr;
    }
    f->valid = TRUE;
    face_metrics(f);
    return f;
}

UINT16 gp_face_glyph(GFace *face, UINT32 cp)
{
    UINT16 g = mime_ttf_glyph_index(&face->ttf, cp);
    if (!g && face->symbol && cp >= 0x20 && cp <= 0xFF)
        g = mime_ttf_glyph_index(&face->ttf, 0xF000 + cp);
    return g;
}

INT gp_face_advance(GFace *face, UINT16 glyph)
{
    INT advance = 0, bearing = 0;
    mime_ttf_glyph_hmetrics(&face->ttf, glyph, &advance, &bearing);
    return advance;
}

/* ------------------------------------------------------------------ outlines */

typedef struct OutlineBuilder {
    MimeVecPath *path;
    MimeVecPoint *pts;
    bool *on;
    UINT count, cap;
    BOOL failed;
} OutlineBuilder;

static void outline_point(void *ctx, float x, float y, bool on_curve)
{
    OutlineBuilder *b = ctx;
    if (b->failed)
        return;
    if (b->count == b->cap) {
        UINT cap = b->cap ? b->cap * 2 : 64;
        MimeVecPoint *pts = gp_realloc(b->pts, cap * sizeof(*pts));
        if (!pts) {
            b->failed = TRUE;
            return;
        }
        b->pts = pts;
        bool *on = gp_realloc(b->on, cap * sizeof(*on));
        if (!on) {
            b->failed = TRUE;
            return;
        }
        b->on = on;
        b->cap = cap;
    }
    b->pts[b->count].x = x;
    b->pts[b->count].y = y;
    b->on[b->count] = on_curve;
    b->count++;
}

/* the quadratic arc through a control point, as a cubic with the two thirds rule. */
static void outline_quad(MimeVecPath *p, MimeVecPoint a, MimeVecPoint c, MimeVecPoint b)
{
    mime_vec_path_bezier_to(p, a.x + (c.x - a.x) * (2.0f / 3.0f), a.y + (c.y - a.y) * (2.0f / 3.0f),
                            b.x + (c.x - b.x) * (2.0f / 3.0f), b.y + (c.y - b.y) * (2.0f / 3.0f), b.x, b.y);
}

static void outline_end(void *ctx)
{
    OutlineBuilder *b = ctx;
    UINT n = b->count;
    b->count = 0;
    if (b->failed || n < 2)
        return;
    MimeVecPoint start;
    UINT first, last;
    if (b->on[0]) {
        start = b->pts[0];
        first = 1;
        last = n;
    } else if (b->on[n - 1]) {
        start = b->pts[n - 1];
        first = 0;
        last = n - 1;
    } else {
        start.x = (b->pts[n - 1].x + b->pts[0].x) * 0.5f;
        start.y = (b->pts[n - 1].y + b->pts[0].y) * 0.5f;
        first = 0;
        last = n;
    }
    mime_vec_path_move_to(b->path, start.x, start.y);
    MimeVecPoint cur = start, off = {0, 0};
    BOOL have_off = FALSE;
    for (UINT i = first; i < last; i++) {
        MimeVecPoint p = b->pts[i];
        if (b->on[i]) {
            if (have_off)
                outline_quad(b->path, cur, off, p);
            else
                mime_vec_path_line_to(b->path, p.x, p.y);
            cur = p;
            have_off = FALSE;
        } else if (have_off) {
            MimeVecPoint mid = {(off.x + p.x) * 0.5f, (off.y + p.y) * 0.5f};
            outline_quad(b->path, cur, off, mid);
            cur = mid;
            off = p;
        } else {
            off = p;
            have_off = TRUE;
        }
    }
    if (have_off)
        outline_quad(b->path, cur, off, start);
    mime_vec_path_close_figure(b->path);
}

const MimeVecPath *gp_face_outline(GFace *face, UINT16 glyph)
{
    if (!face->valid || glyph >= mime_ttf_glyph_count(&face->ttf))
        return nullptr;
    text_lock();
    const MimeVecPath *result = nullptr;
    if (!face->outlines) {
        face->outline_count = mime_ttf_glyph_count(&face->ttf);
        face->outlines = gp_alloc_zero(face->outline_count * sizeof(MimeVecPath *));
    }
    if (face->outlines) {
        MimeVecPath *cached = face->outlines[glyph];
        if (!cached) {
            cached = gp_alloc_zero(sizeof(*cached));
            if (cached) {
                mime_vec_path_init(cached, &gp_allocator);
                OutlineBuilder b = {cached, nullptr, nullptr, 0, 0, FALSE};
                MimeTtfSink sink = {outline_point, outline_end, &b};
                BOOL ok = mime_ttf_walk_outline(&face->ttf, glyph, &sink) && !b.failed;
                gp_free(b.pts);
                gp_free(b.on);
                if (!ok)
                    mime_vec_path_clear(cached);
                face->outlines[glyph] = cached;
            }
        }
        result = cached && cached->count ? cached : nullptr;
    }
    text_unlock();
    return result;
}

/* ------------------------------------------------------------------ the device context for font data */

static HDC g_dc;

static HDC font_dc(void)
{
    if (!g_dc)
        g_dc = CreateCompatibleDC(nullptr);
    return g_dc;
}

/* a table of the font selected in the device context, NULL when it has none. */
static BYTE *read_table(HDC dc, DWORD tag, SIZE_T *size)
{
    DWORD n = GetFontData(dc, tag, 0, nullptr, 0);
    if (n == GDI_ERROR || !n)
        return nullptr;
    BYTE *data = gp_alloc(n);
    if (data && GetFontData(dc, tag, 0, data, n) != n) {
        gp_free(data);
        data = nullptr;
    }
    if (data)
        *size = n;
    return data;
}

/*
 * the bytes of the face a logical font stands for, through GetFontData. a face of a collection comes as the whole
 * collection, and the face is the one whose header table is the one of the font in the context.
 */
static BYTE *read_font_data(const LOGFONTW *lf, SIZE_T *size, UINT *index)
{
    HDC dc = font_dc();
    *index = 0;
    if (!dc)
        return nullptr;
    HFONT font = CreateFontIndirectW(lf);
    if (!font)
        return nullptr;
    HGDIOBJ old = SelectObject(dc, font);
    BYTE *data = read_table(dc, 0x66637474, size);
    if (data) {
        SIZE_T head_size = 0;
        BYTE *head = read_table(dc, 0x64616568, &head_size);
        UINT faces = mime_ttf_face_count(data, *size);
        for (UINT i = 0; head && i < faces; i++) {
            MimeTtfFont ttf;
            if (mime_ttf_open(&ttf, data, *size, i) && ttf.head.size == head_size &&
                !memcmp(ttf.head.data, head, head_size)) {
                *index = i;
                break;
            }
        }
        gp_free(head);
    } else {
        data = read_table(dc, 0, size);
    }
    SelectObject(dc, old);
    DeleteObject(font);
    return data;
}

/* ------------------------------------------------------------------ collections and families */

struct GBlob {
    struct GBlob *next;
    BYTE *data;
    SIZE_T size;
    WCHAR path[MAX_PATH];
};

static GCollection g_installed = {GP_TAG_COLLECTION, TRUE, FALSE, nullptr, 0, nullptr};

static GFamily *family_new(GCollection *c, const WCHAR *name)
{
    GFamily *f = gp_alloc_zero(sizeof(*f));
    if (!f)
        return nullptr;
    f->tag = GP_TAG_FAMILY;
    name_copy(f->name, name);
    name_upper(f->upper, name);
    f->collection = c;
    for (INT i = 0; i < 4; i++)
        f->slot[i].lf.lfCharSet = DEFAULT_CHARSET;
    return f;
}

/* adds a family in the order of the names, ignoring a name the collection has. */
static GFamily *collection_add(GCollection *c, const WCHAR *name)
{
    WCHAR upper[LF_FACESIZE];
    name_upper(upper, name);
    GFamily **link = &c->first;
    while (*link) {
        INT order = name_compare(upper, (*link)->upper);
        if (order == 0)
            return *link;
        if (order < 0)
            break;
        link = &(*link)->next;
    }
    GFamily *f = family_new(c, name);
    if (!f)
        return nullptr;
    f->next = *link;
    *link = f;
    c->family_count++;
    return f;
}

static GFamily *collection_find(GCollection *c, const WCHAR *name)
{
    WCHAR upper[LF_FACESIZE];
    name_upper(upper, name);
    for (GFamily *f = c->first; f; f = f->next)
        if (!name_compare(upper, f->upper))
            return f;
    return nullptr;
}

static int CALLBACK enum_family_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    if (!(type & TRUETYPE_FONTTYPE) || !lf->lfFaceName[0] || lf->lfFaceName[0] == '@')
        return 1;
    collection_add((GCollection *)param, lf->lfFaceName);
    return 1;
}

static void installed_load(void)
{
    if (g_installed.loaded)
        return;
    HDC dc = font_dc();
    if (dc) {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &lf, enum_family_proc, (LPARAM)&g_installed, 0);
    }
    g_installed.loaded = TRUE;
}

static BOOL collection_load(GCollection *c)
{
    if (c->installed) {
        text_lock();
        installed_load();
        text_unlock();
    }
    return TRUE;
}

typedef struct FaceEnum {
    GFamily *family;
} FaceEnum;

static INT slot_of(INT weight, BOOL italic)
{
    return (weight > 500 ? 1 : 0) | (italic ? 2 : 0);
}

/* how close a face is to the regular or the bold of its slot, for picking among several. */
static INT slot_distance(INT weight, INT slot)
{
    INT want = slot & 1 ? 700 : 400;
    return weight > want ? weight - want : want - weight;
}

static int CALLBACK enum_face_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    FaceEnum *e = (FaceEnum *)param;
    GFamily *f = e->family;
    if (!(type & TRUETYPE_FONTTYPE))
        return 1;
    WCHAR upper[LF_FACESIZE];
    name_upper(upper, lf->lfFaceName);
    if (name_compare(upper, f->upper))
        return 1;
    INT s = slot_of(lf->lfWeight ? lf->lfWeight : 400, lf->lfItalic != 0);
    GSlot *slot = &f->slot[s];
    if (slot->present && slot_distance(slot->lf.lfWeight, s) <= slot_distance(lf->lfWeight, s))
        return 1;
    slot->present = TRUE;
    slot->lf = *lf;
    slot->lf.lfHeight = -64;
    slot->lf.lfWidth = 0;
    slot->lf.lfEscapement = slot->lf.lfOrientation = 0;
    slot->lf.lfUnderline = slot->lf.lfStrikeOut = 0;
    slot->lf.lfOutPrecision = OUT_TT_PRECIS;
    slot->lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    slot->lf.lfQuality = DEFAULT_QUALITY;
    slot->lf.lfPitchAndFamily = DEFAULT_PITCH;
    return 1;
}

/* the styles a family has. a family with no regular face takes one of its other faces as the regular one. */
static BOOL family_load(GFamily *f)
{
    BOOL ok = TRUE;
    text_lock();
    if (!f->loaded) {
        if (f->collection->installed) {
            HDC dc = font_dc();
            if (dc) {
                FaceEnum e = {f};
                LOGFONTW lf;
                memset(&lf, 0, sizeof(lf));
                lf.lfCharSet = DEFAULT_CHARSET;
                name_copy(lf.lfFaceName, f->name);
                EnumFontFamiliesExW(dc, &lf, enum_face_proc, (LPARAM)&e, 0);
            }
        }
        if (!f->slot[0].present) {
            static const INT order[3] = {2, 1, 3};
            for (INT i = 0; i < 3; i++) {
                if (f->slot[order[i]].present) {
                    f->slot[0] = f->slot[order[i]];
                    f->slot[0].face = nullptr;
                    f->slot[0].failed = FALSE;
                    f->synthesized_regular = TRUE;
                    break;
                }
            }
        }
        f->loaded = TRUE;
    }
    if (!f->slot[0].present)
        ok = FALSE;
    text_unlock();
    return ok;
}

static GFace *slot_face(GFamily *f, INT s)
{
    GSlot *slot = &f->slot[s];
    if (!slot->present || slot->failed)
        return nullptr;
    if (slot->face)
        return slot->face;
    GFace *face = nullptr;
    if (f->collection->installed) {
        SIZE_T size = 0;
        UINT index = 0;
        BYTE *data = read_font_data(&slot->lf, &size, &index);
        if (data) {
            face = face_open(data, size, TRUE, index);
            if (!face)
                gp_free(data);
        }
    } else if (slot->blob) {
        face = face_open(slot->blob, slot->blob_size, FALSE, slot->face_index);
    }
    if (!face) {
        slot->failed = TRUE;
        return nullptr;
    }
    slot->face = face;
    return face;
}

GFace *gp_family_face(GFamily *f, INT style)
{
    if (!f || !family_load(f))
        return nullptr;
    /* the order gdi+ tries: the style itself, then what a missing bold or italic is shown with. */
    static const INT order[4][4] = {{0, -1, -1, -1}, {1, 0, -1, -1}, {2, 0, -1, -1}, {3, 1, 2, 0}};
    text_lock();
    GFace *face = nullptr;
    for (INT i = 0; i < 4 && !face; i++) {
        INT s = order[style & 3][i];
        if (s < 0)
            break;
        face = slot_face(f, s);
    }
    text_unlock();
    return face;
}

/* ------------------------------------------------------------------ names and substitution */

/* the names programs use for fonts that windows maps to others. */
static const WCHAR *const g_subst[][2] = {
    {L"MS Shell Dlg", L"Microsoft Sans Serif"},
    {L"MS Shell Dlg 2", L"Tahoma"},
    {L"MS Sans Serif", L"Microsoft Sans Serif"},
    {L"Helv", L"Microsoft Sans Serif"},
    {L"Helvetica", L"Arial"},
    {L"Tms Rmn", L"Times New Roman"},
    {L"Times", L"Times New Roman"},
    {L"MS Serif", L"Times New Roman"},
    {L"Courier", L"Courier New"},
    {L"Arial Baltic", L"Arial"},
    {L"Arial CE", L"Arial"},
    {L"Arial CYR", L"Arial"},
    {L"Arial Greek", L"Arial"},
    {L"Arial TUR", L"Arial"},
    {L"Courier New Baltic", L"Courier New"},
    {L"Courier New CE", L"Courier New"},
    {L"Courier New CYR", L"Courier New"},
    {L"Courier New Greek", L"Courier New"},
    {L"Courier New TUR", L"Courier New"},
    {L"Times New Roman Baltic", L"Times New Roman"},
    {L"Times New Roman CE", L"Times New Roman"},
    {L"Times New Roman CYR", L"Times New Roman"},
    {L"Times New Roman Greek", L"Times New Roman"},
    {L"Times New Roman TUR", L"Times New Roman"},
};

static GFamily *family_by_name(GCollection *c, const WCHAR *name)
{
    GFamily *f = collection_find(c, name);
    if (f)
        return f;
    WCHAR upper[LF_FACESIZE];
    name_upper(upper, name);
    for (UINT i = 0; i < sizeof(g_subst) / sizeof(g_subst[0]); i++) {
        WCHAR from[LF_FACESIZE];
        name_upper(from, g_subst[i][0]);
        if (!name_compare(from, upper))
            return collection_find(&g_installed, g_subst[i][1]);
    }
    return nullptr;
}

GFamily *gp_family_find(const WCHAR *name)
{
    collection_load(&g_installed);
    text_lock();
    GFamily *f = family_by_name(&g_installed, name);
    text_unlock();
    return f;
}

static GFamily *find_first(const WCHAR *const *names, UINT count)
{
    for (UINT i = 0; i < count; i++) {
        GFamily *f = gp_family_find(names[i]);
        if (f && family_load(f))
            return f;
    }
    return nullptr;
}

static GFamily *generic_sans(void)
{
    static const WCHAR *const names[] = {L"Microsoft Sans Serif", L"Arial", L"Tahoma"};
    GFamily *f = find_first(names, 3);
    if (!f) {
        collection_load(&g_installed);
        f = g_installed.first;
    }
    return f;
}

static GFamily *generic_serif(void)
{
    static const WCHAR *const names[] = {L"Times New Roman"};
    GFamily *f = find_first(names, 1);
    return f ? f : generic_sans();
}

static GFamily *generic_mono(void)
{
    static const WCHAR *const names[] = {L"Courier New", L"Lucida Console"};
    GFamily *f = find_first(names, 2);
    return f ? f : generic_sans();
}

GFamily *gp_family_cast(const void *p)
{
    if (p == GP_FAMILY_SANS_SERIF)
        return generic_sans();
    if (p == GP_FAMILY_SERIF)
        return generic_serif();
    if (p == GP_FAMILY_MONOSPACE)
        return generic_mono();
    return GP_CAST(GFamily, GP_TAG_FAMILY, p);
}

/* the fonts programs reach for when the one asked has no glyph, in the order they are tried. */
static const WCHAR *const g_fallbacks[] = {
    L"Segoe UI",         L"Microsoft YaHei",   L"Microsoft JhengHei", L"Yu Gothic",      L"Malgun Gothic",
    L"MS Gothic",        L"Segoe UI Symbol",   L"Segoe UI Emoji",     L"Nirmala UI",     L"Leelawadee UI",
    L"Ebrima",           L"Gadugi",            L"Sylfaen",            L"Cambria Math",   L"Arial Unicode MS",
    L"Arial",
};

GFamily *gp_family_fallback(UINT32 cp, const GFamily *avoid)
{
    for (UINT i = 0; i < sizeof(g_fallbacks) / sizeof(g_fallbacks[0]); i++) {
        GFamily *f = gp_family_find(g_fallbacks[i]);
        if (!f || f == avoid)
            continue;
        GFace *face = gp_family_face(f, 0);
        if (face && gp_face_glyph(face, cp))
            return f;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ private collections */

static void blob_free_all(GCollection *c)
{
    while (c->blobs) {
        struct GBlob *b = c->blobs;
        c->blobs = b->next;
        gp_free(b->data);
        gp_free(b);
    }
}

static void family_free(GFamily *f)
{
    for (INT i = 0; i < 4; i++) {
        GFace *face = f->slot[i].face;
        if (!face)
            continue;
        /* a face may serve two slots, free it from the first one only. */
        BOOL shared = FALSE;
        for (INT j = 0; j < i; j++)
            shared |= f->slot[j].face == face;
        if (shared)
            continue;
        if (face->outlines) {
            for (UINT g = 0; g < face->outline_count; g++) {
                if (face->outlines[g]) {
                    mime_vec_path_free(face->outlines[g]);
                    gp_free(face->outlines[g]);
                }
            }
            gp_free(face->outlines);
        }
        if (face->owns_data)
            gp_free(face->data);
        gp_free(face);
    }
    gp_free(f);
}

static void collection_free(GCollection *c)
{
    GFamily *f = c->first;
    while (f) {
        GFamily *next = f->next;
        family_free(f);
        f = next;
    }
    blob_free_all(c);
    c->tag = 0;
    gp_free(c);
}

/* the families a font file holds go into the collection by the family name of their win32 names. */
static GpStatus collection_add_blob(GCollection *c, BYTE *data, SIZE_T size, const WCHAR *path)
{
    UINT faces = mime_ttf_face_count(data, size);
    if (!faces) {
        gp_free(data);
        return GenericError;
    }
    struct GBlob *blob = gp_alloc_zero(sizeof(*blob));
    if (!blob) {
        gp_free(data);
        return OutOfMemory;
    }
    blob->data = data;
    blob->size = size;
    if (path) {
        SIZE_T n = 0;
        while (path[n] && n < MAX_PATH - 1) {
            blob->path[n] = path[n] >= 'a' && path[n] <= 'z' ? (WCHAR)(path[n] - 32) : path[n];
            n++;
        }
        blob->path[n] = 0;
    }
    blob->next = c->blobs;
    c->blobs = blob;
    for (UINT i = 0; i < faces; i++) {
        MimeTtfFont ttf;
        if (!mime_ttf_open(&ttf, data, size, i))
            continue;
        MimeTtfMetrics m;
        mime_ttf_metrics(&ttf, &m);
        uint16_t name[64];
        size_t length = 0;
        if (!mime_ttf_name(&ttf, 1, name, 64, &length) || !length)
            if (!mime_ttf_name(&ttf, 16, name, 64, &length) || !length)
                continue;
        WCHAR family[LF_FACESIZE];
        for (UINT k = 0; k < LF_FACESIZE; k++)
            family[k] = k < length && k < 63 ? name[k] : 0;
        family[LF_FACESIZE - 1] = 0;
        GFamily *f = collection_add(c, family);
        if (!f)
            return OutOfMemory;
        INT s = slot_of(m.weight, m.italic);
        GSlot *slot = &f->slot[s];
        if (slot->present && slot_distance(slot->lf.lfWeight, s) <= slot_distance(m.weight, s))
            continue;
        slot->present = TRUE;
        slot->lf.lfWeight = m.weight;
        slot->blob = data;
        slot->blob_size = size;
        slot->face_index = i;
        slot->face = nullptr;
        slot->failed = FALSE;
        f->loaded = FALSE;
        f->synthesized_regular = FALSE;
    }
    return Ok;
}

/* ------------------------------------------------------------------ fonts */

GFont *gp_font_cast(const void *p)
{
    GFont *f = GP_CAST(GFont, GP_TAG_FONT, p);
    return f && f->family ? f : nullptr;
}

float gp_font_world_size(const GFont *font, const GGraphics *g)
{
    float k = 1.0f;
    switch (font->unit) {
    case UnitPoint:
    case UnitInch:
    case UnitDocument:
    case UnitMillimeter: {
        /* gdi+ takes the vertical resolution and the vertical page scale for both axes. */
        float px = gp_unit_to_pixels(font->unit, g ? g->dpi_y : 96.0f);
        float sx = 1.0f, sy = 1.0f;
        if (g)
            gp_graphics_page_scale(g, &sx, &sy);
        k = sy > 0.0f ? px / sy : px;
        break;
    }
    default:
        break;
    }
    return font->size * k;
}

/* the size of the font in pixels at a resolution, for the unit it was made in. */
static float font_pixels_at(const GFont *font, float dpi)
{
    switch (font->unit) {
    case UnitPoint:
        return font->size * dpi / 72.0f;
    case UnitInch:
        return font->size * dpi;
    case UnitDocument:
        return font->size * dpi / 300.0f;
    case UnitMillimeter:
        return font->size * dpi / 25.4f;
    default:
        return font->size;
    }
}

static GFont *font_new(GFamily *family, float size, INT style, Unit unit)
{
    GFont *font = gp_alloc_zero(sizeof(*font));
    if (!font)
        return nullptr;
    font->tag = GP_TAG_FONT;
    font->family = family;
    font->size = size;
    font->style = style;
    font->unit = unit;
    return font;
}

/* a font as the device context holds it: the size without the internal leading, and the style flags. */
static GFont *font_from_dc(HDC dc)
{
    TEXTMETRICW tm;
    WCHAR name[LF_FACESIZE + 1];
    memset(name, 0, sizeof(name));
    if (!GetTextMetricsW(dc, &tm))
        return nullptr;
    GetTextFaceW(dc, LF_FACESIZE, name);
    name[LF_FACESIZE - 1] = 0;
    INT style = 0;
    if (tm.tmWeight > 400)
        style |= FontStyleBold;
    if (tm.tmItalic)
        style |= FontStyleItalic;
    if (tm.tmUnderlined)
        style |= FontStyleUnderline;
    if (tm.tmStruckOut)
        style |= FontStyleStrikeout;
    GFamily *family = gp_family_find(name);
    if (family && !gp_family_face(family, style))
        family = nullptr;
    if (!family)
        return nullptr;
    return font_new(family, (float)(tm.tmHeight - tm.tmInternalLeading), style, UnitWorld);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFont(const GpFontFamily *family, REAL size, INT style, Unit unit,
                                          GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!font)
        return InvalidParameter;
    if (size <= 0.0f || unit == UnitDisplay || (UINT)unit > UnitMillimeter)
        return InvalidParameter;
    GFamily *f = gp_family_cast(family);
    if (!f)
        return InvalidParameter;
    GFont *created = font_new(f, size, style, unit);
    if (!created)
        return OutOfMemory;
    if (!gp_family_face(f, style)) {
        gp_free(created);
        *font = nullptr;
        return FontStyleNotFound;
    }
    *font = created;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneFont(GpFont *font, GpFont **clone)
{
    if (!clone)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    GFont *copy = font_new(f->family, f->size, f->style, f->unit);
    if (!copy)
        return OutOfMemory;
    *clone = copy;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteFont(GpFont *font)
{
    GFont *f = GP_CAST(GFont, GP_TAG_FONT, font);
    if (!f)
        return InvalidParameter;
    f->tag = 0;
    gp_free(f);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFamily(GpFont *font, GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    *family = f->family;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontStyle(GpFont *font, INT *style)
{
    if (!style)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    *style = f->style;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontSize(GpFont *font, REAL *size)
{
    if (!size)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    *size = f->size;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontUnit(GpFont *font, Unit *unit)
{
    if (!unit)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    *unit = f->unit;
    return Ok;
}

/* the line spacing of a font in pixels of the given size. */
static GpStatus font_line_spacing(const GFont *f, float pixels, float *height)
{
    GFace *face = gp_family_face(f->family, f->style);
    if (!face)
        return InvalidParameter;
    *height = (float)(UINT16)(face->ascent + face->descent + face->gap) * pixels / (float)face->upem;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontHeight(const GpFont *font, const GpGraphics *graphics, REAL *height)
{
    if (!height)
        return InvalidParameter;
    const GGraphics *g = nullptr;
    if (graphics) {
        g = gp_graphics_cast(graphics);
        if (!g)
            return InvalidParameter;
        if (g->got_dc)
            return ObjectBusy;
    }
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    return font_line_spacing(f, gp_font_world_size(f, g), height);
}

WINGDIPAPI GpStatus WINAPI GdipGetFontHeightGivenDPI(const GpFont *font, REAL dpi, REAL *height)
{
    if (!height)
        return InvalidParameter;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    return font_line_spacing(f, font_pixels_at(f, dpi), height);
}

/* the charset gdi lists the family with, the first truetype entry of its enumeration. */
static int CALLBACK charset_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    if (!(type & TRUETYPE_FONTTYPE))
        return 1;
    *(BYTE *)param = lf->lfCharSet;
    return 0;
}

static BYTE family_charset(const GFamily *family)
{
    BYTE charset = DEFAULT_CHARSET;
    text_lock();
    HDC dc = font_dc();
    if (dc) {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        name_copy(lf.lfFaceName, family->name);
        EnumFontFamiliesExW(dc, &lf, charset_proc, (LPARAM)&charset, 0);
    }
    text_unlock();
    return charset;
}

static GpStatus font_logfont(const GFont *f, const GGraphics *g, LOGFONTW *lf)
{
    if (!gp_family_face(f->family, f->style))
        return GenericError;
    /* the size in device pixels, and the turn of the world transform in tenths of a degree. */
    const MimeVecMatrix *m = gp_graphics_w2d((GGraphics *)g);
    float sy = mime_vec_sqrtf(m->m21 * m->m21 + m->m22 * m->m22);
    float angle = mime_vec_atan2f(m->m12, m->m11);
    INT turn = (INT)(angle * 1800.0f / 3.14159265358979f);
    INT tenths = turn ? 3600 - turn : 0;
    float px = gp_font_world_size(f, g) * sy;
    memset(lf, 0, sizeof(*lf));
    lf->lfHeight = -(LONG)mime_vec_floorf(px + 0.5f);
    lf->lfEscapement = tenths;
    lf->lfOrientation = tenths;
    lf->lfWeight = f->style & FontStyleBold ? 700 : 400;
    lf->lfItalic = (f->style & FontStyleItalic) != 0;
    lf->lfUnderline = (f->style & FontStyleUnderline) != 0;
    lf->lfStrikeOut = (f->style & FontStyleStrikeout) != 0;
    lf->lfCharSet = family_charset(f->family);
    name_copy(lf->lfFaceName, f->family->name);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetLogFontW(GpFont *font, GpGraphics *graphics, LOGFONTW *logfont)
{
    if (!logfont)
        return InvalidParameter;
    GGraphics *g = gp_graphics_cast(graphics);
    if (!g)
        return InvalidParameter;
    if (g->got_dc)
        return ObjectBusy;
    GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    return font_logfont(f, g, logfont);
}

WINGDIPAPI GpStatus WINAPI GdipGetLogFontA(GpFont *font, GpGraphics *graphics, LOGFONTA *logfont)
{
    if (!logfont)
        return InvalidParameter;
    LOGFONTW w;
    GpStatus status = GdipGetLogFontW(font, graphics, &w);
    if (status != Ok)
        return status;
    memcpy(logfont, &w, offsetof(LOGFONTW, lfFaceName));
    memset(logfont->lfFaceName, 0, sizeof(logfont->lfFaceName));
    WideCharToMultiByte(CP_ACP, 0, w.lfFaceName, -1, logfont->lfFaceName, LF_FACESIZE, nullptr, nullptr);
    logfont->lfFaceName[LF_FACESIZE - 1] = 0;
    return Ok;
}

/* the font a logical font is realized as in the device context, the one gdi would draw with. */
static GpStatus font_from_logfont(HDC dc, const LOGFONTW *lf, GpFont **out)
{
    if (!dc || !lf || !out)
        return InvalidParameter;
    GFont *font = nullptr;
    /* a device context of its own, so that the font selected does not meet what the caller's context kept. */
    HDC scratch = CreateCompatibleDC(dc);
    HFONT hf = scratch ? CreateFontIndirectW(lf) : nullptr;
    if (hf) {
        HGDIOBJ old = SelectObject(scratch, hf);
        font = font_from_dc(scratch);
        SelectObject(scratch, old);
        DeleteObject(hf);
    }
    if (scratch)
        DeleteDC(scratch);
    if (!font) {
        *out = nullptr;
        return NotTrueTypeFont;
    }
    *out = font;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromDC(HDC hdc, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!hdc || !font)
        return InvalidParameter;
    GFont *created = font_from_dc(hdc);
    if (!created) {
        *font = nullptr;
        return NotTrueTypeFont;
    }
    *font = created;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW *logfont, GpFont **font)
{
    GP_REQUIRE_INIT();
    return font_from_logfont(hdc, logfont, font);
}

WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA *logfont, GpFont **font)
{
    GP_REQUIRE_INIT();
    if (!hdc || !logfont || !font)
        return InvalidParameter;
    LOGFONTW w;
    memcpy(&w, logfont, offsetof(LOGFONTA, lfFaceName));
    memset(w.lfFaceName, 0, sizeof(w.lfFaceName));
    MultiByteToWideChar(CP_ACP, 0, logfont->lfFaceName, -1, w.lfFaceName, LF_FACESIZE);
    w.lfFaceName[LF_FACESIZE - 1] = 0;
    return font_from_logfont(hdc, &w, font);
}

/* ------------------------------------------------------------------ font families */

WINGDIPAPI GpStatus WINAPI GdipCreateFontFamilyFromName(const WCHAR *name, GpFontCollection *collection,
                                                        GpFontFamily **family)
{
    GP_REQUIRE_INIT();
    if (!name || !family)
        return InvalidParameter;
    GCollection *c = &g_installed;
    if (collection) {
        c = GP_CAST(GCollection, GP_TAG_COLLECTION, collection);
        if (!c)
            return InvalidParameter;
    }
    collection_load(c);
    text_lock();
    GFamily *f = family_by_name(c, name);
    text_unlock();
    if (!f) {
        *family = nullptr;
        return FontFamilyNotFound;
    }
    *family = f;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteFontFamily(GpFontFamily *family)
{
    if (!family)
        return InvalidParameter;
    if (family == GP_FAMILY_SANS_SERIF || family == GP_FAMILY_SERIF || family == GP_FAMILY_MONOSPACE)
        return Ok;
    return GP_CAST(GFamily, GP_TAG_FAMILY, family) ? Ok : InvalidParameter;
}

WINGDIPAPI GpStatus WINAPI GdipCloneFontFamily(GpFontFamily *family, GpFontFamily **clone)
{
    if (!clone)
        return InvalidParameter;
    if (family == GP_FAMILY_SANS_SERIF || family == GP_FAMILY_SERIF || family == GP_FAMILY_MONOSPACE) {
        *clone = family;
        return Ok;
    }
    if (!GP_CAST(GFamily, GP_TAG_FAMILY, family))
        return InvalidParameter;
    *clone = family;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    if (!generic_sans())
        return FontFamilyNotFound;
    *family = GP_FAMILY_SANS_SERIF;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySerif(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    if (!generic_serif())
        return FontFamilyNotFound;
    *family = GP_FAMILY_SERIF;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilyMonospace(GpFontFamily **family)
{
    if (!family)
        return InvalidParameter;
    if (!generic_mono())
        return FontFamilyNotFound;
    *family = GP_FAMILY_MONOSPACE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFamilyName(const GpFontFamily *family, WCHAR name[LF_FACESIZE], LANGID language)
{
    GFamily *f = gp_family_cast(family);
    if (!f || !name)
        return InvalidParameter;
    name_copy(name, f->name);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsStyleAvailable(const GpFontFamily *family, INT style, BOOL *available)
{
    if (!available)
        return InvalidParameter;
    GFamily *f = gp_family_cast(family);
    if (!f)
        return InvalidParameter;
    *available = gp_family_face(f, style) != nullptr;
    return Ok;
}

/* the metrics of a family in design units, 0 when no face of it can be read. */
static GpStatus family_metric(const GpFontFamily *family, INT style, UINT16 *out, INT which)
{
    if (!out)
        return InvalidParameter;
    GFamily *f = gp_family_cast(family);
    if (!f)
        return InvalidParameter;
    GFace *face = gp_family_face(f, style);
    UINT16 v = 0;
    if (face) {
        switch (which) {
        case 0:
            v = face->upem;
            break;
        case 1:
            v = face->ascent;
            break;
        case 2:
            v = face->descent;
            break;
        default:
            v = (UINT16)(face->ascent + face->descent + face->gap);
            break;
        }
    }
    *out = v;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetEmHeight(const GpFontFamily *family, INT style, UINT16 *height)
{
    return family_metric(family, style, height, 0);
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

/* ------------------------------------------------------------------ collections */

WINGDIPAPI GpStatus WINAPI GdipNewInstalledFontCollection(GpFontCollection **collection)
{
    if (!collection)
        return InvalidParameter;
    *collection = &g_installed;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipNewPrivateFontCollection(GpFontCollection **collection)
{
    GP_REQUIRE_INIT();
    if (!collection)
        return InvalidParameter;
    GCollection *c = gp_alloc_zero(sizeof(*c));
    if (!c) {
        *collection = nullptr;
        return OutOfMemory;
    }
    c->tag = GP_TAG_COLLECTION;
    c->loaded = TRUE;
    *collection = c;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeletePrivateFontCollection(GpFontCollection **collection)
{
    GP_REQUIRE_INIT();
    if (!collection)
        return InvalidParameter;
    GCollection *c = GP_CAST(GCollection, GP_TAG_COLLECTION, *collection);
    if (*collection && !c)
        return InvalidParameter;
    if (c && !c->installed)
        collection_free(c);
    *collection = nullptr;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyCount(GpFontCollection *collection, INT *count)
{
    GCollection *c = GP_CAST(GCollection, GP_TAG_COLLECTION, collection);
    if (!count || !c)
        return InvalidParameter;
    collection_load(c);
    *count = (INT)c->family_count;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyList(GpFontCollection *collection, INT sought,
                                                           GpFontFamily **families, INT *found)
{
    GCollection *c = GP_CAST(GCollection, GP_TAG_COLLECTION, collection);
    if (!families || !found || !c)
        return InvalidParameter;
    collection_load(c);
    INT n = 0;
    for (GFamily *f = c->first; f && n < sought; f = f->next)
        families[n++] = f;
    *found = n;
    return Ok;
}

/* the file a name stands for: the fonts directory first for a relative name, then the usual search. */
static BOOL resolve_font_path(const WCHAR *name, WCHAR *path)
{
    BOOL absolute = name[0] == '\\' || (name[0] && name[1] == ':' && name[2] == '\\');
    WCHAR *part = nullptr;
    DWORD n = 0;
    if (!absolute) {
        WCHAR fonts[MAX_PATH];
        UINT w = GetWindowsDirectoryW(fonts, MAX_PATH - 8);
        if (w && w < MAX_PATH - 8) {
            memcpy(fonts + w, L"\\Fonts", 7 * sizeof(WCHAR));
            n = SearchPathW(fonts, name, nullptr, MAX_PATH, path, &part);
        }
    }
    if (!n)
        n = SearchPathW(nullptr, name, nullptr, MAX_PATH, path, &part);
    return n && n < MAX_PATH;
}

static BOOL path_equal(const WCHAR *a, const WCHAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static BYTE *read_whole_file(const WCHAR *path, SIZE_T *size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER len;
    BYTE *data = nullptr;
    if (GetFileSizeEx(h, &len) && len.QuadPart > 0 && len.QuadPart < 0x10000000ll) {
        data = gp_alloc((SIZE_T)len.QuadPart);
        SIZE_T got = 0;
        while (data && got < (SIZE_T)len.QuadPart) {
            DWORD n = 0;
            if (!ReadFile(h, data + got, (DWORD)((SIZE_T)len.QuadPart - got), &n, nullptr) || !n) {
                gp_free(data);
                data = nullptr;
                break;
            }
            got += n;
        }
        if (data)
            *size = got;
    }
    CloseHandle(h);
    return data;
}

WINGDIPAPI GpStatus WINAPI GdipPrivateAddFontFile(GpFontCollection *collection, const WCHAR *filename)
{
    GCollection *c = GP_CAST(GCollection, GP_TAG_COLLECTION, collection);
    if (!filename || !c || c->installed)
        return InvalidParameter;
    WCHAR path[MAX_PATH];
    if (!resolve_font_path(filename, path))
        return FileNotFound;
    WCHAR upper[MAX_PATH];
    SIZE_T n = 0;
    for (; path[n] && n < MAX_PATH - 1; n++)
        upper[n] = path[n] >= 'a' && path[n] <= 'z' ? (WCHAR)(path[n] - 32) : path[n];
    upper[n] = 0;
    text_lock();
    for (struct GBlob *b = c->blobs; b; b = b->next) {
        if (b->path[0] && path_equal(b->path, upper)) {
            text_unlock();
            return Ok;
        }
    }
    SIZE_T size = 0;
    BYTE *data = read_whole_file(path, &size);
    GpStatus status = FileNotFound;
    if (data)
        status = collection_add_blob(c, data, size, path);
    text_unlock();
    return status;
}

WINGDIPAPI GpStatus WINAPI GdipPrivateAddMemoryFont(GpFontCollection *collection, const void *memory, INT length)
{
    GCollection *c = GP_CAST(GCollection, GP_TAG_COLLECTION, collection);
    if (!memory || !c || c->installed || length <= 0)
        return InvalidParameter;
    BYTE *copy = gp_dup(memory, (SIZE_T)length);
    if (!copy)
        return OutOfMemory;
    text_lock();
    GpStatus status = collection_add_blob(c, copy, (SIZE_T)length, nullptr);
    text_unlock();
    return status;
}
