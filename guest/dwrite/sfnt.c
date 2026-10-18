/*
 * what the tables of a font file say: its table directory, names, style,
 * metrics and coverage.
 */
#include "dw_private.h"

/* ------------------------------------------------------------------ opening */

HRESULT DwFontDataOpen(IDWriteFontFile *file, UINT32 face_index, DwFontData **out)
{
    IDWriteFontFileStream *stream;
    HRESULT hr = DwFontFileOpen(file, &stream);
    if (FAILED(hr))
        return hr;
    UINT64 size = 0;
    const void *start = nullptr;
    void *context = nullptr;
    hr = stream->lpVtbl->GetFileSize(stream, &size);
    if (SUCCEEDED(hr))
        hr = stream->lpVtbl->ReadFileFragment(stream, &start, 0, size, &context);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        return hr;
    }
    DwFontData *d = DwAllocZero(sizeof(*d));
    if (!d) {
        stream->lpVtbl->ReleaseFileFragment(stream, context);
        stream->lpVtbl->Release(stream);
        return E_OUTOFMEMORY;
    }
    d->refs = 1;
    d->stream = stream;
    d->bytes = start;
    d->size = size;
    d->fragment = context;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    if (!start || !DwSniff(start, size, &file_type, &face_type, &d->face_count) || face_index >= d->face_count) {
        DwFontDataRelease(d);
        return DWRITE_E_FILEFORMAT;
    }
    d->collection = file_type == DWRITE_FONT_FILE_TYPE_TRUETYPE_COLLECTION;
    d->cff = file_type == DWRITE_FONT_FILE_TYPE_CFF;
    d->face_index = face_index;
    d->sfnt_offset = d->collection ? DwBe32(d->bytes + 12 + 4ull * face_index) : 0;
    if (d->sfnt_offset > size || size - d->sfnt_offset < 12) {
        DwFontDataRelease(d);
        return DWRITE_E_FILEFORMAT;
    }
    d->num_tables = DwBe16(d->bytes + d->sfnt_offset + 4);
    if (size - d->sfnt_offset - 12 < 16ull * d->num_tables) {
        DwFontDataRelease(d);
        return DWRITE_E_FILEFORMAT;
    }
    d->directory = d->bytes + d->sfnt_offset + 12;
    d->ttf_ok = mime_ttf_open(&d->ttf, d->bytes, (size_t)size, face_index);
    if (!d->ttf_ok) {
        DwFontDataRelease(d);
        return DWRITE_E_FILEFORMAT;
    }
    mime_ttf_metrics(&d->ttf, &d->metrics);
    *out = d;
    return S_OK;
}

void DwFontDataAddRef(DwFontData *d) { InterlockedIncrement(&d->refs); }

void DwFontDataRelease(DwFontData *d)
{
    if (!d || InterlockedDecrement(&d->refs))
        return;
    if (d->stream) {
        if (d->bytes || d->fragment)
            d->stream->lpVtbl->ReleaseFileFragment(d->stream, d->fragment);
        d->stream->lpVtbl->Release(d->stream);
    }
    DwFree(d);
}

