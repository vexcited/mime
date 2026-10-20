/*
 * the glyphs of a layout as outlines: where each glyph, underline and strikeout goes, the table that turns coverage
 * into the alpha of a pixel of text, and the hint a graphics draws text with.
 */
#include "text_private.h"

/* ------------------------------------------------------------------ hints and contrast */

TextRenderingHint gp_text_resolved_hint(const GGraphics *g)
{
    TextRenderingHint hint = g->text_hint;
    if (hint != TextRenderingHintSystemDefault)
        return hint;
    BOOL smoothing = FALSE;
    if (SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) && smoothing) {
        UINT type = 0;
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
        hint = (type & 2) ? TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAliasGridFit;
    } else {
        hint = TextRenderingHintSingleBitPerPixelGridFit;
    }
    /* a target of eight bits or fewer cannot show the gray levels. */
    INT bits = 32;
    if (g->target)
        bits = (INT)((g->target->format >> 8) & 0xff);
    else if (g->hdc)
        bits = GetDeviceCaps(g->hdc, BITSPIXEL) * GetDeviceCaps(g->hdc, PLANES);
    if (bits <= 8)
        hint = TextRenderingHintSingleBitPerPixelGridFit;
    return hint;
}

enum { CONTRAST_ROWS = 12 };

static UINT8 g_contrast[CONTRAST_ROWS][256];
static volatile LONG g_contrast_built;

/*
 * the table of contrast k lifts the dark end of a coverage ramp. Its entry i is 255 times i over 255 raised to the
 * power 1 over 1 plus a tenth of k, rounded, and a pixel of coverage c gets the alpha 255 less the entry 255 - c. The
 * tables are made the first time one is asked for.
 */
const UINT8 *gp_text_contrast_map(UINT contrast)
{
    if (contrast == 0 || contrast > CONTRAST_ROWS)
        return nullptr;
    UINT row = contrast - 1;
    if (!(g_contrast_built & (1L << row))) {
        double exponent = 1.0 / (1.0 + 0.1 * (double)contrast);
        for (INT i = 0; i < 256; i++) {
            double t = 255.0 * gp_pow((double)i / 255.0, exponent) + 0.5;
            INT entry = (INT)t;
            g_contrast[row][255 - i] = (UINT8)(255 - entry);
        }
        InterlockedOr(&g_contrast_built, 1L << row);
    }
    return g_contrast[row];
}

/* ------------------------------------------------------------------ placing glyphs */

/* where a line sits: the pen at its start, and the directions along it and up from its baseline. */
typedef struct Frame {
    float ox, oy;
    float ux, uy;
    float vx, vy;
} Frame;

static void line_frame(const GTextLayout *L, const GTextLine *line, Frame *f)
{
    if (!L->vertical) {
        f->ox = L->x + line->shift + L->lead;
        f->oy = L->y + L->block_shift + line->top + L->ascent;
        f->ux = 1.0f;
        f->uy = 0.0f;
        f->vx = 0.0f;
        f->vy = -1.0f;
        return;
    }
    float x0 = L->right_to_left ? L->x + (L->w - L->text_height - L->block_shift) : L->x + L->block_shift;
    f->ox = x0 + L->text_height - line->top - L->ascent;
    f->oy = L->y + line->shift + L->lead;
    f->ux = 0.0f;
    f->uy = 1.0f;
    f->vx = 1.0f;
    f->vy = 0.0f;
}

void gp_text_span_rect(const GTextLayout *L, const GTextLine *line, float u0, float u1, RectF *out)
{
    Frame f;
    line_frame(L, line, &f);
    float cell = L->ascent + L->descent;
    if (!L->vertical) {
        out->X = f.ox + u0;
        out->Y = f.oy - L->ascent;
        out->Width = u1 - u0;
        out->Height = cell;
    } else {
        out->X = f.ox - L->descent;
        out->Y = f.oy + u0;
        out->Width = cell;
        out->Height = u1 - u0;
    }
}

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

static float round_to_unit(float v) { return mime_vec_floorf(v + 0.5f); }

/* a slanted copy of the up direction, which is how a face with no italic is made to lean. */
#define ITALIC_SHEAR 0.2126f

typedef struct Emit {
    const GTextLayout *layout;
    const GTextStyle *style;
    const MimeVecMatrix *to_target;
    BOOL snap;
    MimeVecPath *path;
} Emit;

