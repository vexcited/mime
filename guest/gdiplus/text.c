/*
 * the calls that draw and measure text: strings in a rectangle, ranges of their characters, strings of glyphs,
 * and text as a path.
 */
#include "text_private.h"

#define GFX_PREAMBLE(g, p)              \
    GGraphics *g = gp_graphics_cast(p); \
    if (!g)                             \
        return InvalidParameter;        \
    if (g->got_dc)                      \
        return ObjectBusy

/* the length of a string argument: a negative length means up to the terminator. */
static INT string_length(const WCHAR *s, INT length) { return length < 0 ? (INT)lstrlenW(s) : length; }

static MimeVecPoint map_point(const MimeVecMatrix *m, float x, float y)
{
    MimeVecPoint p = {x * m->m11 + y * m->m21 + m->dx, x * m->m12 + y * m->m22 + m->dy};
    return p;
}

static MimeVecPoint map_vector(const MimeVecMatrix *m, float x, float y)
{
    MimeVecPoint p = {x * m->m11 + y * m->m21, x * m->m12 + y * m->m22};
    return p;
}

static BOOL hint_snaps(TextRenderingHint hint)
{
    return hint == TextRenderingHintSingleBitPerPixelGridFit || hint == TextRenderingHintAntiAliasGridFit ||
           hint == TextRenderingHintClearTypeGridFit;
}

static BOOL hint_is_one_bit(TextRenderingHint hint)
{
    return hint == TextRenderingHintSingleBitPerPixelGridFit || hint == TextRenderingHintSingleBitPerPixel;
}

/* clear type needs the destination under it, which a replacing composition does not keep. */
static BOOL text_mode_refused(const GGraphics *g)
{
    return g->compositing_mode == CompositingModeSourceCopy &&
           gp_text_resolved_hint(g) == TextRenderingHintClearTypeGridFit;
}

/* ------------------------------------------------------------------ what a call is given */

typedef struct TextCall {
    GGraphics *g;
    const GFont *font;
    GFamily *family;
    const GStrFmt *format;
    GTextStyle style;
    GTextInput in;
} TextCall;

static GpStatus call_font(TextCall *t, const GFont *font)
{
    t->font = font;
    t->family = gp_family_resolve(font->family);
    return t->family ? Ok : FontFamilyNotFound;
}

static void call_input(TextCall *t, const WCHAR *text, INT length, const RectF *r)
{
    GTextInput *in = &t->in;
    memset(in, 0, sizeof(*in));
    in->text = text;
    in->length = length;
    in->family = t->family;
    in->style = t->font->style;
    in->size = gp_font_world_size(t->font->size, t->font->unit, t->g);
    in->format = t->format;
    in->x = r->X;
    in->y = r->Y;
    in->w = r->Width;
    in->h = r->Height;
    in->has_matrix = TRUE;
    in->w2d = *gp_graphics_w2d(t->g);
}

/* which of the styles are made up because the family has no face for them. */
static void call_style(GTextStyle *s, INT style, INT slot)
{
    s->simulate_bold = (style & FontStyleBold) && !(slot & 1);
    s->simulate_italic = (style & FontStyleItalic) && !(slot & 2);
    s->underline = (style & FontStyleUnderline) != 0;
    s->strikeout = (style & FontStyleStrikeout) != 0;
}

/* a format argument: NULL is none, anything else has to be a string format. */
static GpStatus format_arg(const GpStringFormat *format, const GStrFmt **out)
{
    *out = nullptr;
    if (!format)
        return Ok;
    *out = gp_strfmt_cast(format);
    return *out ? Ok : InvalidParameter;
}

/* ------------------------------------------------------------------ filling outlines */

/* glyph curves are cut into lines finer than the shapes of the drawing calls, which show every corner of a small glyph. */
#define TEXT_FLATNESS 0.1f