const BYTE *DwFontDataTable(const DwFontData *d, UINT32 tag, UINT32 *size)
{
    for (UINT32 i = 0; i < d->num_tables; i++) {
        const BYTE *record = d->directory + 16ull * i;
        if (DwBe32(record) != tag)
            continue;
        UINT32 offset = DwBe32(record + 8), length = DwBe32(record + 12);
        if (offset > d->size || length > d->size - offset)
            return nullptr;
        if (size)
            *size = length;
        return d->bytes + offset;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ style */

void DwFontDataAttributes(const DwFontData *d, DwAttributes *a)
{
    UINT32 os2_size = 0, head_size = 0, post_size = 0;
    const BYTE *os2 = DwFontDataTable(d, DW_TAG('O', 'S', '/', '2'), &os2_size);
    const BYTE *head = DwFontDataTable(d, DW_TAG('h', 'e', 'a', 'd'), &head_size);
    const BYTE *post = DwFontDataTable(d, DW_TAG('p', 'o', 's', 't'), &post_size);
    UINT32 weight = 0, width = 5, selection = 0, mac = head && head_size >= 46 ? DwBe16(head + 44) : 0;
    if (os2 && os2_size >= 64) {
        weight = DwBe16(os2 + 4);
        width = DwBe16(os2 + 6);
        selection = DwBe16(os2 + 62);
    }
    if (weight >= 1 && weight <= 9)
        weight *= 100;
    if (!weight)
        weight = (mac & 1) || (selection & 0x20) ? 700 : 400;
    if (weight > 999)
        weight = 999;
    a->weight = (DWRITE_FONT_WEIGHT)weight;
    a->stretch = width >= 1 && width <= 9 ? (DWRITE_FONT_STRETCH)width : DWRITE_FONT_STRETCH_NORMAL;
    if ((selection & 1) || (mac & 2))
        a->style = DWRITE_FONT_STYLE_ITALIC;
    else if ((selection & 0x200) || (post && post_size >= 8 && DwBe32(post + 4)))
        a->style = DWRITE_FONT_STYLE_OBLIQUE;
    else
        a->style = DWRITE_FONT_STYLE_NORMAL;
    a->symbol = d->ttf.cmap_symbol;
    a->monospaced = post && post_size >= 16 && DwBe32(post + 12) != 0;
}

/* ------------------------------------------------------------------ names */

static BOOL mac_roman_ascii(const BYTE *p, UINT32 length, WCHAR *out)
{
    for (UINT32 i = 0; i < length; i++) {
        if (p[i] >= 0x80)
            return FALSE;
        out[i] = p[i];
    }
    return TRUE;
}

DwStrings *DwFontDataNames(const DwFontData *d, UINT32 name_id)
{
    UINT32 size = 0;
    const BYTE *name = DwFontDataTable(d, DW_TAG('n', 'a', 'm', 'e'), &size);
    if (!name || size < 6)
        return nullptr;
    UINT32 count = DwBe16(name + 2), strings = DwBe16(name + 4);
    if (strings > size || size - 6 < 12ull * count)
        return nullptr;
    DwStrings *result = DwStringsNew();
    if (!result)
        return nullptr;
    /* the windows records first, so that the macintosh ones only fill the languages they add. */
    for (int pass = 0; pass < 2; pass++) {
        for (UINT32 i = 0; i < count; i++) {
            const BYTE *r = name + 6 + 12ull * i;
            UINT32 platform = DwBe16(r), encoding = DwBe16(r + 2), language = DwBe16(r + 4);
            UINT32 id = DwBe16(r + 6), length = DwBe16(r + 8), offset = DwBe16(r + 10);
            if (id != name_id || length > size || offset > size - strings || strings + offset > size - length)
                continue;
            const BYTE *text = name + strings + offset;
            WCHAR locale[LOCALE_NAME_MAX_LENGTH];
            WCHAR *buffer;
            UINT32 chars;
            if (pass == 0 && (platform == 3 || platform == 0)) {
                if (platform == 3 && encoding != 0 && encoding != 1 && encoding != 10)
                    continue;
                if (platform == 3) {
                    if (language >= 0x8000 || !LCIDToLocaleName(language, locale, LOCALE_NAME_MAX_LENGTH, 0))
                        continue;
                } else {
                    wcscpy(locale, L"en-us");
                }
                chars = length / 2;
                buffer = DwAlloc((chars + 1) * sizeof(WCHAR));
                if (!buffer)
                    continue;
                for (UINT32 c = 0; c < chars; c++)
                    buffer[c] = DwBe16(text + 2 * c);
            } else if (pass == 1 && platform == 1 && encoding == 0 && language == 0) {
                wcscpy(locale, L"en-us");
                chars = length;
                buffer = DwAlloc((chars + 1) * sizeof(WCHAR));
                if (!buffer || !mac_roman_ascii(text, length, buffer)) {
                    DwFree(buffer);
                    continue;
                }
            } else {
                continue;
            }
            DwStringsAdd(result, locale, (UINT32)wcslen(locale), buffer, chars);
            DwFree(buffer);
        }
    }
    if (!DwStringsCount(result)) {
        DwStringsRelease(result);
        return nullptr;
    }
    return result;
}

/* ------------------------------------------------------------------ metrics */

static INT16 s16(const BYTE *p) { return (INT16)DwBe16(p); }

void DwFontDataMetrics(const DwFontData *d, DWRITE_FONT_METRICS1 *m)
{
    memset(m, 0, sizeof(*m));
    const MimeTtfFont *f = &d->ttf;
    const MimeTtfMetrics *t = &d->metrics;
    m->designUnitsPerEm = f->units_per_em;
    int ascent = t->ascender, descent = -t->descender, gap = t->line_gap;
    if (t->use_typo_metrics && (t->typo_ascender || t->typo_descender)) {
        ascent = t->typo_ascender;
        descent = -t->typo_descender;
        gap = t->typo_line_gap;
    }
    m->ascent = (UINT16)(ascent < 0 ? 0 : ascent);
    m->descent = (UINT16)(descent < 0 ? 0 : descent);
    m->lineGap = (INT16)gap;
    m->capHeight = (UINT16)t->cap_height;
    m->xHeight = (UINT16)t->x_height;
    m->underlinePosition = (INT16)t->underline_position;
    m->underlineThickness = (UINT16)t->underline_thickness;
    m->strikethroughPosition = (INT16)t->strikeout_position;
    m->strikethroughThickness = (UINT16)t->strikeout_size;
    m->glyphBoxLeft = (INT16)t->x_min;
    m->glyphBoxTop = (INT16)t->y_max;
    m->glyphBoxRight = (INT16)t->x_max;
    m->glyphBoxBottom = (INT16)t->y_min;
    if (f->os2.data && f->os2.size >= 26) {
        const BYTE *os2 = f->os2.data;
        m->subscriptSizeX = s16(os2 + 10);
        m->subscriptSizeY = s16(os2 + 12);
        m->subscriptPositionX = s16(os2 + 14);
        m->subscriptPositionY = s16(os2 + 16);
        m->superscriptSizeX = s16(os2 + 18);
        m->superscriptSizeY = s16(os2 + 20);
        m->superscriptPositionX = s16(os2 + 22);
        m->superscriptPositionY = s16(os2 + 24);
    }
    m->hasTypographicMetrics = t->use_typo_metrics;
}

/* the box of a glyph's outline in font units, zero for a glyph without one. */
static BOOL glyph_box(const DwFontData *d, UINT16 glyph, int *x_min, int *y_min, int *x_max, int *y_max)
{
    const MimeTtfFont *f = &d->ttf;
    *x_min = *y_min = *x_max = *y_max = 0;
    if (!f->has_outlines || glyph >= f->num_glyphs)
        return FALSE;
    UINT64 start, end;
    if (f->loca_format == 0) {
        start = 2ull * DwBe16(f->loca.data + 2ull * glyph);
        end = 2ull * DwBe16(f->loca.data + 2ull * glyph + 2);
    } else {
        start = DwBe32(f->loca.data + 4ull * glyph);
        end = DwBe32(f->loca.data + 4ull * glyph + 4);
    }
    if (start >= end || end > f->glyf.size || end - start < 10)
        return FALSE;
    const BYTE *g = f->glyf.data + start;
    *x_min = s16(g + 2);
    *y_min = s16(g + 4);
    *x_max = s16(g + 6);
    *y_max = s16(g + 8);
    return TRUE;
}

void DwFontDataGlyphMetrics(const DwFontData *d, UINT16 glyph, DWRITE_GLYPH_METRICS *m)
{
    int advance, lsb, x_min, y_min, x_max, y_max;
    mime_ttf_glyph_hmetrics(&d->ttf, glyph, &advance, &lsb);
    glyph_box(d, glyph, &x_min, &y_min, &x_max, &y_max);
    DWRITE_FONT_METRICS1 fm;
    DwFontDataMetrics(d, &fm);
    int origin = fm.ascent, height = fm.ascent + fm.descent;
    m->advanceWidth = (UINT32)advance;
    m->leftSideBearing = lsb;
    m->rightSideBearing = advance - lsb - (x_max - x_min);
    m->advanceHeight = (UINT32)height;
    m->verticalOriginY = origin;
    m->topSideBearing = origin - y_max;
    m->bottomSideBearing = height - origin + y_min;
}

BOOL DwFontDataHasCharacter(const DwFontData *d, UINT32 code_point)
{
    return mime_ttf_glyph_index(&d->ttf, code_point) != 0;
}

/* ------------------------------------------------------------------ coverage */

typedef struct Ranges {
    DWRITE_UNICODE_RANGE *out;
    UINT32 max, count;
    UINT32 previous_last;
} Ranges;

/* ranges that touch merge, and the count is the number there are even past the room given. */
static void add_range(Ranges *r, UINT32 first, UINT32 last)
{
    if (r->count && first == r->previous_last + 1) {
        if (r->out && r->count <= r->max)
            r->out[r->count - 1].last = last;
        r->previous_last = last;
        return;
    }
    if (r->out && r->count < r->max) {
        r->out[r->count].first = first;
        r->out[r->count].last = last;
    }
    r->count++;
    r->previous_last = last;
}

UINT32 DwFontDataRanges(const DwFontData *d, DWRITE_UNICODE_RANGE *out, UINT32 max)
{
    const MimeTtfFont *f = &d->ttf;
    Ranges r = {out, max, 0, 0};
    const BYTE *s = f->cmap_sub.data;
    UINT32 size = f->cmap_sub.size;
    if (!s)
        return 0;
    switch (f->cmap_format) {
    case 4: {
        if (size < 16)
            break;
        UINT32 segs = DwBe16(s + 6) / 2;
        if (size < 16 + 8ull * segs)
            break;
        for (UINT32 i = 0; i < segs; i++) {
            UINT32 end = DwBe16(s + 14 + 2ull * i), start = DwBe16(s + 16 + 2ull * segs + 2ull * i);
            if (start > end || start == 0xffff)
                continue;
            add_range(&r, start, end);
        }
        break;
    }
    case 12:
    case 13: {
        if (size < 16)
            break;
        UINT32 groups = DwBe32(s + 12);
        if (groups > (size - 16) / 12)
            groups = (size - 16) / 12;
        for (UINT32 i = 0; i < groups; i++) {
            UINT32 start = DwBe32(s + 16 + 12ull * i), end = DwBe32(s + 20 + 12ull * i);
            if (start <= end && end <= 0x10ffff)
                add_range(&r, start, end);
        }
        break;
    }
    case 6: {
        if (size < 10)
            break;
        UINT32 first = DwBe16(s + 6), n = DwBe16(s + 8);
        if (n)
            add_range(&r, first, first + n - 1);
        break;
    }
    case 0:
        add_range(&r, 0, 255);
        break;
    default:
        break;
    }
    return r.count;
}