static GpStatus emit_glyph(const Emit *e, const Frame *frame, const GTextGlyph *g)
{
    if (!g->face)
        return Ok;
    const GFace *face = g->face;
    float k = e->layout->size / (float)face->em;
    MimeVecPoint o = map_point(e->to_target, frame->ox + frame->ux * g->x, frame->oy + frame->uy * g->x);
    MimeVecPoint u = map_vector(e->to_target, frame->ux * k, frame->uy * k);
    MimeVecPoint v = map_vector(e->to_target, frame->vx * k, frame->vy * k);
    if (e->style->simulate_italic) {
        v.x += ITALIC_SHEAR * u.x;
        v.y += ITALIC_SHEAR * u.y;
    }
    if (e->snap) {
        o.x = round_to_unit(o.x);
        o.y = round_to_unit(o.y);
    }
    GpStatus status = gp_face_outline(face, g->glyph, e->path, o.x, o.y, u.x, u.y, v.x, v.y);
    if (status == Ok && e->style->simulate_bold) {
        /* a second copy a thirty second of the em along the line. */
        float shift = (float)face->em / 32.0f;
        status = gp_face_outline(face, g->glyph, e->path, o.x + u.x * shift, o.y + u.y * shift, u.x, u.y, v.x, v.y);
    }
    return status;
}

/*
 * a bar along the line from gx0 to gx1 with its top gy_top and bottom gy_bottom above the baseline, in font units.
 * The corners run the way the outer contours of a glyph do, so bars and glyphs never cancel where they overlap.
 */
static GpStatus emit_bar(const Emit *e, const Frame *frame, float line_start, float gx0, float gx1, float gy_top,
                         float gy_bottom)
{
    const GFace *face = e->layout->face;
    float k = e->layout->size / (float)face->em;
    MimeVecPoint o = map_point(e->to_target, frame->ox + frame->ux * line_start, frame->oy + frame->uy * line_start);
    MimeVecPoint u = map_vector(e->to_target, frame->ux * k, frame->uy * k);
    MimeVecPoint v = map_vector(e->to_target, frame->vx * k, frame->vy * k);
    MimeVecPoint p[4] = {
        {o.x + gx0 * u.x + gy_top * v.x, o.y + gx0 * u.y + gy_top * v.y},
        {o.x + gx1 * u.x + gy_top * v.x, o.y + gx1 * u.y + gy_top * v.y},
        {o.x + gx1 * u.x + gy_bottom * v.x, o.y + gx1 * u.y + gy_bottom * v.y},
        {o.x + gx0 * u.x + gy_bottom * v.x, o.y + gx0 * u.y + gy_bottom * v.y},
    };
    if (e->snap) {
        for (INT i = 0; i < 4; i++) {
            p[i].x = round_to_unit(p[i].x);
            p[i].y = round_to_unit(p[i].y);
        }
        /* a bar is at least one unit of the target thick. */
        if (p[0].y == p[3].y) {
            p[2].y += 1.0f;
            p[3].y += 1.0f;
        }
    }
    int status = mime_vec_path_move_to(e->path, p[0].x, p[0].y);
    for (INT i = 1; i < 4 && status == MIME_VEC_OK; i++)
        status = mime_vec_path_line_to(e->path, p[i].x, p[i].y);
    if (status == MIME_VEC_OK)
        status = mime_vec_path_close_figure(e->path);
    return gp_path_status(status);
}

GpStatus gp_text_emit(const GTextLayout *layout, const GTextStyle *style, const MimeVecMatrix *to_target, BOOL snap,
                      MimeVecPath *out)
{
    Emit e = {layout, style, to_target, snap, out};
    const GFace *face = layout->face;
    if (!face)
        return Ok;
    float k = layout->size / (float)face->em;
    GpStatus status = Ok;
    for (INT i = 0; i < layout->line_count && status == Ok; i++) {
        const GTextLine *line = &layout->lines[i];
        Frame frame;
        line_frame(layout, line, &frame);
        for (INT j = 0; j < line->count && status == Ok; j++)
            status = emit_glyph(&e, &frame, &layout->glyphs[line->first + j]);
        /* a hotkey is underlined across the advance of its character. */
        float under_top = (float)face->underline_pos;
        float under_bottom = under_top - (float)face->underline_size;
        for (INT j = 0; j < line->count && status == Ok; j++) {
            const GTextGlyph *g = &layout->glyphs[line->first + j];
            if (g->hotkey)
                status = emit_bar(&e, &frame, 0.0f, g->x / k, (g->x + g->advance) / k, under_top, under_bottom);
        }
        if (status == Ok && style->underline && line->width > 0.0f)
            status = emit_bar(&e, &frame, 0.0f, 0.0f, line->width / k, under_top, under_bottom);
        if (status == Ok && style->strikeout && line->width > 0.0f)
            status = emit_bar(&e, &frame, 0.0f, 0.0f, line->width / k, (float)face->strike_pos,
                              (float)(face->strike_pos - face->strike_size));
    }
    return status;
}