/* outlines in device space, drawn the way text is: the rasterization of the hint, the contrast of the graphics. */
static GpStatus fill_outlines(GGraphics *g, const MimeVecPath *path, const GBrush *brush, const RectF *clip_rect)
{
    TextRenderingHint hint = gp_text_resolved_hint(g);
    BOOL one_bit = hint_is_one_bit(hint);
    const UINT8 *map = one_bit ? nullptr : gp_text_contrast_map(g->text_contrast);
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    int e = mime_vec_path_flatten(path, nullptr, TEXT_FLATNESS, &polys);
    GpStatus status = e == MIME_VEC_OK ? Ok : gp_path_status(e);
    MimeVecMask clip;
    mime_vec_mask_init(&clip, &gp_allocator);
    BOOL have_clip = FALSE;
    if (status == Ok && clip_rect) {
        /* the rectangle text is cut to goes through the transform the glyphs went through. */
        const MimeVecMatrix *m = gp_graphics_w2d(g);
        MimeVecPoint corners[4] = {
            map_point(m, clip_rect->X, clip_rect->Y),
            map_point(m, clip_rect->X + clip_rect->Width, clip_rect->Y),
            map_point(m, clip_rect->X + clip_rect->Width, clip_rect->Y + clip_rect->Height),
            map_point(m, clip_rect->X, clip_rect->Y + clip_rect->Height),
        };
        MimeVecPolys box;
        mime_vec_polys_init(&box, &gp_allocator);
        RECT bounds;
        gp_graphics_bounds(g, &bounds);
        MimeVecRect area = {bounds.left, bounds.top, bounds.right, bounds.bottom};
        int be = mime_vec_polys_add_sub(&box, corners, 4, true);
        status = be == MIME_VEC_OK ? gp_mask_from_polys(&clip, &box, MIME_VEC_FILL_WINDING, &area) : gp_path_status(be);
        mime_vec_polys_free(&box);
        have_clip = TRUE;
    }
    if (status == Ok && polys.nsubs)
        status = gp_fill_polys_text(g, &polys, one_bit ? 0 : 1, brush, map, have_clip ? &clip : nullptr);
    mime_vec_mask_free(&clip);
    mime_vec_polys_free(&polys);
    return status;
}

/* ------------------------------------------------------------------ strings in a rectangle */

WINGDIPAPI GpStatus WINAPI GdipDrawString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font,
                                          const RectF *rect, const GpStringFormat *format, const GpBrush *brush)
{
    if (!string || !rect)
        return InvalidParameter;
    GFX_PREAMBLE(g, graphics);
    const GBrush *b = gp_brush_cast(brush);
    if (!b || length < -1)
        return InvalidParameter;
    INT len = string_length(string, length);
    if (len == 0)
        return Ok;
    const GFont *f = gp_font_cast(font);
    TextCall t;
    memset(&t, 0, sizeof(t));
    t.g = g;
    if (!f || format_arg(format, &t.format) != Ok)
        return InvalidParameter;
    GpStatus status = call_font(&t, f);
    if (status != Ok)
        return status;
    if (text_mode_refused(g))
        return InvalidParameter;
    BOOL no_clip = t.format && (t.format->flags & StringFormatFlagsNoClip);
    if (!no_clip && rect->Width != 0.0f && rect->Height != 0.0f && (rect->Width < 0.0f || rect->Height < 0.0f))
        return Ok;

    call_input(&t, string, len, rect);
    GTextLayout layout;
    status = gp_text_layout(&t.in, &layout);
    if (status != Ok)
        return status;
    call_style(&t.style, f->style, layout.slot);

    MimeVecPath path;
    mime_vec_path_init(&path, &gp_allocator);
    MimeVecMatrix to_target;
    mime_vec_matrix_identity(&to_target);
    BOOL recording = g->meta != nullptr;
    if (!recording)
        to_target = *gp_graphics_w2d(g);
    BOOL snap = !recording && hint_snaps(gp_text_resolved_hint(g)) && !layout.vertical && to_target.m12 == 0.0f &&
                to_target.m21 == 0.0f;
    status = gp_text_emit(&layout, &t.style, &to_target, snap, &path);
    if (status == Ok && path.count) {
        if (recording) {
            status = gp_fill_path(g, &path, FillModeWinding, b);
        } else {
            BOOL clipped = !no_clip && rect->Width != 0.0f && rect->Height != 0.0f;
            status = fill_outlines(g, &path, b, clipped ? rect : nullptr);
        }
    }
    mime_vec_path_free(&path);
    gp_text_layout_free(&layout);
    return status;
}

WINGDIPAPI GpStatus WINAPI GdipMeasureString(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font,
                                             const RectF *layout_rect, const GpStringFormat *format, RectF *bounding_box,
                                             INT *codepoints_fitted, INT *lines_filled)
{
    if (!string || !layout_rect || !bounding_box)
        return InvalidParameter;
    GFX_PREAMBLE(g, graphics);
    if (length < -1)
        return InvalidParameter;
    INT len = string_length(string, length);
    if (len == 0) {
        bounding_box->X = bounding_box->Y = bounding_box->Width = bounding_box->Height = 0.0f;
        if (codepoints_fitted)
            *codepoints_fitted = 0;
        if (lines_filled)
            *lines_filled = 0;
        return Ok;
    }
    const GFont *f = gp_font_cast(font);
    TextCall t;
    memset(&t, 0, sizeof(t));
    t.g = g;
    if (!f || format_arg(format, &t.format) != Ok)
        return InvalidParameter;
    GpStatus status = call_font(&t, f);
    if (status != Ok)
        return status;
    call_input(&t, string, len, layout_rect);
    GTextLayout layout;
    status = gp_text_layout(&t.in, &layout);
    if (status != Ok)
        return status;
    gp_text_layout_box(&layout, bounding_box);
    if (codepoints_fitted)
        *codepoints_fitted = layout.fitted;
    if (lines_filled)
        *lines_filled = layout.line_count;
    gp_text_layout_free(&layout);
    return Ok;
}

/* ------------------------------------------------------------------ ranges of characters */

/* the rectangles of the glyphs of a line that belong to a range of characters, touching ones joined. */
static void line_range_rects(const GTextLayout *L, const GTextLine *line, INT range_begin, INT range_end,
                             RectF *out, INT *count, INT capacity)
{
    float start = 0.0f, end = 0.0f;
    BOOL open = FALSE;
    for (INT i = 0; i <= line->count; i++) {
        const GTextGlyph *g = i < line->count ? &L->glyphs[line->first + i] : nullptr;
        BOOL inside = g && g->src_begin >= 0 && g->src_begin < range_end && g->src_end > range_begin;
        if (inside && open && g->x - end < 0.001f && end - g->x < 0.001f) {
            end = g->x + g->advance;
            continue;
        }
        if (open) {
            if (*count < capacity)
                gp_text_span_rect(L, line, start, end, &out[(*count)++]);
            open = FALSE;
        }
        if (inside) {
            start = g->x;
            end = g->x + g->advance;
            open = TRUE;
        }
    }
}

static BOOL rect_intersect(const RectF *a, const RectF *b, RectF *out)
{
    float x0 = a->X > b->X ? a->X : b->X;
    float y0 = a->Y > b->Y ? a->Y : b->Y;
    float x1 = a->X + a->Width < b->X + b->Width ? a->X + a->Width : b->X + b->Width;
    float y1 = a->Y + a->Height < b->Y + b->Height ? a->Y + a->Height : b->Y + b->Height;
    if (x1 <= x0 || y1 <= y0)
        return FALSE;
    out->X = x0;
    out->Y = y0;
    out->Width = x1 - x0;
    out->Height = y1 - y0;
    return TRUE;
}

WINGDIPAPI GpStatus WINAPI GdipMeasureCharacterRanges(GpGraphics *graphics, const WCHAR *string, INT length,
                                                      const GpFont *font, const RectF *layout_rect,
                                                      const GpStringFormat *format, INT region_count,
                                                      GpRegion **regions)
{
    if (!string || !layout_rect)
        return InvalidParameter;
    GFX_PREAMBLE(g, graphics);
    const GStrFmt *fmt = gp_strfmt_cast(format);
    if (!fmt || !regions || length < -1)
        return InvalidParameter;
    INT len = string_length(string, length);
    if (len == 0)
        return InvalidParameter;
    const GFont *f = gp_font_cast(font);
    if (!f || region_count != fmt->range_count)
        return InvalidParameter;
    TextCall t;
    memset(&t, 0, sizeof(t));
    t.g = g;
    t.format = fmt;
    GpStatus status = call_font(&t, f);
    if (status != Ok)
        return status;
    for (INT i = 0; i < region_count; i++) {
        const CharacterRange *r = &fmt->ranges[i];
        INT begin = r->Length < 0 ? r->First + r->Length : r->First;
        INT end = r->Length < 0 ? r->First : r->First + r->Length;
        if (!gp_region_cast(regions[i]) || r->First < 0 || r->First > len || begin < 0 || end > len)
            return InvalidParameter;
    }
    if (region_count == 0)
        return Ok;

    call_input(&t, string, len, layout_rect);
    t.in.force_general = TRUE;
    GTextLayout layout;
    status = gp_text_layout(&t.in, &layout);
    if (status != Ok)
        return status;
    BOOL clip = !layout.no_clip;
    for (INT i = 0; i < region_count && status == Ok; i++) {
        const CharacterRange *r = &fmt->ranges[i];
        INT begin = r->Length < 0 ? r->First + r->Length : r->First;
        INT end = r->Length < 0 ? r->First : r->First + r->Length;
        GpRegion *region = regions[i];
        status = GdipSetEmpty(region);
        for (INT l = 0; l < layout.line_count && status == Ok && begin < end; l++) {
            RectF rects[32];
            INT count = 0;
            line_range_rects(&layout, &layout.lines[l], begin, end, rects, &count, 32);
            for (INT k = 0; k < count && status == Ok; k++) {
                RectF cell = rects[k];
                if (clip && !rect_intersect(&cell, layout_rect, &cell))
                    continue;
                status = GdipCombineRegionRect(region, (const GpRectF *)&cell, CombineModeUnion);
            }
        }
    }
    gp_text_layout_free(&layout);
    return status;
}

/* ------------------------------------------------------------------ strings of glyphs */

typedef struct DriverRun {
    GFamily *family;
    const GFace *face;
    INT slot;
    float size;
    INT count;
    UINT16 *glyphs;
    MimeVecPoint *origins;
} DriverRun;

static void driver_run_free(DriverRun *r)
{
    gp_free(r->glyphs);
    gp_free(r->origins);
}

/* glyph indices and the places they go, in the world of the graphics. */
static GpStatus driver_run_make(DriverRun *r, GGraphics *g, const GFont *font, const UINT16 *text, INT length,
                                const PointF *positions, INT flags)
{
    memset(r, 0, sizeof(*r));
    r->family = gp_family_resolve(font->family);
    if (!r->family)
        return FontFamilyNotFound;
    r->face = gp_family_face(r->family, font->style, &r->slot);
    if (!r->face)
        return FontStyleNotFound;
    r->size = gp_font_world_size(font->size, font->unit, g);
    r->count = length;
    r->glyphs = gp_alloc(sizeof(UINT16) * (SIZE_T)length);
    r->origins = gp_alloc(sizeof(MimeVecPoint) * (SIZE_T)length);
    if (!r->glyphs || !r->origins) {
        driver_run_free(r);
        return OutOfMemory;
    }
    float k = r->size / (float)r->face->em;
    BOOL vertical = (flags & DriverStringOptionsVertical) != 0;
    MimeVecPoint pen = {positions[0].X, positions[0].Y};
    for (INT i = 0; i < length; i++) {
        r->glyphs[i] = (flags & DriverStringOptionsCmapLookup) ? gp_face_glyph(r->face, text[i]) : text[i];
        if (flags & DriverStringOptionsRealizedAdvance) {
            r->origins[i] = pen;
            float advance = (float)gp_face_advance(r->face, r->glyphs[i]) * k;
            if (vertical)
                pen.y += advance;
            else
                pen.x += advance;
        } else {
            r->origins[i].x = positions[i].X;
            r->origins[i].y = positions[i].Y;
        }
    }
    return Ok;
}

/* the graphics and the optional matrix both glyph string calls start with. */
static GpStatus driver_graphics(GpGraphics *graphics, const GpMatrix *matrix, GGraphics **g, const GMatrix **m)
{
    *g = gp_graphics_cast(graphics);
    if (!*g)
        return InvalidParameter;
    if ((*g)->got_dc)
        return ObjectBusy;
    *m = nullptr;
    if (matrix) {
        *m = gp_matrix_cast(matrix);
        if (!*m)
            return InvalidParameter;
    }
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDrawDriverString(GpGraphics *graphics, const UINT16 *text, INT length, const GpFont *font,
                                                const GpBrush *brush, const PointF *positions, INT flags,
                                                const GpMatrix *matrix)
{
    GGraphics *g;
    const GMatrix *gm;
    GpStatus status = driver_graphics(graphics, matrix, &g, &gm);
    if (status != Ok)
        return status;
    const GBrush *b = gp_brush_cast(brush);
    if (!b || !text || !positions)
        return InvalidParameter;
    if (length == -1) {
        if (!(flags & DriverStringOptionsCmapLookup))
            return InvalidParameter;
        length = 0;
        while (text[length])
            length++;
    }
    if (length < 0)
        return InvalidParameter;
    if (length == 0)
        return Ok;
    const GFont *f = gp_font_cast(font);
    if (!f || (f->style & (FontStyleUnderline | FontStyleStrikeout)))
        return InvalidParameter;
    if (text_mode_refused(g))
        return InvalidParameter;

    DriverRun run;
    status = driver_run_make(&run, g, f, text, length, positions, flags);
    if (status != Ok)
        return status;
    GTextStyle style;
    call_style(&style, f->style, run.slot);

    /* the matrix of the call moves the glyphs in the world, then the world goes to the device */
    MimeVecMatrix total;
    BOOL recording = g->meta != nullptr;
    if (recording)
        mime_vec_matrix_identity(&total);
    else
        total = *gp_graphics_w2d(g);
    if (gm) {
        if (recording)
            total = gm->m;
        else
            mime_vec_matrix_multiply(&total, &gm->m, gp_graphics_w2d(g));
    }
    BOOL snap = !recording && hint_snaps(gp_text_resolved_hint(g)) && total.m12 == 0.0f && total.m21 == 0.0f;

    MimeVecPath path;
    mime_vec_path_init(&path, &gp_allocator);
    float k = run.size / (float)run.face->em;
    for (INT i = 0; i < length && status == Ok; i++) {
        MimeVecPoint o = map_point(&total, run.origins[i].x, run.origins[i].y);
        MimeVecPoint u = map_vector(&total, k, 0.0f);
        MimeVecPoint v = map_vector(&total, 0.0f, -k);
        if (style.simulate_italic) {
            v.x += 0.2126f * u.x;
            v.y += 0.2126f * u.y;
        }
        if (snap) {
            o.x = mime_vec_floorf(o.x + 0.5f);
            o.y = mime_vec_floorf(o.y + 0.5f);
        }
        status = gp_face_outline(run.face, run.glyphs[i], &path, o.x, o.y, u.x, u.y, v.x, v.y);
        if (status == Ok && style.simulate_bold) {
            float shift = (float)run.face->em / 32.0f;
            status = gp_face_outline(run.face, run.glyphs[i], &path, o.x + u.x * shift, o.y + u.y * shift, u.x, u.y,
                                     v.x, v.y);
        }
    }
    if (status == Ok && path.count)
        status = recording ? gp_fill_path(g, &path, FillModeWinding, b) : fill_outlines(g, &path, b, nullptr);
    mime_vec_path_free(&path);
    driver_run_free(&run);
    return status;
}

WINGDIPAPI GpStatus WINAPI GdipMeasureDriverString(GpGraphics *graphics, const UINT16 *text, INT length,
                                                   const GpFont *font, const PointF *positions, INT flags,
                                                   const GpMatrix *matrix, RectF *bounding_box)
{
    GGraphics *g;
    const GMatrix *gm;
    GpStatus status = driver_graphics(graphics, matrix, &g, &gm);
    if (status != Ok)
        return status;
    if (!bounding_box || !text || !positions)
        return InvalidParameter;
    if (length == -1) {
        if (!(flags & DriverStringOptionsCmapLookup))
            return InvalidParameter;
        length = 0;
        while (text[length])
            length++;
    }
    if (length < 0)
        return InvalidParameter;
    if (length == 0) {
        bounding_box->X = bounding_box->Y = bounding_box->Width = bounding_box->Height = 0.0f;
        return Ok;
    }
    const GFont *f = gp_font_cast(font);
    if (!f)
        return InvalidParameter;
    DriverRun run;
    status = driver_run_make(&run, g, f, text, length, positions, flags);
    if (status != Ok)
        return status;
    float k = run.size / (float)run.face->em;
    float ascent = (float)run.face->ascent * k, descent = (float)run.face->descent * k;
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    BOOL moved = gm && (flags & DriverStringOptionsRealizedAdvance);
    for (INT i = 0; i < length; i++) {
        float advance = (float)gp_face_advance(run.face, run.glyphs[i]) * k;
        float x0 = run.origins[i].x, y0 = run.origins[i].y - ascent;
        float x1 = x0 + advance, y1 = run.origins[i].y + descent;
        MimeVecPoint corners[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        for (INT c = 0; c < 4; c++) {
            MimeVecPoint p = moved ? map_point(&gm->m, corners[c].x, corners[c].y) : corners[c];
            if (i == 0 && c == 0) {
                left = right = p.x;
                top = bottom = p.y;
            }
            if (p.x < left)
                left = p.x;
            if (p.x > right)
                right = p.x;
            if (p.y < top)
                top = p.y;
            if (p.y > bottom)
                bottom = p.y;
        }
    }
    bounding_box->X = left;
    bounding_box->Y = top;
    bounding_box->Width = right - left;
    bounding_box->Height = bottom - top;
    driver_run_free(&run);
    return Ok;
}

/* ------------------------------------------------------------------ text as a path */

static GpStatus add_path_string(GpPath *path, const WCHAR *string, INT length, const GpFontFamily *family, INT style,
                                REAL em_size, const RectF *rect, const GpStringFormat *format)
{
    GPath *p = gp_path_cast(path);
    if (!p || !string || !family || !rect)
        return InvalidParameter;
    if (length < -1)
        return InvalidParameter;
    const GStrFmt *fmt;
    if (format_arg(format, &fmt) != Ok)
        return InvalidParameter;
    GFamily *resolved = gp_family_resolve(family);
    if (!resolved)
        return FontFamilyNotFound;
    INT len = string_length(string, length);
    if (len == 0)
        return Ok;
    if (!(em_size > 0.0f))
        return InvalidParameter;

    GTextInput in;
    memset(&in, 0, sizeof(in));
    in.text = string;
    in.length = len;
    in.family = resolved;
    in.style = style;
    in.size = em_size;
    in.format = fmt;
    in.x = rect->X;
    in.y = rect->Y;
    in.w = rect->Width;
    in.h = rect->Height;
    in.has_matrix = TRUE;
    mime_vec_matrix_identity(&in.w2d);
    GTextLayout layout;
    GpStatus status = gp_text_layout(&in, &layout);
    if (status != Ok)
        return status;
    GTextStyle text_style;
    call_style(&text_style, style, layout.slot);
    MimeVecPath outlines;
    mime_vec_path_init(&outlines, &gp_allocator);
    MimeVecMatrix identity;
    mime_vec_matrix_identity(&identity);
    status = gp_text_emit(&layout, &text_style, &identity, FALSE, &outlines);
    if (status == Ok && outlines.count)
        status = gp_path_status(mime_vec_path_append(&p->path, &outlines, false));
    mime_vec_path_free(&outlines);
    gp_text_layout_free(&layout);
    return status;
}

WINGDIPAPI GpStatus WINAPI GdipAddPathString(GpPath *path, const WCHAR *string, INT length,
                                             const GpFontFamily *family, INT style, REAL em_size, const RectF *rect,
                                             const GpStringFormat *format)
{
    return add_path_string(path, string, length, family, style, em_size, rect, format);
}

WINGDIPAPI GpStatus WINAPI GdipAddPathStringI(GpPath *path, const WCHAR *string, INT length,
                                              const GpFontFamily *family, INT style, REAL em_size, const Rect *rect,
                                              const GpStringFormat *format)
{
    if (!rect)
        return InvalidParameter;
    RectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    return add_path_string(path, string, length, family, style, em_size, &r, format);
}
